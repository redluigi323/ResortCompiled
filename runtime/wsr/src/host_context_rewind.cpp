#include "host_context_rewind.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <limits>
#include <exception>

#if (defined(__linux__) || defined(_WIN32)) && defined(__x86_64__)
#include <libco.h>
#include <xmmintrin.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#define RESORT_REWIND_CONTEXT_X64 1
#endif

namespace HostContext {
struct SnapshotAccess {
    static Snapshot Make(uint64_t generation, Handle handle, uintptr_t start,
                         const void* registers, const void* stack, size_t size, const std::fenv_t& fp, uint32_t mxcsr) {
        Snapshot result; result.generation_=generation; result.handle_=handle; result.stackStart_=start;
        const auto* bytes=static_cast<const uint8_t*>(registers);
        result.registers_.assign(bytes,bytes+512);
        bytes=static_cast<const uint8_t*>(stack);result.stack_.assign(bytes,bytes+size);
        result.floatingPoint_=fp;result.mxcsr_=mxcsr;return result;
    }
    static uint64_t Generation(const Snapshot& s){return s.generation_;}
    static Handle Owner(const Snapshot& s){return s.handle_;}
    static uintptr_t Start(const Snapshot& s){return s.stackStart_;}
    static const auto& Registers(const Snapshot& s){return s.registers_;}
    static const auto& Stack(const Snapshot& s){return s.stack_;}
    static const auto& FP(const Snapshot& s){return s.floatingPoint_;}
    static uint32_t MXCSR(const Snapshot& s){return s.mxcsr_;}
};
namespace Rewindable {
#if defined(RESORT_REWIND_CONTEXT_X64)
namespace {
struct Context {
    cothread_t native=nullptr;
    void* allocation=nullptr;
    size_t allocationSize=0;
    uintptr_t bottom=0,top=0;
    Entry entry=nullptr;void* argument=nullptr;
    uint64_t generation=0;
    std::fenv_t fp{};
    uint32_t mxcsr=0;
    ~Context() {
        if (!allocation) return;
#if defined(_WIN32)
        VirtualFree(allocation,0,MEM_RELEASE);
#else
        munmap(allocation,allocationSize);
#endif
    }
#if defined(_WIN32)
    void* stackBase=nullptr;void* stackLimit=nullptr;
#endif
};
thread_local Context* current=nullptr;
thread_local Context* scheduler=nullptr;
thread_local std::unordered_map<Handle,std::unique_ptr<Context>> contexts;
thread_local uint64_t nextGeneration=1;
Context& Lookup(Handle handle) {
    const auto found=contexts.find(handle);
    if(found==contexts.end())throw std::invalid_argument("Context is not live on this host thread");
    return *found->second;
}
void EntryPoint() {
    Context& context=Lookup(current);
    context.entry(context.argument);
    // Guest FiberProc must yield after exit; returning has no native caller.
    std::terminate();
}
void Release(Context& context) {
    if(!context.allocation)return;
#if defined(_WIN32)
    VirtualFree(context.allocation,0,MEM_RELEASE);
#else
    munmap(context.allocation,context.allocationSize);
#endif
    context.allocation=nullptr;
}
void Suspended(Context& context) {
    if(!context.allocation || &context==current || current!=scheduler)
        throw std::logic_error("Continuation checkpoint requires a suspended worker and scheduler boundary");
}
}
bool InitializeScheduler(Handle* out) {
    if(!out || scheduler || !contexts.empty() || !co_serializable())return false;
    auto context=std::make_unique<Context>();context->native=co_active();
    context->generation=nextGeneration++;std::fegetenv(&context->fp);context->mxcsr=_mm_getcsr();
#if defined(_WIN32)
    context->stackBase=reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase;
    context->stackLimit=reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackLimit;
#endif
    current=scheduler=context.get();*out=current;contexts.emplace(current,std::move(context));return true;
}
void ShutdownScheduler(Handle handle) {
    if(handle!=scheduler || current!=scheduler || contexts.size()!=1)
        throw std::logic_error("Rewindable scheduler shutdown with live workers or wrong context");
    contexts.erase(handle);current=scheduler=nullptr;
}
Handle Create(size_t size,Entry entry,void* argument) {
    if(!scheduler || !entry || size<64*1024 || size>16*1024*1024)return nullptr;
#if defined(_WIN32)
    SYSTEM_INFO info{};GetSystemInfo(&info);const size_t page=info.dwPageSize;
#else
    const size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
    size=(size+page-1)/page*page;
    auto context=std::make_unique<Context>();context->allocationSize=size+2*page;
#if defined(_WIN32)
    context->allocation=VirtualAlloc(nullptr,context->allocationSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!context->allocation)return nullptr;
    DWORD old;const bool protectedPage=VirtualProtect(static_cast<char*>(context->allocation)+page,page,PAGE_NOACCESS,&old)!=0;
#else
    context->allocation=mmap(nullptr,context->allocationSize,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(context->allocation==MAP_FAILED)return nullptr;
    const bool protectedPage=mprotect(static_cast<char*>(context->allocation)+page,page,PROT_NONE)==0;
#endif
    if(!protectedPage){Release(*context);return nullptr;}
    // The first page holds libco's register block. A guard page separates it
    // from the descending stack, so overflow cannot corrupt saved registers.
    context->bottom=reinterpret_cast<uintptr_t>(context->allocation)+2*page;
    context->top=context->bottom+size;
    context->native=co_derive(context->allocation,static_cast<unsigned>(context->allocationSize),EntryPoint);
    context->entry=entry;context->argument=argument;context->generation=nextGeneration++;
    std::fegetenv(&context->fp);
    context->mxcsr=_mm_getcsr();
#if defined(_WIN32)
    context->stackBase=reinterpret_cast<void*>(context->top);context->stackLimit=reinterpret_cast<void*>(context->bottom);
#endif
    Handle handle=context.get();
    try {contexts.emplace(handle,std::move(context));}
    catch(...){if(context)Release(*context);throw;}
    return handle;
}
void Destroy(Handle handle) {
    auto& context=Lookup(handle);
    if(&context==current || &context==scheduler)throw std::logic_error("Cannot destroy current/scheduler continuation");
    Release(context);contexts.erase(handle);
}
bool IsCurrent(Handle handle){return handle && handle==current;}
void Switch(Handle handle) {
    auto& target=Lookup(handle);if(&target==current)return;
    auto* source=current;std::fegetenv(&source->fp);source->mxcsr=_mm_getcsr();
    std::fesetenv(&target.fp);
    _mm_setcsr(target.mxcsr);
#if defined(_WIN32)
    reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase=target.stackBase;
    reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackLimit=target.stackLimit;
#endif
    current=&target;co_switch(target.native);current=source;
}
Snapshot Capture(Handle handle) {
    auto& context=Lookup(handle);Suspended(context);
    uintptr_t sp;std::memcpy(&sp,context.native,sizeof(sp));
    if(sp<context.bottom || sp>=context.top)throw std::runtime_error("Suspended stack pointer outside owned allocation");
    const uintptr_t start=std::max(context.bottom,sp-std::min<uintptr_t>(128,sp-context.bottom));
    return SnapshotAccess::Make(context.generation,handle,start,context.native,
                               reinterpret_cast<void*>(start),context.top-start,context.fp,context.mxcsr);
}
void ValidateRestore(Handle handle,const Snapshot& snapshot) {
    auto& context=Lookup(handle);Suspended(context);
    const auto start=SnapshotAccess::Start(snapshot);
    const auto& registers=SnapshotAccess::Registers(snapshot);
    if(SnapshotAccess::Owner(snapshot)!=handle || SnapshotAccess::Generation(snapshot)!=context.generation ||
       registers.size()!=512 || start<context.bottom || start>=context.top ||
       SnapshotAccess::Stack(snapshot).size()!=context.top-start)
        throw std::invalid_argument("Continuation snapshot identity/layout disagreement");
    uintptr_t sp;std::memcpy(&sp,registers.data(),sizeof(sp));
    if(sp<start || sp>=context.top)throw std::invalid_argument("Invalid saved continuation stack pointer");
}
void Restore(Handle handle,const Snapshot& snapshot) {
    Rewindable::ValidateRestore(handle,snapshot);auto& context=Lookup(handle);
    std::memcpy(reinterpret_cast<void*>(SnapshotAccess::Start(snapshot)),SnapshotAccess::Stack(snapshot).data(),SnapshotAccess::Stack(snapshot).size());
    std::memcpy(context.native,SnapshotAccess::Registers(snapshot).data(),512);
    context.fp=SnapshotAccess::FP(snapshot);
    context.mxcsr=SnapshotAccess::MXCSR(snapshot);
}
#else
bool InitializeScheduler(Handle*) {return false;}
void ShutdownScheduler(Handle) {}
Handle Create(size_t,Entry,void*) {return nullptr;}
void Destroy(Handle) {throw std::logic_error("Rewindable continuations unavailable on this architecture");}
bool IsCurrent(Handle) {return false;}
void Switch(Handle) {throw std::logic_error("Rewindable continuations unavailable on this architecture");}
Snapshot Capture(Handle) {throw std::logic_error("Rewindable continuations unavailable on this architecture");}
void ValidateRestore(Handle,const Snapshot&) {throw std::logic_error("Rewindable continuations unavailable on this architecture");}
void Restore(Handle,const Snapshot&) {throw std::logic_error("Rewindable continuations unavailable on this architecture");}
#endif
}
}

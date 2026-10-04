#include "host_context.h"
#include <array>
#include <cfenv>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <xmmintrin.h>
#if defined(_WIN32)
#include <windows.h>
#endif
namespace {
HostContext::Handle scheduler,worker;
uint64_t input=3;
std::vector<uint64_t> outputs;
uintptr_t firstAddress=0;
bool rejectedActive=false;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Fn> void rejected(Fn fn){bool caught=false;try{fn();}catch(const std::exception&){caught=true;}require(caught,"Expected rejection");}
void Nested(unsigned depth) {
    volatile uint64_t locals[32];
    for(unsigned i=0;i<32;++i)locals[i]=depth*100+i;
    if(depth)Nested(depth-1);
    else {
        firstAddress=reinterpret_cast<uintptr_t>(&locals[0]);
        try{HostContext::Capture(worker);}catch(const std::logic_error&){rejectedActive=true;}
        std::fesetround(FE_DOWNWARD);
        _mm_setcsr(_mm_getcsr()|0x8040); // FTZ/DAZ are guest FPSCR NI state.
        HostContext::Switch(scheduler);
        for(unsigned round=0;round<20;++round) {
            require(reinterpret_cast<uintptr_t>(&locals[0])==firstAddress,"Continuation stack address changed");
            require(std::fegetround()==(round?FE_UPWARD:FE_DOWNWARD),"Floating-point environment not restored");
            require((_mm_getcsr()&0x8040)==(round?0:0x8040),"MXCSR FTZ/DAZ state not restored");
#if defined(_WIN32)
            const auto address=reinterpret_cast<uintptr_t>(&locals[0]);
            require(address>=reinterpret_cast<uintptr_t>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackLimit)&&
                    address<reinterpret_cast<uintptr_t>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase),"Windows TEB stack limits wrong");
#endif
            locals[0]+=input;
            outputs.push_back(locals[0]+round*1000);
            std::fesetround(FE_UPWARD);
            _mm_setcsr(_mm_getcsr()&~0x8040u);
            HostContext::Switch(scheduler);
        }
    }
    // Keep nested frames and their locals live across the yield.
    require(locals[1]==depth*100+1,"Nested frame corrupted");
}
void Worker(void*){Nested(20);}
}
int main() {
    try {
        const auto originalRound=std::fegetround();
        const auto originalMxcsr=_mm_getcsr();
        require(HostContext::InitializeScheduler(&scheduler,HostContext::Backend::Rewindable),"Rewindable init");
        HostContext::Handle ignored;
        require(!HostContext::InitializeScheduler(&ignored),"Double scheduler initialization rejected");
        require(!HostContext::Create(1,Worker,nullptr),"Small stack rejected");
        worker=HostContext::Create(256*1024,Worker,nullptr);require(worker!=nullptr,"Worker creation");
        HostContext::Switch(worker);require(rejectedActive,"Active worker capture rejected");
        require(std::fegetround()==originalRound,"Scheduler FP state changed");
        // Allocation/CRT calls can raise sticky FP exception flags. Compare
        // rounding, masks and NI controls rather than pre-initialization flags.
        require((_mm_getcsr()&~0x3fu)==(originalMxcsr&~0x3fu),"Scheduler MXCSR control changed");
        auto first=HostContext::Capture(worker);
        require(first.Bytes()>512&&first.Bytes()<64*1024,"Only used stack and registers captured");
        rejected([&]{HostContext::Capture(scheduler);});
        rejected([&]{HostContext::ShutdownScheduler(scheduler);});
        auto wrong=HostContext::Create(64*1024,Worker,nullptr);
        rejected([&]{HostContext::Restore(wrong,first);});HostContext::Destroy(wrong);
        bool rejectedThread=false;
        std::thread t([&]{try{HostContext::Capture(worker);}catch(const std::exception&){rejectedThread=true;}});t.join();
        require(rejectedThread,"Wrong-thread capture rejected");
        for(unsigned i=0;i<4;++i)HostContext::Switch(worker);
        const auto baseline=outputs;outputs.clear();
        HostContext::Restore(worker,first);
        for(unsigned i=0;i<4;++i)HostContext::Switch(worker);
        require(outputs==baseline,"Native local/return continuation replay differs");outputs.clear();
        // Repeated rewinds must work; corrected input must produce a different
        // result from the same saved native locals, not from their later values.
        HostContext::Restore(worker,first);input=7;
        for(unsigned i=0;i<4;++i)HostContext::Switch(worker);
        const auto corrected=outputs;require(corrected!=baseline,"Corrected continuation input ignored");outputs.clear();
        HostContext::Restore(worker,first);
        for(unsigned i=0;i<4;++i)HostContext::Switch(worker);
        require(outputs==corrected,"Corrected native continuation replay differs");
        HostContext::Destroy(worker);rejected([&]{HostContext::Restore(worker,first);});
        HostContext::ShutdownScheduler(scheduler);
        require(std::fegetround()==originalRound,"FP state after shutdown");
        require(HostContext::InitializeScheduler(&scheduler),"Default backend preserved");
        rejected([&]{HostContext::Capture(scheduler);});HostContext::ShutdownScheduler(scheduler);
        std::cout<<"Suspended native continuation: nested stack/local replay, corrected input, FP state and identity/thread/boundary guards passed ("<<first.Bytes()<<" bytes).\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;
}

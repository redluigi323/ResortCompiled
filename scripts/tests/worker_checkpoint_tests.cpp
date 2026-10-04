// Relink the real native manager, without starting the game or graphics.
#include "fiber_manager.h"
#include "abi_bridge.h"
#include "memory.h"
#include "netplay/checkpoint.h"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
constexpr uint32_t mainThread=0x81000000,workerThread=0x81001000,entry=0x8170F000;
uint64_t input=3;
std::vector<uint64_t> output;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Fn> void rejected(Fn fn){bool caught=false;try{fn();}catch(const std::exception&){caught=true;}require(caught,"Expected worker checkpoint rejection");}
void ParkWorker(CpuContext* cpu) {
    Memory::Write32(0x800000D4,0);Memory::Write32(0x800000E4,0);
    Fiber::GuestFiberManager::SwitchToThread(mainThread,cpu);
}
void Worker(CpuContext* cpu) {
    volatile uint64_t local=17;
    ParkWorker(cpu);
    for(unsigned step=0;step<20;++step) {
        local+=input;
        Memory::Write32(0x81008000,uint32_t(local));
        cpu->gpr[3]=uint32_t(local);
        output.push_back(local+step*1000);
        ParkWorker(cpu);
    }
}
}
extern "C" int __wrap_main(int,char**) {
    using Manager=Fiber::GuestFiberManager;
    using namespace Riisorted::Netplay::Rollback;
    try {
        Memory::Init(Memory::Config::WiiDefaults());
        CpuContext cpu{};cpu.gpr[1]=0x81700000;cpu.gpr[2]=0x806FE720;cpu.gpr[13]=0x806F87C0;
        CpuContextScope scope(&cpu);
        TranslatedFunctionInfo info;info.address=entry;info.name="rollback_worker_probe";
        info.kind=FunctionKind::Native;info.rawCpuInvoker=Worker;info.entryPoint=reinterpret_cast<void*>(Worker);
        TranslatedFunctionRegistry::Register(info);
        Manager::Initialize(HostContext::Backend::Rewindable);
        Manager::RegisterMainThreadAsFiber(mainThread,&cpu);
        require(Manager::CreateGuestFiber(workerThread,entry,0,0x81600000),"Guest worker creation");
        Memory::Write32(workerThread+4,0x81600000);
        Memory::Write32(workerThread+8,0x806FE720);
        Memory::Write32(workerThread+0x34,0x806F87C0);
        auto advance=[&] {
            Memory::Write32(0x800000D4,workerThread);Memory::Write32(0x800000E4,workerThread);
            Manager::SwitchToThread(workerThread,&cpu);
            require(Manager::GetCurrentGuestThread()==0,"Worker did not return to outer scheduler");
        };
        advance();
        PageStore pages;auto spans=GuestRamSpans();auto ram=pages.Capture(spans);
        auto checkpoint=Manager::CaptureSuspendedWorkers();const auto savedCpu=cpu;
        require(checkpoint.workers.size()==1,"Suspended worker inventory");
        for(unsigned i=0;i<4;++i)advance();const auto baseline=output;output.clear();
        pages.Restore(ram,spans);cpu=savedCpu;MkwApplyHostNiMode(cpu.fpscr);
        Manager::RestoreSuspendedWorkers(checkpoint);
        for(unsigned i=0;i<4;++i)advance();require(output==baseline,"Guest worker native/CPU/RAM replay differs");output.clear();
        pages.Restore(ram,spans);cpu=savedCpu;MkwApplyHostNiMode(cpu.fpscr);
        Manager::RestoreSuspendedWorkers(checkpoint);input=7;
        for(unsigned i=0;i<4;++i)advance();const auto corrected=output;output.clear();require(corrected!=baseline,"Corrected worker input ignored");
        pages.Restore(ram,spans);cpu=savedCpu;MkwApplyHostNiMode(cpu.fpscr);
        Manager::RestoreSuspendedWorkers(checkpoint);
        for(unsigned i=0;i<4;++i)advance();require(output==corrected,"Corrected guest-worker replay differs");
        auto invalid=checkpoint;invalid.workers[0].thread+=0x100;
        rejected([&]{Manager::RestoreSuspendedWorkers(invalid);});
        require(Manager::CreateGuestFiber(workerThread+0x1000,entry,0,0x81500000),"Topology probe creation");
        rejected([&]{Manager::RestoreSuspendedWorkers(checkpoint);});
        Manager::Shutdown();
        std::cout<<"Real guest-worker checkpoint: native locals, saved CPU metadata and RAM replay/correction, identity/topology guards passed.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;
}

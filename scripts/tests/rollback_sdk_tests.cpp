// Component restore proof at a returned SDK-call boundary. This does NOT
// restore a running match, native fiber stacks, graphics, storage or audio.
// Link with --wrap=main; no game startup, SDL window or graphics initialization.
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "resort_motion.h"
#include "netplay/checkpoint.h"
#include <fstream>
#include <vector>
#include <array>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
extern "C" void func_800CD8C0(CpuContext*);
extern "C" void func_800CFCB0(CpuContext*);
using namespace Riisorted::Netplay::Rollback;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
extern "C" int __wrap_main(int argc,char**argv) {
    try {
        require(argc==2,"Pass main.dol");
        Memory::Init(Memory::Config::WiiDefaults());
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<unsigned char> dol((std::istreambuf_iterator<char>(file)),{});
        require(dol.size()>0x100,"Invalid DOL");
        auto u=[&](int offset){return(uint32_t(dol.at(offset))<<24)|(uint32_t(dol.at(offset+1))<<16)|
                                         (uint32_t(dol.at(offset+2))<<8)|dol.at(offset+3);};
        for(int section=0;section<18;++section)
            for(uint32_t i=0;i<u(0x90+section*4);++i)
                Memory::Write8(u(0x48+section*4)+i,dol.at(u(section*4)+i));
        CpuContext cpu{};cpu.gpr[1]=0x81700000;cpu.gpr[2]=0x806FE720;cpu.gpr[13]=0x806F87C0;
        CpuContextScope scope(&cpu);
        const uint32_t work=0x81000000,raw=0x81010000,out=0x81020000;
        cpu.gpr[3]=work;func_800CD8C0(&cpu);
        Memory::Write8(0x8076A718+0x659,5);
        Memory::Write32(work+3488,raw);Memory::Write32(work+3492,raw);Memory::Write32(work+3496,0);
        PageStore pages;
        const auto spans=GuestRamSpans();
        auto initial=pages.Capture(spans);
        const auto initialCpu=cpu;
        auto advance=[&](unsigned first,unsigned end) {
            std::vector<uint32_t> results;
            for(unsigned frame=first;frame<end;++frame) {
                const auto wii=ResortMotion::wiiGyro({frame*.013,frame*.009,-frame*.007});
                const auto pitch=ResortMotion::encode(wii.x),yaw=ResortMotion::encode(wii.z),roll=ResortMotion::encode(wii.y);
                for(unsigned i=0;i<0x180;i+=4)Memory::Write32(out+i,0);
                Memory::Write8(raw+54,1|(roll.slow?2:0)|(pitch.slow?4:0)|(yaw.slow?8:0));
                Memory::Write16(raw+56,pitch.value);Memory::Write16(raw+58,yaw.value);Memory::Write16(raw+60,roll.value);
                for(unsigned axis=0;axis<3;++axis)Memory::WriteFloat32(out+0x100+12+axis*4,axis==1?-1.0:0.0);
                cpu.gpr[3]=0;cpu.gpr[4]=out;cpu.gpr[5]=2;cpu.gpr[6]=1;cpu.gpr[7]=out+0x100;cpu.fpr[1].d=1.0/60;
                func_800CFCB0(&cpu);
                for(unsigned i=0;i<15;++i)results.push_back(Memory::Read32(out+i*4));
            }
            return results;
        };
        const auto first=advance(0,20);
        const auto firstFinal=pages.Capture(spans,&initial);
        const auto finalCpu=cpu;
        pages.Restore(initial,spans);cpu=initialCpu;MkwApplyHostNiMode(cpu.fpscr);
        require(pages.Capture(spans,&initial).Digest()==initial.Digest(),"Full RAM restore mismatch");
        const auto replay=advance(0,20);
        const auto replayFinal=pages.Capture(spans,&firstFinal);
        require(first==replay,"Translated MotionPlus outputs differ after rewind/replay");
        require(firstFinal.Digest()==replayFinal.Digest(),"Replayed RAM differs");
        require(std::equal(cpu.gpr,cpu.gpr+32,finalCpu.gpr),"Replayed integer CPU state differs");
        require(std::memcmp(cpu.fpr,finalCpu.fpr,sizeof(cpu.fpr))==0 &&
                cpu.fpscr==finalCpu.fpscr && cpu.cr==finalCpu.cr && cpu.xer==finalCpu.xer &&
                cpu.lr==finalCpu.lr && cpu.ctr==finalCpu.ctr && cpu.pc==finalCpu.pc &&
                std::equal(cpu.gqr,cpu.gqr+8,finalCpu.gqr),"Replayed floating/register-control state differs");
        // A corrected replay must differ, and must reproduce a fresh baseline
        // with those same corrected inputs rather than keep the old result.
        pages.Restore(initial,spans);cpu=initialCpu;MkwApplyHostNiMode(cpu.fpscr);
        const auto corrected=advance(20,40);
        const auto correctedFinal=pages.Capture(spans,&initial);
        require(corrected!=first,"Changed input did not change solver state");
        pages.Restore(initial,spans);cpu=initialCpu;MkwApplyHostNiMode(cpu.fpscr);
        require(advance(20,40)==corrected,"Corrected input replay mismatch");
        require(pages.Capture(spans,&correctedFinal).Digest()==correctedFinal.Digest(),"Corrected RAM replay mismatch");
        std::cout<<"Translated SDK rollback component: full physical RAM restore, exact replay and corrected input replay passed; "
                 <<pages.ResidentBytes()<<" bytes retained.\n";
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    return 0;
}

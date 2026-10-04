// Relink with --wrap=main to test state transfer using the built runtime,
// without graphics, game startup or instruction tracing.
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "netplay/motionplus_report.h"
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
extern "C" void func_800CD8C0(CpuContext*);
namespace ResortWiimote {
std::vector<uint8_t> CaptureOnlineSolverState(uint32_t);
void RestoreOnlineSolverState(uint32_t,const std::vector<uint8_t>&);
}
void require(bool condition,const char* why) {if(!condition){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
extern "C" int __wrap_main(int argc,char**argv) {
 require(argc==2,"main.dol argument");Memory::Init(Memory::Config::WiiDefaults());
 std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char>d((std::istreambuf_iterator<char>(file)),{});
 auto u=[&](int o){return(uint32_t(d[o])<<24)|(uint32_t(d[o+1])<<16)|(uint32_t(d[o+2])<<8)|d[o+3];};
 for(int s=0;s<18;s++)for(uint32_t i=0;i<u(0x90+s*4);i++)Memory::Write8(u(0x48+s*4)+i,d[u(s*4)+i]);
 CpuContext cpu{};cpu.gpr[1]=0x81700000;cpu.gpr[2]=0x806FE720;cpu.gpr[13]=0x806F87C0;CpuContextScope scope(&cpu);
 const uint32_t host=0x81000000,guest=0x81200000;
 cpu.gpr[3]=host;func_800CD8C0(&cpu);
 for(unsigned channel=0;channel<2;++channel) {
  const auto work=host+channel*3504;
  Memory::WriteFloat32(work+32,0.5f+channel);
  Memory::Write32(work+192,0x1234+channel);
  for(unsigned p=0;p<3;++p)Memory::Write32(work+3488+p*4,0x81010000+p*0x100);
 }
 auto first=ResortWiimote::CaptureOnlineSolverState(0),second=ResortWiimote::CaptureOnlineSolverState(1);
 require(first.size()==Riisorted::Netplay::kMotionPlusStateBytes,"all scalar state encoded");
 cpu.gpr[3]=guest;func_800CD8C0(&cpu);
 for(unsigned channel=0;channel<2;++channel) {
  const auto work=guest+channel*3504;
  for(unsigned p=0;p<3;++p)Memory::Write32(work+3488+p*4,0x81210000+p*0x100);
  ResortWiimote::RestoreOnlineSolverState(channel,channel?second:first);
  require(ResortWiimote::CaptureOnlineSolverState(channel)==(channel?second:first),"calibration state matches host");
  for(unsigned p=0;p<3;++p)require(Memory::Read32(work+3488+p*4)==0x81210000+p*0x100,"guest raw-buffer pointer preserved");
 }
 std::puts("MotionPlus state transfer preserves local pointers and synchronizes both channel states.");return 0;
}

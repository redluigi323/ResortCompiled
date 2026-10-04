#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "resort_motion.h"
#include "netplay/session.h"
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <array>
extern "C" void func_800CD8C0(CpuContext*);
extern "C" void func_800CFCB0(CpuContext*);
extern "C" int __wrap_main(int argc,char**argv) {
 if(argc!=3 && argc!=4)return 2;
 Memory::Init(Memory::Config::WiiDefaults());
 std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char>d((std::istreambuf_iterator<char>(file)),{});
 auto u=[&](int o){return(uint32_t(d[o])<<24)|(uint32_t(d[o+1])<<16)|(uint32_t(d[o+2])<<8)|d[o+3];};
 for(int s=0;s<18;s++)for(uint32_t i=0;i<u(0x90+s*4);i++)Memory::Write8(u(0x48+s*4)+i,d[u(s*4)+i]);
 std::ifstream input(argv[2],std::ios::binary);std::vector<unsigned char>bytes((std::istreambuf_iterator<char>(input)),{});
 auto batch=Riisorted::Netplay::DecodeMotionBatch(bytes);
 std::array<std::vector<uint32_t>,2> results;
 for(unsigned mode=0;mode<2;++mode) {
  CpuContext c{};c.gpr[1]=0x81700000;c.gpr[2]=0x806FE720;c.gpr[13]=0x806F87C0;c.fpscr=mode?4:0;
  CpuContextScope scope(&c);
  const uint32_t w=0x81000000,raw=0x81010000,out=0x81020000;
  for(uint32_t i=0;i<3504;i+=4)Memory::Write32(w+i,0);
  c.gpr[3]=w;func_800CD8C0(&c);Memory::Write8(0x8076A718+0x659,5);
  Memory::Write32(w+3488,raw);Memory::Write32(w+3492,raw);Memory::Write32(w+3496,0);
  for(unsigned n=0;n<1500;++n) {
   auto s=batch.samples[n%batch.samples.size()];
   auto wii=ResortMotion::wiiGyro({s.gyro[0],s.gyro[1],s.gyro[2]});
   auto p=ResortMotion::encode(wii.x),y=ResortMotion::encode(wii.z),r=ResortMotion::encode(wii.y);
   for(unsigned i=0;i<0x180;i+=4)Memory::Write32(out+i,0);
   Memory::Write8(raw+54,1|(r.slow?2:0)|(p.slow?4:0)|(y.slow?8:0));
   Memory::Write16(raw+56,p.value);Memory::Write16(raw+58,y.value);Memory::Write16(raw+60,r.value);
   for(unsigned i=0;i<3;++i)Memory::WriteFloat32(out+0x100+12+i*4,s.acceleration[i]);
   c.gpr[3]=0;c.gpr[4]=out;c.gpr[5]=2;c.gpr[6]=1;c.gpr[7]=out+0x100;c.fpr[1].d=s.seconds;
   func_800CFCB0(&c);
   for(unsigned i=0;i<15;++i)results[mode].push_back(Memory::Read32(out+i*4));
  }
 }
 if(argc==4) {std::ofstream output(argv[3],std::ios::binary); for(auto& run:results)output.write(reinterpret_cast<const char*>(run.data()),run.size()*4);}
 unsigned diffs=0;for(unsigned i=0;i<results[0].size();++i)if(results[0][i]!=results[1][i]) {
  if(diffs<5)std::printf("sample %u word %u: %08x vs %08x\n",i/15,i%15,results[0][i],results[1][i]);++diffs;
 }
 std::printf("NI differing words %u\n",diffs);
 return 0;
}

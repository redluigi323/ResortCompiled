// Link against the built game with --wrap=main. Runs translated KPAD math
// against synthetic physical reports, without starting graphics or tracing.
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "resort_motion.h"
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
extern "C" void func_800CD8C0(CpuContext*);
extern "C" void func_800CFCB0(CpuContext*);
extern "C" void func_800CE180(CpuContext*);
extern "C" int32_t KPADReadEx_HLE(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
extern "C" int g_gxFrameCount;
void require(bool ok,const char* name) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
extern "C" int __wrap_main(int argc,char**argv) {
 require(argc==2,"provide extracted main.dol");
 Memory::Init(Memory::Config::WiiDefaults());
 std::ifstream file(argv[1],std::ios::binary);
 std::vector<unsigned char> d((std::istreambuf_iterator<char>(file)),{});
 require(d.size()>0x100,"DOL loaded");
 auto u=[&](int o){return (uint32_t(d[o])<<24)|(uint32_t(d[o+1])<<16)|(uint32_t(d[o+2])<<8)|d[o+3];};
 for(int s=0;s<18;s++)for(uint32_t i=0;i<u(0x90+s*4);i++)Memory::Write8(u(0x48+s*4)+i,d[u(s*4)+i]);
 CpuContext c{};c.gpr[1]=0x81700000;c.gpr[2]=0x806FE720;c.gpr[13]=0x806F87C0;
 CpuContextScope scope(&c);
 const uint32_t w=0x81000000,raw=0x81010000,out=0x81020000;
 c.gpr[3]=w;func_800CD8C0(&c);
 Memory::Write8(0x8076A718+0x659,4);
 Memory::Write32(w+3488,raw);Memory::Write32(w+3492,raw);Memory::Write32(w+3496,0);
 const auto step=[&](ResortMotion::Vec body) {
  auto wii=ResortMotion::wiiGyro(body);
  auto p=ResortMotion::encode(wii.x),y=ResortMotion::encode(wii.z),r=ResortMotion::encode(wii.y);
  Memory::Write8(raw+54,1|(r.slow?2:0)|(p.slow?4:0)|(y.slow?8:0));
  Memory::Write16(raw+56,p.value);Memory::Write16(raw+58,y.value);Memory::Write16(raw+60,r.value);
  Memory::WriteFloat32(out+0x100+16,-1); // KPAD acc at fusion +12
  c.gpr[3]=0;c.gpr[4]=out;c.gpr[5]=2;c.gpr[6]=1;c.gpr[7]=out+0x100;c.fpr[1].d=.005;
  func_800CFCB0(&c);
  for(int i=0;i<15;i++)require(std::isfinite(Memory::ReadFloat32(out+i*4)),"finite SDK output");
 };
 for(int n=0;n<400;n++)step({});
 require(Memory::Read8(w+192)==0,"startup hardware calibration completes");
 for(int n=0;n<100;n++)step({ResortMotion::pi/2,0,0});
 require(std::abs(Memory::ReadFloat32(out)-.25)<.001,"sustained 90 degrees/sec pitch survives calibration");
 require(std::abs(Memory::ReadFloat32(out+12)-.125)<.002,"pitch angle integrates half second");
 require(Memory::ReadFloat32(out+12+12+12+8)>0,"pitch direction sign");
 for(int n=0;n<100;n++)step({-ResortMotion::pi/2,0,0});
 for(int n=0;n<20;n++)step({});
 require(std::abs(Memory::ReadFloat32(out+12))<.003,"reverse swing returns pitch angle");
 c.gpr[3]=0;func_800CE180(&c);
 for(int axis=0;axis<3;axis++)for(int cycle=0;cycle<12;cycle++) {
  for(int sign:{1,-1})for(int n=0;n<40;n++) {
   ResortMotion::Vec gyro{};if(axis==0)gyro.x=sign*2.;if(axis==1)gyro.y=sign*2.;if(axis==2)gyro.z=sign*2.;step(gyro);
  }
  for(int n=0;n<20;n++)step({});
 }
 for(int row=0;row<3;row++)for(int col=0;col<3;col++) {
  double dot=0;for(int k=0;k<3;k++)dot+=Memory::ReadFloat32(out+24+row*12+k*4)*Memory::ReadFloat32(out+24+col*12+k*4);
  require(std::abs(dot-(row==col?1.:0.))<.003,"SDK directions remain orthonormal after repeated swings");
 }
 for(int i=0;i<3;i++)require(std::abs(Memory::ReadFloat32(out+i*4))<.001,"released swing has zero rate");
 // Two consumers may read one batch, but the same consumer must not replay it.
 ++g_gxFrameCount;
 require(KPADReadEx_HLE(0,0x81040000,16,0,0)>0,"first KPAD consumer reads");
 require(KPADReadEx_HLE(0,0x81040000,16,0,0)==0,"duplicate read does not replay motion");
 require(KPADReadEx_HLE(0,0x81041000,16,0,0)>0,"second KPAD consumer reads same batch");
 std::puts("Translated KPAD calibration, sustained and repeated swings, direction matrices, and reader tests passed.");
 return 0;
}

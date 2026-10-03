#include "resort_motion.h"
#include <cstdio>
#include <cstdlib>
using namespace ResortMotion;
void check(bool ok,const char* name){if(!ok){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
void near(double a,double b,const char* name){check(std::abs(a-b)<1e-6,name);}
int main(){
 const Vec p=bodyGyro({1,0,0}),y=bodyGyro({0,0,1}),r=bodyGyro({0,1,0});
 near(p.x,1,"pitch axis");near(y.y,1,"yaw axis");near(r.z,-1,"roll axis");
 Quaternion q;q.integrate(p,pi/6);check(q.rotate({0,0,-1}).y>0,"pitch up points up");
 q={};q.integrate(y,pi/6);check(q.rotate({0,0,-1}).x<0,"turn left points left");
 q={};q.integrate(r,pi/6);check(q.rotate({0,1,0}).x>0,"roll right tilts horizon right");
 Vec a=kpadAccel({0,0,9.80665});near(a.y,-1,"flat neutral acceleration");
 a=kpadAccel({2,3,9.80665});check(a.x>0 && a.z<0,"linear acceleration signs");
 check(encode(0).value==8192,"raw neutral midpoint");
 check(encode(1).slow && !encode(12).slow,"raw slow/fast transition");
 for(double t:{-20.,-5.,0.,5.,20.}) {
  auto raw=encode(t);double decoded=(int(raw.value)-8192)*(raw.slow?270.:1200.)/4352.*pi/180.;
  check(std::abs(decoded-t)<.006,"raw sensor round trip");
 }
 Bias b;for(int i=0;i<400;i++)b.correct({.04,-.03,.02},{0,-1,0},.005);
 near(length(b.correct({.04,-.03,.02},{0,-1,0},.005)),0,"stationary bias removed");
 check(b.correct({3,0,0},{0,-1,0},.005).x>2.9,"swing not learned as bias");
 q={};for(int cycle=0;cycle<100;cycle++) {
  for(int i=0;i<100;i++)q.integrate({1,2,3},.005);
  for(int i=0;i<100;i++)q.integrate({-1,-2,-3},.005);
 }
 near(q.w,1,"repeated swings return to neutral");near(length(q.rotate({1,0,0})),1,"normalized direction after repeated swings");
 q={};q.integrate({1,0,0},pi/2);q.integrate({0,1,0},pi/2);
 Vec f=q.rotate({0,0,-1});near(f.x,-1,"body axes follow tilted controller");
 std::puts("Motion sensor mapping, bias, encoding, and repeated-swing tests passed.");
}

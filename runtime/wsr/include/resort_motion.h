#pragma once
// Sensor coordinates follow SDL3's fixed device axes. For a face-up
// DualSense, +X is right, +Y is towards the triggers, and +Z is out of the face.
// The virtual remote points towards the triggers: body X right, Y face, Z back.
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace ResortMotion {
constexpr double pi = 3.14159265358979323846;
struct Vec { double x=0,y=0,z=0; };
inline Vec operator+(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec operator-(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec operator*(Vec a,double t){return {a.x*t,a.y*t,a.z*t};}
inline double length(Vec a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
inline Vec bodyGyro(Vec sdl){return {sdl.x,sdl.z,-sdl.y};}
inline Vec kpadAccel(Vec sdl){return {sdl.x/9.80665,-sdl.z/9.80665,-sdl.y/9.80665};}
// Wii physical frame: X left, Y back, Z out of the button face.
inline Vec wiiGyro(Vec body){return {-body.x,body.z,body.y};}
struct Quaternion {
 double w=1,x=0,y=0,z=0;
 Quaternion operator*(const Quaternion& b)const {
  return {w*b.w-x*b.x-y*b.y-z*b.z,w*b.x+x*b.w+y*b.z-z*b.y,
          w*b.y-x*b.z+y*b.w+z*b.x,w*b.z+x*b.y-y*b.x+z*b.w};
 }
 void integrate(Vec rate,double dt) {
  double a=length(rate)*dt;
  double k=a<1e-9?dt*.5:std::sin(a*.5)/length(rate);
  *this=*this*Quaternion{std::cos(a*.5),rate.x*k,rate.y*k,rate.z*k};
  double n=std::sqrt(w*w+x*x+y*y+z*z);w/=n;x/=n;y/=n;z/=n;
 }
 Vec rotate(Vec v)const {
  Quaternion q=*this*Quaternion{0,v.x,v.y,v.z}*Quaternion{w,-x,-y,-z};
  return {q.x,q.y,q.z};
 }
};
// MotionPlus uses a 14-bit value centred at 8192. Match Dolphin's sensor
// ranges/calibration (4352 counts at 270 or 1200 degrees/second).
struct RawAxis { uint16_t value;bool slow; };
inline RawAxis encode(double radians) {
 double degrees=radians*180/pi;
 bool slow=std::abs(degrees)<8192.0*270.0/4352.0;
 double scale=4352.0/(slow?270.0:1200.0);
 return {static_cast<uint16_t>(std::clamp(std::lround(8192+degrees*scale),0l,16383l)),slow};
}
struct Bias {
 Vec mean{},bias{};double duration=0;
 Vec correct(Vec value,Vec acc,double dt) {
  // Never learn a swing as the rest rate. Require low rate, one g, and a
  // stable gyro; unlike gravity subtraction this remains valid at any tilt.
  if(length(value)>.12 || std::abs(length(acc)-1)>.08 ||
     (duration>0 && length(value-mean)>.012)) {mean={};duration=0;}
  else {
   duration+=dt;mean=mean+(value-mean)*(dt/duration);
   if(duration>=1.0)bias=mean;
  }
  Vec r=value-bias;
  if(std::abs(r.x)<.025)r.x=0;
  if(std::abs(r.y)<.025)r.y=0;
  if(std::abs(r.z)<.025)r.z=0;
  return r;
 }
};
}

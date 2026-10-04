#include "netplay/motionplus_report.h"
// Resortcompiled: virtual Wii Remote with Wii MotionPlus, fed to WSR at the KPAD level.
//
// WSR reads its remote through a newer KPAD (MotionPlus-aware) than Mario Kart Wii, so WiiCompiled's MKW
// KPAD HLE does not apply. What the game actually consumes (traced in main.dol):
//
//   KPADReadEx      0x800CB890  up to 16 KPADStatus (0xF0 each, newest first) per call; the controller
//                               object at 0x80241324 reads chan 0 once per frame and uses every sample
//   KPAD raw copy   0x800CD620  0x40-byte KPADUnifiedWpadStatus per sample; the game's MotionPlus / Nunchuk
//                               subsystems only look at err (+0x29), dev (+0x28) and fmt (+0x3E) there:
//                               MotionPlus is "present" when dev is 5..7 and fmt is 0x10 (WPAD_FMT_MPLS)
//   WPAD MPLS mode  0x800242E0  the game switches MotionPlus on with KPADEnableMpls(chan, 4|5), which only
//                               stores the wish in KPAD state (+0x659), then polls this until it matches
//   WPADProbe       0x80020670  (wpad.cpp) connection + device type
//
// Physical controllers provide measured acceleration and timestamped gyro reports.
// The translated game's own KPAD MotionPlus solver supplies rates, angles and
// direction vectors, including its calibration and direction-revise settings.
// The gyro pointer has a separate body-rate quaternion; cursor projection never
// feeds back into the sports' sensor data.
#include <SDL3/SDL.h>
#include <dolphin/pad.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <unordered_map>

#include "guest_interrupt_context.h"
#include "resort_motion.h"
#include "netplay/input_tape.h"
#include "netplay/live.h"
#include "aurora_events.h"
#include "guest_clock.h"
extern "C" int g_gxFrameCount;
extern "C" void func_800CFCB0(CpuContext*);
extern "C" void func_800CE180(CpuContext*);

#include "hle_stubs.h"
#include "input_bindings.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "wii_remote_input.h"

namespace ResortWiimote {

// ------------------------------------------------------------------------------------------------ guest layout
namespace guest {
constexpr uint32_t kKpadStateBase = 0x8076A718u;   // per-channel KPAD state (lis 0x8077; addi -0x58E8)
constexpr uint32_t kKpadStateStride = 0x668u;
constexpr uint32_t kKpadMplsRequestedMode = 0x659u; // byte written by KPADEnableMpls / KPADDisableMpls

constexpr uint32_t kStatusSize = 0xF0u;
constexpr uint32_t kHold = 0x00, kTrig = 0x04, kRelease = 0x08, kAcc = 0x0C, kAccValue = 0x18,
                   kAccSpeed = 0x1C, kPos = 0x20, kVec = 0x28, kSpeed = 0x30, kHorizon = 0x34,
                   kHoriVec = 0x3C, kHoriSpeed = 0x44, kDist = 0x48, kDistVec = 0x4C, kDistSpeed = 0x50,
                   kAccVertical = 0x54, kDevType = 0x5C, kWpadErr = 0x5D, kDpdValid = 0x5E, kDataFormat = 0x5F,
                   kExStatus = 0x60, kMpls = 0xB0, kMplsAngle = 0xBC, kMplsDirX = 0xC8, kMplsDirY = 0xD4,
                   kMplsDirZ = 0xE0, kTail = 0xEC;

constexpr uint32_t kRawSize = 0x40u;
constexpr uint32_t kRawButton = 0x00, kRawAcc = 0x02, kRawObj = 0x08, kRawDev = 0x28, kRawErr = 0x29,
                   kRawMpStat = 0x36, kRawMpPitch = 0x38, kRawMpYaw = 0x3A, kRawMpRoll = 0x3C, kRawFmt = 0x3E;

constexpr uint8_t kDevCore = 0, kDevMpls = 5;
constexpr uint8_t kFmtCoreAccDpd = 2, kFmtMpls = 0x10;
constexpr int32_t kKpadErrNone = 0, kKpadErrNoController = -2;
}  // namespace guest

// WPAD_BUTTON_* (core remote)
enum : uint32_t {
    kBtnLeft = 0x0001, kBtnRight = 0x0002, kBtnDown = 0x0004, kBtnUp = 0x0008, kBtnPlus = 0x0010,
    kBtn2 = 0x0100, kBtn1 = 0x0200, kBtnB = 0x0400, kBtnA = 0x0800, kBtnMinus = 0x1000, kBtnHome = 0x8000,
};

// ------------------------------------------------------------------------------------------------ bindings
// First pass: fixed defaults, printed at startup. (Config.toml remapping comes with the settings page.)
struct KeyBinding { SDL_Scancode key; uint32_t button; };
constexpr std::array<KeyBinding, 17> kKeyBindings = {{
    {SDL_SCANCODE_Z, kBtnA},        {SDL_SCANCODE_X, kBtnB},
    {SDL_SCANCODE_1, kBtn1},        {SDL_SCANCODE_2, kBtn2},
    {SDL_SCANCODE_RETURN, kBtnPlus}, {SDL_SCANCODE_EQUALS, kBtnPlus},
    {SDL_SCANCODE_MINUS, kBtnMinus}, {SDL_SCANCODE_BACKSPACE, kBtnMinus},
    {SDL_SCANCODE_H, kBtnHome},
    {SDL_SCANCODE_UP, kBtnUp},      {SDL_SCANCODE_DOWN, kBtnDown},
    {SDL_SCANCODE_LEFT, kBtnLeft},  {SDL_SCANCODE_RIGHT, kBtnRight},
    {SDL_SCANCODE_W, kBtnUp},       {SDL_SCANCODE_S, kBtnDown},
    {SDL_SCANCODE_A, kBtnLeft},     {SDL_SCANCODE_D, kBtnRight},
}};
constexpr SDL_Scancode kKeyShake = SDL_SCANCODE_SPACE;
constexpr SDL_Scancode kKeyTilt = SDL_SCANCODE_LSHIFT;
constexpr SDL_Scancode kKeyRecenter = SDL_SCANCODE_R;
// Mouse: left = A, right = B, middle = Tilt (same as Left Shift), wheel = roll.

// ------------------------------------------------------------------------------------------------ tuning
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kSampleHz = 200.0;              // Wii Remote report rate KPAD sees
constexpr double kHalfFovX = 20.0 * kDeg;        // yaw that takes the pointer to the screen edge
constexpr double kHalfFovY = 15.0 * kDeg;
constexpr double kTiltDegPerPixel = 0.35;        // mouse sensitivity while tilting
constexpr double kRollDegPerWheelStep = 12.0;
constexpr double kMaxPitch = 85.0 * kDeg;
constexpr double kReturnTau = 0.08;              // seconds; ease back to the mouse pointer after a tilt
constexpr double kShakeHz = 6.0;
constexpr double kShakeG = 2.6;
constexpr double kLeverM = 0.12;                 // wrist -> accelerometer distance for motion acceleration
constexpr double kGravity = 9.81;
constexpr double kMplsUnitRadPerSec = 2.0 * kPi; // KPAD mpls/angle: 1.0 = one revolution (per second)
constexpr float kDist = 1.5f;                    // metres to the sensor bar

// Sign conventions of KPAD's MotionPlus output relative to our remote frame (X right, Y up out of the button
// face, Z towards the player; the remote points along -Z). Overridable with RESORT_MPLS_SIGNS="sx,sy,sz" and
// RESORT_MPLS_DIRSIGN=±1 while we confirm them against the game.
struct Conventions { double gyro[3] = {1, 1, 1}; double dir = 1; };

// ------------------------------------------------------------------------------------------------ math
using Vec3 = ResortMotion::Vec;
struct Mat3 { double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; };

Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Len(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

Mat3 Mul(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = 0;
            for (int k = 0; k < 3; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
        }
    return r;
}
Mat3 Transpose(const Mat3& a) {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[j][i];
    return r;
}
Vec3 Apply(const Mat3& a, Vec3 v) {
    return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z, a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
            a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}
Vec3 Column(const Mat3& a, int c) { return {a.m[0][c], a.m[1][c], a.m[2][c]}; }
// Our simulation uses X right, Y out of the button face, Z toward the
// player. KPAD's Wii Remote frame uses X left, Y toward the player, Z out of
// the button face. This is a proper basis rotation, not three unrelated signs.
Vec3 ToWii(Vec3 v) { return {-v.x, v.z, v.y}; }
Vec3 WiiDir(const Mat3& r, int axis) {
    if (axis == 0) return ToWii(Column(r, 0) * -1.0);
    return ToWii(Column(r, axis == 1 ? 2 : 1));
}
Mat3 RotX(double t) { Mat3 r; r.m[1][1] = std::cos(t); r.m[1][2] = -std::sin(t); r.m[2][1] = std::sin(t); r.m[2][2] = std::cos(t); return r; }
Mat3 RotY(double t) { Mat3 r; r.m[0][0] = std::cos(t); r.m[0][2] = std::sin(t); r.m[2][0] = -std::sin(t); r.m[2][2] = std::cos(t); return r; }
Mat3 RotZ(double t) { Mat3 r; r.m[0][0] = std::cos(t); r.m[0][1] = -std::sin(t); r.m[1][0] = std::sin(t); r.m[1][1] = std::cos(t); return r; }

// Remote -> world rotation from aim angles: yaw right+, pitch up+, roll right (clockwise as seen by the player)+.
Mat3 Orientation(double yaw, double pitch, double roll) { return Mul(Mul(RotY(-yaw), RotX(pitch)), RotZ(-roll)); }

// Body-frame angular velocity (rad/s) that takes `a` to `b` in `dt` seconds.
Vec3 AngularVelocity(const Mat3& a, const Mat3& b, double dt) {
    const Mat3 d = Mul(Transpose(a), b);
    Vec3 w{(d.m[2][1] - d.m[1][2]) * 0.5, (d.m[0][2] - d.m[2][0]) * 0.5, (d.m[1][0] - d.m[0][1]) * 0.5};
    const double s = Len(w);
    if (s > 1e-9) {
        const double c = std::clamp((d.m[0][0] + d.m[1][1] + d.m[2][2] - 1.0) * 0.5, -1.0, 1.0);
        w = w * (std::atan2(s, c) / s);
    }
    return w * (1.0 / dt);
}

// ------------------------------------------------------------------------------------------------ state
struct Sample {
    uint32_t hold = 0;
    Mat3 R;                 // remote -> world
    Vec3 gyro;              // body rad/s
    Vec3 angle;
    double dt = 0.005;
    std::array<uint32_t,15> mpls{};
    bool sdkMpls = false;
    Vec3 acc;               // KPAD acc (g, "gravity direction" convention: (0,-1,0) at rest)
    double posX = 0, posY = 0;
    bool pointerValid = false;
    double roll = 0;
};

struct Remote {
    bool announced = false;
    Conventions conv;
    // aim
    double yaw = 0, pitch = 0, roll = 0;
    bool tilting = false;
    double easeLeft = 0;         // seconds of easing back to the mouse after a tilt
    double wheelSteps = 0;
    // time
    std::chrono::steady_clock::time_point lastRead{};
    double simTime = 0;          // seconds of simulated remote time
    double shakeStart = -1;
    // derived history
    Sample last;
    Mat3 prevR;
    Vec3 prevGyro;
    Vec3 angle;                  // integrated, KPAD units
    uint32_t prevHold = 0;
    double prevPosX = 0, prevPosY = 0, prevDist = kDist;
    // newest-first ring of generated samples for the raw copy
    std::array<Sample, 16> recent{};
    std::unordered_map<uint32_t, uint32_t> prevHoldByBuffer;   // KPAD reader buffer -> hold it last saw
    bool log = false;
    uint32_t recentCount = 0;
    SDL_JoystickID activeGamepad = 0;
    SDL_JoystickID sensorEnabledFor = 0;
    SDL_JoystickID accelEnabledFor = 0;
    struct SensorReport { uint64_t time; Vec3 gyro,acc; };
    std::deque<SensorReport> sensors;
    Vec3 sensorAcc{0,-1,0};
    ResortMotion::Quaternion pointerPose;
    ResortMotion::Bias bias;
    uint64_t sensorTime = 0;
    double binTime = 0;
    Vec3 binGyro;
    bool recenterHeld = false;
    int stepFrame = -1;
    uint64_t batch = 0;
    std::unordered_map<uint32_t,uint64_t> readerBatch;

};

Remote g_local;
std::array<Remote, 2> g_onlineRemotes;
Remote* g_currentRemote = &g_local;
Remote& CurrentRemote() { return *g_currentRemote; }
struct RemoteScope {
    Remote* previous;
    explicit RemoteScope(Remote& remote) : previous(g_currentRemote) { g_currentRemote = &remote; }
    ~RemoteScope() { g_currentRemote = previous; }
};
namespace Online = Riisorted::Netplay::Live;
int g_onlineFrame = -1;
uint64_t g_onlineSequence = 0;
uint64_t g_previousOnlineSolverHash = 14695981039346656037ull;


namespace Tape = Riisorted::Netplay::InputTape;
Riisorted::Netplay::MotionBatch g_capture;
uint64_t g_solverHash = 14695981039346656037ull;

void AccumulateSolverResult(const Sample& sample) {
    if (!Tape::Recording() && !Tape::Replaying() && !Online::Active()) return;
    const auto add = [](uint8_t byte) { g_solverHash = (g_solverHash ^ byte) * 1099511628211ull; };
    add(sample.sdkMpls ? 1 : 0);
    for (uint32_t word : sample.mpls)
        for (int shift = 24; shift >= 0; shift -= 8) add(static_cast<uint8_t>(word >> shift));
}

void CaptureMappedSample(const Sample& s) {
    if (!Tape::Recording() && !Online::Active()) return;
    if (g_capture.samples.size() == Riisorted::Netplay::kMaxMotionSamples)
        throw std::runtime_error("Motion capture backlog exceeds the bounded sample window");
    Riisorted::Netplay::MotionSample input;
    input.buttons = s.hold;
    for (size_t row = 0; row < 3; ++row)
        for (size_t col = 0; col < 3; ++col)
            input.orientation[row * 3 + col] = s.R.m[row][col];
    input.gyro = {s.gyro.x, s.gyro.y, s.gyro.z};
    input.angle = {s.angle.x, s.angle.y, s.angle.z};
    input.acceleration = {s.acc.x, s.acc.y, s.acc.z};
    input.seconds = s.dt;
    input.pointerX = s.posX;
    input.pointerY = s.posY;
    input.roll = s.roll;
    input.pointerValid = s.pointerValid;
    g_capture.samples.push_back(input);
}

Sample RestoreMappedSample(const Riisorted::Netplay::MotionSample& input) {
    Sample s;
    s.hold = input.buttons;
    for (size_t row = 0; row < 3; ++row)
        for (size_t col = 0; col < 3; ++col)
            s.R.m[row][col] = input.orientation[row * 3 + col];
    s.gyro = {input.gyro[0], input.gyro[1], input.gyro[2]};
    s.angle = {input.angle[0], input.angle[1], input.angle[2]};
    s.acc = {input.acceleration[0], input.acceleration[1], input.acceleration[2]};
    s.dt = input.seconds;
    s.posX = input.pointerX;
    s.posY = input.pointerY;
    s.roll = input.roll;
    s.pointerValid = input.pointerValid;
    return s;
}

void LoadConventions() {
    if (const char* s = std::getenv("RESORT_MPLS_SIGNS")) {
        double a = 1, b = 1, c = 1;
        if (std::sscanf(s, "%lf,%lf,%lf", &a, &b, &c) == 3) CurrentRemote().conv.gyro[0] = a, CurrentRemote().conv.gyro[1] = b, CurrentRemote().conv.gyro[2] = c;
    }
    if (const char* s = std::getenv("RESORT_MPLS_DIRSIGN")) CurrentRemote().conv.dir = std::atof(s) < 0 ? -1 : 1;
}

void Announce() {
    RemoteScope local(g_local);
    if (CurrentRemote().announced) return;
    CurrentRemote().announced = true;
    LoadConventions();
    CurrentRemote().log = std::getenv("RESORT_WIIMOTE_LOG") != nullptr;
    std::fprintf(stderr,
                 "[wiimote] virtual Wii Remote + MotionPlus on channel 0 (keyboard/mouse or DualSense)\n"
                 "[wiimote]   pointer: mouse | A: left click / Z | B: right click / X | 1 / 2: 1 / 2\n"
                 "[wiimote]   +: Enter / = | -: Backspace / - | Home: H | D-pad: arrows / WASD\n"
                 "[wiimote]   tilt (MotionPlus): hold Left Shift or middle mouse and move the mouse, wheel rolls\n"
                 "[wiimote]   shake: Space | recenter: R\n");
}

SDL_Gamepad* DualSenseGamepad() {
    const int32_t index = PADGetIndexForPort(0);
    if (index < 0) return nullptr;
    SDL_Gamepad* pad = PADGetSDLGamepadForIndex(static_cast<uint32_t>(index));
    return pad && SDL_GetGamepadType(pad) == SDL_GAMEPAD_TYPE_PS5 &&
                   SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO) ? pad : nullptr;
}

bool VirtualRemoteActive(uint32_t chan) {
    if (Online::Active()) return chan < 2;
    if (Tape::Replaying()) return chan == 0;
    if (chan == 0 && Tape::Recording() && WiiRemoteInput::IsRemoteChannel(0))
        throw std::runtime_error("Motion recording currently supports the virtual remote only");
    // A real Bluetooth remote on the channel wins; the virtual one only stands in for channel 0.
    return chan == 0 && !WiiRemoteInput::IsRemoteChannel(0);
}

uint8_t RequestedMplsMode(uint32_t chan) {
    return Memory::Read8(guest::kKpadStateBase + chan * guest::kKpadStateStride + guest::kKpadMplsRequestedMode);
}

// ------------------------------------------------------------------------------------------------ host input
struct HostInput {
    bool focused = false;
    bool controller = false;
    uint32_t hold = 0;
    bool tilt = false, shake = false, recenter = false;
    Vec3 gyro;
    Vec3 acc;
    bool accValid = false;
    double mouseX = 0, mouseY = 0;   // -1..1, +y down
    double relX = 0, relY = 0;       // pixels since last read
    SDL_Window* window = nullptr;
    int w = 1, h = 1;
};

HostInput ReadHost() {
    HostInput in;
    in.window = SDL_GetKeyboardFocus();
    in.focused = in.window != nullptr && !InputBindings::InputBlocked();
    SDL_Gamepad* pad = DualSenseGamepad();
    const SDL_JoystickID padId = pad ? SDL_GetGamepadID(pad) : 0;
    if (padId != CurrentRemote().activeGamepad) {
        CurrentRemote().activeGamepad = padId;
        CurrentRemote().yaw = CurrentRemote().pitch = CurrentRemote().roll = 0;
        CurrentRemote().prevR = Mat3{};
        CurrentRemote().prevGyro = {};
        CurrentRemote().angle = {};
        CurrentRemote().easeLeft = 0;
        CurrentRemote().tilting = false;
        CurrentRemote().wheelSteps = 0;
        CurrentRemote().sensors.clear();
        CurrentRemote().sensorTime = 0;
        CurrentRemote().binTime = 0;
        CurrentRemote().binGyro = {};
        CurrentRemote().pointerPose = {};
        CurrentRemote().bias = {};
        CurrentRemote().recenterHeld = false;
        if (in.window) SDL_SetWindowRelativeMouseMode(in.window, false);
    }
    if (pad && !in.focused) {
        in.controller=true;
        CurrentRemote().sensors.clear();CurrentRemote().sensorTime=0;
        CurrentRemote().binTime=0;CurrentRemote().binGyro={};
        return in;
    }
    if (pad && in.focused) {
        in.controller = true;
        if (CurrentRemote().sensorEnabledFor != padId && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true)) {
            CurrentRemote().sensorEnabledFor = padId;
        }
        float gyro[3]{};
        if (SDL_GamepadSensorEnabled(pad, SDL_SENSOR_GYRO) &&
            SDL_GetGamepadSensorData(pad, SDL_SENSOR_GYRO, gyro, 3) &&
            std::isfinite(gyro[0]) && std::isfinite(gyro[1]) && std::isfinite(gyro[2]) &&
            std::max({std::fabs(gyro[0]), std::fabs(gyro[1]), std::fabs(gyro[2])}) < 30.0f) {
            const auto deadzone = [](float value) { return std::fabs(value) < 0.025f ? 0.0 : static_cast<double>(value); };
            in.gyro = {deadzone(gyro[0]), deadzone(gyro[1]), deadzone(gyro[2])};
        }
        if (SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL)) {
            if (CurrentRemote().accelEnabledFor != padId && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_ACCEL, true)) {
                CurrentRemote().accelEnabledFor = padId;
            }
            float acc[3]{};
            if (SDL_GetGamepadSensorData(pad, SDL_SENSOR_ACCEL, acc, 3) &&
                std::isfinite(acc[0]) && std::isfinite(acc[1]) && std::isfinite(acc[2])) {
                // SDL sensor axes are fixed to the pad, irrespective of tilt.
                // Map support force directly to KPAD; do not subtract a
                // baseline gravity vector or add simulated wrist acceleration.
                in.acc = ResortMotion::kpadAccel({acc[0], acc[1], acc[2]});
                in.accValid = Len(in.acc) > 0.2 && Len(in.acc) < 5.0;
            }
        }
        const auto button = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
        if (button(SDL_GAMEPAD_BUTTON_SOUTH)) in.hold |= kBtnA;
        if (button(SDL_GAMEPAD_BUTTON_EAST) ||
            SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 10000) in.hold |= kBtnB;
        if (button(SDL_GAMEPAD_BUTTON_WEST)) in.hold |= kBtn1;
        if (button(SDL_GAMEPAD_BUTTON_NORTH)) in.hold |= kBtn2;
        if (button(SDL_GAMEPAD_BUTTON_START)) in.hold |= kBtnPlus;
        if (button(SDL_GAMEPAD_BUTTON_BACK)) in.hold |= kBtnMinus;
        if (button(SDL_GAMEPAD_BUTTON_GUIDE)) in.hold |= kBtnHome;
        if (button(SDL_GAMEPAD_BUTTON_DPAD_UP)) in.hold |= kBtnUp;
        if (button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) in.hold |= kBtnDown;
        if (button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) in.hold |= kBtnLeft;
        if (button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) in.hold |= kBtnRight;
        in.shake = button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        in.recenter = button(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        return in;
    }
    float mx = 0, my = 0;
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(&mx, &my);
    float rx = 0, ry = 0;
    SDL_GetRelativeMouseState(&rx, &ry);
    if (!in.focused) return in;
    SDL_GetWindowSize(in.window, &in.w, &in.h);
    in.mouseX = std::clamp(2.0 * mx / std::max(1, in.w) - 1.0, -1.0, 1.0);
    in.mouseY = std::clamp(2.0 * my / std::max(1, in.h) - 1.0, -1.0, 1.0);
    in.relX = rx;
    in.relY = ry;
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    auto down = [&](SDL_Scancode s) { return keys && s < count && keys[s]; };
    for (const auto& b : kKeyBindings)
        if (down(b.key)) in.hold |= b.button;
    if (mouse & SDL_BUTTON_LMASK) in.hold |= kBtnA;
    if (mouse & SDL_BUTTON_RMASK) in.hold |= kBtnB;
    in.tilt = down(kKeyTilt) || (mouse & SDL_BUTTON_MMASK);
    in.shake = down(kKeyShake);
    in.recenter = down(kKeyRecenter);
    return in;
}

uint32_t StepController(const HostInput& in);
void ProcessMotionPlus(Sample& s, uint32_t channel = 0);

// ------------------------------------------------------------------------------------------------ simulation
// Advances the remote by one frame of host input and returns how many 200 Hz samples to emit.
uint32_t StepLive(uint32_t maxSamples) {
    Remote& r = CurrentRemote();
    const auto now = GuestClock::SchedulingNow();
    double dt = r.lastRead.time_since_epoch().count() == 0 ? 1.0 / 60.0
                                                           : std::chrono::duration<double>(now - r.lastRead).count();
    r.lastRead = now;
    dt = std::clamp(dt, 1.0 / kSampleHz, 0.1);
    const uint32_t n = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(dt * kSampleHz)), 1u, maxSamples);

    HostInput in = ReadHost();
    if (in.controller) return StepController(in);

    // Pointer / tilt target for the end of this frame.
    const double startYaw = r.yaw, startPitch = r.pitch, startRoll = r.roll;
    double endYaw = r.yaw, endPitch = r.pitch, endRoll = r.roll + r.wheelSteps * kRollDegPerWheelStep * kDeg;
    r.wheelSteps = 0;
    if (in.recenter) endYaw = endPitch = endRoll = 0;

    if (in.focused && in.tilt) {
        if (!r.tilting) {
            r.tilting = true;
            SDL_SetWindowRelativeMouseMode(in.window, true);
            in.relX = in.relY = 0;  // drop the jump from switching modes
        }
        endYaw += in.relX * kTiltDegPerPixel * kDeg;
        endPitch -= in.relY * kTiltDegPerPixel * kDeg;
    } else {
        if (r.tilting) {
            r.tilting = false;
            r.easeLeft = 0.25;
            if (in.window) {
                SDL_SetWindowRelativeMouseMode(in.window, false);
                // Put the OS cursor where the remote now points, so easing back causes no jump.
                const double px = std::tan(r.yaw) / std::tan(kHalfFovX), py = -std::tan(r.pitch) / std::tan(kHalfFovY);
                if (std::fabs(px) <= 1 && std::fabs(py) <= 1)
                    SDL_WarpMouseInWindow(in.window, static_cast<float>((px + 1) * 0.5 * in.w),
                                          static_cast<float>((py + 1) * 0.5 * in.h));
            }
        }
        if (in.focused) {
            // The pointer follows the mouse 1:1; only right after a tilt (aim may be off-screen) ease back so the
            // MotionPlus does not report a violent snap.
            const double targetYaw = std::atan(in.mouseX * std::tan(kHalfFovX));
            const double targetPitch = std::atan(-in.mouseY * std::tan(kHalfFovY));
            if (r.easeLeft > 0) {
                const double k = 1.0 - std::exp(-dt / kReturnTau);
                endYaw += std::remainder(targetYaw - endYaw, 2 * kPi) * k;
                endPitch += (targetPitch - endPitch) * k;
                r.easeLeft -= dt;
            } else {
                endYaw = targetYaw;
                endPitch = targetPitch;
            }
        }
    }
    // Limit mouse aim only; the physical pad uses an unrestricted quaternion.
    endPitch = std::clamp(endPitch, -kMaxPitch, kMaxPitch);
    endYaw = std::remainder(endYaw, 2 * kPi);
    endRoll = std::remainder(endRoll, 2 * kPi);

    if (in.shake && r.shakeStart < 0) r.shakeStart = r.simTime;
    if (!in.shake) r.shakeStart = -1;

    // Generate n samples across the frame, oldest first, then store newest first.
    std::array<Sample, 16> gen{};
    const double sdt = dt / n;
    for (uint32_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i + 1) / n;
        Sample s;
        s.hold = in.hold;
        const double yaw = startYaw + std::remainder(endYaw - startYaw, 2 * kPi) * t;
        const double pitch = startPitch + (endPitch - startPitch) * t;
        const double roll = startRoll + std::remainder(endRoll - startRoll, 2 * kPi) * t;
        s.R = Orientation(yaw, pitch, roll);
        s.roll = roll;
        s.gyro = AngularVelocity(r.prevR, s.R, sdt);
        r.simTime += sdt;

        // Accelerometer: gravity direction in the remote frame, minus the motion of the tip (lever along -Z).
        const Vec3 alpha = (s.gyro - r.prevGyro) * (1.0 / sdt);
        const Vec3 lever{0, 0, -kLeverM};
        const Vec3 aLin = Cross(alpha, lever) + Cross(s.gyro, Cross(s.gyro, lever));
        s.acc = Apply(Transpose(s.R), Vec3{0, -1, 0}) - aLin * (1.0 / kGravity);
        if (r.shakeStart >= 0) {
            const double ph = 2 * kPi * kShakeHz * (r.simTime - r.shakeStart);
            s.acc = s.acc + Vec3{0, kShakeG * std::sin(ph), 0.5 * kShakeG * std::sin(ph)};
        }

        // Pointer: where the aim ray crosses the screen plane.
        const Vec3 fwd = Column(s.R, 2) * -1.0;
        if (fwd.z < -0.2) {
            const double ax = std::atan2(fwd.x, -fwd.z), ay = std::atan2(fwd.y, -fwd.z);
            s.posX = std::tan(ax) / std::tan(kHalfFovX);
            s.posY = -std::tan(ay) / std::tan(kHalfFovY);
            s.pointerValid = std::fabs(s.posX) <= 1.15 && std::fabs(s.posY) <= 1.15;
        }
        r.angle = r.angle + s.gyro * (sdt / kMplsUnitRadPerSec);
        s.angle = r.angle;
        s.dt = sdt;
        CaptureMappedSample(s);
        if (!Online::Active() && RequestedMplsMode(0)) ProcessMotionPlus(s);
        AccumulateSolverResult(s);
        r.prevR = s.R;
        r.prevGyro = s.gyro;
        gen[i] = s;
    }
    r.yaw = endYaw;
    r.pitch = endPitch;
    r.roll = endRoll;
    for (uint32_t i = 0; i < n; ++i) r.recent[i] = gen[n - 1 - i];
    r.recentCount = n;
    return n;
}

// ------------------------------------------------------------------------------------------------ guest writes
void WriteVec3(uint32_t a, Vec3 v) {
    Memory::WriteFloat32(a, static_cast<float>(v.x));
    Memory::WriteFloat32(a + 4, static_cast<float>(v.y));
    Memory::WriteFloat32(a + 8, static_cast<float>(v.z));
}
void WriteVec2(uint32_t a, double x, double y) {
    Memory::WriteFloat32(a, static_cast<float>(x));
    Memory::WriteFloat32(a + 4, static_cast<float>(y));
}
void Zero(uint32_t a, uint32_t size) {
    for (uint32_t o = 0; o < size; o += 4) Memory::Write32(a + o, 0);
}

void WriteStatus(uint32_t a, const Sample& s, const Sample* older, bool mpls, uint32_t& prevHold) {
    Remote& r = CurrentRemote();
    Zero(a, guest::kStatusSize);
    Memory::Write32(a + guest::kHold, s.hold);
    Memory::Write32(a + guest::kTrig, s.hold & ~prevHold);
    Memory::Write32(a + guest::kRelease, prevHold & ~s.hold);
    prevHold = s.hold;

    WriteVec3(a + guest::kAcc, s.acc);
    Memory::WriteFloat32(a + guest::kAccValue, static_cast<float>(Len(s.acc)));
    Memory::WriteFloat32(a + guest::kAccSpeed, static_cast<float>(older ? Len(s.acc - older->acc) : 0.0));

    if (s.pointerValid) {
        const double vx = older && older->pointerValid ? s.posX - older->posX : 0;
        const double vy = older && older->pointerValid ? s.posY - older->posY : 0;
        WriteVec2(a + guest::kPos, s.posX, s.posY);
        WriteVec2(a + guest::kVec, vx, vy);
        Memory::WriteFloat32(a + guest::kSpeed, static_cast<float>(std::sqrt(vx * vx + vy * vy)));
        r.prevPosX = s.posX;
        r.prevPosY = s.posY;
    } else {
        WriteVec2(a + guest::kPos, r.prevPosX, r.prevPosY);
    }
    WriteVec2(a + guest::kHorizon, std::cos(s.roll), -std::sin(s.roll));
    Memory::WriteFloat32(a + guest::kDist, kDist);
    // acc_vertical: the gravity direction projected on the remote's Y/Z plane.
    WriteVec2(a + guest::kAccVertical, -s.acc.y, s.acc.z);

    Memory::Write8(a + guest::kDevType, mpls ? guest::kDevMpls : guest::kDevCore);
    Memory::Write8(a + guest::kWpadErr, 0);
    Memory::Write8(a + guest::kDpdValid, s.pointerValid ? 2 : 0);
    Memory::Write8(a + guest::kDataFormat, mpls ? guest::kFmtMpls : guest::kFmtCoreAccDpd);

    if (mpls && s.sdkMpls) {
        for (uint32_t i=0;i<s.mpls.size();++i) Memory::Write32(a+guest::kMpls+i*4,s.mpls[i]);
    } else if (mpls) {
        const Conventions& c = r.conv;
        const Vec3 rate = ToWii(s.gyro);
        const Vec3 angle = ToWii(s.angle);
        const Vec3 g{rate.x * c.gyro[0], rate.y * c.gyro[1], rate.z * c.gyro[2]};
        WriteVec3(a + guest::kMpls, g * (1.0 / kMplsUnitRadPerSec));
        WriteVec3(a + guest::kMplsAngle, Vec3{angle.x * c.gyro[0], angle.y * c.gyro[1], angle.z * c.gyro[2]});
        WriteVec3(a + guest::kMplsDirX, WiiDir(s.R, 0) * c.dir);
        WriteVec3(a + guest::kMplsDirY, WiiDir(s.R, 1) * c.dir);
        WriteVec3(a + guest::kMplsDirZ, WiiDir(s.R, 2) * c.dir);
    }
}

uint16_t RawAcc(double g) { return static_cast<uint16_t>(std::clamp(512.0 + g * 100.0, 0.0, 1023.0)); }

void WriteRaw(uint32_t a, const Sample& s, bool mpls) {
    Zero(a, guest::kRawSize);
    Memory::Write16(a + guest::kRawButton, static_cast<uint16_t>(s.hold & 0xFFFF));
    Memory::Write16(a + guest::kRawAcc + 0, RawAcc(-s.acc.x));
    Memory::Write16(a + guest::kRawAcc + 2, RawAcc(s.acc.z));
    Memory::Write16(a + guest::kRawAcc + 4, RawAcc(-s.acc.y));
    // Two sensor-bar dots on the IR camera (1024x768), mirrored like the real camera image.
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t o = a + guest::kRawObj + i * 8;
        if (i < 2 && s.pointerValid) {
            const double side = i == 0 ? -1.0 : 1.0;
            const double cx = 512.0 - s.posX * 384.0, cy = 384.0 - s.posY * 288.0;
            const double dx = side * 90.0 * std::cos(s.roll), dy = side * 90.0 * std::sin(s.roll);
            Memory::Write16(o + 0, static_cast<uint16_t>(std::clamp(cx + dx, 0.0, 1023.0)));
            Memory::Write16(o + 2, static_cast<uint16_t>(std::clamp(cy + dy, 0.0, 767.0)));
            Memory::Write16(o + 4, 4);
            Memory::Write8(o + 6, static_cast<uint8_t>(i));
        } else {
            Memory::Write16(o + 0, 0x3FF);
            Memory::Write16(o + 2, 0x3FF);
        }
    }
    Memory::Write8(a + guest::kRawDev, mpls ? guest::kDevMpls : guest::kDevCore);
    Memory::Write8(a + guest::kRawErr, 0);
    if (mpls) {
        const Vec3 wii = ResortMotion::wiiGyro(s.gyro);
        const auto pitch = ResortMotion::encode(wii.x);
        const auto yaw = ResortMotion::encode(wii.z);
        const auto roll = ResortMotion::encode(wii.y);
        // Unified WPAD flags are slow roll/pitch/yaw at bits 1/2/3.
        Memory::Write8(a + guest::kRawMpStat, 0x01 | (roll.slow?2:0) | (pitch.slow?4:0) | (yaw.slow?8:0));
        Memory::Write16(a + guest::kRawMpPitch, pitch.value);
        Memory::Write16(a + guest::kRawMpYaw, yaw.value);
        Memory::Write16(a + guest::kRawMpRoll, roll.value);
    }
    Memory::Write8(a + guest::kRawFmt, mpls ? guest::kFmtMpls : guest::kFmtCoreAccDpd);
}

// Run WSR's MotionPlus solver once per new sensor sample. Its work area is
// also used by the game's KPADResetMpls / direction and calibration functions.
void ProcessMotionPlus(Sample& s, uint32_t channel) {
    GuestInterruptCallbackContext call;
    CpuContext* ctx=call.get();
    if(!Memory::Contains(ctx->gpr[13]-15184,4))return;
    const uint32_t base=Memory::Read32(ctx->gpr[13]-15184);
    if (!base || channel >= 4) return;
    const uint32_t work=base + channel * 3504u;
    if(!Memory::Contains(work,3504) || !Memory::Contains(ctx->gpr[1]-0x1000,0x1000))return;
    const uint32_t scratch=ctx->gpr[1]-0x300;
    ctx->gpr[1]-=0x400;
    WriteRaw(scratch,s,true);
    Zero(scratch+0x40,0x80);
    WriteVec3(scratch+0x40+12,s.acc);
    // The virtual cursor is not an optical measurement of the physical pad:
    // do not let it revise the sports pose or recenter a swing behind the user.
    Memory::Write8(scratch+0x40+94,0);
    uint32_t old[3];
    for(int i=0;i<3;i++)old[i]=Memory::Read32(work+3488+i*4);
    Memory::Write32(work+3488,scratch);
    Memory::Write32(work+3492,scratch);
    Memory::Write32(work+3496,0);
    ctx->gpr[3]=channel;ctx->gpr[4]=scratch+0xc0;
    ctx->gpr[5]=2;ctx->gpr[6]=1;ctx->gpr[7]=scratch+0x40;
    ctx->fpr[1].d=s.dt;
    func_800CFCB0(ctx);
    for(int i=0;i<3;i++)Memory::Write32(work+3488+i*4,old[i]);
    for(uint32_t i=0;i<s.mpls.size();i++)s.mpls[i]=Memory::Read32(scratch+0xc0+i*4);
    s.sdkMpls=true;
}

uint32_t StepController(const HostInput& in) {
    Remote& r=CurrentRemote();
    if(in.recenter && !r.recenterHeld) {
        if (Tape::Recording() || Online::Active()) g_capture.recenter = true;
        r.pointerPose={};
        if (!Online::Active()) {
            GuestInterruptCallbackContext call;
            call.get()->gpr[3]=0;
            func_800CE180(call.get());
        }
    }
    r.recenterHeld=in.recenter;
    std::deque<Sample> generated;
    const auto emit=[&](Vec3 gyro,Vec3 acc,double dt) {
        Sample s;
        s.hold=in.hold;s.gyro=gyro;s.acc=acc;s.dt=dt;
        for(int i=0;i<3;i++) {
            const Vec3 axis=r.pointerPose.rotate(i==0?Vec3{1,0,0}:i==1?Vec3{0,1,0}:Vec3{0,0,1});
            s.R.m[0][i]=axis.x;s.R.m[1][i]=axis.y;s.R.m[2][i]=axis.z;
        }
        const Vec3 forward=r.pointerPose.rotate({0,0,-1});
        if(forward.z<-.2) {
            s.posX=forward.x/-forward.z/std::tan(kHalfFovX);
            s.posY=-forward.y/-forward.z/std::tan(kHalfFovY);
            s.pointerValid=std::abs(s.posX)<=1.15 && std::abs(s.posY)<=1.15;
        }
        s.roll=std::atan2(s.R.m[0][1],s.R.m[1][1]);
        r.angle=r.angle+gyro*(dt/kMplsUnitRadPerSec);s.angle=r.angle;
        CaptureMappedSample(s);
        if(!Online::Active() && RequestedMplsMode(0))ProcessMotionPlus(s);
        AccumulateSolverResult(s);
        generated.push_back(s);
        if(generated.size()>16)generated.pop_front();
    };
    while(!r.sensors.empty()) {
        const auto report=r.sensors.front();r.sensors.pop_front();
        if(r.sensorTime && report.time<=r.sensorTime)continue;
        double dt=r.sensorTime?static_cast<double>(report.time-r.sensorTime)*1e-9:0;
        r.sensorTime=report.time;
        if(dt<=0 || dt>.05) {r.binTime=0;r.binGyro={};continue;}
        const Vec3 gyro=r.bias.correct(report.gyro,report.acc,dt);
        while(dt>1e-9) {
            const double part=std::min(dt,1.0/kSampleHz-r.binTime);
            r.pointerPose.integrate(gyro,part);
            r.binGyro=r.binGyro+gyro*part;
            r.binTime+=part;dt-=part;
            if(r.binTime>=1.0/kSampleHz-1e-9) {
                emit(r.binGyro*(1.0/r.binTime),report.acc,r.binTime);
                r.binTime=0;r.binGyro={};
            }
        }
    }
    // No new sensor report means no new angular motion. Reusing the last
    // nonzero rate here causes the sword/pointer to keep rotating after a swing.
    if(generated.empty())emit({},in.accValid?in.acc:r.sensorAcc,1.0/kSampleHz);
    const uint32_t n=static_cast<uint32_t>(generated.size());
    for(uint32_t i=0;i<n;i++)r.recent[i]=generated[n-1-i];
    r.recentCount=n;
    return n;
}

uint32_t Step(uint32_t maxSamples) {
    g_solverHash = 14695981039346656037ull;
    if (Tape::Replaying()) {
        auto next = Tape::Replay(static_cast<uint64_t>(g_gxFrameCount), RequestedMplsMode(0));
        if (!next) {
            std::fprintf(stderr, "[riisorted] motion input playback completed\n");
            std::fflush(stderr);
            ExitForAuroraWindowClose();
        }
        const auto& batch = *next;
        if (batch.recenter) {
            GuestInterruptCallbackContext call;
            call.get()->gpr[3] = 0;
            func_800CE180(call.get());
        }
        std::deque<Sample> history;
        for (const auto& input : batch.samples) {
            Sample s = RestoreMappedSample(input);
            if (batch.motionPlusMode) ProcessMotionPlus(s);
            AccumulateSolverResult(s);
            history.push_back(s);
            if (history.size() > 16) history.pop_front();
        }
        CurrentRemote().recentCount = static_cast<uint32_t>(history.size());
        for (uint32_t i = 0; i < CurrentRemote().recentCount; ++i)
            CurrentRemote().recent[i] = history[history.size() - 1 - i];
        Tape::ObserveSolverResult(batch.sequence, g_solverHash);
        return CurrentRemote().recentCount;
    }
    if (Tape::Recording()) {
        g_capture = {};
        g_capture.sequence = CurrentRemote().batch;
        g_capture.interval = static_cast<uint64_t>(g_gxFrameCount);
        g_capture.motionPlusMode = RequestedMplsMode(0);
    }
    const uint32_t count = StepLive(maxSamples);
    if (Tape::Recording()) {
        Tape::Record(g_capture);
        Tape::ObserveSolverResult(g_capture.sequence, g_solverHash);
    }
    return count;
}

uint32_t OnlineSolverWork(uint32_t channel) {
    const auto* cpu = TryGetCpuContext();
    if(!cpu)cpu=&GetPersistentCpuContext();
    if(!Memory::Contains(cpu->gpr[13]-15184,4))return 0;
    const auto base=Memory::Read32(cpu->gpr[13]-15184);
    const auto work=base+channel*3504u;
    return base && Memory::Contains(work,3504)?work:0;
}
std::vector<uint8_t> CaptureOnlineSolverState(uint32_t channel) {
    const auto work=OnlineSolverWork(channel);std::vector<uint8_t> result;
    if(!work)return result;
    result.reserve(Riisorted::Netplay::kMotionPlusStateBytes);
    for(uint32_t i=0;i<3504;++i)if(i<3488 || i>=3500)result.push_back(Memory::Read8(work+i));
    return result;
}
void RestoreOnlineSolverState(uint32_t channel,const std::vector<uint8_t>& state) {
    if(state.empty())return;
    const auto work=OnlineSolverWork(channel);
    if(!work)throw std::runtime_error("Guest MotionPlus state is not initialized at the host's frame");
    size_t cursor=0;
    for(uint32_t i=0;i<3504;++i)if(i<3488 || i>=3500)Memory::Write8(work+i,state[cursor++]);
}

// Exchange once for both channels even when the game reads channel 1 first.
void EnsureOnlineFrame() {
    if (!Online::Active() || g_onlineFrame == g_gxFrameCount) return;
    Announce();
    g_capture = {};
    g_capture.sequence = g_onlineSequence;
    g_capture.interval = static_cast<uint64_t>(g_gxFrameCount);
    g_capture.channel = Online::LocalChannel();
    const std::array<uint8_t, 2> modes{RequestedMplsMode(0), RequestedMplsMode(1)};
    g_capture.motionPlusMode = modes[g_capture.channel];
    {
        RemoteScope local(g_local);
        StepLive(16);
    }
    const auto agreed = Online::Exchange(g_capture, modes, g_previousOnlineSolverHash);
    Riisorted::Netplay::MotionPlusReport report;
    report.sequence=g_onlineSequence;report.frame=static_cast<uint64_t>(g_gxFrameCount);report.modes=modes;
    const bool host=Online::LocalChannel()==0;
    if(!host) {
        report=Riisorted::Netplay::DecodeMotionPlusReport(Online::ShareMotionPlusReport());
        if(report.sequence!=g_onlineSequence || report.frame!=static_cast<uint64_t>(g_gxFrameCount) || report.modes!=modes)
            throw std::runtime_error("MotionPlus report frame/mode disagreement");
        for(unsigned channel=0;channel<2;++channel)
            if(report.samples[channel].size()!=agreed.players[channel].samples.size())
                throw std::runtime_error("MotionPlus report sample count disagrees with paired raw inputs");
    }
    g_solverHash = 14695981039346656037ull;
    for (uint32_t channel = 0; channel < 2; ++channel) {
        Remote& remote = g_onlineRemotes[channel];
        RemoteScope scope(remote);
        remote.conv = g_local.conv;
        const auto& batch = agreed.players[channel];
        if (batch.motionPlusMode != modes[channel])
            throw std::runtime_error("Online MotionPlus mode disagreement");
        if (batch.recenter) {
            GuestInterruptCallbackContext call;
            call.get()->gpr[3] = channel;
            func_800CE180(call.get());
        }
        std::deque<Sample> history;
        size_t sampleIndex=0;
        for (const auto& input : batch.samples) {
            Sample sample = RestoreMappedSample(input);
            if(host) {
                if (modes[channel]) ProcessMotionPlus(sample, channel);
                report.samples[channel].push_back({sample.sdkMpls,sample.mpls});
            } else {
                const auto& canonical=report.samples[channel][sampleIndex];
                sample.sdkMpls=canonical.valid;sample.mpls=canonical.words;
            }
            ++sampleIndex;
            AccumulateSolverResult(sample);
            history.push_back(sample);
            if (history.size() > 16) history.pop_front();
        }
        remote.recentCount = static_cast<uint32_t>(history.size());
        for (uint32_t i = 0; i < remote.recentCount; ++i)
            remote.recent[i] = history[history.size() - 1 - i];
        remote.stepFrame = g_gxFrameCount;
        ++remote.batch;
        if(host)report.states[channel]=CaptureOnlineSolverState(channel);
        else RestoreOnlineSolverState(channel,report.states[channel]);
    }
    if(host)Online::ShareMotionPlusReport(Riisorted::Netplay::EncodeMotionPlusReport(report));
    g_previousOnlineSolverHash = g_solverHash;
    g_onlineFrame = g_gxFrameCount;
    ++g_onlineSequence;
}

// ------------------------------------------------------------------------------------------------ public hooks
bool CursorShouldHide() {
    if (Tape::Replaying()) return false;
    return VirtualRemoteActive(0) && !DualSenseGamepad() && SDL_GetKeyboardFocus() != nullptr &&
           !InputBindings::InputBlocked();
}

bool DualSenseActive() { return VirtualRemoteActive(0) && DualSenseGamepad() != nullptr; }

const char* ActiveInputName() {
    if (Tape::Replaying()) return "Motion input playback";
    if (WiiRemoteInput::IsRemoteChannel(0)) return "Wii Remote (Bluetooth)";
    return DualSenseActive() ? "DualSense gyro" : "Keyboard and mouse";
}

void HandleSdlEvent(const SDL_Event& ev) {
    if (Tape::Replaying()) return;
    if (ev.type == SDL_EVENT_GAMEPAD_SENSOR_UPDATE && ev.gsensor.which == g_local.activeGamepad &&
        !InputBindings::InputBlocked()) {
        const auto& e = ev.gsensor;
        const Vec3 data{e.data[0],e.data[1],e.data[2]};
        if (std::isfinite(data.x) && std::isfinite(data.y) && std::isfinite(data.z)) {
            if(e.sensor == SDL_SENSOR_ACCEL && Len(data)<60) {
                g_local.sensorAcc = ResortMotion::kpadAccel(data);
                // SDL's HIDAPI reports gyro before accel at the same device timestamp.
                if(!g_local.sensors.empty() && g_local.sensors.back().time==e.sensor_timestamp)
                    g_local.sensors.back().acc=g_local.sensorAcc;
            } else if(e.sensor == SDL_SENSOR_GYRO && Len(data)<40) {
                if(g_local.sensors.size()==512)g_local.sensors.pop_front();
                g_local.sensors.push_back({e.sensor_timestamp,ResortMotion::bodyGyro(data),g_local.sensorAcc});
            }
        }
    }
    if (ev.type == SDL_EVENT_MOUSE_WHEEL && !DualSenseGamepad() && !InputBindings::InputBlocked()) {
        const float y = ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
        g_local.wheelSteps += y;
    }
    if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST && g_local.tilting) {
        g_local.tilting = false;
        if (SDL_Window* w = SDL_GetWindowFromID(ev.window.windowID)) SDL_SetWindowRelativeMouseMode(w, false);
    }
}

// WPADProbe for the virtual remote. Returns false when chan is not ours.
bool Probe(uint32_t chan, uint32_t typePtr, int32_t& result) {
    if (!VirtualRemoteActive(chan)) return false;
    Announce();
    if (typePtr) Memory::Write32(typePtr, RequestedMplsMode(chan) != 0 ? guest::kDevMpls : guest::kDevCore);
    result = 0;
    return true;
}

bool IsVirtualRemote(uint32_t chan) { return VirtualRemoteActive(chan); }

}  // namespace ResortWiimote

using namespace ResortWiimote;

// WPAD's calibration getter normally reads hardware control blocks that
// WPADInit HLE never creates. Supply the same signed calibration used by
// Dolphin: pitch/yaw scales decrease, roll increases, zero is 8192.
extern "C" void WPADGetMplsCalibration_HLE(uint32_t chan,uint32_t fast,uint32_t slow) {
    if(!VirtualRemoteActive(chan)) {
        if(chan>=4)return;
        const uint32_t control=Memory::Read32(0x807083D0+chan*4);
        if(fast && slow && control) {
            for(uint32_t i=0;i<28;i+=4) {
                Memory::Write32(fast+i,Memory::Read32(control+2208+i));
                Memory::Write32(slow+i,Memory::Read32(control+2236+i));
            }
        }
        return;
    }
    for(int b=0;b<2;b++) {
        uint32_t out=b?slow:fast;
        if(!out)continue;
        for(int axis=0;axis<3;axis++) {
            Memory::WriteFloat32(out+axis*8,8192);
            Memory::WriteFloat32(out+axis*8+4,axis==2?4352:-4352);
        }
        Memory::Write32(out+24,b?270:1200);
    }
}
PPC_NATIVE_OVERRIDE_VOID(80025830, WPADGetMplsCalibration_HLE,
                         (uint32_t chan,uint32_t fast,uint32_t slow),(chan,fast,slow));

// Complete the virtual MotionPlus activation/calibration command immediately.
// Without its callback KPAD remains in the short startup bias-learning phase,
// which absorbs a sustained swing after about 225 ms.
extern "C" int32_t WPADControlMpls_HLE(uint32_t chan,uint32_t mode,uint32_t callback) {
    (void)mode;
    const int32_t result=VirtualRemoteActive(chan)?0:-1;
    if(callback && TranslatedFunctionRegistry::FindByAddressPtr(callback)) {
        GuestInterruptCallbackContext call;
        call.get()->gpr[3]=chan;call.get()->gpr[4]=static_cast<uint32_t>(result);
        InvokeIndirectCpu(callback,call.get());
    }
    return result;
}
PPC_NATIVE_OVERRIDE(800254C0, WPADControlMpls_HLE, int32_t,
                    (uint32_t chan,uint32_t mode,uint32_t callback),(chan,mode,callback));

// KPADReadEx(chan, KPADStatus* buf, u32 len, s32* err, BOOL keepLastOnNoData) -> number of samples
extern "C" int32_t KPADReadEx_HLE(uint32_t chan, uint32_t buf, uint32_t len, uint32_t errPtr, uint32_t keepLast) {
    RemoteScope remote(Online::Active() && chan < 2 ? g_onlineRemotes[chan] : g_local);
    (void)keepLast;
    int32_t err = guest::kKpadErrNone;
    int32_t written = 0;
    try {
        if (!VirtualRemoteActive(chan) || buf == 0 || len == 0) {
            err = guest::kKpadErrNoController;
            if (buf != 0 && len != 0) {
                Zero(buf, guest::kStatusSize);
                Memory::Write8(buf + guest::kDevType, 0xFD);   // WPAD_DEV_NOT_FOUND, as KPAD does
                Memory::Write8(buf + guest::kWpadErr, 0xFF);
            }
        } else {
            Announce();
            const bool mpls = RequestedMplsMode(chan) != 0;
            // Several readers poll KPAD every frame (EGG::CoreController at 0x801C4728 and the game's own
            // controller at 0x802412F8, each with its own buffer). Advance the remote once per frame and hand every
            // reader the same samples; button edges are tracked per reader buffer so each one sees the press.
            if (Online::Active()) EnsureOnlineFrame();
            else if (CurrentRemote().recentCount == 0 || CurrentRemote().stepFrame != g_gxFrameCount) {
                Step(16);
                CurrentRemote().stepFrame = g_gxFrameCount;
                ++CurrentRemote().batch;
            }
            if (CurrentRemote().readerBatch[buf] == CurrentRemote().batch) {
                if (errPtr) Memory::Write32(errPtr, static_cast<uint32_t>(-1));
                return 0;
            }
            CurrentRemote().readerBatch[buf] = CurrentRemote().batch;
            const uint32_t n = std::min<uint32_t>(std::min<uint32_t>(len, 16), CurrentRemote().recentCount);
            // Buttons are sampled once per host frame, so every sample of this read has the same hold. The game
            // (0x80241670) takes hold/trig/release from status[0] only, so the edge for this frame goes on the
            // newest sample; the older ones carry hold with no edges (no double presses for code that ORs them).
            uint32_t& readerPrevHold = CurrentRemote().prevHoldByBuffer[buf];
            for (int32_t i = static_cast<int32_t>(n) - 1; i >= 0; --i) {
                const Sample* older = (i + 1 < static_cast<int32_t>(n)) ? &CurrentRemote().recent[i + 1] : nullptr;
                uint32_t edgeBase = i == 0 ? readerPrevHold : CurrentRemote().recent[i].hold;
                WriteStatus(buf + static_cast<uint32_t>(i) * guest::kStatusSize, CurrentRemote().recent[i], older, mpls,
                            edgeBase);
            }
            if (CurrentRemote().log && CurrentRemote().recent[0].hold != readerPrevHold) {
                std::fprintf(stderr, "[wiimote] read buf=0x%08X n=%u hold 0x%04X -> 0x%04X mpls=%d ptr=(%.2f,%.2f)%s\n",
                             buf, n, readerPrevHold, CurrentRemote().recent[0].hold, mpls ? 1 : 0, CurrentRemote().recent[0].posX,
                             CurrentRemote().recent[0].posY, CurrentRemote().recent[0].pointerValid ? "" : " offscreen");
            }
            readerPrevHold = CurrentRemote().recent[0].hold;
            // KPAD keeps a copy of the newest status at the start of its channel state.
            const uint32_t state = guest::kKpadStateBase + chan * guest::kKpadStateStride;
            for (uint32_t o = 0; o < guest::kStatusSize; o += 4) Memory::Write32(state + o, Memory::Read32(buf + o));
            written = static_cast<int32_t>(n);
        }
        if (errPtr) Memory::Write32(errPtr, static_cast<uint32_t>(err));
    } catch (const Memory::AccessViolation&) {
        return 0;
    }
    return written;
}
PPC_NATIVE_OVERRIDE(800CB890, KPADReadEx_HLE, int32_t,
                    (uint32_t chan, uint32_t buf, uint32_t len, uint32_t errPtr, uint32_t keepLast),
                    (chan, buf, len, errPtr, keepLast));

// KPAD raw sample copy (KPADGetUnifiedWpadStatus): count 0x40-byte samples, newest first.
extern "C" void KPADGetUnifiedWpadStatus_HLE(uint32_t chan, uint32_t dst, uint32_t count) {
    RemoteScope remote(Online::Active() && chan < 2 ? g_onlineRemotes[chan] : g_local);
    if (Online::Active() && chan < 2) EnsureOnlineFrame();
    try {
        count = std::min<uint32_t>(count, 16);
        const bool active = VirtualRemoteActive(chan);
        const bool mpls = active && RequestedMplsMode(chan) != 0;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t a = dst + i * guest::kRawSize;
            if (!active) {
                Zero(a, guest::kRawSize);
                Memory::Write8(a + guest::kRawDev, 0xFD);
                Memory::Write8(a + guest::kRawErr, 0xFF);  // WPAD_ERR_NO_CONTROLLER
                continue;
            }
            const uint32_t idx = std::min(i, CurrentRemote().recentCount ? CurrentRemote().recentCount - 1 : 0);
            WriteRaw(a, CurrentRemote().recent[idx], mpls);
        }
    } catch (const Memory::AccessViolation&) {
    }
}
PPC_NATIVE_OVERRIDE_VOID(800CD620, KPADGetUnifiedWpadStatus_HLE, (uint32_t chan, uint32_t dst, uint32_t count),
                         (chan, dst, count));

// WPAD MotionPlus mode query (reads the control block's dev/mode on hardware). The virtual MotionPlus switches
// instantly to whatever mode KPADEnableMpls asked for.
extern "C" uint32_t WPADGetMplsStatus_HLE(uint32_t chan) {
    if (!VirtualRemoteActive(chan)) return 0;
    try {
        return RequestedMplsMode(chan);
    } catch (const Memory::AccessViolation&) {
        return 0;
    }
}
PPC_NATIVE_OVERRIDE(800242E0, WPADGetMplsStatus_HLE, uint32_t, (uint32_t chan), (chan));

// WPADIsDpdEnabled: the HLE'd WPADInit never builds control blocks for the translated getter to read.
extern "C" uint32_t WPADIsDpdEnabled_HLE(uint32_t chan) { return VirtualRemoteActive(chan) ? 1u : 0u; }
PPC_NATIVE_OVERRIDE(80022F90, WPADIsDpdEnabled_HLE, uint32_t, (uint32_t chan), (chan));

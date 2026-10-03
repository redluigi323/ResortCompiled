# DualSense motion bridge

## References

The sensor conventions and transport were checked against primary sources:

- [SDL3 sensor units and fixed device axes](https://github.com/libsdl-org/SDL/blob/main/include/SDL3/SDL_sensor.h).
- [Dolphin SDL directional mappings](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/InputCommon/ControllerInterface/SDL/SDLGamepad.h).
- [Dolphin physical gyro calibration](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/InputCommon/ControllerEmu/ControlGroup/IMUGyroscope.cpp).
- [Dolphin MotionPlus encoding and calibration](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/WiimoteEmu/MotionPlus.cpp), [ranges](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/WiimoteEmu/MotionPlus.h).

The implementation uses WSR's own translated KPAD processing. No Dolphin source
was copied, and its emulation polling loop is not used.

## Frames and samples

For the user's normal grip (DualSense face up, triggers pointing forward):

| Quantity | SDL to virtual remote |
|---|---|
| Body gyro (X right, Y out of face, Z towards player) | `(x, z, -y)` |
| Physical Wii gyro (X left, Y towards player, Z out of face) | `(-x, -y, z)` |
| KPAD acceleration in g | `(x, -z, -y) / 9.80665` |

These are fixed device mappings. Tilting the controller does not swap axes.
The pointer integrates body rates with quaternion multiplication on the right.
It never generates gyro rates by differentiating Euler angles.

SDL sensor timestamps drive integration. Consecutive reports are accumulated
into 5 ms intervals for KPAD, which accepts at most 16 samples per read. Longer
batches are all processed through the SDK before returning their newest 16.
The physical accelerometer supplies gravity and movement directly; simulated
wrist acceleration, gravity subtraction, and amplified peak replacement are
removed from this path. Mouse input still synthesizes a remote's movement.

A stationary bias estimate requires low, stable gyro rates and approximately
one g. It does not learn high rates during a swing. R3 resets the pointer and
KPAD direction reference; the game's own calibration/reset calls also take effect.

## Guest MotionPlus

The physical samples are encoded as WPAD's 14-bit MotionPlus values, centred
at 8192, with slow/fast flags and matching signed calibration. Calibration
uses 4352 counts at 270/1200 degrees per second, matching Dolphin's constants.
KPAD rates are turns per second, not radians per second; the SDK performs its
internal integer conversion before reporting these rates.

The bridge calls translated `0x800CFCB0` with a private CPU register context and
scratch memory below the interrupted guest stack. Its channel work pointer
comes from `r13 - 15184`. Rate, angle and direction output comes from the same
work area used by KPAD configuration and reset calls. Each sample stores its
own resulting 60-byte MotionPlus block.

The virtual `WPADGetMplsCalibration` supplies the calibration normally read
from Bluetooth hardware. `WPADControlMpls` completes its virtual-device callback
successfully. Without that callback, an isolated SDK test stayed in startup
bias calibration: a constant 90 degrees/second rotation became zero after
roughly 225 ms. With completion, its rate remains approximately 0.25 turns/sec,
and half a second integrates to 0.125 turns.

IR revision does not use the simulated cursor as a physical optical measurement.
Acceleration revision and explicit game direction revision remain under the
SDK's controls. A batch advances once per rendered guest frame, and each
reader buffer consumes it once. Multiple readers share the same computed data.

## Verification

After building, run `python3 scripts/test_motion.py`.

This tests axis signs, raw encoding, bias rejection of swings, quaternion
normalization, and calls the actual translated SDK to check calibration
completion, sustained and reverse rotations, repeated swings, and duplicate
KPAD reads. It loads the local extracted DOL constants and starts no renderer.

These checks establish the mathematical and guest ABI behavior. Controller
feel, swordplay's sport-specific behavior, and pointing with a different grip
still require a physical gameplay run. Real Bluetooth MotionPlus transport
is not implemented by this virtual-device bridge.

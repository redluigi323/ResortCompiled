// Resortcompiled: GX FIFO breakpoint HLE.
#include <cstdint>
#include <cstdio>

#include "hle_stubs.h"
#include "ppc_runtime.h"

// WSR's EGG::ProcessMeter (the CPU/GPU perf meter behind EGG::CpuGpMonitor) arms a GP FIFO breakpoint at the
// current write pointer each frame so the CP interrupt tells it when the GPU got there. Mario Kart Wii never
// uses this, so WiiCompiled had these as fatal stubs. Aurora has no GP FIFO to break on; the breakpoint
// simply never fires and the meter reads zero, which only affects the (hidden) debug meter.
// GXSetBreakPtCallback itself only swaps a global and is left to the translated code.
static bool g_loggedBreakPt = false;

extern "C" void GXEnableBreakPt_HLE(uint32_t fifoPtr) {
    if (!g_loggedBreakPt) {
        g_loggedBreakPt = true;
        std::fprintf(stderr, "[gx] GXEnableBreakPt(0x%08X) ignored - no GP FIFO breakpoints (EGG::ProcessMeter)\n",
                     fifoPtr);
    }
}
PPC_NATIVE_OVERRIDE_VOID(80033620, GXEnableBreakPt_HLE, (uint32_t fifoPtr), (fifoPtr));

extern "C" void GXDisableBreakPt_HLE() {}
PPC_NATIVE_OVERRIDE_VOID(800336C0, GXDisableBreakPt_HLE, (), ());

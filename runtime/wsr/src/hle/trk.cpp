#include "hle_stubs.h"
#include "runtime_log.h"
#include <cstdint>
#include <cstdio>

extern "C" uint32_t Stub_8001B408(uint32_t ctx, int code)
{
    RT_LOGF(RT_TAG_HLE, "Stub_8001B408(0x%08x, %d)\n", ctx, code);
    (void)ctx;
    (void)code;
    return 0;
}

PPC_NATIVE_OVERRIDE(8001B408, Stub_8001B408, uint32_t, (uint32_t ctx, int code), (ctx, code));

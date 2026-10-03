// gx_init.cpp - GX Initialization and FIFO Management
#include "gx_internal.h"
#include "runtime_log.h"

#include <cstdio>

// Forward declarations for HLE functions used by GXInit
extern "C" void __GX__FifoInit_80033710();
extern "C" void __GX__PEInit_800354e0();
extern "C" void __GX__SetTmemConfig_80037D70(uint32_t mode);
extern "C" void GX__InitFifoBase_80032d10(uint32_t fa, uint32_t ba, uint32_t s);
// GX__SetCPUFifo_80032ea0 is already declared in gx_internal.h.
extern "C" void GX__SetGPFifo_80033080(uint32_t fa);
extern "C" GXFifoObj* GXInit(void* base, u32 size);

// ============================================================================
// GXInit
// ============================================================================


/**
 * GXInit HLE - Initialize the Graphics subsystem
 * This replaces the translated function that writes to MMIO addresses.
 */
extern "C" uint32_t GX__Init_80031de0(uint32_t fifoBase, uint32_t fifoSize)
{
    constexpr uint32_t kFifoObjAddr = 0x8070C490u;
    constexpr uint32_t kGXDataAddr = 0x8070C510u;
    constexpr uint32_t kGXDataSize = 0x600u;

    GXInit(GuestToHostPtr(fifoBase, fifoSize), fifoSize);

    // Initialize GXData structure in guest memory
    try {
        for (uint32_t offset = 0; offset < kGXDataSize; offset += 4) {
            Memory::Write32(kGXDataAddr + offset, 0);
        }
        Memory::Write8(kGXDataAddr + 0x5f8, 0);
        Memory::Write8(kGXDataAddr + 0x5f9, 1);
        Memory::Write8(kGXDataAddr + 0x5fa, 1);
        Memory::Write32(kGXDataAddr + 0x5e4, 0);
        Memory::Write32(kGXDataAddr + 0x5e8, 0);
        Memory::Write32(kGXDataAddr + 0x5fc, 0);
        Memory::Write32(kGXDataPtrAddr, kGXDataAddr);
        // GXData+0x5F8 was just cleared; drop any stale recording shadow with it.
        BeginDisplayListRecording(0, 0);
    } catch (const ::Memory::AccessViolation& e) {
        RT_LOGF(RT_TAG_GX, "Memory access violation during GXData init: %s\n", e.what());
    }

    __GX__FifoInit_80033710();
    GX__InitFifoBase_80032d10(kFifoObjAddr, fifoBase, fifoSize);
    GX__SetCPUFifo_80032ea0(kFifoObjAddr);
    GX__SetGPFifo_80033080(kFifoObjAddr);
    __GX__PEInit_800354e0();

    try {
        uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            Memory::Write32(gd + 0x254, 0);
            Memory::Write32(gd + 0x174, 0x0f0000ff);
            Memory::Write32(gd + 0x7c, 0x22000000);
            Memory::Write32(gd + 0x170, 0x27000000);
            const uint32_t baseRegs[] = {0x30, 0x38};
            for (int i = 0; i < 2; ++i) {
                uint32_t r = baseRegs[i];
                Memory::Write32(gd + 0x108 + i * 0x10, r << 24);
                Memory::Write32(gd + 0x128 + i * 0x10, (r + 1) << 24);
                Memory::Write32(gd + 0x10c + i * 0x10, (r + 2) << 24);
                Memory::Write32(gd + 0x12c + i * 0x10, (r + 3) << 24);
                Memory::Write32(gd + 0x110 + i * 0x10, (r + 4) << 24);
                Memory::Write32(gd + 0x130 + i * 0x10, (r + 5) << 24);
                Memory::Write32(gd + 0x114 + i * 0x10, (r + 6) << 24);
                Memory::Write32(gd + 0x134 + i * 0x10, (r + 7) << 24);
            }
        }
    } catch (...) {}

    __GX__SetTmemConfig_80037D70(2);

    RT_LOGF(RT_TAG_GX, "GX initialized, FIFO at 0x%08X (base=0x%08X size=0x%08X)\n",
            kFifoObjAddr, fifoBase, fifoSize);

    return kFifoObjAddr;
}
PPC_NATIVE_OVERRIDE(80031de0, GX__Init_80031de0, uint32_t, (uint32_t fifoBase, uint32_t fifoSize), (fifoBase, fifoSize));

// ============================================================================
// FIFO Management
// ============================================================================

extern "C" void GX__InitFifoBase_80032d10(uint32_t fa, uint32_t ba, uint32_t s) { GXInitFifoBase((GXFifoObj*)GuestToHostPtr(fa, sizeof(GXFifoObj)), GuestToHostPtr(ba, s), s); }

extern "C" void GX__SetCPUFifo_80032ea0(uint32_t fa) { GXSetCPUFifo((GXFifoObj*)GuestToHostPtr(fa, sizeof(GXFifoObj))); }
PPC_NATIVE_OVERRIDE_VOID(80032ea0, GX__SetCPUFifo_80032ea0, (uint32_t fa), (fa));

extern "C" void GX__SetGPFifo_80033080(uint32_t fa) { GXSetGPFifo((GXFifoObj*)GuestToHostPtr(fa, sizeof(GXFifoObj))); }
PPC_NATIVE_OVERRIDE_VOID(80033080, GX__SetGPFifo_80033080, (uint32_t fa), (fa));

extern "C" void __GX__SaveFifo_80033310(uint32_t fa) { GXSaveCPUFifo((GXFifoObj*)GuestToHostPtr(fa, sizeof(GXFifoObj))); }
PPC_NATIVE_OVERRIDE_VOID(80033310, __GX__SaveFifo_80033310, (uint32_t fa), (fa));

extern "C" void GX__GetCPUFifo_80033470(uint32_t fa) { auto* d=(GXFifoObj*)GuestToHostPtr(fa, sizeof(GXFifoObj)); auto* s=GXGetCPUFifo(); if(d&&s) std::memcpy(d,s,sizeof(GXFifoObj)); }
PPC_NATIVE_OVERRIDE_VOID(80033470, GX__GetCPUFifo_80033470, (uint32_t fa), (fa));

extern "C" void __GX__FifoInit_80033710()
{
    constexpr uint32_t kCpInterruptId = 0x11u;
    constexpr uint32_t kCpInterruptMask = 0x4000u;
    constexpr uint32_t kCpInterruptHandlerAddr = 0x80032bb0u;
    constexpr uint32_t kGxCurrentThreadPtrAddr = 0x806f4644u;
    constexpr uint32_t kGxThreadQueueAddr = 0x806f4640u;
    constexpr uint32_t kCpuFifoObjAddr = 0x8070cb34u;
    constexpr uint32_t kGpFifoObjAddr = 0x8070cb10u;
    constexpr size_t kFifoObjSize = 0x24u;
    constexpr uint32_t kFifoWrapFlagAddr = 0x806f4630u;
    constexpr uint32_t kFifoWrapFlag2Addr = 0x806f4631u;

    __OSSetInterruptHandler_8004af80_hle(kCpInterruptId, kCpInterruptHandlerAddr);
    __OSUnmaskInterrupts_8004b360_hle(kCpInterruptMask);

    try { Memory::Write32(kGxCurrentThreadPtrAddr, OS__GetCurrentThread_8004dcc0_hle()); } catch (const ::Memory::AccessViolation&) {}
    try { Memory::Write32(kGxThreadQueueAddr, 0); } catch (const ::Memory::AccessViolation&) {}

    auto zeroBlock = [](uint32_t addr, size_t sizeBytes) {
        for (size_t offset = 0; offset < sizeBytes; offset += 4) {
            Memory::Write32(addr + static_cast<uint32_t>(offset), 0);
        }
    };

    try { zeroBlock(kCpuFifoObjAddr, kFifoObjSize); } catch (const ::Memory::AccessViolation&) {}
    try { zeroBlock(kGpFifoObjAddr, kFifoObjSize); } catch (const ::Memory::AccessViolation&) {}

    try { Memory::Write8(kFifoWrapFlagAddr, 0); } catch (const ::Memory::AccessViolation&) {}
    try { Memory::Write8(kFifoWrapFlag2Addr, 0); } catch (const ::Memory::AccessViolation&) {}
}

extern "C" void __GX__PEInit_800354e0()
{
    constexpr uint32_t kPeTokenInterruptId = 0x12u;
    constexpr uint32_t kPeFinishInterruptId = 0x13u;
    constexpr uint32_t kPeTokenInterruptMask = 0x1000u;
    constexpr uint32_t kPeFinishInterruptMask = 0x2000u;
    constexpr uint32_t kPeTokenHandlerAddr = 0x80035380;
    constexpr uint32_t kPeFinishHandlerAddr = 0x80035460;
    constexpr uint32_t kPeThreadQueueAddr = 0x806f4650u;

    __OSSetInterruptHandler_8004af80_hle(kPeTokenInterruptId, kPeTokenHandlerAddr);
    __OSSetInterruptHandler_8004af80_hle(kPeFinishInterruptId, kPeFinishHandlerAddr);
    __OSUnmaskInterrupts_8004b360_hle(kPeTokenInterruptMask);
    __OSUnmaskInterrupts_8004b360_hle(kPeFinishInterruptMask);

    try { Memory::Write32(kPeThreadQueueAddr, 0); } catch (const ::Memory::AccessViolation&) {}
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            const uint16_t cur = Memory::Read16(gd + 0x0Au);
            Memory::Write16(gd + 0x0Au, static_cast<uint16_t>(cur | 0x000Fu));
        }
    } catch (const ::Memory::AccessViolation&) {}
}
PPC_NATIVE_OVERRIDE_VOID(800354e0, __GX__PEInit_800354e0, (), ());

// ============================================================================
// Display List Recording
// ============================================================================

extern "C" void GX__BeginDisplayList_800397a0(uint32_t la, uint32_t s) {
    try {
        uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (!gd) return;
        if (Memory::Read32(gd + 0x5FCu)) GX__SetDirtyState_80035550();
        if (Memory::Read8(gd + 0x5F9u)) std::memcpy(Memory::GetPointer(0x8070CE60, 0x600), Memory::GetPointer(gd, 0x600), 0x600);
        Memory::Write32(0x8070CDE4, la + s - 4u);
        Memory::Write32(0x8070CDFC, 0);
        Memory::Write32(0x8070CDE0, la);
        Memory::Write32(0x8070CDE8, s);
        Memory::Write32(0x8070CDF4, la);
        Memory::Write32(0x8070CDF8, la);
        Memory::Write8(gd + 0x5F8u, 1u);
        // Mirror the guest fifo-object fields the FIFO write path consumes so
        // HleFifoWrite never has to read them back out of guest memory.
        BeginDisplayListRecording(la, s);
        GXFlush();
        GX__GetCPUFifo_80033470(0x8070D460);
        GX__SetCPUFifo_80032ea0(0x8070CDE0);
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(800397a0, GX__BeginDisplayList_800397a0, (uint32_t la, uint32_t s), (la, s));

extern "C" uint32_t GX__EndDisplayList_80039860() {
    try {
        GXFlush();
        GX__GetCPUFifo_80033470(0x8070CDE0);
        const uint8_t wrapped = Memory::Read8(kDlFifoAddr + kDlWrapFlagOffset);
        GX__SetCPUFifo_80032ea0(0x8070D460);

        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            if (Memory::Read8(gd + 0x5F9u) != 0) {
                const int32_t interruptLevel = OS__DisableInterrupts_8004af10();
                const uint32_t savedWord8 = Memory::Read32(gd + 0x08u);
                std::memcpy(Memory::GetPointer(gd, 0x600),
                            Memory::GetPointer(0x8070CE60, 0x600),
                            0x600);
                Memory::Write32(gd + 0x08u, savedWord8);
                OS__RestoreInterrupts_8004af50(interruptLevel);
            }
            Memory::Write8(gd + 0x5F8u, 0);
        }

        // Publish the cached cursor/count before the count is read back below.
        // Done unconditionally: the original read of GXData+0x5F8 returned
        // "inactive" whenever the GXData pointer was null, so recording must
        // stop here even on the gd == 0 path.
        EndDisplayListRecording();

        return wrapped == 0 ? Memory::Read32(kDlCountAddr) : 0;
    } catch (...) {
        // Leaving the shadow active would silently swallow every subsequent
        // FIFO write into a dead list.
        EndDisplayListRecording();
        return 0;
    }
}
PPC_NATIVE_OVERRIDE(80039860, GX__EndDisplayList_80039860, uint32_t, (), ());

// ============================================================================
// Flush
// ============================================================================

extern "C" void GX__Flush_80034c50() { GXFlush(); }
PPC_NATIVE_OVERRIDE_VOID(80034c50, GX__Flush_80034c50, (), ());

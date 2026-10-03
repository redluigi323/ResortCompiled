#include "gx_internal.h"
#include "runtime_log.h"

extern "C" void __GXSetSUTexRegs();

// ============================================================================
// FIFO Write Helpers
// ============================================================================

extern "C" void GX_HLE_FIFO_WriteFloat(float val) {
    u32 raw; std::memcpy(&raw, &val, 4);
    try { HleFifoWrite(raw, 4); } catch (...) { RT_LOGF(RT_TAG_GX, "FIFO write float failed\n"); }
}

extern "C" void GX_HLE_FIFO_Write32(uint32_t val) { HleFifoWrite(val, 4); }
extern "C" void GX_HLE_FIFO_Write16(uint16_t val) { HleFifoWrite(static_cast<u32>(val), 2); }
extern "C" void GX_HLE_FIFO_Write8(uint8_t val) { HleFifoWrite(static_cast<u32>(val), 1); }

extern "C" void GX__SetDrawSync_800353bc(uint32_t token) {
    (void)token;
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) {
        if (Memory::Read32(gd + 0x5FCu)) GX__SetDirtyState_80035550();
        Memory::Write16(gd + 2, 0);
    } } catch (...) {}
}

extern "C" void GX__SetDrawSync_80035020(uint32_t token) { GX__SetDrawSync_800353bc(token); }
PPC_NATIVE_OVERRIDE_VOID(80035020, GX__SetDrawSync_80035020, (uint32_t token), (token));

extern "C" void GX__FinishInterruptHandler_80035460() {
    try {
        uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) Memory::Write16(gd + 0x0Au, static_cast<uint16_t>(Memory::Read16(gd + 0x0Au) | 0x0008u));
        Memory::Write8(kGxDrawDoneFlagAddr, 1);
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80035460, GX__FinishInterruptHandler_80035460, (), ());

extern "C" void GX__DrawDone_800350e0() {
    try { Memory::Write8(kGxDrawDoneFlagAddr, 0); } catch (...) {}
    GXDrawDone(); GX__FinishInterruptHandler_80035460();
}
PPC_NATIVE_OVERRIDE_VOID(800350e0, GX__DrawDone_800350e0, (), ());

extern "C" void GX__PixModeSync_800351b0() {
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write16(gd + 2, 0); } catch (...) {}
    GXPixModeSync();
}
PPC_NATIVE_OVERRIDE_VOID(800351b0, GX__PixModeSync_800351b0, (), ());

// ============================================================================
// Hardware Revision / Thread Query - No-ops
// ============================================================================

extern "C" void __GX__InitRevisionBits_80031cb0() {}
PPC_NATIVE_OVERRIDE_VOID(80031cb0, __GX__InitRevisionBits_80031cb0, (), ());

// ============================================================================
// Texture State Management - Aurora handles internally
// ============================================================================

extern "C" void __GX__SetSUTexRegs_80037c00() {
    __GXSetSUTexRegs();
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write16(gd + 2, 0); } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80037c00, __GX__SetSUTexRegs_80037c00, (), ());

extern "C" void __GX__SetTmemConfig_80037D70(uint32_t mode) {
    // TMEM layout configuration - Aurora manages internally
    (void)mode;
}
PPC_NATIVE_OVERRIDE_VOID(80037D70, __GX__SetTmemConfig_80037D70, (uint32_t mode), (mode));

extern "C" void __GX__FlushTextureState_80038550() {
    // BP texture state flush - Aurora handles via API
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write16(gd + 2, 0); } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80038550, __GX__FlushTextureState_80038550, (), ());

// ============================================================================
// Copy Configuration - No-ops for features Aurora doesn't use
// ============================================================================

extern "C" void GX__SetDispCopyFrame2Field_80035d40(uint32_t f) {
    GXSetDispCopyFrame2Field(f);
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            Memory::Write32(gd + 0x23Cu, (Memory::Read32(gd + 0x23Cu) & 0xFFFFCFFFu) | ((f & 3u) << 12));
            Memory::Write32(gd + 0x24Cu, Memory::Read32(gd + 0x24Cu) & 0xFFFFCFFFu);
        }
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80035d40, GX__SetDispCopyFrame2Field_80035d40, (uint32_t f), (f));

extern "C" void GX__SetCopyClamp_80035d60(uint32_t c) {
    GXSetCopyClamp(static_cast<GXFBClamp>(c));
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) {
            const uint32_t clamp = c & 3u;
            Memory::Write32(gd + 0x23Cu, (Memory::Read32(gd + 0x23Cu) & 0xFFFFFFFCu) | clamp);
            Memory::Write32(gd + 0x24Cu, (Memory::Read32(gd + 0x24Cu) & 0xFFFFFFFCu) | clamp);
        }
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80035d60, GX__SetCopyClamp_80035d60, (uint32_t c), (c));

extern "C" void GX__ClearBoundingBox_80036650() {
    GXClearBoundingBox();
    try {
        const uint32_t gd = Memory::Read32(kGXDataPtrAddr);
        if (gd) Memory::Write16(gd + 2, 0);
    } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80036650, GX__ClearBoundingBox_80036650, (), ());

// ============================================================================
// FIFO/State Management - No-ops
// ============================================================================

extern "C" void GX__SetDirtyState_80035550() {
    try { uint32_t gd = Memory::Read32(kGXDataPtrAddr); if (gd) Memory::Write32(gd + 0x5FCu, 0); } catch (...) {}
}
PPC_NATIVE_OVERRIDE_VOID(80035550, GX__SetDirtyState_80035550, (), ());

extern "C" void GX__ResetWriteGatherPipe_80034cb0() {
    // WPAR reset - not needed on host
}
PPC_NATIVE_OVERRIDE_VOID(80034cb0, GX__ResetWriteGatherPipe_80034cb0, (), ());

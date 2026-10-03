// gx_egg.cpp - EGG Library Helper Stubs
#include "gx_internal.h"

// Vertex helper declarations not used by the direct-call catalog.
extern "C" void GX__SetArray_80034900(uint32_t a, uint32_t ba, uint32_t str);
extern "C" void GX_HLE_FIFO_WriteFloat(float val);
extern "C" void GX_HLE_FIFO_Write8(uint8_t val);

// ============================================================================
// EGG::DrawGX Functions
// ============================================================================

// Use translated implementations for the main DrawGX setup routines.
// Keep HLE fallbacks only for known missing alias entry points.
static void EGG__DrawGX__SetVtxState_HLE(uint32_t state) {
    GX__ClearVtxDesc_800341f0();
    auto setDesc = [](uint32_t attr, uint32_t type) { GX__SetVtxDesc_80033940(attr, type); };
    auto setFmt = [](uint32_t attr, uint32_t cnt, uint32_t type, uint32_t frac) { GX__SetVtxAttrFmt_80034230(0, attr, cnt, type, frac); };
    auto setArray = [](uint32_t attr, uint32_t addr, uint32_t stride) { if (addr && stride) GX__SetArray_80034900(attr, addr, stride); };

    switch (state) {
    case 0:
        setFmt(GX_VA_POS, GX_POS_XYZ, GX_S16, 0xE);
        setFmt(GX_VA_NRM, GX_NRM_XYZ, GX_S16, 0xE);
        setArray(GX_VA_POS, 0x80662920u, 6);
        setArray(GX_VA_NRM, 0x80662960u, 6);
        setDesc(GX_VA_POS, GX_INDEX8);
        setDesc(GX_VA_NRM, GX_INDEX8);
        break;
    case 1:
        setDesc(GX_VA_POS, GX_DIRECT);
        setFmt(GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        break;
    case 2: case 3: case 4: case 5:
        setDesc(GX_VA_POS, GX_DIRECT); setDesc(GX_VA_NRM, GX_DIRECT);
        setFmt(GX_VA_POS, GX_POS_XYZ, GX_F32, 0); setFmt(GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
        break;
    case 6:
        setDesc(GX_VA_POS, GX_DIRECT); setDesc(GX_VA_NRM, GX_DIRECT); setDesc(GX_VA_CLR0, GX_DIRECT);
        setFmt(GX_VA_POS, GX_POS_XYZ, GX_F32, 0); setFmt(GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
        setFmt(GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        break;
    case 7: case 8:
        setDesc(GX_VA_POS, GX_INDEX8); setDesc(GX_VA_NRM, GX_INDEX8);
        if (state == 7) setDesc(GX_VA_TEX0, GX_DIRECT);
        setFmt(GX_VA_POS, GX_POS_XY, GX_S16, 0xE); setFmt(GX_VA_NRM, GX_NRM_XYZ, GX_S16, 0xE);
        if (state == 7) setFmt(GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        setArray(GX_VA_POS, 0x806629A0u, 4); setArray(GX_VA_NRM, 0x806f8220u, 6);
        break;
    case 9:
        setDesc(GX_VA_POS, GX_INDEX8); setDesc(GX_VA_NRM, GX_INDEX8); setDesc(GX_VA_TEX0, GX_DIRECT);
        setFmt(GX_VA_POS, GX_POS_XYZ, GX_S16, 0xE); setFmt(GX_VA_NRM, GX_NRM_XYZ, GX_S16, 0xE);
        setFmt(GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        setArray(GX_VA_POS, 0x806629C0u, 6); setArray(GX_VA_NRM, 0x806f8260u, 6);
        break;
    case 10: case 11: case 12: case 13: {
        const bool useAltPos = (state == 11 || state == 13);
        const bool useTex = (state == 10 || state == 11);
        setDesc(GX_VA_POS, GX_INDEX8);
        if (useTex) setDesc(GX_VA_TEX0, GX_INDEX8);
        const uint32_t posBase = useAltPos ? 0x806f82a0u : 0x806f8280u;
        setArray(GX_VA_POS, posBase, 2);
        if (useTex) setArray(GX_VA_TEX0, 0x806f82a0u, 2);
        setFmt(GX_VA_POS, GX_POS_XY, GX_U8, 0);
        if (useTex) setFmt(GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);
        break;
    }
    default: break;
    }
}
PPC_NATIVE_OVERRIDE_VOID(801a7ae0, EGG__DrawGX__SetVtxState_HLE, (uint32_t state), (state));
PPC_NATIVE_OVERRIDE_VOID(801a7e28, EGG__DrawGX__SetVtxState_HLE, (uint32_t state), (state));

extern "C" void EGG__LightTexture__SetupTevFinish_HLE_8022e2bc(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    const uint32_t stageCount = Memory::Read8(self + 0x75);
    const uint32_t tevCount = Memory::Read16(self + 0x78);

    if (stageCount != 0) {
        uint32_t remainder = tevCount % stageCount;
        if (remainder > 0) {
            const GXColor black{0, 0, 0, 255};
            while (remainder < stageCount) {
                GXSetTevColor(static_cast<GXTevRegID>(remainder + 1), black);
                GXSetTevKColor(static_cast<GXTevKColorID>(remainder), black);
                ++remainder;
            }

            const uint32_t mode = Memory::Read32(self + 0x44);
            const float origin = Memory::ReadFloat32(ctx->gpr[2] + ((mode == 2) ? -25004 : -25040));
            const float span = Memory::ReadFloat32(ctx->gpr[2] - 25360);
            const float end = static_cast<float>(origin + span);
            const float lower = static_cast<float>(origin - span);

            auto writeVertex = [](uint8_t posIdx, float s, float t) {
                GX_HLE_FIFO_Write8(posIdx);
                GX_HLE_FIFO_WriteFloat(s);
                GX_HLE_FIFO_WriteFloat(t);
            };

            GX__Begin_800357d0(GX_QUADS, GX_VTXFMT0, 4);
            writeVertex(0, origin, origin);
            writeVertex(1, origin, lower);
            writeVertex(2, end, lower);
            writeVertex(3, end, origin);
        }
    }

    Memory::Write8(self + 0x74, 2);
}
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_OVERRIDE_VOID(8022e2bc, EGG__LightTexture__SetupTevFinish_HLE_8022e2bc, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED

// ============================================================================
// EGG::AsyncDisplay
// ============================================================================

extern "C" void EGG__AsyncDisplay__endRender_HLE_8020ff9c(CpuContext* ctx) {
    uint32_t p = ctx->gpr[3]; ctx->gpr[3] = p; ctx->lr = 0x8020FF9C;
    InvokeIndirectCpu(0x80219FB4u, ctx);
    InvokeIndirectCpu(0x80035410u, ctx);
}
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_OVERRIDE_VOID(8020FF9C, EGG__AsyncDisplay__endRender_HLE_8020ff9c, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED

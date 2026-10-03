// gx_indirect.cpp - Indirect Texture / Bump Mapping
#include "gx_internal.h"

// ============================================================================
// Indirect Texture Stages
// ============================================================================

extern "C" void GX__SetNumIndStages_80038440(uint32_t n) { GXSetNumIndStages((u8)n); }
PPC_NATIVE_OVERRIDE_VOID(80038440, GX__SetNumIndStages_80038440, (uint32_t n), (n));

extern "C" void GX__SetIndTexOrder_80038370(uint32_t s, uint32_t c, uint32_t m) {
    GXSetIndTexOrder((GXIndTexStageID)s, (GXTexCoordID)(c==0xFFu?0:c), (GXTexMapID)(m==0xFFu?0:m));
}
PPC_NATIVE_OVERRIDE_VOID(80038370, GX__SetIndTexOrder_80038370, (uint32_t s, uint32_t c, uint32_t m), (s, c, m));

extern "C" void GX__SetIndTexCoordScale_80038270(uint32_t s, uint32_t ss, uint32_t ts) {
    GXSetIndTexCoordScale((GXIndTexStageID)s, (GXIndTexScale)ss, (GXIndTexScale)ts);
}
PPC_NATIVE_OVERRIDE_VOID(80038270, GX__SetIndTexCoordScale_80038270, (uint32_t s, uint32_t ss, uint32_t ts), (s, ss, ts));

extern "C" void GX__SetIndTexMtx_80038130(uint32_t id, uint32_t ma, uint32_t se) {
    float m[6]; for(int i=0; i<6; ++i) m[i]=Memory::ReadFloat32(ma+i*4);
    GXSetIndTexMtx((GXIndTexMtxID)id, m, (s8)se);
}
PPC_NATIVE_OVERRIDE_VOID(80038130, GX__SetIndTexMtx_80038130, (uint32_t id, uint32_t ma, uint32_t se), (id, ma, se));

// ============================================================================
// TEV Indirect Texture Control
// ============================================================================

extern "C" void GX__SetTevDirect_80038460(uint32_t s) { GXSetTevDirect((GXTevStageID)s); }
PPC_NATIVE_OVERRIDE_VOID(80038460, GX__SetTevDirect_80038460, (uint32_t s), (s));

extern "C" void GX__SetTevIndWarp_800384b0(uint32_t ts, uint32_t is, uint32_t so, uint32_t rm, uint32_t ms) {
    GXSetTevIndWarp((GXTevStageID)std::min(ts, 15u), (GXIndTexStageID)std::min(is, 3u),
        (GXBool)so, (GXBool)rm, (GXIndTexMtxID)ms);
}
PPC_NATIVE_OVERRIDE_VOID(800384b0, GX__SetTevIndWarp_800384b0, (uint32_t ts, uint32_t is, uint32_t so, uint32_t rm, uint32_t ms), (ts, is, so, rm, ms));

extern "C" void GX__SetTevIndirect_800380c0(uint32_t ts, uint32_t is, uint32_t f, uint32_t bs, uint32_t ms, uint32_t ws, uint32_t wt, uint32_t ap, uint32_t il, uint32_t as) {
    GXSetTevIndirect((GXTevStageID)std::min(ts,15u), (GXIndTexStageID)std::min(is,3u), (GXIndTexFormat)f,
        (GXIndTexBiasSel)bs, (GXIndTexMtxID)ms, (GXIndTexWrap)ws, (GXIndTexWrap)wt,
        (GXBool)ap, (GXBool)il, (GXIndTexAlphaSel)as);
}
PPC_NATIVE_OVERRIDE_VOID(800380c0, GX__SetTevIndirect_800380c0, (uint32_t ts, uint32_t is, uint32_t f, uint32_t bs, uint32_t ms, uint32_t ws, uint32_t wt, uint32_t ap, uint32_t il, uint32_t as), (ts, is, f, bs, ms, ws, wt, ap, il, as));

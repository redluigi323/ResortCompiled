// Native call catalog used by InvokeDirectCpu. Keep project-specific HLE
// address mappings here; abi_bridge.h contains only the generic ABI machinery.
//
// This is a .inc, not a header: it has no includes of its own and references
// CpuContext, KnownTypedNativeCpuCall<> and KnownNativeCpuCall<>, all of which
// are declared above its single mid-file include site in abi_bridge.h. It
// cannot compile standalone and must never be included anywhere else.

extern "C" void nw4r__lyt__detail__DrawQuad_801a2e60(CpuContext* ctx);
extern "C" void nw4r__lyt__detail__DrawQuad_801a33c0(CpuContext* ctx);

extern "C" void GX__SetVtxDesc_80033940(uint32_t a, uint32_t t);
extern "C" void GX__ClearVtxDesc_800341f0();
extern "C" void GX__SetVtxAttrFmt_80034230(uint32_t vf, uint32_t a, uint32_t c, uint32_t t, uint32_t fr);
extern "C" void GX__SetTexCoordGen2_80034950(uint32_t dc, uint32_t f, uint32_t sp, uint32_t m, uint32_t n, uint32_t pm);
extern "C" void GX__SetNumTexGens_80034ba0(uint32_t n);
extern "C" void GX__DrawDone_800350e0();
extern "C" void GX__Begin_800357d0(uint32_t t, uint32_t vf, uint32_t nv);
extern "C" void GX__SetLineWidth_80035a00(uint32_t width, uint32_t texOffsets);
extern "C" void GX__SetPointSize_80035a40(uint32_t pointSize, uint32_t texOffsets);
extern "C" void GX__SetCullMode_80035ac0(uint32_t m);
extern "C" void GX__SetNumChans_80036d60(uint32_t n);
extern "C" void GX__SetChanCtrl_80036D90(uint32_t ch, uint32_t en, uint32_t as, uint32_t ms, uint32_t lm, uint32_t df, uint32_t af);
extern "C" void GX__InitTexObj_80037030(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m);
extern "C" void GX__InitTexObjLOD_80037290(uint32_t oa, uint32_t mif, uint32_t maf, float mil, float mal, float lb, uint32_t bc, uint32_t el, uint32_t ma);
extern "C" void GX__InvalidateTexAll_80037980();
extern "C" void GX__LoadTexObj_80037780(uint32_t oa, uint32_t tid);
extern "C" void GX__SetNumIndStages_80038440(uint32_t n);
extern "C" void GX__SetTevDirect_80038460(uint32_t s);
extern "C" void GX__SetTevColorIn_80038620(uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
extern "C" void GX__SetTevAlphaIn_80038660(uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
extern "C" void GX__SetTevColorOp_800386a0(uint32_t s, uint32_t op, uint32_t b, uint32_t sc, uint32_t cl, uint32_t or_);
extern "C" void GX__SetTevAlphaOp_80038700(uint32_t s, uint32_t op, uint32_t b, uint32_t sc, uint32_t cl, uint32_t or_);
extern "C" void GX__SetTevColorS10_800387c0(uint32_t id, uint32_t cp);
extern "C" void GX__SetTevKColor_80038830(uint32_t id, uint32_t cp);
extern "C" void GX__SetTevKColorSel_80038890(uint32_t s, uint32_t sel);
extern "C" void GX__SetTevKAlphaSel_800388e0(uint32_t s, uint32_t sel);
extern "C" void GX__SetTevSwapMode_80038930(uint32_t s, uint32_t rs, uint32_t ts);
extern "C" void GX__SetTevSwapModeTable_80038970(uint32_t id, uint32_t r, uint32_t g, uint32_t b, uint32_t a);
extern "C" void GX__SetTevOrder_80038ac0(uint32_t s, uint32_t c, uint32_t m, uint32_t col);
extern "C" void GX__SetNumTevStages_80038c20(uint32_t n);
extern "C" void GX__SetBlendMode_800390f0(uint32_t t, uint32_t s, uint32_t d, uint32_t op);
extern "C" void GX__LoadPosMtxImm_80039ae0(uint32_t ma, uint32_t id);
extern "C" void GX__SetCurrentMtx_80039C00(uint32_t id);

#define MKW_KNOWN_TYPED_NATIVE(addr, fn, ...) \
template <> struct KnownTypedNativeCpuCall<addr> { \
    static constexpr bool kAvailable = true; \
    static void Invoke(CpuContext* cpu) { (void)cpu; fn(__VA_ARGS__); } \
}

MKW_KNOWN_TYPED_NATIVE(0x80033940u, GX__SetVtxDesc_80033940, cpu->gpr[3], cpu->gpr[4]);
MKW_KNOWN_TYPED_NATIVE(0x800341F0u, GX__ClearVtxDesc_800341f0);
MKW_KNOWN_TYPED_NATIVE(0x80034230u, GX__SetVtxAttrFmt_80034230, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7]);
MKW_KNOWN_TYPED_NATIVE(0x80034950u, GX__SetTexCoordGen2_80034950, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7], cpu->gpr[8]);
MKW_KNOWN_TYPED_NATIVE(0x80034BA0u, GX__SetNumTexGens_80034ba0, cpu->gpr[3]);
MKW_KNOWN_TYPED_NATIVE(0x800350E0u, GX__DrawDone_800350e0);
MKW_KNOWN_TYPED_NATIVE(0x800357D0u, GX__Begin_800357d0, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5]);
MKW_KNOWN_TYPED_NATIVE(0x80035A00u, GX__SetLineWidth_80035a00, cpu->gpr[3], cpu->gpr[4]);
MKW_KNOWN_TYPED_NATIVE(0x80035A40u, GX__SetPointSize_80035a40, cpu->gpr[3], cpu->gpr[4]);
MKW_KNOWN_TYPED_NATIVE(0x80035AC0u, GX__SetCullMode_80035ac0, cpu->gpr[3]);
MKW_KNOWN_TYPED_NATIVE(0x80036D60u, GX__SetNumChans_80036d60, cpu->gpr[3]);
MKW_KNOWN_TYPED_NATIVE(0x80036D90u, GX__SetChanCtrl_80036D90, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7], cpu->gpr[8], cpu->gpr[9]);
MKW_KNOWN_TYPED_NATIVE(0x80037030u, GX__InitTexObj_80037030, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7], cpu->gpr[8], cpu->gpr[9], cpu->gpr[10]);
MKW_KNOWN_TYPED_NATIVE(0x80037290u, GX__InitTexObjLOD_80037290, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], static_cast<float>(cpu->fpr[1].d), static_cast<float>(cpu->fpr[2].d), static_cast<float>(cpu->fpr[3].d), cpu->gpr[6], cpu->gpr[7], cpu->gpr[8]);
MKW_KNOWN_TYPED_NATIVE(0x80037980u, GX__InvalidateTexAll_80037980);
MKW_KNOWN_TYPED_NATIVE(0x80037780u, GX__LoadTexObj_80037780, cpu->gpr[3], cpu->gpr[4]);
MKW_KNOWN_TYPED_NATIVE(0x80038440u, GX__SetNumIndStages_80038440, cpu->gpr[3]);
MKW_KNOWN_TYPED_NATIVE(0x80038460u, GX__SetTevDirect_80038460, cpu->gpr[3]);
MKW_KNOWN_TYPED_NATIVE(0x80038620u, GX__SetTevColorIn_80038620, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7]);
MKW_KNOWN_TYPED_NATIVE(0x80038660u, GX__SetTevAlphaIn_80038660, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7]);
MKW_KNOWN_TYPED_NATIVE(0x800386A0u, GX__SetTevColorOp_800386a0, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7], cpu->gpr[8]);
MKW_KNOWN_TYPED_NATIVE(0x80038700u, GX__SetTevAlphaOp_80038700, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7], cpu->gpr[8]);
MKW_KNOWN_TYPED_NATIVE(0x800387C0u, GX__SetTevColorS10_800387c0, cpu->gpr[3], cpu->gpr[4]);
MKW_KNOWN_TYPED_NATIVE(0x80038830u, GX__SetTevKColor_80038830, cpu->gpr[3], cpu->gpr[4]);
MKW_KNOWN_TYPED_NATIVE(0x80038890u, GX__SetTevKColorSel_80038890, cpu->gpr[3], cpu->gpr[4]);
MKW_KNOWN_TYPED_NATIVE(0x800388E0u, GX__SetTevKAlphaSel_800388e0, cpu->gpr[3], cpu->gpr[4]);
MKW_KNOWN_TYPED_NATIVE(0x80038930u, GX__SetTevSwapMode_80038930, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5]);
MKW_KNOWN_TYPED_NATIVE(0x80038970u, GX__SetTevSwapModeTable_80038970, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[7]);
MKW_KNOWN_TYPED_NATIVE(0x80038AC0u, GX__SetTevOrder_80038ac0, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6]);
MKW_KNOWN_TYPED_NATIVE(0x80038C20u, GX__SetNumTevStages_80038c20, cpu->gpr[3]);
MKW_KNOWN_TYPED_NATIVE(0x800390F0u, GX__SetBlendMode_800390f0, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6]);
MKW_KNOWN_TYPED_NATIVE(0x80039AE0u, GX__LoadPosMtxImm_80039ae0, cpu->gpr[3], cpu->gpr[4]);
MKW_KNOWN_TYPED_NATIVE(0x80039C00u, GX__SetCurrentMtx_80039C00, cpu->gpr[3]);

#undef MKW_KNOWN_TYPED_NATIVE

#define MKW_KNOWN_NATIVE_CPU_CALL(addr, fn) \
template <> struct KnownNativeCpuCall<addr> { \
    static constexpr bool kAvailable = true; \
    static constexpr uint32_t kNonvolatileFprWriteMask = 0; \
    static constexpr void (*Entry)(CpuContext*) = &fn; \
}

MKW_KNOWN_NATIVE_CPU_CALL(0x801A2E60u, nw4r__lyt__detail__DrawQuad_801a2e60);
MKW_KNOWN_NATIVE_CPU_CALL(0x801A33C0u, nw4r__lyt__detail__DrawQuad_801a33c0);

#undef MKW_KNOWN_NATIVE_CPU_CALL

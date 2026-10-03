// Exercise the real HLE display-list bridge without starting a GPU or the game.
// Only the final Aurora submission is captured; scanning, guest address
// translation, flattening and encoder state publication run normally.
#include "hle/gx/gx_internal.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
namespace aurora::gx::fifo { void init(); }
static std::vector<unsigned char> submitted;
static void require(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
extern "C" void __wrap_GXCallDisplayList(const void* p, u32 n) {
    const auto* b = static_cast<const unsigned char*>(p);
    submitted.insert(submitted.end(), b, b + n);
}
extern "C" int __wrap_main(int, char**) {
    Memory::Init(Memory::Config::WiiDefaults());
    aurora::gx::fifo::init();
    g_auroraFrameActive.store(true);
    constexpr uint32_t list = 0x81000000, child = 0x81001000;
    const auto write = [](uint32_t addr, const std::vector<unsigned char>& bytes) {
        for (unsigned i = 0; i < bytes.size(); ++i) Memory::Write8(addr + i, bytes[i]);
    };
    // Empty draws are legal and have no vertex payload. A subsequent FIFO
    // command must not be interpreted as the first vertex of an endless draw.
    GX__Begin_800357d0(GX_LINES, GX_VTXFMT0, 0);
    require(!g_hleGxState.inBegin, "empty GXBegin leaves parser at command boundary");
    GX_HLE_FIFO_Write8(GX_LINES);
    GX_HLE_FIFO_Write16(0);
    require(!g_hleGxState.inBegin, "empty raw draw leaves parser at command boundary");
    const std::vector<unsigned char> cp{8,0x50,0,0,0x14,0, 8,0x60,0,0,0,2};
    std::vector<unsigned char> burst{GX_LINES, 0, 0};
    burst.insert(burst.end(), cp.begin(), cp.end());
    GX_HLE_FIFO_WriteBurst(burst.data(), burst.size());
    require(!g_hleGxState.inBegin && g_hleGxState.fifoByteCount == 0 &&
            g_hleGxState.vtxDesc[GX_VA_POS] == GX_INDEX8 &&
            g_hleGxState.vtxDesc[GX_VA_TEX0] == GX_INDEX8,
            "commands immediately following an empty draw remain commands");
    write(list, cp);
    GX__CallDisplayList_80039910(list, cp.size());
    require(g_hleGxState.vtxDesc[GX_VA_POS] == GX_INDEX8 &&
            g_hleGxState.vtxDesc[GX_VA_NRM] == GX_INDEX8 &&
            g_hleGxState.vtxDesc[GX_VA_TEX0] == GX_INDEX8,
            "register-only list updates guest vertex layout");

    const std::vector<unsigned char> xf{0x10,0,0,0,0,0x3f,0x80,0,0};
    write(child, xf);
    write(list, {0x40,0x81,0,0x10,0,0,0,0,9});
    submitted.clear();
    GX__CallDisplayList_80039910(list, 9);
    require(submitted == xf, "register-only nested matrix list is inlined");
    Memory::Write32(child + 5, 0x40000000);
    submitted.clear();
    GX__CallDisplayList_80039910(list, 9);
    require(submitted.size() == 9 && submitted[5] == 0x40,
            "rewritten nested list is not hidden by outer-list cache");

    // A direct immediate draw may alter Aurora without changing guest HLE
    // state. The next display-list draw must publish the requested VAT again.
    g_hleGxState.vtxDesc[GX_VA_NRM] = GX_NONE;
    g_hleGxState.vtxDesc[GX_VA_TEX0] = GX_NONE;
    g_hleGxState.vtxAttrFmt[0][GX_VA_POS] = {GX_POS_XYZ,GX_F32,0};
    g_hleGxState.vtxArray[GX_VA_POS].base = 0x81002000;
    g_hleGxState.vtxArray[GX_VA_POS].stride = 12;
    g_hleGxState.InvalidateVtxLayoutHash();
    write(list, {0x90,0,3,0,1,2});
    GX__CallDisplayList_80039910(list, 6);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 4);
    GX__CallDisplayList_80039910(list, 6);
    GXCompCnt cnt; GXCompType type; u8 frac;
    GXGetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,&cnt,&type,&frac);
    require(cnt == GX_POS_XYZ && type == GX_F32 && frac == 0,
            "display-list VAT restored after immediate encoder changes");
    g_hleGxState.vtxAttrFmt[1][GX_VA_POS] = {GX_POS_XYZ,GX_F32,0};
    g_hleGxState.InvalidateVtxLayoutHash();
    write(list, {0x90,0,3,0,1,2, 0x91,0,3,0,1,2});
    GX__CallDisplayList_80039910(list, 12);
    GXSetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XY, GX_S16, 4);
    GX__CallDisplayList_80039910(list, 12);
    GXGetVtxAttrFmt(GX_VTXFMT1,GX_VA_POS,&cnt,&type,&frac);
    require(cnt == GX_POS_XYZ && type == GX_F32 && frac == 0,
            "mixed-format list restores every VAT row independently");
    std::puts("PASS: empty draws, following FIFO commands, register state, nested matrices, and vertex formats");
    return 0;
}

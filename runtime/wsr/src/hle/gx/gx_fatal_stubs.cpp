// Auto-generated GX fatal stubs
#include "hle_stubs.h"
#include "runtime_log.h"

namespace {
[[noreturn]] void HaltGX(uint32_t addr, const char* name) {
    const char* symbol = name ? name : "<unknown GX symbol>";
    RT_LOGF(RT_TAG_GX,
            "unimplemented GX entry point: %s at guest address 0x%08X.\n"
            "[gx] This graphics call has no Aurora implementation bound to it yet, so the\n"
            "[gx] runtime cannot continue without silently dropping GPU state. Bind it in\n"
            "[gx] runtime/src/hle/gx/ and remove the stub from gx_fatal_stubs.cpp.\n",
            symbol, addr);
    std::fflush(stderr);
    char message[512]{};
    std::snprintf(message, sizeof(message),
                  "%s at guest address 0x%08X has no Aurora implementation bound to it, so the "
                  "runtime stopped rather than keep rendering with missing GPU state.",
                  symbol, addr);
    ShowRuntimeFatalPopup("the game called an unimplemented graphics function", message);
    std::abort();
}
} // namespace

// Every fatal stub is the same two statements with the address and the symbol
// name substituted, so the body comes from this macro. The registration is
// deliberately still spelled out per entry so the translator's runtime-native
// index sees the literal PPC_NATIVE_OVERRIDE_VOID invocation. Hiding it inside
// this macro would leave the index unable to associate an address with the stub.
#define GX_FATAL_STUB(addr, sym) \
    extern "C" void gx_stub_##addr(CpuContext* ctx) { (void)ctx; HaltGX(0x##addr, sym); }

GX_FATAL_STUB(80031a20, "__GX__DefaultTexRegionCallback_80031a20") PPC_NATIVE_OVERRIDE_VOID(80031a20, gx_stub_80031a20, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80031b10, "__GX__DefaultTlutRegionCallback_80031b10") PPC_NATIVE_OVERRIDE_VOID(80031b10, gx_stub_80031b10, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80031b40, "__GX__Shutdown_80031b40") PPC_NATIVE_OVERRIDE_VOID(80031b40, gx_stub_80031b40, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80032bb0, "GX__CPInterruptHandler_80032bb0") PPC_NATIVE_OVERRIDE_VOID(80032bb0, gx_stub_80032bb0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033790, "__GX__CleanGPFifo_80033790") PPC_NATIVE_OVERRIDE_VOID(80033790, gx_stub_80033790, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003395c, "GX__SetVtxDesc_switch_8003395c") PPC_NATIVE_OVERRIDE_VOID(8003395c, gx_stub_8003395c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033960, "GX__SetVtxDesc_caseD_0_80033960") PPC_NATIVE_OVERRIDE_VOID(80033960, gx_stub_80033960, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033974, "GX__SetVtxDesc_caseD_1_80033974") PPC_NATIVE_OVERRIDE_VOID(80033974, gx_stub_80033974, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033988, "GX__SetVtxDesc_caseD_2_80033988") PPC_NATIVE_OVERRIDE_VOID(80033988, gx_stub_80033988, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003399c, "GX__SetVtxDesc_caseD_3_8003399c") PPC_NATIVE_OVERRIDE_VOID(8003399c, gx_stub_8003399c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800339b0, "GX__SetVtxDesc_caseD_4_800339b0") PPC_NATIVE_OVERRIDE_VOID(800339b0, gx_stub_800339b0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800339c4, "GX__SetVtxDesc_caseD_5_800339c4") PPC_NATIVE_OVERRIDE_VOID(800339c4, gx_stub_800339c4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800339d8, "GX__SetVtxDesc_caseD_6_800339d8") PPC_NATIVE_OVERRIDE_VOID(800339d8, gx_stub_800339d8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800339ec, "GX__SetVtxDesc_caseD_7_800339ec") PPC_NATIVE_OVERRIDE_VOID(800339ec, gx_stub_800339ec, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033a00, "GX__SetVtxDesc_caseD_8_80033a00") PPC_NATIVE_OVERRIDE_VOID(80033a00, gx_stub_80033a00, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033a14, "GX__SetVtxDesc_caseD_9_80033a14") PPC_NATIVE_OVERRIDE_VOID(80033a14, gx_stub_80033a14, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033a28, "GX__SetVtxDesc_caseD_a_80033a28") PPC_NATIVE_OVERRIDE_VOID(80033a28, gx_stub_80033a28, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033a5c, "GX__SetVtxDesc_caseD_19_80033a5c") PPC_NATIVE_OVERRIDE_VOID(80033a5c, gx_stub_80033a5c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033a90, "GX__SetVtxDesc_caseD_b_80033a90") PPC_NATIVE_OVERRIDE_VOID(80033a90, gx_stub_80033a90, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033aa4, "GX__SetVtxDesc_caseD_c_80033aa4") PPC_NATIVE_OVERRIDE_VOID(80033aa4, gx_stub_80033aa4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033ab8, "GX__SetVtxDesc_caseD_d_80033ab8") PPC_NATIVE_OVERRIDE_VOID(80033ab8, gx_stub_80033ab8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033acc, "GX__SetVtxDesc_caseD_e_80033acc") PPC_NATIVE_OVERRIDE_VOID(80033acc, gx_stub_80033acc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033ae0, "GX__SetVtxDesc_caseD_f_80033ae0") PPC_NATIVE_OVERRIDE_VOID(80033ae0, gx_stub_80033ae0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033af4, "GX__SetVtxDesc_caseD_10_80033af4") PPC_NATIVE_OVERRIDE_VOID(80033af4, gx_stub_80033af4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033b08, "GX__SetVtxDesc_caseD_11_80033b08") PPC_NATIVE_OVERRIDE_VOID(80033b08, gx_stub_80033b08, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033b1c, "GX__SetVtxDesc_caseD_12_80033b1c") PPC_NATIVE_OVERRIDE_VOID(80033b1c, gx_stub_80033b1c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033b30, "GX__SetVtxDesc_caseD_13_80033b30") PPC_NATIVE_OVERRIDE_VOID(80033b30, gx_stub_80033b30, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033b44, "GX__SetVtxDesc_caseD_14_80033b44") PPC_NATIVE_OVERRIDE_VOID(80033b44, gx_stub_80033b44, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033b54, "GX__SetVtxDesc_caseD_15_80033b54") PPC_NATIVE_OVERRIDE_VOID(80033b54, gx_stub_80033b54, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033dc0, "__GX__SetVCD_80033dc0") PPC_NATIVE_OVERRIDE_VOID(80033dc0, gx_stub_80033dc0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033e70, "__GX__CalculateVLim_80033e70") PPC_NATIVE_OVERRIDE_VOID(80033e70, gx_stub_80033e70, (CpuContext* ctx), (ctx));
// moved to gx_vertex.cpp: GX__GetVtxDesc_80033fa0
GX_FATAL_STUB(80033fbc, "GX__GetVtxDesc_switch_80033fbc") PPC_NATIVE_OVERRIDE_VOID(80033fbc, gx_stub_80033fbc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033fc0, "GX__GetVtxDesc_caseD_0_80033fc0") PPC_NATIVE_OVERRIDE_VOID(80033fc0, gx_stub_80033fc0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033fd0, "GX__GetVtxDesc_caseD_1_80033fd0") PPC_NATIVE_OVERRIDE_VOID(80033fd0, gx_stub_80033fd0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033fe0, "GX__GetVtxDesc_caseD_2_80033fe0") PPC_NATIVE_OVERRIDE_VOID(80033fe0, gx_stub_80033fe0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80033ff0, "GX__GetVtxDesc_caseD_3_80033ff0") PPC_NATIVE_OVERRIDE_VOID(80033ff0, gx_stub_80033ff0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034000, "GX__GetVtxDesc_caseD_4_80034000") PPC_NATIVE_OVERRIDE_VOID(80034000, gx_stub_80034000, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034010, "GX__GetVtxDesc_caseD_5_80034010") PPC_NATIVE_OVERRIDE_VOID(80034010, gx_stub_80034010, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034020, "GX__GetVtxDesc_caseD_6_80034020") PPC_NATIVE_OVERRIDE_VOID(80034020, gx_stub_80034020, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034030, "GX__GetVtxDesc_caseD_7_80034030") PPC_NATIVE_OVERRIDE_VOID(80034030, gx_stub_80034030, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034040, "GX__GetVtxDesc_caseD_8_80034040") PPC_NATIVE_OVERRIDE_VOID(80034040, gx_stub_80034040, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034050, "GX__GetVtxDesc_caseD_9_80034050") PPC_NATIVE_OVERRIDE_VOID(80034050, gx_stub_80034050, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034060, "GX__GetVtxDesc_caseD_a_80034060") PPC_NATIVE_OVERRIDE_VOID(80034060, gx_stub_80034060, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034084, "GX__GetVtxDesc_caseD_19_80034084") PPC_NATIVE_OVERRIDE_VOID(80034084, gx_stub_80034084, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800340a8, "GX__GetVtxDesc_caseD_b_800340a8") PPC_NATIVE_OVERRIDE_VOID(800340a8, gx_stub_800340a8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800340b8, "GX__GetVtxDesc_caseD_c_800340b8") PPC_NATIVE_OVERRIDE_VOID(800340b8, gx_stub_800340b8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800340c8, "GX__GetVtxDesc_caseD_d_800340c8") PPC_NATIVE_OVERRIDE_VOID(800340c8, gx_stub_800340c8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800340d8, "GX__GetVtxDesc_caseD_e_800340d8") PPC_NATIVE_OVERRIDE_VOID(800340d8, gx_stub_800340d8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800340e8, "GX__GetVtxDesc_caseD_f_800340e8") PPC_NATIVE_OVERRIDE_VOID(800340e8, gx_stub_800340e8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800340f8, "GX__GetVtxDesc_caseD_10_800340f8") PPC_NATIVE_OVERRIDE_VOID(800340f8, gx_stub_800340f8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034108, "GX__GetVtxDesc_caseD_11_80034108") PPC_NATIVE_OVERRIDE_VOID(80034108, gx_stub_80034108, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034118, "GX__GetVtxDesc_caseD_12_80034118") PPC_NATIVE_OVERRIDE_VOID(80034118, gx_stub_80034118, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034128, "GX__GetVtxDesc_caseD_13_80034128") PPC_NATIVE_OVERRIDE_VOID(80034128, gx_stub_80034128, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034138, "GX__GetVtxDesc_caseD_14_80034138") PPC_NATIVE_OVERRIDE_VOID(80034138, gx_stub_80034138, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034148, "GX__GetVtxDesc_caseD_15_80034148") PPC_NATIVE_OVERRIDE_VOID(80034148, gx_stub_80034148, (CpuContext* ctx), (ctx));
// moved to gx_vertex.cpp: GX__GetVtxDescv_80034160
GX_FATAL_STUB(8003425c, "GX__SetVtxAttrFmt_switch_8003425c") PPC_NATIVE_OVERRIDE_VOID(8003425c, gx_stub_8003425c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034260, "GX__SetVtxAttrFmt_caseD_9_80034260") PPC_NATIVE_OVERRIDE_VOID(80034260, gx_stub_80034260, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034278, "GX__SetVtxAttrFmt_caseD_19_80034278") PPC_NATIVE_OVERRIDE_VOID(80034278, gx_stub_80034278, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800342b4, "GX__SetVtxAttrFmt_caseD_b_800342b4") PPC_NATIVE_OVERRIDE_VOID(800342b4, gx_stub_800342b4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800342c8, "GX__SetVtxAttrFmt_caseD_c_800342c8") PPC_NATIVE_OVERRIDE_VOID(800342c8, gx_stub_800342c8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800342dc, "GX__SetVtxAttrFmt_caseD_d_800342dc") PPC_NATIVE_OVERRIDE_VOID(800342dc, gx_stub_800342dc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800342f4, "GX__SetVtxAttrFmt_caseD_e_800342f4") PPC_NATIVE_OVERRIDE_VOID(800342f4, gx_stub_800342f4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003430c, "GX__SetVtxAttrFmt_caseD_f_8003430c") PPC_NATIVE_OVERRIDE_VOID(8003430c, gx_stub_8003430c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034324, "GX__SetVtxAttrFmt_caseD_10_80034324") PPC_NATIVE_OVERRIDE_VOID(80034324, gx_stub_80034324, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003433c, "GX__SetVtxAttrFmt_caseD_11_8003433c") PPC_NATIVE_OVERRIDE_VOID(8003433c, gx_stub_8003433c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003435c, "GX__SetVtxAttrFmt_caseD_12_8003435c") PPC_NATIVE_OVERRIDE_VOID(8003435c, gx_stub_8003435c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034374, "GX__SetVtxAttrFmt_caseD_13_80034374") PPC_NATIVE_OVERRIDE_VOID(80034374, gx_stub_80034374, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003438c, "GX__SetVtxAttrFmt_caseD_14_8003438c") PPC_NATIVE_OVERRIDE_VOID(8003438c, gx_stub_8003438c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800343a0, "GX__SetVtxAttrFmt_caseD_15_800343a0") PPC_NATIVE_OVERRIDE_VOID(800343a0, gx_stub_800343a0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034414, "GX__SetVtxAttrFmtv_switch_80034414") PPC_NATIVE_OVERRIDE_VOID(80034414, gx_stub_80034414, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034418, "GX__SetVtxAttrFmtv_caseD_9_80034418") PPC_NATIVE_OVERRIDE_VOID(80034418, gx_stub_80034418, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034430, "GX__SetVtxAttrFmtv_caseD_19_80034430") PPC_NATIVE_OVERRIDE_VOID(80034430, gx_stub_80034430, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003446c, "GX__SetVtxAttrFmtv_caseD_b_8003446c") PPC_NATIVE_OVERRIDE_VOID(8003446c, gx_stub_8003446c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034480, "GX__SetVtxAttrFmtv_caseD_c_80034480") PPC_NATIVE_OVERRIDE_VOID(80034480, gx_stub_80034480, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034494, "GX__SetVtxAttrFmtv_caseD_d_80034494") PPC_NATIVE_OVERRIDE_VOID(80034494, gx_stub_80034494, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800344ac, "GX__SetVtxAttrFmtv_caseD_e_800344ac") PPC_NATIVE_OVERRIDE_VOID(800344ac, gx_stub_800344ac, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800344c4, "GX__SetVtxAttrFmtv_caseD_f_800344c4") PPC_NATIVE_OVERRIDE_VOID(800344c4, gx_stub_800344c4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800344dc, "GX__SetVtxAttrFmtv_caseD_10_800344dc") PPC_NATIVE_OVERRIDE_VOID(800344dc, gx_stub_800344dc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800344f4, "GX__SetVtxAttrFmtv_caseD_11_800344f4") PPC_NATIVE_OVERRIDE_VOID(800344f4, gx_stub_800344f4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034514, "GX__SetVtxAttrFmtv_caseD_12_80034514") PPC_NATIVE_OVERRIDE_VOID(80034514, gx_stub_80034514, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003452c, "GX__SetVtxAttrFmtv_caseD_13_8003452c") PPC_NATIVE_OVERRIDE_VOID(8003452c, gx_stub_8003452c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034544, "GX__SetVtxAttrFmtv_caseD_14_80034544") PPC_NATIVE_OVERRIDE_VOID(80034544, gx_stub_80034544, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034558, "GX__SetVtxAttrFmtv_caseD_15_80034558") PPC_NATIVE_OVERRIDE_VOID(80034558, gx_stub_80034558, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800345a0, "__GX__SetVAT_800345a0") PPC_NATIVE_OVERRIDE_VOID(800345a0, gx_stub_800345a0, (CpuContext* ctx), (ctx));
// moved to gx_vertex.cpp: GX__GetVtxAttrFmt_80034620
GX_FATAL_STUB(8003464c, "GX__GetVtxAttrFmt_switch_8003464c") PPC_NATIVE_OVERRIDE_VOID(8003464c, gx_stub_8003464c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034650, "GX__GetVtxAttrFmt_caseD_9_80034650") PPC_NATIVE_OVERRIDE_VOID(80034650, gx_stub_80034650, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034678, "GX__GetVtxAttrFmt_caseD_19_80034678") PPC_NATIVE_OVERRIDE_VOID(80034678, gx_stub_80034678, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800346dc, "GX__GetVtxAttrFmt_caseD_b_800346dc") PPC_NATIVE_OVERRIDE_VOID(800346dc, gx_stub_800346dc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034700, "GX__GetVtxAttrFmt_caseD_c_80034700") PPC_NATIVE_OVERRIDE_VOID(80034700, gx_stub_80034700, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034724, "GX__GetVtxAttrFmt_caseD_d_80034724") PPC_NATIVE_OVERRIDE_VOID(80034724, gx_stub_80034724, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003474c, "GX__GetVtxAttrFmt_caseD_e_8003474c") PPC_NATIVE_OVERRIDE_VOID(8003474c, gx_stub_8003474c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034774, "GX__GetVtxAttrFmt_caseD_f_80034774") PPC_NATIVE_OVERRIDE_VOID(80034774, gx_stub_80034774, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003479c, "GX__GetVtxAttrFmt_caseD_10_8003479c") PPC_NATIVE_OVERRIDE_VOID(8003479c, gx_stub_8003479c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800347c4, "GX__GetVtxAttrFmt_caseD_11_800347c4") PPC_NATIVE_OVERRIDE_VOID(800347c4, gx_stub_800347c4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800347ec, "GX__GetVtxAttrFmt_caseD_12_800347ec") PPC_NATIVE_OVERRIDE_VOID(800347ec, gx_stub_800347ec, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034814, "GX__GetVtxAttrFmt_caseD_13_80034814") PPC_NATIVE_OVERRIDE_VOID(80034814, gx_stub_80034814, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003483c, "GX__GetVtxAttrFmt_caseD_14_8003483c") PPC_NATIVE_OVERRIDE_VOID(8003483c, gx_stub_8003483c, (CpuContext* ctx), (ctx));
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e2a0, "GX__GetVtxAttrFmt_caseD_15_8016e2a0") RESORT_UNMAPPED_OVERRIDE_VOID(8016e2a0, gx_stub_8016e2a0, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
// moved to gx_vertex.cpp: GX__GetVtxAttrFmtv_80034880
GX_FATAL_STUB(80034978, "GX__SetTexCoordGen2_switch_80034978") PPC_NATIVE_OVERRIDE_VOID(80034978, gx_stub_80034978, (CpuContext* ctx), (ctx));
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e3a8, "GX__SetTexCoordGen2_caseD_0_8016e3a8") RESORT_UNMAPPED_OVERRIDE_VOID(8016e3a8, gx_stub_8016e3a8, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e3b4, "GX__SetTexCoordGen2_caseD_1_8016e3b4") RESORT_UNMAPPED_OVERRIDE_VOID(8016e3b4, gx_stub_8016e3b4, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e3c0, "GX__SetTexCoordGen2_caseD_2_8016e3c0") RESORT_UNMAPPED_OVERRIDE_VOID(8016e3c0, gx_stub_8016e3c0, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e3cc, "GX__SetTexCoordGen2_caseD_3_8016e3cc") RESORT_UNMAPPED_OVERRIDE_VOID(8016e3cc, gx_stub_8016e3cc, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e3d8, "GX__SetTexCoordGen2_caseD_13_8016e3d8") RESORT_UNMAPPED_OVERRIDE_VOID(8016e3d8, gx_stub_8016e3d8, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
GX_FATAL_STUB(80034a20, "GX__SetTexCoordGen2_caseD_14_80034a20") PPC_NATIVE_OVERRIDE_VOID(80034a20, gx_stub_80034a20, (CpuContext* ctx), (ctx));
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e3e8, "GX__SetTexCoordGen2_caseD_4_8016e3e8") RESORT_UNMAPPED_OVERRIDE_VOID(8016e3e8, gx_stub_8016e3e8, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e3f0, "GX__SetTexCoordGen2_caseD_5_8016e3f0") RESORT_UNMAPPED_OVERRIDE_VOID(8016e3f0, gx_stub_8016e3f0, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e3f8, "GX__SetTexCoordGen2_caseD_6_8016e3f8") RESORT_UNMAPPED_OVERRIDE_VOID(8016e3f8, gx_stub_8016e3f8, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e400, "GX__SetTexCoordGen2_caseD_7_8016e400") RESORT_UNMAPPED_OVERRIDE_VOID(8016e400, gx_stub_8016e400, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e408, "GX__SetTexCoordGen2_caseD_8_8016e408") RESORT_UNMAPPED_OVERRIDE_VOID(8016e408, gx_stub_8016e408, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e410, "GX__SetTexCoordGen2_caseD_9_8016e410") RESORT_UNMAPPED_OVERRIDE_VOID(8016e410, gx_stub_8016e410, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e418, "GX__SetTexCoordGen2_caseD_a_8016e418") RESORT_UNMAPPED_OVERRIDE_VOID(8016e418, gx_stub_8016e418, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e420, "GX__SetTexCoordGen2_caseD_b_8016e420") RESORT_UNMAPPED_OVERRIDE_VOID(8016e420, gx_stub_8016e420, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e424, "GX__SetTexCoordGen2_caseD_10_8016e424") RESORT_UNMAPPED_OVERRIDE_VOID(8016e424, gx_stub_8016e424, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e514, "GX__SetTexCoordGen2_switch_8016e514") RESORT_UNMAPPED_OVERRIDE_VOID(8016e514, gx_stub_8016e514, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e518, "GX__SetTexCoordGen2_caseD_0_8016e518") RESORT_UNMAPPED_OVERRIDE_VOID(8016e518, gx_stub_8016e518, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e528, "GX__SetTexCoordGen2_caseD_1_8016e528") RESORT_UNMAPPED_OVERRIDE_VOID(8016e528, gx_stub_8016e528, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e538, "GX__SetTexCoordGen2_caseD_2_8016e538") RESORT_UNMAPPED_OVERRIDE_VOID(8016e538, gx_stub_8016e538, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e548, "GX__SetTexCoordGen2_caseD_3_8016e548") RESORT_UNMAPPED_OVERRIDE_VOID(8016e548, gx_stub_8016e548, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e558, "GX__SetTexCoordGen2_caseD_4_8016e558") RESORT_UNMAPPED_OVERRIDE_VOID(8016e558, gx_stub_8016e558, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e568, "GX__SetTexCoordGen2_caseD_5_8016e568") RESORT_UNMAPPED_OVERRIDE_VOID(8016e568, gx_stub_8016e568, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e578, "GX__SetTexCoordGen2_caseD_6_8016e578") RESORT_UNMAPPED_OVERRIDE_VOID(8016e578, gx_stub_8016e578, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016e588, "GX__SetTexCoordGen2_caseD_7_8016e588") RESORT_UNMAPPED_OVERRIDE_VOID(8016e588, gx_stub_8016e588, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
GX_FATAL_STUB(80034cf0, "__GX__Abort_80034cf0") PPC_NATIVE_OVERRIDE_VOID(80034cf0, gx_stub_80034cf0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80034e60, "GX__AbortFrame_80034e60") PPC_NATIVE_OVERRIDE_VOID(80034e60, gx_stub_80034e60, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800351e0, "GX__PokeAlphaMode_800351e0") PPC_NATIVE_OVERRIDE_VOID(800351e0, gx_stub_800351e0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800351f0, "GX__PokeAlphaUpdate_800351f0") PPC_NATIVE_OVERRIDE_VOID(800351f0, gx_stub_800351f0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80035210, "GX__PokeAlphaUpdate_80035210") PPC_NATIVE_OVERRIDE_VOID(80035210, gx_stub_80035210, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80035230, "GX__PokeBlendMode_80035230") PPC_NATIVE_OVERRIDE_VOID(80035230, gx_stub_80035230, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80035290, "GX__PokeColorUpdate_80035290") PPC_NATIVE_OVERRIDE_VOID(80035290, gx_stub_80035290, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800352b0, "GX__PokeDstAlpha_800352b0") PPC_NATIVE_OVERRIDE_VOID(800352b0, gx_stub_800352b0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800352d0, "GX__PokeDither_800352d0") PPC_NATIVE_OVERRIDE_VOID(800352d0, gx_stub_800352d0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(800352f0, "GX__PokeZMode_800352f0") PPC_NATIVE_OVERRIDE_VOID(800352f0, gx_stub_800352f0, (CpuContext* ctx), (ctx));
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(8016f23c, "__GX__SendFlushPrim_8016f23c") RESORT_UNMAPPED_OVERRIDE_VOID(8016f23c, gx_stub_8016f23c, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
GX_FATAL_STUB(80035b50, "__GX__SetGenMode_80035b50") PPC_NATIVE_OVERRIDE_VOID(80035b50, gx_stub_80035b50, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80039990, "__GX__SetProjection_80039990") PPC_NATIVE_OVERRIDE_VOID(80039990, gx_stub_80039990, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80039ce0, "__GX__SetViewport_80039ce0") PPC_NATIVE_OVERRIDE_VOID(80039ce0, gx_stub_80039ce0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80039F60, "__GX__SetMatrixIndex_80039F60") PPC_NATIVE_OVERRIDE_VOID(80039F60, gx_stub_80039F60, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(80039ff0, "GX__SetGPMetric_80039ff0") PPC_NATIVE_OVERRIDE_VOID(80039ff0, gx_stub_80039ff0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a108, "GX__SetGPMetric_switch_8003a108") PPC_NATIVE_OVERRIDE_VOID(8003a108, gx_stub_8003a108, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a10c, "GX__SetGPMetric_caseD_0_8003a10c") PPC_NATIVE_OVERRIDE_VOID(8003a10c, gx_stub_8003a10c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A12C, "GX__SetGPMetric_caseD_1_8003A12C") PPC_NATIVE_OVERRIDE_VOID(8003A12C, gx_stub_8003A12C, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A14C, "GX__SetGPMetric_caseD_2_8003A14C") PPC_NATIVE_OVERRIDE_VOID(8003A14C, gx_stub_8003A14C, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A16C, "GX__SetGPMetric_caseD_3_8003A16C") PPC_NATIVE_OVERRIDE_VOID(8003A16C, gx_stub_8003A16C, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A18C, "GX__SetGPMetric_caseD_4_8003A18C") PPC_NATIVE_OVERRIDE_VOID(8003A18C, gx_stub_8003A18C, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A1AC, "GX__SetGPMetric_caseD_5_8003A1AC") PPC_NATIVE_OVERRIDE_VOID(8003A1AC, gx_stub_8003A1AC, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a1cc, "GX__SetGPMetric_caseD_6_8003a1cc") PPC_NATIVE_OVERRIDE_VOID(8003a1cc, gx_stub_8003a1cc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a1ec, "GX__SetGPMetric_caseD_7_8003a1ec") PPC_NATIVE_OVERRIDE_VOID(8003a1ec, gx_stub_8003a1ec, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a20c, "GX__SetGPMetric_caseD_8_8003a20c") PPC_NATIVE_OVERRIDE_VOID(8003a20c, gx_stub_8003a20c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A22C, "GX__SetGPMetric_caseD_9_8003A22C") PPC_NATIVE_OVERRIDE_VOID(8003A22C, gx_stub_8003A22C, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A24C, "GX__SetGPMetric_caseD_22_8003A24C") PPC_NATIVE_OVERRIDE_VOID(8003A24C, gx_stub_8003A24C, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A26C, "GX__SetGPMetric_caseD_a_8003A26C") PPC_NATIVE_OVERRIDE_VOID(8003A26C, gx_stub_8003A26C, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A28C, "GX__SetGPMetric_caseD_b_8003A28C") PPC_NATIVE_OVERRIDE_VOID(8003A28C, gx_stub_8003A28C, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A2A8, "GX__SetGPMetric_caseD_c_8003A2A8") PPC_NATIVE_OVERRIDE_VOID(8003A2A8, gx_stub_8003A2A8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a2c4, "GX__SetGPMetric_caseD_d_8003a2c4") PPC_NATIVE_OVERRIDE_VOID(8003a2c4, gx_stub_8003a2c4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a2e0, "GX__SetGPMetric_caseD_e_8003a2e0") PPC_NATIVE_OVERRIDE_VOID(8003a2e0, gx_stub_8003a2e0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a2fc, "GX__SetGPMetric_caseD_f_8003a2fc") PPC_NATIVE_OVERRIDE_VOID(8003a2fc, gx_stub_8003a2fc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A318, "GX__SetGPMetric_caseD_10_8003A318") PPC_NATIVE_OVERRIDE_VOID(8003A318, gx_stub_8003A318, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A334, "GX__SetGPMetric_caseD_11_8003A334") PPC_NATIVE_OVERRIDE_VOID(8003A334, gx_stub_8003A334, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a350, "GX__SetGPMetric_caseD_12_8003a350") PPC_NATIVE_OVERRIDE_VOID(8003a350, gx_stub_8003a350, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A36C, "GX__SetGPMetric_caseD_13_8003A36C") PPC_NATIVE_OVERRIDE_VOID(8003A36C, gx_stub_8003A36C, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A388, "GX__SetGPMetric_caseD_14_8003A388") PPC_NATIVE_OVERRIDE_VOID(8003A388, gx_stub_8003A388, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003A3A4, "GX__SetGPMetric_caseD_15_8003A3A4") PPC_NATIVE_OVERRIDE_VOID(8003A3A4, gx_stub_8003A3A4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a3c0, "GX__SetGPMetric_caseD_16_8003a3c0") PPC_NATIVE_OVERRIDE_VOID(8003a3c0, gx_stub_8003a3c0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a3dc, "GX__SetGPMetric_caseD_17_8003a3dc") PPC_NATIVE_OVERRIDE_VOID(8003a3dc, gx_stub_8003a3dc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a3f8, "GX__SetGPMetric_caseD_18_8003a3f8") PPC_NATIVE_OVERRIDE_VOID(8003a3f8, gx_stub_8003a3f8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a414, "GX__SetGPMetric_caseD_19_8003a414") PPC_NATIVE_OVERRIDE_VOID(8003a414, gx_stub_8003a414, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a430, "GX__SetGPMetric_caseD_1a_8003a430") PPC_NATIVE_OVERRIDE_VOID(8003a430, gx_stub_8003a430, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a44c, "GX__SetGPMetric_caseD_1b_8003a44c") PPC_NATIVE_OVERRIDE_VOID(8003a44c, gx_stub_8003a44c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a468, "GX__SetGPMetric_caseD_1c_8003a468") PPC_NATIVE_OVERRIDE_VOID(8003a468, gx_stub_8003a468, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a484, "GX__SetGPMetric_caseD_1d_8003a484") PPC_NATIVE_OVERRIDE_VOID(8003a484, gx_stub_8003a484, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a4a0, "GX__SetGPMetric_caseD_1e_8003a4a0") PPC_NATIVE_OVERRIDE_VOID(8003a4a0, gx_stub_8003a4a0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a4bc, "GX__SetGPMetric_caseD_1f_8003a4bc") PPC_NATIVE_OVERRIDE_VOID(8003a4bc, gx_stub_8003a4bc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a4d8, "GX__SetGPMetric_caseD_20_8003a4d8") PPC_NATIVE_OVERRIDE_VOID(8003a4d8, gx_stub_8003a4d8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a4f4, "GX__SetGPMetric_caseD_21_8003a4f4") PPC_NATIVE_OVERRIDE_VOID(8003a4f4, gx_stub_8003a4f4, (CpuContext* ctx), (ctx));
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(80173af8, "GX__SetGPMetric_caseD_23_80173af8") RESORT_UNMAPPED_OVERRIDE_VOID(80173af8, gx_stub_80173af8, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
GX_FATAL_STUB(8003a52c, "GX__SetGPMetric_switch_8003a52c") PPC_NATIVE_OVERRIDE_VOID(8003a52c, gx_stub_8003a52c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a530, "GX__SetGPMetric_caseD_0_8003a530") PPC_NATIVE_OVERRIDE_VOID(8003a530, gx_stub_8003a530, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a54c, "GX__SetGPMetric_caseD_1_8003a54c") PPC_NATIVE_OVERRIDE_VOID(8003a54c, gx_stub_8003a54c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a568, "GX__SetGPMetric_caseD_2_8003a568") PPC_NATIVE_OVERRIDE_VOID(8003a568, gx_stub_8003a568, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a584, "GX__SetGPMetric_caseD_3_8003a584") PPC_NATIVE_OVERRIDE_VOID(8003a584, gx_stub_8003a584, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a5a0, "GX__SetGPMetric_caseD_8_8003a5a0") PPC_NATIVE_OVERRIDE_VOID(8003a5a0, gx_stub_8003a5a0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a5bc, "GX__SetGPMetric_caseD_15_8003a5bc") PPC_NATIVE_OVERRIDE_VOID(8003a5bc, gx_stub_8003a5bc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a5d8, "GX__SetGPMetric_caseD_4_8003a5d8") PPC_NATIVE_OVERRIDE_VOID(8003a5d8, gx_stub_8003a5d8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a5f4, "GX__SetGPMetric_caseD_5_8003a5f4") PPC_NATIVE_OVERRIDE_VOID(8003a5f4, gx_stub_8003a5f4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a610, "GX__SetGPMetric_caseD_6_8003a610") PPC_NATIVE_OVERRIDE_VOID(8003a610, gx_stub_8003a610, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a62c, "GX__SetGPMetric_caseD_7_8003a62c") PPC_NATIVE_OVERRIDE_VOID(8003a62c, gx_stub_8003a62c, (CpuContext* ctx), (ctx));
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(80173c34, "GX__SetGPMetric_caseD_9_80173c34") RESORT_UNMAPPED_OVERRIDE_VOID(80173c34, gx_stub_80173c34, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(80173c64, "GX__SetGPMetric_caseD_a_80173c64") RESORT_UNMAPPED_OVERRIDE_VOID(80173c64, gx_stub_80173c64, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(80173c94, "GX__SetGPMetric_caseD_b_80173c94") RESORT_UNMAPPED_OVERRIDE_VOID(80173c94, gx_stub_80173c94, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(80173cc4, "GX__SetGPMetric_caseD_c_80173cc4") RESORT_UNMAPPED_OVERRIDE_VOID(80173cc4, gx_stub_80173cc4, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(80173cf4, "GX__SetGPMetric_caseD_d_80173cf4") RESORT_UNMAPPED_OVERRIDE_VOID(80173cf4, gx_stub_80173cf4, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(80173d24, "GX__SetGPMetric_caseD_e_80173d24") RESORT_UNMAPPED_OVERRIDE_VOID(80173d24, gx_stub_80173d24, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
#if 0 // RESORT-UNMAPPED: MKW address with no WSR match
RESORT_UNMAPPED_GX_STUB(80173d54, "GX__SetGPMetric_caseD_f_80173d54") RESORT_UNMAPPED_OVERRIDE_VOID(80173d54, gx_stub_80173d54, (CpuContext* ctx), (ctx));
#endif // RESORT-UNMAPPED
GX_FATAL_STUB(8003a798, "GX__SetGPMetric_caseD_10_8003a798") PPC_NATIVE_OVERRIDE_VOID(8003a798, gx_stub_8003a798, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a7c4, "GX__SetGPMetric_caseD_11_8003a7c4") PPC_NATIVE_OVERRIDE_VOID(8003a7c4, gx_stub_8003a7c4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a7d4, "GX__SetGPMetric_caseD_12_8003a7d4") PPC_NATIVE_OVERRIDE_VOID(8003a7d4, gx_stub_8003a7d4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a7e4, "GX__SetGPMetric_caseD_13_8003a7e4") PPC_NATIVE_OVERRIDE_VOID(8003a7e4, gx_stub_8003a7e4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a7f4, "GX__SetGPMetric_caseD_14_8003a7f4") PPC_NATIVE_OVERRIDE_VOID(8003a7f4, gx_stub_8003a7f4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a800, "GX__SetGPMetric_caseD_16_8003a800") PPC_NATIVE_OVERRIDE_VOID(8003a800, gx_stub_8003a800, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(8003a810, "GX__ClearGPMetric_8003a810") PPC_NATIVE_OVERRIDE_VOID(8003a810, gx_stub_8003a810, (CpuContext* ctx), (ctx));

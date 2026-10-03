// Data/instruction/locked cache maintenance HLE.

#include <cstdint>
#include <cstring>
#include <iostream>

#include "abi_bridge.h"
#include "memory.h"
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "runtime_log.h"
#include "hle/gx/gx_internal.h"

// ============================================================================
// Data cache maintenance (DCInvalidate/Flush/Store)
// These touch hardware on console; on host we fast-path them to avoid huge
// translated loops while still validating the guest range.
// ============================================================================
namespace {
struct CacheRange {
    uint32_t start = 0;
    uint32_t size = 0;
};

constexpr uint32_t kCacheLineSize = 32;

bool NormalizeCacheRange(uint32_t addr, uint32_t length, CacheRange& out)
{
    if (length == 0) {
        return false;
    }

    const uint32_t alignedStart = addr & ~(kCacheLineSize - 1u);
    const uint64_t end = static_cast<uint64_t>(addr) + static_cast<uint64_t>(length) + (kCacheLineSize - 1u);
    const uint64_t alignedEnd = end & ~(static_cast<uint64_t>(kCacheLineSize) - 1u);
    if (alignedEnd <= alignedStart || alignedEnd >= 0x100000000ull) {
        return false;
    }

    out.start = alignedStart;
    out.size = static_cast<uint32_t>(alignedEnd - alignedStart);
    return out.size != 0;
}

bool ValidateCacheRange(const char* label, const CacheRange& range)
{
    if (range.size == 0) {
        return false;
    }

    if (!::Memory::Contains(range.start, range.size)) {
        RT_LOG(RT_TAG_OS) << label << ": range 0x" << std::hex << range.start
                  << " len=0x" << range.size << std::dec
                  << " outside guest memory; skipping" << std::endl;
        return false;
    }
    return true;
}

void DcRangeOp(const char* label, uint32_t addr, uint32_t length)
{
    CacheRange range{};
    if (!NormalizeCacheRange(addr, length, range) || !ValidateCacheRange(label, range)) {
        return;
    }

    GxNotifyGuestRamDmaWrite(range.start, range.size);
}
} // namespace

extern "C" void DCInvalidateRange_800460b0(uint32_t addr, uint32_t length)
{
    DcRangeOp("DCInvalidateRange_800460b0", addr, length);
}

extern "C" void DCFlushRange_800460e0(uint32_t addr, uint32_t length)
{
    DcRangeOp("DCFlushRange_800460e0", addr, length);
}

extern "C" void DCStoreRange_80046110(uint32_t addr, uint32_t length)
{
    DcRangeOp("DCStoreRange_80046110", addr, length);
}

extern "C" void DCFlushRangeNoSync_80046140(uint32_t addr, uint32_t length)
{
    DcRangeOp("DCFlushRangeNoSync_80046140", addr, length);
}

extern "C" void DCStoreRangeNoSync_80046170(uint32_t addr, uint32_t length)
{
    DcRangeOp("DCStoreRangeNoSync_80046170", addr, length);
}

PPC_NATIVE_OVERRIDE_VOID(800460B0, DCInvalidateRange_800460b0, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(800460E0, DCFlushRange_800460e0, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(80046110, DCStoreRange_80046110, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(80046140, DCFlushRangeNoSync_80046140, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(80046170, DCStoreRangeNoSync_80046170, (uint32_t addr, uint32_t length), (addr, length));

// ----------------------------------------------------------------------------
// CPU Cache Maintenance Stubs (DC/IC/LC)
// These are safe to no-op because the host CPU handles caching.
// ----------------------------------------------------------------------------
extern "C" void Cache_Maintenance_Stub()
{
}

namespace {

constexpr uint32_t kCacheOpLineSize = 32u;
constexpr uint32_t kLcOpPageSize = 4096u;
constexpr uint32_t kLcOpMaxBlocksPerTransfer = 128u;

uint32_t AlignDown32(uint32_t value)
{
    return value & ~(kCacheOpLineSize - 1u);
}

uint32_t AlignUp32(uint32_t value)
{
    return (value + (kCacheOpLineSize - 1u)) & ~(kCacheOpLineSize - 1u);
}

uint32_t DecodeLcBlockCount(uint32_t encodedBlockCount)
{
    const uint32_t blocks = encodedBlockCount & 0x7Fu;
    return blocks == 0 ? kLcOpMaxBlocksPerTransfer : blocks;
}

bool CopyGuestRange(uint32_t dstAddr, uint32_t srcAddr, uint32_t len, const char* opName)
{
    if (len == 0) {
        return true;
    }

    try {
        auto* dst = ::Memory::GetPointer(dstAddr, len);
        auto* src = ::Memory::GetPointer(srcAddr, len);
        std::memmove(dst, src, len);
        return true;
    } catch (const ::Memory::AccessViolation& e) {
        RT_LOG(RT_TAG_OS) << opName << ": memory access failed @0x" << std::hex << e.address()
                  << " len=0x" << len << std::dec << " (" << e.reason() << ")" << std::endl;
        return false;
    }
}

} // namespace

extern "C" void DCZeroRange_HLE_800461a0(CpuContext* ctx)
{
    const uint32_t addr = static_cast<uint32_t>(ctx->gpr[3]);
    const uint32_t len = static_cast<uint32_t>(ctx->gpr[4]);
    if (len == 0) {
        return;
    }

    const uint32_t alignedAddr = AlignDown32(addr);
    const uint32_t alignedLen = AlignUp32((addr - alignedAddr) + len);
    try {
        auto* dst = ::Memory::GetPointer(alignedAddr, alignedLen);
        std::memset(dst, 0, alignedLen);
        GxNotifyGuestRamDmaWrite(alignedAddr, alignedLen);
    } catch (const ::Memory::AccessViolation& e) {
        RT_LOG(RT_TAG_OS) << "DCZeroRange: memory access failed @0x" << std::hex << e.address()
                  << " len=0x" << alignedLen << std::dec << " (" << e.reason() << ")" << std::endl;
    }
}

extern "C" void LCLoadBlocks_HLE_80046380(CpuContext* ctx)
{
    const uint32_t dstAddr = static_cast<uint32_t>(ctx->gpr[3]);
    const uint32_t srcAddr = static_cast<uint32_t>(ctx->gpr[4]);
    const uint32_t blocks = DecodeLcBlockCount(static_cast<uint32_t>(ctx->gpr[5]));
    const uint32_t len = blocks * kCacheOpLineSize;
    if (CopyGuestRange(dstAddr, srcAddr, len, "LCLoadBlocks")) {
        // RAM->LC loads are DMA reads on console, but the destination range is
        // still a guest RAM alias as far as the runtime is concerned; notify it
        // the same way LCStoreBlocks below does.
        GxNotifyGuestRamDmaWrite(dstAddr, len);
    }
}

extern "C" void LCStoreBlocks_HLE_800463b0(CpuContext* ctx)
{
    const uint32_t dstAddr = static_cast<uint32_t>(ctx->gpr[3]);
    const uint32_t srcAddr = static_cast<uint32_t>(ctx->gpr[4]);
    const uint32_t blocks = DecodeLcBlockCount(static_cast<uint32_t>(ctx->gpr[5]));
    const uint32_t len = blocks * kCacheOpLineSize;
    if (CopyGuestRange(dstAddr, srcAddr, len, "LCStoreBlocks")) {
        // LC->RAM stores are DMA writes on console; no flush follows them.
        GxNotifyGuestRamDmaWrite(dstAddr, len);
    }
}

extern "C" uint32_t LCStoreData_HLE_800463e0(CpuContext* ctx)
{
    const uint32_t dstAddr = static_cast<uint32_t>(ctx->gpr[3]);
    const uint32_t srcAddr = static_cast<uint32_t>(ctx->gpr[4]);
    const uint32_t len = static_cast<uint32_t>(ctx->gpr[5]);
    if (CopyGuestRange(dstAddr, srcAddr, len, "LCStoreData") && len != 0) {
        GxNotifyGuestRamDmaWrite(dstAddr, len);
    }

    const uint32_t pagesQueued = len == 0 ? 0u : ((len + kLcOpPageSize - 1u) / kLcOpPageSize);
    ctx->gpr[3] = pagesQueued;
    return pagesQueued;
}

extern "C" uint32_t LCQueueLength_HLE_80046480(CpuContext*)
{
    // We execute LC transfers synchronously, so the DMA queue is always drained.
    return 0;
}

extern "C" void LCQueueWait_HLE_80046490(CpuContext*)
{
    // Synchronous HLE copy completes immediately.
}

PPC_NATIVE_OVERRIDE_VOID(80046090, Cache_Maintenance_Stub, (), ()); // DCEnable
PPC_NATIVE_OVERRIDE_VOID(800461a0, DCZeroRange_HLE_800461a0, (CpuContext* ctx), (ctx)); // DCZeroRange
PPC_NATIVE_OVERRIDE_VOID(800461d0, Cache_Maintenance_Stub, (), ()); // ICInvalidateRange
PPC_NATIVE_OVERRIDE_VOID(80046210, Cache_Maintenance_Stub, (), ()); // ICFlashInvalidate
PPC_NATIVE_OVERRIDE_VOID(80046220, Cache_Maintenance_Stub, (), ()); // ICEnable
PPC_NATIVE_OVERRIDE_VOID(80046240, Cache_Maintenance_Stub, (), ()); // __LCEnable
PPC_NATIVE_OVERRIDE_VOID(80046310, Cache_Maintenance_Stub, (), ()); // LCEnable
PPC_NATIVE_OVERRIDE_VOID(80046350, Cache_Maintenance_Stub, (), ()); // LCDisable
PPC_NATIVE_OVERRIDE_VOID(80046380, LCLoadBlocks_HLE_80046380, (CpuContext* ctx), (ctx)); // LCLoadBlocks
PPC_NATIVE_OVERRIDE_VOID(800463b0, LCStoreBlocks_HLE_800463b0, (CpuContext* ctx), (ctx)); // LCStoreBlocks
PPC_NATIVE_OVERRIDE(800463e0, LCStoreData_HLE_800463e0, uint32_t, (CpuContext* ctx), (ctx)); // LCStoreData
PPC_NATIVE_OVERRIDE(80046480, LCQueueLength_HLE_80046480, uint32_t, (CpuContext* ctx), (ctx)); // LCQueueLength
PPC_NATIVE_OVERRIDE_VOID(80046490, LCQueueWait_HLE_80046490, (CpuContext* ctx), (ctx)); // LCQueueWait
PPC_NATIVE_OVERRIDE_VOID(80046600, Cache_Maintenance_Stub, (), ()); // OS____CacheInit

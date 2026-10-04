#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Riisorted::Netplay::Rollback {
// Caller must stop every reader/writer first, including audio, DVD and GPU
// readback workers. This is RAM storage, not a complete runtime savestate.
struct MemorySpan {
    uint32_t address;
    uint8_t* data;
    size_t size;
};
class PageStore {
    struct Budget;
    struct Page;
public:
    static constexpr size_t kPageBytes = 32 * 1024;
    class Image {
        friend class PageStore;
        std::shared_ptr<Budget> owner_;
        std::vector<std::pair<uint32_t, size_t>> layout_;
        std::vector<std::shared_ptr<const Page>> pages_;
        uint64_t digest_ = 0;
    public:
        uint64_t Digest() const noexcept { return digest_; }
    };
    explicit PageStore(size_t byteLimit = 256 * 1024 * 1024);
    Image Capture(const std::vector<MemorySpan>& spans, const Image* previous = nullptr);
    // Validate the entire layout before writing a byte. Shared immutable pages
    // keep old frames valid when current RAM changes or a newer image is freed.
    void Restore(const Image& image, const std::vector<MemorySpan>& spans) const;
    size_t ResidentBytes() const noexcept;
private:
    std::shared_ptr<Budget> budget_;
};

// One copy per physical bank; cached/uncached windows alias the same bytes.
// No host continuation, HLE state, MMIO or dynamically mapped fault region.
std::vector<MemorySpan> GuestRamSpans();
}

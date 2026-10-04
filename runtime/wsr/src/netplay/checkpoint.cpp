#include "netplay/checkpoint.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <array>

namespace Riisorted::Netplay::Rollback {
struct PageStore::Budget {
    size_t limit;
    std::atomic<size_t> resident{0};
    explicit Budget(size_t value) : limit(value) {}
};
struct PageStore::Page {
    std::array<uint8_t, kPageBytes> bytes{};
    uint64_t digest = 0;
};
namespace {
uint64_t Hash(const uint8_t* bytes, size_t size, uint64_t hash = 14695981039346656037ull) {
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return hash;
}
uint64_t Number(uint64_t value, uint64_t hash) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        hash ^= static_cast<uint8_t>(value >> shift); hash *= 1099511628211ull;
    }
    return hash;
}
std::vector<std::pair<uint32_t,size_t>> Layout(const std::vector<MemorySpan>& spans) {
    std::vector<std::pair<uint32_t,size_t>> result;
    uint64_t end = 0;
    for (const auto& span : spans) {
        if (!span.data || !span.size || uint64_t(span.address) < end ||
            span.size > (uint64_t(1) << 32) - span.address)
            throw std::invalid_argument("Invalid checkpoint memory layout");
        end = uint64_t(span.address) + span.size;
        result.emplace_back(span.address, span.size);
    }
    if (result.empty()) throw std::invalid_argument("Empty checkpoint memory layout");
    return result;
}
}
PageStore::PageStore(size_t byteLimit) : budget_(std::make_shared<Budget>(byteLimit)) {
    if (byteLimit < sizeof(Page)) throw std::invalid_argument("Checkpoint budget smaller than one page");
}
size_t PageStore::ResidentBytes() const noexcept { return budget_->resident.load(); }
PageStore::Image PageStore::Capture(const std::vector<MemorySpan>& spans, const Image* previous) {
    Image result;
    result.layout_ = Layout(spans);
    result.owner_ = budget_;
    if (previous && (previous->owner_ != budget_ || previous->layout_ != result.layout_))
        throw std::invalid_argument("Checkpoint image belongs to a different store or layout");
    result.digest_ = 14695981039346656037ull;
    size_t index = 0;
    for (const auto& span : spans) {
        result.digest_ = Number(span.address, result.digest_);
        result.digest_ = Number(span.size, result.digest_);
        for (size_t offset = 0; offset < span.size; offset += kPageBytes, ++index) {
            const size_t length = std::min(kPageBytes, span.size - offset);
            std::shared_ptr<const Page> page;
            if (previous && std::memcmp(previous->pages_.at(index)->bytes.data(), span.data + offset, length) == 0)
                page = previous->pages_[index];
            else {
                // Capture is guest-thread confined; the atomic counter also
                // accounts for images released by an owning harness thread.
                const size_t old = budget_->resident.fetch_add(sizeof(Page));
                if (old > budget_->limit - sizeof(Page)) {
                    budget_->resident.fetch_sub(sizeof(Page));
                    throw std::runtime_error("Checkpoint page memory limit reached");
                }
                Page* fresh;
                try { fresh = new Page(); }
                catch (...) { budget_->resident.fetch_sub(sizeof(Page)); throw; }
                auto budget = budget_;
                std::shared_ptr<Page> mutablePage(fresh, [budget](Page* value) {
                    delete value; budget->resident.fetch_sub(sizeof(Page));
                });
                std::memcpy(mutablePage->bytes.data(), span.data + offset, length);
                mutablePage->digest = Hash(mutablePage->bytes.data(), length);
                page = std::move(mutablePage);
            }
            result.digest_ = Number(page->digest, result.digest_);
            result.pages_.push_back(std::move(page));
        }
    }
    return result;
}
void PageStore::Restore(const Image& image, const std::vector<MemorySpan>& spans) const {
    if (image.owner_ != budget_ || Layout(spans) != image.layout_)
        throw std::invalid_argument("Checkpoint restore layout/store disagreement");
    size_t index = 0;
    for (const auto& span : spans)
        for (size_t offset = 0; offset < span.size; offset += kPageBytes)
            std::memcpy(span.data + offset, image.pages_[index++]->bytes.data(),
                        std::min(kPageBytes, span.size - offset));
}
}

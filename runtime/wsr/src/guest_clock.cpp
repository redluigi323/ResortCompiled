#include "guest_clock.h"
#include "timebase_contract.h"
#include <chrono>
#include <mutex>

namespace GuestClock {
namespace {
const auto start = std::chrono::steady_clock::now();
std::chrono::steady_clock::duration paused{};
std::chrono::steady_clock::time_point pauseStart{};
unsigned depth = 0;
std::mutex clockMutex;
}
uint64_t ReadTimeBase() noexcept {
    const auto elapsed = SchedulingNow() - start;
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    return TimeBaseContract::NanosecondsToTicks(static_cast<uint64_t>(nanos));
}
std::chrono::steady_clock::time_point SchedulingNow() noexcept {
    std::lock_guard<std::mutex> lock(clockMutex);
    return (depth ? pauseStart : std::chrono::steady_clock::now()) - paused;
}
std::chrono::steady_clock::time_point ToHostTime(std::chrono::steady_clock::time_point guest) noexcept {
    std::lock_guard<std::mutex> lock(clockMutex);
    return guest + paused + (depth ? std::chrono::steady_clock::now() - pauseStart :
                                    std::chrono::steady_clock::duration{});
}
Pause::Pause() noexcept {
    std::lock_guard<std::mutex> lock(clockMutex);
    if (depth++ == 0) pauseStart = std::chrono::steady_clock::now();
}
Pause::~Pause() {
    std::lock_guard<std::mutex> lock(clockMutex);
    if (--depth == 0) paused += std::chrono::steady_clock::now() - pauseStart;
}
}

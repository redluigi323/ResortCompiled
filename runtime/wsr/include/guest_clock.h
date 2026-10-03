#pragma once
#include <cstdint>
#include <chrono>

// Central entry point for guest-visible time. Offline time retains its real-time
// behavior. Session clock activation is intentionally unavailable until all VI,
// alarm, sleep and audio event sources obey deterministic scheduling.
namespace GuestClock {
uint64_t ReadTimeBase() noexcept;
// Guest event deadlines use this source; renderer pacing and host diagnostics
// continue to use steady_clock directly. Currently retains offline wall time.
std::chrono::steady_clock::time_point SchedulingNow() noexcept;
std::chrono::steady_clock::time_point ToHostTime(std::chrono::steady_clock::time_point guest) noexcept;
// Network waits do not become guest retrace/alarm/audio catch-up time.
class Pause {
public:
    Pause() noexcept;
    ~Pause();
    Pause(const Pause&) = delete;
    Pause& operator=(const Pause&) = delete;
};
}

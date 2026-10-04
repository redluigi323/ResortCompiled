#include "guest_clock.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <cstdio>

int main() {
    using namespace std::chrono;
    // Ordinary bridge work belongs inside, not after, the guest frame period.
    auto guest = GuestClock::SchedulingNow();
    auto host = steady_clock::now();
    for (unsigned i = 0; i < 25; ++i) {
        GuestClock::Pause pause(milliseconds(2));
        std::this_thread::sleep_for(milliseconds(1));
    }
    const auto bridgeGuest = GuestClock::SchedulingNow() - guest;
    const auto bridgeHost = steady_clock::now() - host;
    assert(bridgeGuest >= milliseconds(20));
    assert(bridgeGuest <= bridgeHost + milliseconds(1));

    // A large network stall still cannot inject a burst of guest timer/audio work.
    guest = GuestClock::SchedulingNow();
    {
        GuestClock::Pause pause(milliseconds(2));
        auto frozen = GuestClock::SchedulingNow();
        std::this_thread::sleep_for(milliseconds(30));
        assert(GuestClock::SchedulingNow() == frozen);
    }
    auto retained = GuestClock::SchedulingNow() - guest;
    assert(retained >= milliseconds(2) && retained < milliseconds(10));

    // Exercise VI's actual guest-deadline-to-host-deadline conversion with a
    // 1 ms bridge on each of 30 frames. Report pacing without a machine-speed
    // assertion: CI scheduling delays must not masquerade as protocol failures.
    const auto pace = [](steady_clock::duration budget) {
        const auto started = steady_clock::now();
        auto deadline = GuestClock::SchedulingNow();
        for (unsigned i = 0; i < 30; ++i) {
            deadline += microseconds(16667);
            { GuestClock::Pause pause(budget); std::this_thread::sleep_for(milliseconds(1)); }
            std::this_thread::sleep_until(GuestClock::ToHostTime(deadline));
        }
        return 30.0 / duration<double>(steady_clock::now() - started).count();
    };
    const auto oldFps = pace({});
    const auto correctedFps = pace(milliseconds(2));
    std::printf("Guest deadline pacing with 1 ms bridge: %.2f -> %.2f frames/s\n", oldFps, correctedFps);

    // Default boot waits remain fully excluded, including nested RAII pauses.
    guest = GuestClock::SchedulingNow();
    {
        GuestClock::Pause outer;
        { GuestClock::Pause inner(milliseconds(2)); std::this_thread::sleep_for(milliseconds(20)); }
    }
    assert(GuestClock::SchedulingNow() - guest < milliseconds(10));
}

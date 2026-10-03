#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace Riisorted::Netplay {

inline constexpr uint32_t kProtocolVersion = 1;
// Preserve solver inputs even when a device backlog exceeds KPAD's 16-entry
// output history. Oversized backlogs fail explicitly rather than lose motion.
inline constexpr size_t kMaxMotionSamples = 128;
inline constexpr size_t kMaxMotionPacketBytes = 24 + kMaxMotionSamples * 181;

// The locally mapped sensor input BEFORE the translated MotionPlus solver.
// IEEE binary64 fields retain the existing mapper's rounding points. The wire
// format writes each field explicitly; this struct is never memcpy-serialized.
struct MotionSample {
    uint32_t buttons = 0;
    std::array<double, 9> orientation{1,0,0, 0,1,0, 0,0,1};
    std::array<double, 3> gyro{};
    std::array<double, 3> angle{};
    std::array<double, 3> acceleration{0,-1,0};
    double seconds = 0.005;
    double pointerX = 0, pointerY = 0, roll = 0;
    bool pointerValid = false;
};

struct MotionBatch {
    uint64_t sequence = 0;
    uint64_t interval = 0;
    uint8_t channel = 0;
    uint8_t motionPlusMode = 0;
    bool recenter = false;
    // Oldest first: the solver must consume samples in their original order.
    std::vector<MotionSample> samples;
};

std::vector<uint8_t> EncodeMotionBatch(const MotionBatch& batch);
MotionBatch DecodeMotionBatch(const std::vector<uint8_t>& bytes);

// Logical Broadway time, advanced only by an agreed simulation event. No
// wall-clock reads and no implicit advancement when the network is waiting.
class SimulationClock {
public:
    uint64_t Ticks() const noexcept { return ticks_; }
    void AdvanceTo(uint64_t ticks);
    // Exact rational intervals; 60 Hz is 1,012,500 Broadway ticks per interval.
    void AdvanceInterval(uint32_t hz);
private:
    uint64_t ticks_ = 0;
    uint64_t remainder_ = 0;
    uint32_t rate_ = 0;
};

struct AgreedInputs {
    uint64_t interval = 0;
    std::array<MotionBatch, 2> players;
};

// Transport-independent two-player gate. A missing batch never becomes a
// predicted/neutral swing. The caller alone advances simulation after Take().
class LockstepQueue {
public:
    explicit LockstepQueue(uint64_t firstInterval = 0, uint32_t capacity = 120);
    void Submit(MotionBatch batch);
    bool Ready() const;
    std::optional<AgreedInputs> Take();
    uint64_t NextInterval() const noexcept { return next_; }
private:
    uint64_t next_;
    uint32_t capacity_;
    std::array<std::map<uint64_t, MotionBatch>, 2> pending_;
};

} // namespace Riisorted::Netplay

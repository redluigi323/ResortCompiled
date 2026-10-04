#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>
#include "netplay/session.h"

namespace Riisorted::Netplay::Rollback {

using Bytes = std::vector<uint8_t>;
using Inputs = std::array<Bytes, 2>;
enum class Pass { Forward, Replay };

// A frame is a logical gameplay update, never a GPU presentation or KPAD read.
// The adapter must cover continuations, timers, devices and pending callbacks,
// as well as RAM. No shipped game adapter currently satisfies this contract.
struct Adapter {
    std::function<Bytes()> save;
    std::function<void(const Bytes&)> load;
    std::function<void(uint64_t, const Inputs&, Pass)> advance;
    std::function<Bytes(const Bytes&)> predict;
    // Effects produced by advance are staged by frame, replaced during replay
    // and published here only after both real inputs are known.
    std::function<void(uint64_t)> commit;
    std::function<void(uint64_t)> discardFrom;
};
struct Limits {
    uint32_t window = 8;
    size_t snapshotBytes = 64 * 1024 * 1024;
    size_t inputBytes = kMaxMotionPacketBytes;
};

// Transport-independent rollback engine. EOS will supply Submit() once the
// game adapter can restore and advance exactly one logical update.
class Session {
public:
    explicit Session(Adapter adapter, Limits limits = {});
    void Submit(uint8_t player, uint64_t frame, Bytes input);
    // Reconcile corrected past input without advancing a new frame.
    void Reconcile();
    // False means the prediction window is full: pause, never drop input.
    bool Advance();
    uint64_t NextFrame() const noexcept { return next_; }
    uint64_t ConfirmedThrough() const noexcept { return confirmed_; } // exclusive
    uint64_t Rollbacks() const noexcept { return rollbacks_; }
    size_t SavedBytes() const noexcept { return savedBytes_; }
private:
    struct Frame {
        std::array<std::optional<Bytes>, 2> actual;
        Inputs used;
        Bytes before;
        bool executed = false;
    };
    Adapter adapter_;
    Limits limits_;
    std::map<uint64_t, Frame> frames_;
    Inputs lastConfirmed_;
    uint64_t next_ = 0, confirmed_ = 0, floor_ = 0, rollbacks_ = 0;
    size_t savedBytes_ = 0;
    std::optional<uint64_t> dirty_;
    bool failed_ = false;
    Inputs Select(uint64_t frame);
    void Execute(uint64_t frame, Pass pass);
    void Confirm();
    void Healthy() const;
};

// Hold pose and buttons, but do not invent another acceleration/gyro pulse,
// a recenter edge or a variable-size burst of motion samples.
MotionBatch PredictMotion(const MotionBatch& previous, uint64_t sequence,
                          uint64_t interval);
}

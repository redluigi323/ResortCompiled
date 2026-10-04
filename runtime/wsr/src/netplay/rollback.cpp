#include "netplay/rollback.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace Riisorted::Netplay::Rollback {
Session::Session(Adapter adapter, Limits limits)
    : adapter_(std::move(adapter)), limits_(limits) {
    if (!adapter_.save || !adapter_.load || !adapter_.advance || !adapter_.predict ||
        !adapter_.commit || !adapter_.discardFrom || !limits_.window ||
        limits_.window > 30 || !limits_.snapshotBytes || !limits_.inputBytes)
        throw std::invalid_argument("Incomplete rollback adapter or invalid rollback limits");
}
void Session::Healthy() const {
    if (failed_) throw std::logic_error("Rollback adapter failed; session cannot continue");
}
void Session::Submit(uint8_t player, uint64_t frame, Bytes input) {
    Healthy();
    if (player > 1 || input.size() > limits_.inputBytes)
        throw std::invalid_argument("Invalid rollback input");
    if (frame < floor_ || (frame > next_ && frame - next_ > limits_.window))
        throw std::out_of_range("Rollback input outside retained frame window");
    auto& entry = frames_[frame];
    auto& actual = entry.actual[player];
    if (actual) {
        if (*actual != input) throw std::runtime_error("Conflicting confirmed rollback input");
        return;
    }
    if (frame < confirmed_) throw std::logic_error("Missing input for committed frame");
    if (entry.executed && entry.used[player] != input)
        dirty_ = dirty_ ? std::min(*dirty_, frame) : frame;
    actual = std::move(input);
    // Confirmation is deferred until all corrected frames have been replayed.
}
Inputs Session::Select(uint64_t frame) {
    Inputs result;
    auto& entry = frames_.at(frame);
    for (unsigned player = 0; player < 2; ++player) {
        if (entry.actual[player]) result[player] = *entry.actual[player];
        else {
            const auto previous = frames_.find(frame == 0 ? UINT64_MAX : frame - 1);
            result[player] = adapter_.predict(previous == frames_.end() ?
                                              lastConfirmed_[player] : previous->second.used[player]);
        }
        if (result[player].size() > limits_.inputBytes)
            throw std::runtime_error("Rollback predictor exceeded input limit");
    }
    return result;
}
void Session::Execute(uint64_t frame, Pass pass) {
    auto& entry = frames_[frame];
    auto before = adapter_.save();
    const size_t withoutOld = savedBytes_ - entry.before.size();
    if (before.size() > limits_.snapshotBytes - withoutOld)
        throw std::runtime_error("Rollback snapshot memory limit reached");
    auto selected = Select(frame);
    savedBytes_ = withoutOld + before.size();
    entry.before = std::move(before);
    entry.used = std::move(selected);
    adapter_.advance(frame, entry.used, pass);
    entry.executed = true;
}
void Session::Confirm() {
    while (confirmed_ < next_) {
        auto& entry = frames_.at(confirmed_);
        if (!entry.actual[0] || !entry.actual[1]) break;
        adapter_.commit(confirmed_);
        lastConfirmed_ = entry.used;
        savedBytes_ -= entry.before.size();
        Bytes{}.swap(entry.before);
        ++confirmed_;
    }
    // Retain recent real inputs for duplicate validation, but no committed RAM.
    floor_ = confirmed_ > limits_.window ? confirmed_ - limits_.window : 0;
    while (!frames_.empty() && frames_.begin()->first < floor_) frames_.erase(frames_.begin());
}
void Session::Reconcile() {
    Healthy();
    try {
        if (dirty_) {
            const uint64_t first = *dirty_;
            if (first < confirmed_ || first >= next_ || next_ - first > limits_.window)
                throw std::logic_error("Rollback correction outside restorable window");
            adapter_.discardFrom(first);
            adapter_.load(frames_.at(first).before);
            // Future snapshots will be replaced; release them before capturing
            // replay states so the same window fits the same memory budget.
            for (auto it = frames_.lower_bound(first); it != frames_.end() && it->first < next_; ++it) {
                savedBytes_ -= it->second.before.size();
                Bytes{}.swap(it->second.before);
                it->second.executed = false;
            }
            for (uint64_t frame = first; frame < next_; ++frame) Execute(frame, Pass::Replay);
            dirty_.reset();
            ++rollbacks_;
        }
        Confirm();
    } catch (...) { failed_ = true; throw; }
}
bool Session::Advance() {
    Reconcile();
    if (next_ - confirmed_ >= limits_.window) return false;
    if (next_ == UINT64_MAX) throw std::overflow_error("Rollback frame counter overflow");
    try {
        Execute(next_, Pass::Forward);
        ++next_;
        Confirm();
    } catch (...) { failed_ = true; throw; }
    return true;
}
MotionBatch PredictMotion(const MotionBatch& previous, uint64_t sequence, uint64_t interval) {
    MotionBatch result;
    result.sequence = sequence;
    result.interval = interval;
    result.channel = previous.channel;
    result.motionPlusMode = previous.motionPlusMode;
    MotionSample sample;
    sample.seconds = 1.0 / 60.0;
    if (!previous.samples.empty()) {
        sample = previous.samples.back();
        sample.gyro = {};
        sample.angle = {};
        // A coarse held-gravity estimate: remove pulse magnitude. Physical
        // controller orientation is not currently reconstructed by the mapper,
        // so its pose matrix cannot supply a trustworthy gravity direction.
        const double length = std::hypot(sample.acceleration[0], sample.acceleration[1], sample.acceleration[2]);
        sample.acceleration = {0,-1,0};
        if (std::isfinite(length) && length > 0.001)
            for (unsigned axis = 0; axis < 3; ++axis)
                sample.acceleration[axis] = previous.samples.back().acceleration[axis] / length;
    }
    result.samples.push_back(sample);
    EncodeMotionBatch(result); // Same finite/range checks as real wire inputs.
    return result;
}
}

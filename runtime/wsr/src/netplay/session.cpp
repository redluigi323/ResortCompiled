#include "netplay/session.h"
#include "timebase_contract.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Riisorted::Netplay {
namespace {
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
constexpr size_t kSampleBytes = 4 + 22 * 8 + 1;
constexpr size_t kHeaderBytes = 4 + 8 + 8 + 4;
static_assert(kHeaderBytes + kMaxMotionSamples * kSampleBytes == kMaxMotionPacketBytes);

void Put(std::vector<uint8_t>& out, uint64_t value, size_t count) {
    for (size_t i = count; i != 0; --i)
        out.push_back(static_cast<uint8_t>(value >> ((i - 1) * 8)));
}
void PutDouble(std::vector<uint8_t>& out, double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Non-finite motion input");
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    Put(out, bits, 8);
}
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& data) : data_(data) {}
    uint64_t Get(size_t count) {
        if (count > data_.size() - offset_) throw std::invalid_argument("Truncated motion packet");
        uint64_t result = 0;
        while (count--) result = (result << 8) | data_[offset_++];
        return result;
    }
    double Double() {
        uint64_t bits = Get(8);
        double result;
        std::memcpy(&result, &bits, sizeof(result));
        if (!std::isfinite(result)) throw std::invalid_argument("Non-finite motion input");
        return result;
    }
private:
    const std::vector<uint8_t>& data_;
    size_t offset_ = 0;
};
void Validate(const MotionBatch& batch) {
    if (batch.channel >= 2 || batch.samples.empty() || batch.samples.size() > kMaxMotionSamples ||
        batch.motionPlusMode > 5)
        throw std::invalid_argument("Invalid motion batch metadata");
    for (const auto& sample : batch.samples) {
        if (!(sample.seconds > 0 && sample.seconds <= 0.1))
            throw std::invalid_argument("Invalid motion sample duration");
    }
}
} // namespace

std::vector<uint8_t> EncodeMotionBatch(const MotionBatch& batch) {
    Validate(batch);
    std::vector<uint8_t> out;
    out.reserve(kHeaderBytes + batch.samples.size() * kSampleBytes);
    Put(out, kProtocolVersion, 4);
    Put(out, batch.sequence, 8);
    Put(out, batch.interval, 8);
    Put(out, batch.channel, 1);
    Put(out, batch.motionPlusMode, 1);
    Put(out, batch.recenter, 1);
    Put(out, batch.samples.size(), 1);
    for (const auto& sample : batch.samples) {
        Put(out, sample.buttons, 4);
        for (double value : sample.orientation) PutDouble(out, value);
        for (double value : sample.gyro) PutDouble(out, value);
        for (double value : sample.angle) PutDouble(out, value);
        for (double value : sample.acceleration) PutDouble(out, value);
        PutDouble(out, sample.seconds);
        PutDouble(out, sample.pointerX);
        PutDouble(out, sample.pointerY);
        PutDouble(out, sample.roll);
        Put(out, sample.pointerValid, 1);
    }
    return out;
}

MotionBatch DecodeMotionBatch(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < kHeaderBytes || bytes.size() > kHeaderBytes + kMaxMotionSamples * kSampleBytes)
        throw std::invalid_argument("Invalid motion packet size");
    Reader in(bytes);
    if (in.Get(4) != kProtocolVersion) throw std::invalid_argument("Motion protocol version mismatch");
    MotionBatch batch;
    batch.sequence = in.Get(8);
    batch.interval = in.Get(8);
    batch.channel = static_cast<uint8_t>(in.Get(1));
    batch.motionPlusMode = static_cast<uint8_t>(in.Get(1));
    const auto recenter = in.Get(1);
    const auto count = in.Get(1);
    if (recenter > 1 || count == 0 || count > kMaxMotionSamples ||
        bytes.size() != kHeaderBytes + count * kSampleBytes)
        throw std::invalid_argument("Invalid motion packet layout");
    batch.recenter = recenter != 0;
    batch.samples.resize(count);
    for (auto& sample : batch.samples) {
        sample.buttons = static_cast<uint32_t>(in.Get(4));
        for (double& value : sample.orientation) value = in.Double();
        for (double& value : sample.gyro) value = in.Double();
        for (double& value : sample.angle) value = in.Double();
        for (double& value : sample.acceleration) value = in.Double();
        sample.seconds = in.Double();
        sample.pointerX = in.Double();
        sample.pointerY = in.Double();
        sample.roll = in.Double();
        const auto valid = in.Get(1);
        if (valid > 1) throw std::invalid_argument("Invalid pointer validity flag");
        sample.pointerValid = valid != 0;
    }
    Validate(batch);
    return batch;
}

void SimulationClock::Restore(State state) {
    if ((!state.rate && state.remainder) || (state.rate && state.remainder >= state.rate))
        throw std::invalid_argument("Invalid simulation clock checkpoint");
    ticks_ = state.ticks;
    remainder_ = state.remainder;
    rate_ = state.rate;
}
void SimulationClock::AdvanceTo(uint64_t ticks) {
    if (ticks < ticks_) throw std::invalid_argument("Simulation time cannot go backwards");
    ticks_ = ticks;
    remainder_ = 0;
    rate_ = 0;
}
void SimulationClock::AdvanceInterval(uint32_t hz) {
    if (hz == 0 || (rate_ && rate_ != hz))
        throw std::invalid_argument("Clock rate changes require an explicit time boundary");
    const uint64_t total = TimeBaseContract::kTicksPerSecond + remainder_;
    const uint64_t delta = total / hz;
    if (delta > UINT64_MAX - ticks_) throw std::overflow_error("Simulation clock overflow");
    rate_ = hz;
    remainder_ = total % hz;
    ticks_ += delta;
}

LockstepQueue::LockstepQueue(uint64_t firstInterval, uint32_t capacity)
    : next_(firstInterval), capacity_(capacity) {
    if (capacity == 0 || capacity > 600) throw std::invalid_argument("Invalid input queue capacity");
}
void LockstepQueue::Submit(MotionBatch batch) {
    // Validate even locally supplied input through the same canonical codec.
    const auto encoded = EncodeMotionBatch(batch);
    if (batch.interval < next_) return; // Already agreed/consumed retransmission.
    if (batch.interval - next_ >= capacity_) throw std::invalid_argument("Input beyond session window");
    auto& queue = pending_[batch.channel];
    auto found = queue.find(batch.interval);
    if (found != queue.end()) {
        if (EncodeMotionBatch(found->second) != encoded)
            throw std::invalid_argument("Conflicting input for an existing session interval");
        return;
    }
    queue.emplace(batch.interval, std::move(batch));
}
bool LockstepQueue::Ready() const {
    return pending_[0].count(next_) && pending_[1].count(next_);
}
std::optional<AgreedInputs> LockstepQueue::Take() {
    if (!Ready()) return std::nullopt;
    if (next_ == UINT64_MAX) throw std::overflow_error("Session interval overflow");
    AgreedInputs result;
    result.interval = next_;
    for (size_t i = 0; i < 2; ++i) {
        result.players[i] = std::move(pending_[i].at(next_));
        pending_[i].erase(next_);
    }
    ++next_;
    return result;
}
} // namespace Riisorted::Netplay

#include "netplay/motionplus_report.h"
namespace Riisorted::Netplay {
std::vector<uint8_t> EncodeMotionPlusReport(const MotionPlusReport& r) {
    std::vector<uint8_t> out{'D',1};
    const auto put = [&](uint64_t value, unsigned size) {
        for (int i=int(size)-1;i>=0;--i) out.push_back(uint8_t(value>>(i*8)));
    };
    put(r.sequence,8); put(r.frame,8);
    for (auto mode:r.modes) { if(mode>5)throw std::invalid_argument("Invalid solver mode"); put(mode,1); }
    for (unsigned channel=0;channel<2;++channel) {
        const auto& samples=r.samples[channel]; const auto& state=r.states[channel];
        if(samples.empty() || samples.size()>kMaxMotionSamples ||
           (!state.empty() && state.size()!=kMotionPlusStateBytes))
            throw std::invalid_argument("Invalid solver report bounds");
        put(samples.size(),1); put(state.empty()?0:1,1);
        for(const auto& s:samples) {put(s.valid?1:0,1);for(auto word:s.words)put(word,4);}
        out.insert(out.end(),state.begin(),state.end());
    }
    return out;
}
MotionPlusReport DecodeMotionPlusReport(const std::vector<uint8_t>& bytes) {
    size_t cursor=0;
    const auto get=[&](unsigned size) {
        if(size>bytes.size()-cursor)throw std::invalid_argument("Truncated solver report");
        uint64_t value=0; while(size--)value=(value<<8)|bytes[cursor++]; return value;
    };
    if(get(1)!='D' || get(1)!=1)throw std::invalid_argument("Invalid solver report version");
    MotionPlusReport r; r.sequence=get(8);r.frame=get(8);
    for(auto& mode:r.modes) {mode=get(1);if(mode>5)throw std::invalid_argument("Invalid solver report mode");}
    for(unsigned channel=0;channel<2;++channel) {
        const auto count=get(1),present=get(1);
        if(!count || count>kMaxMotionSamples || present>1)throw std::invalid_argument("Invalid solver report bounds");
        for(unsigned i=0;i<count;++i) {
            SolverSample s;const auto valid=get(1);if(valid>1)throw std::invalid_argument("Invalid solver validity");
            s.valid=valid;for(auto& word:s.words)word=get(4);r.samples[channel].push_back(s);
        }
        if(present)for(size_t i=0;i<kMotionPlusStateBytes;++i)r.states[channel].push_back(get(1));
    }
    if(cursor!=bytes.size())throw std::invalid_argument("Trailing solver report bytes");
    return r;
}
}

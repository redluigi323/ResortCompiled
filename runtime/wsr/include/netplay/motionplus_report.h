#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "netplay/session.h"
namespace Riisorted::Netplay {
// SDK-owned 3504-byte channel state with its three raw-buffer pointers removed.
// Guest pointers remain local; this report never transfers native process state.
inline constexpr size_t kMotionPlusStateBytes = 3492;
struct SolverSample {
    bool valid = false;
    std::array<uint32_t,15> words{};
};
struct MotionPlusReport {
    uint64_t sequence = 0, frame = 0;
    std::array<uint8_t,2> modes{};
    std::array<std::vector<SolverSample>,2> samples;
    std::array<std::vector<uint8_t>,2> states;
};
std::vector<uint8_t> EncodeMotionPlusReport(const MotionPlusReport& report);
MotionPlusReport DecodeMotionPlusReport(const std::vector<uint8_t>& bytes);
}

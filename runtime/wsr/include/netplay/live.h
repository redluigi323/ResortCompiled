#pragma once
#include "netplay/session.h"

namespace Riisorted::Netplay::Live {
// The launcher owns internet TLS, session data and lifecycle. The native runtime
// talks only to its authenticated loopback broker, never an unverified remote.
void InitializeFromEnvironment();
bool Active() noexcept;
uint8_t LocalChannel() noexcept;
// Host publishes canonical virtual MotionPlus results; guest receives them.
std::vector<uint8_t> ShareMotionPlusReport(const std::vector<uint8_t>& hostReport = {});
AgreedInputs Exchange(const MotionBatch& local, const std::array<uint8_t, 2>& modes,
                     uint64_t previousSolverHash);
}

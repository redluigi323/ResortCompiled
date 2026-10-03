#pragma once
#include "netplay/session.h"

namespace Riisorted::Netplay::InputTape {
// Experimental local motion playback, NOT a complete deterministic game replay.
// RESORT_INPUT_RECORD and RESORT_INPUT_REPLAY are mutually exclusive file paths.
void InitializeFromEnvironment();
bool Recording() noexcept;
bool Replaying() noexcept;
void Record(const MotionBatch& batch);
// Diagnostic only: compare translated MotionPlus outputs after each batch.
// A matching hash is NOT proof of matching complete game state.
void ObserveSolverResult(uint64_t sequence, uint64_t hash);
// Empty at a clean end marker. The caller exits without unwinding native fibers.
std::optional<MotionBatch> Replay(uint64_t displayFrame, uint8_t motionPlusMode);
void Finish();
}

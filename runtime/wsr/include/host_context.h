#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <cfenv>

// HostContext is the deliberately small boundary between the guest scheduler
// and the host's cooperative-context facility. Windows uses native Fibers and
// Linux uses libco; macOS AArch64 uses the local assembly backend because it
// must preserve Darwin's platform-reserved x18 register, which libco's AArch64
// backend does not save. Its handles are only valid on the thread that
// initialized the scheduler.
namespace HostContext {

using Handle = void*;
using Entry = void (*)(void*);
enum class Backend { Default, Rewindable };

// Local suspended continuation only. RAM, heap objects, TLS, callbacks and
// scheduler state must be checkpointed separately at a quiescent boundary.
// Never serialize these native pointers or register bytes to a peer.
class Snapshot {
    friend Snapshot Capture(Handle);
    friend void Restore(Handle, const Snapshot&);
    friend void ValidateRestore(Handle, const Snapshot&);
    friend struct SnapshotAccess;
    uint64_t generation_ = 0;
    Handle handle_ = nullptr;
    uintptr_t stackStart_ = 0;
    std::vector<uint8_t> registers_, stack_;
    std::fenv_t floatingPoint_{};
    uint32_t mxcsr_ = 0;
public:
    size_t Bytes() const noexcept { return registers_.size() + stack_.size() + sizeof(floatingPoint_) + sizeof(mxcsr_); }
};

bool InitializeScheduler(Handle* scheduler, Backend backend = Backend::Default);
void ShutdownScheduler(Handle scheduler);

Handle Create(std::size_t stackSize, Entry entry, void* argument);
void Destroy(Handle context);
bool IsCurrent(Handle context);
void Switch(Handle target);
Snapshot Capture(Handle suspended);
void ValidateRestore(Handle suspended, const Snapshot& snapshot);
void Restore(Handle suspended, const Snapshot& snapshot);

} // namespace HostContext

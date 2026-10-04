#pragma once
#include "host_context.h"
namespace HostContext::Rewindable {
bool InitializeScheduler(Handle*);
void ShutdownScheduler(Handle);
Handle Create(size_t, Entry, void*);
void Destroy(Handle);
bool IsCurrent(Handle);
void Switch(Handle);
Snapshot Capture(Handle);
void ValidateRestore(Handle, const Snapshot&);
void Restore(Handle, const Snapshot&);
}

#include "netplay/checkpoint.h"
#include "memory.h"

namespace Riisorted::Netplay::Rollback {
std::vector<MemorySpan> GuestRamSpans() {
    return {{Memory::kMem1CachedBase, Memory::GetPointer(Memory::kMem1CachedBase, Memory::kMem1Size), Memory::kMem1Size},
            {Memory::kMem2CachedBase, Memory::GetPointer(Memory::kMem2CachedBase, Memory::kMem2Size), Memory::kMem2Size}};
}
}

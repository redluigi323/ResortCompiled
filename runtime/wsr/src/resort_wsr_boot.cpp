// Resortcompiled: Wii Sports Resort (RZTP01) boot-flow HLE.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"

// During boot (func_8022DD40) WSR calls this SC getter - SCFindBoolItem(item 0x24) - to ask whether the
// MotionPlus explanation movie has been shown on this console. If not, it OSExecs
// "sys/mpls_movie/player.dol" (the 7 RELs under files/sys/mpls_movie belong to that player), which the port
// cannot run: the main thread sits in the exec path forever on a black screen.
// Report "already watched" so boot continues straight into the game.
// TODO: recompile player.dol + its RELs as a second product if we ever want the movie.
extern "C" uint32_t SCGetMplsMovieWatched_HLE() {
    return 1;
}
PPC_NATIVE_OVERRIDE(80054320, SCGetMplsMovieWatched_HLE, uint32_t, (), ());

// OSExec-style DOL launch (matched as MKW's "OS::LaunchInstaller"). Launching another executable from the
// disc is not supported by a static recompilation; fail loudly with the requested path instead of hanging.
extern "C" void OSLaunchDol_HLE(CpuContext* ctx) {
    const char* path = "?";
    if (ctx && ctx->gpr[3] && Memory::Contains(ctx->gpr[3], 1)) {
        path = reinterpret_cast<const char*>(Memory::GetPointer(ctx->gpr[3]));
    }
    std::fprintf(stderr, "[resortcompiled] game asked to launch another executable from disc: '%s' - not supported\n",
                 path);
    std::abort();
}
PPC_NATIVE_OVERRIDE_VOID(80048DF0, OSLaunchDol_HLE, (CpuContext* ctx), (ctx));

// Resortcompiled: IPL ROM reads (__OSReadROM) for the system fonts.
//
// WSR calls OSInitFont -> ReadFont, which pulls the Yay0-compressed IPL font out of the console's boot ROM
// over EXI (__OSReadROM: ANSI/Windows-1252 font at ROM offset 0x1FCF00, Shift-JIS font at 0x1AFF00). The
// runtime stubs EXI transfers, so ReadFont got no data and spun forever. This serves those ROM ranges from
// font files next to the executable (or in UserData/): font_western.bin and font_japanese.bin.
// setup.sh fetches Dolphin's freely-licensed replacement fonts (Data/Sys/GC) for this; a dump of your own
// console's fonts works too.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

#include "hle_stubs.h"
#include "memory.h"
#include "runtime_config.h"

namespace {

struct RomFont {
    const char* file;
    uint32_t romOffset;
    uint32_t romSize;
    std::vector<uint8_t> data;
    bool tried = false;
};

RomFont g_fonts[] = {
    {"font_western.bin", 0x001FCF00u, 0x00003000u, {}},
    {"font_japanese.bin", 0x001AFF00u, 0x0004D000u, {}},
};
std::mutex g_romMutex;

void LoadFont(RomFont& font) {
    if (font.tried) {
        return;
    }
    font.tried = true;
    std::vector<std::filesystem::path> candidates;
    if (const auto exeDir = RuntimeConfigFile::ExecutableDirectory()) {
        candidates.push_back(*exeDir / font.file);
        candidates.push_back(*exeDir / "fonts" / font.file);
    }
    candidates.push_back(RuntimeConfigFile::ApplicationDataDirectory() / font.file);
    for (const auto& path : candidates) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            continue;
        }
        font.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        std::fprintf(stderr, "[ipl] loaded %s (%zu bytes) for ROM 0x%08X\n", path.string().c_str(),
                     font.data.size(), font.romOffset);
        return;
    }
    std::fprintf(stderr,
                 "[ipl] FATAL: the game reads the IPL system font (%s) but it was not found next to the "
                 "executable or in UserData/. Re-run setup.sh (it downloads Dolphin's replacement fonts) or copy "
                 "%s next to out/Resortcompiled.\n",
                 font.file, font.file);
    std::abort();
}

} // namespace

extern "C" int32_t OSReadROM_HLE(uint32_t buf, uint32_t length, uint32_t offset) {
    if (length == 0) {
        return 1;
    }
    if (!Memory::Contains(buf, length)) {
        return 0;
    }
    uint8_t* dst = Memory::GetPointer(buf, length);
    std::memset(dst, 0, length);
    std::lock_guard<std::mutex> lock(g_romMutex);
    for (auto& font : g_fonts) {
        const uint64_t start = std::max<uint64_t>(offset, font.romOffset);
        const uint64_t end = std::min<uint64_t>(uint64_t(offset) + length, uint64_t(font.romOffset) + font.romSize);
        if (start >= end) {
            continue;
        }
        LoadFont(font);
        const uint64_t srcOff = start - font.romOffset;
        if (srcOff >= font.data.size()) {
            continue;
        }
        const uint64_t n = std::min<uint64_t>(end - start, font.data.size() - srcOff);
        std::memcpy(dst + (start - offset), font.data.data() + srcOff, n);
    }
    return 1;
}
PPC_NATIVE_OVERRIDE(8004D4A0, OSReadROM_HLE, int32_t, (uint32_t buf, uint32_t length, uint32_t offset),
                    (buf, length, offset));

#include "netplay/input_tape.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <stdexcept>

namespace Riisorted::Netplay::InputTape {
namespace {
enum class Mode { Off, Record, Replay };
struct Tape {
    Mode mode = Mode::Off;
    FILE* file = nullptr;
    FILE* solverFile = nullptr;
    uint64_t sequence = 0;
    uint64_t solverMismatches = 0;
    ~Tape() { if (file) std::fclose(file); if (solverFile) std::fclose(solverFile); }
} tape;

void Write(const void* data, size_t size) {
    if (std::fwrite(data, 1, size, tape.file) != size)
        throw std::runtime_error("Could not write motion input recording");
}
void Read(void* data, size_t size) {
    if (std::fread(data, 1, size, tape.file) != size)
        throw std::runtime_error("Incomplete motion recording: missing data or clean end marker");
}
void Write32(uint32_t value) {
    const uint8_t bytes[]{uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
    Write(bytes, sizeof(bytes));
}
uint32_t Read32() {
    uint8_t bytes[4];
    Read(bytes, sizeof(bytes));
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
           (uint32_t(bytes[2]) << 8) | bytes[3];
}
uint32_t Checksum(const std::vector<uint8_t>& bytes) {
    uint32_t value = 2166136261u;
    for (uint8_t byte : bytes) value = (value ^ byte) * 16777619u;
    return value;
}
std::array<uint8_t, 32> Conventions() {
    std::array<double, 4> values{1,1,1,1};
    if (const char* text = std::getenv("RESORT_MPLS_SIGNS")) {
        double a = 1, b = 1, c = 1;
        if (std::sscanf(text, "%lf,%lf,%lf", &a, &b, &c) == 3)
            values[0] = a, values[1] = b, values[2] = c;
    }
    if (const char* text = std::getenv("RESORT_MPLS_DIRSIGN"))
        values[3] = std::atof(text) < 0 ? -1 : 1;
    std::array<uint8_t, 32> result{};
    for (size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) throw std::invalid_argument("Invalid MotionPlus conventions");
        uint64_t bits;
        std::memcpy(&bits, &values[i], sizeof(bits));
        for (size_t j = 0; j < 8; ++j) result[i * 8 + j] = uint8_t(bits >> ((7 - j) * 8));
    }
    return result;
}
FILE* Open(const char* path, bool record) {
#ifdef _WIN32
    return _wfopen(std::filesystem::u8path(path).c_str(), record ? L"wbx" : L"rb");
#else
    return std::fopen(path, record ? "wbx" : "rb");
#endif
}
} // namespace

void InitializeFromEnvironment() {
    const char* record = std::getenv("RESORT_INPUT_RECORD");
    const char* replay = std::getenv("RESORT_INPUT_REPLAY");
    if (record && !*record) record = nullptr;
    if (replay && !*replay) replay = nullptr;
    if (record && replay) throw std::invalid_argument("Choose motion recording OR playback");
    if (!record && !replay) return;
    if (tape.file) throw std::logic_error("Motion tape already initialized");
    const auto conventions = Conventions();
    tape.file = Open(record ? record : replay, record != nullptr);
    if (!tape.file) throw std::runtime_error(record
        ? "Cannot create motion recording (choose a new file in an existing folder)"
        : "Cannot open motion recording for playback");
    constexpr std::array<char, 8> magic{'R','I','I','S','I','N','P','1'};
    if (record) {
        Write(magic.data(), magic.size());
        Write32(kProtocolVersion);
        Write(conventions.data(), conventions.size());
        tape.mode = Mode::Record;
    } else {
        std::array<char, 8> received;
        Read(received.data(), received.size());
        if (received != magic || Read32() != kProtocolVersion)
            throw std::runtime_error("Incompatible motion recording version");
        std::array<uint8_t, 32> receivedConventions;
        Read(receivedConventions.data(), receivedConventions.size());
        if (receivedConventions != conventions)
            throw std::runtime_error("MotionPlus conventions differ from the recording");
        tape.mode = Mode::Replay;
    }
    std::fprintf(stderr, "[riisorted] motion %s: %s (experimental; runtime time is still offline)\n",
                 record ? "recording" : "playback", record ? record : replay);
    const std::string solverPath = std::string(record ? record : replay) + ".solver";
    tape.solverFile = Open(solverPath.c_str(), record != nullptr);
    if (record && !tape.solverFile) throw std::runtime_error("Cannot create MotionPlus comparison file");
    constexpr char solverMagic[] = "RIISCHK1";
    if (tape.solverFile) {
        if (record) {
            if (std::fwrite(solverMagic, 1, 8, tape.solverFile) != 8)
                throw std::runtime_error("Cannot write MotionPlus comparison header");
        } else {
            char received[8];
            if (std::fread(received, 1, 8, tape.solverFile) != 8 || std::memcmp(received, solverMagic, 8))
                throw std::runtime_error("Invalid MotionPlus comparison header");
        }
    } else {
        std::fprintf(stderr, "[riisorted] older recording: no MotionPlus result comparisons available\n");
    }
}
bool Recording() noexcept { return tape.mode == Mode::Record; }
bool Replaying() noexcept { return tape.mode == Mode::Replay; }

void ObserveSolverResult(uint64_t sequence, uint64_t hash) {
    if (!tape.solverFile) return;
    uint8_t bytes[16];
    for (size_t i = 0; i < 8; ++i) {
        bytes[i] = uint8_t(sequence >> ((7 - i) * 8));
        bytes[8 + i] = uint8_t(hash >> ((7 - i) * 8));
    }
    if (Recording()) {
        if (std::fwrite(bytes, 1, sizeof(bytes), tape.solverFile) != sizeof(bytes) ||
            std::fflush(tape.solverFile))
            throw std::runtime_error("Could not write MotionPlus comparison");
    } else {
        uint8_t expected[16];
        if (std::fread(expected, 1, sizeof(expected), tape.solverFile) != sizeof(expected))
            throw std::runtime_error("Incomplete MotionPlus comparison file");
        if (std::memcmp(expected, bytes, 8))
            throw std::runtime_error("MotionPlus comparison sequence mismatch");
        if (std::memcmp(expected + 8, bytes + 8, 8)) {
            if (tape.solverMismatches++ == 0)
                std::fprintf(stderr, "[riisorted] first MotionPlus solver output difference at batch %llu\n",
                             static_cast<unsigned long long>(sequence));
        }
    }
}

void Record(const MotionBatch& batch) {
    if (!Recording()) return;
    if (batch.sequence != tape.sequence) throw std::logic_error("Motion recording sequence mismatch");
    const auto bytes = EncodeMotionBatch(batch);
    Write32(static_cast<uint32_t>(bytes.size()));
    Write32(Checksum(bytes));
    Write(bytes.data(), bytes.size());
    if (std::fflush(tape.file) != 0) throw std::runtime_error("Cannot flush motion recording");
    ++tape.sequence;
}
std::optional<MotionBatch> Replay(uint64_t displayFrame, uint8_t motionPlusMode) {
    if (!Replaying()) throw std::logic_error("Motion playback is not active");
    const uint32_t size = Read32();
    const uint32_t checksum = Read32();
    if (size == 0) {
        if (checksum || std::fgetc(tape.file) != EOF || std::ferror(tape.file))
            throw std::runtime_error("Invalid motion recording end marker");
        if (tape.solverFile && (std::fgetc(tape.solverFile) != EOF || std::ferror(tape.solverFile)))
            throw std::runtime_error("MotionPlus comparison has unexpected trailing data");
        return std::nullopt;
    }
    if (size > kMaxMotionPacketBytes) throw std::runtime_error("Motion recording packet exceeds size limit");
    std::vector<uint8_t> bytes(size);
    Read(bytes.data(), bytes.size());
    if (Checksum(bytes) != checksum) throw std::runtime_error("Motion recording checksum mismatch");
    MotionBatch batch = DecodeMotionBatch(bytes);
    if (batch.channel != 0 || batch.sequence != tape.sequence || batch.interval != displayFrame ||
        batch.motionPlusMode != motionPlusMode)
        throw std::runtime_error("Motion playback diverged at batch " + std::to_string(tape.sequence) +
                                 ": expected display frame " + std::to_string(batch.interval) +
                                 ", observed " + std::to_string(displayFrame) +
                                 "; recorded/current MotionPlus mode " +
                                 std::to_string(batch.motionPlusMode) + "/" + std::to_string(motionPlusMode));
    ++tape.sequence;
    return batch;
}
void Finish() {
    if (!tape.file) return;
    if (Recording()) {
        Write32(0);
        Write32(0);
    }
    if (tape.solverFile) {
        FILE* solver = tape.solverFile;
        tape.solverFile = nullptr;
        if (std::fclose(solver)) throw std::runtime_error("Could not close MotionPlus comparison file");
        if (Replaying())
            std::fprintf(stderr, "[riisorted] MotionPlus comparison: %llu differing batches\n",
                         static_cast<unsigned long long>(tape.solverMismatches));
        else
            std::fprintf(stderr, "[riisorted] MotionPlus result checkpoints saved\n");
    }
    FILE* file = tape.file;
    tape.file = nullptr;
    tape.mode = Mode::Off;
    if (std::fclose(file) != 0) throw std::runtime_error("Could not close motion recording");
    std::fprintf(stderr, "[riisorted] motion tape closed after %llu batches\n",
                 static_cast<unsigned long long>(tape.sequence));
}
} // namespace Riisorted::Netplay::InputTape

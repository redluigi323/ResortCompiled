#include "netplay/live.h"
#include "guest_clock.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <deque>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Riisorted::Netplay::Live {
namespace {
bool active = false;
uint8_t channel = 0;
uint32_t inputDelay = 0;
std::deque<MotionBatch> captures;
#if defined(__linux__) || defined(_WIN32)
struct Connection {
#if defined(_WIN32)
    SOCKET fd = INVALID_SOCKET;
    bool initialized = false;
    ~Connection() { if (fd != INVALID_SOCKET) closesocket(fd); if (initialized) WSACleanup(); }
#else
    int fd = -1;
    ~Connection() { if (fd >= 0) close(fd); }
#endif
} connection;
void Transfer(void* data, size_t size, bool write) {
    auto* bytes = static_cast<uint8_t*>(data);
    while (size) {
        const auto count = write ? send(connection.fd, reinterpret_cast<const char*>(bytes), static_cast<int>(size),
#if defined(_WIN32)
                                       0)
#else
                                       MSG_NOSIGNAL)
#endif
                                 : recv(connection.fd, reinterpret_cast<char*>(bytes), static_cast<int>(size), 0);
        if (count <= 0) throw std::runtime_error("Online session disconnected or timed out; session saves were not committed");
        bytes += count;
        size -= static_cast<size_t>(count);
    }
}
void Send(const std::vector<uint8_t>& bytes) {
    uint32_t size = htonl(static_cast<uint32_t>(bytes.size()));
    Transfer(&size, 4, true);
    Transfer(const_cast<uint8_t*>(bytes.data()), bytes.size(), true);
}
std::vector<uint8_t> Receive() {
    uint32_t size;
    Transfer(&size, 4, false);
    size = ntohl(size);
    if (!size || size > 2 * kMaxMotionPacketBytes + 64)
        throw std::runtime_error("Invalid online bridge response size");
    std::vector<uint8_t> bytes(size);
    Transfer(bytes.data(), bytes.size(), false);
    if (bytes[0] == 'E') throw std::runtime_error("Online session: " + std::string(bytes.begin() + 1, bytes.end()));
    return bytes;
}
uint32_t Number(const char* text, uint32_t max) {
    if (!text || !*text) throw std::invalid_argument("Missing online bridge setting");
    char* end = nullptr;
    const auto value = std::strtoul(text, &end, 10);
    if (*end || value > max) throw std::invalid_argument("Invalid online bridge setting");
    return static_cast<uint32_t>(value);
}
#endif
}
bool Active() noexcept { return active; }
uint8_t LocalChannel() noexcept { return channel; }
void InitializeFromEnvironment() {
#ifdef RIISORTED_SIMULATION_ID
    // This marker is checked by the launcher; it identifies simulation sources,
    // while the installation manifest verifies each platform's own binaries.
    std::fprintf(stderr, "RIISORTED_SIMULATION_ID=" RIISORTED_SIMULATION_ID "\n");
#endif
    const char* portText = std::getenv("RESORT_NETPLAY_BRIDGE_PORT");
    if (!portText) return;
#if defined(__linux__) || defined(_WIN32)
    if (std::getenv("RESORT_INPUT_RECORD") || std::getenv("RESORT_INPUT_REPLAY"))
        throw std::invalid_argument("Online play cannot run together with input tape mode");
    const uint32_t port = Number(portText, 65535);
    if (!port) throw std::invalid_argument("Invalid online bridge port");
    channel = static_cast<uint8_t>(Number(std::getenv("RESORT_NETPLAY_PLAYER"), 1));
    inputDelay = Number(std::getenv("RESORT_NETPLAY_INPUT_DELAY"), 30);
    if (inputDelay < 2) throw std::invalid_argument("Invalid online input delay");
    const char* secret = std::getenv("RESORT_NETPLAY_BRIDGE_SECRET");
    if (!secret || std::strlen(secret) != 64) throw std::invalid_argument("Invalid online bridge secret");
    GuestClock::Pause pause;
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("Cannot initialize online sockets");
    connection.initialized = true;
#endif
    connection.fd = socket(AF_INET, SOCK_STREAM, 0);
#if defined(_WIN32)
    if (connection.fd == INVALID_SOCKET)
#else
    if (connection.fd < 0)
#endif
        throw std::runtime_error("Cannot create online bridge socket");
#if defined(_WIN32)
    const DWORD timeout = 20000;
#else
    const timeval timeout{20, 0};
#endif
    const int enabled = 1;
    setsockopt(connection.fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(connection.fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(connection.fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(connection.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)))
        throw std::runtime_error("Cannot connect to the launcher online bridge");
    Send(std::vector<uint8_t>(secret, secret + 64));
    if (Receive() != std::vector<uint8_t>{'R'}) throw std::runtime_error("Invalid online bridge handshake");
    active = true;
    std::fprintf(stderr, "RIISORTED_NETPLAY_V1: connected as Wii Remote %u; experimental replicated simulation\n", channel + 1);
#else
    throw std::runtime_error("Online sockets are unavailable on this platform");
#endif
}
std::vector<uint8_t> ShareMotionPlusReport(const std::vector<uint8_t>& report) {
#if defined(__linux__) || defined(_WIN32)
    if(!active)throw std::logic_error("MotionPlus report outside online play");
    GuestClock::Pause pause(std::chrono::milliseconds(2));
    if(channel==0) {
        if(report.empty() || report[0]!='D')throw std::logic_error("Missing host solver report");
        Send(report);
        if(Receive()!=std::vector<uint8_t>{'A'})throw std::runtime_error("Invalid solver report acknowledgement");
        return report;
    }
    if(!report.empty())throw std::logic_error("Guest cannot publish solver reports");
    Send({'D'});
    return Receive();
#else
    throw std::runtime_error("Online runtime unavailable");
#endif
}
AgreedInputs Exchange(const MotionBatch& local, const std::array<uint8_t, 2>& modes, uint64_t hash) {
#if defined(__linux__) || defined(_WIN32)
    if (!active || local.channel != channel) throw std::logic_error("Invalid local online input assignment");
    auto encoded = EncodeMotionBatch(local);
    std::vector<uint8_t> request{'I', modes[0], modes[1]};
    for (int shift = 56; shift >= 0; shift -= 8) request.push_back(uint8_t(hash >> shift));
    request.insert(request.end(), encoded.begin(), encoded.end());
    // Ordinary loopback/EOS enqueue work must fit inside the VI period, not
    // stretch it. Exclude only the excess beyond this bounded 2 ms budget.
    GuestClock::Pause pause(std::chrono::milliseconds(2));
    captures.push_back(local);
    Send(request);
    const auto reply = Receive();
    if (local.sequence < inputDelay) {
        if (reply != std::vector<uint8_t>{'W'}) throw std::runtime_error("Invalid online pipeline startup");
        AgreedInputs neutral{};
        neutral.interval = local.interval;
        for (uint8_t player = 0; player < 2; ++player) {
            auto& batch = neutral.players[player];
            batch.sequence = local.sequence;
            batch.interval = local.interval;
            batch.channel = player;
            batch.motionPlusMode = modes[player];
            // The only fabricated inputs are the agreed initial neutral window.
            batch.samples.emplace_back();
        }
        return neutral;
    }
    if (reply[0] != 'P' || reply.size() < 5) throw std::runtime_error("Invalid agreed input response");
    const size_t first = (size_t(reply[1]) << 24) | (size_t(reply[2]) << 16) |
                         (size_t(reply[3]) << 8) | reply[4];
    if (first > kMaxMotionPacketBytes || first > reply.size() - 5)
        throw std::runtime_error("Invalid agreed input lengths");
    const auto expected = std::move(captures.front());
    captures.pop_front();
    LockstepQueue queue(expected.interval, 1);
    queue.Submit(DecodeMotionBatch(std::vector<uint8_t>(reply.begin() + 5, reply.begin() + 5 + first)));
    queue.Submit(DecodeMotionBatch(std::vector<uint8_t>(reply.begin() + 5 + first, reply.end())));
    auto result = queue.Take();
    if (!result || EncodeMotionBatch(result->players[channel]) != EncodeMotionBatch(expected) ||
        result->players[0].sequence != local.sequence - inputDelay ||
        result->players[1].sequence != local.sequence - inputDelay)
        throw std::runtime_error("Online input assignment disagrees with captured local input");
    // Raw sensor samples are delayed, but the game solver runs in the current
    // frame's requested mode. Both peers make this same deterministic binding.
    result->interval = local.interval;
    for (uint8_t player = 0; player < 2; ++player) {
        result->players[player].sequence = local.sequence;
        result->players[player].interval = local.interval;
        result->players[player].motionPlusMode = modes[player];
    }
    return std::move(*result);
#else
    throw std::runtime_error("Online runtime is unavailable on this platform");
#endif
}
}

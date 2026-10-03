#include "netplay/live.h"
#include "guest_clock.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(__linux__)
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Riisorted::Netplay::Live {
namespace {
bool active = false;
uint8_t channel = 0;
#if defined(__linux__)
struct Connection {
    int fd = -1;
    ~Connection() { if (fd >= 0) close(fd); }
} connection;
void Transfer(void* data, size_t size, bool write) {
    auto* bytes = static_cast<uint8_t*>(data);
    while (size) {
        const auto count = write ? send(connection.fd, bytes, size, MSG_NOSIGNAL)
                                 : recv(connection.fd, bytes, size, 0);
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
    const char* portText = std::getenv("RESORT_NETPLAY_BRIDGE_PORT");
    if (!portText) return;
#if defined(__linux__)
    if (std::getenv("RESORT_INPUT_RECORD") || std::getenv("RESORT_INPUT_REPLAY"))
        throw std::invalid_argument("Online play cannot run together with input tape mode");
    const uint32_t port = Number(portText, 65535);
    if (!port) throw std::invalid_argument("Invalid online bridge port");
    channel = static_cast<uint8_t>(Number(std::getenv("RESORT_NETPLAY_PLAYER"), 1));
    const char* secret = std::getenv("RESORT_NETPLAY_BRIDGE_SECRET");
    if (!secret || std::strlen(secret) != 64) throw std::invalid_argument("Invalid online bridge secret");
    GuestClock::Pause pause;
    connection.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connection.fd < 0) throw std::runtime_error("Cannot create online bridge socket");
    const timeval timeout{20, 0};
    const int enabled = 1;
    setsockopt(connection.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(connection.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(connection.fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
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
    throw std::runtime_error("This first online runtime supports Linux only");
#endif
}
AgreedInputs Exchange(const MotionBatch& local, const std::array<uint8_t, 2>& modes, uint64_t hash) {
#if defined(__linux__)
    if (!active || local.channel != channel) throw std::logic_error("Invalid local online input assignment");
    auto encoded = EncodeMotionBatch(local);
    std::vector<uint8_t> request{'I', modes[0], modes[1]};
    for (int shift = 56; shift >= 0; shift -= 8) request.push_back(uint8_t(hash >> shift));
    request.insert(request.end(), encoded.begin(), encoded.end());
    GuestClock::Pause pause;
    Send(request);
    const auto reply = Receive();
    if (reply[0] != 'P' || reply.size() < 5) throw std::runtime_error("Invalid agreed input response");
    const size_t first = (size_t(reply[1]) << 24) | (size_t(reply[2]) << 16) |
                         (size_t(reply[3]) << 8) | reply[4];
    if (first > kMaxMotionPacketBytes || first > reply.size() - 5)
        throw std::runtime_error("Invalid agreed input lengths");
    LockstepQueue queue(local.interval, 1);
    queue.Submit(DecodeMotionBatch(std::vector<uint8_t>(reply.begin() + 5, reply.begin() + 5 + first)));
    queue.Submit(DecodeMotionBatch(std::vector<uint8_t>(reply.begin() + 5 + first, reply.end())));
    auto result = queue.Take();
    if (!result || EncodeMotionBatch(result->players[channel]) != encoded)
        throw std::runtime_error("Online input assignment disagrees with local input");
    return std::move(*result);
#else
    throw std::runtime_error("Online runtime is unavailable on this platform");
#endif
}
}

#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace netplay {
// Exact N64 values on the wire. No host struct serialization or float rounding.
struct Pad {
    uint16_t buttons{};
    int8_t x{}, y{};
    bool operator==(const Pad&) const = default;
};
using Pads = std::array<Pad, 4>;
enum class Transport { Steam, Direct };
struct Options {
    std::string address = "127.0.0.1"; // public IP/DNS to advertise in the code
    uint16_t port = 7777; // host forwards this TCP port to their computer
    std::string code; // empty creates a room
    bool detect_public_ip{true}; // direct transport only
    Transport transport{Transport::Steam};
};
// Validate fields without opening a socket or ending an offline game.
void validate_options(const Options&);
struct Status {
    std::string code, message;
    int slot{-1}, players{};
    bool connected{}, running{}, failed{};
    std::array<bool,4> ready{};
    std::array<std::string,4> routes{};
};
class Session {
public:
    explicit Session(const Options&);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Status status() const;
    void start(const std::vector<uint8_t>& save); // host only; requires ready peers
    std::vector<uint8_t> initial_save() const;
    uint32_t seed() const;
    uint8_t connected_mask() const;
    // Called once per simulation frame. Throws on timeout, disconnect or desync.
    Pads exchange(Pad local, uint64_t checksum);
    void close();
private:
    struct State;
    std::shared_ptr<State> state;
};
const char* build_id();
// SDL launcher. Returns null when cancelled; --netplay skips offline play.
std::unique_ptr<Session> lobby(const Options&, const std::vector<uint8_t>& host_save, bool connect_immediately = false);
}

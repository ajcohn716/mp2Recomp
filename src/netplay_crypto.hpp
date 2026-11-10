#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace netplay::crypto {
using Key=std::array<unsigned char,32>;
Key random_key();
std::string encode(std::span<const unsigned char>);
std::vector<unsigned char> decode(const std::string&);
struct Invite {
    std::string address;
    uint16_t port{};
    Key key{};
};
std::string invitation(const Invite&);
Invite parse_invitation(const std::string&);
// Per-connection directional keys derived with HKDF-SHA256; monotonically
// numbered AES-256-GCM records reject modifications and replays.
class Channel {
    Key sending{}, receiving{};
    uint64_t send_sequence{}, receive_sequence{};
    bool initialized{};
public:
    Channel()=default;
    Channel(const Channel&)=delete;
    Channel& operator=(const Channel&)=delete;
    ~Channel();
    void initialize(const Key& secret,const Key& client_nonce,const Key& host_nonce,bool host);
    std::vector<unsigned char> seal(const std::string&);
    std::string open(std::span<const unsigned char>);
};
}

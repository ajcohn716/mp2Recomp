#pragma once
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace netplay {
struct SteamFriend { std::string id, name; };
struct SteamInvitation { std::string id, name; };
struct SteamSocialStatus {
    bool available{}, invite_ready{};
    std::vector<SteamFriend> friends;
    std::vector<SteamInvitation> invitations;
    std::string message, join_code;
    uint64_t message_revision{};
};
// Shares one standalone helper with the Steam transport. Startup runs away
// from the UI thread; status and commands never wait for Steam initialization.
class SteamSocial {
public:
    SteamSocial();
    ~SteamSocial();
    SteamSocialStatus status();
    void refresh();
    void invite(const std::string& friend_id);
    void accept(const std::string& invitation_id);
    void dismiss(const std::string& invitation_id);
private:
    struct State;
    std::unique_ptr<State> state;
};
}

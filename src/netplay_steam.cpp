// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Cohn
#include "netplay_transport.hpp"
#include <stdexcept>

#ifndef MP2_HAS_STEAMWORKS
namespace netplay::transport {
std::unique_ptr<Transport> steam() {
    throw std::runtime_error("Steam netplay is not built. Configure MP2_STEAMWORKS_SDK with your Steamworks SDK, or select Direct TCP.");
}
}
#else
#include <steam/steam_api.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>

namespace netplay::transport {
namespace {
std::mutex steam_mutex;
struct Steam;
struct SteamSocket final : Socket, std::enable_shared_from_this<SteamSocket> {
    Steam& owner;
    HSteamNetConnection handle{};
    bool connected{}, pending_close{};
    std::deque<Message> received;
    explicit SteamSocket(Steam& transport) : owner(transport) {}
    ~SteamSocket() override;
    void open(const std::string&,uint16_t) override;
    bool is_open() const override {return connected;}
    size_t buffered() const override;
    void send(const std::string&) override;
    void send(const Binary&) override;
    void send_record(uint8_t kind,const void*,size_t);
    void close() override;
};
struct Steam final : Transport {
    // Steam API state is process-global. Reject overlapping Steam rooms rather
    // than letting either worker shut down another room's API or callbacks.
    std::unique_lock<std::mutex> lease{steam_mutex,std::try_to_lock};
    bool initialized{};
    ISteamNetworkingSockets* api{};
    HSteamListenSocket listener{};
    HSteamNetPollGroup group{};
    std::function<void(std::shared_ptr<Socket>)> accept;
    std::map<HSteamNetConnection,std::shared_ptr<SteamSocket>> peers;
    static thread_local Steam* current;
    CSteamID invitation_lobby;
    std::string connect_data;
    bool creating_lobby{};
    CCallResult<Steam,LobbyCreated_t> lobby_created;
    CCallResult<Steam,LobbyEnter_t> lobby_joined;
    CCallback<Steam,LobbyInvite_t> lobby_invite{this,&Steam::invited};
    CCallback<Steam,GameLobbyJoinRequested_t> lobby_requested{this,&Steam::join_requested};
    void emit(char kind,std::string data={}) {if(social_event)social_event(kind,std::move(data));}
    static std::string persona(CSteamID id) {
        std::string name=SteamFriends()->GetFriendPersonaName(id);
        for(auto& c:name)if(static_cast<unsigned char>(c)<32)c=' ';
        return name.substr(0,128);
    }
    bool eligible(CSteamID id) {
        FriendGameInfo_t game{};
        return SteamFriends()->GetFriendRelationship(id)==k_EFriendRelationshipFriend &&
            SteamFriends()->GetFriendGamePlayed(id,&game) && game.m_gameID==CGameID(steam_app_id);
    }
    void invited(LobbyInvite_t* event) {
        if(CGameID(event->m_ulGameID)!=CGameID(steam_app_id)||
           SteamFriends()->GetFriendRelationship(CSteamID(event->m_ulSteamIDUser))!=k_EFriendRelationshipFriend)return;
        emit('v',std::to_string(event->m_ulSteamIDLobby)+"\n"+persona(CSteamID(event->m_ulSteamIDUser)));
    }
    void join_requested(GameLobbyJoinRequested_t* event) {
        // Steam's Join button also opens the in-game confirmation.
        if(SteamFriends()->GetFriendRelationship(event->m_steamIDFriend)==k_EFriendRelationshipFriend)
            emit('v',std::to_string(event->m_steamIDLobby.ConvertToUint64())+"\n"+persona(event->m_steamIDFriend));
    }
    void created(LobbyCreated_t* event,bool failed) {
        creating_lobby=false;
        if(failed||event->m_eResult!=k_EResultOK){emit('s',"Steam could not create the invite lobby. Try again.");return;}
        invitation_lobby=CSteamID(event->m_ulSteamIDLobby);
        if(connect_data.empty()){clear_invite_lobby();return;}
        if(!SteamMatchmaking()->SetLobbyData(invitation_lobby,"connect",connect_data.c_str())) {
            clear_invite_lobby();emit('s',"Steam could not publish the invite. Try again.");return;
        }
        emit('p',"1");
    }
    void joined(LobbyEnter_t* event,bool failed) {
        CSteamID lobby(event->m_ulSteamIDLobby);
        if(failed||event->m_EChatRoomEnterResponse!=k_EChatRoomEnterResponseSuccess) {
            emit('s',"Invite expired or room unavailable.");return;
        }
        std::string code=SteamMatchmaking()->GetLobbyData(lobby,"connect");
        if(lobby!=invitation_lobby)SteamMatchmaking()->LeaveLobby(lobby);
        if(code.empty()||code.size()>512)emit('s',"Invite expired or room unavailable.");
        else emit('r',std::move(code));
    }
    void clear_invite_lobby() {
        if(invitation_lobby.IsValid()) {
            SteamMatchmaking()->SetLobbyJoinable(invitation_lobby,false);
            SteamMatchmaking()->DeleteLobbyData(invitation_lobby,"connect");
            SteamMatchmaking()->LeaveLobby(invitation_lobby);invitation_lobby.Clear();
        }
        emit('p');
    }
    void social(char kind,const std::string& data) override {
        if(kind=='f') {
            emit('f');
            int count=SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
            for(int i=0;i<count;++i) {
                auto id=SteamFriends()->GetFriendByIndex(i,k_EFriendFlagImmediate);
                if(eligible(id))emit('g',std::to_string(id.ConvertToUint64())+"\n"+persona(id));
            }
        } else if(kind=='p') {
            if(data.size()>512)throw std::runtime_error("Invite connect data too large");
            connect_data=data;clear_invite_lobby();
            if(!data.empty()&&!creating_lobby) {
                creating_lobby=true;
                lobby_created.Set(SteamMatchmaking()->CreateLobby(k_ELobbyTypePrivate,4),this,&Steam::created);
            }
        } else if(kind=='i') {
            CSteamID id(std::stoull(data));
            if(!invitation_lobby.IsValid()||connect_data.empty())emit('s',"Invite lobby is not ready. Try again.");
            else if(!eligible(id))emit('s',"That friend is no longer playing Spacewar. Refresh the list.");
            else emit('s',SteamMatchmaking()->InviteUserToLobby(invitation_lobby,id)?"Steam invite sent":"Steam could not send the invite. Try again.");
        } else if(kind=='j') {
            CSteamID id(std::stoull(data));
            if(!id.IsLobby()){emit('s',"Invalid Steam lobby invitation");return;}
            lobby_joined.Set(SteamMatchmaking()->JoinLobby(id),this,&Steam::joined);
        } else throw std::runtime_error("Unknown Steam social command");
    }
    Steam() {
        if(!lease.owns_lock())throw std::runtime_error("Leave the current Steam room before opening another.");
#ifdef _WIN32
        _putenv_s("SteamAppId","480");_putenv_s("SteamGameId","480");
#else
        setenv("SteamAppId","480",1);setenv("SteamGameId","480",1);
#endif
        if(!SteamAPI_Init())throw std::runtime_error("Steam initialization failed (App ID 480). Start Steam and sign in under the same OS user.");
        initialized=true;
        if(!SteamUser()||!SteamUser()->BLoggedOn()||!SteamUtils()||SteamUtils()->GetAppID()!=steam_app_id||
           !(api=SteamNetworkingSockets())||!SteamNetworkingUtils()||!SteamFriends()||!SteamMatchmaking()) {
            SteamAPI_Shutdown();initialized=false;
            throw std::runtime_error("Steam must be online with networking available under App ID 480.");
        }
        group=api->CreatePollGroup();
        if(!group){SteamAPI_Shutdown();initialized=false;throw std::runtime_error("Could not create Steam receive group");}
        SteamNetworkingUtils()->InitRelayNetworkAccess();
    }
    ~Steam() override {
        lobby_created.Cancel();lobby_joined.Cancel();connect_data.clear();clear_invite_lobby();
        social_event={};accept={};
        for(auto& [id,socket]:peers){socket->reset_callbacks();socket->close();}
        peers.clear();
        stop_listening();
        if(group)api->DestroyPollGroup(group);
        if(initialized){SteamAPI_ReleaseCurrentThreadMemory();SteamAPI_Shutdown();}
    }
    static void status_changed(SteamNetConnectionStatusChangedCallback_t* event) {
        if(current)current->status(*event);
    }
    std::array<SteamNetworkingConfigValue_t,7> connection_options() {
        std::array<SteamNetworkingConfigValue_t,7> options;
        options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,reinterpret_cast<void*>(&status_changed));
        options[1].SetInt32(k_ESteamNetworkingConfig_SendBufferSize,1024*1024);
        options[2].SetInt32(k_ESteamNetworkingConfig_RecvBufferSize,1024*1024);
        options[3].SetInt32(k_ESteamNetworkingConfig_RecvBufferMessages,256);
        options[4].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize,16409);
        options[5].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial,30000);
        options[6].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected,15000);
        return options;
    }
    uint16_t listen(const std::string&,uint16_t,std::function<void(std::shared_ptr<Socket>)> callback) override {
        accept=std::move(callback);
        auto options=connection_options();
        listener=api->CreateListenSocketP2P(0,int(options.size()),options.data());
        if(!listener)throw std::runtime_error("Could not create Steam P2P room");
        return 1; // invitation format requires nonzero port; Steam uses virtual port 0
    }
    void stop_listening() override {
        accept={};if(listener)api->CloseListenSocket(listener);listener=0;
        connect_data.clear();clear_invite_lobby();
    }
    std::string host_address(const std::string&) const override {
        return std::to_string(SteamUser()->GetSteamID().ConvertToUint64());
    }
    std::shared_ptr<Socket> socket() override {return std::make_shared<SteamSocket>(*this);}
    void status(const SteamNetConnectionStatusChangedCallback_t& event) {
        auto found=peers.find(event.m_hConn);
        if(event.m_info.m_eState==k_ESteamNetworkingConnectionState_Connecting&&found==peers.end()) {
            if(!listener||event.m_info.m_hListenSocket!=listener||!accept||peers.size()>=16||
               !event.m_info.m_identityRemote.GetSteamID().IsValid()) {
                api->CloseConnection(event.m_hConn,0,"Room unavailable",false);return;
            }
            auto socket=std::make_shared<SteamSocket>(*this);socket->handle=event.m_hConn;
            peers.emplace(event.m_hConn,socket);
            // Queue Session::attach before data is dispatched from the poll group.
            accept(socket);
            if(!api->SetConnectionPollGroup(event.m_hConn,group)||api->AcceptConnection(event.m_hConn)!=k_EResultOK) {
                socket->close();socket->pending_close=true;
            }
            return;
        }
        if(found==peers.end())return;
        auto socket=found->second;
        switch(event.m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connected:
            socket->connected=true;if(socket->on_open)socket->on_open();break;
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            socket->close();socket->pending_close=true;break;
        default:break;
        }
    }
    void poll() override {
        SteamAPI_RunCallbacks();
        current=this;api->RunCallbacks();current=nullptr;
        for(auto& [id,socket]:peers) {
            if(socket->pending_close&&socket->on_close){socket->pending_close=false;socket->on_close();}
            if(socket->on_message)while(!socket->received.empty()) {
                socket->on_message(std::move(socket->received.front()));socket->received.pop_front();
            }
        }
        // Bound each iteration so controller work and shutdown remain responsive.
        for(int i=0;i<64;++i) {
            SteamNetworkingMessage_t* raw{};
            int count=api->ReceiveMessagesOnPollGroup(group,&raw,1);
            if(count<0)throw std::runtime_error("Steam receive failed");
            if(!count)break;
            std::unique_ptr<SteamNetworkingMessage_t,void(*)(SteamNetworkingMessage_t*)> message(raw,[](auto* p){p->Release();});
            auto found=peers.find(raw->m_conn);
            if(found==peers.end())continue;
            auto socket=found->second;
            auto* data=static_cast<const std::byte*>(raw->m_pData);
            if(raw->m_cbSize<1||raw->m_cbSize>16409||(data[0]!=std::byte{0}&&data[0]!=std::byte{1})) {
                socket->close();socket->pending_close=true;continue;
            }
            if(socket->received.size()>=64){socket->close();socket->pending_close=true;continue;}
            Message decoded;
            if(data[0]==std::byte{0})decoded=std::string(reinterpret_cast<const char*>(data+1),raw->m_cbSize-1);
            else decoded=Binary(data+1,data+raw->m_cbSize);
            if(socket->on_message)socket->on_message(std::move(decoded));
            else socket->received.push_back(std::move(decoded));
        }
        // Keep handles alive through queued close notifications; release closed
        // records once the Session has detached all callbacks.
        std::erase_if(peers,[](const auto& entry){return !entry.second->handle&&!entry.second->on_close&&!entry.second->pending_close;});
    }
};
thread_local Steam* Steam::current{};
SteamSocket::~SteamSocket(){close();}
void SteamSocket::open(const std::string& address,uint16_t) {
    SteamNetworkingIdentity remote;remote.SetSteamID64(std::stoull(address));
    if(remote.GetSteamID()==SteamUser()->GetSteamID())throw std::runtime_error("Use a different Steam account and computer to join this room.");
    auto options=owner.connection_options();handle=owner.api->ConnectP2P(remote,0,int(options.size()),options.data());
    if(!handle)throw std::runtime_error("Could not connect to Steam host");
    owner.peers.emplace(handle,std::static_pointer_cast<SteamSocket>(shared_from_this()));
    if(!owner.api->SetConnectionPollGroup(handle,owner.group))throw std::runtime_error("Could not assign Steam receive group");
}
size_t SteamSocket::buffered() const {
    SteamNetConnectionRealTimeStatus_t status{};
    if(!handle||owner.api->GetConnectionRealTimeStatus(handle,&status,0,nullptr)!=k_EResultOK)return 0;
    return size_t(std::max(0,status.m_cbPendingReliable))+size_t(std::max(0,status.m_cbSentUnackedReliable));
}
void SteamSocket::send_record(uint8_t kind,const void* data,size_t size) {
    if(!connected||!handle||size>16408||buffered()>1024*1024)throw std::runtime_error("Steam peer disconnected or send queue full");
    Binary record(size+1);record[0]=std::byte{kind};
    std::copy_n(static_cast<const std::byte*>(data),size,record.data()+1);
    if(owner.api->SendMessageToConnection(handle,record.data(),uint32_t(record.size()),k_nSteamNetworkingSend_ReliableNoNagle,nullptr)!=k_EResultOK)
        throw std::runtime_error("Steam reliable send failed");
}
void SteamSocket::send(const std::string& message){send_record(0,message.data(),message.size());}
void SteamSocket::send(const Binary& message){send_record(1,message.data(),message.size());}
void SteamSocket::close() {
    connected=false;
    if(handle){owner.api->CloseConnection(handle,0,"Room closed",false);handle=0;}
}
}
std::unique_ptr<Transport> steam(){return std::make_unique<Steam>();}
}
#endif

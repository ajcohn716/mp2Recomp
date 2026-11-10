#include "netplay_transport.hpp"
#include "netplay_steam_social.hpp"
#include "steam_helper_protocol.hpp"
#include "steam_helper_process.hpp"
#include <chrono>
#include <map>
#include <future>
#include <algorithm>
#include <stdexcept>

namespace netplay::transport {
namespace {
// All pipe access is serialized, but callbacks are dispatched only by the
// transport worker. UI polls can safely collect social events during play.
struct Service {
    steam_helper::Process process;
    steam_helper::Inbox inbox;
    std::mutex mutex;
    std::string identity, error;
    netplay::SteamSocialStatus view;
    std::deque<steam_helper::Record> network;
    size_t network_bytes{};
    bool attached{};
    uint32_t next_reset{1}, reset_ack{};
    Service() {
        try {
            process.start();inbox.start(process.output);
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(identity.empty()) {
                pump();
                if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Steam helper timed out. Start Steam and sign in.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            view.available=true;
            command('f');
        }catch(...){process.stop();throw;}
    }
    ~Service() {
        try{steam_helper::write(process.input,{'Q',0,{}});}catch(...){}
        process.stop();
    }
    void command(char kind,uint32_t peer=0,std::string data={}) {
        std::lock_guard lock(mutex);
        if(!error.empty())throw std::runtime_error(error);
        steam_helper::write(process.input,{kind,peer,std::move(data)});
    }
    void pump() { // caller holds mutex after construction
        if(!error.empty())throw std::runtime_error(error);
        try {
            for(auto& record:inbox.take()) {
                if(record.kind=='E')throw std::runtime_error(record.data);
                if(record.kind=='H') {
                    if(!identity.empty()||!record.data.starts_with("2:"))throw std::runtime_error("Incompatible Steam helper. Install the matching helper bundle.");
                    identity=record.data.substr(2);
                    if(identity.empty()||identity.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid Steam helper identity");
                } else if(record.kind=='U') {
                    reset_ack=record.peer;network.clear();network_bytes=0;
                } else if(record.kind=='f')view.friends.clear();
                else if(record.kind=='g'||record.kind=='v') {
                    auto split=record.data.find('\n');
                    if(split==std::string::npos)throw std::runtime_error("Invalid Steam social record");
                    auto id=record.data.substr(0,split), name=record.data.substr(split+1);
                    if(id.empty()||id.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid Steam social identity");
                    if(record.kind=='g') {
                        if(view.friends.size()<1024)view.friends.push_back({id,name});
                    } else {
                        std::erase_if(view.invitations,[&](const auto& invite){return invite.id==id;});
                        if(view.invitations.size()>=16)view.invitations.erase(view.invitations.begin());
                        view.invitations.push_back({id,name});
                    }
                } else if(record.kind=='p')view.invite_ready=record.data=="1";
                else if(record.kind=='s')view.message=std::move(record.data);
                else if(record.kind=='r')view.join_code=std::move(record.data);
                else {
                    if(!attached)continue;
                    if(network.size()>=1024||network_bytes+record.data.size()>2*1024*1024)throw std::runtime_error("Steam transport queue full");
                    network_bytes+=record.data.size();network.push_back(std::move(record));
                }
            }
            if(inbox.finished())throw std::runtime_error("Steam helper exited. Start Steam and sign in.");
        }catch(const std::exception& e){error=e.what();view.available=false;throw;}
    }
    std::deque<steam_helper::Record> take_network() {
        std::lock_guard lock(mutex);pump();
        std::deque<steam_helper::Record> result;result.swap(network);network_bytes=0;return result;
    }
};
std::mutex service_mutex;
std::weak_ptr<Service> shared_service;
std::shared_ptr<Service> service() {
    std::lock_guard lock(service_mutex);
    auto result=shared_service.lock();
    if(!result){result=std::make_shared<Service>();shared_service=result;}
    return result;
}
struct SteamProxy;
struct ProxySocket final : Socket {
    SteamProxy& owner;uint32_t id;bool connected{},closed{},pending_close{},released{};
    std::deque<Message> received;
    ProxySocket(SteamProxy& service,uint32_t value):owner(service),id(value){}
    void open(const std::string&,uint16_t) override;
    bool is_open() const override{return connected;}
    // Writes are handed to a bounded helper queue synchronously; the helper
    // enforces the Steam send-buffer limit and reports failures on its stream.
    size_t buffered() const override{return 0;}
    void send(const std::string&) override;
    void send(const Binary&) override;
    void close() override;
};
struct SteamProxy final : Transport {
    std::shared_ptr<Service> helper;
    std::map<uint32_t,std::shared_ptr<ProxySocket>> peers;
    std::function<void(std::shared_ptr<Socket>)> accept;
    uint32_t next_id{1};
    bool listening{},active{true};
    SteamProxy():helper(service()) {
        uint32_t reset;
        {
            std::lock_guard lock(helper->mutex);
            if(helper->attached)throw std::runtime_error("Leave the current Steam room before opening another.");
            helper->attached=true;reset=helper->next_reset++;
        }
        try {
            helper->command('U',reset);
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            for(;;) {
                {std::lock_guard lock(helper->mutex);helper->pump();if(helper->reset_ack==reset)break;}
                if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Steam helper reset timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch(...) {
            std::lock_guard lock(helper->mutex);helper->attached=false;throw;
        }
    }
    ~SteamProxy() override {
        for(auto& [id,socket]:peers){socket->reset_callbacks();socket->close();}
        try{command('U',0);}catch(...){}
        active=false;
        std::lock_guard lock(helper->mutex);
        helper->attached=false;helper->network.clear();helper->network_bytes=0;
    }
    void command(char kind,uint32_t id,std::string data={}) {
        if(!active)throw std::runtime_error("Steam helper stopped");
        helper->command(kind,id,std::move(data));
    }
    void social(char kind,const std::string& data) override {command(kind,0,data);}
    uint16_t listen(const std::string&,uint16_t,std::function<void(std::shared_ptr<Socket>)> callback) override{
        accept=std::move(callback);command('L',0);
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!listening){poll();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Steam listener timed out");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        return 1;
    }
    std::string host_address(const std::string&) const override{return helper->identity;}
    std::shared_ptr<Socket> socket() override{
        if(peers.size()>=16)throw std::runtime_error("Too many Steam connections");
        auto result=std::make_shared<ProxySocket>(*this,next_id++);peers.emplace(result->id,result);return result;
    }
    void poll() override{
        if(!active)return;
        auto records=helper->take_network();
        for(auto& record:records){
            if(record.kind=='L'){listening=true;continue;}
            if(record.kind=='A'){
                if(!accept||record.peer<0x80000000||peers.size()>=16||peers.contains(record.peer))throw std::runtime_error("Unexpected Steam peer");
                auto socket=std::make_shared<ProxySocket>(*this,record.peer);peers.emplace(record.peer,socket);accept(socket);continue;
            }
            auto found=peers.find(record.peer);if(found==peers.end())continue;
            auto socket=found->second;if(socket->closed)continue;
            if(record.kind=='O'){socket->connected=true;if(socket->on_open)socket->on_open();}
            else if(record.kind=='X'){socket->connected=false;socket->closed=true;socket->pending_close=true;}
            else if(record.kind=='T'||record.kind=='B'){
                if(socket->received.size()>=64)throw std::runtime_error("Steam peer receive queue full");
                if(record.kind=='T')socket->received.emplace_back(std::move(record.data));
                else socket->received.emplace_back(Binary(reinterpret_cast<const std::byte*>(record.data.data()),reinterpret_cast<const std::byte*>(record.data.data()+record.data.size())));
            }else throw std::runtime_error("Unknown Steam helper event");
        }
        for(auto& [id,socket]:peers){
            while(socket->on_message&&!socket->received.empty()){
                auto message=std::move(socket->received.front());socket->received.pop_front();socket->on_message(std::move(message));
            }
            if(socket->pending_close&&socket->on_close){socket->pending_close=false;socket->on_close();}
        }
        std::erase_if(peers,[](const auto& entry){return entry.second->closed&&!entry.second->on_close&&!entry.second->pending_close;});

    }
};
void ProxySocket::open(const std::string& address,uint16_t){owner.command('C',id,address);}
void ProxySocket::send(const std::string& text){if(!connected)throw std::runtime_error("Steam peer disconnected");owner.command('T',id,text);}
void ProxySocket::send(const Binary& bytes){if(!connected)throw std::runtime_error("Steam peer disconnected");owner.command('B',id,std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}
void ProxySocket::close(){if(released)return;released=true;connected=false;closed=true;pending_close=false;try{owner.command('X',id);}catch(...){} }
}
std::unique_ptr<Transport> steam(){return std::make_unique<SteamProxy>();}
}

namespace netplay {
struct SteamSocial::State {
    std::future<std::shared_ptr<transport::Service>> pending=std::async(std::launch::async,transport::service);
    std::shared_ptr<transport::Service> helper;
    std::string error;
    bool ready() {
        if(!helper&&pending.valid()&&pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try{helper=pending.get();}catch(const std::exception& e){error=e.what();}
        }
        return bool(helper);
    }
    void command(char kind,const std::string& data={}) {
        if(!ready())throw std::runtime_error(error.empty()?"Connecting to Steam...":error);
        helper->command(kind,0,data);
    }
};
SteamSocial::SteamSocial():state(std::make_unique<State>()){}
SteamSocial::~SteamSocial()=default;
SteamSocialStatus SteamSocial::status() {
    if(!state->ready())return {.message=state->error.empty()?"Connecting to Steam...":state->error};
    std::lock_guard lock(state->helper->mutex);
    try{state->helper->pump();}catch(const std::exception& e){state->helper->view.message=e.what();}
    auto result=state->helper->view;
    state->helper->view.join_code.clear();return result;
}
void SteamSocial::refresh(){state->command('f');}
void SteamSocial::invite(const std::string& id){state->command('i',id);}
void SteamSocial::accept(const std::string& id){state->command('j',id);}
void SteamSocial::dismiss(const std::string& id) {
    if(!state->ready())return;
    std::lock_guard lock(state->helper->mutex);
    std::erase_if(state->helper->view.invitations,[&](const auto& invite){return invite.id==id;});
}
}

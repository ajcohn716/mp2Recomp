#include "netplay_transport.hpp"
#include "steam_helper_protocol.hpp"
#include "steam_helper_process.hpp"
#include <chrono>
#include <map>
#include <stdexcept>

namespace netplay::transport {
namespace {
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
    // Process precedes Inbox so its output FILE remains valid through join.
    steam_helper::Process process;
    steam_helper::Inbox inbox;
    std::map<uint32_t,std::shared_ptr<ProxySocket>> peers;
    std::function<void(std::shared_ptr<Socket>)> accept;
    uint32_t next_id{1};std::string identity;
    bool listening{},active{true};
    SteamProxy(){
        try{
            process.start();inbox.start(process.output);
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(identity.empty()){
                poll();
                if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Steam helper timed out. Start Steam and sign in, or select Direct TCP.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }catch(...){process.stop();throw;}
    }
    ~SteamProxy() override {
        active=false;
        for(auto& [id,socket]:peers)socket->reset_callbacks();
        try{steam_helper::write(process.input,{'Q',0,{}});}catch(...){}
        process.stop();
    }
    void command(char kind,uint32_t id,std::string data={}){
        if(!active)throw std::runtime_error("Steam helper stopped");
        steam_helper::write(process.input,{kind,id,std::move(data)});
    }
    uint16_t listen(const std::string&,uint16_t,std::function<void(std::shared_ptr<Socket>)> callback) override{
        accept=std::move(callback);command('L',0);
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!listening){poll();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Steam listener timed out");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        return 1;
    }
    std::string host_address(const std::string&) const override{return identity;}
    std::shared_ptr<Socket> socket() override{
        if(peers.size()>=16)throw std::runtime_error("Too many Steam connections");
        auto result=std::make_shared<ProxySocket>(*this,next_id++);peers.emplace(result->id,result);return result;
    }
    void poll() override{
        if(!active)return;
        auto records=inbox.take();
        for(auto& record:records){
            if(record.kind=='E')throw std::runtime_error(record.data);
            if(record.kind=='H'){
                if(!identity.empty()||!record.data.starts_with("1:"))throw std::runtime_error("Incompatible Steam helper");
                identity=record.data.substr(2);
                if(identity.empty()||identity.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid Steam helper identity");
                continue;
            }
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
        if(inbox.finished())throw std::runtime_error("Steam helper exited. Start Steam and sign in, or select Direct TCP.");
    }
};
void ProxySocket::open(const std::string& address,uint16_t){owner.command('C',id,address);}
void ProxySocket::send(const std::string& text){if(!connected)throw std::runtime_error("Steam peer disconnected");owner.command('T',id,text);}
void ProxySocket::send(const Binary& bytes){if(!connected)throw std::runtime_error("Steam peer disconnected");owner.command('B',id,std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}
void ProxySocket::close(){if(released)return;released=true;connected=false;closed=true;pending_close=false;try{owner.command('X',id);}catch(...){} }
}
std::unique_ptr<Transport> steam(){return std::make_unique<SteamProxy>();}
}

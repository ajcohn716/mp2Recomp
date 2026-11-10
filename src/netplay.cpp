#include "netplay.hpp"
#include "netplay_crypto.hpp"
#include "netplay_build.hpp"
#include "netplay_transport.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace netplay {
using nlohmann::json;
using namespace std::chrono_literals;
namespace {
constexpr unsigned delay=3, window=256;
int integer(const json& j, int lo, int hi) {
    if (!j.is_number_integer()) throw std::runtime_error("Invalid integer in peer message");
    auto n = j.get<int64_t>();
    if (n < lo || n > hi) throw std::runtime_error("Peer value outside protocol limits");
    return int(n);
}
uint64_t frame_number(const json& j) {
    if (!j.is_number_unsigned() && !j.is_number_integer()) throw std::runtime_error("Invalid input frame");
    if (j.is_number_integer() && j.get<int64_t>() < 0) throw std::runtime_error("Negative input frame");
    auto n = j.get<uint64_t>();
    if (n > (uint64_t(1) << 40)) throw std::runtime_error("Invalid input frame");
    return n;
}
Pad decode_pad(const json& j) {
    if (!j.is_array() || j.size() != 3) throw std::runtime_error("Invalid controller input");
    return {uint16_t(integer(j[0],0,65535)), int8_t(integer(j[1],-128,127)), int8_t(integer(j[2],-128,127))};
}
json encode_pad(Pad p) { return json::array({p.buttons, int(p.x), int(p.y)}); }
json encode_pads(const Pads& pads) {
    json out = json::array();
    for (auto p : pads) out.push_back(encode_pad(p));
    return out;
}
std::vector<uint8_t> decode_save(const json& j) {
    if (!j.is_array() || (j.size() != 0 && j.size() != 512)) throw std::runtime_error("Invalid initial EEPROM");
    std::vector<uint8_t> out;
    for (auto& b : j) out.push_back(uint8_t(integer(b,0,255)));
    return out;
}
}
const char* build_id(){return MP2_NETPLAY_BUILD_ID;}

struct Session::State : std::enable_shared_from_this<Session::State> {
    Options options;
    mutable std::mutex mutex;
    std::condition_variable changed;
    Status view;
    std::vector<uint8_t> save;
    uint32_t rng_seed{};
    uint8_t mask{};
    uint64_t consumed{};
    std::map<uint64_t,Pads> frames;
    bool stopping{};
    std::deque<std::function<void()>> tasks;
    std::thread worker;
    // Only the worker touches transports, membership and pending input records.
    std::unique_ptr<transport::Transport> wire;
    crypto::Invite invite;
    struct Connection {
        std::shared_ptr<transport::Socket> socket;
        crypto::Channel cipher;
        crypto::Key nonce{};
        bool encrypted{}, joined{};
        int slot{-1};
        std::chrono::steady_clock::time_point created=std::chrono::steady_clock::now();
    };
    std::map<unsigned,std::unique_ptr<Connection>> connections;
    std::array<unsigned,4> peer_ids{};
    std::array<bool,4> roster{};
    unsigned next_connection{1};
    std::map<uint64_t,std::array<std::optional<Pad>,4>> pending;
    std::map<uint64_t,std::array<std::optional<uint64_t>,4>> checks;
    uint64_t commit_next{delay};
    bool running{}, hosting{};
    int slot{-1};
    std::chrono::steady_clock::time_point connect_started=std::chrono::steady_clock::now();

    void post(std::function<void()> task){
        std::lock_guard lock(mutex);
        if(!stopping && tasks.size()<1024)tasks.push_back(std::move(task));
        changed.notify_all();
    }
    void fail(std::string error){
        std::lock_guard lock(mutex);
        if(stopping||view.failed)return;
        view.failed=true;view.message=std::move(error);
        fprintf(stderr,"[NETPLAY] %s\n",view.message.c_str());changed.notify_all();
    }
    template<typename F> auto callback(F action){
        return [weak=weak_from_this(),action](auto... args){
            if(auto s=weak.lock())s->post([s,action,args...]() mutable {action(*s,args...);});
        };
    }
    void send_connection(unsigned id,const json& message){
        auto it=connections.find(id);
        if(it==connections.end()||!it->second->socket->is_open()||!it->second->encrypted)throw std::runtime_error("A player disconnected");
        auto& c=*it->second;
        if(c.socket->buffered()>1024*1024)throw std::runtime_error("A player connection is not keeping up");
        auto sealed=c.cipher.seal(message.dump());
        c.socket->send(transport::Binary(reinterpret_cast<const std::byte*>(sealed.data()),reinterpret_cast<const std::byte*>(sealed.data()+sealed.size())));
    }
    void send(int target,const json& message){send_connection(peer_ids[target],message);}
    void broadcast(const json& message){for(int i=1;i<4;++i)if(roster[i])send(i,message);}
    void publish_roster(){
        json members=json::array();for(int i=0;i<4;++i)if(roster[i])members.push_back(i);
        broadcast({{"type","roster"},{"slots",members}});
        std::lock_guard lock(mutex);view.players=int(members.size());
        for(int i=0;i<4;++i){view.ready[i]=roster[i];view.routes[i]=roster[i]&&i!=slot?(options.transport==Transport::Steam?"Steam P2P":"Direct"):"";}
        changed.notify_all();
    }
    void disconnect(unsigned id){
        auto it=connections.find(id);if(it==connections.end())return;
        int remote=it->second->slot;
        it->second->socket->reset_callbacks();it->second->socket->close();connections.erase(it);
        if(!hosting){fail("Host disconnected. Create or join a new room.");return;}
        if(remote<0)return;
        if(running){
            roster[remote]=false;
            try{broadcast({{"type","error"},{"message","A player disconnected. Session stopped."}});}catch(...){}
            fail("A player disconnected. Session stopped.");
        }else{roster[remote]=false;peer_ids[remote]=0;publish_roster();}
    }
    void attach(std::shared_ptr<transport::Socket> socket){
        if(connections.size()>=16 || (hosting&&running)){socket->close();return;}
        unsigned id=next_connection++;
        auto c=std::make_unique<Connection>();c->socket=socket;
        connections.emplace(id,std::move(c));
        socket->on_message=callback([id](State& s,transport::Message raw){
            try{s.receive(id,raw);}catch(const std::exception&){s.disconnect(id);}
        });
        socket->on_close=callback([id](State& s){s.disconnect(id);});
        if(!hosting){
            peer_ids[0]=id;
            socket->on_open=callback([id](State& s){
                auto& c=*s.connections.at(id);c.nonce=crypto::random_key();
                c.socket->send(json({{"type","hello"},{"nonce",crypto::encode(c.nonce)}}).dump());
            });
            socket->open(invite.address,invite.port);
        }
    }
    void receive(unsigned id,const transport::Message& raw){
        auto it=connections.find(id);if(it==connections.end())return;
        auto& c=*it->second;
        if(!c.encrypted){
            if(!std::holds_alternative<std::string>(raw))throw std::runtime_error("Invalid handshake");
            const auto& text=std::get<std::string>(raw);if(text.size()>256)throw std::runtime_error("Invalid handshake");
            auto hello=json::parse(text);
            if(hello.at("type")!="hello")throw std::runtime_error("Invalid handshake");
            auto bytes=crypto::decode(hello.at("nonce").get<std::string>());
            if(bytes.size()!=32)throw std::runtime_error("Invalid handshake");
            crypto::Key nonce{};std::copy(bytes.begin(),bytes.end(),nonce.begin());
            if(hosting){
                c.nonce=crypto::random_key();c.cipher.initialize(invite.key,nonce,c.nonce,true);
                c.socket->send(json({{"type","hello"},{"nonce",crypto::encode(c.nonce)}}).dump());
                c.encrypted=true;
            }else{
                c.cipher.initialize(invite.key,c.nonce,nonce,false);c.encrypted=true;
                send_connection(id,{{"type","join"},{"build",build_id()}});
            }
            return;
        }
        if(!std::holds_alternative<transport::Binary>(raw))throw std::runtime_error("Unencrypted peer message");
        const auto& data=std::get<transport::Binary>(raw);
        auto j=json::parse(c.cipher.open({reinterpret_cast<const unsigned char*>(data.data()),data.size()}));
        auto type=j.at("type").get<std::string>();
        if(type=="join"&&hosting&&!c.joined){
            if(j.at("build")!=build_id()){send_connection(id,{{"type","error"},{"message","Game builds differ; install the same version."}});return;}
            int remote=1;while(remote<4&&roster[remote])++remote;
            if(remote==4||running){send_connection(id,{{"type","error"},{"message",running?"Game already started":"Room is full (four players)"}});return;}
            c.joined=true;c.slot=remote;peer_ids[remote]=id;roster[remote]=true;
            send_connection(id,{{"type","welcome"},{"slot",remote}});publish_roster();
        }else if(type=="welcome"&&!hosting&&!c.joined){
            slot=integer(j.at("slot"),1,3);c.joined=true;c.slot=0;
            std::lock_guard lock(mutex);view.slot=slot;view.connected=true;view.code=options.code;
            view.message="Connected. Waiting for the host.";fprintf(stderr,"[NETPLAY] joined as player %d\n",slot+1);changed.notify_all();
        }else if(type=="roster"&&!hosting&&c.joined&&!running){
            roster.fill(false);for(auto& p:j.at("slots"))roster[integer(p,0,3)]=true;
            if(!roster[0]||slot<0||!roster[slot])throw std::runtime_error("Invalid room membership");
            std::lock_guard lock(mutex);view.players=int(std::count(roster.begin(),roster.end(),true));
            for(int i=0;i<4;++i){view.ready[i]=roster[i];view.routes[i]=roster[i]&&i!=slot?(options.transport==Transport::Steam?"Steam P2P":"Direct"):"";}
            changed.notify_all();
        }else if(type=="start"&&!hosting&&c.joined){begin(j);}
        else if(type=="error"&&!hosting){fail(j.at("message").get<std::string>());}
        else if(c.joined&&running){peer_message(c.slot,j);}
        else throw std::runtime_error("Unexpected peer message");
    }
    void begin(const json& j){
        if(running||j.at("build")!=build_id()||integer(j.at("delay"),delay,delay)!=int(delay))throw std::runtime_error("Incompatible session start");
        uint8_t members=uint8_t(integer(j.at("mask"),3,15));
        if(!(members&1)||!(members&(1<<slot)))throw std::runtime_error("Invalid controller slots");
        for(int i=0;i<4;++i)if(bool(members&(1<<i))!=roster[i])throw std::runtime_error("Start roster mismatch");
        auto initial=decode_save(j.at("save"));auto seed_value=j.at("seed").get<uint64_t>();
        if(seed_value>UINT32_MAX)throw std::runtime_error("Invalid random seed");
        running=true;
        std::lock_guard lock(mutex);mask=members;rng_seed=uint32_t(seed_value);save=std::move(initial);
        for(unsigned f=0;f<delay;++f)frames[f]=Pads{};
        view.running=true;view.message="Game started";changed.notify_all();
    }
    void check(int remote, uint64_t f, uint64_t hash) {
        // Checkpoints travel independently of input delay; they never add a round trip per frame.
        if(f%60!=0 || f+window<commit_next || f>commit_next+window) throw std::runtime_error("Invalid state checkpoint");
        auto& group=checks[f];
        if(group[remote]) throw std::runtime_error("Duplicate state checkpoint");
        group[remote]=hash;
        for(int i=0;i<4;++i) if(roster[i] && !group[i]) return;
        for(int i=1;i<4;++i) if(roster[i] && group[i]!=group[0]) {
            json error={{"type","error"},{"message","Game desynchronized at frame "+std::to_string(f)+". Session stopped."}};
            broadcast(error); fail(error["message"].get<std::string>()); return;
        }
        checks.erase(f);
    }
    void submit(int remote, uint64_t f, Pad pad) {
        if(f<commit_next || f>commit_next+window) throw std::runtime_error("Input outside receive window");
        auto& group=pending[f];
        if(group[remote]) throw std::runtime_error("Duplicate input frame");
        group[remote]=pad;
        for(;;) {
            auto it=pending.find(commit_next);
            if(it==pending.end()) return;
            Pads pads{};
            for(int i=0;i<4;++i) if(roster[i]) {
                if(!it->second[i]) return;
                pads[i]=*it->second[i];
            }
            broadcast({{"type","frame"},{"frame",commit_next},{"pads",encode_pads(pads)}});
            { std::lock_guard lock(mutex); frames.emplace(commit_next,pads); changed.notify_all(); }
            pending.erase(it); ++commit_next;
        }
    }

    void peer_message(int remote,const json& j){
        auto type=j.at("type").get<std::string>();
        if(type=="input"&&hosting&&remote>0){submit(remote,frame_number(j.at("frame")),decode_pad(j.at("pad")));}
        else if(type=="check"&&hosting&&remote>0){check(remote,frame_number(j.at("frame")),j.at("hash").get<uint64_t>());}
        else if(type=="frame"&&!hosting&&remote==0){
            auto f=frame_number(j.at("frame"));auto& data=j.at("pads");
            if(!data.is_array()||data.size()!=4)throw std::runtime_error("Invalid committed inputs");
            Pads pads{};for(int i=0;i<4;++i)pads[i]=decode_pad(data[i]);
            std::lock_guard lock(mutex);
            if(f<consumed||f>=consumed+window||frames.contains(f))throw std::runtime_error("Invalid committed frame sequence");
            frames.emplace(f,pads);changed.notify_all();
        }else throw std::runtime_error("Unexpected peer message");
    }
    void run(){
        try{
            hosting=options.code.empty();
            wire=options.transport==Transport::Steam?transport::steam():transport::direct();
            if(hosting){
                invite={wire->host_address(options.address),options.port,crypto::random_key(),options.transport==Transport::Steam};
                invite.port=wire->listen(options.address,options.port,callback([](State& s,std::shared_ptr<transport::Socket> socket){s.attach(std::move(socket));}));
                auto code=crypto::invitation(invite);
                slot=0;roster[0]=true;
                std::lock_guard lock(mutex);view.connected=true;view.slot=0;view.players=1;view.ready[0]=true;view.code=code;
                view.message=options.transport==Transport::Steam?"Steam room ready (App ID 480). Share the code; no port forwarding needed.":"Share the code. Forward TCP port "+std::to_string(invite.port)+" for internet play.";
                fprintf(stderr,"[NETPLAY] connection code %s\n",code.c_str());changed.notify_all();
            }else{
                invite=crypto::parse_invitation(options.code);
                attach(wire->socket());
            }
        }catch(const std::exception& e){fail(e.what());}
        for(;;){
            std::function<void()> task;
            {
                std::unique_lock lock(mutex);changed.wait_for(lock,5ms,[&]{return stopping||!tasks.empty();});
                if(stopping)break;
                if(!tasks.empty()){task=std::move(tasks.front());tasks.pop_front();}
                else if(!view.connected&&!view.failed&&std::chrono::steady_clock::now()-connect_started>30s){lock.unlock();fail(options.transport==Transport::Steam?"Could not reach Steam host. Check Steam is online and the room code is current.":"Could not reach host. Check the address and TCP port forwarding.");continue;}
            }
            try{
                if(task)task();
                if(wire)wire->poll();
                std::vector<unsigned> expired;
                for(auto& [id,c]:connections)if(!c->joined&&std::chrono::steady_clock::now()-c->created>(options.transport==Transport::Steam?30s:10s))expired.push_back(id);
                for(auto id:expired)disconnect(id);
            }catch(const std::exception& e){fail(e.what());}
        }
        {std::lock_guard lock(mutex);tasks.clear();}
        for(auto& [id,c]:connections){c->socket->reset_callbacks();c->socket->close();}
        connections.clear();
        wire.reset();
    }
};
void validate_options(const Options& options){
    if(options.code.empty()&&options.transport==Transport::Direct)crypto::invitation({options.address,options.port?options.port:uint16_t(1),{}});
    else if(options.code.empty())return;
    else crypto::parse_invitation(options.code);
}
Session::Session(const Options& options):state(std::make_shared<State>()){
    validate_options(options);
    state->options=options;
    if(!options.code.empty())state->options.transport=crypto::parse_invitation(options.code).steam?Transport::Steam:Transport::Direct;
    state->view.message=state->options.transport==Transport::Steam?"Connecting through Steam (App ID 480)...":"Connecting directly to host...";
    state->worker=std::thread([s=state]{s->run();});
}
Session::~Session(){close();}
void Session::close(){
    if(!state)return;
    {std::lock_guard lock(state->mutex);state->stopping=true;state->changed.notify_all();}
    if(state->worker.joinable())state->worker.join();
}
Status Session::status() const {std::lock_guard lock(state->mutex);return state->view;}
void Session::start(const std::vector<uint8_t>& save){
    state->post([s=state,save]{
        if(!s->hosting||s->running||s->slot<0)throw std::runtime_error("Only the room host can start");
        if(save.size()!=0&&save.size()!=512)throw std::runtime_error("Host EEPROM must be exactly 512 bytes");
        if(std::count(s->roster.begin(),s->roster.end(),true)<2)throw std::runtime_error("Wait for at least one friend");
        uint8_t members=0;for(int i=0;i<4;++i)if(s->roster[i])members|=1<<i;
        auto random=crypto::random_key();uint32_t seed=uint32_t(random[0])<<24|uint32_t(random[1])<<16|uint32_t(random[2])<<8|random[3];
        json start={{"type","start"},{"seed",seed},{"mask",members},{"delay",delay},{"save",save},{"build",build_id()}};
        s->broadcast(start);s->begin(start);
    });
}
std::vector<uint8_t> Session::initial_save() const {std::lock_guard lock(state->mutex);return state->save;}
uint32_t Session::seed() const {std::lock_guard lock(state->mutex);return state->rng_seed;}
uint8_t Session::connected_mask() const {std::lock_guard lock(state->mutex);return state->mask;}
Pads Session::exchange(Pad local,uint64_t checksum){
    uint64_t f;
    {std::lock_guard lock(state->mutex);f=state->consumed;if(!state->view.running||state->view.failed||state->stopping)throw std::runtime_error(state->view.message);}
    state->post([s=state,f,local,checksum]{
        if(s->hosting){if(f%60==0)s->check(0,f,checksum);s->submit(0,f+delay,local);}
        else{if(f%60==0)s->send(0,{{"type","check"},{"frame",f},{"hash",checksum}});s->send(0,{{"type","input"},{"frame",f+delay},{"pad",encode_pad(local)}});}
    });
    std::unique_lock lock(state->mutex);
    if(!state->changed.wait_for(lock,15s,[&]{return state->stopping||state->view.failed||state->frames.contains(f);})){lock.unlock();state->fail("Timed out waiting for player inputs. Session stopped.");lock.lock();}
    if(state->stopping||state->view.failed)throw std::runtime_error(state->view.message);
    auto pads=state->frames.at(f);state->frames.erase(f);++state->consumed;return pads;
}
}

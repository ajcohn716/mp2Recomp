// Include the adapter to exercise its socket implementation without exporting
// test-only hooks in the production transport interface. Uses the real SDK and
// Steam client, with encrypted UDP loopback, not a mock Steam implementation.
#include "netplay_steam.cpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
using namespace netplay::transport;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main() try {
    for(int attempt=0;attempt<2;++attempt) {
        Steam room;
        require(SteamUtils()->GetAppID()==480,"Unexpected Steam App ID");
        bool rejected=false;try{auto duplicate=steam();}catch(...){rejected=true;}
        require(rejected,"Overlapping Steam API owners accepted");
        require(!room.host_address("").empty(),"Missing host Steam identity");
        require(room.listen("",0,[](auto){})==1,"Steam listener creation failed");
        auto first=std::static_pointer_cast<SteamSocket>(room.socket());
        auto second=std::static_pointer_cast<SteamSocket>(room.socket());
        require(room.api->CreateSocketPair(&first->handle,&second->handle,true,nullptr,nullptr),"Steam UDP socket pair failed");
        first->connected=second->connected=true;
        room.peers.emplace(first->handle,first);room.peers.emplace(second->handle,second);
        require(room.api->SetConnectionPollGroup(first->handle,room.group),"First receive group failed");
        require(room.api->SetConnectionPollGroup(second->handle,room.group),"Second receive group failed");
        int received=0;
        second->on_message=[&](Message message){
            if(received%2==0)require(std::get<std::string>(message)=="hello "+std::to_string(received/2),"Steam text ordering failed");
            else require(std::get<Binary>(message)==Binary(16408,std::byte{uint8_t(received/2)}),"Steam binary ordering failed");
            ++received;
        };
        for(int i=0;i<16;++i) {
            first->send("hello "+std::to_string(i));first->send(Binary(16408,std::byte{uint8_t(i)}));
        }
        auto deadline=std::chrono::steady_clock::now()+10s;
        while(received!=32&&std::chrono::steady_clock::now()<deadline){room.poll();std::this_thread::sleep_for(1ms);}
        require(received==32,"Steam reliable messages timed out");
        bool oversized=false;try{first->send(Binary(16409));}catch(...){oversized=true;}
        require(oversized,"Oversized Steam packet accepted");
        first->reset_callbacks();second->reset_callbacks();first->close();second->close();
        require(!first->is_open()&&!second->is_open(),"Steam close failed");
    }
    std::cout<<"PASS: real Steam App ID 480, P2P listener, encrypted UDP loopback, reliable text/binary ordering, bounds, shutdown/reinitialize\n";
    return 0;
} catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}

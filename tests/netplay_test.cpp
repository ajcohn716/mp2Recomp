// Integration test against a real host listener and encrypted direct connections.
#include "netplay.hpp"
#include "netplay_crypto.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class F> void wait(F predicate) {
    auto deadline=std::chrono::steady_clock::now()+20s;
    while(!predicate()) {if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Connection test timed out");std::this_thread::sleep_for(10ms);}
}
void crypto_checks() {
    using namespace netplay::crypto;
    auto secret=random_key(), client=random_key(), host=random_key();
    auto code=invitation({"127.0.0.1",7777,secret});auto invite=parse_invitation(code);
    require(invite.key==secret && invite.address=="127.0.0.1" && invite.port==7777,"Invitation round trip");
    for(const auto& invalid:{std::string("garbage"),code+"=",std::string("MP2-A")}) {
        bool rejected=false;try{parse_invitation(invalid);}catch(...){rejected=true;}require(rejected,"Malformed code accepted");
    }
    Channel sender,receiver;sender.initialize(secret,client,host,false);receiver.initialize(secret,client,host,true);
    auto record=sender.seal("controller inputs");require(receiver.open(record)=="controller inputs","Encryption round trip");
    bool rejected=false;try{receiver.open(record);}catch(...){rejected=true;}require(rejected,"Replay accepted");
    auto next=sender.seal("next frame");next[8]^=1;rejected=false;try{receiver.open(next);}catch(...){rejected=true;}require(rejected,"Modified ciphertext accepted");
    Channel wrong;wrong.initialize(random_key(),client,host,true);rejected=false;try{wrong.open(record);}catch(...){rejected=true;}require(rejected,"Wrong session key accepted");
    Channel reflected;reflected.initialize(secret,client,host,false);rejected=false;try{reflected.open(record);}catch(...){rejected=true;}require(rejected,"Reflected packet accepted");
    std::cout<<"PASS: invitation parsing, authenticated encryption, tamper/replay/wrong-key/reflection rejection\n";
}
int main() try {
    crypto_checks();
    netplay::Session host({"127.0.0.1",0,""});
    wait([&]{auto s=host.status();if(s.failed)throw std::runtime_error(s.message);return s.connected;});
    auto code=host.status().code;
    {
        auto wrong=netplay::crypto::parse_invitation(code);wrong.key=netplay::crypto::random_key();
        netplay::Session intruder({"127.0.0.1",0,netplay::crypto::invitation(wrong)});
        wait([&]{return intruder.status().failed;});
        require(!host.status().failed && host.status().players==1,"Invalid invitation disrupted host");
    }
    std::array<std::unique_ptr<netplay::Session>,3> guests;
    for(int i=0;i<3;++i) {
        guests[i]=std::make_unique<netplay::Session>(netplay::Options{"127.0.0.1",0,code});
        wait([&]{auto s=guests[i]->status();if(s.failed)throw std::runtime_error(s.message);return s.connected;});
        require(guests[i]->status().slot==i+1,"Controller slot assignment");
    }
    wait([&]{auto s=host.status();if(s.failed)throw std::runtime_error(s.message);return s.players==4 && s.ready[1] && s.ready[2] && s.ready[3];});
    {
        netplay::Session fifth({"127.0.0.1",0,code});wait([&]{return fifth.status().failed;});
        require(fifth.status().message.find("full")!=std::string::npos,"Fifth player not rejected");
    }
    std::vector<uint8_t> save(512);for(int i=0;i<512;++i)save[i]=uint8_t(i);
    host.start(save);
    wait([&]{if(host.status().failed)throw std::runtime_error(host.status().message);for(auto& guest:guests){if(guest->status().failed)throw std::runtime_error(guest->status().message);if(!guest->status().running)return false;}return host.status().running;});
    std::array<netplay::Session*,4> clients{&host,guests[0].get(),guests[1].get(),guests[2].get()};
    for(auto* c:clients){require(c->initial_save()==save,"EEPROM mismatch");require(c->seed()==host.seed(),"Random seed mismatch");require(c->connected_mask()==15,"Controller mask mismatch");}
    std::array<std::future<std::vector<netplay::Pads>>,4> runs;
    for(int p=0;p<4;++p) runs[p]=std::async(std::launch::async,[&,p]{
        std::vector<netplay::Pads> result;
        for(int f=0;f<360;++f) {
            // Different rates and asymmetric bursts model a slow peer. Reliable
            // direct streams handle retransmission; no fake transport is used.
            if((f+p)%17==0)std::this_thread::sleep_for(std::chrono::milliseconds(2+p*3));
            auto pads=clients[p]->exchange({uint16_t((f*31+p)&65535),int8_t(f%161-80),int8_t(p*20-30)},uint64_t(f*123));
            if(f<3)require(pads==netplay::Pads{},"Initial delay not neutral");
            else for(int i=0;i<4;++i)require(pads[i]==netplay::Pad{uint16_t(((f-3)*31+i)&65535),int8_t((f-3)%161-80),int8_t(i*20-30)},"Input ordering or controller ownership failed");
            result.push_back(pads);
        }
        return result;
    });
    auto baseline=runs[0].get();for(int i=1;i<4;++i)require(runs[i].get()==baseline,"Peers disagree on input stream");
    std::cout<<"PASS: four real peers, "<<"encrypted direct TCP"<<", 360 input frames, slot ownership, delay, seed and EEPROM agreement\n";
    // Inject a real state mismatch at the next checkpoint (frame 360).
    std::array<std::future<bool>,4> faults;
    for(int p=0;p<4;++p)faults[p]=std::async(std::launch::async,[&,p]{try{for(int f=360;f<380;++f)clients[p]->exchange({},p==2?999:1000);return false;}catch(const std::exception&){return true;}});
    for(auto& f:faults)require(f.get(),"Desynchronization did not stop all clients");
    std::cout<<"PASS: injected desynchronization stops every peer\n";
    {
        netplay::Session next_host({"127.0.0.1",0,""});wait([&]{return next_host.status().connected;});
        auto guest=std::make_unique<netplay::Session>(netplay::Options{"127.0.0.1",0,next_host.status().code});
        wait([&]{return guest->status().connected && next_host.status().players==2;});
        guest.reset();wait([&]{return next_host.status().players==1;});
        guest=std::make_unique<netplay::Session>(netplay::Options{"127.0.0.1",0,next_host.status().code});
        wait([&]{return guest->status().connected && next_host.status().players==2;});
        require(guest->status().slot==1,"Vacated slot not reused");
        next_host.start({});wait([&]{return guest->status().running;});
        netplay::Session late({"127.0.0.1",0,next_host.status().code});wait([&]{return late.status().failed;});
        require(!next_host.status().failed,"Late join disrupted running session");
        guest.reset();wait([&]{return next_host.status().failed;});
        std::cout<<"PASS: wrong-key/full/late-join rejection, lobby slot reuse and running disconnect shutdown\n";
    }
    return 0;
} catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}

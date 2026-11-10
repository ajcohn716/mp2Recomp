// Exercises the process protocol and shared proxy, without a Steam client/SDK.
#include "netplay_steam_social.hpp"
#include "netplay_transport.hpp"
#include "steam_helper_protocol.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace std::chrono_literals;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void wait(F ready) {
    auto deadline=std::chrono::steady_clock::now()+5s;
    while(!ready()){require(std::chrono::steady_clock::now()<deadline,"Timed out");std::this_thread::sleep_for(1ms);}
}
int main(int argc,char** argv) try {
    if(argc==2&&std::string(argv[1])=="--stdio") {
#ifdef _WIN32
        _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
        using namespace steam_helper;
        write(stdout,{'H',0,"2:76561198000000000"});
        Record command;
        while(read(stdin,command)) {
            if(command.kind=='Q')break;
            if(command.kind=='f') {
                write(stdout,{'f',0,{}});write(stdout,{'g',0,"76561198000000001\nSpacewar friend"});
                write(stdout,{'v',0,"109775240000000001\nInviting friend"});
            }else if(command.kind=='L')write(stdout,{'L',0,{}});
            else if(command.kind=='p')write(stdout,{'p',0,command.data.empty()?"":"1"});
            else if(command.kind=='i')write(stdout,{'s',0,"Steam invite sent"});
            else if(command.kind=='j')write(stdout,{'r',0,"test-connect-code"});
            else if(command.kind=='C')write(stdout,{'O',command.peer,{}});
            else if(command.kind=='T'||command.kind=='B')write(stdout,command);
            else if(command.kind=='U')write(stdout,command);
        }
        return 0;
    }
    auto path=std::filesystem::absolute(argv[0]).string();
#ifdef _WIN32
    _putenv_s("MP2_STEAM_HELPER",path.c_str());
#else
    setenv("MP2_STEAM_HELPER",path.c_str(),1);
#endif
    netplay::SteamSocial social;
    netplay::SteamSocialStatus view;
    wait([&]{view=social.status();return view.available&&!view.friends.empty()&&!view.invitations.empty();});
    require(view.friends[0].name=="Spacewar friend","Lost friend metadata");
    require(view.invitations[0].name=="Inviting friend","Lost invitation metadata");
    social.dismiss(view.invitations[0].id);require(social.status().invitations.empty(),"Dismiss failed");
    social.refresh();wait([&]{return !social.status().invitations.empty();});
    social.accept("109775240000000001");
    wait([&]{return social.status().join_code=="test-connect-code";});
    require(social.status().join_code.empty(),"Join request was delivered twice");
    for(int attempt=0;attempt<2;++attempt) {
        auto wire=netplay::transport::steam();
        bool rejected=false;try{auto duplicate=netplay::transport::steam();}catch(...){rejected=true;}
        require(rejected,"Two transports shared socket state");
        require(wire->listen("",0,[](auto){})==1,"Listen failed");
        wire->social('p',"private-code");wait([&]{return social.status().invite_ready;});
        social.invite("76561198000000001");wait([&]{return social.status().message=="Steam invite sent";});
        auto socket=wire->socket();socket->open("76561198000000001",0);
        wait([&]{social.status();wire->poll();return socket->is_open();});
        int received=0;socket->on_message=[&](auto message){require(std::get<std::string>(message)=="hello","Corrupt transport message");++received;};
        socket->send("hello");
        // UI polling must retain network events for the worker.
        wait([&]{social.status();wire->poll();return received==1;});
        wire->social('p',"");wait([&]{return !social.status().invite_ready;});
        socket->reset_callbacks();socket->close();
    }
    social.refresh();wait([&]{return !social.status().friends.empty();});
    std::cout<<"PASS: shared Steam helper, nonblocking social startup, friends/invites, accept/dismiss, socket event routing, transport reuse\n";
} catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}

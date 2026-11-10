// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Cohn
// Standalone Steam networking service. No game, ROM, GPL runtime, or SDL linkage.
#include "netplay_transport.hpp"
#include "steam_helper_protocol.hpp"
#include <chrono>
#include <cstring>
#include <map>
#include <iostream>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif
using namespace netplay::transport;
using namespace steam_helper;
int main(int argc,char** argv){
    if(argc==2&&std::strcmp(argv[1],"--self-test")==0){
        FILE* file=tmpfile();if(!file)return 1;
        write(file,{'B',42,std::string("a\0b",3)});rewind(file);Record record;
        bool ok=read(file,record)&&record.kind=='B'&&record.peer==42&&record.data==std::string("a\0b",3);
        fclose(file);std::cout<<"Steam helper protocol 2; App ID 480; standalone MIT service\n";return ok?0:1;
    }
    if(argc!=2||std::strcmp(argv[1],"--stdio")!=0){
        std::cout<<"Usage: mp2recomp-steam-helper --stdio | --self-test\n"
                   "Framed stdin/stdout: L listen, C connect, T text, B bytes, X close, Q quit.\n";return 0;
    }
    // Keep SDK/client diagnostics out of the protocol channel.
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    FILE* output=_fdopen(_dup(_fileno(stdout)),"wb");_dup2(_fileno(stderr),_fileno(stdout));
#else
    FILE* output=fdopen(dup(fileno(stdout)),"wb");dup2(fileno(stderr),fileno(stdout));
#endif
    if(!output)return 1;
    // The reader is joined after Q or EOF. Fatal errors terminate this separate
    // service after destroying the Steam transport; no blocked reader is leaked.
    auto inbox=std::make_unique<Inbox>();
    int result=0;
    try{
        auto network=steam();std::map<uint32_t,std::shared_ptr<Socket>> peers;uint32_t accepted=0x80000000;
        auto attach=[&](uint32_t id,std::shared_ptr<Socket> socket){
            peers.emplace(id,socket);
            socket->on_open=[&,id]{write(output,{'O',id,{}});};
            socket->on_close=[&,id]{write(output,{'X',id,{}});};
            socket->on_message=[&,id](Message message){
                if(auto* text=std::get_if<std::string>(&message))write(output,{'T',id,*text});
                else {auto& bytes=std::get<Binary>(message);write(output,{'B',id,std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size())});}
            };
        };
        network->social_event=[&](char kind,std::string data){write(output,{kind,0,std::move(data)});};
        write(output,{'H',0,"2:"+network->host_address("")});inbox->start(stdin);
        bool quit=false;
        while(!quit){
            for(auto& command:inbox->take()){
                if(command.kind=='Q'){quit=true;break;}
                if(command.kind=='f'||command.kind=='p'||command.kind=='i'||command.kind=='j'){network->social(command.kind,command.data);continue;}
                if(command.kind=='U'){
                    for(auto& [id,socket]:peers){socket->reset_callbacks();socket->close();}peers.clear();
                    network->stop_listening();write(output,{'U',command.peer,{}});continue;
                }
                if(command.kind=='L'){
                    network->listen("",0,[&](std::shared_ptr<Socket> socket){
                        if(peers.size()>=16){socket->close();return;}
                        auto id=accepted++;attach(id,socket);write(output,{'A',id,{}});
                    });
                    write(output,{'L',0,{}});continue;
                }
                if(command.kind=='C'){
                    if(!command.peer||command.peer>=0x80000000||peers.contains(command.peer)||peers.size()>=16)
                        throw std::runtime_error("Invalid helper connection ID");
                    auto socket=network->socket();attach(command.peer,socket);socket->open(command.data,0);continue;
                }
                auto found=peers.find(command.peer);
                if(found==peers.end())throw std::runtime_error("Unknown helper connection ID");
                auto socket=found->second;
                if(command.kind=='X'){socket->reset_callbacks();socket->close();peers.erase(found);}
                else if(command.kind=='T')socket->send(command.data);
                else if(command.kind=='B')socket->send(Binary(reinterpret_cast<const std::byte*>(command.data.data()),reinterpret_cast<const std::byte*>(command.data.data()+command.data.size())));
                else throw std::runtime_error("Unknown helper command");
            }
            if(quit||inbox->finished())break;
            network->poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for(auto& [id,socket]:peers){socket->reset_callbacks();socket->close();}peers.clear();
        network.reset();
    }catch(const std::exception& e){
        try{write(output,{'E',0,e.what()});}catch(...){}result=1;
    }
    fclose(output);
    // On a fatal error the parent may still hold stdin open. Process exit closes
    // the pipe and lets the parent report the error without a shutdown deadlock.
    std::_Exit(result);
}

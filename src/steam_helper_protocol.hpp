// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Cohn
// Generic framed stream protocol; contains no game or emulator state.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#endif

namespace steam_helper {
inline constexpr size_t max_payload=16408;
struct Record { char kind{}; uint32_t peer{}; std::string data; };
inline void encode32(unsigned char* p,uint32_t n){for(int i=0;i<4;++i)p[i]=static_cast<unsigned char>(n>>(8*i));}
inline uint32_t decode32(const unsigned char* p){uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(p[i])<<(8*i);return n;}
inline bool read(FILE* input,Record& record){
    std::array<unsigned char,9> header{};
    size_t n=fread(header.data(),1,header.size(),input);
    if(!n&&feof(input))return false;
    if(n!=header.size())throw std::runtime_error("Steam helper stream ended inside a header");
    auto size=decode32(header.data());
    if(size>max_payload)throw std::runtime_error("Steam helper record too large");
    record.kind=char(header[4]);record.peer=decode32(header.data()+5);record.data.resize(size);
    if(size&&fread(record.data.data(),1,size,input)!=size)throw std::runtime_error("Steam helper stream ended inside a record");
    return true;
}
inline void write(FILE* output,const Record& record){
    if(record.data.size()>max_payload)throw std::runtime_error("Steam helper record too large");
#ifndef _WIN32
    // A helper exiting must report an error instead of terminating the game.
    sigset_t block,previous,pending;sigemptyset(&block);sigaddset(&block,SIGPIPE);
    pthread_sigmask(SIG_BLOCK,&block,&previous);sigpending(&pending);
    bool already_pending=sigismember(&pending,SIGPIPE);
#endif
    std::array<unsigned char,9> header{};encode32(header.data(),uint32_t(record.data.size()));
    header[4]=static_cast<unsigned char>(record.kind);encode32(header.data()+5,record.peer);
    bool ok=fwrite(header.data(),1,header.size(),output)==header.size();
    if(ok&&!record.data.empty())ok=fwrite(record.data.data(),1,record.data.size(),output)==record.data.size();
    if(ok)ok=fflush(output)==0;
#ifndef _WIN32
    if(!already_pending){timespec timeout{};sigtimedwait(&block,nullptr,&timeout);}
    pthread_sigmask(SIG_SETMASK,&previous,nullptr);
#endif
    if(!ok)throw std::runtime_error("Steam helper disconnected");
}
struct Inbox {
    std::mutex mutex;
    std::deque<Record> pending;
    std::thread reader;
    std::string error;
    bool ended{};
    size_t bytes{};
    void start(FILE* input){reader=std::thread([this,input]{
        try{
            Record record;
            while(read(input,record)){
                bool quit=record.kind=='Q';
                {std::lock_guard lock(mutex);
                    if(pending.size()>=1024||bytes+record.data.size()>2*1024*1024)throw std::runtime_error("Steam helper queue full");
                    bytes+=record.data.size();pending.push_back(std::move(record));}
                if(quit)break;
            }
        }catch(const std::exception& e){std::lock_guard lock(mutex);error=e.what();}
        std::lock_guard lock(mutex);ended=true;
    });}
    std::deque<Record> take(){
        std::lock_guard lock(mutex);
        if(!error.empty())throw std::runtime_error(error);
        std::deque<Record> result;result.swap(pending);bytes=0;return result;
    }
    bool finished(){std::lock_guard lock(mutex);return ended;}
    ~Inbox(){if(reader.joinable())reader.join();}
};
}

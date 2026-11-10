// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Cohn
#pragma once
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

namespace steam_helper {
inline std::filesystem::path executable_directory(){
#ifdef _WIN32
    std::wstring path(32768,L'\0');auto n=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
    if(!n||n>=path.size())throw std::runtime_error("Cannot locate executable directory");
    path.resize(n);return std::filesystem::path(path).parent_path();
#else
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
}
struct Process {
    FILE* input{}; // parent writes commands
    FILE* output{}; // parent reads events
#ifdef _WIN32
    HANDLE child{};
#else
    pid_t child{-1};
#endif
    void start(){
        auto path=executable_directory()/
#ifdef _WIN32
            "mp2recomp-steam-helper.exe";
#else
            "mp2recomp-steam-helper";
#endif
        if(auto override_path=std::getenv("MP2_STEAM_HELPER"))path=std::filesystem::u8path(override_path);
        if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Steam helper missing. Reinstall the complete v0.2.1 installer, or select Direct TCP.");
#ifdef _WIN32
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        HANDLE in_read{},in_write{},out_read{},out_write{};
        if(!CreatePipe(&in_read,&in_write,&security,0))throw std::runtime_error("Cannot create Steam helper input");
        if(!CreatePipe(&out_read,&out_write,&security,0)){CloseHandle(in_read);CloseHandle(in_write);throw std::runtime_error("Cannot create Steam helper output");}
        SetHandleInformation(in_write,HANDLE_FLAG_INHERIT,0);SetHandleInformation(out_read,HANDLE_FLAG_INHERIT,0);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdInput=in_read;startup.hStdOutput=out_write;startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process{};std::wstring command=L"\""+path.wstring()+L"\" --stdio";
        bool ok=CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);
        CloseHandle(in_read);CloseHandle(out_write);
        if(!ok){CloseHandle(in_write);CloseHandle(out_read);throw std::runtime_error("Cannot start Steam helper (check bundled runtime files)");}
        child=process.hProcess;CloseHandle(process.hThread);
        input=_fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(in_write),_O_BINARY),"wb");
        output=_fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(out_read),_O_BINARY),"rb");
#else
        int commands[2],events[2];
        if(pipe(commands))throw std::runtime_error("Cannot create Steam helper input");
        if(pipe(events)){close(commands[0]);close(commands[1]);throw std::runtime_error("Cannot create Steam helper output");}
        std::string program=path.string();child=fork();
        if(child==0){
            dup2(commands[0],STDIN_FILENO);dup2(events[1],STDOUT_FILENO);
            close(commands[0]);close(commands[1]);close(events[0]);close(events[1]);
            execl(program.c_str(),program.c_str(),"--stdio",static_cast<char*>(nullptr));_exit(127);
        }
        close(commands[0]);close(events[1]);
        if(child<0){close(commands[1]);close(events[0]);throw std::runtime_error("Cannot start Steam helper");}
        input=fdopen(commands[1],"wb");output=fdopen(events[0],"rb");
#endif
        if(!input||!output)throw std::runtime_error("Cannot open Steam helper streams");
    }
    void stop(){
        if(input){fclose(input);input=nullptr;}
#ifdef _WIN32
        if(child){if(WaitForSingleObject(child,2000)==WAIT_TIMEOUT){TerminateProcess(child,1);WaitForSingleObject(child,2000);}CloseHandle(child);child=nullptr;}
#else
        if(child>0){
            bool exited=false;
            for(int i=0;i<200;++i){auto r=waitpid(child,nullptr,WNOHANG);if(r==child||r<0){exited=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            if(!exited){kill(child,SIGKILL);waitpid(child,nullptr,0);}child=-1;
        }
#endif
    }
    ~Process(){stop();if(output)fclose(output);}
};
}

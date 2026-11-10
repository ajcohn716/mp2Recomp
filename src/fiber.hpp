#pragma once
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <ucontext.h>
#include <memory>
#endif

// Fibers never cross OS threads. FIBER_FLAG_FLOAT_SWITCH preserves host floating
// point state just as swapcontext does on POSIX.
namespace mp2 {
class Scheduler;
class Fiber {
    friend class Scheduler;
#ifdef _WIN32
    void* handle{};
    static void WINAPI enter(void* p) { static_cast<Fiber*>(p)->entry(); std::abort(); }
#else
    ucontext_t context{};
    std::unique_ptr<char[]> stack;
#endif
    void (*entry)(){};
public:
    explicit Fiber(void (*function)()):entry(function) {
#ifdef _WIN32
        handle=CreateFiberEx(64*1024,1024*1024,FIBER_FLAG_FLOAT_SWITCH,enter,this);
        if(!handle)std::abort();
#else
        stack=std::make_unique<char[]>(1024*1024);
        if(getcontext(&context))std::abort();
        context.uc_stack.ss_sp=stack.get();context.uc_stack.ss_size=1024*1024;
        context.uc_link=nullptr;makecontext(&context,function,0);
#endif
    }
    ~Fiber(){
#ifdef _WIN32
        if(handle)DeleteFiber(handle);
#endif
    }
    Fiber(const Fiber&)=delete;
    Fiber& operator=(const Fiber&)=delete;
};
class Scheduler {
#ifdef _WIN32
    void* handle{};
    bool converted{};
    void prepare(){
        if(!handle){
            if(IsThreadAFiber())handle=GetCurrentFiber();
            else{handle=ConvertThreadToFiberEx(nullptr,FIBER_FLAG_FLOAT_SWITCH);converted=true;}
            if(!handle)std::abort();
        }
    }
#else
    ucontext_t context{};
#endif
public:
    ~Scheduler(){
#ifdef _WIN32
        if(converted)ConvertFiberToThread();
#endif
    }
    void resume(Fiber& fiber){
#ifdef _WIN32
        prepare();SwitchToFiber(fiber.handle);
#else
        if(swapcontext(&context,&fiber.context))std::abort();
#endif
    }
    void yield(Fiber& fiber){
#ifdef _WIN32
        SwitchToFiber(handle);
#else
        if(swapcontext(&fiber.context,&context))std::abort();
#endif
    }
};
}

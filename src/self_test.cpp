#include "fiber.hpp"
#include <cstdio>
namespace {
mp2::Scheduler scheduler;
mp2::Fiber* current;
int progress;
void task(){int retained=37;progress=1;scheduler.yield(*current);progress=retained+5;scheduler.yield(*current);std::abort();}
}
int mp2_self_test(){
    mp2::Fiber fiber(task);current=&fiber;
    scheduler.resume(fiber);if(progress!=1)return 1;
    scheduler.resume(fiber);if(progress!=42)return 2;
    puts("Native fiber suspend/resume check passed.");return 0;
}

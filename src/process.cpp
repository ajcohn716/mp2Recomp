// Hudson's cooperative processes retain entire guest and host stacks across
// sleeps. POSIX ucontext fibers provide that behavior without jumping back into
// C stack frames which have already returned.
#include "fiber.hpp"
#include <array>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include "recomp.h"
#include "funcs.h"
#include "guest_memory.hpp"

namespace {
constexpr uint32_t current_addr = 0x800EB8F0, top_addr = 0x800EB8EC;
struct Fiber {
    mp2::Fiber native;
    explicit Fiber(void (*entry)()):native(entry) {}
    recomp_context ctx{};
    uint8_t* rdram{};
    uint32_t process{};
    bool dead{};
};
thread_local std::unordered_map<uint32_t, std::unique_ptr<Fiber>> fibers;
thread_local mp2::Scheduler scheduler;
thread_local Fiber* active;

void fiber_entry() {
    Fiber& f = *active;
    uint8_t* rdram = f.rdram;
    uint32_t entry = MEM_W(0x30, f.process);
    f.ctx.r29 = MEM_W(0x2C, f.process);
    f.ctx.f_odd = &f.ctx.f0.u32h;
    get_function(entry)(rdram, &f.ctx);
    HuPrcEnd(rdram, &f.ctx);
    std::abort();
}
void yield() { scheduler.yield(active->native); }
}

extern "C" void HuPrcSleep(uint8_t* rdram, recomp_context* ctx) {
    uint32_t p = MEM_W(0, current_addr);
    if (int32_t(ctx->r4) != 0 && MEM_HU(0x1C, p) != 3) {
        MEM_H(0x1C, p) = 1;
        MEM_W(0x24, p) = ctx->r4;
    }
    yield();
}
extern "C" void HuPrcChildWatch(uint8_t* rdram, recomp_context* ctx) {
    uint32_t p = MEM_W(0, current_addr);
    if (MEM_W(0x08, p)) { MEM_H(0x1C, p) = 2; yield(); }
}
extern "C" void HuPrcTerminate(uint8_t* rdram, recomp_context* ctx) {
    uint32_t p = ctx->r4;
    uint32_t destructor = MEM_W(0x88, p);
    if (destructor) get_function(destructor)(rdram, ctx);
    ctx->r4 = (int32_t)top_addr; ctx->r5 = (int32_t)p;
    HuUnlinkPrc(rdram, ctx);
    MEM_H(0, 0x800EB8F4) -= 1;
    active->dead = true;
    yield();
    std::abort();
}
extern "C" void HuPrcCall(uint8_t* rdram, recomp_context* ctx) {
    int32_t ticks = ctx->r4;
    uint32_t p = MEM_W(0, top_addr);
    while (p) {
        MEM_W(0, current_addr) = p;
        uint16_t mode = MEM_HU(0x1C, p);
        bool run = true;
        if ((MEM_HU(0x1E, p) & 1) && mode != 3) run = false;
        else if (mode == 1) {
            int32_t remaining = MEM_W(0x24, p);
            if (remaining > 0) {
                remaining -= ticks;
                if (remaining <= 0) { remaining = 0; MEM_H(0x1C, p) = 0; }
                MEM_W(0x24, p) = remaining;
            }
            run = false;
        } else if (mode == 2) {
            if (MEM_W(0x08, p)) run = false;
            else MEM_H(0x1C, p) = 0;
        }
        if (run) {
            if (mode == 3) {
                // Discard the suspended continuation and enter the game's cleanup.
                fibers.erase(p);
                MEM_W(0x30, p) = (int32_t)0x8007D9A4; // HuPrcEnd
            }
            auto& slot = fibers[p];
            if (!slot) {
                slot = std::make_unique<Fiber>(fiber_entry);
                auto& f = *slot;
                f.ctx = *ctx; f.rdram = rdram; f.process = p;
                fprintf(stderr, "[MP2] process %08x entry %08x\n", p, uint32_t(MEM_W(0x30, p)));
            }
            active = slot.get();
            scheduler.resume(active->native);
            active = nullptr;
            if (slot->dead) {
                uint32_t heap = MEM_W(0x18, p);
                uint32_t next = MEM_W(0, p);
                fibers.erase(p);
                gpr saved = ctx->r4; ctx->r4 = (int32_t)heap;
                HuMemMemoryFreePerm(rdram, ctx); ctx->r4 = saved;
                p = next;
                continue;
            }
        }
        p = MEM_W(0, p);
    }
    MEM_W(0, current_addr) = 0;
}

// Host-authored adapter for the supported US ROM. Only guest addresses and
// controller buffer layout are recorded here; game instructions stay generated.
#include "netplay_game.hpp"
#include "guest_memory.hpp"
#include "ultramodern/ultramodern.hpp"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <stdexcept>

void convert_to_n64_range(float,float,int8_t&,int8_t&);
extern "C" void mp2_original_osGetTime_recomp(uint8_t*,recomp_context*);
extern "C" void mp2_original_osGetCount_recomp(uint8_t*,recomp_context*);
namespace netplay::game {
namespace {
Session* session{};
unsigned probe{}, stop{};
bool bot{};
std::atomic<bool> error{}, cancelled{};
std::atomic<uint64_t> frame{};
std::mutex input_mutex;
Pad input;
std::ofstream trace;
uint64_t state_checksum(uint8_t* rdram) {
    // Canonical gameplay checkpoint, excluding asynchronous audio/graphics and
    // native pointers. Detects RNG, board/player and overlay-history divergence.
    // It isn't a hash of all simulation memory; see docs/netplay.md.
    uint64_t hash=14695981039346656037ULL;
    const std::pair<uint32_t,uint32_t> ranges[]={{0x800C99B4,4},{0x800FD2C0,0xD0},
        {0x800F93A8,0x28},{0x800FDBE8,0x60},{0x800CD418,2}};
    for(auto [base,size]:ranges)for(uint32_t i=0;i<size;++i){hash^=MEM_BU(i,base);hash*=1099511628211ULL;}
    return hash;
}
Pad sample(uint64_t f,int slot) {
    if(!bot){std::lock_guard lock(input_mutex);return input;}
    // Repeatable start/A presses, then direction changes to exercise menus.
    Pad pad{};
    if(f>300 && f%180<3)pad.buttons=0x1000;
    if(f>650 && f%90>=20 && f%90<23)pad.buttons|=0x8000;
    if(f>1000 && f%240<4)pad.y=slot%2? -60:60;
    return pad;
}
}
void configure(Session* s,unsigned p,unsigned end,const std::filesystem::path& path,bool automated) {
    session=s;probe=p;stop=end;bot=automated;
    if(!path.empty()){trace.open(path);if(!trace)throw std::runtime_error("Cannot open netplay trace");}
}
void local_input(uint16_t buttons,float x,float y) {
    Pad pad{buttons};convert_to_n64_range(x,y,pad.x,pad.y);
    std::lock_guard lock(input_mutex);input=pad;
}
bool active(){return session || probe;}
void cancel(){cancelled=true;if(session)session->close();}
bool failed(){return error.load();}
uint8_t connected_mask(){return session?session->connected_mask():probe?15:1;}
void initialize(uint8_t* rdram){if(active())MEM_W(0,0x800C99B4)=session?session->seed():0x4D503232;}
}
extern "C" int mp2_netplay_active(){return netplay::game::active();}
extern "C" void mp2_netplay_latch(uint8_t* rdram,recomp_context*) {
    using namespace netplay;
    using namespace netplay::game;
    if(!active())return;
    uint64_t f=frame.load();
    try {
        auto checksum=state_checksum(rdram);
        Pads pads{};
        if(session)pads=session->exchange(sample(f,session->status().slot),checksum);
        else for(int p=0;p<4;++p)pads[p]=sample(f,p);
        if(trace){trace<<f<<' '<<std::hex<<checksum<<' '<<uint32_t(MEM_W(0,0x800C99B4))<<std::dec;
            for(auto p:pads)trace<<' '<<p.buttons<<' '<<int(p.x)<<' '<<int(p.y);
            trace<<'\n';trace.flush();}
        // Supply exactly one entry to the game's original input processor.
        // Its asynchronous acquisition routine is disabled by the entry hook.
        MEM_H(0,0x800D8180)=1; // queued samples
        MEM_H(0,0x800D8182)=0; // read cursor
        MEM_H(0,0x800D8184)=1; // write cursor
        for(int p=0;p<4;++p){uint32_t address=0x800D8040+6*p;
            MEM_H(0,address)=pads[p].buttons;
            MEM_B(2,address)=pads[p].x;MEM_B(3,address)=pads[p].y;
            MEM_B(4,address)=(connected_mask()&(1<<p))?0:8;
        }
        frame=f+1;
        if(f%300==0)fprintf(stderr,"[NETPLAY] simulation frame %llu, state %016llx\n",(unsigned long long)f,(unsigned long long)checksum);
        if((probe && frame>=probe)||(stop && frame>=stop))ultramodern::quit();
    } catch(const std::exception& e) {
        if(!cancelled){fprintf(stderr,"[NETPLAY] %s\n",e.what());error=true;}
        ultramodern::quit();
    }
}
// Only game-facing clock reads are virtualized. Audio/VI host pacing continues
// using the runtime clocks. An N64 Count tick is 1/46,875,000 second.
extern "C" void osGetTime_recomp(uint8_t* rdram,recomp_context* ctx) {
    if(!netplay::game::active()){mp2_original_osGetTime_recomp(rdram,ctx);return;}
    uint64_t ticks=netplay::game::frame.load()*781250ULL;
    ctx->r2=int32_t(ticks>>32);ctx->r3=int32_t(ticks);
}
extern "C" void osGetCount_recomp(uint8_t* rdram,recomp_context* ctx) {
    if(!netplay::game::active()){mp2_original_osGetCount_recomp(rdram,ctx);return;}
    ctx->r2=int32_t(netplay::game::frame.load()*781250ULL);
}

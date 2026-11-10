#include "settings.hpp"
#include <SDL.h>
#include <cassert>
#include <fstream>
#include <cmath>
#include <optional>
#include <iterator>
#include <atomic>
#include <future>
#include <algorithm>

namespace {
std::promise<std::string> lookup_result;
std::atomic<int> lookup_calls{};
}
namespace netplay {
std::string lookup_public_ipv4(){auto result=lookup_result.get_future();++lookup_calls;return result.get();}
}

int main(int argc,char**argv){
 assert(argc==2);
 assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)==0);
 settings::init(argv[1]);settings::toggle();assert(settings::visible());
 uint16_t b=123;float x=1,y=1;settings::update(b,x,y);assert(!b&&!x&&!y);
 SDL_Window*w=nullptr;for(int i=1;i<20;++i)if(auto* candidate=SDL_GetWindowFromID(i))w=candidate;assert(w);
 auto click=[&](int px,int py){SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(w);e.button.button=SDL_BUTTON_LEFT;e.button.x=px;e.button.y=py;settings::event(e);e.type=SDL_MOUSEBUTTONUP;settings::event(e);};
 auto key=[&](SDL_Scancode k,Uint16 mod=0){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(w);e.key.keysym.scancode=k;e.key.keysym.mod=mod;settings::event(e);};
 auto type=[&](const char* text){SDL_Event e{};e.type=SDL_TEXTINPUT;e.text.windowID=SDL_GetWindowID(w);SDL_strlcpy(e.text.text,text,sizeof(e.text.text));settings::event(e);};
 // Input and audio occupy separate pages; edits survive tab changes.
 click(250,240);key(SDL_SCANCODE_V);
 click(240,90);click(380,190);assert(std::abs(settings::gain(0)-.5f)<.01);
 click(240,390);assert(settings::gain(0)==0);click(240,390);assert(std::abs(settings::gain(0)-.5f)<.01);
 click(420,90);click(300,195);click(300,260);click(300,322);click(300,386);
 auto g=settings::graphics();assert(g.resolution==2&&g.aspect==1&&g.fullscreen&&g.filtering==2);
 // Reset affects only the selected page.
 click(240,90);click(40,690);assert(settings::gain(0)==1);assert(settings::graphics()==g);
 click(380,190);assert(std::abs(settings::gain(0)-.5f)<.01);
 std::optional<netplay::Options> request;
 // Default Steam room creation needs neither IP detection nor a valid TCP port.
 settings::set_netplay({.connect=[&](const auto& o){request=o;}});
 click(620,90);settings::update(b,x,y);assert(lookup_calls==0);
 click(40,264);assert(request&&request->transport==netplay::Transport::Steam);request.reset();
 // Switch to Direct TCP and retain its validation and address lookup behavior.
 click(40,164);click(40,264);assert(request&&request->transport==netplay::Transport::Direct);request.reset();
 settings::set_netplay({.connect=[&](const auto& o){request=o;}},{.detect_public_ip=false,.transport=netplay::Transport::Direct});
 click(620,90);click(650,215);key(SDL_SCANCODE_A,KMOD_CTRL);type("0");click(40,264);assert(!request);
 click(650,215);key(SDL_SCANCODE_A,KMOD_CTRL);type("17778");click(40,264);
 assert(request&&request->address=="127.0.0.1"&&request->port==17778&&request->code.empty());request.reset();
 click(40,468);assert(!request); // Empty invitation cannot launch.
 click(40,370);type("MP2-test-code");click(40,468);assert(request&&request->code=="MP2-test-code");
 // Public-IP work must not block the UI, replace an edit, or ignore explicit CLI addresses.
 auto tick=[&]{settings::update(b,x,y);};
 auto wait=[&](auto ready){auto end=SDL_GetTicks()+2000;while(!ready()){assert(SDL_GetTicks()<end);tick();SDL_Delay(1);}};
 auto setup=[&](netplay::Options initial=netplay::Options{.transport=netplay::Transport::Direct}){request.reset();settings::set_netplay({.connect=[&](const auto& o){request=o;}},initial);tick();};
 auto host_address=[&]{click(40,264);assert(request);return request->address;};
 auto expect_address=[&](const char* address){wait([&]{return host_address()==address;});};
 auto started=SDL_GetTicks();setup();assert(SDL_GetTicks()-started<500);wait([&]{return lookup_calls==1;});
 lookup_result.set_value("93.184.216.34");expect_address("93.184.216.34");
 lookup_result=std::promise<std::string>{};setup();wait([&]{return lookup_calls==2;});
 click(40,215);key(SDL_SCANCODE_A,KMOD_CTRL);type("192.168.1.42");
 lookup_result.set_value("93.184.216.35");
 // Once completion is consumed, retry starts a fresh request. The manual edit survives.
 std::promise<std::string> retry_result;
 // Reassign the mock promise only after the worker has obtained its future.
 lookup_result=std::move(retry_result);
 wait([&]{tick();click(250,264);return lookup_calls==3;});
 assert(host_address()=="192.168.1.42");
 lookup_result.set_value("93.184.216.36");expect_address("93.184.216.36");
 setup({.address="127.0.0.1",.detect_public_ip=false,.transport=netplay::Transport::Direct});for(int i=0;i<3;++i)tick();
 assert(lookup_calls==3&&host_address()=="127.0.0.1");
 setup({.address="my-host.example",.transport=netplay::Transport::Direct});for(int i=0;i<3;++i)tick();assert(lookup_calls==3&&host_address()=="my-host.example");
 setup({.code="MP2-test-code",.transport=netplay::Transport::Direct});for(int i=0;i<3;++i)tick();assert(lookup_calls==3);
 lookup_result=std::promise<std::string>{};setup();wait([&]{return lookup_calls==4;});
 lookup_result.set_value("");lookup_result=std::promise<std::string>{};
 wait([&]{tick();click(250,264);return lookup_calls==5;});assert(host_address()=="127.0.0.1");
 lookup_result.set_value("93.184.216.37");expect_address("93.184.216.37");
 // Steam friend picker pages by stable Steam ID, and incoming invites work
 // with settings closed during a running game. These callbacks do not send mail
 // or contact Steam; the real SDK filtering is checked by netplay_steam_test.
 netplay::SteamSocialStatus social{.available=true,.invite_ready=true};
 for(int i=0;i<10;++i)social.friends.push_back({"friend"+std::to_string(i),"Spacewar friend "+std::to_string(i)});
 netplay::Status host{.code="MP2S-host",.slot=0,.players=1,.connected=true};
 int refreshes=0,invites=0,accepts=0,dismisses=0;
 std::string invited,accepted;
 auto actions=[&]{return settings::NetplayActions{
     .connect=[&](const auto& o){request=o;},.status=[&]{return host;},.lobby=true,
     .steam_status=[&]{auto result=social;social.join_code.clear();return result;},
     .refresh_friends=[&]{++refreshes;},
     .invite_friend=[&](const auto& id){++invites;invited=id;},
     .accept_invite=[&](const auto& id){++accepts;accepted=id;},
     .dismiss_invite=[&](const auto& id){++dismisses;std::erase_if(social.invitations,[&](const auto& i){return i.id==id;});}
 };};
 auto social_tick=[&]{SDL_Delay(260);tick();};
 settings::set_netplay(actions());tick();
 click(260,314);assert(refreshes==1);click(620,224);assert(invites==1&&invited=="friend0");
 click(190,560);click(620,264);assert(invites==2&&invited=="friend9");
 click(40,602);assert(refreshes==2);click(650,134);
 // The picker is unavailable for guests, full/running rooms, and Direct TCP.
 auto unavailable=[&]{settings::set_netplay(actions());tick();click(260,314);click(620,224);assert(invites==2);};
 host.slot=1;unavailable();host.slot=0;host.players=4;unavailable();host.players=1;
 host.running=true;unavailable();host.running=false;host.code="MP2-direct";unavailable();host.code="MP2S-host";
 // Arrival surfaces the inbox even during play with settings closed.
 host.running=true;settings::set_netplay(actions());tick();settings::toggle();assert(!settings::visible());request.reset();
 social.invitations.push_back({"lobby1","Friend one"});social_tick();assert(settings::visible());
 click(510,224);assert(accepts==1&&accepted=="lobby1"&&!request);
 click(510,224);assert(accepts==1); // no duplicate requests while joining
 social.join_code="MP2S-invited-room";social_tick();assert(request&&request->code=="MP2S-invited-room"&&dismisses==1);
 request.reset();social.invitations.push_back({"lobby2","Friend two"});social_tick();
 click(650,224);assert(dismisses==2&&!request&&accepts==1);social_tick();
 social.invitations.push_back({"lobby3","Friend three"});social_tick();click(510,224);assert(accepts==2);
 social.join_code="unrelated-spacewar-game";social_tick();assert(!request);
 // A repeated failure must release the pending state on every retry.
 for(int i=0;i<2;++i){click(510,224);social.message="Invite expired";++social.message_revision;social_tick();}
 assert(accepts==4&&!request);
 settings::set_netplay({});
 // Focus is cleared on tab switches and F1 isn't captured as a binding.
 click(40,90);click(250,240);key(SDL_SCANCODE_ESCAPE);assert(settings::visible());
 SDL_Event quit{};quit.type=SDL_QUIT;assert(!settings::event(quit));
 settings::toggle();settings::shutdown();SDL_Quit();
 auto path=std::filesystem::path(argv[1])/"settings.cfg";
 std::ifstream in(path);int version;float a,c,d;int mute,dz,k,pad;assert(in>>version>>a>>c>>d>>mute>>dz>>k>>pad);assert(version==2&&k==SDL_SCANCODE_V&&a==.5f);
 assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)==0);settings::init(argv[1]);
 assert(std::abs(settings::gain(1)-.5f)<.01);assert(settings::graphics()==g);settings::shutdown();SDL_Quit();
 // Version 1 files keep the user's bindings/audio and use graphics defaults.
 std::ifstream saved(path);std::string legacy((std::istreambuf_iterator<char>(saved)),{});saved.close();legacy[0]='1';
 std::ofstream(path)<<legacy;
 assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)==0);settings::init(argv[1]);
 assert(std::abs(settings::gain(1)-.5f)<.01);assert(settings::graphics()==settings::Graphics{});
 settings::shutdown();SDL_Quit();
}

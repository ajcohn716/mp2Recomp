#include "settings.hpp"
#include <SDL.h>
#include <cassert>
#include <fstream>
#include <cmath>
#include <optional>
#include <iterator>

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
 settings::set_netplay({.connect=[&](const auto& o){request=o;}});
 click(620,90);click(650,215);key(SDL_SCANCODE_A,KMOD_CTRL);type("0");click(40,264);assert(!request);
 click(650,215);key(SDL_SCANCODE_A,KMOD_CTRL);type("17778");click(40,264);
 assert(request&&request->address=="127.0.0.1"&&request->port==17778&&request->code.empty());request.reset();
 click(40,468);assert(!request); // Empty invitation cannot launch.
 click(40,370);type("MP2-test-code");click(40,468);assert(request&&request->code=="MP2-test-code");
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

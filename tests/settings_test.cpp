#include "settings.hpp"
#include <SDL.h>
#include <cassert>
#include <fstream>
#include <cmath>
int main(int argc,char**argv){
 assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)==0);
 settings::init(argv[1]);settings::toggle();assert(settings::visible());
 uint16_t b=123;float x=1,y=1;settings::update(b,x,y);assert(!b&&!x&&!y);
 SDL_Window*w=nullptr;for(int i=1;i<20;++i)if(auto* candidate=SDL_GetWindowFromID(i))w=candidate;assert(w);
 auto click=[&](int px,int py){SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(w);e.button.button=SDL_BUTTON_LEFT;e.button.x=px;e.button.y=py;settings::event(e);e.type=SDL_MOUSEBUTTONUP;settings::event(e);};
 click(370,120);assert(std::abs(settings::gain(0)-.5f)<.01);
 click(40,220);assert(settings::gain(0)==0);click(40,220);assert(std::abs(settings::gain(0)-.5f)<.01);
 click(250,370);SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.scancode=SDL_SCANCODE_V;settings::event(e);
 settings::toggle();settings::shutdown();SDL_Quit();
 std::ifstream in(std::string(argv[1])+"/settings.cfg");int version;float a,c,d;int mute,dz,key,pad;assert(in>>version>>a>>c>>d>>mute>>dz>>key>>pad);assert(version==1&&key==SDL_SCANCODE_V&&a==.5f);
 SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER);settings::init(argv[1]);assert(std::abs(settings::gain(1)-.5f)<.01);settings::shutdown();SDL_Quit();
}

#include "netplay.hpp"
#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <stdexcept>

namespace netplay {
std::unique_ptr<Session> lobby(const Options& initial,const std::vector<uint8_t>& host_save) {
    if(TTF_Init()!=0)throw std::runtime_error(TTF_GetError());
    TTF_Font* font{};
    const char* fonts[]={"C:/Windows/Fonts/segoeui.ttf","C:/Windows/Fonts/arial.ttf","/usr/share/fonts/noto/NotoSans-Regular.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/TTF/DejaVuSans.ttf"};
    for(auto path:fonts)if((font=TTF_OpenFont(path,18)))break;
    if(!font){TTF_Quit();throw std::runtime_error("Install Noto Sans or DejaVu Sans to show the multiplayer lobby");}
    auto* window=SDL_CreateWindow("Mario Party 2 | Multiplayer",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,760,580,SDL_WINDOW_ALLOW_HIGHDPI);
    auto* renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE):nullptr;
    if(!renderer){if(window)SDL_DestroyWindow(window);TTF_CloseFont(font);TTF_Quit();throw std::runtime_error(SDL_GetError());}
    SDL_RenderSetLogicalSize(renderer,760,580);
    std::unique_ptr<Session> session;
    Options options=initial;
    std::string port=std::to_string(options.port),error;
    int field=options.code.empty()?0:2;
    bool done=false,started=false;
    auto rect=[&](SDL_Rect r,SDL_Color c){SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);SDL_RenderFillRect(renderer,&r);};
    auto text=[&](int x,int y,const std::string& s,SDL_Color c=SDL_Color{224,233,246,255}){
        if(s.empty())return;
        auto* surface=TTF_RenderUTF8_Blended(font,s.c_str(),c);if(!surface)return;
        auto* texture=SDL_CreateTextureFromSurface(renderer,surface);
        SDL_Rect dest{x,y,surface->w,surface->h};SDL_RenderCopy(renderer,texture,nullptr,&dest);SDL_DestroyTexture(texture);SDL_FreeSurface(surface);
    };
    auto button=[&](int x,int y,int width,const char* label){rect({x,y,width,40},{43,73,112,255});text(x+14,y+8,label);};
    SDL_StartTextInput();
    while(!done) {
        Status status=session?session->status():Status{};
        if(status.running){started=true;break;}
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT||(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_CLOSE))done=true;
            if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_ESCAPE)done=true;
            if(!session&&e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_TAB)field=(field+1)%3;
            auto& target=field==0?options.address:field==1?port:options.code;
            auto append=[&](std::string value){if(target.size()+value.size()<512)target+=value;};
            if(!session&&e.type==SDL_TEXTINPUT)append(e.text.text);
            if(!session&&e.type==SDL_KEYDOWN) {
                if(e.key.keysym.sym==SDLK_BACKSPACE&&!target.empty())target.pop_back();
                if(e.key.keysym.sym==SDLK_a&&(e.key.keysym.mod&KMOD_CTRL))target.clear();
                if(e.key.keysym.sym==SDLK_v&&(e.key.keysym.mod&KMOD_CTRL)){char* clip=SDL_GetClipboardText();if(clip){append(clip);SDL_free(clip);}}
            }
            if(e.type!=SDL_MOUSEBUTTONDOWN||e.button.button!=SDL_BUTTON_LEFT)continue;
            float mx,my;SDL_RenderWindowToLogical(renderer,e.button.x,e.button.y,&mx,&my);int x=int(mx),y=int(my);
            if(!session){
                if(y>=118&&y<158)field=x>=580?1:0;
                if(y>=200&&y<296)field=2;
                if(y>=330&&y<370) {
                    try {
                        if(x>=28&&x<218){
                            size_t used{};auto number=std::stoul(port,&used);
                            if(used!=port.size()||!number||number>65535)throw std::runtime_error("Port must be 1..65535");
                            auto create=options;create.code.clear();create.port=uint16_t(number);session=std::make_unique<Session>(create);error.clear();
                        }else if(x>=236&&x<426){if(options.code.empty())error="Paste the full connection code first.";else{session=std::make_unique<Session>(options);error.clear();}}
                    }catch(const std::exception& ex){error=ex.what();}
                }
            }else{
                if(y>=222&&y<262&&x>=28&&x<218){SDL_SetClipboardText(status.code.c_str());error="Connection code copied";}
                if(y>=462&&y<502){
                    if(x>=28&&x<218&&status.slot==0&&!status.failed){
                        if(status.players>=2)session->start(host_save);else error="Wait for a friend to connect.";
                    }
                    if(x>=236&&x<426){session.reset();error.clear();}
                }
            }
        }
        rect({0,0,760,580},{18,26,40,255});
        text(28,24,"Mario Party 2",{255,255,255,255});text(28,57,"Multiplayer  /  Direct connections, up to four players");
        if(!session){
            text(28,91,"Host's public IP or DNS name");text(580,91,"TCP port");
            rect({28,118,536,40},field==0?SDL_Color{47,66,92,255}:SDL_Color{30,43,62,255});text(38,126,options.address);
            rect({580,118,140,40},field==1?SDL_Color{47,66,92,255}:SDL_Color{30,43,62,255});text(590,126,port);
            text(28,173,"Joining? Paste your friend's connection code here");
            rect({28,200,692,96},field==2?SDL_Color{47,66,92,255}:SDL_Color{30,43,62,255});
            for(size_t i=0;i<options.code.size()&&i<180;i+=60)text(38,210+int(i/60)*26,options.code.substr(i,60));
            button(28,330,190,"Create room");button(236,330,190,"Join room");
            text(28,402,"Host: forward the TCP port above to this computer.");
            text(28,434,"Friends only need the code. No shared server or relay.");
            text(28,474,"Tab changes fields. Ctrl+A clears. Ctrl+V pastes.");
        }else{
            text(28,91,"Share this connection code privately");rect({28,118,692,90},{30,43,62,255});
            for(size_t i=0;i<status.code.size()&&i<180;i+=60)text(38,126+int(i/60)*26,status.code.substr(i,60));
            button(28,222,190,"Copy code");
            for(int p=0;p<4;++p){
                std::string line="Player "+std::to_string(p+1)+(status.slot==p?" (you)":"");
                line+="   ";line+=status.ready[p]?(p==status.slot?"Ready":"Connected"):"Waiting";
                text(28,286+p*36,line,status.ready[p]?SDL_Color{132,218,179,255}:SDL_Color{169,183,204,255});
            }
            if(status.slot==0)button(28,462,190,"Start game");else text(28,470,"Waiting for host");
            button(236,462,190,"Leave room");
        }
        std::string message=error.empty()?status.message:error;
        for(size_t i=0;i<message.size()&&i<144;i+=72)text(28,518+int(i/72)*25,message.substr(i,72),{245,199,133,255});
        SDL_RenderPresent(renderer);SDL_Delay(16);
    }
    SDL_StopTextInput();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);TTF_CloseFont(font);TTF_Quit();
    return started?std::move(session):nullptr;
}
}

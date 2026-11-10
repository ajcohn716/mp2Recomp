#include "settings.hpp"
#include <SDL_ttf.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace settings {
namespace {
struct Binding { const char* name; uint16_t mask; int key; int button; };
std::array<Binding, 14> bindings{{
    {"A",0x8000,SDL_SCANCODE_X,SDL_CONTROLLER_BUTTON_A},
    {"B",0x4000,SDL_SCANCODE_C,SDL_CONTROLLER_BUTTON_X},
    {"Z",0x2000,SDL_SCANCODE_Z,SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {"Start",0x1000,SDL_SCANCODE_RETURN,SDL_CONTROLLER_BUTTON_START},
    {"L",0x20,SDL_SCANCODE_Q,SDL_CONTROLLER_BUTTON_BACK},
    {"R",0x10,SDL_SCANCODE_E,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {"C up",8,SDL_SCANCODE_I,SDL_CONTROLLER_BUTTON_Y},
    {"C down",4,SDL_SCANCODE_K,SDL_CONTROLLER_BUTTON_B},
    {"C left",2,SDL_SCANCODE_J,SDL_CONTROLLER_BUTTON_LEFTSTICK},
    {"C right",1,SDL_SCANCODE_L,SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    {"D-pad up",0x800,SDL_SCANCODE_W,SDL_CONTROLLER_BUTTON_DPAD_UP},
    {"D-pad down",0x400,SDL_SCANCODE_S,SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    {"D-pad left",0x200,SDL_SCANCODE_A,SDL_CONTROLLER_BUTTON_DPAD_LEFT},
    {"D-pad right",0x100,SDL_SCANCODE_D,SDL_CONTROLLER_BUTTON_DPAD_RIGHT}
}};
const auto defaults = bindings;
SDL_Window* window{};
SDL_Renderer* renderer{};
TTF_Font* font{};
SDL_GameController* controller{};
std::vector<int> devices;
int selected{-1};
std::string preferred;
std::filesystem::path file;
std::array<std::atomic<float>,3> levels{1.f,1.f,1.f};
std::atomic<bool> muted{};
bool open{}, dirty{};
int deadzone{15}, capture{-1}, drag{-1};
bool capture_pad{};
std::string status;
void save() {
    auto temp=file; temp += ".tmp";
    std::ofstream out(temp);
    out << "1\n" << levels[0] << ' ' << levels[1] << ' ' << levels[2] << ' ' << muted << ' ' << deadzone << '\n';
    for (auto& b:bindings) out << b.key << ' ' << b.button << '\n';
    out << preferred << '\n'; out.close();
    std::error_code ec;
    if (out.fail()) status="Could not save settings";
    else { std::filesystem::rename(temp,file,ec); status=ec ? "Could not save settings" : "Settings saved"; }
    dirty=true;
}
std::string identity(int index) {
    char guid[33]; SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index),guid,sizeof(guid)); return guid;
}
void select(int device) {
    if(controller) SDL_GameControllerClose(controller);
    controller=nullptr; selected=device;
    if(device>=0) controller=SDL_GameControllerOpen(device);
    if(controller) preferred=identity(device);
    else {selected=-1; preferred="keyboard";}
    dirty=true;
}
void refresh() {
    devices.clear();
    for(int i=0;i<SDL_NumJoysticks();++i) if(SDL_IsGameController(i)) devices.push_back(i);
    int choice=-1;
    if(preferred!="keyboard") for(int i:devices) if(preferred.empty() || preferred==identity(i)) {choice=i;break;}
    std::string wanted=preferred;
    select(choice);
    if(choice<0 && wanted!="keyboard") preferred=wanted;
}
void text(int x,int y,const std::string& s,SDL_Color color={223,230,242,255}) {
    SDL_Surface* surface=TTF_RenderUTF8_Blended(font,s.c_str(),color);
    if(!surface)return;
    SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer,surface);
    SDL_Rect dest{x,y,surface->w,surface->h}; SDL_RenderCopy(renderer,texture,nullptr,&dest);
    SDL_DestroyTexture(texture); SDL_FreeSurface(surface);
}
void box(SDL_Rect r,SDL_Color c) {SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);SDL_RenderFillRect(renderer,&r);}
void button(int x,int y,int width,const std::string& s,bool active=false) {
    box({x,y,width,26},active?SDL_Color{54,96,151,255}:SDL_Color{42,53,73,255});text(x+8,y+2,s);
}
void draw() {
    SDL_SetRenderDrawColor(renderer,20,27,40,255);SDL_RenderClear(renderer);
    text(24,14,"Settings",{255,255,255,255});
    text(24,44,"F1 opens settings. Escape closes. Changes save automatically.");
    text(24,82,"Audio");
    const char* labels[]={"Master volume","Left channel","Right channel","Stick deadzone"};
    for(int i=0;i<4;++i) {
        int y=i<3?112+i*32:302;
        int value=i<3?int(std::lround(levels[i].load()*100)):deadzone;
        text(24,y,labels[i]);box({220,y+8,300,6},{53,66,88,255});
        box({220,y+8,value*3,6},{85,159,242,255});
        box({216+value*3,y+3,8,16},{217,234,255,255});text(540,y,std::to_string(value)+"%");
    }
    button(24,210,130,muted?"Unmute":"Mute",muted);
    text(24,254,"Controller (port 1)");
    std::string device=controller?SDL_GameControllerName(controller):"Keyboard only";
    button(220,250,420,device+"  >");
    text(24,340,"Input");text(220,340,"Keyboard");text(430,340,"Controller button");
    for(size_t i=0;i<bindings.size();++i) {
        int y=368+int(i)*29;auto& b=bindings[i];
        text(24,y,b.name);
        button(220,y,190,capture==int(i)&&!capture_pad?"Press a key...":SDL_GetScancodeName(SDL_Scancode(b.key)),capture==int(i)&&!capture_pad);
        const char* name=SDL_GameControllerGetStringForButton(SDL_GameControllerButton(b.button));
        button(430,y,210,capture==int(i)&&capture_pad?"Press a button...":(name?name:"Unassigned"),capture==int(i)&&capture_pad);
    }
    text(24,784,"Analog stick: left stick / arrow keys. Escape cancels rebinding.");
    button(24,820,160,"Reset defaults");button(530,820,110,"Done");
    text(24,858,status,{155,193,234,255});SDL_RenderPresent(renderer);dirty=false;
}
void slider(int which,int x) {
    float value=std::clamp((x-220)/300.f,0.f,1.f);
    if(which<3)levels[which]=value;else deadzone=std::min(95,int(value*100));
    dirty=true;
}
}
void init(const std::filesystem::path& directory) {
    file=directory/"settings.cfg";
    std::ifstream in(file); int version;
    if(in>>version && version==1) {
        float a,b,c;int mute,dz;
        if(in>>a>>b>>c>>mute>>dz && std::isfinite(a)&&std::isfinite(b)&&std::isfinite(c)) {
            levels[0]=std::clamp(a,0.f,1.f);levels[1]=std::clamp(b,0.f,1.f);levels[2]=std::clamp(c,0.f,1.f);
            muted=mute!=0;deadzone=std::clamp(dz,0,95);
            for(auto& binding:bindings) {int key,pad;if(!(in>>key>>pad))break;
                if(key>SDL_SCANCODE_UNKNOWN&&key<SDL_NUM_SCANCODES&&key!=SDL_SCANCODE_F1&&key!=SDL_SCANCODE_ESCAPE)binding.key=key;
                if(pad>=0&&pad<SDL_CONTROLLER_BUTTON_MAX)binding.button=pad;
            }
            in.ignore(4096,'\n');std::getline(in,preferred);
        }
    }
    refresh();
}
bool visible(){return open;}
float gain(int channel){return muted.load()?0.f:levels[0].load()*levels[channel+1].load();}
void toggle() {
    if(open){open=false;capture=-1;SDL_HideWindow(window);return;}
    if(!window) {
        if(TTF_Init()!=0){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Settings",TTF_GetError(),nullptr);return;}
        const char* fonts[]={"C:/Windows/Fonts/segoeui.ttf","C:/Windows/Fonts/arial.ttf","/usr/share/fonts/noto/NotoSans-Regular.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/TTF/DejaVuSans.ttf"};
        for(auto path:fonts)if((font=TTF_OpenFont(path,16)))break;
        if(!font){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Settings","Install Noto Sans or DejaVu Sans to display settings.",nullptr);return;}
        window=SDL_CreateWindow("Mario Party 2 Settings",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,670,900,SDL_WINDOW_ALLOW_HIGHDPI);
        if(window)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if(!renderer){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Settings",SDL_GetError(),nullptr);return;}
        SDL_RenderSetLogicalSize(renderer,670,900);
    }
    open=true;dirty=true;SDL_ShowWindow(window);SDL_RaiseWindow(window);
}
bool event(const SDL_Event& e) {
    if(e.type==SDL_CONTROLLERDEVICEADDED||e.type==SDL_CONTROLLERDEVICEREMOVED){refresh();dirty=true;}
    if(e.type==SDL_KEYDOWN&&!e.key.repeat&&e.key.keysym.scancode==SDL_SCANCODE_F1){toggle();return true;}
    if(!open)return false;
    if(e.type==SDL_WINDOWEVENT && e.window.windowID==SDL_GetWindowID(window)) {
        if(e.window.event==SDL_WINDOWEVENT_CLOSE)toggle();else dirty=true;
    }
    if(e.type==SDL_KEYDOWN&&!e.key.repeat) {
        if(e.key.keysym.scancode==SDL_SCANCODE_ESCAPE){if(capture>=0){capture=-1;dirty=true;}else toggle();}
        else if(capture>=0&&!capture_pad){bindings[capture].key=e.key.keysym.scancode;capture=-1;save();}
    }
    if(e.type==SDL_CONTROLLERBUTTONDOWN&&capture>=0&&capture_pad&&controller&&
       e.cbutton.which==SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) {
        bindings[capture].button=e.cbutton.button;capture=-1;save();
    }
    if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.windowID==SDL_GetWindowID(window)&&e.button.button==SDL_BUTTON_LEFT) {
        float fx,fy;SDL_RenderWindowToLogical(renderer,e.button.x,e.button.y,&fx,&fy);int x=fx,y=fy;
        for(int i=0;i<4;++i){int top=i<3?112+i*32:302;if(x>=210&&x<=530&&y>=top&&y<top+24){drag=i;slider(i,x);}}
        if(y>=210&&y<236&&x>=24&&x<154){muted=!muted;save();}
        if(y>=250&&y<276&&x>=220&&x<640){
            auto it=std::find(devices.begin(),devices.end(),selected);
            int next=selected<0?(devices.empty()?-1:devices.front()):((it!=devices.end()&&++it!=devices.end())?*it:-1);
            select(next);save();
        }
        if(y>=368&&y<774){int row=(y-368)/29;if(row<int(bindings.size())&&x>=220&&x<640){capture=row;capture_pad=x>=430;dirty=true;}}
        if(y>=820&&y<846){if(x>=530&&x<640)toggle();else if(x>=24&&x<184){bindings=defaults;levels[0]=levels[1]=levels[2]=1.f;muted=false;deadzone=15;capture=-1;save();}}
    }
    if(e.type==SDL_MOUSEMOTION&&drag>=0&&e.motion.windowID==SDL_GetWindowID(window)){
        float x,y;SDL_RenderWindowToLogical(renderer,e.motion.x,e.motion.y,&x,&y);slider(drag,x);
    }
    if(e.type==SDL_MOUSEBUTTONUP&&drag>=0){drag=-1;save();}
    return true;
}
void update(uint16_t& b,float& x,float& y) {
    b=0;x=y=0;
    if(open){if(dirty)draw();return;}
    if(SDL_GetKeyboardFocus()==nullptr)return;
    auto keys=SDL_GetKeyboardState(nullptr);
    for(auto& binding:bindings)if(keys[binding.key]||(controller&&SDL_GameControllerGetButton(controller,SDL_GameControllerButton(binding.button))))b|=binding.mask;
    x=float(keys[SDL_SCANCODE_RIGHT])-float(keys[SDL_SCANCODE_LEFT]);
    y=float(keys[SDL_SCANCODE_UP])-float(keys[SDL_SCANCODE_DOWN]);
    if(controller){
        float cx=SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTX)/32768.f;
        float cy=-SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTY)/32768.f;
        float length=std::sqrt(cx*cx+cy*cy), threshold=deadzone/100.f;
        if(length>threshold){float scale=std::min(1.f,(length-threshold)/(1-threshold))/length;x=std::clamp(x+cx*scale,-1.f,1.f);y=std::clamp(y+cy*scale,-1.f,1.f);}
    }
}
void shutdown(){if(controller)SDL_GameControllerClose(controller);if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);if(font)TTF_CloseFont(font);if(TTF_WasInit())TTF_Quit();controller=nullptr;renderer=nullptr;window=nullptr;font=nullptr;open=false;}
}

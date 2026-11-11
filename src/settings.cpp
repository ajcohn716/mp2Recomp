#include "settings.hpp"
#include "public_ip.hpp"
#include <SDL_ttf.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <stdexcept>
#include <future>

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
SDL_Window* game_window{};
SDL_Renderer* renderer{};
TTF_Font* font{};
std::array<SDL_GameController*, Inputs{}.size()> controllers{};
std::vector<int> devices;
std::string preferred;
std::filesystem::path file;
std::array<std::atomic<float>,3> levels{1.f,1.f,1.f};
std::atomic<bool> muted{};
bool open{}, dirty{};
int deadzone{15}, capture{-1}, drag{-1};
bool capture_pad{};
std::string status;
Page page{Page::Input};
std::atomic<unsigned> graphics_bits{1u | (1u << 5)};
NetplayActions network_actions;
netplay::Options network;
std::string port{"7777"};
int field{-1};
netplay::Status room;
netplay::SteamSocialStatus steam_social;
enum class NetworkPanel { Room, Friends, Invites };
NetworkPanel network_panel{NetworkPanel::Room};
size_t social_page{};
Uint32 last_social_poll{};
std::string social_message, accepting_invite;
uint64_t social_message_revision{};
bool can_invite() {
    return room.slot==0&&room.connected&&!room.running&&!room.failed&&room.players<4&&room.code.starts_with("MP2S-");
}

std::future<std::string> public_ip;
unsigned address_revision{}, lookup_revision{};
bool lookup_attempted{};
std::string lookup_status;
void detect_public_ip() {
    if(public_ip.valid())return;
    lookup_attempted=true;lookup_revision=address_revision;
    try {
        public_ip=std::async(std::launch::async,netplay::lookup_public_ipv4);
        lookup_status="Looking up public IPv4...";
    } catch(...) {lookup_status="IP lookup unavailable. Enter your address manually.";}
    dirty=true;
}
constexpr int width=800, height=740;
void text_input() {
    if(open && page==Page::Netplay && field>=0)SDL_StartTextInput();else SDL_StopTextInput();
}
void change_page(Page next) {
    drag=-1;
    capture=-1;field=-1;page=next;dirty=true;text_input();
}
void set_graphics(Graphics g) {
    graphics_bits=unsigned(g.resolution)|(unsigned(g.aspect)<<3)|(unsigned(g.filtering)<<5)|(unsigned(g.fullscreen)<<7);
}

void save() {
    auto temp=file; temp += ".tmp";
    std::ofstream out(temp);
    out << "2\n" << levels[0] << ' ' << levels[1] << ' ' << levels[2] << ' ' << muted << ' ' << deadzone << '\n';
    for (auto& b:bindings) out << b.key << ' ' << b.button << '\n';
    out << preferred << '\n';
    auto g=graphics();
    out << g.resolution << ' ' << g.aspect << ' ' << g.filtering << ' ' << g.fullscreen << '\n';
    out.close();
    std::error_code ec;
    if (out.fail()) status="Could not save settings";
    else { std::filesystem::rename(temp,file,ec); status=ec ? "Could not save settings" : "Settings saved"; }
    dirty=true;
}
std::string identity(int index) {
    char guid[33]; SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index),guid,sizeof(guid)); return guid;
}
SDL_JoystickID instance_id(SDL_GameController* controller) {
    return controller ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) : -1;
}
auto assigned_port(SDL_JoystickID id) {
    return std::find_if(controllers.begin(), controllers.end(), [id](auto* controller) {
        return controller && instance_id(controller) == id;
    });
}
void refresh() {
    for (auto& controller : controllers) {
        if (controller && !SDL_GameControllerGetAttached(controller)) {
            SDL_GameControllerClose(controller);
            controller = nullptr;
        }
    }

    devices.clear();
    int count = SDL_NumJoysticks();
    for (int i = 0; i < count; ++i) {
        if (SDL_IsGameController(i)) devices.push_back(i);
    }

    auto unassigned = [](int device) {
        return assigned_port(SDL_JoystickGetDeviceInstanceID(device)) == controllers.end();
    };
    auto& primary = controllers.front();
    if (!primary && preferred != "keyboard") {
        for (int device : devices) {
            if (unassigned(device) && (preferred.empty() || preferred == identity(device))) {
                primary = SDL_GameControllerOpen(device);
                if (primary) {
                    preferred = identity(device);
                    break;
                }
            }
        }
    }
    // Keep attached controllers on their ports even when SDL device indices change.
    for (int device : devices) {
        auto vacant = std::find(controllers.begin() + 1, controllers.end(), nullptr);
        if (vacant == controllers.end()) break;
        if (unassigned(device)) *vacant = SDL_GameControllerOpen(device);
    }
    dirty = true;
}
void cycle_primary() {
    auto& primary = controllers.front();
    auto current = std::find_if(devices.begin(), devices.end(), [&](int device) {
        return primary && SDL_JoystickGetDeviceInstanceID(device) == instance_id(primary);
    });
    auto next = current == devices.end() ? devices.begin() : std::next(current);
    if (next == devices.end()) {
        if (primary) SDL_GameControllerClose(primary);
        primary = nullptr;
        preferred = "keyboard";
    } else {
        auto assigned = assigned_port(SDL_JoystickGetDeviceInstanceID(*next));
        if (assigned != controllers.end()) {
            std::swap(primary, *assigned);
        } else {
            auto* replacement = SDL_GameControllerOpen(*next);
            if (!replacement) {
                status = SDL_GetError();
                dirty = true;
                return;
            }
            if (primary) SDL_GameControllerClose(primary);
            primary = replacement;
        }
        preferred = identity(*next);
    }
    capture = -1;
    refresh();
    save();
}
std::string controller_name(SDL_GameController* controller, const char* fallback) {
    if (controller) {
        if (const char* name = SDL_GameControllerName(controller)) return name;
        return "Gamepad";
    }
    return fallback;
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
bool hit(int x,int y,int left,int top,int w,int h=30) {return x>=left&&x<left+w&&y>=top&&y<top+h;}
bool room_active(){return room.slot>=0||room.connected||room.failed||!room.message.empty();}
void clipped_text(int x,int y,int w,const std::string& value) {
    SDL_Rect clip{x,y,w,26};SDL_RenderSetClipRect(renderer,&clip);
    text(x,y,value);SDL_RenderSetClipRect(renderer,nullptr);
}
void field_box(int id,int x,int y,int w,int h,const std::string& value) {
    box({x,y,w,h},field==id?SDL_Color{47,66,92,255}:SDL_Color{30,43,62,255});
    if(id==2)for(size_t i=0;i<value.size()&&i<210;i+=70)text(x+10,y+6+int(i/70)*24,value.substr(i,70));
    else clipped_text(x+10,y+4,w-20,value);
}
void draw() {
    SDL_SetRenderDrawColor(renderer,20,27,40,255);SDL_RenderClear(renderer);
    text(24,14,"Mario Party 2  /  Settings",{255,255,255,255});
    text(24,43,"F1 / Escape closes. Changes save automatically.");
    const char* tabs[]={"Input","Audio","Graphics","Netplay"};
    for(int i=0;i<4;++i)button(24+i*190,80,182,tabs[i],int(page)==i);
    if(page==Page::Input) {
        text(24,126,"Player 1 controller");
        box({230,122,540,26},{42,53,73,255});
        clipped_text(238,124,524,controller_name(controllers.front(),"Keyboard only")+"  >");
        text(24,163,"Stick deadzone");
        box({230,173,300,6},{53,66,88,255});box({230,173,deadzone*3,6},{85,159,242,255});
        box({226+deadzone*3,168,8,16},{217,234,255,255});text(550,163,std::to_string(deadzone)+"%");
        text(24,206,"N64 input");text(230,206,"Keyboard");text(490,206,"Controller button");
        for(size_t i=0;i<bindings.size();++i) {
            int y=236+int(i)*29;auto& b=bindings[i];text(24,y,b.name);
            button(230,y,240,capture==int(i)&&!capture_pad?"Press a key...":SDL_GetScancodeName(SDL_Scancode(b.key)),capture==int(i)&&!capture_pad);
            const char* name=SDL_GameControllerGetStringForButton(SDL_GameControllerButton(b.button));
            button(490,y,280,capture==int(i)&&capture_pad?"Press a button...":(name?name:"Unassigned"),capture==int(i)&&capture_pad);
        }
        if (!network_actions.lobby && !room.running) {
            for (size_t port = 1; port < controllers.size(); ++port) {
                clipped_text(24 + int(port - 1) * 250, 648, 240,
                    "P" + std::to_string(port + 1) + "  " + controller_name(controllers[port], "Disconnected"));
            }
        }
    } else if(page==Page::Audio) {
        text(24,130,"Audio levels");
        const char* labels[]={"Master volume","Left channel","Right channel"};
        for(int i=0;i<3;++i) {
            int y=184+i*60,value=int(std::lround(levels[i].load()*100));
            text(24,y,labels[i]);box({230,y+8,300,6},{53,66,88,255});
            box({230,y+8,value*3,6},{85,159,242,255});
            box({226+value*3,y+3,8,16},{217,234,255,255});text(550,y,std::to_string(value)+"%");
        }
        button(230,380,180,muted?"Unmute":"Mute",muted);
    } else if(page==Page::Graphics) {
        auto g=graphics();
        const char* resolutions[]={"Fit window","Original (1x)","2x native","3x native","4x native"};
        const char* aspects[]={"Original (4:3)","Widescreen (16:9)","Fit window"};
        const char* filters[]={"Nearest","Linear","Pixel scaling"};
        text(24,130,"Graphics changes apply immediately.");
        text(24,190,"Render resolution");button(280,186,380,std::string(resolutions[g.resolution])+"  >");
        text(24,254,"Aspect ratio");button(280,250,380,std::string(aspects[g.aspect])+"  >");
        text(24,318,"Display mode");button(280,314,380,g.fullscreen?"Fullscreen  >":"Windowed  >");
        text(24,382,"Output filtering");button(280,378,380,std::string(filters[g.filtering])+"  >");
        text(24,460,"Higher resolutions use more GPU power.");
        text(24,490,"Widescreen expands the view; some game UI may stay 4:3.");
    } else {
        bool connected=room_active();
        if(network_panel!=NetworkPanel::Room) {
            bool friends=network_panel==NetworkPanel::Friends;
            text(24,130,friends?"Invite Steam friends":"Steam invitations");
            button(620,124,150,"Back to room");
            text(24,166,friends?"Only friends currently playing Spacewar (App ID 480) are shown.":"Accept joins a new room and ends your current game or room.");
            size_t count=friends?steam_social.friends.size():steam_social.invitations.size();
            if(social_page*8>=count)social_page=0;
            if(!count)text(24,218,friends?"No friends currently playing Spacewar.":"No pending Steam invitations.");
            for(size_t i=social_page*8;i<count&&i<(social_page+1)*8;++i) {
                int y=214+int(i%8)*40;
                if(friends) {
                    clipped_text(24,y,550,steam_social.friends[i].name);
                    button(600,y,170,"Invite");
                } else {
                    clipped_text(24,y,450,steam_social.invitations[i].name);
                    button(490,y,130,accepting_invite==steam_social.invitations[i].id?"Joining...":"Accept");
                    button(640,y,130,"Dismiss");
                }
            }
            if(count>8) {
                button(24,550,130,"Previous");button(170,550,130,"Next");
                text(330,552,"Page "+std::to_string(social_page+1)+" / "+std::to_string((count+7)/8));
            }
            if(friends) {
                button(24,592,190,"Refresh friends");
                text(24,630,steam_social.invite_ready?"Choose a friend to send a Steam invite.":"Steam invite lobby is not ready. Refresh to retry.");
            } else button(24,592,190,"Refresh Steam");
            clipped_text(24,654,746,steam_social.message);
        } else if(!connected) {
            text(24,130,network_actions.lobby?"Create a room or join a friend before starting the game.":"Creating or joining restarts the game into the room lobby.");
            bool steam=network.transport==netplay::Transport::Steam;
            button(24,154,350,steam?"Transport: Steam (App ID 480)  >":"Transport: Direct TCP  >");
            if(steam) {
                text(24,204,"Sign into Steam. Friends join using their own Steam accounts.");
                text(24,230,"Create a room, then invite friends or share its code.");
            } else {
                text(24,182,"Host's public IP / DNS name");text(640,182,"TCP port");
                field_box(0,24,204,592,34,network.address);field_box(1,640,204,130,34,port);
            }
            button(24,254,190,"Create room");
            if(!steam){button(236,254,190,public_ip.valid()?"Looking up IP...":"Detect public IP");
                clipped_text(24,286,746,lookup_status);}
            text(24,320,"Join with a connection code");field_box(2,24,352,746,90,network.code);
            button(24,458,190,"Join room");
            text(24,522,steam?"No IP lookup or router port forwarding is needed.":"Host: forward the TCP port above to this computer.");
            text(24,552,steam?"Share the MP2S code privately, then start when everyone joins.":"Use your LAN IP for local play. Friends only need the code.");
            text(24,582,"Tab changes fields. Ctrl+A clears. Ctrl+V pastes.");
            if(network_actions.steam_status)button(24,620,300,"Steam invites ("+std::to_string(steam_social.invitations.size())+")");
        } else {
            text(24,130,room.running?"Netplay session in progress":"Room lobby  /  Up to four players");
            text(24,168,"Share this connection code privately");field_box(2,24,198,746,90,room.code);
            button(24,304,190,"Copy code");
            if(can_invite()&&network_actions.invite_friend)button(230,304,240,"Invite friends");
            if(network_actions.steam_status)button(490,304,280,"Steam invites ("+std::to_string(steam_social.invitations.size())+")");
            for(int p=0;p<4;++p) {
                std::string line="Player "+std::to_string(p+1)+(room.slot==p?" (you)":"");
                line+="   ";line+=room.ready[p]?"Connected":"Waiting";
                if(!room.routes[p].empty())line+="  /  "+room.routes[p];
                text(24,366+p*38,line,room.ready[p]?SDL_Color{132,218,179,255}:SDL_Color{169,183,204,255});
            }
            if(network_actions.lobby) {
                if(room.slot==0)button(24,544,190,"Start game");else text(24,548,"Waiting for host to start the game");
                button(500,544,190,"Leave room");
            }
            for(size_t i=0;i<room.message.size()&&i<160;i+=80)text(24,598+int(i/80)*24,room.message.substr(i,80));
        }
    }
    if(page!=Page::Netplay)button(24,684,180,"Reset this page");
    button(660,684,110,network_actions.lobby?"Cancel":"Done");
    clipped_text(220,688,425,status);SDL_RenderPresent(renderer);dirty=false;
}
void slider(int which,int x) {
    float value=std::clamp((x-230)/300.f,0.f,1.f);
    if(which<3)levels[which]=value;else deadzone=std::min(95,int(value*100));
    dirty=true;
}
void connect(bool host) {
    try {
        if(!network_actions.connect)throw std::runtime_error("Netplay is unavailable in this mode.");
        auto options=network;
        if(host&&options.transport==netplay::Transport::Direct) {
            size_t used{};auto number=std::stoul(port,&used);
            if(used!=port.size()||!number||number>65535)throw std::runtime_error("Port must be 1..65535");
            if(options.address.empty())throw std::runtime_error("Enter the host's IP or DNS name.");
            options.port=uint16_t(number);options.code.clear();
        }
        if(host)options.code.clear();
        else if(options.code.empty())throw std::runtime_error("Paste the full connection code first.");
        network_actions.connect(options);
        if(network_actions.status)room=network_actions.status();
        field=-1;text_input();status.clear();
    } catch(const std::exception& ex){status=ex.what();}
    dirty=true;
}
}
void init(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    file=directory/"settings.cfg";
    bindings=defaults;levels[0]=levels[1]=levels[2]=1.f;muted=false;deadzone=15;
    preferred.clear();set_graphics({});status.clear();page=Page::Input;capture=drag=field=-1;
    std::ifstream in(file); int version;
    if(in>>version && (version==1||version==2)) {
        float a,b,c;int mute,dz;
        if(in>>a>>b>>c>>mute>>dz && std::isfinite(a)&&std::isfinite(b)&&std::isfinite(c)) {
            levels[0]=std::clamp(a,0.f,1.f);levels[1]=std::clamp(b,0.f,1.f);levels[2]=std::clamp(c,0.f,1.f);
            muted=mute!=0;deadzone=std::clamp(dz,0,95);
            for(auto& binding:bindings) {int key,pad;if(!(in>>key>>pad))break;
                if(key>SDL_SCANCODE_UNKNOWN&&key<SDL_NUM_SCANCODES&&key!=SDL_SCANCODE_F1&&key!=SDL_SCANCODE_ESCAPE)binding.key=key;
                if(pad>=0&&pad<SDL_CONTROLLER_BUTTON_MAX)binding.button=pad;
            }
            in.ignore(4096,'\n');std::getline(in,preferred);
            Graphics g;int full;
            if(version==2 && in>>g.resolution>>g.aspect>>g.filtering>>full) {
                g.resolution=std::clamp(g.resolution,0,4);g.aspect=std::clamp(g.aspect,0,2);
                g.filtering=std::clamp(g.filtering,0,2);g.fullscreen=full!=0;set_graphics(g);
            }
        }
    }
    refresh();
}
Graphics graphics() {
    unsigned bits=graphics_bits.load();return {int(bits&7),int((bits>>3)&3),int((bits>>5)&3),bool(bits&128)};
}
void set_game_window(SDL_Window* value){game_window=value;}
void set_netplay(NetplayActions actions,const netplay::Options& initial) {
    network_actions=std::move(actions);network=initial;port=std::to_string(initial.port);
    room=network_actions.status?network_actions.status():netplay::Status{};
    ++address_revision;lookup_attempted=false;lookup_status.clear();
    field=-1;status.clear();network_panel=NetworkPanel::Room;social_page=0;
    last_social_poll=0;social_message.clear();social_message_revision=0;accepting_invite.clear();steam_social={};dirty=true;text_input();
}
void show(Page next){if(drag>=0)save();if(!open)toggle();change_page(next);}
bool visible(){return open;}
float gain(int channel){return muted.load()?0.f:levels[0].load()*levels[channel+1].load();}
void toggle() {
    if(open){if(drag>=0)save();open=false;capture=drag=field=-1;text_input();SDL_HideWindow(window);return;}
    if(!window) {
        if(TTF_Init()!=0){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Settings",TTF_GetError(),nullptr);return;}
        const char* fonts[]={"C:/Windows/Fonts/segoeui.ttf","C:/Windows/Fonts/arial.ttf","/usr/share/fonts/noto/NotoSans-Regular.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/TTF/DejaVuSans.ttf"};
        for(auto path:fonts)if((font=TTF_OpenFont(path,16)))break;
        if(!font){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Settings","Install Noto Sans or DejaVu Sans to display settings.",nullptr);return;}
        window=SDL_CreateWindow("Mario Party 2 Settings",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,width,height,SDL_WINDOW_ALLOW_HIGHDPI);
        if(window)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if(!renderer){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Settings",SDL_GetError(),nullptr);return;}
        SDL_RenderSetLogicalSize(renderer,width,height);
    }
    open=true;dirty=true;SDL_ShowWindow(window);SDL_RaiseWindow(window);
}
bool event(const SDL_Event& e) {
    if(e.type==SDL_CONTROLLERDEVICEADDED||e.type==SDL_CONTROLLERDEVICEREMOVED)refresh();
    if(e.type==SDL_KEYDOWN&&!e.key.repeat&&e.key.keysym.scancode==SDL_SCANCODE_F1){toggle();return true;}
    if(!open || e.type==SDL_QUIT)return false;
    auto id=SDL_GetWindowID(window);
    if(e.type==SDL_WINDOWEVENT) {
        if(e.window.windowID!=id)return false;
        if(e.window.event==SDL_WINDOWEVENT_CLOSE)toggle();else dirty=true;
    }
    if(e.type==SDL_KEYDOWN&&!e.key.repeat&&(e.key.windowID==id||e.key.windowID==0)) {
        auto key=e.key.keysym.scancode;
        if(key==SDL_SCANCODE_ESCAPE){if(capture>=0){capture=-1;dirty=true;}else toggle();}
        else if(capture>=0&&!capture_pad){bindings[capture].key=key;capture=-1;save();}
        else if(page==Page::Netplay&&network_panel==NetworkPanel::Room&&!room_active()) {
            if(key==SDL_SCANCODE_TAB){field=network.transport==netplay::Transport::Steam?2:(field+1)%3;text_input();}
            else if(field>=0) {
                auto& target=field==0?network.address:field==1?port:network.code;
                bool ctrl=(e.key.keysym.mod&(KMOD_CTRL|KMOD_GUI))!=0;
                if(field==0&&(key==SDL_SCANCODE_BACKSPACE||(ctrl&&(key==SDL_SCANCODE_A||key==SDL_SCANCODE_V)))) {
                    ++address_revision;lookup_attempted=true;lookup_status.clear();
                }
                if(key==SDL_SCANCODE_BACKSPACE&&!target.empty())target.pop_back();
                if(key==SDL_SCANCODE_A&&ctrl)target.clear();
                if(key==SDL_SCANCODE_V&&ctrl){char* clip=SDL_GetClipboardText();if(clip){if(target.size()+std::char_traits<char>::length(clip)<=512)target+=clip;SDL_free(clip);}}
            }
            dirty=true;
        }
    }
    if(e.type==SDL_TEXTINPUT&&e.text.windowID==id&&page==Page::Netplay&&field>=0) {
        auto& target=field==0?network.address:field==1?port:network.code;
        std::string value=e.text.text;
        if(field==0){++address_revision;lookup_attempted=true;lookup_status.clear();}
        if(target.size()+value.size()<=512)target+=value;
        dirty=true;
    }
    if(e.type==SDL_CONTROLLERBUTTONDOWN&&capture>=0&&capture_pad&&assigned_port(e.cbutton.which)!=controllers.end()) {
        bindings[capture].button=e.cbutton.button;capture=-1;save();
    }
    if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.windowID==id&&e.button.button==SDL_BUTTON_LEFT) {
        float fx,fy;SDL_RenderWindowToLogical(renderer,e.button.x,e.button.y,&fx,&fy);int x=fx,y=fy;
        for(int i=0;i<4;++i)if(hit(x,y,24+i*190,80,182,26)){if(drag>=0)save();change_page(Page(i));}
        if(hit(x,y,660,684,110,26)){toggle();return true;}
        if(page==Page::Input) {
            if(hit(x,y,220,163,320,24)){drag=3;slider(3,x);}
            if(hit(x,y,230,122,540,26))cycle_primary();
            if(y>=236&&y<642){int row=(y-236)/29;if(row<int(bindings.size())&&y<236+row*29+26&&x>=230&&x<770){capture=row;capture_pad=x>=490;dirty=true;}}
        } else if(page==Page::Audio) {
            for(int i=0;i<3;++i)if(hit(x,y,220,184+i*60,320,24)){drag=i;slider(i,x);}
            if(hit(x,y,230,380,180,26)){muted=!muted;save();}
        } else if(page==Page::Graphics) {
            auto g=graphics();bool changed=false;
            if(hit(x,y,280,186,380,26)){g.resolution=(g.resolution+1)%5;changed=true;}
            if(hit(x,y,280,250,380,26)){g.aspect=(g.aspect+1)%3;changed=true;}
            if(hit(x,y,280,314,380,26)){g.fullscreen=!g.fullscreen;changed=true;}
            if(hit(x,y,280,378,380,26)){g.filtering=(g.filtering+1)%3;changed=true;}
            if(changed){set_graphics(g);save();}
        } else {
            auto social_action=[&](auto action){
                try{action();}catch(const std::exception& ex){status=ex.what();accepting_invite.clear();}
                dirty=true;
            };
            if(network_panel!=NetworkPanel::Room) {
                bool friends=network_panel==NetworkPanel::Friends;
                if(hit(x,y,620,124,150,26)){network_panel=NetworkPanel::Room;dirty=true;return true;}
                size_t count=friends?steam_social.friends.size():steam_social.invitations.size();
                if(hit(x,y,24,550,130,26)&&social_page)social_page--;
                if(hit(x,y,170,550,130,26)&&(social_page+1)*8<count)social_page++;
                if(hit(x,y,24,592,190,26)&&network_actions.refresh_friends)social_action(network_actions.refresh_friends);
                for(size_t i=social_page*8;i<count&&i<(social_page+1)*8;++i) {
                    int row=214+int(i%8)*40;
                    if(friends) {
                        if(hit(x,y,600,row,170,26)&&can_invite()&&steam_social.invite_ready&&network_actions.invite_friend)
                            social_action([&]{network_actions.invite_friend(steam_social.friends[i].id);status="Sending Steam invite...";});
                    } else {
                        auto id=steam_social.invitations[i].id;
                        if(hit(x,y,490,row,130,26)&&accepting_invite.empty()&&network_actions.connect&&network_actions.accept_invite)
                            social_action([&]{network_actions.accept_invite(id);accepting_invite=id;status="Joining invited room...";});
                        if(hit(x,y,640,row,130,26)&&accepting_invite.empty()&&network_actions.dismiss_invite)
                            social_action([&]{network_actions.dismiss_invite(id);last_social_poll=0;});
                    }
                }
                dirty=true;
            } else if(network_actions.steam_status&&hit(x,y,room_active()?490:24,room_active()?304:620,room_active()?280:300,26)) {
                network_panel=NetworkPanel::Invites;social_page=0;field=-1;text_input();dirty=true;
            } else if(can_invite()&&network_actions.invite_friend&&hit(x,y,230,304,240,26)) {
                network_panel=NetworkPanel::Friends;social_page=0;field=-1;text_input();
                if(network_actions.refresh_friends)social_action(network_actions.refresh_friends);
                dirty=true;
            } else if(!room_active()) {
                if(hit(x,y,24,154,350,26)) {
                    network.transport=network.transport==netplay::Transport::Steam?netplay::Transport::Direct:netplay::Transport::Steam;
                    field=-1;status.clear();
                }
                if(network.transport==netplay::Transport::Direct) {
                    if(hit(x,y,24,204,592,34))field=0;
                    if(hit(x,y,640,204,130,34))field=1;
                }
                if(hit(x,y,24,352,746,90))field=2;
                if(hit(x,y,24,254,190,26))connect(true);
                if(network.transport==netplay::Transport::Direct&&hit(x,y,236,254,190,26))detect_public_ip();
                if(hit(x,y,24,458,190,26))connect(false);
                text_input();dirty=true;
            } else {
                try {
                    if(hit(x,y,24,304,190,26))status=SDL_SetClipboardText(room.code.c_str())==0?"Connection code copied":"Could not copy code";
                    if(network_actions.lobby&&hit(x,y,24,544,190,26)&&room.slot==0&&!room.failed&&network_actions.start)network_actions.start();
                    if(network_actions.lobby&&hit(x,y,500,544,190,26)&&network_actions.leave){network_actions.leave();room={};status.clear();}
                }catch(const std::exception& ex){status=ex.what();}
                dirty=true;
            }
        }
        if(page!=Page::Netplay&&hit(x,y,24,684,180,26)) {
            if(page==Page::Input){bindings=defaults;deadzone=15;capture=-1;}
            if(page==Page::Audio){levels[0]=levels[1]=levels[2]=1.f;muted=false;}
            if(page==Page::Graphics)set_graphics({});
            save();
        }
    }
    if(e.type==SDL_MOUSEMOTION&&drag>=0&&e.motion.windowID==id){
        float x,y;SDL_RenderWindowToLogical(renderer,e.motion.x,e.motion.y,&x,&y);slider(drag,x);
    }
    if(e.type==SDL_MOUSEBUTTONUP&&drag>=0){drag=-1;save();}
    return true;
}
void update() {
    if(network_actions.steam_status&&(last_social_poll==0||SDL_GetTicks()-last_social_poll>=250)) {
        last_social_poll=SDL_GetTicks();
        try {
            auto previous=steam_social.invitations;
            steam_social=network_actions.steam_status();
            if(steam_social.message!=social_message||steam_social.message_revision!=social_message_revision) {
                social_message=steam_social.message;social_message_revision=steam_social.message_revision;
                if(!social_message.empty()){status=social_message;accepting_invite.clear();}
            }
            bool incoming=std::any_of(steam_social.invitations.begin(),steam_social.invitations.end(),[&](const auto& invite){
                return std::none_of(previous.begin(),previous.end(),[&](const auto& old){return old.id==invite.id;});
            });
            if(incoming) {show(Page::Netplay);network_panel=NetworkPanel::Invites;social_page=0;}
            if(!steam_social.join_code.empty()&&network_actions.connect) {
                if(!steam_social.join_code.starts_with("MP2S-"))throw std::runtime_error("Invite is not a Mario Party 2 Steam room.");
                netplay::Options options;options.code=steam_social.join_code;
                network_actions.connect(options);
                if(network_actions.dismiss_invite&&!accepting_invite.empty())network_actions.dismiss_invite(accepting_invite);
                accepting_invite.clear();network_panel=NetworkPanel::Room;
                if(network_actions.status)room=network_actions.status();
            }
        }catch(const std::exception& ex){status=ex.what();accepting_invite.clear();}
        dirty=true;
    }
    if(game_window) {
        bool current=(SDL_GetWindowFlags(game_window)&SDL_WINDOW_FULLSCREEN)!=0;
        if(current!=graphics().fullscreen && SDL_SetWindowFullscreen(game_window,graphics().fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0)!=0) {
            auto g=graphics();g.fullscreen=current;set_graphics(g);save();status=SDL_GetError();dirty=true;
        }
    }
    if(open){
        if(page==Page::Netplay) {
            if(public_ip.valid()&&public_ip.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                std::string address;
                try {address=public_ip.get();}catch(...){}
                if(network.transport==netplay::Transport::Direct&&lookup_revision==address_revision&&!room_active()) {
                    if(!address.empty()) {network.address=address;lookup_status="Public IPv4 detected. Port forwarding is still required.";}
                    else lookup_status="Could not detect public IP. Enter it manually or retry.";
                }
                dirty=true;
            }
            if(network.transport==netplay::Transport::Direct&&!lookup_attempted&&!room_active()&&network_actions.connect&&network.detect_public_ip&&network.code.empty()&&network.address=="127.0.0.1")detect_public_ip();
        }
        static Uint32 last_poll{};
        if(page==Page::Netplay&&network_actions.status&&SDL_GetTicks()-last_poll>=100){room=network_actions.status();dirty=true;last_poll=SDL_GetTicks();}
        if(dirty)draw();
    }
}
Inputs read_inputs(bool focused) {
    Inputs inputs{};
    inputs.front().connected = true; // The keyboard always provides port 1.
    for (size_t port = 0; port < controllers.size(); ++port) {
        auto* controller = controllers[port];
        if (!controller) continue;
        auto& input = inputs[port];
        input.connected = true;
        if (!focused || open) continue;

        for (const auto& binding : bindings) {
            if (SDL_GameControllerGetButton(controller, SDL_GameControllerButton(binding.button))) {
                input.buttons |= binding.mask;
            }
        }
        float x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) / 32768.f;
        float y = -SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) / 32768.f;
        float length = std::sqrt(x * x + y * y);
        float threshold = deadzone / 100.f;
        if (length > threshold) {
            float scale = std::min(1.f, (length - threshold) / (1 - threshold)) / length;
            input.x = x * scale;
            input.y = y * scale;
        }
    }
    if (focused && !open) {
        auto keys = SDL_GetKeyboardState(nullptr);
        auto& input = inputs.front();
        for (const auto& binding : bindings) {
            if (keys[binding.key]) input.buttons |= binding.mask;
        }
        float x = float(keys[SDL_SCANCODE_RIGHT]) - float(keys[SDL_SCANCODE_LEFT]);
        float y = float(keys[SDL_SCANCODE_UP]) - float(keys[SDL_SCANCODE_DOWN]);
        input.x = std::clamp(input.x + x, -1.f, 1.f);
        input.y = std::clamp(input.y + y, -1.f, 1.f);
    }
    return inputs;
}
void shutdown() {
    for (auto& controller : controllers) {
        if (controller) SDL_GameControllerClose(controller);
        controller = nullptr;
    }
    devices.clear();
    if(renderer)SDL_DestroyRenderer(renderer);
    if(window)SDL_DestroyWindow(window);
    if(font)TTF_CloseFont(font);
    if(TTF_WasInit())TTF_Quit();
    renderer=nullptr;window=nullptr;font=nullptr;open=false;game_window=nullptr;network_actions={};room={};SDL_StopTextInput();
}
}

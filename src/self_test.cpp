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

#ifndef _WIN32
#include <gtk/gtk.h>
#include <fontconfig/fontconfig.h>
#include <xkbcommon/xkbcommon.h>
#endif
int mp2_runtime_self_test(){
#ifndef _WIN32
    // No ROM: initialize the same GTK/display stack that needs the bundle data.
    if(!gtk_init_check(nullptr,nullptr)){fputs("GTK display initialization failed.\n",stderr);return 1;}
    FcChar8* file=FcConfigFilename(nullptr);
    if(!file){fputs("Fontconfig configuration missing.\n",stderr);return 2;}
    FcConfig* fonts=FcConfigCreate();
    bool valid=fonts && FcConfigParseAndLoad(fonts,file,FcTrue);
    FcStrFree(file);if(fonts)FcConfigDestroy(fonts);
    if(!valid){fputs("Fontconfig configuration cannot be parsed.\n",stderr);return 3;}
    xkb_context* context=xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* keymap=context?xkb_keymap_new_from_names(context,nullptr,XKB_KEYMAP_COMPILE_NO_FLAGS):nullptr;
    if(keymap)xkb_keymap_unref(keymap);
    if(context)xkb_context_unref(context);
    if(!keymap){fputs("XKB default keymap failed.\n",stderr);return 4;}
    puts("GTK, Fontconfig and XKB runtime data check passed.");
#endif
    return 0;
}

#include <SDL.h>
#include <SDL_vulkan.h>
#include <cstring>
#include <vector>
int mp2_video_self_test(){
    bool wayland=false,x11=false;
    for(int i=0;i<SDL_GetNumVideoDrivers();++i){
        const char* name=SDL_GetVideoDriver(i);printf("SDL video backend: %s\n",name);
        wayland|=std::strcmp(name,"wayland")==0;x11|=std::strcmp(name,"x11")==0;
    }
#ifndef _WIN32
    if(!wayland || !x11){fputs("SDL must include Wayland and X11.\n",stderr);return 1;}
#endif
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR,"1");
    if(SDL_Init(SDL_INIT_VIDEO)!=0){fprintf(stderr,"SDL video initialization failed: %s\n",SDL_GetError());return 2;}
    SDL_Window* window=SDL_CreateWindow("mp2Recomp display check",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
        320,240,SDL_WINDOW_VULKAN|SDL_WINDOW_SHOWN);
    if(!window){fprintf(stderr,"SDL Vulkan window failed: %s\n",SDL_GetError());SDL_Quit();return 3;}
    unsigned count=0;
    bool valid=SDL_Vulkan_GetInstanceExtensions(window,&count,nullptr)==SDL_TRUE;
    std::vector<const char*> extensions(count);
    valid=valid && SDL_Vulkan_GetInstanceExtensions(window,&count,extensions.data())==SDL_TRUE;
    if(std::strcmp(SDL_GetCurrentVideoDriver(),"wayland")==0){
        bool surface=false;for(const char* extension:extensions)surface|=std::strcmp(extension,"VK_KHR_wayland_surface")==0;
        valid=valid && surface;
    }
    printf("SDL active video backend: %s\n",SDL_GetCurrentVideoDriver());
    for(int i=0;i<30;++i){SDL_PumpEvents();SDL_Delay(10);}
    SDL_DestroyWindow(window);SDL_Quit();
    if(!valid){fputs("Vulkan surface extensions are unavailable.\n",stderr);return 4;}
    puts("SDL Vulkan window check passed.");return 0;
}

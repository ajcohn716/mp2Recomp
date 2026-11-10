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

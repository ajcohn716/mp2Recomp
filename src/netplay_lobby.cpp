#include "netplay.hpp"
#include "settings.hpp"
#include <SDL.h>
#include <stdexcept>

namespace netplay {
std::unique_ptr<Session> lobby(const Options& initial,const std::vector<uint8_t>& host_save,bool connect_immediately) {
    std::unique_ptr<Session> session;
    auto connect=[&](const Options& options){session=std::make_unique<Session>(options);};
    settings::set_netplay({
        .connect=connect,
        .status=[&]{return session?session->status():Status{};},
        .start=[&]{
            if(!session)return;
            auto status=session->status();
            if(status.players<2)throw std::runtime_error("Wait for a friend to connect.");
            session->start(host_save);
        },
        .leave=[&]{session.reset();},
        .lobby=true
    },initial);
    settings::show(settings::Page::Netplay);
    if(!settings::visible())throw std::runtime_error("Cannot open the netplay settings window");
    if(connect_immediately) {
        // The initial connection has already been validated by the in-game tab.
        try {connect(initial);} catch(const std::exception& ex) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Netplay",ex.what(),nullptr);
        }
    }
    bool started=false;
    while(settings::visible()) {
        if(session&&session->status().running){started=true;break;}
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT){if(settings::visible())settings::toggle();break;}
            settings::event(e);
        }
        uint16_t buttons;float x,y;settings::update(buttons,x,y);
        SDL_Delay(16);
    }
    if(settings::visible())settings::toggle();
    settings::set_netplay({});
    return started?std::move(session):nullptr;
}
}

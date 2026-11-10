#pragma once
#include <SDL.h>
#include <filesystem>
#include <functional>
#include "netplay.hpp"
namespace settings {
enum class Page { Input, Audio, Graphics, Netplay };
struct Graphics {
    int resolution{1}; // 0 = fit window, 1..4 = native resolution multiplier
    int aspect{}; // original, 16:9, fit window
    int filtering{1}; // nearest, linear, pixel scaling
    bool fullscreen{};
    bool operator==(const Graphics&) const = default;
};
struct NetplayActions {
    std::function<void(const netplay::Options&)> connect;
    std::function<netplay::Status()> status;
    std::function<void()> start, leave;
    bool lobby{};
};
void init(const std::filesystem::path& directory);
void set_game_window(SDL_Window* window);
Graphics graphics();
void set_netplay(NetplayActions actions, const netplay::Options& initial = {});
void show(Page page);
void shutdown();
void toggle();
bool event(const SDL_Event& event);
void update(uint16_t& buttons, float& x, float& y);
float gain(int channel);
bool visible();
}

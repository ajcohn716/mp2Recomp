#pragma once
#include <SDL.h>
#include <filesystem>
namespace settings {
void init(const std::filesystem::path& directory);
void shutdown();
void toggle();
bool event(const SDL_Event& event);
void update(uint16_t& buttons, float& x, float& y);
float gain(int channel);
bool visible();
}

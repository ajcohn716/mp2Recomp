#pragma once
#include "debug.hpp"
#include <cstdint>
#include <filesystem>
namespace mp2::checkpoint {
using Json = debug::Json;
void configure(bool enabled, const std::filesystem::path &load, const std::filesystem::path &save_dir);
bool enabled();
void initialize_memory(uint8_t *rdram);
void local_input(uint16_t buttons, float x, float y);
void latch(uint8_t *rdram);
uint64_t frame();
Json status();
void save(const std::filesystem::path &path);
void load(const std::filesystem::path &path);
std::filesystem::path requested_load();
bool is_paused();
void pause();
void step(unsigned frames);
void resume();
void cancel();
} // namespace mp2::checkpoint

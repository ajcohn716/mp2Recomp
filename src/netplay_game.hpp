#pragma once
#include "netplay.hpp"
#include <filesystem>
namespace netplay::game {
void configure(Session* session, unsigned probe_frames, unsigned stop_frame,
               const std::filesystem::path& trace, bool bot);
void local_input(uint16_t buttons, float x, float y);
bool active();
uint64_t checksum(uint8_t* rdram);
void cancel();
bool failed();
uint8_t connected_mask();
void initialize(uint8_t* rdram);
}

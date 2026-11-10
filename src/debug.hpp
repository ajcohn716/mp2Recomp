#pragma once
#include "json/json.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Private, local diagnostics. The command inbox is deliberately opt-in.
namespace mp2::debug {
using Json = nlohmann::json;
void initialize(const std::filesystem::path &root, bool controls, bool headless);
void shutdown();
void quick_save();
void quick_load();
bool needs_present();
bool next_trace(std::filesystem::path& path);
void write_trace(const std::filesystem::path& path, Json data);
void poll(); // SDL/main thread
void vi(unsigned count);
void input(uint16_t &buttons, float &x, float &y);
void audio_rate(unsigned rate);
void audio(const int16_t *samples, size_t count);
void telemetry(Json values);
void event(const std::string &type, Json values = Json::object());
void request_capture(unsigned seconds = 15, unsigned fps = 10);
struct FrameRequest {
    std::filesystem::path path;
    Json metadata;
};
bool next_frame(FrameRequest &request); // presentation thread, before GPU copy
void frame_ready(FrameRequest request, unsigned width, unsigned height, std::vector<uint8_t> rgba);
void frame_error(const FrameRequest &request, const std::string &error);
} // namespace mp2::debug

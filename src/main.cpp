#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif
#include "settings.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"

void register_mp2_overlays();
void mp2_entry(uint8_t*, recomp_context*);
void mp2_init(uint8_t*, recomp_context*);
std::unique_ptr<ultramodern::renderer::RendererContext> create_mp2_renderer(uint8_t*, ultramodern::renderer::WindowHandle, bool);
extern RspUcodeFunc aspMain;

namespace {
SDL_Window* window;
SDL_AudioDeviceID audio_device{};
std::atomic<uint16_t> buttons{};
std::atomic<float> stick_x{}, stick_y{};
std::atomic<unsigned> vi_count{};
std::atomic<size_t> last_audio_frames{};
unsigned quit_after{};
unsigned auto_start_at{};
void set_frequency(uint32_t rate) {
    if (audio_device) SDL_CloseAudioDevice(audio_device);
    SDL_AudioSpec spec{};
    spec.freq = rate; spec.format = AUDIO_S16SYS; spec.channels = 2; spec.samples = 1024;
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    if (audio_device) SDL_PauseAudioDevice(audio_device, 0);
    fprintf(stderr, "[MP2] audio %u Hz, device=%u\n", rate, audio_device);
}
void queue_audio(int16_t* samples, size_t count) {
    if (!audio_device) return;
    if (count > 0x8000) {
        fprintf(stderr, "[MP2] invalid audio sample count: %zu\n", count);
        std::abort();
    }
    last_audio_frames = count / 2;
    std::vector<int16_t> swapped(count);
    for (size_t i = 0; i < count; ++i) swapped[i] = int16_t(samples[i ^ 1] * settings::gain(int(i % 2)));
    SDL_QueueAudio(audio_device, swapped.data(), count * sizeof(int16_t));
}
size_t audio_remaining() {
    // AI_LEN describes the current hardware DMA, not the sum of all buffers
    // waiting in SDL. Reporting the whole queue can underflow MP2's next size.
    return audio_device ? std::min(size_t(SDL_GetQueuedAudioSize(audio_device) / 4), last_audio_frames.load()) : 0;
}
bool get_input(int port, uint16_t* b, float* x, float* y) {
    if (port != 0) return false;
    *b = buttons.load(); *x = stick_x.load(); *y = stick_y.load();
    unsigned vi = vi_count.load();
    if (auto_start_at && vi >= auto_start_at && vi < auto_start_at + 4) *b |= 0x1000;
    return true;
}
ultramodern::input::connected_device_info_t connected(int port) {
    using namespace ultramodern::input;
    return {port == 0 ? Device::Controller : Device::None, Pak::None};
}
void update_gfx(void*) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (settings::event(event)) continue;
        if (event.type == SDL_QUIT) ultramodern::quit();
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) ultramodern::quit();
    }
    uint16_t b; float x, y;
    settings::update(b, x, y);
    buttons.store(b); stick_x = x; stick_y = y;
}
void vi_callback() {
    unsigned count = ++vi_count;
    if (count % 300 == 0) fprintf(stderr, "[MP2] VI %u\n", count);
    if (quit_after && count >= quit_after) ultramodern::quit();
}
void message_box(const char* msg) { fprintf(stderr, "[MP2 ERROR] %s\n", msg); }
RspUcodeFunc* get_ucode(const OSTask* task) {
    if (task->t.type == M_AUDTASK) return aspMain;
    fprintf(stderr, "[MP2] unknown RSP task: %u\n", task->t.type);
    return nullptr;
}
}

int main(int argc, char** argv) {
    for(int i=1;i<argc;++i) if(std::string_view(argv[i])=="--self-test") {
        extern int mp2_self_test();return mp2_self_test();
    }
#ifdef _WIN32
    if(char* base=SDL_GetBasePath()) { std::filesystem::current_path(std::filesystem::u8path(base));SDL_free(base); }
#endif
    bool open_settings = false;
    std::filesystem::path rom = "Mario Party 2 (USA).z64";
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--settings") open_settings = true;
        else if (std::string_view(argv[i]) == "--rom" && i+1 < argc) rom = argv[++i];
        else if (std::string_view(argv[i]) == "--frames" && i+1 < argc) quit_after = std::stoul(argv[++i]);
        else if (std::string_view(argv[i]) == "--auto-start" && i+1 < argc) auto_start_at = std::stoul(argv[++i]);
    }
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError()); return 1;
    }
    window = SDL_CreateWindow("Mario Party 2 — Native Recompilation POC", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_RESIZABLE
#ifndef _WIN32
        | SDL_WINDOW_VULKAN
#endif
    );
    if (!window) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    auto config_dir = std::filesystem::absolute("userdata");
    std::filesystem::create_directories(config_dir);
    recomp::register_config_path(config_dir);
    settings::init(config_dir);
    if (open_settings) settings::toggle();
    register_mp2_overlays();
    recomp::register_game({
        .rom_hash = 0xd2dbfb3b4d7a5e81ULL,
        .internal_name = "MarioParty2",
        .display_name = "Mario Party 2 (USA)",
        .game_id = u8"mp2.us",
        .mod_game_id = "mp2",
        .save_type = recomp::SaveType::Eep4k,
        .is_enabled = true,
        .entrypoint_address = gpr(int64_t(int32_t(0x80000400))),
        .entrypoint = mp2_entry,
        .on_init_callback = mp2_init,
    });
    auto result = recomp::select_rom(rom, u8"mp2.us");
    if (result != recomp::RomValidationError::Good) {
        fprintf(stderr, "ROM validation failed: %d\n", int(result)); return 1;
    }
    recomp::Configuration config{};
    config.project_version = {0, 1, 0, "poc"};
#ifdef _WIN32
    SDL_SysWMinfo wm{};SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(window,&wm)) { fprintf(stderr,"%s\n",SDL_GetError());return 1; }
    config.window_handle = {wm.info.win.window, GetCurrentThreadId()};
#else
    config.window_handle = window;
#endif
    config.rsp_callbacks.get_rsp_microcode = get_ucode;
    config.renderer_callbacks.create_render_context = create_mp2_renderer;
    config.audio_callbacks = {queue_audio, audio_remaining, set_frequency};
    config.input_callbacks.get_input = get_input;
    config.input_callbacks.get_connected_device_info = connected;
    config.gfx_callbacks.update_gfx = update_gfx;
    config.events_callbacks.vi_callback = vi_callback;
    config.error_handling_callbacks.message_box = message_box;
    recomp::start_game(u8"mp2.us", "");
    recomp::start(config);
    if (audio_device) SDL_CloseAudioDevice(audio_device);
    settings::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <gtk/gtk.h>
#endif
#include "settings.hpp"
#include "debug.hpp"
#include "debug_state.hpp"
#include "netplay_game.hpp"
#include <cstdlib>
#include <thread>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <optional>
#include <cerrno>
#include <cstring>
#ifndef _WIN32
#include <unistd.h>
#endif
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"

void register_mp2_overlays();
void mp2_entry(uint8_t*, recomp_context*);
void mp2_init(uint8_t*, recomp_context*);
std::unique_ptr<ultramodern::renderer::RendererContext> create_mp2_renderer(uint8_t*, ultramodern::renderer::WindowHandle, bool);
extern RspUcodeFunc aspMain;
std::unique_ptr<ultramodern::renderer::RendererContext> create_mp2_null_renderer(uint8_t*, ultramodern::renderer::WindowHandle, bool);

namespace {
constexpr auto rom_filename = "Mario Party 2 (USA).z64";
constexpr auto rom_size = 32 * 1024 * 1024;
const std::u8string game_id = u8"mp2.us";

std::string utf8_path(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}

#ifdef _WIN32
std::wstring quote_argument(const std::wstring& value) {
    std::wstring result=L"\"";
    size_t slashes=0;
    for(wchar_t c:value) {
        if(c==L'\\'){++slashes;continue;}
        result.append(c==L'"'?slashes*2+1:slashes,L'\\');
        result+=c;slashes=0;
    }
    result.append(slashes*2,L'\\');
    return result+L'"';
}
#endif

std::optional<std::filesystem::path> choose_rom_file() {
#ifdef _WIN32
    std::wstring filename(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrTitle = L"Choose your Mario Party 2 USA ROM";
    dialog.lpstrFilter = L"Nintendo 64 ROMs (*.z64;*.v64;*.n64)\0*.z64;*.v64;*.n64\0All files\0*.*\0";
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) return std::filesystem::path(filename.c_str());
    if (CommDlgExtendedError()) throw std::runtime_error("Could not open the ROM picker. Use --rom to select a file.");
    return std::nullopt;
#else
    if (!gtk_init_check(nullptr, nullptr)) {
        throw std::runtime_error("Could not open the ROM picker. Use --rom to select a file.");
    }
    auto* dialog = gtk_file_chooser_native_new("Choose your Mario Party 2 USA ROM", nullptr,
                                              GTK_FILE_CHOOSER_ACTION_OPEN, "Play", "Cancel");
    auto* chooser = GTK_FILE_CHOOSER(dialog);
    auto* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Nintendo 64 ROMs");
    for (const auto* pattern : {"*.[zZ]64", "*.[vV]64", "*.[nN]64"}) {
        gtk_file_filter_add_pattern(filter, pattern);
    }
    gtk_file_chooser_add_filter(chooser, filter);
    std::optional<std::filesystem::path> path;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        if (char* filename = gtk_file_chooser_get_filename(chooser)) {
            path.emplace(filename);
            g_free(filename);
        }
    }
    g_object_unref(dialog);
    return path;
#endif
}

std::optional<std::filesystem::path> prepare_rom(const std::filesystem::path& requested,
                                                 const std::filesystem::path& config_dir,
                                                 const std::filesystem::path& game_directory,
                                                 bool allow_picker) {
    recomp::GameEntry game{
        .rom_hash = 0xd2dbfb3b4d7a5e81ULL,
        .internal_name = "MarioParty2",
        .display_name = "Mario Party 2 (USA)",
        .game_id = game_id,
        .mod_game_id = "mp2",
        .save_type = recomp::SaveType::Eep4k,
        .is_enabled = true,
        .entrypoint_address = gpr(int64_t(int32_t(0x80000400))),
        .entrypoint = mp2_entry,
        .on_init_callback = mp2_init,
    };
    recomp::register_game(game);
    std::filesystem::create_directories(config_dir);
    recomp::register_config_path(config_dir);
    const auto stored = config_dir / game.stored_filename();
    auto correct_size = [](const std::filesystem::path& path) {
        std::error_code error;
        return std::filesystem::file_size(path, error) == rom_size;
    };
    auto import = [&](const std::filesystem::path& path) {
        if (!correct_size(path)) return false;
        std::error_code error;
        if (std::filesystem::equivalent(path, stored, error)) return recomp::load_stored_rom(game_id);
        if (recomp::select_rom(path, game_id) != recomp::RomValidationError::Good) return false;
        if (!recomp::load_stored_rom(game_id)) {
            throw std::runtime_error("Could not save the selected ROM. Check that userdata is writable.");
        }
        return true;
    };
    constexpr auto invalid_rom = "Select an unmodified 32 MiB Mario Party 2 USA ROM (.z64, .v64, or .n64).";
    if (!requested.empty()) {
        if (import(requested)) return stored;
        throw std::runtime_error(invalid_rom);
    }
    if ((correct_size(stored) && recomp::load_stored_rom(game_id)) || import(rom_filename)) {
        return stored;
    }
    std::error_code directory_error;
    for (std::filesystem::directory_iterator entry(game_directory / "games", directory_error), end;
         !directory_error && entry != end; entry.increment(directory_error)) {
        const auto& path = entry->path();
        const auto extension = path.extension().u8string();
        if (extension != u8".z64" && extension != u8".Z64" &&
            extension != u8".v64" && extension != u8".V64" &&
            extension != u8".n64" && extension != u8".N64") continue;
        std::error_code file_error;
        if (entry->is_regular_file(file_error) && import(path)) return stored;
    }
    if (!allow_picker) throw std::runtime_error(invalid_rom);
    while (auto selected = choose_rom_file()) {
        if (import(*selected)) return stored;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mario Party 2 ROM", invalid_rom, nullptr);
    }
    return std::nullopt;
}

void configure_bundled_runtime() {
#ifndef _WIN32
    char* base=SDL_GetBasePath();
    if (!base) return;
    std::filesystem::path directory(base);SDL_free(base);
    auto fonts=directory/"etc/fonts";
    if(std::filesystem::is_regular_file(fonts/"fonts.conf")) {
        setenv("FONTCONFIG_PATH",fonts.c_str(),1);
        setenv("FONTCONFIG_FILE",(fonts/"fonts.conf").c_str(),1);
    }
    auto decorations=directory/"lib/libdecor/plugins-1";
    if(std::filesystem::is_directory(decorations))
        setenv("LIBDECOR_PLUGIN_DIR",decorations.c_str(),1);
    auto locale=directory/"share/X11/locale";
    if(std::filesystem::is_regular_file(locale/"compose.dir"))
        setenv("XLOCALEDIR",locale.c_str(),1);
    auto keyboard=directory/"share/X11/xkb";
    if(std::filesystem::is_regular_file(keyboard/"rules/evdev"))
        setenv("XKB_CONFIG_ROOT",keyboard.c_str(),1);
#endif
}
SDL_Window* window;
SDL_AudioDeviceID audio_device{};
std::atomic<uint16_t> buttons{};
std::atomic<float> stick_x{}, stick_y{};
std::atomic<unsigned> vi_count{};
std::atomic<size_t> last_audio_frames{};
unsigned quit_after{};
unsigned auto_start_at{};
bool headless{};
std::optional<netplay::Options> requested_room;
void set_frequency(uint32_t rate) {
    mp2::debug::audio_rate(rate);
    if (audio_device) SDL_CloseAudioDevice(audio_device);
    SDL_AudioSpec spec{};
    spec.freq = rate; spec.format = AUDIO_S16SYS; spec.channels = 2; spec.samples = 1024;
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    if (audio_device) SDL_PauseAudioDevice(audio_device, 0);
    fprintf(stderr, "[MP2] audio %u Hz, device=%u\n", rate, audio_device);
}
void queue_audio(int16_t* samples, size_t count) {
    if (count > 0x8000) {
        fprintf(stderr, "[MP2] invalid audio sample count: %zu\n", count);
        std::abort();
    }
    last_audio_frames = count / 2;
    std::vector<int16_t> swapped(count);
    for (size_t i = 0; i < count; ++i) swapped[i] = int16_t(samples[i ^ 1] * settings::gain(int(i % 2)));
    mp2::debug::audio(swapped.data(), count);
    if(audio_device) SDL_QueueAudio(audio_device, swapped.data(), count * sizeof(int16_t));
}
size_t audio_remaining() {
    // AI_LEN describes the current hardware DMA, not the sum of all buffers
    // waiting in SDL. Reporting the whole queue can underflow MP2's next size.
    return audio_device ? std::min(size_t(SDL_GetQueuedAudioSize(audio_device) / 4), last_audio_frames.load()) : 0;
}
bool get_input(int port, uint16_t* b, float* x, float* y) {
    if (netplay::game::active() || mp2::checkpoint::enabled()) return false; // online samples enter at the game latch
    if (port != 0) return false;
    *b = buttons.load(); *x = stick_x.load(); *y = stick_y.load();
    mp2::debug::input(*b,*x,*y);
    unsigned vi = vi_count.load();
    if (auto_start_at && vi >= auto_start_at && vi < auto_start_at + 4) *b |= 0x1000;
    return true;
}
ultramodern::input::connected_device_info_t connected(int port) {
    using namespace ultramodern::input;
    return {port >= 0 && port < 4 && (netplay::game::connected_mask() & (1 << port)) ? Device::Controller : Device::None, Pak::None};
}
void update_gfx(void*) {
    mp2::debug::poll();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_F8) { mp2::debug::request_capture(); continue; }
        if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_F5) { mp2::debug::quick_save(); continue; }
        if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_F9) { mp2::debug::quick_load(); continue; }
        if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_F6) { mp2::checkpoint::resume(); continue; }
        if (settings::event(event)) continue;
        if (event.type == SDL_QUIT) { netplay::game::cancel(); ultramodern::quit(); }
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { netplay::game::cancel(); ultramodern::quit(); }
    }
    uint16_t b; float x, y;
    settings::update(b, x, y);
    if(mp2::checkpoint::enabled()&&auto_start_at&&vi_count>=auto_start_at&&vi_count<auto_start_at+4)b|=0x1000;
    buttons.store(b); stick_x = x; stick_y = y;
    netplay::game::local_input(b,x,y);
    mp2::checkpoint::local_input(b,x,y);
}
void vi_callback() {
    unsigned count = ++vi_count;
    mp2::debug::vi(count);
    if (count % 300 == 0) fprintf(stderr, "[MP2] VI %u\n", count);
    if (quit_after && count >= quit_after) { mp2::checkpoint::cancel(); ultramodern::quit(); }
}
void message_box(const char* msg) { fprintf(stderr, "[MP2 ERROR] %s\n", msg); }
RspUcodeFunc* get_ucode(const OSTask* task) {
    if (task->t.type == M_AUDTASK) return aspMain;
    fprintf(stderr, "[MP2] unknown RSP task: %u\n", task->t.type);
    return nullptr;
}
}

int main(int argc, char** argv) try {
#ifdef _WIN32
    // The CRT's narrow argv can lose characters in ROM and save paths.
    std::unique_ptr<wchar_t*, decltype(&LocalFree)> wide_argv(CommandLineToArgvW(GetCommandLineW(), &argc), LocalFree);
    if (!wide_argv) throw std::runtime_error("Could not read command-line arguments.");
    std::vector<std::string> utf8_arguments;
    for (int i = 0; i < argc; ++i) utf8_arguments.push_back(utf8_path(wide_argv.get()[i]));
    std::vector<char*> utf8_argv;
    for (auto& argument : utf8_arguments) utf8_argv.push_back(argument.data());
    utf8_argv.push_back(nullptr);
    argv = utf8_argv.data();
#endif
    configure_bundled_runtime();
    for(int i=1;i<argc;++i) if(std::string_view(argv[i])=="--video-self-test") {
        extern int mp2_video_self_test();return mp2_video_self_test();
    }
    for(int i=1;i<argc;++i) if(std::string_view(argv[i])=="--runtime-self-test") {
        extern int mp2_runtime_self_test();return mp2_runtime_self_test();
    }
    for(int i=1;i<argc;++i) if(std::string_view(argv[i])=="--self-test") {
        extern int mp2_self_test();return mp2_self_test();
    }
    const auto launch_directory = std::filesystem::current_path();
    auto game_directory = launch_directory;
    if (const std::unique_ptr<char, decltype(&SDL_free)> base{SDL_GetBasePath(), SDL_free}; base) {
        game_directory = std::filesystem::u8path(base.get());
#ifdef _WIN32
        std::filesystem::current_path(game_directory);
#endif
    }
    bool open_settings = false, multiplayer = false, bot = false, connect_immediately = false;
    unsigned probe = 0, stop_frame = 0, auto_players = 0;
    netplay::Options network;
    std::filesystem::path data_directory = "userdata", trace, debug_directory;
    bool debug_controls=false, checkpoints=false;
    std::filesystem::path checkpoint_load;

    std::filesystem::path requested_rom;
    for (int i = 1; i < argc; ++i) {
        auto argument = std::string_view(argv[i]);
        auto value = [&]() -> const char* { if(i+1>=argc) throw std::runtime_error("Missing value for " + std::string(argument)); return argv[++i]; };
        if (argument == "--debug-checkpoints") checkpoints=true;
        else if (argument == "--debug-load") { checkpoint_load=std::filesystem::u8path(value()); checkpoints=true; }
        else if (argument == "--debug-controls") debug_controls=true;
        else if (argument == "--debug-dir") debug_directory=std::filesystem::u8path(value());
        else if (argument == "--netplay" || argument == "--host") multiplayer = true;
        else if (argument == "--netplay-connect") { multiplayer = true; connect_immediately = true; }
        else if (argument == "--join") { multiplayer = true; network.code = value(); }
        else if (argument == "--host-address") { network.address = value(); network.detect_public_ip = false; network.transport = netplay::Transport::Direct; }
        else if (argument == "--netplay-transport") {
            std::string_view transport=value();
            if(transport=="steam")network.transport=netplay::Transport::Steam;
            else if(transport=="direct")network.transport=netplay::Transport::Direct;
            else throw std::runtime_error("Netplay transport must be steam or direct");
        }
        else if (argument == "--host-port") { auto port = std::stoul(value()); if (port == 0 || port > 65535) throw std::runtime_error("Host port must be 1..65535"); network.port = uint16_t(port); }
        else if (argument == "--data-dir") data_directory = std::filesystem::u8path(value());
        else if (argument == "--netplay-trace") trace = std::filesystem::u8path(value());
        else if (argument == "--netplay-probe") probe = std::stoul(value());
        else if (argument == "--netplay-frames") stop_frame = std::stoul(value());
        else if (argument == "--netplay-auto-start") auto_players = std::stoul(value());
        else if (argument == "--netplay-bot") bot = true;
        else if (argument == "--headless") headless = true;
        else if (std::string_view(argv[i]) == "--settings") open_settings = true;
        else if (argument == "--rom") requested_rom = launch_directory / std::filesystem::u8path(value());
        else if (std::string_view(argv[i]) == "--frames" && i+1 < argc) quit_after = std::stoul(argv[++i]);
        else if (std::string_view(argv[i]) == "--auto-start" && i+1 < argc) auto_start_at = std::stoul(argv[++i]);
        else throw std::runtime_error("Unknown argument: " + std::string(argument));
    }
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError()); return 1;
    }
    if (auto_players && (auto_players < 2 || auto_players > 4 || !multiplayer)) throw std::runtime_error("Automatic lobby start requires --netplay and 2..4 players");
    if (probe && multiplayer) throw std::runtime_error("Use either a local probe or multiplayer");
    if ((probe || multiplayer) && auto_start_at) throw std::runtime_error("Use --netplay-bot for deterministic automated inputs");
    auto config_dir = std::filesystem::absolute(data_directory);
    const auto rom = prepare_rom(requested_rom, config_dir, game_directory, !headless && !probe);
    if (!rom) { SDL_Quit(); return 0; }
    debug_controls=debug_controls||checkpoints;
    mp2::debug::initialize(debug_directory.empty()?data_directory/"debug":debug_directory,debug_controls,headless);
    settings::init(config_dir);
    std::unique_ptr<netplay::SteamSocial> steam_social;
    if(!headless&&!probe&&!checkpoints)steam_social=std::make_unique<netplay::SteamSocial>();
    std::unique_ptr<netplay::Session> session;
    if (multiplayer) {
        std::vector<uint8_t> host_save;
        std::ifstream in(config_dir / "saves/mp2.us.bin", std::ios::binary);
        if (in) { host_save.assign(std::istreambuf_iterator<char>(in), {}); if (host_save.size() != 512) throw std::runtime_error("The host save must be 512 bytes"); }
        if (auto_players) {
            session = std::make_unique<netplay::Session>(network);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
            bool requested = false;
            while (!session->status().running) {
                auto status = session->status();
                if (status.failed) throw std::runtime_error(status.message);
                if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Automatic lobby timed out");
                if (status.slot == 0 && !requested && status.players == int(auto_players)) {
                    bool ready = true;
                    for (int p = 0; p < int(auto_players); ++p) ready &= status.ready[p];
                    if (ready) { session->start(host_save); requested = true; }
                }
                SDL_PumpEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } else session = netplay::lobby(network, host_save, connect_immediately);
        if (!session) { settings::shutdown(); SDL_Quit(); return 0; }
        config_dir /= "netplay/session-p" + std::to_string(session->status().slot + 1) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(config_dir / "saves");
        auto initial = session->initial_save();
        if (!initial.empty()) { std::ofstream out(config_dir / "saves/mp2.us.bin", std::ios::binary); out.write(reinterpret_cast<const char*>(initial.data()), initial.size()); if (!out) throw std::runtime_error("Cannot write session save"); }
    } else if (probe) {
        config_dir /= "netplay/probe-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    if(checkpoints && (probe||multiplayer)) throw std::runtime_error("Checkpoints require offline play");
    if(!checkpoint_load.empty())config_dir/= "debug-replays/"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    mp2::checkpoint::configure(checkpoints,checkpoint_load,config_dir/"saves");
    netplay::game::configure(session.get(),probe,stop_frame,trace,bot);
    window = SDL_CreateWindow("Mario Party 2 — Native Recompilation POC", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_RESIZABLE
#ifndef _WIN32
        | (headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_VULKAN)
#endif
    );
    if (!window) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    if (session) SDL_SetWindowTitle(window, ("Mario Party 2 | Netplay | Player " + std::to_string(session->status().slot + 1)).c_str());
    std::filesystem::create_directories(config_dir);
    // Netplay and checkpoint runs use separate save directories. Give the
    // runtime the same validated ROM in that directory as well.
    if (config_dir != rom->parent_path()) {
        std::filesystem::copy_file(*rom, config_dir / rom->filename(), std::filesystem::copy_options::overwrite_existing);
    }
    recomp::register_config_path(config_dir);
    settings::set_game_window(window);
    settings::NetplayActions network_actions;
    if(session)network_actions.status=[&]{return session->status();};
    if(!probe&&!checkpoints)network_actions.connect=[](const netplay::Options& options){
        netplay::validate_options(options);
        requested_room=options;
        netplay::game::cancel();
        ultramodern::quit();
    };
    if(steam_social) {
        network_actions.steam_status=[&]{return steam_social->status();};
        network_actions.refresh_friends=[&]{steam_social->refresh();};
        network_actions.invite_friend=[&](const auto& id){steam_social->invite(id);};
        network_actions.accept_invite=[&](const auto& id){steam_social->accept(id);};
        network_actions.dismiss_invite=[&](const auto& id){steam_social->dismiss(id);};
    }
    settings::set_netplay(std::move(network_actions),network);
    if (open_settings) settings::toggle();
    register_mp2_overlays();
    recomp::Configuration config{};
    config.project_version = {0, 3, 1, "alpha"};
#ifdef _WIN32
    SDL_SysWMinfo wm{};SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(window,&wm)) { fprintf(stderr,"%s\n",SDL_GetError());return 1; }
    config.window_handle = {wm.info.win.window, GetCurrentThreadId()};
#else
    config.window_handle = window;
#endif
    config.rsp_callbacks.get_rsp_microcode = get_ucode;
    config.renderer_callbacks.create_render_context = headless ? create_mp2_null_renderer : create_mp2_renderer;
    config.audio_callbacks = {queue_audio, audio_remaining, set_frequency};
    config.input_callbacks.get_input = get_input;
    config.input_callbacks.get_connected_device_info = connected;
    config.gfx_callbacks.update_gfx = update_gfx;
    config.events_callbacks.vi_callback = vi_callback;
    config.error_handling_callbacks.message_box = message_box;
    recomp::start_game(game_id, "");
    recomp::start(config);
    if (audio_device) SDL_CloseAudioDevice(audio_device);
    if (session && netplay::game::failed() && !headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Multiplayer session ended", session->status().message.c_str(), window);
    settings::shutdown();
    SDL_DestroyWindow(window);
    session.reset();steam_social.reset();
    // The runtime has process-global simulation/thread state. Replace the
    // process after orderly shutdown to boot a synchronized room from frame 0.
    auto load_state=mp2::checkpoint::requested_load();
    if (requested_room || !load_state.empty()) {
        char* base=SDL_GetBasePath();
        auto executable=base?std::filesystem::u8path(base)/std::filesystem::u8path(argv[0]).filename():std::filesystem::absolute(std::filesystem::u8path(argv[0]));
        SDL_free(base);
        std::vector<std::string> arguments{utf8_path(executable),"--rom",utf8_path(*rom),
            "--data-dir",utf8_path(std::filesystem::absolute(data_directory))};
        if(requested_room) {
            arguments.insert(arguments.end(),{"--netplay-connect","--host-address",requested_room->address,"--host-port",std::to_string(requested_room->port),"--netplay-transport",requested_room->transport==netplay::Transport::Steam?"steam":"direct"});
            if(!requested_room->code.empty()){arguments.push_back("--join");arguments.push_back(requested_room->code);}
        } else arguments.insert(arguments.end(),{"--debug-checkpoints","--debug-load",utf8_path(load_state)});
        if(headless)arguments.push_back("--headless");
        if(debug_controls)arguments.push_back("--debug-controls");
        if(!debug_directory.empty()){arguments.push_back("--debug-dir");arguments.push_back(utf8_path(std::filesystem::absolute(debug_directory)));}
        if(bot)arguments.push_back("--netplay-bot");
        if(stop_frame){arguments.push_back("--netplay-frames");arguments.push_back(std::to_string(stop_frame));}
        mp2::debug::shutdown();
        SDL_Quit();
#ifdef _WIN32
        std::wstring command;
        for(const auto& arg:arguments){if(!command.empty())command+=L' ';command+=quote_argument(std::filesystem::u8path(arg).wstring());}
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        PROCESS_INFORMATION child{};
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child))
            throw std::runtime_error("Cannot restart game: "+std::system_category().message(GetLastError()));
        CloseHandle(child.hThread);CloseHandle(child.hProcess);
        return 0;
#else
        std::vector<char*> pointers;
        for(auto& arg:arguments)pointers.push_back(arg.data());
        pointers.push_back(nullptr);
        execv(executable.c_str(),pointers.data());
#endif
        throw std::runtime_error(std::string("Cannot restart game: ")+std::strerror(errno));
    }
    mp2::debug::shutdown();
    SDL_Quit();
    return netplay::game::failed() || mp2::checkpoint::status()["failed"].get<bool>() ? 1 : 0;
} catch (const std::exception& e) {
    fprintf(stderr, "[MP2] %s\n", e.what());
    return 1;
}

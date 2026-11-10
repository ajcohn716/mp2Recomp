#include "debug.hpp"
#include "debug_state.hpp"
#include "netplay_game.hpp"
#include "settings.hpp"
#include "stb/stb_image_write.h"
#include "ultramodern/ultramodern.hpp"
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <process.h>
#define MP2_DUP _dup
#define MP2_DUP2 _dup2
#define MP2_CLOSE _close
#define MP2_READ _read
#define MP2_WRITE _write
#else
#include <unistd.h>
#define MP2_DUP dup
#define MP2_DUP2 dup2
#define MP2_CLOSE close
#define MP2_READ read
#define MP2_WRITE write
#endif
namespace mp2::debug {
namespace {
using Clock = std::chrono::steady_clock;
struct Image {
    FrameRequest request;
    unsigned width, height;
    std::vector<uint8_t> rgba;
};
struct Pad {
    unsigned start, end;
    uint16_t buttons;
    float x, y;
};
struct State {
    std::filesystem::path directory;
    bool controls{}, headless{}, initialized{};
    std::atomic<unsigned> vi{}, pending_frames{};
    Clock::time_point started = Clock::now(), next_poll{}, next_status{}, capture_end{}, next_image{};
    unsigned fps = 10, capture_id{}, image_id{}, rate{}, audio_part{};
    size_t image_bytes{};
    uint64_t audio_frames{}, dropped{};
    std::mutex mutex, writer_mutex;
    std::condition_variable condition;
    std::deque<Image> images;
    std::deque<FrameRequest> snapshots;
    std::vector<Pad> pads;
    bool writer_stop{};
    std::thread writer, logger;
    int stdout_fd = -1, stderr_fd = -1, pipe_read = -1;
    Json context = Json::object();
    std::ofstream events, frame_index, wav;
    std::filesystem::path capture_dir, trace_request;
};
State &state() {
    static auto *s = new State;
    return *s;
}
double elapsed() { return std::chrono::duration<double>(Clock::now() - state().started).count(); }
void atomic_json(const std::filesystem::path &path, const Json &data) {
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out << data.dump(2) << '\n';
    }
#ifdef _WIN32
    // Windows rename doesn't replace an existing file. Readers retry the small
    // gap.
    std::error_code ec;
    std::filesystem::remove(path, ec);
#endif
    std::filesystem::rename(temporary, path);
}
Json status_locked() {
    auto &s = state();
    return {{"schema", 1},
            {"session", s.directory.string()},
            {"elapsed_seconds", elapsed()},
            {"vi", s.vi.load()},
            {"headless", s.headless},
            {"controls", s.controls},
            {"capture_active", s.capture_end != Clock::time_point()},
            {"capture_directory", s.capture_dir.string()},
            {"pending_frames", s.pending_frames.load()},
            {"capture_fps", s.fps},
            {"audio_rate", s.rate},
            {"dropped_frames", s.dropped},
            {"renderer", s.context},
            {"checkpoint", checkpoint::status()},
            {"graphics",
             {{"resolution", settings::graphics().resolution},
              {"aspect", settings::graphics().aspect},
              {"filtering", settings::graphics().filtering}}}};
}
void emit_locked(const std::string &type, Json values) {
    auto &s = state();
    if (!s.initialized)
        return;
    values["event"] = type;
    values["seconds"] = elapsed();
    values["vi"] = s.vi.load();
    s.events << values.dump() << '\n';
    s.events.flush();
}
void u32(std::ostream &out, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        out.put(char(n >> (8 * i)));
}
void u16(std::ostream &out, uint16_t n) {
    out.put(char(n));
    out.put(char(n >> 8));
}
void close_audio_locked() {
    auto &s = state();
    if (!s.wav.is_open())
        return;
    s.wav.seekp(4);
    u32(s.wav, 36 + uint32_t(s.audio_frames * 4));
    s.wav.seekp(40);
    u32(s.wav, uint32_t(s.audio_frames * 4));
    s.wav.close();
}
void open_audio_locked() {
    auto &s = state();
    if (!s.rate || s.capture_end == Clock::time_point())
        return;
    auto path = s.capture_dir / ("audio-" + std::to_string(++s.audio_part) + ".wav");
    s.wav.open(path, std::ios::binary);
    if (!s.wav)
        throw std::runtime_error("Cannot create audio dump");
    s.audio_frames = 0;
    s.wav.write("RIFF", 4);
    u32(s.wav, 36);
    s.wav.write("WAVEfmt ", 8);
    u32(s.wav, 16);
    u16(s.wav, 1);
    u16(s.wav, 2);
    u32(s.wav, s.rate);
    u32(s.wav, s.rate * 4);
    u16(s.wav, 4);
    u16(s.wav, 16);
    s.wav.write("data", 4);
    u32(s.wav, 0);
    emit_locked("audio_segment", {{"path", path.string()}, {"rate", s.rate}});
}
void finish_locked(const char *reason) {
    auto &s = state();
    if (s.capture_end == Clock::time_point())
        return;
    close_audio_locked();
    s.capture_end = {};
    emit_locked("capture_finished",
                {{"reason", reason}, {"directory", s.capture_dir.string()}, {"dropped_frames", s.dropped}});
}
void capture_locked(unsigned seconds, unsigned fps) {
    auto &s = state();
    if (seconds < 1 || seconds > 60 || fps < 1 || fps > 30)
        throw std::runtime_error("Capture requires seconds 1..60 and fps 1..30");
    if (s.capture_end != Clock::time_point())
        throw std::runtime_error("A capture is already active");
    s.capture_dir = s.directory / ("capture-" + std::to_string(++s.capture_id));
    std::filesystem::create_directory(s.capture_dir);
    s.capture_end = Clock::now() + std::chrono::seconds(seconds);
    s.next_image = {};
    s.fps = fps;
    s.image_id = 0;
    s.audio_part = 0;
    s.dropped = 0;
    atomic_json(s.capture_dir / "manifest.json",
                {{"schema", 1},
                 {"seconds", seconds},
                 {"fps", fps},
                 {"started_seconds", elapsed()},
                 {"started_vi", s.vi.load()},
                 {"audio", "stereo PCM16, after volume/mute, before SDL queue; "
                           "segments on rate changes"},
                 {"frames", "PNG of presented game window; frames.jsonl contains "
                            "actual timestamps (not fixed-rate video)"}});
    open_audio_locked();
    emit_locked("capture_started",
                {{"directory", s.capture_dir.string()}, {"seconds", seconds}, {"fps", fps}});
}
void write_images() {
    auto &s = state();
    for (;;) {
        Image job;
        {
            std::unique_lock lock(s.writer_mutex);
            s.condition.wait(lock, [&] { return s.writer_stop || !s.images.empty(); });
            if (s.images.empty())
                break;
            job = std::move(s.images.front());
            s.images.pop_front();
            s.image_bytes -= job.rgba.size();
        }
        auto &meta = job.request.metadata;
        meta["path"] = job.request.path.string();
        meta["width"] = job.width;
        meta["height"] = job.height;
        // Use callback I/O for native filesystem paths on Windows as well.
        std::ofstream out(job.request.path, std::ios::binary);
        auto write = [](void *context, void *data, int size) {
            static_cast<std::ofstream *>(context)->write(static_cast<char *>(data), size);
        };
        bool ok = bool(out) && stbi_write_png_to_func(write, &out, int(job.width), int(job.height), 4,
                                                      job.rgba.data(), int(job.width * 4));
        out.close();
        ok = ok && !out.fail();
        meta["ok"] = ok;
        if (!ok)
            meta["error"] = "PNG encoding or write failed";
        try {
            atomic_json(job.request.path.string() + ".json", meta);
        } catch (const std::exception &e) {
            ok = false;
            meta["error"] = e.what();
        }
        std::lock_guard lock(s.mutex);
        meta["ok"] = ok;
        --s.pending_frames;
        s.frame_index << meta.dump() << '\n';
        s.frame_index.flush();
        emit_locked(ok ? "frame_written" : "frame_error", meta);
    }
}
void tee_logs() {
    auto &s = state();
    auto path = s.directory / "runtime.log";
    std::ofstream out(path, std::ios::binary | std::ios::app);
    size_t bytes = 0;
    char data[4096];
    for (;;) {
        int n = MP2_READ(s.pipe_read, data, sizeof(data));
        if (n <= 0)
            break;
        // Persist each chunk before mirroring it to the original console.
        out.write(data, n);
        out.flush();
        bytes += n;
        MP2_WRITE(s.stderr_fd, data, n);
        if (bytes >= 8 * 1024 * 1024) {
            out.close();
            std::error_code ec;
            std::filesystem::remove(path.string() + ".3", ec);
            for (int i = 2; i >= 1; --i)
                std::filesystem::rename(path.string() + "." + std::to_string(i),
                                        path.string() + "." + std::to_string(i + 1), ec);
            std::filesystem::rename(path, path.string() + ".1", ec);
            out.open(path, std::ios::binary | std::ios::trunc);
            bytes = 0;
        }
    }
}
} // namespace
void initialize(const std::filesystem::path &root, bool controls, bool headless) {
    auto &s = state();
    if (s.initialized)
        return;
    std::filesystem::create_directories(root);
    auto stamp = std::chrono::system_clock::now().time_since_epoch();
    s.directory =
        std::filesystem::absolute(root) /
        ("session-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(stamp).count()) +
         "-" +
         std::to_string(
#ifdef _WIN32
             _getpid()
#else
             getpid()
#endif
                 ));
    std::filesystem::create_directories(s.directory / "commands");
    std::filesystem::create_directory(s.directory / "responses");
    s.events.open(s.directory / "events.jsonl");
    s.frame_index.open(s.directory / "frames.jsonl");
    if (!s.events || !s.frame_index)
        throw std::runtime_error("Cannot create debug logs");
    s.controls = controls;
    s.headless = headless;
    s.initialized = true;
    s.started = Clock::now();
    atomic_json(s.directory / "session.json", {{"schema", 1},
                                               {"pid",
#ifdef _WIN32
                                                _getpid()
#else
                                                getpid()
#endif
                                               },
                                               {"controls", controls},
                                               {"headless", headless},
                                               {"sdl_version", SDL_GetRevision()}});
    atomic_json(std::filesystem::absolute(root) / "latest.json",
                {{"schema", 1}, {"session", s.directory.string()}});
    fprintf(stderr, "[DEBUG] Session: %s\n", s.directory.string().c_str());
    fflush(nullptr);
    int descriptors[2];
#ifdef _WIN32
    if (_pipe(descriptors, 65536, _O_BINARY) < 0)
        throw std::runtime_error("Cannot create log pipe");
#else
    if (pipe(descriptors) < 0)
        throw std::runtime_error("Cannot create log pipe");
#endif
    s.stdout_fd = MP2_DUP(1);
    s.stderr_fd = MP2_DUP(2);
    s.pipe_read = descriptors[0];
    if (s.stdout_fd < 0 || s.stderr_fd < 0 || MP2_DUP2(descriptors[1], 1) < 0 ||
        MP2_DUP2(descriptors[1], 2) < 0)
        throw std::runtime_error("Cannot persist console logs");
    MP2_CLOSE(descriptors[1]);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    s.logger = std::thread(tee_logs);
    s.writer = std::thread(write_images);
    std::atexit(shutdown);
    fprintf(stderr, "[DEBUG] Persistent diagnostics active; controls=%d\n", controls);
    event("session_started");
}
void shutdown() {
    auto &s = state();
    if (!s.initialized)
        return;
    {
        std::lock_guard lock(s.mutex);
        finish_locked("shutdown");
        s.pads.clear();
    }
    {
        std::lock_guard lock(s.writer_mutex);
        s.writer_stop = true;
    }
    s.condition.notify_all();
    if (s.writer.joinable())
        s.writer.join();
    {
        std::lock_guard lock(s.mutex);
        emit_locked("session_finished", Json::object());
        try {
            auto status = status_locked();
            status["stopped"] = true;
            atomic_json(s.directory / "status.json", status);
        } catch (...) {
        }
        s.initialized = false;
    }
    fflush(nullptr);
    MP2_DUP2(s.stdout_fd, 1);
    MP2_DUP2(s.stderr_fd, 2);
    MP2_CLOSE(s.stdout_fd);
    MP2_CLOSE(s.stderr_fd);
    if (s.logger.joinable())
        s.logger.join();
    MP2_CLOSE(s.pipe_read);
}
void event(const std::string &type, Json values) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    emit_locked(type, std::move(values));
}
void telemetry(Json values) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    s.context.update(values);
}
void vi(unsigned count) { state().vi = count; }
void input(uint16_t &buttons, float &x, float &y) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    unsigned vi = s.vi.load();
    std::erase_if(s.pads, [&](const Pad &p) { return p.end <= vi; });
    for (auto &p : s.pads)
        if (p.start <= vi) {
            buttons |= p.buttons;
            x = p.x;
            y = p.y;
        }
}
void audio_rate(unsigned rate) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    if (s.rate == rate)
        return;
    close_audio_locked();
    s.rate = rate;
    open_audio_locked();
}
void audio(const int16_t *samples, size_t count) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    if (s.capture_end == Clock::time_point())
        return;
    if (Clock::now() >= s.capture_end) {
        finish_locked("duration");
        return;
    }
    if (s.wav.is_open()) { // Portable little-endian WAV, including big-endian
                           // hosts.
        for (size_t i = 0; i < count; ++i)
            u16(s.wav, uint16_t(samples[i]));
        s.audio_frames += count / 2;
        // Keep the header usable if the application crashes mid-capture.
        auto end = s.wav.tellp();
        s.wav.seekp(4);
        u32(s.wav, 36 + uint32_t(s.audio_frames * 4));
        s.wav.seekp(40);
        u32(s.wav, uint32_t(s.audio_frames * 4));
        s.wav.seekp(end);
        s.wav.flush();
    }
}
void request_capture(unsigned seconds, unsigned fps) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    try {
        capture_locked(seconds, fps);
    } catch (const std::exception &e) {
        emit_locked("command_error", {{"error", e.what()}});
    }
}
bool next_frame(FrameRequest &request) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    if (!s.initialized)
        return false;
    auto now = Clock::now();
    if (s.capture_end != Clock::time_point() && now >= s.capture_end)
        finish_locked("duration");
    if (!s.snapshots.empty()) {
        request = std::move(s.snapshots.front());
        s.snapshots.pop_front();
    } else if (s.capture_end != Clock::time_point() && now >= s.next_image) {
        s.next_image = now + std::chrono::microseconds(1000000 / s.fps);
        request.path = s.capture_dir / ("frame-" + std::to_string(++s.image_id) + ".png");
    } else
        return false;
    ++s.pending_frames;
    request.metadata = {{"seconds", elapsed()},
                        {"vi", s.vi.load()},
                        {"renderer", s.context},
                        {"graphics",
                         {{"resolution", settings::graphics().resolution},
                          {"aspect", settings::graphics().aspect},
                          {"filtering", settings::graphics().filtering}}}};
    return true;
}
void frame_error(const FrameRequest &request, const std::string &error) {
    --state().pending_frames;
    event("frame_error", {{"path", request.path.string()}, {"error", error}});
}
void frame_ready(FrameRequest request, unsigned width, unsigned height, std::vector<uint8_t> rgba) {
    auto &s = state();
    bool dropped = false;
    {
        std::lock_guard lock(s.writer_mutex);
        if (s.image_bytes + rgba.size() > 64 * 1024 * 1024 || s.writer_stop)
            dropped = true;
        else {
            s.image_bytes += rgba.size();
            s.images.push_back({std::move(request), width, height, std::move(rgba)});
        }
    }
    if (dropped) {
        std::lock_guard lock(s.mutex);
        ++s.dropped;
        --s.pending_frames;
        emit_locked("frame_dropped", {{"path", request.path.string()}, {"reason", "writer queue full"}});
    } else
        s.condition.notify_one();
}
bool needs_present() {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    return !s.snapshots.empty() || s.capture_end != Clock::time_point() || s.pending_frames.load() != 0;
}
bool next_trace(std::filesystem::path &path) {
    auto &s = state();
    std::lock_guard lock(s.mutex);
    if (s.trace_request.empty())
        return false;
    path = std::move(s.trace_request);
    s.trace_request.clear();
    return true;
}
void write_trace(const std::filesystem::path &path, Json data) {
    atomic_json(path, data);
    event("trace_written", {{"path", path.string()}});
}
void quick_save() {
    try {
        auto path = state().directory / ("state-" + std::to_string(state().vi.load()) + ".json");
        checkpoint::save(path);
        atomic_json(state().directory.parent_path() / "latest-state.json", {{"path", path.string()}});
    } catch (const std::exception &e) {
        event("state_error", {{"error", e.what()}});
    }
}
void quick_load() {
    try {
        std::ifstream in(state().directory.parent_path() / "latest-state.json");
        Json pointer = Json::parse(in);
        checkpoint::load(pointer.at("path").get<std::string>());
    } catch (const std::exception &e) {
        event("state_error", {{"error", e.what()}});
    }
}
void poll() {
    auto &s = state();
    if (!s.initialized)
        return;
    auto now = Clock::now();
    if (now < s.next_poll)
        return;
    s.next_poll = now + std::chrono::milliseconds(100);
    try {
        std::lock_guard lock(s.mutex);
        if (s.capture_end != Clock::time_point() && now >= s.capture_end)
            finish_locked("duration");
        if (s.controls)
            for (const auto &entry : std::filesystem::directory_iterator(s.directory / "commands")) {
                if (entry.path().extension() != ".json" || !entry.is_regular_file())
                    continue;
                Json response;
                bool quit = false;
                try {
                    if (entry.file_size() > 16384)
                        throw std::runtime_error("Command exceeds 16 KiB");
                    std::ifstream in(entry.path());
                    Json command = Json::parse(in);
                    std::string op = command.at("op");
                    response = {{"ok", true}, {"op", op}};
                    if (op == "status")
                        response["status"] = status_locked();
                    else if (op == "capture") {
                        capture_locked(command.value("seconds", 15u), command.value("fps", 10u));
                        response["directory"] = s.capture_dir.string();
                    } else if (op == "stop_capture")
                        finish_locked("command");
                    else if (op == "screenshot") {
                        if (s.headless)
                            throw std::runtime_error("Screenshots require the Vulkan renderer");
                        if (s.snapshots.size() >= 8)
                            throw std::runtime_error("Too many pending screenshots");
                        auto path = s.directory / ("screenshot-" + entry.path().stem().string() + ".png");
                        s.snapshots.push_back({path, {}});
                        response["path"] = path.string();
                    } else if (op == "trace") {
                        if (s.headless)
                            throw std::runtime_error("Trace requires the Vulkan renderer");
                        if (!s.trace_request.empty())
                            throw std::runtime_error("Trace already pending");
                        s.trace_request = s.directory / ("trace-" + entry.path().stem().string() + ".json");
                        response["path"] = s.trace_request.string();
                    } else if (op == "save_state") {
                        auto path = s.directory / ("state-" + entry.path().stem().string() + ".json");
                        checkpoint::save(path);
                        response["path"] = path.string();
                        atomic_json(s.directory.parent_path() / "latest-state.json",
                                    {{"path", path.string()}});
                    } else if (op == "load_state") {
                        checkpoint::load(command.at("path").get<std::string>());
                        response["restarting"] = true;
                    } else if (op == "pause")
                        checkpoint::pause();
                    else if (op == "step")
                        checkpoint::step(command.value("frames", 1u));
                    else if (op == "resume")
                        checkpoint::resume();
                    else if (op == "input") {
                        unsigned duration = command.value("frames", 4u),
                                 start = command.value("at_vi", s.vi.load());
                        unsigned b = command.value("buttons", 0u);
                        float x = command.value("x", 0.f), y = command.value("y", 0.f);
                        if (duration < 1 || duration > 3600 || b > 65535 || x < -1 || x > 1 || y < -1 ||
                            y > 1 || !std::isfinite(x) || !std::isfinite(y) ||
                            start > UINT32_MAX - duration || s.pads.size() >= 1024)
                            throw std::runtime_error(
                                "Input requires frames 1..3600, buttons 0..65535, x/y -1..1 "
                                "and a valid VI range");
                        if (netplay::game::active())
                            throw std::runtime_error("Debug input is disabled during netplay/probes");
                        if (checkpoint::status()["replaying"].get<bool>())
                            throw std::runtime_error("Wait for checkpoint replay to finish");
                        checkpoint::resume();
                        s.pads.push_back({start, start + duration, uint16_t(b), x, y});
                        response["start_vi"] = start;
                        response["end_vi"] = start + duration;
                    } else if (op == "quit")
                        quit = true;
                    else
                        throw std::runtime_error("Unknown operation: " + op);
                    emit_locked("command", command);
                } catch (const std::exception &e) {
                    response = {{"ok", false}, {"error", e.what()}};
                }
                atomic_json(s.directory / "responses" / entry.path().filename(), response);
                std::filesystem::remove(entry.path());
                if (quit) {
                    netplay::game::cancel();
                    ultramodern::quit();
                }
            }
        if (now >= s.next_status) {
            s.next_status = now + std::chrono::seconds(1);
            atomic_json(s.directory / "status.json", status_locked());
        }
    } catch (const std::exception &e) {
        event("debug_error", {{"error", e.what()}});
    }
}
} // namespace mp2::debug

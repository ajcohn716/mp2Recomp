#include "debug_state.hpp"
#include "guest_memory.hpp"
#include "netplay_game.hpp"
#include "ultramodern/ultramodern.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

void convert_to_n64_range(float, float, int8_t &, int8_t &);
namespace mp2::checkpoint {
namespace {
constexpr uint32_t seed = 0x4D503244;
std::atomic<bool> active{}, paused{}, cancelled{}, replaying{}, failed{};
std::atomic<uint64_t> tick{}, pause_after{UINT64_MAX};
std::mutex mutex;
netplay::Pad pad;
float local_x{}, local_y{};
Json history = Json::array(), replay, initial_save;
std::filesystem::path save_request, load_request;
uint64_t target{}, target_hash{};
Json read(const std::filesystem::path &path) {
    if (std::filesystem::file_size(path) > 128 * 1024 * 1024)
        throw std::runtime_error("Checkpoint exceeds 128 MiB");
    std::ifstream in(path);
    auto data = Json::parse(in);
    if (data.at("schema") != 1 || data.at("kind") != "mp2-input-checkpoint" || data.at("seed") != seed)
        throw std::runtime_error("Incompatible checkpoint");
    if (data.at("rom") != "d2dbfb3b4d7a5e81" || data.at("inputs").size() != data.at("frame").get<uint64_t>())
        throw std::runtime_error("Invalid checkpoint ROM or frame count");
    auto initial = data.at("initial_save").get<std::vector<uint8_t>>();
    if (!initial.empty() && initial.size() != 512)
        throw std::runtime_error("Invalid checkpoint EEPROM");
    for (const auto &row : data.at("inputs")) {
        if (!row.is_array() || row.size() != 4)
            throw std::runtime_error("Invalid checkpoint input");
        row[0].get<uint64_t>();
        row[1].get<uint16_t>();
        row[2].get<int8_t>();
        row[3].get<int8_t>();
    }
    data.at("hash").get<uint64_t>();
    return data;
}
void write(const std::filesystem::path &path, const Json &data) {
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out << data.dump() << '\n';
    }
    std::filesystem::rename(temp, path);
}
} // namespace
void configure(bool enable, const std::filesystem::path &load, const std::filesystem::path &save_dir) {
    active = enable || !load.empty();
    if (!active)
        return;
    if (!load.empty()) {
        auto data = read(load);
        replay = data.at("inputs");
        target = data.at("frame");
        target_hash = data.at("hash");
        initial_save = data.at("initial_save");
        replaying = true;
        // The caller provides an isolated directory, so loading never rewinds
        // the player's ordinary EEPROM file.
        std::filesystem::create_directories(save_dir);
        auto bytes = initial_save.get<std::vector<uint8_t>>();
        if (!bytes.empty()) {
            std::ofstream out(save_dir / "mp2.us.bin", std::ios::binary);
            out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            if (!out)
                throw std::runtime_error("Cannot restore checkpoint EEPROM");
        }
    } else {
        std::ifstream in(save_dir / "mp2.us.bin", std::ios::binary);
        std::vector<uint8_t> bytes;
        if (in)
            bytes.assign(std::istreambuf_iterator<char>(in), {});
        if (!bytes.empty() && bytes.size() != 512)
            throw std::runtime_error("Invalid initial EEPROM");
        initial_save = bytes;
    }
    debug::event("checkpoint_mode", {{"replaying", replaying.load()}, {"target", target}});
}
bool enabled() { return active.load(); }
uint64_t frame() { return tick.load(); }
void initialize_memory(uint8_t *rdram) {
    if (active)
        MEM_W(0, 0x800C99B4) = seed;
}
void local_input(uint16_t buttons, float x, float y) {
    std::lock_guard lock(mutex);
    pad = {buttons};
    local_x = x;
    local_y = y;
}
Json status() {
    return {{"enabled", active.load()}, {"frame", tick.load()},    {"replaying", replaying.load()},
            {"target_frame", target},   {"paused", paused.load()}, {"failed", failed.load()}};
}
void save(const std::filesystem::path &path) {
    if (!active)
        throw std::runtime_error("Launch with --debug-checkpoints to capture loadable states");
    std::lock_guard lock(mutex);
    if (!save_request.empty())
        throw std::runtime_error("Checkpoint save already pending");
    save_request = path;
}
void load(const std::filesystem::path &path) {
    read(path);
    std::lock_guard lock(mutex);
    load_request = std::filesystem::absolute(path);
    cancelled = true;
    ultramodern::quit();
}
std::filesystem::path requested_load() {
    std::lock_guard lock(mutex);
    return load_request;
}
bool is_paused() { return paused.load(); }
void pause() {
    if (!active || replaying)
        throw std::runtime_error("Pause requires checkpoint mode after replay finishes");
    paused = true;
}
void step(unsigned frames) {
    if (!active || !paused || frames < 1 || frames > 600)
        throw std::runtime_error("Step requires a paused checkpoint and 1..600 frames");
    pause_after = tick.load() + frames;
    paused = false;
}
void resume() {
    pause_after = UINT64_MAX;
    paused = false;
}
void cancel() {
    cancelled = true;
    paused = false;
}
void latch(uint8_t *rdram) {
    if (!active)
        return;
    try {
        uint64_t f = tick.load(), hash = netplay::game::checksum(rdram);
        if (f >= pause_after) {
            paused = true;
            pause_after = UINT64_MAX;
        }
        if (replaying && f == target) {
            if (hash != target_hash)
                throw std::runtime_error("Checkpoint final state hash mismatch");
            replaying = false;
            paused = true;
            debug::event("state_loaded", {{"frame", f}, {"hash", hash}, {"verified", true}});
        }
        auto save_at_boundary = [&] {
            std::filesystem::path output;
            {
                std::lock_guard lock(mutex);
                output = std::move(save_request);
                save_request.clear();
            }
            if (!output.empty()) {
                write(output, {{"schema", 1},
                               {"kind", "mp2-input-checkpoint"},
                               {"rom", "d2dbfb3b4d7a5e81"},
                               {"seed", seed},
                               {"frame", f},
                               {"hash", hash},
                               {"initial_save", initial_save},
                               {"inputs", history}});
                debug::event("state_saved", {{"path", output.string()}, {"frame", f}, {"hash", hash}});
            }
        };
        save_at_boundary();
        while (paused && !cancelled) {
            save_at_boundary();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (cancelled)
            return;
        netplay::Pad current;
        if (replaying) {
            const auto &row = replay.at(f);
            if (row[0].get<uint64_t>() != hash)
                throw std::runtime_error("Checkpoint diverged at simulation frame " + std::to_string(f));
            current = {row[1].get<uint16_t>(), row[2].get<int8_t>(), row[3].get<int8_t>()};
        } else {
            {
                std::lock_guard lock(mutex);
                current = pad;
            }
            uint16_t buttons = current.buttons;
            float x, y;
            {
                std::lock_guard lock(mutex);
                x = local_x;
                y = local_y;
            }
            debug::input(buttons, x, y);
            current.buttons = buttons;
            convert_to_n64_range(x, y, current.x, current.y);
        }
        history.push_back({hash, current.buttons, current.x, current.y});
        MEM_H(0, 0x800D8180) = 1;
        MEM_H(0, 0x800D8182) = 0;
        MEM_H(0, 0x800D8184) = 1;
        for (int p = 0; p < 4; ++p) {
            uint32_t address = 0x800D8040 + 6 * p;
            MEM_H(0, address) = p ? 0 : current.buttons;
            MEM_B(2, address) = p ? 0 : current.x;
            MEM_B(3, address) = p ? 0 : current.y;
            MEM_B(4, address) = p ? 8 : 0;
        }
        tick = f + 1;
    } catch (const std::exception &e) {
        failed = true;
        debug::event("state_error", {{"error", e.what()}, {"frame", tick.load()}});
        cancelled = true;
        ultramodern::quit();
    }
}
} // namespace mp2::checkpoint

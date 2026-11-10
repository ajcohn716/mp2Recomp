#include <memory>
#include "hle/rt64_application.h"
#include "hle/rt64_rsp.h"
#include "ultramodern/ultramodern.hpp"

namespace {
uint8_t dmem_gfx[4096]{}, imem_gfx[4096]{}, header[64]{};
uint32_t mi{}, dpc[8]{};
void check_interrupts() {}

class MP2Renderer final : public ultramodern::renderer::RendererContext {
    std::unique_ptr<RT64::Application> app;
public:
    MP2Renderer(uint8_t* rdram, SDL_Window* window) {
        RT64::Application::Core core{};
        core.window = window;
        core.HEADER = header;
        core.RDRAM = rdram;
        core.DMEM = dmem_gfx;
        core.IMEM = imem_gfx;
        core.MI_INTR_REG = &mi;
        core.DPC_START_REG = &dpc[0]; core.DPC_END_REG = &dpc[1];
        core.DPC_CURRENT_REG = &dpc[2]; core.DPC_STATUS_REG = &dpc[3];
        core.DPC_CLOCK_REG = &dpc[4]; core.DPC_BUFBUSY_REG = &dpc[5];
        core.DPC_PIPEBUSY_REG = &dpc[6]; core.DPC_TMEM_REG = &dpc[7];
        core.checkInterrupts = check_interrupts;
        auto* vi = ultramodern::renderer::get_vi_regs();
#define VI_REG(name) core.name = &vi->name
        VI_REG(VI_STATUS_REG); VI_REG(VI_ORIGIN_REG); VI_REG(VI_WIDTH_REG);
        VI_REG(VI_INTR_REG); VI_REG(VI_V_CURRENT_LINE_REG); VI_REG(VI_TIMING_REG);
        VI_REG(VI_V_SYNC_REG); VI_REG(VI_H_SYNC_REG); VI_REG(VI_LEAP_REG);
        VI_REG(VI_H_START_REG); VI_REG(VI_V_START_REG); VI_REG(VI_V_BURST_REG);
        VI_REG(VI_X_SCALE_REG); VI_REG(VI_Y_SCALE_REG);
#undef VI_REG
        RT64::ApplicationConfiguration config;
        config.appId = "mp2recomp";
        config.useConfigurationFile = false;
        app = std::make_unique<RT64::Application>(core, config);
        app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
        app->userConfig.resolution = RT64::UserConfiguration::Resolution::Original;
        app->userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Original;
        app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
        auto result = app->setup(0);
        setup_result = static_cast<ultramodern::renderer::SetupResult>(result);
        chosen_api = ultramodern::renderer::GraphicsApi::Vulkan;
        fprintf(stderr, "[MP2] RT64 setup result: %d\n", int(result));
        if (result != RT64::Application::SetupResult::Success) app.reset();
    }
    bool valid() override { return app != nullptr; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
    void send_dl(const OSTask* task) override {
        static unsigned count = 0;
        if (++count <= 5 || count % 300 == 0) fprintf(stderr, "[MP2] graphics task %u, dl=%08x\n", count, task->t.data_ptr);
        app->state->rsp->reset();
        app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
        app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
    }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override { app->updateScreen(); }
    void shutdown() override { if (app) app->end(); }
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
};
}
std::unique_ptr<ultramodern::renderer::RendererContext> create_mp2_renderer(uint8_t* rdram, SDL_Window* window, bool) {
    return std::make_unique<MP2Renderer>(rdram, window);
}

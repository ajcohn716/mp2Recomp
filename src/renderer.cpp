#include <memory>
#include "settings.hpp"
#include "debug.hpp"
#include "debug_state.hpp"
#include "debug_renderer.hpp"
#include "render_patches.hpp"
#include "gbi/rt64_gbi_rdp.h"
#include "hle/rt64_rdp.h"
#include "hle/rt64_application.h"
#include "hle/rt64_rsp.h"
#include "ultramodern/ultramodern.hpp"

namespace {
uint8_t dmem_gfx[4096]{}, imem_gfx[4096]{}, header[64]{};
uint32_t mi{}, dpc[8]{};
void check_interrupts() {}
void aligned_texrect(RT64::State* state,RT64::DisplayList** dl) {
    std::array<RT64::DisplayList,3> commands{(*dl)[0],(*dl)[1],(*dl)[2]};
    const auto& rdp=*state->rdp;
    const auto& scissor=rdp.scissorRectStack[rdp.scissorStackSize-1];
    if(mp2::align_background_tile(commands[0].w0,commands[0].w1,commands[2].w1,
        rdp.otherMode.cycleType()==G_CYC_1CYCLE,rdp.colorImage.width,rdp.colorImage.siz,
        {scissor.ulx,scissor.uly,scissor.lrx,scissor.lry})) {
        auto* local=commands.data();RT64::GBI_RDP::texrect(state,&local);*dl+=2;
    } else RT64::GBI_RDP::texrect(state,dl);
}
std::array<RT64::GBIFunction,256> trace_functions;
mp2::debug::Json trace_commands;
void trace_command(RT64::State* state,RT64::DisplayList** dl) {
    const auto& command=**dl;unsigned op=command.w0>>24;
    if(trace_commands.size()<16384) {
        const auto& scissor=state->rdp->scissorRectStack[state->rdp->scissorStackSize-1];
        trace_commands.push_back({{"address",uint32_t(reinterpret_cast<uint8_t*>(*dl)-state->RDRAM)},
            {"w0",command.w0},{"w1",command.w1},{"color_image",state->rdp->colorImage.address},
            {"cycle",state->rdp->otherMode.cycleType()},
            {"scissor_q2",{scissor.ulx,scissor.uly,scissor.lrx,scissor.lry}}});
    }
    trace_functions[op](state,dl);
}

class MP2Renderer final : public ultramodern::renderer::RendererContext {
    std::unique_ptr<RT64::Application> app;
    settings::Graphics applied;
    void configure_graphics() {
        applied=settings::graphics();
        using Config=RT64::UserConfiguration;
        app->userConfig.resolution=applied.resolution==0?Config::Resolution::WindowIntegerScale:
            applied.resolution==1?Config::Resolution::Original:Config::Resolution::Manual;
        app->userConfig.resolutionMultiplier=std::max(1,applied.resolution);
        app->userConfig.aspectRatio=applied.aspect==0?Config::AspectRatio::Original:
            applied.aspect==1?Config::AspectRatio::Manual:Config::AspectRatio::Expand;
        app->userConfig.aspectTarget=16.0/9.0;
        app->userConfig.filtering=static_cast<Config::Filtering>(applied.filtering);
    }
public:
    MP2Renderer(uint8_t* rdram, ultramodern::renderer::WindowHandle window) {
        RT64::Application::Core core{};
#ifdef _WIN32
        core.window = window.window;
#else
        core.window = window;
#endif
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
        configure_graphics();
        app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
        auto result = app->setup(
#ifdef _WIN32
            window.thread_id
#else
            0
#endif
        );
        setup_result = static_cast<ultramodern::renderer::SetupResult>(result);
        chosen_api = ultramodern::renderer::GraphicsApi::Vulkan;
        fprintf(stderr, "[MP2] RT64 setup result: %d\n", int(result));
        if (result != RT64::Application::SetupResult::Success) { app.reset(); mp2::debug::clear_render_hooks(); }
        else { mp2::debug::install_render_hooks(app.get()); const auto& device=app->device->getDescription();mp2::debug::event("graphics_device",{{"name",device.name},{"driver",device.driverVersion}}); }
    }
    bool valid() override { return app != nullptr; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
    void send_dl(const OSTask* task) override {
        if(applied!=settings::graphics()){configure_graphics();app->updateUserConfig(true);}
        static unsigned count = 0;
        if (++count <= 5 || count % 300 == 0) fprintf(stderr, "[MP2] graphics task %u, dl=%08x\n", count, task->t.data_ptr);
        app->state->rsp->reset();
        app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
        // Patch the host interpreter, never guest command memory or generated C.
        auto* task_gbi=app->interpreter->hleGBI;
        if(task_gbi->map[0xE4]==RT64::GBI_RDP::texrect)task_gbi->map[0xE4]=aligned_texrect;
        std::filesystem::path trace_path;
        bool tracing=mp2::debug::next_trace(trace_path);
        auto* gbi=app->interpreter->hleGBI;
        if(tracing) {
            trace_commands=mp2::debug::Json::array();
            for(unsigned op=0;op<256;++op){trace_functions[op]=gbi->map[op];if(gbi->map[op])gbi->map[op]=trace_command;}
        }
        app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
        if(tracing) {
            for(unsigned op=0;op<256;++op)gbi->map[op]=trace_functions[op];
            mp2::debug::write_trace(trace_path,{{"schema",1},{"task",count},{"ucode",uint32_t(gbi->ucode)},
                {"commands",std::move(trace_commands)}});
        }
        const auto& rdp=*app->state->rdp;
        const auto& scissor=rdp.scissorRectStack[rdp.scissorStackSize-1];
        mp2::debug::telemetry({{"graphics_task",count},{"display_list",task->t.data_ptr},
            {"color_image",{{"address",rdp.colorImage.address},{"width",rdp.colorImage.width},{"size",rdp.colorImage.siz}}},
            {"scissor_q2",{scissor.ulx,scissor.uly,scissor.lrx,scissor.lry}}});
    }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {
        app->updateScreen();
        if(mp2::checkpoint::is_paused()&&mp2::debug::needs_present()&&app->state->presentId>0) {
            // Unchanged VI/RAM normally suppresses presents. Re-present the
            // last completed image so screenshots/captures work while paused.
            auto& queue=*app->presentQueue;
            queue.waitForPresentId(app->state->presentId);queue.waitForIdle();
            queue.presents[queue.previousWriteCursor()].paused=true;
            queue.repeatLastPresent();
        }
    }
    void shutdown() override { if (app) app->end(); mp2::debug::clear_render_hooks(); }
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return float(std::max(1,applied.resolution)); }
};
}
std::unique_ptr<ultramodern::renderer::RendererContext> create_mp2_renderer(uint8_t* rdram, ultramodern::renderer::WindowHandle window, bool) {
    return std::make_unique<MP2Renderer>(rdram, window);
}

// Used by deterministic multi-instance checks. CPU and audio tasks still run;
// graphics are acknowledged without creating a Vulkan device.
namespace {
class NullRenderer final : public ultramodern::renderer::RendererContext {
public:
    NullRenderer() {setup_result = ultramodern::renderer::SetupResult::Success; chosen_api = ultramodern::renderer::GraphicsApi::Vulkan;}
    bool valid() override {return true;}
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override {return false;}
    void enable_instant_present() override {}
    void send_dl(const OSTask*) override {}
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {}
    void shutdown() override {}
    uint32_t get_display_framerate() const override {return 60;}
    float get_resolution_scale() const override {return 1.f;}
};
}
std::unique_ptr<ultramodern::renderer::RendererContext> create_mp2_null_renderer(uint8_t*, ultramodern::renderer::WindowHandle, bool) {
    return std::make_unique<NullRenderer>();
}

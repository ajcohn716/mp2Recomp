#pragma once
namespace RT64 {
struct Application;
}
namespace mp2::debug {
void install_render_hooks(RT64::Application *application);
void clear_render_hooks();
} // namespace mp2::debug

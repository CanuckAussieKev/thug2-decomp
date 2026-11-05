#include "app/renderer_choice.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>

#include "engine/gfx/render_device.h"
#include "engine/gfx/ui_backend.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "platform/sdl_window.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "gpu_d3d9/d3d9_context.h"
#include "render_pc/d3d9_device.h"
#include "gpu_d3d9/d3d9_ui_backend.h"
#include "gpu_d3d9/gpu_d3d9.h"

namespace app {

namespace {

// False until src/render_vulkan has a render::Device (the V6 / V7 tasks, docs/design/vulkan.md). When it does: set
// this true and make install_vulkan below install it - that is the whole change to choose it.
constexpr bool kVulkanDeviceExists = false;

#ifdef THUG2_VULKAN
constexpr bool kVulkanBuilt = true;
#else
constexpr bool kVulkanBuilt = false;
#endif

void install_direct3d9()
{
    render::install_device(render::d3d9::device());
    render::install_gpu(render::d3d9::gpu());
    render::install_gpu_context(render::d3d9::gpu_context());
    render::install_ui_backend(render::d3d9::ui_backend());
}

// The one line the Vulkan device is chosen with: its device and UI backend (src/render_vulkan).
void install_vulkan()
{
    install_direct3d9();
}

// The key's value, lower-cased.
constexpr size_t kValueSize = 16;

// (constant-initialised data: no initialiser or thread-safe static guard of the C++ runtime's)
struct ChoiceState {
    Renderer choice_ = Renderer::kDirect3D9;
    bool read_ = false;
};

ChoiceState& choice_state()
{
    static ChoiceState state;
    return state;
}

Renderer read_choice()
{
    // (main runs before the PC set-up that finds the settings file; finding it again there changes nothing)
    build_info::prepare_all();
    std::array<char, kValueSize> buffer{};
    build_info::fetch_text(build_info::kGraphics, "renderer", "d3d9", buffer.data(), buffer.size());
    std::string value(buffer.data());
    std::ranges::transform(value, value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == "vulkan") {
        if (!kVulkanBuilt) {
            get_log("renderer=vulkan ignored: this build was made without the Vulkan library (THUG2_VULKAN); using Direct3D 9");
        } else if (!kVulkanDeviceExists) {
            get_log("renderer=vulkan: the Vulkan renderer is not available yet; using Direct3D 9");
        } else {
            get_log("renderer: Vulkan");
            return Renderer::kVulkan;
        }
    } else if (value != "d3d9" && !value.empty()) {
        get_log("renderer=%s is not a renderer (d3d9 or vulkan); using Direct3D 9", value.c_str());
    }
    return Renderer::kDirect3D9;
}

} // namespace

Renderer chosen_renderer()
{
    ChoiceState& state = choice_state();
    if (!state.read_) {
        state.choice_ = read_choice();
        state.read_ = true;
    }
    return state.choice_;
}

sys::WindowApi renderer_window_api()
{
    return chosen_renderer() == Renderer::kVulkan ? sys::WindowApi::kVulkan : sys::WindowApi::kDirect3D9;
}

void install_renderer()
{
    if (chosen_renderer() == Renderer::kVulkan) {
        install_vulkan();
    } else {
        install_direct3d9();
    }
}

} // namespace app

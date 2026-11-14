// NOT ORIGINAL - thug2_vulkan_framegen_check.exe: the Vulkan Gpu's frame generation and Reflex
// (gpu_vulkan/vulkan_framegen.h) tried on this machine.
//
// The frames are made as render_pc's draw_viewport makes them - the upscaler's scale and jitter taken up, the world into
// the back buffer, rt::view_begins / opaque_view_drawn / world_view_drawn told - with a "HUD" quad drawn over the world
// after it (the 2D frame generation keeps out of its frames) and the main loop's latency markers, then presented.
//   - Off (frame_generation=off, the upscaler on): no provider, no feature made, no frame shown but the drawn ones, the
//     swapchain Vulkan's own.
//   - DLSS-G (an NVIDIA RTX 40 / 50 device with a driver that has it; else "not here"): the provider made, Reflex on,
//     frames on while the camera moves, more frames shown than drawn (the made ones), frames off under a menu (the
//     pause menu's ViewDrawn::menu) and back on, a cut (frame_blend::note_cut) resetting the history, its feature
//     released at the device's end as often as it was made.
//   - FSR (frame_generation_provider=fsr: any device with spare present queues): the same, through its own swapchain.
//   thug2_vulkan_framegen_check [--device N] [--display N] [--validation] [--mode 2x|3x|4x] [--frames N]
//                               [--only off|dlss|fsr]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan or no frame generation.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "engine/sys/frame_blend.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "platform/run_log.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuSurface;
namespace value = render::gpu_value;
using render::GpuContext;
using Rows = std::array<std::array<float, 4>, 4>;
using Floats16 = std::array<float, 16>;

constexpr uint32_t kWidth = 1280;
constexpr uint32_t kHeight = 720;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kFvf = 0x042; // XYZ | DIFFUSE
constexpr uint32_t kSkyColour = 0xFF6080A0;
constexpr float kNearZ = 2.0F;
constexpr float kFarZ = 4000.0F;
constexpr float kHalfFovTan = 0.6F;
constexpr int kMenuFrames = 6;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = false;
    std::string mode = "2x";
    int frames = 60;
    std::string only;
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--validation") {
            options.validation = true;
        } else if (argument == "--device" && has_value) {
            options.device = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--display" && has_value) {
            options.display = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--mode" && has_value) {
            options.mode = core::at(arguments, ++i);
        } else if (argument == "--frames" && has_value) {
            options.frames = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--only" && has_value) {
            options.only = core::at(arguments, ++i);
        }
    }
    return options;
}

void print_line(const std::string& line) { text::print("  %s\n", line.c_str()); }
void print_log_line(const char* line) { text::print("  log: %s\n", line); }

struct Tally {
    int passed = 0;
    int failed = 0;
    void check(bool ok, const std::string& what)
    {
        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
        text::print("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    }
};

// --- the scene ------------------------------------------------------------------------------------------------------

struct ColourVertex {
    std::array<float, 3> position{};
    uint32_t colour = 0;
};

Floats16 flat(const Rows& rows)
{
    Floats16 out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(out, (row * 4) + column) = core::at(core::at(rows, row), column);
        }
    }
    return out;
}

Rows identity()
{
    return {{{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}}};
}

// Direct3D's left-handed view from `eye` along +z turned by `yaw` (radians), and the perspective.
Rows view_of(const std::array<float, 3>& eye, float yaw)
{
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    const std::array<float, 3> right{c, 0.0F, -s};
    const std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    const std::array<float, 3> forward{s, 0.0F, c};
    const auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return (a.at(0) * b.at(0)) + (a.at(1) * b.at(1)) + (a.at(2) * b.at(2));
    };
    Rows view = identity();
    for (std::size_t row = 0; row < 3; ++row) {
        core::at(core::at(view, row), 0) = core::at(right, row);
        core::at(core::at(view, row), 1) = core::at(up, row);
        core::at(core::at(view, row), 2) = core::at(forward, row);
    }
    core::at(view, 3) = {-dot(right, eye), -dot(up, eye), -dot(forward, eye), 1.0F};
    return view;
}

Rows perspective()
{
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    Rows projection{};
    core::at(core::at(projection, 0), 0) = 1.0F / kHalfFovTan;
    core::at(core::at(projection, 1), 1) = aspect / kHalfFovTan;
    core::at(core::at(projection, 2), 2) = kFarZ / (kFarZ - kNearZ);
    core::at(core::at(projection, 2), 3) = 1.0F;
    core::at(core::at(projection, 3), 2) = -kNearZ * kFarZ / (kFarZ - kNearZ);
    return projection;
}

void quad(std::vector<ColourVertex>* out, const std::array<std::array<float, 3>, 4>& corners, uint32_t colour)
{
    for (const std::size_t corner : {0U, 1U, 2U, 0U, 2U, 3U}) {
        out->push_back({.position = core::at(corners, corner), .colour = colour});
    }
}

// A checkered ground and a row of posts (something for the motion to show).
std::vector<ColourVertex> world()
{
    std::vector<ColourVertex> out;
    constexpr int kTiles = 12;
    constexpr float kTile = 40.0F;
    for (int x = -kTiles; x < kTiles; ++x) {
        for (int z = -kTiles; z < kTiles; ++z) {
            const float left = static_cast<float>(x) * kTile;
            const float front = static_cast<float>(z) * kTile;
            quad(&out, {{{left, 0.0F, front}, {left + kTile, 0.0F, front}, {left + kTile, 0.0F, front + kTile}, {left, 0.0F, front + kTile}}},
                 ((x + z) & 1) != 0 ? 0xFF808080 : 0xFFB0B0B0);
        }
    }
    for (int post = -5; post <= 5; ++post) {
        const float x = static_cast<float>(post) * 50.0F;
        quad(&out, {{{x - 5.0F, 0.0F, 150.0F}, {x + 5.0F, 0.0F, 150.0F}, {x + 5.0F, 80.0F, 150.0F}, {x - 5.0F, 80.0F, 150.0F}}},
             0xFFC04040);
    }
    return out;
}

void common_states(render::Gpu& gpu)
{
    using render::GpuRenderState;
    using render::GpuStageState;
    gpu.set_render_state(GpuRenderState::kZEnable, 1);
    gpu.set_render_state(GpuRenderState::kZWriteEnable, 1);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_vertex_format(kFvf);
    gpu.set_texture(0, nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
}

struct Targets {
    GpuSurface* colour = nullptr; // the back buffer
    GpuSurface* depth = nullptr;
};

// One frame as the game makes it: the markers, the world (scaled and jittered as the upscaler asks), the HUD, the present.
void draw_frame(render::Gpu& gpu, const Targets& targets, int index, bool menu)
{
    GpuContext& context = render::vulkan::gpu_context();
    context.latency_marker(GpuContext::LatencyMarker::kSimulationStart);
    context.latency_marker(GpuContext::LatencyMarker::kSimulationEnd);
    context.latency_marker(GpuContext::LatencyMarker::kRenderSubmitStart);
    gpu.begin_scene();
    gpu.set_render_target(0, targets.colour);
    gpu.set_depth_surface(targets.depth);
    gpu.set_viewport({.x = 0, .y = 0, .width = kWidth, .height = kHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth | value::kClearStencil, kSkyColour, 1.0F, 0);
    const std::array<uint32_t, 4> output{0, 0, kWidth, kHeight};
    const render::rt::ViewScale scale = render::rt::view_scale();
    render::rt::ViewDrawn view;
    view.rect = scale.on ? render::rt::scaled_rect(output, scale.scale) : output;
    view.jitter = scale.on ? scale.jitter : std::array<float, 2>{};
    view.output_rect = output;
    view.depth_range = {0.0F, 1.0F};
    view.menu = menu;
    const float t = static_cast<float>(index);
    view.camera.view = view_of({t * 3.0F, 60.0F, -200.0F + (t * 2.0F)}, 0.02F * t);
    view.camera.projection = perspective();
    if (scale.on) {
        render::rt::jitter_projection(&view.camera.projection, view.jitter, core::at(view.rect, 2), core::at(view.rect, 3));
    }
    gpu.set_viewport({.x = 0, .y = 0, .width = core::at(view.rect, 2), .height = core::at(view.rect, 3), .min_z = 0.0F, .max_z = 1.0F});
    const Floats16 world_matrix = flat(identity());
    const Floats16 view_matrix = flat(view.camera.view);
    const Floats16 projection_matrix = flat(view.camera.projection);
    gpu.set_transform(render::GpuTransform::kWorld, world_matrix.data());
    gpu.set_transform(render::GpuTransform::kView, view_matrix.data());
    gpu.set_transform(render::GpuTransform::kProjection, projection_matrix.data());
    render::rt::view_begins(view);
    common_states(gpu);
    gpu.set_motion_source({});
    static const std::vector<ColourVertex> kWorld = world();
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, static_cast<uint32_t>(kWorld.size() / 3), kWorld.data(),
                         sizeof(ColourVertex));
    render::rt::opaque_view_drawn(view);
    render::rt::world_view_drawn(view);
    // the HUD: a quad in the top-left corner, in clip space, over everything
    gpu.set_viewport({.x = 0, .y = 0, .width = kWidth, .height = kHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.set_transform(render::GpuTransform::kView, world_matrix.data());
    gpu.set_transform(render::GpuTransform::kProjection, world_matrix.data());
    gpu.set_render_state(render::GpuRenderState::kZEnable, 0);
    std::vector<ColourVertex> hud;
    quad(&hud, {{{-0.95F, 0.95F, 0.5F}, {-0.5F, 0.95F, 0.5F}, {-0.5F, 0.75F, 0.5F}, {-0.95F, 0.75F, 0.5F}}}, 0xFFFFFF00);
    gpu.draw_from_memory(render::GpuPrimitive::kTriangleList, 2, hud.data(), sizeof(ColourVertex));
    context.latency_marker(GpuContext::LatencyMarker::kRenderSubmitEnd);
    gpu.end_scene();
    gpu.present();
    SDL_PumpEvents();
}

enum class Case : uint8_t { kOff, kDlss, kFsr };

const char* case_name(Case which)
{
    switch (which) {
    case Case::kOff:
        return "off";
    case Case::kDlss:
        return "DLSS-G";
    case Case::kFsr:
        return "FSR";
    }
    return "?";
}

// The device opened with the settings (as renderer_choice sets them: frame generation, then the upscaler at native).
bool open_device(const Options& options, SDL_Window* window, const render::vulkan::FrameGenSettings& framegen,
                 const render::vulkan::UpscaleSettings& upscale)
{
    render::vulkan::ContextOptions context_options{.validation = options.validation,
                                                   .device_index = options.device,
                                                   .on_message = print_line};
    render::vulkan::set_frame_generation_settings(framegen);
    render::vulkan::set_upscale_settings(upscale);
    context_options.additions = render::vulkan::upscale_context_additions();
    render::vulkan::set_context_options(context_options, window);
    GpuContext& context = render::vulkan::gpu_context();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return false;
    }
    context.configure({.width = kWidth,
                       .height = kHeight,
                       .windowed = true,
                       .vsync = false,
                       .present_mode = 2,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24S8});
    const long made = context.create_device({.width = kWidth, .height = kHeight, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
    if (made != value::kResultOk) {
        text::print("create_device failed (%ld)\n", made);
        context.shutdown();
        return false;
    }
    context.mark_ready();
    text::print("device: %s\n", context.adapter().description.data());
    return true;
}

// One case: the device made with its settings, frames drawn, the counts checked; 2 when the case can't run here.
int run_case(const Options& options, SDL_Window* window, Case which, Tally* tally)
{
    using render::vulkan::FrameGenSource;
    text::print("\n--- %s ---\n", case_name(which));
    render::vulkan::FrameGenSettings framegen;
    render::vulkan::UpscaleSettings upscale{.upscaler = render::vulkan::Upscaler::kFsr,
                                            .quality = render::vulkan::UpscaleQuality::kQuality};
    if (which != Case::kOff) {
        if (!render::vulkan::parse_frame_generation(options.mode, &framegen.mode) ||
            framegen.mode == render::vulkan::FrameGeneration::kOff) {
            framegen.mode = render::vulkan::FrameGeneration::k2x;
        }
        framegen.source = which == Case::kDlss ? FrameGenSource::kDlss : FrameGenSource::kFsr;
        upscale.upscaler = which == Case::kDlss ? render::vulkan::Upscaler::kDlss : render::vulkan::Upscaler::kFsr;
        upscale.quality = render::vulkan::UpscaleQuality::kNative;
    }
    if (!open_device(options, window, framegen, upscale)) {
        return 1;
    }
    render::Gpu& gpu = render::vulkan::gpu();
    GpuContext& context = render::vulkan::gpu_context();
    Targets targets;
    const render::GpuSurfaceDesc depth{.width = kWidth,
                                       .height = kHeight,
                                       .format = value::kFormatD24S8,
                                       .samples = 0,
                                       .quality = 0,
                                       .lockable_or_discard = false};
    const bool made = gpu.back_buffer(&targets.colour) == value::kResultOk &&
                      gpu.create_depth_surface(depth, &targets.depth) == value::kResultOk;
    tally->check(made, std::string(case_name(which)) + ": the back buffer and a depth surface");
    int result = 0;
    const render::vulkan::FrameGenCounts at_start = render::vulkan::frame_generation_counts();
    if (made && which != Case::kOff && at_start.provider == nullptr) {
        text::print("%s doesn't run on this device in this build - nothing to check\n", case_name(which));
        result = 2;
    } else if (made) {
        // moving frames
        for (int frame = 0; frame < options.frames; ++frame) {
            draw_frame(gpu, targets, frame, false);
        }
        const render::vulkan::FrameGenCounts moving = render::vulkan::frame_generation_counts();
        const auto drawn = static_cast<uint64_t>(options.frames);
        if (which == Case::kOff) {
            tally->check(moving.provider == nullptr, "off: no provider");
            tally->check(moving.features_made == 0 && moving.frames_on == 0, "off: no feature made, no frame on");
            tally->check(context.frames_shown() == 0, "off: no frame shown but the drawn ones (frames_shown 0)");
            tally->check(render::vulkan::backend().context.swapchain_owner() == nullptr, "off: the swapchain is Vulkan's own");
            tally->check(!moving.reflex, "off: Reflex isn't running");
            tally->check(render::vulkan::upscale_passes() > 0, "off: the upscaler still runs");
        } else {
            text::print("%s: %ux, %llu frames on, %llu off, %llu shown for %llu drawn, %llu made / %llu released\n",
                        moving.provider, moving.multiplier, static_cast<unsigned long long>(moving.frames_on),
                        static_cast<unsigned long long>(moving.frames_off), static_cast<unsigned long long>(moving.frames_shown),
                        static_cast<unsigned long long>(drawn), static_cast<unsigned long long>(moving.features_made),
                        static_cast<unsigned long long>(moving.features_released));
            tally->check(moving.features_made >= 1, std::string(case_name(which)) + ": its feature made");
            tally->check(moving.frames_on + 2 >= drawn, std::string(case_name(which)) + ": on for the moving frames (but the first)");
            // (DLSS-G makes no frame for a window without the focus - Streamline's "window not focused" - and the check
            // never takes it: the user may be at the machine. Then the made frames are noted, not checked.)
            const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
            if (which == Case::kDlss && !focused) {
                text::print("  note DLSS-G makes no frames while the window hasn't the focus (it hasn't: the check doesn't "
                            "take it) - %llu shown for %llu drawn\n",
                            static_cast<unsigned long long>(moving.frames_shown), static_cast<unsigned long long>(drawn));
            } else {
                tally->check(moving.frames_shown > moving.frames_on + (drawn / 4),
                             text::format("%s: more frames shown (%llu) than drawn with it on (%llu) - the made ones",
                                          case_name(which), static_cast<unsigned long long>(moving.frames_shown),
                                          static_cast<unsigned long long>(moving.frames_on)));
            }
            tally->check(context.frames_shown() == moving.frames_shown, std::string(case_name(which)) + ": GpuContext::frames_shown");
            if (which == Case::kDlss) {
                tally->check(moving.reflex, "DLSS-G: Reflex low latency on");
                tally->check(render::vulkan::backend().context.swapchain_owner() == nullptr, "DLSS-G: the swapchain is Streamline's, through Vulkan's calls");
            } else {
                tally->check(render::vulkan::backend().context.swapchain_owner() != nullptr, "FSR: the swapchain is FSR's");
            }
            // a menu over the world: off, then on again with a fresh history
            for (int frame = 0; frame < kMenuFrames; ++frame) {
                draw_frame(gpu, targets, options.frames + frame, true);
            }
            const render::vulkan::FrameGenCounts menu = render::vulkan::frame_generation_counts();
            tally->check(menu.frames_off >= moving.frames_off + kMenuFrames && menu.frames_on == moving.frames_on,
                         std::string(case_name(which)) + ": off under a menu");
            draw_frame(gpu, targets, options.frames + kMenuFrames, false);
            draw_frame(gpu, targets, options.frames + kMenuFrames + 1, false);
            const render::vulkan::FrameGenCounts back = render::vulkan::frame_generation_counts();
            tally->check(back.frames_on == menu.frames_on + 2 && back.resets == menu.resets + 1,
                         std::string(case_name(which)) + ": on again after the menu, its history reset once");
            // a cut (a teleport, a respawn): the history reset
            frame_blend::note_cut();
            draw_frame(gpu, targets, 1000, false);
            draw_frame(gpu, targets, 1001, false);
            const render::vulkan::FrameGenCounts cut = render::vulkan::frame_generation_counts();
            tally->check(cut.resets == back.resets + 1, std::string(case_name(which)) + ": a cut resets the history once");
        }
    }
    gpu.set_render_target(0, nullptr);
    gpu.set_depth_surface(nullptr);
    for (GpuSurface* surface : {targets.colour, targets.depth}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
    {
        render::vulkan::Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        render::vulkan::finish_work(state);
    }
    context.shutdown();
    const render::vulkan::FrameGenCounts end = render::vulkan::frame_generation_counts();
    if (which != Case::kOff && result == 0) {
        tally->check(end.provider == nullptr && end.features_released == end.features_made,
                     text::format("%s: its features released at the end (%llu made, %llu released)", case_name(which),
                                  static_cast<unsigned long long>(end.features_made),
                                  static_cast<unsigned long long>(end.features_released)));
    }
    render::vulkan::set_frame_generation_settings({});
    render::vulkan::set_upscale_settings({});
    return result;
}

int run(const Options& options, SDL_Window* window)
{
    Tally tally;
    int ran = 0;
    int unavailable = 0;
    for (const Case which : {Case::kOff, Case::kDlss, Case::kFsr}) {
        const std::string name = which == Case::kOff ? "off" : which == Case::kDlss ? "dlss" : "fsr";
        if (!options.only.empty() && options.only != name) {
            continue;
        }
        const int result = run_case(options, window, which, &tally);
        if (result == 2) {
            ++unavailable;
        } else if (which != Case::kOff) {
            ++ran;
        }
    }
    text::print("\n%d checks passed, %d failed\n", tally.passed, tally.failed);
    if (tally.failed != 0) {
        return 1;
    }
    return ran == 0 && unavailable > 0 ? 2 : 0;
}

int window_position(int display)
{
    int displays = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&displays);
    int chosen = display;
    if (chosen <= 0) {
        chosen = displays > 1 ? 2 : 1;
    }
    int position = static_cast<int>(SDL_WINDOWPOS_CENTERED);
    if (ids != nullptr && chosen <= displays) {
        const std::span<const SDL_DisplayID> listed(ids, static_cast<size_t>(displays));
        position = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(core::at(listed, chosen - 1)));
    }
    SDL_free(ids);
    return position;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_framegen_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    if (!render::vulkan::open_loader()) {
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    set_log_listener(print_log_line);
    SDL_SetMainReady();
    // (the window shown without taking the focus: the user may be at the machine)
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan frame generation check", static_cast<int>(kWidth), static_cast<int>(kHeight),
                                          SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_NOT_FOCUSABLE);
    if (window == nullptr) {
        text::print("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_SetWindowPosition(window, position, position);
    SDL_ShowWindow(window);
    int result = 1;
    try {
        result = run(options, window);
    } catch (const std::exception& error) {
        text::print("the check stopped: %s\n", error.what());
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}

// NOT ORIGINAL - the frame-rate counter (fps_counter.h).

#include "devtools/fps_counter.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numeric>
#include <vector>

#include "engine/ext/extension.h"
#include "engine/gfx/gpu_context.h"
#include "engine/pad_input/input_event.h"
#include "engine/sys/frame_blend.h"
#include "engine/ui/ui_backend.h"
#include "engine/ui/ui_fonts.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "platform/screenshot_files.h"
#include "platform/sdl_keys.h"
#include "platform/sdl_window.h"

namespace devtools::fps_counter {
namespace {

using Clock = std::chrono::steady_clock;

constexpr float kFontPixels = 15.0F; // (at 720 lines)
constexpr float kBaseHeight = 720.0F;
constexpr double kWindowSeconds = 1.0; // the readout's figures: the last second's frames
constexpr double kLowShare = 0.01;     // the 1% low
constexpr float kMargin = 8.0F;
constexpr float kBackgroundAlpha = 0.55F;
constexpr double kStallMs = 1000.0; // a gap longer than this (a load, a hold): not a frame

struct State {
    bool shown = false;
    int ini_value = -1; // [Graphics] show_fps as last read (-1: not read yet)
    int key = SDL_SCANCODE_F3;
    bool context_failed = false;
    ImGuiContext* context = nullptr;
    Clock::time_point last_frame{};
    bool have_last = false;
    Clock::time_point window_start{};
    std::vector<double> frame_ms;
    int ticks = 0;
    Stats shown_stats;
    Clock::time_point last_ini_read{};
    // (frame generation: the frames handed to the display, made ones too - GpuContext::frames_shown)
    uint64_t presented_at_start = 0;
    double presented_fps = 0.0;
};

State& state()
{
    static State s;
    return s;
}

// [Graphics] show_fps, read again at most once a second (the Options screen writes it); a change wins over the key.
void read_ini(Clock::time_point now)
{
    State& s = state();
    if (s.ini_value >= 0 && now - s.last_ini_read < std::chrono::seconds(1)) {
        return;
    }
    s.last_ini_read = now;
    const int value = build_info::fetch_flag(build_info::kGraphics, "show_fps", false) ? 1 : 0;
    if (value != s.ini_value) {
        s.ini_value = value;
        s.shown = value != 0;
    }
}

bool begin_context(ImGuiContext** previous)
{
    State& s = state();
    *previous = ImGui::GetCurrentContext();
    if (!s.context) {
        if (s.context_failed) {
            return false;
        }
        s.context = ImGui::CreateContext();
        ImGui::SetCurrentContext(s.context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoMouse;
        int height = 0;
        if (SDL_Window* window = sys::sdl_window()) {
            SDL_GetWindowSize(window, nullptr, &height);
        }
        const frontend::ui_fonts::CompressedFont mono = frontend::ui_fonts::get_mono();
        io.Fonts->AddFontFromMemoryCompressedTTF(
            mono.data, mono.size, kFontPixels * std::max(1.0F, static_cast<float>(height) / kBaseHeight));
        io.Fonts->Build();
        if (!frontend::ui_backend::prepare_all()) {
            ImGui::DestroyContext(s.context);
            s.context = nullptr;
            s.context_failed = true;
            ImGui::SetCurrentContext(*previous);
            get_log("fps counter: ImGui couldn't start on the game's window / device - off");
            return false;
        }
    }
    ImGui::SetCurrentContext(s.context);
    return true;
}

// Every frame drawn: its time noted (also while hidden, so the figures are ready when it is shown).
void note_frame(Clock::time_point now)
{
    State& s = state();
    if (s.have_last) {
        const double ms = std::chrono::duration<double, std::milli>(now - s.last_frame).count();
        if (ms < kStallMs) {
            s.frame_ms.push_back(ms);
            if (!frame_blend::frame().extra) {
                ++s.ticks;
            }
        }
    } else {
        s.window_start = now;
    }
    s.last_frame = now;
    s.have_last = true;
    const double window = std::chrono::duration<double>(now - s.window_start).count();
    if (window >= kWindowSeconds) {
        s.shown_stats = summarise(s.frame_ms, s.ticks);
        // (the frames and ticks per second over the window's true length)
        const double scale = kWindowSeconds / window;
        s.shown_stats.fps *= scale;
        s.shown_stats.ticks *= scale;
        const uint64_t presented = render::gpu_context().frames_shown();
        s.presented_fps = presented > s.presented_at_start && s.presented_at_start != 0
                              ? static_cast<double>(presented - s.presented_at_start) / window
                              : 0.0;
        s.presented_at_start = presented;
        s.frame_ms.clear();
        s.ticks = 0;
        s.window_start = now;
    }
}

void draw()
{
    const Clock::time_point now = Clock::now();
    read_ini(now);
    note_frame(now);
    State& s = state();
    if (!s.shown) {
        return;
    }
    ImGuiContext* previous = nullptr;
    if (!begin_context(&previous)) {
        s.shown = false;
        return;
    }
    frontend::ui_backend::new_frame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - kMargin, kMargin), ImGuiCond_Always, ImVec2(1.0F, 0.0F));
    ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    const Stats& st = s.shown_stats;
    ImGui::Text("%5.0f FPS", st.fps);
    if (s.presented_fps > 0.0) {
        ImGui::Text("%5.0f shown (frame gen)", s.presented_fps); // (drawn and made frames handed to the display)
    }
    ImGui::Text("%5.1f ms  1%% low %.0f", st.mean_ms, st.low_fps);
    // (the game's own rate: 60 while it keeps up, whatever is drawn)
    const bool behind = st.ticks > 0.0 && st.ticks < 57.0;
    if (behind) {
        ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.2F, 1.0F), "%5.0f game ticks", st.ticks);
    } else {
        ImGui::Text("%5.0f game ticks", st.ticks);
    }
    ImGui::End();
    ImGui::Render();
    frontend::ui_backend::render();
    ImGui::SetCurrentContext(previous);
}

void handle_event(const input::Event& event)
{
    State& s = state();
    if (event.type == input::EventType::kKeyDown && !event.repeat && event.key == s.key && !ext::game_input_blocked()) {
        s.shown = !s.shown;
        get_log("fps counter: %s (this run; [Graphics] show_fps=1 keeps it on)", s.shown ? "shown" : "hidden");
    }
}

void prepare_input()
{
    std::array<char, 64> setting = {""};
    build_info::fetch_text(build_info::kKeybinds, "FpsCounter", "", setting.data(), setting.size());
    state().key = sys::screenshot::parse_key(setting.data(), SDL_SCANCODE_F3, platform::scancode_from_name,
                                             platform::scancode_from_sdl2);
    input::add_event_handler(handle_event);
}

void device_lost()
{
    if (!state().context) {
        return;
    }
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(state().context);
    frontend::ui_backend::invalidate_context_objects();
    ImGui::SetCurrentContext(previous);
}

class FpsCounterExtension final : public ext::Extension {
public:
    void prepare_input() override { fps_counter::prepare_input(); }
    void draw_over_frame() override { draw(); }
    void device_lost() override { fps_counter::device_lost(); }
};

} // namespace

Stats summarise(std::vector<double> frame_ms, int ticks)
{
    Stats out;
    if (frame_ms.empty()) {
        return out;
    }
    const double total = std::accumulate(frame_ms.begin(), frame_ms.end(), 0.0);
    out.fps = static_cast<double>(frame_ms.size());
    out.ticks = static_cast<double>(ticks);
    out.mean_ms = total / static_cast<double>(frame_ms.size());
    std::ranges::sort(frame_ms);
    out.worst_ms = frame_ms.back();
    // the 99th percentile's frame time (the slowest 1%'s fastest): at least the slowest frame of a short window
    const auto slow = static_cast<std::size_t>(std::floor(static_cast<double>(frame_ms.size() - 1) * (1.0 - kLowShare)));
    const double p99 = frame_ms[std::min(slow, frame_ms.size() - 1)];
    out.low_fps = p99 > 0.0 ? 1000.0 / p99 : 0.0;
    return out;
}

ext::Extension& extension()
{
    static FpsCounterExtension counter;
    return counter;
}

} // namespace devtools::fps_counter

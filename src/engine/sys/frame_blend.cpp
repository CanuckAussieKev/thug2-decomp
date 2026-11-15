// NOT ORIGINAL - the render-only frames' running state (see frame_blend.h).

#include "engine/sys/frame_blend.h"

#include <array>

#include "base/core/no_destroy.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"

namespace frame_blend {

namespace {

struct BlendState {
    Frame frame;
    bool cut = false;
    uint64_t cuts = 0;
    int max_fps = frame_pacing::kOriginal;
    frame_pacing::PresentMode present = frame_pacing::PresentMode::kVsync;
};

BlendState& blend_state()
{
    static core::NoDestroy<BlendState> state;
    return state.get();
}

constexpr std::size_t kSettingBytes = 32;

} // namespace

Frame& frame() { return blend_state().frame; }

void note_cut()
{
    blend_state().cut = true;
    ++blend_state().cuts;
}

uint64_t cuts() { return blend_state().cuts; }

bool take_cut()
{
    const bool cut = blend_state().cut;
    blend_state().cut = false;
    return cut;
}

void read_settings()
{
    BlendState& state = blend_state();
    std::array<char, kSettingBytes> text{};
    build_info::fetch_text(build_info::kGraphics, "max_fps", "0", text.data(), text.size());
    state.max_fps = frame_pacing::parse_max_fps(text.data());
    build_info::fetch_text(build_info::kGraphics, "present_mode", "vsync", text.data(), text.size());
    state.present = frame_pacing::parse_present_mode(text.data());
    if (state.max_fps != frame_pacing::kOriginal || state.present != frame_pacing::PresentMode::kVsync) {
        if (state.max_fps == frame_pacing::kUncapped) {
            get_log("frame pacing: max_fps=uncapped present_mode=%s (render-only frames blended between the 60 Hz ticks)",
                    frame_pacing::present_mode_name(state.present));
        } else {
            get_log("frame pacing: max_fps=%d present_mode=%s%s", state.max_fps,
                    frame_pacing::present_mode_name(state.present),
                    state.max_fps != frame_pacing::kOriginal ? " (render-only frames blended between the 60 Hz ticks)"
                                                             : "");
        }
    }
}

int max_fps() { return blend_state().max_fps; }

frame_pacing::PresentMode present_mode() { return blend_state().present; }

} // namespace frame_blend

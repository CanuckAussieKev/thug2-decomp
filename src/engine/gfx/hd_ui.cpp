// NOT ORIGINAL - HD menu UI (see hd_ui.h). The switch is [features] hd_ui.

#include "engine/gfx/hd_ui.h"

#include "platform/run_log.h"
#include "engine/core/no_destroy.h"

#include <cstdint>

namespace render::hd_ui {

namespace {
// NOT ORIGINAL grouping - hd_ui.cpp's state (were globals). Made on first use, never destroyed (hd_ui_state()).
struct HdUiState {
    int images_ = 0;
    int fonts_ = 0;
};

HdUiState& hd_ui_state()
{
    static core::NoDestroy<HdUiState> state;
    return state.get();
}

} // namespace

bool is_switched_on()
{
    static const bool kOn = config_int("features", "hd_ui", 0) != 0;
    return kOn;
}

void note_image(uint32_t scale, uint32_t width, uint32_t height)
{
    ++hd_ui_state().images_;
    // (the first one, then every 100th: how many have been loaded)
    if (hd_ui_state().images_ == 1 || hd_ui_state().images_ % 100 == 0)
        get_log("hd ui: %d upscaled image(s) loaded (this one x%u, %ux%u texels; drawn at %ux%u)", hd_ui_state().images_, scale,
                width, height, width / scale, height / scale);
}

void note_font(const char* name, uint32_t scale, uint32_t width, uint32_t height)
{
    ++hd_ui_state().fonts_;
    get_log("hd ui: font %s upscaled x%u (glyph page %ux%u)", name ? name : "(from memory)", scale, width, height);
}

} // namespace render::hd_ui

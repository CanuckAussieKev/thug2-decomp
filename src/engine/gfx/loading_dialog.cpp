// NOT ORIGINAL - the editor mode's loading dialog: its look and its box (loading_dialog.h); drawn during a load by
// render_pc/loading_dialog_panel.cpp.

#include "engine/gfx/loading_dialog.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <string>

#include "base/core/no_destroy.h"
#include "engine/sys/editor_mode.h"
#include "imgui.h"

namespace gfx::loading_dialog {

namespace {

constexpr float kHalf = 0.5F;
constexpr float kSweepWidth = 0.2F; // (ImGui's ProgressBar: the sweep is a fifth of the bar)

// The look and the title, set by the editor on the main thread, read by the loading screen's timer.
struct DialogState {
    std::mutex lock;
    Look look;
    std::string title;
};

DialogState& dialog_state()
{
    static core::NoDestroy<DialogState> state;
    return state.get();
}

// The bar's filled part, 0..1 each end (a sweep for a fraction below 0, as ImGui's ProgressBar draws it).
struct Fill {
    float from = 0.0F;
    float to = 0.0F;
};

Fill fill_of(float fraction)
{
    if (!(fraction >= 0.0F)) {
        const float phase = std::isnan(fraction) ? 0.0F : std::fmod(-fraction, 1.0F);
        const float from = (phase * (1.0F + kSweepWidth)) - kSweepWidth;
        return {.from = std::clamp(from, 0.0F, 1.0F), .to = std::clamp(from + kSweepWidth, 0.0F, 1.0F)};
    }
    return {.from = 0.0F, .to = std::min(fraction, 1.0F)};
}

} // namespace

void draw_box(ImDrawList* list, const Look& look, const BoxFonts& fonts, const BoxText& text, const Display& display)
{
    if (list == nullptr || fonts.ui == nullptr || fonts.heading == nullptr) {
        return;
    }
    const float line = look.ui_pixels + (2.0F * look.frame_padding_y); // the title bar's height, and the bar's
    const float body = look.padding_y + look.heading_pixels + look.spacing_y + line + look.spacing_y + look.ui_pixels +
                       look.padding_y;
    const float width = std::round(std::min(look.width, display.width - (2.0F * look.padding_x)));
    const float height = std::round(line + body);
    const float left = std::round(display.origin_x + ((display.width - width) * kHalf));
    const float top = std::round(display.origin_y + ((display.height - height) * kHalf));
    const ImVec2 box_min(left, top);
    const ImVec2 box_max(left + width, top + height);
    list->AddRectFilled(box_min, box_max, look.window);
    list->AddRectFilled(box_min, ImVec2(box_max.x, top + line), look.title_bar);
    list->AddLine(ImVec2(left, top + line), ImVec2(box_max.x, top + line), look.border);
    list->AddRect(box_min, box_max, look.border);
    list->AddText(fonts.ui, look.ui_pixels, ImVec2(left + look.frame_padding_x, top + look.frame_padding_y), look.text,
                  text.caption.c_str());

    const float inner_left = left + look.padding_x;
    const float inner_right = box_max.x - look.padding_x;
    const ImVec4 clip(inner_left, top, inner_right, box_max.y);
    float y = top + line + look.padding_y;
    list->AddText(fonts.heading, look.heading_pixels, ImVec2(inner_left, y), look.text, text.heading.c_str(), nullptr,
                  0.0F, &clip);
    y = std::round(y + look.heading_pixels + look.spacing_y);

    // The bar: the track, then the filled part (rounded only where it is wide enough).
    list->AddRectFilled(ImVec2(inner_left, y), ImVec2(inner_right, y + line), look.frame, look.rounding);
    const Fill fill = fill_of(text.fraction);
    const float fill_left = std::round(inner_left + ((inner_right - inner_left) * fill.from));
    const float fill_right = std::round(inner_left + ((inner_right - inner_left) * fill.to));
    if (fill_right > fill_left) {
        const float rounding = fill_right - fill_left >= 2.0F * look.rounding ? look.rounding : 0.0F;
        list->AddRectFilled(ImVec2(fill_left, y), ImVec2(fill_right, y + line), look.accent, rounding);
    }
    y = std::round(y + line + look.spacing_y);

    // The line under it: what is loading, and the time on its right.
    float status_right = inner_right;
    if (!text.time.empty()) {
        const ImVec2 size = fonts.ui->CalcTextSizeA(look.ui_pixels, FLT_MAX, 0.0F, text.time.c_str());
        list->AddText(fonts.ui, look.ui_pixels, ImVec2(std::round(inner_right - size.x), y), look.text_dim,
                      text.time.c_str());
        status_right = inner_right - size.x - look.frame_padding_x;
    }
    const ImVec4 status_clip(inner_left, top, status_right, box_max.y);
    list->AddText(fonts.ui, look.ui_pixels, ImVec2(inner_left, y), text.stalled ? look.warning : look.text_dim,
                  text.status.c_str(), nullptr, 0.0F, &status_clip);
}

void set_look(const Look& look)
{
    const std::scoped_lock hold(dialog_state().lock);
    dialog_state().look = look;
}

void set_title(const std::string& title)
{
    const std::scoped_lock hold(dialog_state().lock);
    dialog_state().title = title;
}

Look look()
{
    const std::scoped_lock hold(dialog_state().lock);
    return dialog_state().look;
}

std::string title()
{
    const std::scoped_lock hold(dialog_state().lock);
    return dialog_state().title;
}

bool replaces_loading_screens() { return sys::editor_mode::on(); }

} // namespace gfx::loading_dialog

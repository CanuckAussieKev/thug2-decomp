// NOT ORIGINAL - the level editor's UI layout and input routing (ours). See level_editor_ui_layout.h.

#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ui.h"
#include "engine/core/checked_index.h"
#include "engine/maths/rect.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <string>

namespace frontend::level_editor::ui_layout {

constexpr double kMinScale = 0.875;
constexpr double kMaxScale = 3.0; // the largest UI scale
constexpr double kReferenceWidth = 1920.0; // the window size where the scale is 1
constexpr double kReferenceHeight = 1080.0;
constexpr double kScaleStepsPerUnit = 4.0; // the scale moves in quarter steps
constexpr double kScaleStepSlack = 1e-6; // keeps a ratio of exactly 1.25 from rounding down
// The fonts' sizes at scale 1 (pixels).
constexpr float kUiFontSize = 14.0f;
constexpr float kSmallFontSize = 12.0f;
constexpr float kMonoFontSize = 13.0f;
constexpr float kHeadingFontSize = 15.0f;
// The bars' heights at scale 1 and the most of the window's height each may take.
constexpr float kMenuBarHeight = 22.0f;
constexpr float kToolStripHeight = 28.0f;
constexpr float kStatusBarHeight = 24.0f;
constexpr float kMenuBarMaxFraction = 0.12f;
constexpr float kToolStripMaxFraction = 0.13f;
constexpr float kStatusBarMaxFraction = 0.25f;
// The side panels' widths at scale 1.
constexpr float kOutlinerWidth = 280.0f;
constexpr float kInspectorWidth = 320.0f;

float scale_for(int window_width, int window_height)
{
    if (window_width <= 0 || window_height <= 0)
        return 1.0f;
    // 1 at 1920 x 1080 (Blender's and Unreal's 100 %), quarter steps from the smaller ratio - 1.25 at 2560 x 1440
    // (Windows' suggested 125 % for a 1440p screen), 2 at 4K; never below 0.875 (Inter at 12 px).
    double scale = std::min(window_width / kReferenceWidth, window_height / kReferenceHeight);
    scale = std::floor(scale * kScaleStepsPerUnit + kScaleStepSlack) / kScaleStepsPerUnit;
    return static_cast<float>(std::max(kMinScale, std::min(kMaxScale, scale)));
}

FontSizes font_sizes_for(float scale)
{
    auto px = [scale](float base) { return std::round(base * scale); };
    return {.ui = px(kUiFontSize), .small = px(kSmallFontSize), .mono = px(kMonoFontSize), .heading = px(kHeadingFontSize)};
}

Layout compute(maths::Size window, float scale, bool outliner_open, bool inspector_open)
{
    const auto width = window.width;
    const auto height = window.height;
    Layout layout = {};
    // The menu bar (the font and its frame padding: 14 + 2 x 4 at scale 1) over the tool strip (its buttons, 14 + 2 x 3,
    // with 4 above and below).
    float menu = std::round(kMenuBarHeight * scale), strip = std::round(kToolStripHeight * scale),
          status = std::round(kStatusBarHeight * scale);
    menu = std::min(menu, height * kMenuBarMaxFraction);
    strip = std::min(strip, height * kToolStripMaxFraction);
    status = std::min(status, height * kStatusBarMaxFraction);
    const float toolbar = menu + strip;
    const float middle_top = toolbar;
    const float middle_height = std::max(0.0f, height - toolbar - status);
    layout.toolbar = {.x = 0.0f, .y = 0.0f, .width = width, .height = toolbar};
    layout.menu = {.x = 0.0f, .y = 0.0f, .width = width, .height = menu};
    layout.strip = {.x = 0.0f, .y = menu, .width = width, .height = strip};
    layout.status = {.x = 0.0f, .y = height - status, .width = width, .height = status};
    // The side panels: 280 / 320 at scale 1, narrowed to 180 each so the viewport keeps 40% of the width.
    float left = outliner_open ? kOutlinerWidth * scale : 0.0f, right = inspector_open ? kInspectorWidth * scale : 0.0f;
    const float room = width * 0.6f;
    if (left + right > room) {
        const float minimum = 180.0f * scale;
        const float shrink = (left + right - room) / std::max(1.0f, left + right);
        left = left > 0.0f ? std::max(minimum, left * (1.0f - shrink)) : 0.0f;
        right = right > 0.0f ? std::max(minimum, right * (1.0f - shrink)) : 0.0f;
        // (still too wide: the Inspector closes first, then the Outliner)
        if (left + right > room && right > 0.0f)
            right = 0.0f;
        if (left > room)
            left = 0.0f;
    }
    left = std::round(left);
    right = std::round(right);
    layout.outliner_shown_ = left > 0.0f;
    layout.inspector_shown_ = right > 0.0f;
    layout.outliner = {.x = 0.0f, .y = middle_top, .width = left, .height = middle_height};
    layout.inspector = {.x = width - right, .y = middle_top, .width = right, .height = middle_height};
    layout.viewport = {.x = left, .y = middle_top, .width = std::max(0.0f, width - left - right), .height = middle_height};
    return layout;
}

bool on_panel(const Layout& layout, float x, float y)
{
    return get_holds(layout.toolbar, x, y) || get_holds(layout.status, x, y) ||
           (layout.outliner_shown_ && get_holds(layout.outliner, x, y)) ||
           (layout.inspector_shown_ && get_holds(layout.inspector, x, y));
}

Route decide(const RouteInput& input)
{
    Route route = {};
    if (input.editor_capture_) {
        route.mouse_to_editor_ = true;
    } else if (input.ui_wants_mouse_ || input.over_panel_) {
        route.mouse_to_ui_ = true;
    } else {
        route.mouse_to_ui_ = true;
        route.mouse_to_editor_ = true;
    }
    route.keys_to_editor_ = !input.ui_wants_keyboard_;
    return route;
}

} // namespace frontend::level_editor::ui_layout

// The Outliner's markers (level_editor_ui.h) - pure, beside the layout's other pure parts.
namespace frontend::level_editor::ui {

namespace {

constexpr std::uint32_t kUnmarkedColour = 0xFFFFFFFFu; // the colour of a bit with no mark
struct MarkInfo {
    char letter;
    uint32_t colour;
    const char* name;
};
const std::array<MarkInfo, kMaxMarkers> kMarks = {{
    {.letter = 'M', .colour = 0xFFFFC040u, .name = "Moved"},          {.letter = '+', .colour = 0xFF60E060u, .name = "Added"},
    {.letter = 'D', .colour = 0xFFFF6060u, .name = "Hidden in game"}, {.letter = 'H', .colour = 0xFF9090A0u, .name = "Hidden in editor"},
    {.letter = 'L', .colour = 0xFF60B0FFu, .name = "Locked"},         {.letter = 'C', .colour = 0xFFFF40E0u, .name = "Collision off"},
}};
} // namespace

char mark_letter(int bit) { return bit >= 0 && bit < kMaxMarkers ? core::at(kMarks, bit).letter : '?'; }
uint32_t mark_colour(int bit) { return bit >= 0 && bit < kMaxMarkers ? core::at(kMarks, bit).colour : kUnmarkedColour; }
const char* mark_name(int bit) { return bit >= 0 && bit < kMaxMarkers ? core::at(kMarks, bit).name : ""; }

bool mark_shown(uint8_t marks, bool changed_only) { return !changed_only || (marks & ((1u << kMaxMarkers) - 1)) != 0; }

std::string mark_letters(uint8_t marks)
{
    std::string letters;
    for (int bit = 0; bit < kMaxMarkers; ++bit) {
        if (marks & (1u << bit))
            letters.push_back(core::at(kMarks, bit).letter);
    }
    return letters;
}

} // namespace frontend::level_editor::ui

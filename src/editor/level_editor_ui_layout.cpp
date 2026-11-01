// NOT ORIGINAL - the level editor's UI layout and input routing (ours). See level_editor_ui_layout.h.

#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ui.h"

#include <algorithm>
#include <cmath>
#include <array>

namespace frontend::level_editor::ui_layout {

constexpr double kMinScale = 0.875;

float scale_for(int window_width, int window_height)
{
    if (window_width <= 0 || window_height <= 0)
        return 1.0f;
    // 1 at 1920 x 1080 (Blender's and Unreal's 100 %), quarter steps from the smaller ratio - 1.25 at 2560 x 1440
    // (Windows' suggested 125 % for a 1440p screen), 2 at 4K; never below 0.875 (Inter at 12 px).
    double scale = std::min(window_width / 1920.0, window_height / 1080.0);
    scale = std::floor(scale * 4.0 + 1e-6) / 4.0;
    return static_cast<float>(std::max(kMinScale, std::min(3.0, scale)));
}

FontSizes font_sizes_for(float scale)
{
    auto px = [scale](float base) { return std::round(base * scale); };
    return {px(14.0f), px(12.0f), px(13.0f), px(15.0f)};
}

Layout compute(float width, float height, float scale, bool outliner_open, bool inspector_open)
{
    Layout layout = {};
    // The menu bar (the font and its frame padding: 14 + 2 x 4 at scale 1) over the tool strip (its buttons, 14 + 2 x 3,
    // with 4 above and below).
    float menu = std::round(22.0f * scale), strip = std::round(28.0f * scale), status = std::round(24.0f * scale);
    menu = std::min(menu, height * 0.12f);
    strip = std::min(strip, height * 0.13f);
    status = std::min(status, height * 0.25f);
    const float toolbar = menu + strip;
    const float middle_top = toolbar;
    const float middle_height = std::max(0.0f, height - toolbar - status);
    layout.toolbar = {0.0f, 0.0f, width, toolbar};
    layout.menu = {0.0f, 0.0f, width, menu};
    layout.strip = {0.0f, menu, width, strip};
    layout.status = {0.0f, height - status, width, status};
    // The side panels: 280 / 320 at scale 1, narrowed to 180 each so the viewport keeps 40% of the width.
    float left = outliner_open ? 280.0f * scale : 0.0f, right = inspector_open ? 320.0f * scale : 0.0f;
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
    layout.outliner = {0.0f, middle_top, left, middle_height};
    layout.inspector = {width - right, middle_top, right, middle_height};
    layout.viewport = {left, middle_top, std::max(0.0f, width - left - right), middle_height};
    return layout;
}

bool on_panel(const Layout& layout, float x, float y)
{
    return layout.toolbar.get_holds(x, y) || layout.status.get_holds(x, y) ||
           (layout.outliner_shown_ && layout.outliner.get_holds(x, y)) ||
           (layout.inspector_shown_ && layout.inspector.get_holds(x, y));
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
struct MarkInfo {
    char letter;
    uint32_t colour;
    const char* name;
};
const std::array<MarkInfo, kMaxMarkers> kMarks = {{
    {'M', 0xFFFFC040u, "Moved"},          {'+', 0xFF60E060u, "Added"},
    {'D', 0xFFFF6060u, "Hidden in game"}, {'H', 0xFF9090A0u, "Hidden in editor"},
    {'L', 0xFF60B0FFu, "Locked"},         {'C', 0xFFFF40E0u, "Collision off"},
}};
} // namespace

char mark_letter(int bit) { return bit >= 0 && bit < kMaxMarkers ? kMarks[bit].letter : '?'; }
uint32_t mark_colour(int bit) { return bit >= 0 && bit < kMaxMarkers ? kMarks[bit].colour : 0xFFFFFFFFu; }
const char* mark_name(int bit) { return bit >= 0 && bit < kMaxMarkers ? kMarks[bit].name : ""; }

bool mark_shown(uint8_t marks, bool changed_only) { return !changed_only || (marks & ((1u << kMaxMarkers) - 1)) != 0; }

std::string mark_letters(uint8_t marks)
{
    std::string letters;
    for (int bit = 0; bit < kMaxMarkers; ++bit) {
        if (marks & (1u << bit))
            letters.push_back(kMarks[bit].letter);
    }
    return letters;
}

} // namespace frontend::level_editor::ui

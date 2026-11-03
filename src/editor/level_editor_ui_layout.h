#pragma once
#include "engine/maths/rect.h"

// NOT ORIGINAL - the level editor's UI layout and input routing (ours; see level_editor_ui.h): pure.
namespace frontend::level_editor::ui_layout {

// Sizes the panels share (at scale 1: the UI scale multiplies them).
constexpr float kReferenceFontSize = 13.0f;    // the font size the fields' widths are made for
constexpr float kBothSides = 2.0f;             // a padding counted on both sides
constexpr float kHelpNameColumnWidth = 90.0f;  // the key help table's columns (the help window and the panel's)
constexpr float kHelpKeysColumnWidth = 190.0f;

struct Rect2 {
    float x, y, width, height;
};

[[nodiscard]] inline bool get_holds(const Rect2& rect2, float px, float py)
{
    return px >= rect2.x && px < rect2.x + rect2.width && py >= rect2.y && py < rect2.y + rect2.height;
}

// The UI's scale for a window: 1 at 1920 x 1080, the smaller of width / 1920 and height / 1080 rounded down to a quarter
// (2560 x 1440: 1.25, 4K: 2), at least 0.875 (1280 x 720), at most 3. [editor] ui_scale_pct overrides it.
float scale_for(int window_width, int window_height);
// The font sizes at a scale (whole pixels, so the glyphs stay crisp): Inter 14 for panels, 12 for tables and hints;
// JetBrains Mono 13.
struct FontSizes {
    float ui, small, mono, heading;
};
FontSizes font_sizes_for(float scale);

// The screen's parts (window pixels): a toolbar across the top (the menu bar over the tool strip), the Outliner on the left and the Inspector on the
// right between it and the status bar across the bottom, the viewport in the middle. A side panel that's closed takes
// no room; the side panels narrow (to 180 at scale 1) so the viewport keeps at least 40% of the width, and close if
// even that doesn't fit.
struct Layout {
    Rect2 toolbar, outliner, inspector, status, viewport;
    Rect2 menu, strip; // (the toolbar's two rows)
    bool outliner_shown_, inspector_shown_;
};
Layout compute(maths::Size window, float scale, bool outliner_open, bool inspector_open);
// Whether a point is on one of the panels (toolbar, side panels shown, status bar) rather than the viewport.
bool on_panel(const Layout& layout, float x, float y);

// Who gets the mouse and the keys this frame.
struct RouteInput {
    bool ui_wants_mouse_;    // ImGui's WantCaptureMouse (the last frame: over a window, or a widget held)
    bool ui_wants_keyboard_; // ImGui's WantCaptureKeyboard (a text field or widget active)
    bool over_panel_;        // the cursor is on a panel (on_panel)
    bool editor_capture_;    // the editor holds the mouse: a drag or a right-button look started in the viewport
};
struct Route {
    bool mouse_to_ui_;     // ImGui gets the mouse's events
    bool mouse_to_editor_; // the editor picks, clicks, drags and wheels
    bool keys_to_editor_;  // the editor's keys (flying, shortcuts, Escape, the Pause key)
};
// The editor's capture keeps the mouse until it lets go (the UI doesn't see it); otherwise ImGui takes it over a panel
// or while it wants it, and the editor gets it in the viewport (ImGui still sees it there, to lose its hover and focus).
// The keys go to the editor unless ImGui wants the keyboard.
Route decide(const RouteInput& input);

} // namespace frontend::level_editor::ui_layout

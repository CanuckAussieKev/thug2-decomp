#pragma once

// NOT ORIGINAL - the level editor's UI layout and input routing (ours; see level_editor_ui.h): pure, self-tested
// (src/hook/selftests_editor_ui.cpp).
namespace Front::LevelEditor::UiLayout {

struct Rect {
    float x, y, width, height;
    bool Contains(float px, float py) const { return px >= x && px < x + width && py >= y && py < y + height; }
};

// The UI's scale for a window (1 at 1280 x 720 and below; up with the smaller of width / 1280 and height / 720, in
// quarter steps, at most 3 - 4K is 3).
float ScaleFor(int windowWidth, int windowHeight);
// The font sizes at a scale (pixels): Inter 14 for panels, 12 for tables and hints; JetBrains Mono 13.
struct FontSizes {
    float ui, small, mono, heading;
};
FontSizes FontSizesFor(float scale);

// The screen's parts (window pixels): a toolbar across the top, the Outliner on the left and the Inspector on the
// right between it and the status bar across the bottom, the viewport in the middle. A side panel that's closed takes
// no room; the side panels narrow (to 180 at scale 1) so the viewport keeps at least 40% of the width, and close if
// even that doesn't fit.
struct Layout {
    Rect toolbar, outliner, inspector, status, viewport;
    bool outlinerShown, inspectorShown;
};
Layout Compute(float width, float height, float scale, bool outlinerOpen, bool inspectorOpen);
// Whether a point is on one of the panels (toolbar, side panels shown, status bar) rather than the viewport.
bool OnPanel(const Layout& layout, float x, float y);

// Who gets the mouse and the keys this frame.
struct RouteInput {
    bool uiWantsMouse;    // ImGui's WantCaptureMouse (the last frame: over a window, or a widget held)
    bool uiWantsKeyboard; // ImGui's WantCaptureKeyboard (a text field or widget active)
    bool overPanel;       // the cursor is on a panel (OnPanel)
    bool editorCapture;   // the editor holds the mouse: a drag or a right-button look started in the viewport
};
struct Route {
    bool mouseToUi;     // ImGui gets the mouse's events
    bool mouseToEditor; // the editor picks, clicks, drags and wheels
    bool keysToEditor;  // the editor's keys (flying, shortcuts, Escape, the Pause key)
};
// The editor's capture keeps the mouse until it lets go (the UI doesn't see it); otherwise ImGui takes it over a panel
// or while it wants it, and the editor gets it in the viewport (ImGui still sees it there, to lose its hover and focus).
// The keys go to the editor unless ImGui wants the keyboard.
Route Decide(const RouteInput& input);

} // namespace Front::LevelEditor::UiLayout

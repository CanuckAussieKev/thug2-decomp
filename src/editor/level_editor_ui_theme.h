#pragma once

struct ImFont;

// NOT ORIGINAL - the level editor's fonts and style (ours; see level_editor_ui.h): Inter for the UI, JetBrains Mono for
// numbers and names (level_editor_ui_fonts.h), sized for a UI scale (level_editor_ui_layout.h), and the dark style with
// the accent colour. Only ImGui's own API: the UI and a headless context build them alike.
namespace frontend::level_editor::ui_theme {

struct Fonts {
    ImFont* ui = nullptr;
    ImFont* small = nullptr;
    ImFont* mono = nullptr;
    ImFont* heading = nullptr;
};
// The current context's font atlas cleared and the four fonts added at `scale`, then built.
Fonts build_fonts(float scale);
// The current context's style: dark, the accent (RGB 0..1), every size scaled.
void apply_style(float scale, const float accent_rgb[3]);

} // namespace MenuSystem::level_editor::ui_theme

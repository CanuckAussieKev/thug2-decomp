#pragma once

struct ImFont;

// NOT ORIGINAL - the level editor's fonts and style (ours; see level_editor_ui.h): Inter for the UI, JetBrains Mono for
// numbers and names (level_editor_ui_fonts.h), sized for a UI scale (level_editor_ui_layout.h), and the dark style with
// the accent colour. Only ImGui's own API: the UI and a headless context build them alike.
namespace Front::LevelEditor::UiTheme {

struct Fonts {
    ImFont* ui = nullptr;
    ImFont* small = nullptr;
    ImFont* mono = nullptr;
    ImFont* heading = nullptr;
};
// The current context's font atlas cleared and the four fonts added at `scale`, then built.
Fonts BuildFonts(float scale);
// The current context's style: dark, the accent (RGB 0..1), every size scaled.
void ApplyStyle(float scale, const float accentRgb[3]);

} // namespace Front::LevelEditor::UiTheme

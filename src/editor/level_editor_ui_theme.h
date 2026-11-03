#pragma once

#include <span>

#include "imgui.h"

// NOT ORIGINAL - the level editor's fonts and style (ours; see level_editor_ui.h): Inter for the UI, JetBrains Mono for
// numbers and names (level_editor_ui_fonts.h), sized for a UI scale (level_editor_ui_layout.h), and the dark style with
// the accent colour. Only ImGui's own API: the UI and a headless context build them alike.
namespace frontend::level_editor::ui_theme {

// Colours the panels share: a button that deletes or turns something off, and a warning's text.
constexpr ImVec4 kDangerButtonColour{0.62F, 0.22F, 0.30F, 0.85F};
constexpr ImVec4 kWarningColour{1.0F, 0.78F, 0.45F, 1.0F};

struct Fonts {
    ImFont* ui = nullptr;
    ImFont* small = nullptr;
    ImFont* mono = nullptr;
    ImFont* heading = nullptr;
};
// The current context's font atlas cleared and the four fonts added at `scale`, then built.
Fonts build_fonts(float scale);
// The current context's style: dark, the accent (RGB 0..1), every size scaled.
void apply_style(float scale, std::span<const float, 3> accent_rgb);

} // namespace frontend::level_editor::ui_theme

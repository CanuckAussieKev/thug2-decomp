#pragma once

// NOT ORIGINAL - the level editor's fonts, compressed into the exe (level_editor_ui_fonts.cpp).
namespace frontend::level_editor::ui_fonts {

// A TTF compressed by ImGui's binary_to_compressed_c (for ImFontAtlas::AddFontFromMemoryCompressedTTF).
struct CompressedFont {
    const void* data;
    int size;
};
CompressedFont inter();         // Inter Regular: the UI
CompressedFont inter_semi_bold(); // Inter SemiBold: headings, the selection's name
CompressedFont get_mono();          // JetBrains Mono Regular: numbers, names, checksums

} // namespace MenuSystem::level_editor::ui_fonts

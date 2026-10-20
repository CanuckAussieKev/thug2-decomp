#pragma once

// NOT ORIGINAL - the level editor's fonts, compressed into the DLL (level_editor_ui_fonts.cpp).
namespace Front::LevelEditor::UiFonts {

// A TTF compressed by ImGui's binary_to_compressed_c (for ImFontAtlas::AddFontFromMemoryCompressedTTF).
struct CompressedFont {
    const void* data;
    int size;
};
CompressedFont Inter();         // Inter Regular: the UI
CompressedFont InterSemiBold(); // Inter SemiBold: headings, the selection's name
CompressedFont Mono();          // JetBrains Mono Regular: numbers, names, checksums

} // namespace Front::LevelEditor::UiFonts

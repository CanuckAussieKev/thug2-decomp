#pragma once

#include "base/core/api.h"

// NOT ORIGINAL - our UI fonts, compressed into the exe (ui_fonts.cpp): the level editor's UI and the loading
// console's.
namespace frontend::ui_fonts {

// A TTF compressed by ImGui's binary_to_compressed_c (for ImFontAtlas::AddFontFromMemoryCompressedTTF).
struct CompressedFont {
    const void* data;
    int size;
};
THUG2_API CompressedFont inter();           // Inter Regular: the UI
THUG2_API CompressedFont inter_semi_bold(); // Inter SemiBold: headings, the selection's name
THUG2_API CompressedFont get_mono();        // JetBrains Mono Regular: numbers, names, checksums

} // namespace frontend::ui_fonts

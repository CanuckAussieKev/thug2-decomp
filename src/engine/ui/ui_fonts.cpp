// NOT ORIGINAL - our UI fonts (the level editor's UI, editor/level_editor_ui.h, and the loading console): Inter 4.1
// (Regular, SemiBold) and JetBrains Mono 2.304 (Regular), both SIL Open Font License 1.1 (THIRD_PARTY_NOTICES.md). The
// .inc files are made at build time from the official release archives (CMakeLists.txt: FetchContent with their
// SHA-256, then ImGui's binary_to_compressed_c - stb compression), so the fonts are in the exe and nothing is added to
// the game folder.

#include "engine/ui/ui_fonts.h"

#include <iterator>

namespace {
#include "Inter_Regular.inc"
#include "Inter_SemiBold.inc"
#include "JetBrainsMono_Regular.inc"
} // namespace

namespace frontend::ui_fonts {

CompressedFont inter() { return {.data = std::data(Inter_Regular_compressed_data), .size = Inter_Regular_compressed_size}; }
CompressedFont inter_semi_bold() { return {.data = std::data(Inter_SemiBold_compressed_data), .size = Inter_SemiBold_compressed_size}; }
CompressedFont get_mono() { return {.data = std::data(JetBrainsMono_Regular_compressed_data), .size = JetBrainsMono_Regular_compressed_size}; }

} // namespace frontend::ui_fonts

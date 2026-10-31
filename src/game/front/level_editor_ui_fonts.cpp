// NOT ORIGINAL - the level editor's fonts (ours; see level_editor_ui.h): Inter 4.1 (Regular, SemiBold) and JetBrains
// Mono 2.304 (Regular), both SIL Open Font License 1.1 (THIRD_PARTY_NOTICES.md). The .inc files are made at build time
// from the official release archives (CMakeLists.txt: FetchContent with their SHA-256, then ImGui's
// binary_to_compressed_c - stb compression), so the fonts are in the exe and nothing is added to the game folder.

#include "game/front/level_editor_ui_fonts.h"

namespace {
#include "Inter_Regular.inc"
#include "Inter_SemiBold.inc"
#include "JetBrainsMono_Regular.inc"
} // namespace

namespace Front::LevelEditor::UiFonts {

CompressedFont Inter() { return {Inter_Regular_compressed_data, Inter_Regular_compressed_size}; }
CompressedFont InterSemiBold() { return {Inter_SemiBold_compressed_data, Inter_SemiBold_compressed_size}; }
CompressedFont Mono() { return {JetBrainsMono_Regular_compressed_data, JetBrainsMono_Regular_compressed_size}; }

} // namespace Front::LevelEditor::UiFonts

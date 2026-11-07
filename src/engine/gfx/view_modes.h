#pragma once

#include <cstdint>

#include "base/core/api.h"

// NOT ORIGINAL - the level editor's view modes (ours): how the frame's 3D passes are drawn while the editor asks for one
// (editor/level_editor_overlays.h - the toolbar's View menu, Alt+1..Alt+4). The modes and their names; the backend draws
// them (render::Device::set_view_mode; Direct3D 9's: render_pc/view_mode.h, which says what each mode does).
namespace render::view_mode {

enum class Mode : uint8_t { kLit, kUnlit, kWireframe, kCollisionOnly, kCount };
THUG2_API const char* get_name(Mode mode);

} // namespace render::view_mode

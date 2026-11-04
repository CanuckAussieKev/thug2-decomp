// NOT ORIGINAL - the level editor's view modes' names (view_modes.h).

#include "engine/gfx/view_modes.h"

namespace render::view_mode {

const char* get_name(Mode mode)
{
    switch (mode) {
    case Mode::kLit:
        return "Lit";
    case Mode::kUnlit:
        return "Unlit";
    case Mode::kWireframe:
        return "Wireframe";
    case Mode::kCollisionOnly:
        return "Collision only";
    default:
        return "?";
    }
}

} // namespace render::view_mode

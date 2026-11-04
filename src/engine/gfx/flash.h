#pragma once

#include "engine/gfx/engine_effects.h"

namespace render {

// 0x004B9030: draw a flash over its viewport, with PartyMod's vertex order fix.
void draw_flash(const Flash* flash);

} // namespace render

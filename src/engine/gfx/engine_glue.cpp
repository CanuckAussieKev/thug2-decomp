// The engine's platform calls that need no graphics device: see engine_glue.h.

#include "engine/gfx/engine_glue.h"

#include <cstdint>
#include <span>

#include "engine/gfx/occlusion.h"
#include "base/maths/vec4.h"
#include "base/core/checked_index.h"
#include "engine/gfx/engine_parts.h"

namespace render {

WeatherSystem* weather() { return engine_parts().weather().get(); }
ScreenEffects* screen_effects() { return engine_parts().screen_effects().get(); }

namespace {

uint32_t& mem_view_toggle()
{
    static uint32_t mem_view_toggle{};
    return mem_view_toggle;
}

} // namespace

// 0x004B2270
void toggle_mem_view() { mem_view_toggle() ^= 1; }

// 0x004B22C0
void add_debug_shape(int type, std::span<const maths::Vec4> points, uint32_t colour)
{
    if (type == 4) {
        insert_occluder_poly(&core::at(points, 0), &core::at(points, 1), &core::at(points, 2), &core::at(points, 3), colour);
    }
}

// 0x004B22F0
void flag_debug_lines(uint32_t id, bool flag) { activate_occluder_poly(id, flag); }

// 0x004B2300
void clear_debug_lines() { clear_occluder_polys(); }

// 0x004B2310
int sphere_visible(const maths::Vec4* centre, float radius) { return sphere_outside_view(centre, radius); }

// 0x004B2320
const char* platform_suffix() { return "Xbx"; }

} // namespace render

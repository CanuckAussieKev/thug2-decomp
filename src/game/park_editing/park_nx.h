#pragma once

#include <cstdint>

#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/sector.h"
#include "engine/maths/vec4.h"
#include "engine/core/raw_view.h"

// The engine's scene calls the park editor's pieces and generator make (Nx). Sectors are looked
// up by name; the park's pieces are clones of the park editor's own sectors.
namespace park_editing::scene {


// A geometry (the original's calls go through the engine's dispatchers `MOV EAX,[ECX]; JMP [EAX + 4 * slot]`).
inline render::Geom* as_geom(void* geometry) { return static_cast<render::Geom*>(geometry); }

// 0x0048C510: the sector named `name` in any scene (null if none)
inline render::WorldCell* look_up_sector(uint32_t name) { return render::engine::look_up_sector(name); }
// 0x0048C700: the scene named `name` (null if none)
inline render::WorldScene* look_up_scene(uint32_t name) { return render::engine::find_scene(name); }

// WorldCell:
// 0x0049E760: quarter turns about y
inline void spin_around_up(render::WorldCell* sector, int rot) { sector->spin_around_up(rot); }
// 0x0049E9B0: drawn and collided with, or neither
inline void change_live(render::WorldCell* sector, bool active) { sector->change_live(active); }
// 0x0049E680: the viewports it shows in (a mask)
inline void toggle_shown(render::WorldCell* sector, uint32_t mask) { sector->toggle_shown(mask); }
// 0x0049E6C0: where it is
inline void place_at_absolute_point(render::WorldCell* sector, const maths::Vec4* position)
{
    sector->place_at_absolute_point(position);
}
// 0x0049E7C0: its scale (x y z)
inline void change_scale(render::WorldCell* sector, const maths::Vec4* scale) { sector->change_scale(scale); }
// 0x004A2BA0 (a folded getter, +0x14): its geometry (null if none)
inline render::Geom* fetch_shape(render::WorldCell* sector) { return static_cast<render::Geom*>(sector->geom_handle_); }

// CGeom (virtual):
// 0x0048DEE0 (slot 20): its orientation (a 4 x 4 matrix)
inline void change_facing(render::Geom* geometry, const void* matrix)
{
    geometry->change_facing(static_cast<const maths::Mat4*>(matrix));
}
// Vtable +0x18: a colour (r g b a in the bytes, low first) over the geometry
inline void change_tint(render::Geom* geometry, uint32_t rgba) { geometry->change_tint(rgba); }
// 0x0048D060 (slot 8): the colour taken off
inline void wipe_tint(render::Geom* geometry) { geometry->wipe_tint(); }

// WorldScene:
// 0x0049DF30: a copy of the sector `name` (of this scene) in `destination`; its name
inline uint32_t duplicate_scene_sector(render::WorldScene* scene, uint32_t name, render::WorldScene* destination, int a, int b, int c)
{
    return scene->clone_sector_named(name, destination, a, b != 0, static_cast<uint32_t>(c));
}
// 0x0049CF80: delete the sector `name`; false if it isn't in the scene
inline bool remove_scene_sector(render::WorldScene* scene, uint32_t name) { return scene->delete_sector_named(name); }
// 0x0049D780: the super sectors brought up to date (the collision rebuilt; a list of sectors to add - none)
inline void get_update_super_sectors(render::WorldScene* scene, void* added)
{
    scene->get_update_super_sectors(static_cast<memory::ListNode*>(added));
}
// 0x0049CE60: the scene's bounding box
inline void assign_scene_bounds(render::WorldScene* scene, const void* box)
{
    scene->init_collision_grid(static_cast<const maths::BBox*>(box));
}
// 0x0049D0B0: a box (a pair of points) of the scene's - `which` and `flag` as the park editor passes them
inline void set_box(render::WorldScene* scene, const void* points, int which, int flag)
{
    scene->set_sectors_in_box(static_cast<const maths::Vec4*>(points), static_cast<uint32_t>(which), flag);
}

} // namespace park_editing::scene

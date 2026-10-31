#pragma once

#include <cstdint>

#include "engine/gfx/engine.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/sector.h"
#include "engine/maths/vec4.h"

// The engine's scene calls the park editor's pieces and generator make (Nx). Sectors are looked
// up by name; the park's pieces are clones of the park editor's own sectors.
namespace park_editing::scene {

inline render::WorldCell* as_sector(uint8_t* sector) { return reinterpret_cast<render::WorldCell*>(sector); }
inline render::WorldScene* as_scene(uint8_t* scene) { return reinterpret_cast<render::WorldScene*>(scene); }

// A virtual call through a geometry's vtable (the engine's dispatchers `MOV EAX,[ECX]; JMP [EAX + 4 * slot]`).
template <typename Fn>
inline Fn geom_slot(const uint8_t* geometry, int slot)
{
    return reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(geometry))[slot]);
}

// 0x0048C510: the sector named `name` in any scene (null if none)
inline uint8_t* look_up_sector(uint32_t name) { return static_cast<uint8_t*>(render::engine::look_up_sector(name)); }
// 0x0048C700: the scene named `name` (null if none)
inline uint8_t* look_up_scene(uint32_t name) { return reinterpret_cast<uint8_t*>(render::engine::find_scene(name)); }

// WorldCell:
// 0x0049E760: quarter turns about y
inline void spin_around_up(uint8_t* sector, int rot) { as_sector(sector)->spin_around_up(rot); }
// 0x0049E9B0: drawn and collided with, or neither
inline void change_live(uint8_t* sector, bool active) { as_sector(sector)->change_live(active); }
// 0x0049E680: the viewports it shows in (a mask)
inline void toggle_shown(uint8_t* sector, uint32_t mask) { as_sector(sector)->toggle_shown(mask); }
// 0x0049E6C0: where it is
inline void place_at_absolute_point(uint8_t* sector, const maths::Vec4* position)
{
    as_sector(sector)->place_at_absolute_point(position);
}
// 0x0049E7C0: its scale (x y z)
inline void change_scale(uint8_t* sector, const maths::Vec4* scale) { as_sector(sector)->change_scale(scale); }
// 0x004A2BA0 (a folded getter, +0x14): its geometry (null if none)
inline uint8_t* fetch_shape(uint8_t* sector) { return static_cast<uint8_t*>(as_sector(sector)->geom_handle_); }

// CGeom (virtual):
// 0x0048DEE0 (slot 20): its orientation (a 4 x 4 matrix)
inline void change_facing(uint8_t* geometry, const void* matrix)
{
    geom_slot<void(__thiscall*)(void*, const void*)>(geometry, 20)(geometry, matrix);
}
// Vtable +0x18: a colour (r g b a in the bytes, low first) over the geometry
inline void change_tint(uint8_t* geometry, uint32_t rgba)
{
    geom_slot<void(__thiscall*)(void*, uint32_t)>(geometry, 6)(geometry, rgba);
}
// 0x0048D060 (slot 8): the colour taken off
inline void wipe_tint(uint8_t* geometry) { geom_slot<void(__thiscall*)(void*)>(geometry, 8)(geometry); }

// WorldScene:
// 0x0049DF30: a copy of the sector `name` (of this scene) in `destination`; its name
inline uint32_t duplicate_scene_sector(uint8_t* scene, uint32_t name, uint8_t* destination, int a, int b, int c)
{
    return as_scene(scene)->clone_sector_named(name, as_scene(destination), a, b != 0, static_cast<uint32_t>(c));
}
// 0x0049CF80: delete the sector `name`; false if it isn't in the scene
inline bool remove_scene_sector(uint8_t* scene, uint32_t name) { return as_scene(scene)->delete_sector_named(name); }
// 0x0049D780: the super sectors brought up to date (the collision rebuilt; a list of sectors to add - none)
inline void get_update_super_sectors(uint8_t* scene, void* added)
{
    as_scene(scene)->get_update_super_sectors(static_cast<memory::ListNode*>(added));
}
// 0x0049CE60: the scene's bounding box
inline void assign_scene_bounds(uint8_t* scene, const void* box)
{
    as_scene(scene)->init_collision_grid(static_cast<const maths::BBox*>(box));
}
// 0x0049D0B0: a box (a pair of points) of the scene's - `which` and `flag` as the park editor passes them
inline void set_box(uint8_t* scene, const void* points, int which, int flag)
{
    as_scene(scene)->set_sectors_in_box(static_cast<const maths::Vec4*>(points), static_cast<uint32_t>(which), flag);
}

} // namespace park_editing::scene

#pragma once

#include <cstdint>

#include "game/gfx/engine.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/sector.h"
#include "game/mth/vector.h"

// The engine's scene calls the park editor's pieces and generator make (Nx). Sectors are looked
// up by name; the park's pieces are clones of the park editor's own sectors.
namespace Ed::Scene {

inline Nx::CSector* AsSector(uint8_t* sector) { return reinterpret_cast<Nx::CSector*>(sector); }
inline Nx::CScene* AsScene(uint8_t* scene) { return reinterpret_cast<Nx::CScene*>(scene); }

// A virtual call through a geometry's vtable (the engine's dispatchers `MOV EAX,[ECX]; JMP [EAX + 4 * slot]`).
template <typename Fn>
inline Fn GeomSlot(const uint8_t* geometry, int slot)
{
    return reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(geometry))[slot]);
}

// 0x0048C510: the sector named `name` in any scene (null if none)
inline uint8_t* GetSector(uint32_t name) { return static_cast<uint8_t*>(Nx::Engine::GetSector(name)); }
// 0x0048C700: the scene named `name` (null if none)
inline uint8_t* GetScene(uint32_t name) { return reinterpret_cast<uint8_t*>(Nx::Engine::FindScene(name)); }

// CSector:
// 0x0049E760: quarter turns about y
inline void SetYRotation(uint8_t* sector, int rot) { AsSector(sector)->SetYRotation(rot); }
// 0x0049E9B0: drawn and collided with, or neither
inline void SetActive(uint8_t* sector, bool active) { AsSector(sector)->SetActive(active); }
// 0x0049E680: the viewports it shows in (a mask)
inline void SetVisibility(uint8_t* sector, uint32_t mask) { AsSector(sector)->SetVisibility(mask); }
// 0x0049E6C0: where it is
inline void SetWorldPosition(uint8_t* sector, const Mth::Vector* position) { AsSector(sector)->SetWorldPosition(position); }
// 0x0049E7C0: its scale (x y z)
inline void SetScale(uint8_t* sector, const Mth::Vector* scale) { AsSector(sector)->SetScale(scale); }
// 0x004A2BA0 (a folded getter, +0x14): its geometry (null if none)
inline uint8_t* GetGeometry(uint8_t* sector) { return static_cast<uint8_t*>(AsSector(sector)->mp_geom); }

// CGeom (virtual):
// 0x0048DEE0 (slot 20): its orientation (a 4 x 4 matrix)
inline void SetOrientation(uint8_t* geometry, const void* matrix)
{
    GeomSlot<void(__thiscall*)(void*, const void*)>(geometry, 20)(geometry, matrix);
}
// Vtable +0x18: a colour (r g b a in the bytes, low first) over the geometry
inline void SetColor(uint8_t* geometry, uint32_t rgba)
{
    GeomSlot<void(__thiscall*)(void*, uint32_t)>(geometry, 6)(geometry, rgba);
}
// 0x0048D060 (slot 8): the colour taken off
inline void ClearColor(uint8_t* geometry) { GeomSlot<void(__thiscall*)(void*)>(geometry, 8)(geometry); }

// CScene:
// 0x0049DF30: a copy of the sector `name` (of this scene) in `destination`; its name
inline uint32_t CloneSector(uint8_t* scene, uint32_t name, uint8_t* destination, int a, int b, int c)
{
    return AsScene(scene)->CloneSectorNamed(name, AsScene(destination), a, b != 0, static_cast<uint32_t>(c));
}
// 0x0049CF80: delete the sector `name`; false if it isn't in the scene
inline bool DeleteSector(uint8_t* scene, uint32_t name) { return AsScene(scene)->DeleteSectorNamed(name); }
// 0x0049D780: the super sectors brought up to date (the collision rebuilt; a list of sectors to add - none)
inline void UpdateSuperSectors(uint8_t* scene, void* added)
{
    AsScene(scene)->UpdateSuperSectors(static_cast<Mem::ListNode*>(added));
}
// 0x0049CE60: the scene's bounding box
inline void SetBoundingBox(uint8_t* scene, const void* box)
{
    AsScene(scene)->InitCollisionGrid(static_cast<const Mth::BBox*>(box));
}
// 0x0049D0B0: a box (a pair of points) of the scene's - `which` and `flag` as the park editor passes them
inline void SetBox(uint8_t* scene, const void* points, int which, int flag)
{
    AsScene(scene)->SetSectorsInBox(static_cast<const Mth::Vector*>(points), static_cast<uint32_t>(which), flag);
}

} // namespace Ed::Scene

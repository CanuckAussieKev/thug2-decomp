#pragma once

// The engine's scene calls the park editor's pieces and generator make (Nx: not rewritten yet). Sectors are looked
// up by name; the park's pieces are clones of the park editor's own sectors.

#include <cstdint>

#include "game/mth/vector.h"
#include "hook/game.h"

namespace Ed::Scene {

// 0x0048C510: the sector named `name` in any scene (null if none)
inline uint8_t* GetSector(uint32_t name) { return GameFunction<uint8_t*(__cdecl*)(uint32_t)>(0x0048C510)(name); }
// 0x0048C700: the scene named `name` (null if none)
inline uint8_t* GetScene(uint32_t name) { return GameFunction<uint8_t*(__cdecl*)(uint32_t)>(0x0048C700)(name); }

// CSector (thiscall):
// 0x0049E760: quarter turns about y
inline void SetYRotation(uint8_t* sector, int rot) { GameFunction<void(__thiscall*)(void*, int)>(0x0049E760)(sector, rot); }
// 0x0049E9B0: drawn and collided with, or neither
inline void SetActive(uint8_t* sector, bool active)
{
    GameFunction<void(__thiscall*)(void*, bool)>(0x0049E9B0)(sector, active);
}
// 0x0049E680: the viewports it shows in (a mask)
inline void SetVisibility(uint8_t* sector, uint32_t mask)
{
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0049E680)(sector, mask);
}
// 0x0049E6C0: where it is
inline void SetWorldPosition(uint8_t* sector, const Mth::Vector* position)
{
    GameFunction<void(__thiscall*)(void*, const Mth::Vector*)>(0x0049E6C0)(sector, position);
}
// 0x0049E7C0: its scale (x y z)
inline void SetScale(uint8_t* sector, const Mth::Vector* scale)
{
    GameFunction<void(__thiscall*)(void*, const Mth::Vector*)>(0x0049E7C0)(sector, scale);
}
// 0x004A2BA0 (a folded getter, +0x14): its geometry (null if none)
inline uint8_t* GetGeometry(uint8_t* sector) { return GameFunction<uint8_t*(__thiscall*)(void*)>(0x004A2BA0)(sector); }

// CGeom (thiscall):
// 0x0048DEE0: its orientation (a 4 x 4 matrix)
inline void SetOrientation(uint8_t* geometry, const void* matrix)
{
    GameFunction<void(__thiscall*)(void*, const void*)>(0x0048DEE0)(geometry, matrix);
}
// Vtable +0x18: a colour (r g b a in the bytes, low first) over the geometry
inline void SetColor(uint8_t* geometry, uint32_t rgba)
{
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>((*reinterpret_cast<void***>(geometry))[6])(geometry, rgba);
}
// 0x0048D060 (a jump to vtable +0x20, folded): the colour taken off
inline void ClearColor(uint8_t* geometry) { GameFunction<void(__thiscall*)(void*)>(0x0048D060)(geometry); }

// CScene (thiscall):
// 0x0049DF30: a copy of the sector `name` (of this scene) in `destination`; its name
inline uint32_t CloneSector(uint8_t* scene, uint32_t name, uint8_t* destination, int a, int b, int c)
{
    return GameFunction<uint32_t(__thiscall*)(void*, uint32_t, void*, int, int, int)>(0x0049DF30)(scene, name,
                                                                                                    destination, a, b, c);
}
// 0x0049CF80: delete the sector `name`; false if it isn't in the scene
inline bool DeleteSector(uint8_t* scene, uint32_t name)
{
    return GameFunction<bool(__thiscall*)(void*, uint32_t)>(0x0049CF80)(scene, name);
}
// 0x0049D780: the super sectors brought up to date (the collision rebuilt; a list of sectors to add - none)
inline void UpdateSuperSectors(uint8_t* scene, void* added)
{
    GameFunction<void(__thiscall*)(void*, void*)>(0x0049D780)(scene, added);
}
// 0x0049CE60: the scene's bounding box
inline void SetBoundingBox(uint8_t* scene, const void* box)
{
    GameFunction<void(__thiscall*)(void*, const void*)>(0x0049CE60)(scene, box);
}
// 0x0049D0B0: a box (a pair of points) of the scene's - `which` and `flag` as the park editor passes them
inline void SetBox(uint8_t* scene, const void* points, int which, int flag)
{
    GameFunction<void(__thiscall*)(void*, const void*, int, int)>(0x0049D0B0)(scene, points, which, flag);
}

} // namespace Ed::Scene

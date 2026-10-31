#pragma once

// Shared by the rail editor's source files (rail_editor*.cpp): its pools, flags and the sector calls it makes.

#include <cstdint>

#include "game/core/pool.h"
#include "game/gfx/sector.h"
#include "game/script/struct.h"

namespace Ed {
namespace RailEditorInternal {

// The points' and rails' CPoolable pool pairs.
Mem::PoolPair& PointPools();
Mem::PoolPair& RailPools();
// 0x0067F000: whether deleting a rail sector also brings the super sectors up to date (DestroyEditedRailSectors'
// DoNotUpdateSuperSectors clears it while it runs).
bool& UpdateSuperSectors();
// A rail sector deleted from the park (none: nothing), the piece counts marked dirty, the collision rebuilt when
// UpdateSuperSectors; the pointer cleared.
void DeleteSector(uint8_t*& sector);

constexpr uint32_t kGraffitiColors = 0x7F1BA1AA; // graffitiColors

// An Nx::CSector's: its checksum (+0x04); its geometry (+0x14) told a colour (its vtable +0x18); the colour cleared
// (0x0049E670); the viewports it shows in (0x0049E680, a mask); drawn and collided with or not (0x0049E9B0).
inline uint32_t SectorName(const uint8_t* sector) { return *reinterpret_cast<const uint32_t*>(sector + 0x04); }
inline void SetSectorColour(uint8_t* sector, uint32_t rgba)
{
    void* geometry = *reinterpret_cast<void**>(sector + 0x14);
    if (geometry) {
        void** vtable = *static_cast<void***>(geometry);
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(vtable[6])(geometry, rgba);
    }
}
inline void ClearSectorColour(uint8_t* sector) { reinterpret_cast<Nx::CSector*>(sector)->ClearColor(); }
inline void SetSectorVisibility(uint8_t* sector, uint32_t mask)
{
    reinterpret_cast<Nx::CSector*>(sector)->SetVisibility(mask);
}
inline void SetSectorActive(uint8_t* sector, bool active)
{
    reinterpret_cast<Nx::CSector*>(sector)->SetActive(active);
}

} // namespace RailEditorInternal
} // namespace Ed

#pragma once

// Shared by the rail editor's source files (rail_editor*.cpp): its pools, flags and the sector calls it makes.

#include <cstdint>

#include "engine/core/slab_bank.h"
#include "engine/gfx/sector.h"
#include "engine/script/param_struct.h"

namespace park_editing {
namespace rail_editor_internal {

// The points' and rails' CPoolable pool pairs.
memory::PoolPair& point_pools();
memory::PoolPair& rail_pools();
// 0x0067F000: whether deleting a rail sector also brings the super sectors up to date (DestroyEditedRailSectors'
// DoNotUpdateSuperSectors clears it while it runs).
bool& get_update_super_sectors();
// A rail sector deleted from the park (none: nothing), the piece counts marked dirty, the collision rebuilt when
// UpdateSuperSectors; the pointer cleared.
void remove_scene_sector(uint8_t*& sector);

constexpr uint32_t kGraffitiColors = 0x7F1BA1AA; // graffitiColors

// An render::WorldCell's: its checksum (+0x04); its geometry (+0x14) told a colour (its vtable +0x18); the colour cleared
// (0x0049E670); the viewports it shows in (0x0049E680, a mask); drawn and collided with or not (0x0049E9B0).
inline uint32_t sector_key_from_spec(const uint8_t* sector)
{
    return *reinterpret_cast<const uint32_t*>(sector + 0x04);
}
inline void set_sector_colour(uint8_t* sector, uint32_t rgba)
{
    void* geometry = *reinterpret_cast<void**>(sector + 0x14);
    if (geometry) {
        void** vtable = *static_cast<void***>(geometry);
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(vtable[6])(geometry, rgba);
    }
}
inline void clear_sector_colour(uint8_t* sector) { reinterpret_cast<render::WorldCell*>(sector)->wipe_tint(); }
inline void set_sector_visibility(uint8_t* sector, uint32_t mask)
{
    reinterpret_cast<render::WorldCell*>(sector)->toggle_shown(mask);
}
inline void enable_sector(uint8_t* sector, bool active)
{
    reinterpret_cast<render::WorldCell*>(sector)->change_live(active);
}

} // namespace rail_editor_internal
} // namespace park_editing

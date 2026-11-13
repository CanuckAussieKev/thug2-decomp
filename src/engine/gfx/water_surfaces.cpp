// NOT ORIGINAL - water shading's contract (water_surfaces.h).
#include "engine/gfx/water_surfaces.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace render::water {

namespace {

struct WaterSlot {
    std::atomic<bool> wanted{false};
    std::atomic<const GpuTexture*> texture{nullptr};
    std::atomic<uint32_t> material{0};
    std::atomic<uint32_t> materials{0};
    std::mutex level_mutex;
    LevelWater level;
};

WaterSlot& slot()
{
    static WaterSlot state;
    return state;
}

} // namespace

void set_wanted(bool wanted)
{
    slot().wanted.store(wanted, std::memory_order_relaxed);
    if (!wanted) {
        set_current(nullptr, 0);
    }
}

bool wanted() { return slot().wanted.load(std::memory_order_relaxed); }

void set_current(const GpuTexture* texture, uint32_t material)
{
    slot().texture.store(texture, std::memory_order_relaxed);
    slot().material.store(material, std::memory_order_relaxed);
}

const GpuTexture* current_texture() { return slot().texture.load(std::memory_order_relaxed); }

uint32_t current_material() { return slot().material.load(std::memory_order_relaxed); }

void set_level_water(const LevelWater& level)
{
    WaterSlot& own = slot();
    const std::scoped_lock lock(own.level_mutex);
    own.level = level;
}

LevelWater level_water()
{
    WaterSlot& own = slot();
    const std::scoped_lock lock(own.level_mutex);
    return own.level;
}

void note_water_material() { slot().materials.fetch_add(1, std::memory_order_relaxed); }

uint32_t water_materials() { return slot().materials.load(std::memory_order_relaxed); }

} // namespace render::water

// NOT ORIGINAL - surface maps' contract (surface_maps.h).
#include "engine/gfx/surface_maps.h"

#include <atomic>

namespace render::surface {

namespace {

std::atomic<SurfaceMaps*>& installed()
{
    static std::atomic<SurfaceMaps*> maps{nullptr};
    return maps;
}

} // namespace

SurfaceMaps* surface_maps() { return installed().load(std::memory_order_acquire); }

void set_surface_maps(SurfaceMaps* maps) { installed().store(maps, std::memory_order_release); }

} // namespace render::surface

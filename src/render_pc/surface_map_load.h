#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/gfx/gpu_handles.h"
#include "formats/surface_map_format.h"

// NOT ORIGINAL - a texture dictionary's surface maps (formats/surface_map_format.h) read as the dictionary loads and
// attached to its textures (engine/gfx/surface_maps.h). Nothing is read while no backend takes them (Direct3D 9, the
// Vulkan Gpu with [Graphics] rt_pbr=0): the dictionary loaders then do as before.
namespace render {

class SurfacePack {
public:
    // The maps beside the dictionary `name` ("<name>.pbr"), when a backend takes surface maps and the file is there.
    explicit SurfacePack(const char* name);
    // Whether any texture can get a map here (callers skip hashing their texels when not).
    [[nodiscard]] bool active() const { return !entries_.empty(); }
    // `texture`'s map attached when the pack has one made from these texels (its first level as the dictionary
    // stores it); counted for the log.
    void attach(GpuTexture* texture, uint32_t checksum, std::span<const uint8_t> first_level);
    // One log line for the dictionary (the maps attached, those whose texture has other texels now); none if inactive.
    void report() const;

private:
    std::string name_;
    std::vector<uint8_t> file_;
    std::vector<surface_map_format::Entry> entries_;
    uint32_t attached_ = 0;
    uint32_t stale_ = 0;
};

} // namespace render

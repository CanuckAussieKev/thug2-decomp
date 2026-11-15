#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
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
    // stores it) - or, for a dictionary a texture set serves (engine/gfx/texture_sets.h: its upscaled texels), from
    // the original texture of that checksum in the game's own dictionary; counted for the log.
    void attach(GpuTexture* texture, uint32_t checksum, std::span<const uint8_t> first_level);
    // One log line for the dictionary (the maps attached, those whose texture has other texels now); none if inactive.
    void report() const;

private:
    // (a texture set's dictionary) each texture's original first level's content hash, read from the game's own file
    void read_originals(const char* name);
    std::string name_;
    std::unordered_map<uint32_t, uint64_t> originals_;
    std::string set_;
    // (added for the de-lit texture sets) the textures set_ made de-lit in this dictionary (texture_sets::delit_textures)
    std::vector<uint32_t> delit_;
    std::vector<uint8_t> file_;
    std::vector<surface_map_format::Entry> entries_;
    uint32_t attached_ = 0;
    uint32_t attached_delit_ = 0;
    uint32_t stale_ = 0;
};

} // namespace render

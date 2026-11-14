#pragma once

#include <cstdint>
#include <span>

#include "engine/gfx/gpu_handles.h"

// NOT ORIGINAL - surface maps' contract (docs/design/vulkan.md "Per-pixel lighting and surface maps"): the normal,
// roughness and height maps made offline beside the game's texture dictionaries (formats/surface_map_format.h) handed
// to a backend that lights per pixel. render_pc reads a dictionary's maps as it loads its textures and attaches each to
// its texture; the backend keeps it with the texture (and lets it go with it). API-neutral: no Vulkan here. Direct3D 9
// installs none (surface_maps() is null: nothing is read); the Vulkan Gpu installs one with [Graphics] rt_pbr=1.
//
// Other work streams meet here: change this file only by adding (a member at a struct's end, a virtual with a default
// body).
namespace render::surface {

inline constexpr float kDielectricF0 = 0.04F; // (F0 of a non-metal: 4% reflected at normal incidence)

// What the maps' texels hold (surface_map_format.h's): 4 bytes a texel, rows top first - normal x (along +u), normal y
// (along +v) as 0 - 255 for -1 - 1, roughness 0 - 1, height 0 - 1 (0.5 the surface).
struct MapInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t material_class = 0; // surface_map_format::MaterialClass
    float f0 = kDielectricF0;   // the reflectance at normal incidence the class gives
    // (added by the detail materials - [Graphics] rt_detail) the texture's checksum and content hash (its first level's,
    // surface_map_format::content_hash): the detail-material library lists textures by them
    uint32_t checksum = 0;
    uint64_t content_hash = 0;
    // (added for the characters - [Graphics] rt_characters) surface_map_format::kFlag*: a character model's map (used
    // only with rt_characters on), hair's strands along u, a garment's skin-coloured texels shaded as skin
    uint8_t flags = 0;
};

class SurfaceMaps {
public:
    virtual ~SurfaceMaps() = default;
    // `colour`'s surface map: `texels` as MapInfo says (width x height x 4 bytes). A texture given a second map keeps
    // the new one. False: refused (the backend couldn't make it).
    virtual bool attach(GpuTexture* colour, const MapInfo& info, std::span<const uint8_t> texels) = 0;
};

// The installed backend (null: none - surface maps are not read).
SurfaceMaps* surface_maps();
void set_surface_maps(SurfaceMaps* maps);

} // namespace render::surface

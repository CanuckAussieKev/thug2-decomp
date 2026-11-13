#pragma once

// NOT ORIGINAL - where ray tracing finds a mesh's vertex attributes (rt::Geometry's offsets): worked out from the vertex
// format the buffer is drawn with, as Direct3D lays a flexible vertex format out and the Vulkan Gpu's vertex input reads
// it (gpu_vulkan/vulkan_draw_state.cpp vertex_layout) - the position (and its blend weights), the normal, a point size,
// the diffuse colour, the specular colour, then the texture coordinate sets.
//
// Not a mesh's own normal / colour / uv offsets (MeshRef::normal_offset() ...): those are the scene file's, and for the
// skater's skinned meshes they describe the packed vertex the file stores (normal 24, colour 28, uv 32), not the
// vertex the CPU skinning writes into the buffer the GPU reads (format 0x152 and its sets: normal 12, colour 24, uv
// 28 - render_pc/pc_mesh_render.cpp get_skin). Every reader of the geometry - the hit shaders' albedo, colours and
// alpha tests, the objects' vertex normals, the relit mode's and the baked sun's copies - goes through this one place.

#include <cstdint>

#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/rt_scene.h"

namespace render::rt {

// A vertex format's attributes: each offset in bytes from the vertex's start, or kNoAttribute (absent - or one ray
// tracing can't read: a position that isn't three floats, texture coordinates that aren't two).
inline constexpr uint32_t kNoAttribute = 0xFFFFFFFFU;
struct VertexAttributes {
    uint32_t position = kNoAttribute; // three floats
    uint32_t normal = kNoAttribute;   // three floats
    uint32_t colour = kNoAttribute;   // the diffuse colour, A8R8G8B8
    uint32_t uv = kNoAttribute;       // the first texture coordinate set, two floats
    uint32_t uv_sets = 0;             // the two-float sets one after another from `uv`
    uint32_t bytes = 0;               // the vertex's size as the format lays it out (a buffer's stride may be more)
};

namespace vertex_format_bits {
inline constexpr uint32_t kPositionMask = 0x400EU; // (0x4002: XYZW)
inline constexpr uint32_t kXyzB5 = 0x00EU;         // XYZ with one to five blend weights after: 0x6 - 0xE
inline constexpr uint32_t kPointSize = 0x020U;
inline constexpr uint32_t kTexCountMask = 0xF00U;
inline constexpr uint32_t kTexCountShift = 8;
inline constexpr uint32_t kTexSizeShift = 16; // (two bits a set: 0 two floats, 1 three, 2 four, 3 one)
inline constexpr uint32_t kTexSizeMask = 3;
inline constexpr uint32_t kTexSizeOne = 3;
inline constexpr uint32_t kMaxTexSets = 8;
inline constexpr uint32_t kFloatBytes = 4;
inline constexpr uint32_t kColourBytes = 4;
} // namespace vertex_format_bits

// The attributes of a vertex of format `fvf` (D3DFVF_*).
[[nodiscard]] constexpr VertexAttributes vertex_attributes(uint32_t fvf)
{
    namespace bits = vertex_format_bits;
    VertexAttributes out;
    const uint32_t position = fvf & bits::kPositionMask;
    uint32_t at = 0;
    if (position >= kFvfXyz && position <= bits::kXyzB5 && (position % 2) == 0) {
        out.position = 0;
        at = (3 + ((position - kFvfXyz) / 2)) * bits::kFloatBytes; // (and the blend weights: not read)
    } else if (position == kFvfXyzRhw) {
        at = 4 * bits::kFloatBytes; // (screen positions: nothing to trace)
    } else {
        return out; // (not a format the scene's meshes use)
    }
    if ((fvf & kFvfNormal) != 0U) {
        out.normal = at;
        at += 3 * bits::kFloatBytes;
    }
    if ((fvf & bits::kPointSize) != 0U) {
        at += bits::kFloatBytes;
    }
    if ((fvf & kFvfDiffuse) != 0U) {
        out.colour = at;
        at += bits::kColourBytes;
    }
    if ((fvf & kFvfSpecular) != 0U) {
        at += bits::kColourBytes;
    }
    const uint32_t sets = (fvf & bits::kTexCountMask) >> bits::kTexCountShift;
    bool two_floats = true; // (the sets read as two floats each: those from the first while they are)
    for (uint32_t set = 0; set < sets && set < bits::kMaxTexSets; ++set) {
        const uint32_t code = (fvf >> (bits::kTexSizeShift + (set * 2))) & bits::kTexSizeMask;
        const uint32_t floats = code == bits::kTexSizeOne ? 1U : code + 2U;
        two_floats = two_floats && floats == 2;
        if (two_floats) {
            out.uv = set == 0 ? at : out.uv;
            ++out.uv_sets;
        }
        at += floats * bits::kFloatBytes;
    }
    out.bytes = at;
    return out;
}

// A geometry's attributes (position, normal, colour, texture coordinates and their sets) from the vertex format its
// buffer is drawn with; each one that doesn't fit within the geometry's vertex_stride left out. False: no position of
// three floats - nothing to trace.
inline bool set_vertex_attributes(uint32_t fvf, Geometry* geometry)
{
    const VertexAttributes found = vertex_attributes(fvf);
    const uint32_t stride = geometry->vertex_stride;
    const auto fits = [stride](uint32_t offset, uint32_t bytes) {
        return offset != kNoAttribute && offset + bytes <= stride;
    };
    constexpr uint32_t kThreeFloats = 3 * vertex_format_bits::kFloatBytes;
    constexpr uint32_t kTwoFloats = 2 * vertex_format_bits::kFloatBytes;
    if (!fits(found.position, kThreeFloats)) {
        return false;
    }
    geometry->position_offset = found.position;
    const bool normal = fits(found.normal, kThreeFloats);
    geometry->normal_offset = normal ? found.normal : 0U;
    geometry->normal_format = normal ? kNormalFloat3 : kAttributeNone;
    const bool colour = fits(found.colour, vertex_format_bits::kColourBytes);
    geometry->colour_offset = colour ? found.colour : 0U;
    geometry->colour_format = colour ? kColourA8R8G8B8 : kAttributeNone;
    const bool uv = fits(found.uv, kTwoFloats);
    geometry->uv_offset = uv ? found.uv : 0U;
    geometry->uv_format = uv ? kUvFloat2 : kAttributeNone;
    geometry->uv_sets = uv ? found.uv_sets : 0U;
    return true;
}

} // namespace render::rt

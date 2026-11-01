#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/byte_stream.h"

// NOT ORIGINAL - the skeleton format (.ske, PC: "x.ske.xbx"), read through util::ByteReader.
//
// A skeleton (little-endian): u32 version, u32 flags (bit 0: mirror data), u32 bone count, then the bones' name
// checksums, their parents' and their mirror bones' (0: none) - count u32s each - then each bone's bind pose
// {float rotation quaternion x, y, z, w; float translation x, y, z, w} (relative to its parent).
//
// Read by gfx::RigData::read_in (engine/gfx/rig.cpp), which holds up to kMaxBones.
namespace skeleton_format {

constexpr int kMaxBones = 64;

struct BindPose {
    float rotation[4] = {};
    float translation[4] = {};
};

struct Skeleton {
    uint32_t version = 0;
    uint32_t flags = 0;
    int count = 0;
    uint32_t names[kMaxBones] = {};
    uint32_t parents[kMaxBones] = {};
    uint32_t mirrors[kMaxBones] = {};
    BindPose poses[kMaxBones];
};

// False if cut short or the count is more than kMaxBones (or negative).
bool read_skeleton(util::ByteReader& reader, Skeleton* out);

} // namespace skeleton_format

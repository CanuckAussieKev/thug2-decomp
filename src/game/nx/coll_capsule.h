#pragma once

// The capsule queries: a standing capsule against triangles and the static collision (slot 17).

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/vector.h"

namespace Nx {

// Where a capsule touches a face (0x3C bytes): the face's nearest point, the offset to it from the segment's
// nearest point, a time (+0x20: left as it was - only written for a moving capsule, which slot 17 never takes),
// the face's normal, its object's checksum and flag 0x40.
struct CapsuleHit {
    Mth::Vector point;
    Mth::Vector offset;      // +0x10
    float t;                 // +0x20
    Mth::Vector normal;      // +0x24
    uint32_t checksum;       // +0x34
    uint8_t faceFlag40;      // +0x38
    uint8_t pad[3];
};
static_assert(sizeof(CapsuleHit) == 0x3C, "layout must match the original");

// What a capsule query fills (0x7A4 bytes): up to 32 hits; a move (direction and sweep: the faces are only
// tested when the sweep is 0), the flags of faces to pass over or to require, and whether to stop at the first hit
// (mode 2).
struct CapsuleHits {
    static constexpr int kMost = 32;

    uint32_t unknown00;
    CapsuleHit hits[kMost];  // +0x004
    Mth::Vector direction;   // +0x784
    float sweep;             // +0x794
    int count;               // +0x798
    uint16_t ignoreFlags;    // +0x79C
    uint16_t requireFlags;   // +0x79E
    int mode;                // +0x7A0
};
static_assert(sizeof(CapsuleHits) == 0x7A4 && offsetof(CapsuleHits, count) == 0x798 &&
                  offsetof(CapsuleHits, mode) == 0x7A0,
              "layout must match the original");

// 0x0040FC00 (cdecl): the capsule against the triangle v0 v1 v2: the face's unit normal (v1 - v0) x (v2 - v0) to
// `normal` (false for a degenerate face, `normal` then unnormalised), the nearest points of the capsule's segment
// and the face (SegmentTriangleDistanceSquared): the face's to `point`, it minus the segment's to `offset`. True
// when the segment is in front of the face (offset against the normal) and within the radius of it.
bool CapsuleHitsTriangle(const Mth::Capsule* capsule, const Mth::Vector* v0, const Mth::Vector* v1,
                         const Mth::Vector* v2, Mth::Vector* point, Mth::Vector* normal, Mth::Vector* offset);

} // namespace Nx

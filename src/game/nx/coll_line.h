#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"

namespace Nx {

class CCollObj;
struct CollisionHit;

// Called (cdecl) with the record each time a line test accepts a hit.
using CollisionHitCallback = void(__cdecl*)(CollisionHit* hit);

// What a line test fills in (0x50 bytes; the caller sets the flags, the distance to beat and the callback): the
// nearest hit so far. (Our name: the original's isn't known.)
struct CollisionHit {
    Mth::Vector normal;             // +0x00 the face's normal (world space)
    Mth::Vector point;              // +0x10 where the line hits (w = 1)
    uint32_t face;                  // +0x20 the face's index in its geometry
    float distance;                 // +0x24 along the line, 0..1 (the one to beat)
    uint16_t ignoreFlags;           // +0x28 faces with any of these flags are passed over
    uint16_t requireFlags;          // +0x2A ... and faces without all of these
    uint16_t faceFlags;             // +0x2C the hit face's
    uint8_t terrain;                // +0x2E the hit face's (low byte)
    uint8_t pad2F;
    CCollObj* object;               // +0x30 what was hit
    uint32_t objectChecksum;        // +0x34 its checksum
    uint32_t unknown38;             // +0x38
    CollisionHitCallback callback;  // +0x3C
    uint32_t callbackData;          // +0x40 (for the callback)
    uint32_t owner;                 // +0x44 while a movable object is tested: its owner (slot 10), else 0
    bool hit;                       // +0x48 a face was accepted
    uint8_t faceFlag40;             // +0x49 the hit face's flag 0x40 (0 or 0x40)
    uint8_t pad4A[2];
    uint32_t scene;                 // +0x4C the scene whose grid held the static object hit (CollideWithStatics)

    // 0x004F8160: every field copied (not the padding). Returns this.
    CollisionHit* CopyFrom(const CollisionHit& other);
};
static_assert(sizeof(CollisionHit) == 0x50 && offsetof(CollisionHit, hit) == 0x48, "layout must match the original");

// A face the line crosses, as a line test hands it on: its normal, where it's crossed, and its index.
struct FaceHit {
    Mth::Vector normal;
    Mth::Vector point;
    uint32_t face;
};
static_assert(sizeof(FaceHit) == 0x24, "layout must match the original");

// 0x004131F0 (cdecl): a face the line hits at `distance` (0..1 along it) offered to `hit`. It's taken (the
// record filled - the point recomputed from the line - and the callback called) unless the face's flags include
// one of the ignored ones or lack a required one; but unless it's nearer than the record's distance (or not
// nearer, while 0x00691910 asks for the farthest), the record is put back as it was afterwards and false
// returned - the callback has still run.
bool ReportHit(const Mth::Line& line, CCollObj* object, const FaceHit& face, float distance, CollisionHit* hit);

extern bool g_wantFarthest;

} // namespace Nx

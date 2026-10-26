#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"

// The PC model (Nx engine): a model's geoms, and their bounding spheres together.
namespace Nx {

// A node of a model's geom list (12 bytes, a ring with a head of its own): what it holds, the next and the previous
// node. What it holds starts with the geom.
struct sModelGeomNode {
    void** entry; // the entry (its first word the geom)
    sModelGeomNode* next;
    sModelGeomNode* prev;
};
static_assert(sizeof(sModelGeomNode) == 0xC, "layout must match the original");

// The PC model (0x50 bytes, vtable 0x0064A4B4; the engine's CModel - constructor 0x00494C40, vtable 0x00649348,
// destructor body 0x00494D00 - for its first 0x4C bytes: +0x10 the geoms' list head, +0x46 how many).
struct CPcModel {
    void** vtable;
    uint8_t base04[0xC];
    sModelGeomNode* geoms; // +0x10: the list's head (its own node)
    uint8_t base14[0x32];
    uint16_t geomCount;    // +0x46
    uint8_t base48[4];
    uint32_t unknown4C;    // +0x4C (cleared)

    // 0x004BAD90: construct. Returns this.
    CPcModel* Construct();
    // 0x004BADB0: the destructor's body (the engine's 0x00494D00).
    void DestroyBody();
    // 0x004BADC0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcModel* Destroy(unsigned int flags);
    // 0x004BADF0 (virtual [13]): every geom's bounding sphere set to `sphere`.
    void SetBoundingSphere(const Mth::Vector* sphere);
    // 0x004BAE30 (virtual [12]): the geoms' bounding spheres together (zero without geoms): each next one merged in
    // unless it's inside - the one so far taken when it's inside the next. The radius comes back doubled, as the
    // original. Returns out.
    Mth::Vector* GetBoundingSphere(Mth::Vector* out);
};
static_assert(offsetof(CPcModel, geoms) == 0x10 && offsetof(CPcModel, geomCount) == 0x46 &&
                  sizeof(CPcModel) == 0x50,
              "layout must match the original");

} // namespace Nx

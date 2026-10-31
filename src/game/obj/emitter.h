#pragma once

// Sound emitter nodes (0x004680E0-0x004685C5): level nodes (class EmitterObject) that sounds can be played from -
// a box (the level sector of the same name), or a sphere of Radius about the node - kept in a hash table by name
// (0x006F2260). A sound played "from" one sounds from the shape's nearest point to the listener.
//
// And the follow trail (0x004685D0-0x00468AFE): a ring of the last 64 positions an object has passed through (one
// every 40 units), and the point and facing a given distance back along it.

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Lst {
class HashTable;
}

namespace Obj {

enum EEmitterShape : int {
    EMITTER_SHAPE_BOX = 0,     // the sector's box (Shape 0xE460ABDE)
    EMITTER_SHAPE_OTHER = 1,   // (Shape 0x03F1E5A7: never inside, no nearest point)
    EMITTER_SHAPE_SPHERE = 2,  // (the default, and Shape 0xAA069978)
};

struct EmitterNode {
    // 0x00468120: whether `point` is in the shape.
    bool IsInside(const Mth::Vector* point);
    // 0x004681A0: `point` itself when it's inside, else the shape's nearest point (the box's surface, the
    // sphere's). The other shape leaves `out` as it was (the original copies its uninitialised locals).
    Mth::Vector* GetClosestPoint(Mth::Vector* out, const Mth::Vector* point);

    uint32_t m_name;          // +0x00
    Mth::Vector m_pos;        // +0x04
    uint8_t m_unknown14[0x10];
    bool m_active;            // +0x24
    uint8_t m_unknown25[3];
    int m_shape;              // +0x28 EEmitterShape
    float m_radius;           // +0x2C
    float m_radiusSquared;    // +0x30
    void* m_sector;           // +0x34
    Mth::BBox m_box;          // +0x38
    uint32_t m_unknown58;
    uint32_t m_script;        // +0x5C
};
static_assert(offsetof(EmitterNode, m_active) == 0x24 && offsetof(EmitterNode, m_sector) == 0x34 &&
                  offsetof(EmitterNode, m_box) == 0x38 && offsetof(EmitterNode, m_script) == 0x5C &&
                  sizeof(EmitterNode) == 0x60,
              "layout must match the original");

// 0x004680E0: the emitter named `name` (null if none).
EmitterNode* Emitter_GetNode(uint32_t name);
// 0x004682B0: every emitter freed and taken out of the table.
void Emitter_Cleanup();
// 0x00468340: the emitter named `name` made active or not.
void Emitter_SetActive(uint32_t name, int active);
// 0x004683B0: the table made (16 buckets), or emptied if there is one.
void Emitter_Init();
// 0x00468450: an emitter for the level node `name`, from its parameters.
void Emitter_AddNode(uint32_t name, bool active);

class CFollowTrail {
public:
    // 0x00468AC0 (returns this) / 0x004685D0: empty, following nothing.
    CFollowTrail* Init();
    void Reset();
    // 0x00468610: the followed object's position added once it's over 40 units (1600 squared) from the last.
    void Update();
    // 0x00468730: the point m_distance back along the trail (from the object's position through the trail,
    // newest first) and, in `matrix`, a facing along it: the old facing's front x10 plus the direction, made
    // level (y = 0) unless m_freeFacing, with up (0, 1, 0) - nothing when the trail is empty or that point's
    // segment has no length.
    void GetPosition(Mth::Vector* position, Mth::Matrix* matrix);

    Mth::Vector m_points[64];  // +0x000: a ring, newest at m_head
    int m_count;               // +0x400
    int m_head;                // +0x404
    uint32_t m_objectId;       // +0x408
    Mth::Vector m_target;      // +0x40C: the object's position at the last Update
    float m_distance;          // +0x41C
    bool m_freeFacing;         // +0x420
};
static_assert(offsetof(CFollowTrail, m_count) == 0x400 && offsetof(CFollowTrail, m_target) == 0x40C &&
                  offsetof(CFollowTrail, m_distance) == 0x41C && offsetof(CFollowTrail, m_freeFacing) == 0x420,
              "layout must match the original");

extern void* g_emitterTable;
} // namespace Obj

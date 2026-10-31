#pragma once

// Sound emitter nodes (0x004680E0-0x004685C5): level nodes (class EmitterObject) that sounds can be played from -
// a box (the level sector of the same name), or a sphere of Radius about the node - kept in a hash table by name
// (0x006F2260). A sound played "from" one sounds from the shape's nearest point to the listener.
//
// And the follow trail (0x004685D0-0x00468AFE): a ring of the last 64 positions an object has passed through (one
// every 40 units), and the point and facing a given distance back along it.

#include <cstddef>
#include <cstdint>

#include "game/maths/shape_tests.h"
#include "game/maths/mat4.h"
#include "game/maths/vec4.h"

namespace ListKit {
class KeyMap;
}

namespace GameObjects {

enum EEmitterShape : int {
    EMITTER_SHAPE_BOX = 0,     // the sector's box (Shape 0xE460ABDE)
    EMITTER_SHAPE_OTHER = 1,   // (Shape 0x03F1E5A7: never inside, no nearest point)
    EMITTER_SHAPE_SPHERE = 2,  // (the default, and Shape 0xAA069978)
};

struct EmitterNode {
    // 0x00468120: whether `point` is in the shape.
    bool IsWithin(const Maths::Vec4* point);
    // 0x004681A0: `point` itself when it's inside, else the shape's nearest point (the box's surface, the
    // sphere's). The other shape leaves `out` as it was (the original copies its uninitialised locals).
    Maths::Vec4* NearestPointOn(Maths::Vec4* out, const Maths::Vec4* point);

    uint32_t m_label;          // +0x00
    Maths::Vec4 m_atPoint;        // +0x04
    uint8_t m_unknown14[0x10];
    bool m_active;            // +0x24
    uint8_t m_unknown25[3];
    int m_form;              // +0x28 EEmitterShape
    float m_radius;           // +0x2C
    float m_reachSq;    // +0x30
    void* m_sector;           // +0x34
    Maths::BBox m_box;          // +0x38
    uint32_t m_unknown58;
    uint32_t m_ownScript;        // +0x5C
};
static_assert(offsetof(EmitterNode, m_active) == 0x24 && offsetof(EmitterNode, m_sector) == 0x34 &&
                  offsetof(EmitterNode, m_box) == 0x38 && offsetof(EmitterNode, m_ownScript) == 0x5C &&
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
    CFollowTrail* PrepareAll();
    void Reset();
    // 0x00468610: the followed object's position added once it's over 40 units (1600 squared) from the last.
    void TickFrame();
    // 0x00468730: the point m_reach back along the trail (from the object's position through the trail,
    // newest first) and, in `matrix`, a facing along it: the old facing's front x10 plus the direction, made
    // level (y = 0) unless m_freeFacing, with up (0, 1, 0) - nothing when the trail is empty or that point's
    // segment has no length.
    void FetchPlace(Maths::Vec4* position, Maths::Mat4* matrix);

    Maths::Vec4 m_pathVertices[64];  // +0x000: a ring, newest at m_head
    int m_num;               // +0x400
    int m_head;                // +0x404
    uint32_t m_entityKey;       // +0x408
    Maths::Vec4 m_aimPoint;      // +0x40C: the object's position at the last Update
    float m_reach;          // +0x41C
    bool m_freeFacing;         // +0x420
};
static_assert(offsetof(CFollowTrail, m_num) == 0x400 && offsetof(CFollowTrail, m_aimPoint) == 0x40C &&
                  offsetof(CFollowTrail, m_reach) == 0x41C && offsetof(CFollowTrail, m_freeFacing) == 0x420,
              "layout must match the original");

extern void* g_emitterTable;
} // namespace Obj

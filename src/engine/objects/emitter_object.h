#pragma once

// Sound emitter nodes (0x004680E0-0x004685C5): level nodes (class EmitterObject) that sounds can be played from -
// a box (the level sector of the same name), or a sphere of Radius about the node - kept in a hash table by name
// (0x006F2260). A sound played "from" one sounds from the shape's nearest point to the listener.
//
// And the follow trail (0x004685D0-0x00468AFE): a ring of the last 64 positions an object has passed through (one
// every 40 units), and the point and facing a given distance back along it.

#include <cstddef>
#include <cstdint>
#include <array>

#include "base/maths/shape_tests.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "base/core/checksums.h"

namespace lists {
class KeyMap;
}

namespace objects {
constexpr std::size_t kUnknown14Bytes = 0x10;
constexpr std::size_t kPathRingSize = 64;

enum class EmitterShape : uint8_t {
    kBox = 0, // the sector's box (Shape 0xE460ABDE)
    kOther = 1, // (Shape 0x03F1E5A7: never inside, no nearest point)
    kSphere = 2, // (the default, and Shape 0xAA069978)
};

struct EmitterNode {

    uint32_t label_{};       // +0x00
    maths::Vec4 at_point_{}; // +0x04
    std::array<uint8_t, kUnknown14Bytes> unknown14_{};
    bool active_{};    // +0x24
    EmitterShape form_{}; // +0x28 (the original's int: the rest is zero)
    std::array<uint8_t, 3> unknown29_{};
    float radius_{};   // +0x2C
    float reach_sq_{}; // +0x30
    void* sector_{};   // +0x34
    maths::BBox box_; // +0x38
    uint32_t unknown58_{};
    uint32_t own_script_{}; // +0x5C
};

// 0x00468120: whether `point` is in the shape.
bool is_within(EmitterNode& node, const maths::Vec4* point);

// 0x004681A0: `point` itself when it's inside, else the shape's nearest point (the box's surface, the
// sphere's). The other shape leaves `out` as it was (the original copies its uninitialised locals).
maths::Vec4* nearest_point_on(EmitterNode& node, maths::Vec4* out, const maths::Vec4* point);

// 0x004680E0: the emitter named `name` (null if none).
EmitterNode* emitter_get_node(uint32_t name);
// 0x004682B0: every emitter freed and taken out of the table.
void emitter_cleanup();
// 0x00468340: the emitter named `name` made active or not.
void emitter_set_active(checksums::Name emitter, int active);
// 0x004683B0: the table made (16 buckets), or emptied if there is one.
void emitter_init();
// 0x00468450: an emitter for the level node `name`, from its parameters.
void emitter_add_node(uint32_t name, bool active);

class FollowTrail {
public:
    // 0x00468AC0 / 0x004685D0: empty, following nothing.
    FollowTrail();
    void get_reset();
    // 0x00468610: the followed object's position added once it's over 40 units (1600 squared) from the last.
    void tick_frame();
    // 0x00468730: the point reach_ back along the trail (from the object's position through the trail,
    // newest first) and, in `matrix`, a facing along it: the old facing's front x10 plus the direction, made
    // level (y = 0) unless free_facing_, with up (0, 1, 0) - nothing when the trail is empty or that point's
    // segment has no length.
    void fetch_place(maths::Vec4* position, maths::Mat4* matrix);

    // The fields other code reads and sets.
    void set_entity_key(uint32_t value) { entity_key_ = value; }
    void set_reach(float value) { reach_ = value; }
    void set_free_facing(bool value) { free_facing_ = value; }

private:
    std::array<maths::Vec4, kPathRingSize> path_vertices_{}; // +0x000: a ring, newest at head_
    int num_{};                       // +0x400
    int head_{};                      // +0x404
    uint32_t entity_key_{};           // +0x408
    maths::Vec4 aim_point_{};         // +0x40C: the object's position at the last Update
    float reach_{};                   // +0x41C
    bool free_facing_{};              // +0x420
};

lists::KeyMap* emitter_table();
} // namespace objects

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace render {
class PcPrimitive;
} // namespace render

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// One link (point) of a chain (0x50 bytes): a Verlet point - where it is, where it was, where the step takes it.
struct ChainLink {
    // 0x00428280: no line.
    ChainLink();
    // 0x00428250: its line deleted.
    void destroy_body();

    maths::Vec4 at_point_; // +0x00
    maths::Vec4 last_pos_; // +0x10
    maths::Vec4 unused20_; // +0x20
    maths::Vec4 next_pos_; // +0x30 this frame's step
    float value40_;        // +0x40 (3 when made; 255 while in the collision range; 0 after a constraint pass)
    float byte_count_;     // +0x44 the rest length to the link before it
    uint32_t unused48_;    // +0x48
    memory::owner<render::PcPrimitive*> line_ptr_; // +0x4C the line drawn to the next link
};

// A chain hung from a bone of its object to a bone of another object (0x684 bytes).
struct Chain {
    static constexpr int kMaxLinks = 20;

    // 0x00428290: 20 links, everything 0 (start_offset (0, 0, 0, 1)), +0x670 on.
    Chain();
    // 0x00428300: the lines between the links shown at the links' positions, opaque black.
    void update_lines();

    uint32_t ident_;                  // +0x000 chainid
    uint32_t bone_start_;             // +0x004 bone_start (its object's skeleton)
    maths::Vec4 start_offset_;        // +0x008 start_offset (in the bone's space)
    uint32_t bone_end_;               // +0x018 bone_end
    uint32_t obj_end_;                // +0x01C obj_end (the object whose bone_end the chain ends at)
    int links_;                       // +0x020 segments + 1
    ChainLink chain_link_[kMaxLinks]; // +0x024
    int iterations_;                  // +0x664 (0x41E56E7B): constraint passes a second
    float damping_;                   // +0x668 damp_force
    float sag_force_;                 // +0x66C gravity_force
    bool move_with_object_;           // +0x670 (0xC96BA2C1): the links carried by the object's velocity
    float plane_offset_;              // +0x674 (0x5F8314B3; Chain member dist=): the collision plane, along the axis
    int collide_first_;               // +0x678 (0x07EBDE63): the links kept to the plane, from
    int collide_end_;                 // +0x67C (0xB721AB4A): ... to (not including)
    bool collide_below_;              // +0x680 (0x4BFBDC28): kept on the plane's other side
};

// The chain component (type 0x6DCFEC46, vtable 0x0064630C, 0x48 bytes; made by 0x004298B0; our name): chains of
// links (ropes) hanging from the object's bones, simulated (Verlet, damping, gravity, fixed-rate constraint passes)
// and drawn as lines. Its parameters' names (chains, chainid, segments, length, bone_start, start_offset, bone_end,
// obj_end, gravity_force, damp_force, collision_info) are from their checksums.
//
// Vtable: 0 Destroy, 1 Update (0x004287A0), 2 configure_from_data (0x004283B0), 3 Finalize, 4 / 9 the empty 0x005480A0,
// 5 0x004E3F90, 6 0x0048DCD0, 7 the base's Suspend, 8 / 10 the empty 0x005BAB90, 11 the base's 0x0045D720, 12
// invoke_method_by_name.
class ChainComponent final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x6DCFEC46;

    // The list of chains (std::list<Chain*>: a head node of 0xC bytes, and the count).
    struct Elem {
        Chain* chain;
        Elem* next;
        Elem* prev;
    };
    struct ChainList {
        // 0x004296B0: the list's destructor (its nodes erased, the head freed).
        void teardown();

        Elem* head;
        int size;
    };

    // 0x004298B0 (registered with the component types): a new one (memory::alloc_zeroed, the constructor).
    static memory::owner<ObjectModule*> fabricate();
    // 0x004296E0: its type, an empty list, nothing found yet,
    // the last frame's length (the slow motion frame) taken now.
    ChainComponent();
    // 0x00429790: the destructor body: each chain's links destroyed and the chain freed, the list emptied and its
    // head freed, then the base's.
    ~ChainComponent() override;
    // 0x00428370 (slot 3): the object's suspend, skeleton and model components kept.
    void seal() override;
    // 0x004283B0 (slot 2): collision_info (the three bones of the collision plane: its point, and the two its axis
    // runs between), then a chain made for each structure of `chains` (each a new line between each link and the
    // next), appended to the list.
    void configure_from_data(script::Struct* params) override;
    // 0x004287A0 (slot 1): each chain stepped (see chain_component.cpp).
    void tick_frame() override;
    // 0x004294C0 (slot 12): 0x286BA49C (the chain chainid's dist=, 0xE4D4B316 / 0x98382F87 its collision range),
    // 0x317A113A (a link of the chain chainid put at the vector 0xEC13B28C: 0xD037C420 its index, else the first with
    // 0x3EA1D93A, else - without 0x5F78A252 - the last), 0x982C70E5 (hidden or not: 0x5B6634D4) - 1; anything else 2.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    ChainList chains_;            // +0x18
    ObjectModule* suspend_;       // +0x20
    ObjectModule* bone_rig_;      // +0x24
    ObjectModule* mesh_instance_; // +0x28
    float final_frame_;           // +0x2C the last frame's length
    float step_time_;             // +0x30 the second's part done
    int steps_;                   // +0x34 the constraint passes done this second
    uint32_t axis_from_bone_;     // +0x38 collision_info's 0x0078395B: the plane's axis runs from it ...
    uint32_t axis_to_bone_;       // +0x3C collision_info's 0xD962E451: ... to it
    uint32_t plane_bone_;         // +0x40 collision_info's 0x94291927: the plane's point
    bool invisible_;              // +0x44
    uint8_t pad45[3];
};

} // namespace objects

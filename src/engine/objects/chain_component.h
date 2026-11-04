#pragma once

#include <cstddef>
#include <cstdint>

#include "base/maths/vec4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>
#include "engine/lists/pointer_ring.h"

namespace render {
class VertexRun;
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

    // The fields other code reads and sets.
    maths::Vec4& at_point() { return at_point_; }
    [[nodiscard]] const maths::Vec4& at_point() const { return at_point_; }
    maths::Vec4& last_pos() { return last_pos_; }
    [[nodiscard]] const maths::Vec4& last_pos() const { return last_pos_; }
    maths::Vec4& next_pos() { return next_pos_; }
    [[nodiscard]] const maths::Vec4& next_pos() const { return next_pos_; }
    void set_value40(float value) { value40_ = value; }
    [[nodiscard]] float byte_count() const { return byte_count_; }
    void set_byte_count(float value) { byte_count_ = value; }
    [[nodiscard]] render::VertexRun* line_ptr() const { return line_ptr_; }
    void set_line_ptr(render::VertexRun* value) { line_ptr_ = value; }

private:
    maths::Vec4 at_point_{}; // +0x00
    maths::Vec4 last_pos_{}; // +0x10
    maths::Vec4 unused20_{}; // +0x20
    maths::Vec4 next_pos_{}; // +0x30 this frame's step
    float value40_{};        // +0x40 (3 when made; 255 while in the collision range; 0 after a constraint pass)
    float byte_count_{};     // +0x44 the rest length to the link before it
    uint32_t unused48_{};    // +0x48
    render::VertexRun* line_ptr_{nullptr}; // +0x4C the line drawn to the next link (the link's: destroy_body)
};

// A chain hung from a bone of its object to a bone of another object (0x684 bytes).
struct Chain {
    static constexpr int kMaxLinks = 20;

    // 0x00428290: 20 links, everything 0 (start_offset (0, 0, 0, 1)), +0x670 on.
    Chain();
    // 0x00428300: the lines between the links shown at the links' positions, opaque black.
    void update_lines();

    // The fields other code reads and sets.
    [[nodiscard]] uint32_t ident() const { return ident_; }
    void set_ident(uint32_t value) { ident_ = value; }
    uint32_t& bone_start() { return bone_start_; }
    [[nodiscard]] uint32_t bone_start() const { return bone_start_; }
    maths::Vec4& start_offset() { return start_offset_; }
    [[nodiscard]] const maths::Vec4& start_offset() const { return start_offset_; }
    uint32_t& bone_end() { return bone_end_; }
    [[nodiscard]] uint32_t bone_end() const { return bone_end_; }
    uint32_t& obj_end() { return obj_end_; }
    [[nodiscard]] uint32_t obj_end() const { return obj_end_; }
    [[nodiscard]] int links() const { return links_; }
    void set_links(int value) { links_ = value; }
    std::array<ChainLink, kMaxLinks>& chain_link() { return chain_link_; }
    [[nodiscard]] const std::array<ChainLink, kMaxLinks>& chain_link() const { return chain_link_; }
    [[nodiscard]] int iterations() const { return iterations_; }
    void set_iterations(int value) { iterations_ = value; }
    float& damping() { return damping_; }
    [[nodiscard]] float damping() const { return damping_; }
    float& sag_force() { return sag_force_; }
    [[nodiscard]] float sag_force() const { return sag_force_; }
    [[nodiscard]] bool move_with_object() const { return move_with_object_; }
    void set_move_with_object(bool value) { move_with_object_ = value; }
    float& plane_offset() { return plane_offset_; }
    [[nodiscard]] float plane_offset() const { return plane_offset_; }
    [[nodiscard]] int collide_first() const { return collide_first_; }
    void set_collide_first(int value) { collide_first_ = value; }
    [[nodiscard]] int collide_end() const { return collide_end_; }
    void set_collide_end(int value) { collide_end_ = value; }
    [[nodiscard]] bool collide_below() const { return collide_below_; }
    void set_collide_below(bool value) { collide_below_ = value; }

private:
    uint32_t ident_{0};               // +0x000 chainid
    uint32_t bone_start_{0};          // +0x004 bone_start (its object's skeleton)
    maths::Vec4 start_offset_{};      // +0x008 start_offset (in the bone's space)
    uint32_t bone_end_{0};            // +0x018 bone_end
    uint32_t obj_end_{0};             // +0x01C obj_end (the object whose bone_end the chain ends at)
    int links_{0};                    // +0x020 segments + 1
    std::array<ChainLink, kMaxLinks> chain_link_; // +0x024
    int iterations_{0};               // +0x664 (0x41E56E7B): constraint passes a second
    float damping_{0.0F};             // +0x668 damp_force
    float sag_force_{0.0F};           // +0x66C gravity_force
    bool move_with_object_{true};     // +0x670 (0xC96BA2C1): the links carried by the object's velocity
    float plane_offset_{0.0F};        // +0x674 (0x5F8314B3; Chain member dist=): the collision plane, along the axis
    int collide_first_{0};            // +0x678 (0x07EBDE63): the links kept to the plane, from
    int collide_end_{0};              // +0x67C (0xB721AB4A): ... to (not including)
    bool collide_below_{false};       // +0x680 (0x4BFBDC28): kept on the plane's other side
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
    ChainComponent(const ChainComponent&) = delete;
    ChainComponent& operator=(const ChainComponent&) = delete;
    static constexpr uint32_t kType = 0x6DCFEC46;

    // The list of chains (std::list<Chain*>: a head node of 0xC bytes, and the count).
    using Elem = lists::PointerRing<Chain>::Node;
    struct ChainList : lists::PointerRing<Chain> {
        // 0x004296B0: the list's destructor (its nodes erased, the head freed).
        void teardown();
    };

    // 0x004298B0 (registered with the component types): a new one (memory::alloc_zeroed, the constructor).
    static gsl::owner<ObjectModule*> fabricate();
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

private:
    ChainList chains_{};          // +0x18
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
    std::array<uint8_t, 3> pad45{};
};

} // namespace objects

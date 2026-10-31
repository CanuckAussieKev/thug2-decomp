#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/maths/shape_tests.h"

namespace render {

extern void** const kCollObjVtable; // 0x00645A30 (coll_obj.cpp)
extern void** const kNodeVtable;    // 0x00645AA0: lists::Elem<HitMesh>

// The geometry a collision object's faces come from (render::TriMeshSource; its methods are rewritten later).
struct TriMeshSource;

// The base of the collision objects (0x10 bytes, vtable 0x00645A30). The methods are virtual in the game - each
// kind has its own vtable - so those here that run another slot call it through the vtable. The slots seen so far:
//   0 the scalar deleting destructor, 1 is_triangle_hit (base: false), 2 get_triangle_attrs(face),
//   3 get_face_terrain(face), 4 assign_shape_data, 5 place_at_absolute_point, 6 change_facing, 7 get_absolute_point,
//   8 fetch_facing, 9 read_vertex_point(face vertex), 10 / 11 a value at +0x38 / +0xA0 get and set, 12 Clone,
//   13 inside_bounds (true if it may touch the box), 14 collide_with_line, 19 a call passed down, 20 get_local_bounds,
//   21 the geometry a cache keeps for it.
class HitMesh {
public:
    enum Flags : uint16_t {
        kOwnsGeometry = 0x8000, // the geometry is deleted with the object (unless kSharedGeometry)
        kSharedGeometry = 0x4000,
    };

    // 0x0040B370 (slot 4): the geometry, and its checksum (the sector's name) as the object's.
    void assign_shape_data(TriMeshSource* geometry);
    // 0x0040FEC0: the checksum, the geometry's too (if it has one).
    void set_checksum(uint32_t checksum);
    // 0x00413390: the scalar deleting destructor (the base's body inlined: the geometry deleted if it's owned and
    // not shared).
    HitMesh* scalar_delete(uint8_t flags);
    // The base's destructor body (inlined in every destructor).
    void destroy_base();

    template <typename R, typename... A>
    R get_call(int slot, A... args)
    {
        return reinterpret_cast<R(__thiscall*)(HitMesh*, A...)>(vtable_[slot])(this, args...);
    }

    void** vtable_;
    uint32_t checksum_;   // +0x04
    uint16_t flag_bits_;  // +0x08
    uint8_t visit_stamp_; // +0x0A the grid queries' mark (0x00693998): already listed this query
    uint8_t pad0b_;
    TriMeshSource* geometry_; // +0x0C
};
static_assert(sizeof(HitMesh) == 0x10, "layout must match the original");

// The node vtable of the lists of collision objects (lists::Elem<HitMesh>, 0x00645AA0) and their heads'
// (lists::Foremost<HitMesh>, 0x00645D34).
struct CollObjListNodes {
    // 0x004131D0: unlinked (lists::Elem<HitMesh>'s body). 0x00411880: its scalar deleting destructor.
    void destroy_node_body();
    memory::ListNode* destroy_node(uint8_t flags);
    // 0x00413180: an empty head (returns it). 0x004131A0: its scalar deleting destructor (the node's body).
    memory::ListNode* construct_head();
    memory::ListNode* destroy_head(uint8_t flags);
};

// A collision object made of others (0x3C bytes, vtable 0x00645AA8): its children in a list; most calls are passed
// to each; its box is theirs together (recomputed when a child moves).
class HitMeshGroup : public HitMesh {
public:
    // 0x004133E0: no children (returns this).
    HitMeshGroup* make_new();
    // 0x00411D10: a copy (sharing the children's list; returns this).
    HitMeshGroup* construct_copy(const HitMeshGroup* other);
    // 0x00411B10: the children deleted (each child and its node), the list, out of the caches, the base.
    void destruct();
    // 0x00413460
    HitMeshGroup* scalar_delete(uint8_t flags);
    // 0x00413490 (slot 12): a copy owning its geometry flag, with a clone of each child.
    HitMeshGroup* replicate(int argument);
    // 0x00411BE0: `child` onto the end of the list.
    void attach_child(HitMesh* child);

    // 0x00411D90 (slot 1): whether every child is (stops asking at the first that isn't).
    bool is_triangle_hit();
    // 0x00411CC0 / 0x00411C30 (slots 4, 5): passed to each child; the box to be recomputed. 0x00411C80 (slot 6):
    // passed to each child.
    void set_geometry_all(void* value);
    void place_at_absolute_point(const void* position);
    void change_facing(const void* orientation);
    // 0x00411F60 / 0x00411F80 (slots 7, 8): the first child's (crashes with none, as in the original).
    void* get_absolute_point();
    void* fetch_facing();
    // 0x00410960 (slot 9): a zero vector.
    maths::Vec4 read_vertex_point(int vertex) const;
    // 0x0040AE20 / 0x0040AE10 (slots 10, 11)
    int get_value38() const;
    void set_value38(int value);
    // 0x00411E80 (slot 13): false only if the box is known and doesn't touch `box`.
    int inside_bounds(const maths::BBox* box);
    // 0x00411EC0 (slot 14): each child asked; whether any hit.
    bool collide_with_line(void* a, void* b, void* c, void* d);
    // 0x00411F20 (slot 19): passed to each child.
    void pass_down(void* a, void* b);
    // 0x00411E50 (slot 20): the box (recomputed if a child moved; null if a child has none).
    maths::BBox* get_local_bounds();
    // 0x00411DD0: the box made from the children's.
    void recompute_b_box();

    memory::ListNode* sub_nodes_; // +0x10 the head (lists::Foremost<HitMesh>)
    maths::BBox box_;             // +0x14
    bool dirty_;                  // +0x34
    bool box_valid_;              // +0x35
    uint8_t pad36[2];
    int value38_; // +0x38
};
static_assert(sizeof(HitMeshGroup) == 0x3C && offsetof(HitMeshGroup, box_) == 0x14, "layout must match the original");

// 0x0040AE30 (HitMeshGroup's slot 3, get_face_terrain): 0.
struct CollObjStubs {
    uint16_t no_terrain(int face);
};

} // namespace render

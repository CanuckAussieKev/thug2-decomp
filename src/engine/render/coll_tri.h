#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/maths/shape_tests.h"
#include "engine/maths/vec4.h"

namespace render {

// A node of a collision geometry's BSP tree (8 bytes; an array of them, made with new[] - a count before the
// first). +0: the low 2 bits the split axis (0 x, 1 y, 2 z) or 3 for a leaf; the rest (>> 2, signed) the split
// position in sixteenths. A leaf has its face count at +2 and a pointer to its face indices (u16) at +4. A
// split node has its two children at +4 (the low 2 bits masked off): the one at +0 is the "less" side (below
// the split) unless bit 0 is set, which swaps them.
struct HitTreeNode {
    int32_t axis_tag_;
    uint32_t chain_link_;

    // 0x00414D70 (not a function in Ghidra: new[]'s constructor): zeroed.
    HitTreeNode* make_new();

    bool is_end_node() const { return (axis_tag_ & 3) == 3; }
    int get_axis() const { return axis_tag_ & 3; }
    int get_split() const { return axis_tag_ >> 2; }
    void set_split(int value) { axis_tag_ = (value << 2) | (axis_tag_ & 3); }
    uint16_t leaf_face_count() const { return reinterpret_cast<const uint16_t*>(this)[1]; }
    uint16_t* get_leaf_faces() const { return reinterpret_cast<uint16_t*>(chain_link_); }
    HitTreeNode* get_children() const { return reinterpret_cast<HitTreeNode*>(chain_link_ & ~3u); }
    HitTreeNode* less_child() const { return get_children() + ((chain_link_ & 1) ? 1 : 0); }
    HitTreeNode* greater_child() const { return get_children() + ((chain_link_ & 1) ? 0 : 1); }

    // 0x00415C90 / 0x00415CE0: the nodes in this subtree / the faces in its leaves.
    int tally_nodes() const;
    int count_faces() const;
    // 0x00415D30: the lowest of the leaves' face pointers (where their shared array starts).
    uint16_t* first_face_list() const;
    // 0x00415D80: the splits moved by `delta` (each rounded down to a sixteenth, toward zero).
    void get_slide(const maths::Vec4* delta);
    // 0x00415E00: turned by `quarter_turns` (1-3) quarter turns about Y around `centre` (moved there and back
    // when `move_to_centre`): x and z splits swap axis, some negated (their sides swapped).
    void rotate_y(const maths::Vec4* centre, int quarter_turns, bool move_to_centre);
    // 0x00415F30: scaled by `scale` around `centre` (moved there and back when `move_to_centre`).
    void scale_tool(const maths::Vec4* centre, const maths::Vec4* scale, bool move_to_centre);
    // 0x00416000 (cdecl): the tree read from a file fixed up: child offsets + `node_base`, leaf face offsets
    // (in u16s) into pointers from `face_base`. Returns true.
    static bool relocate(HitTreeNode* node, uint32_t node_base, uint32_t face_base);
    // 0x00416A10: a copy of the tree and its face indices (the argument unused); null if it has no nodes.
    HitTreeNode* replicate(int unused) const;
};
static_assert(sizeof(HitTreeNode) == 8, "layout must match the original");

// A collision object's geometry (render::TriMeshSource, from a .col file): its vertices (floats, or 16-bit offsets
// from its box's minimum in sixteenths when "compressed"), faces and BSP tree.
struct TriMeshSource {
    uint32_t checksum_; // +0x00 the sector's name
    uint16_t unknown04_;
    uint16_t num_vertices_;   // +0x06
    uint16_t tri_count_;      // +0x08 (also the size of a sector object's own buffer)
    bool small_faces_;        // +0x0A faces of 8 bytes (else 10)
    bool compressed_;         // +0x0B the vertices are 16-bit
    uint8_t* faces_;          // +0x0C each: u16 flags, u16 terrain, the three vertex indices (u8 / u16)
    maths::Vec4 lower_limit_; // +0x10 the box (+0x20 its max)
    maths::Vec4 upper_limit_;
    void* vertices_;        // +0x30 float[3] each, or uint16_t[3] when compressed
    HitTreeNode* bsp_;      // +0x34
    uint8_t* vertex_bytes_; // +0x38 one byte per vertex (copied and freed with the geometry), or null
    uint32_t unknown3c_;    // +0x3C a float: the face-rejection scale (cleared when turned or scaled; a movable
                            // triangle object keeps a copy)

    // 0x0040B230: vertex `index` (w = 0).
    maths::Vec4 read_stored_vertex(int index) const;
    // 0x0040B2D0: the same into `out`.
    void read_stored_vertex(int index, maths::Vec4* out) const;
    // 0x00414EE0: vertex `index` set (compressed: in sixteenths from the box's minimum, toward zero).
    void write_stored_vertex(int index, const maths::Vec4* position);
    // 0x00414F90 (0x00415C00 jumps to it): every vertex into `out` (w = 0).
    void get_stored_vertex_array(maths::Vec4* out) const;
    // 0x00415C10: every vertex set from `positions`, the box made from them first.
    void choose_raw_verts(const maths::Vec4* positions);
    // 0x00414D80: face `face`'s normal ((v1 - v0) x (v2 - v0), normalised unless zero; w = 0).
    maths::Vec4 get_triangle_normal(int face) const;
    // 0x00415B70: every field copied (the arrays shared). Returns this.
    TriMeshSource* copy_from(const TriMeshSource* other);
    // 0x00416BF0: a copy with its own vertices, faces, vertex bytes and tree (`bsp_argument` passed to the tree's
    // Clone). Null when `need_vertices` and there are none, or an array couldn't be allocated.
    TriMeshSource* replicate(int bsp_argument, bool need_vertices) const;
    // 0x00416970: the arrays freed, the tree deleted.
    void destruct();
    // 0x00416080: the tree and its face indices freed (false if there was none).
    bool delete_bsp();
    // 0x004169C0: read from a file: the arrays' offsets turned into pointers (the first argument unused).
    bool relocate(uint32_t unused, uint32_t vertex_base, uint32_t vertex_byte_base, uint32_t face_base,
                  uint32_t node_base, uint32_t face_list_base);
    // 0x00416400: vertices (uncompressed), box and tree moved by `delta`.
    void get_slide(const maths::Vec4* delta);
    // 0x004164F0: turned by `quarter_turns` quarter turns about Y around `centre` (compressed vertices through a
    // temporary array on the top-down heap); the box made from its turned corners.
    void rotate_y(const maths::Vec4* centre, int quarter_turns);
    // 0x00416730: scaled by `scale` around `centre`; the box's corners scaled (w kept, not re-sorted).
    void scale_tool(const maths::Vec4* centre, const maths::Vec4* scale);
    // 0x00414D30: the shared index table 0x0067C6B8 filled with 0..2047 the first time (its first entry is
    // 0xFFFF until then); whether the geometry has a tree.
    bool set_up_all_faces();
    // 0x00414FE0: occlusion polygons: each face paired with the first later face that shares an edge with it and
    // faces the same way (normals within 0.05 on each axis) - the four corners to render::add_debug_shape (a quad: an
    // occlusion polygon) with the checksum.
    void add_occlusion_polys();
    // 0x00416320: the faces in leaves `box` reaches, gathered into `buffer` (`count` set). Without a tree, or
    // more faces than the geometry has, every face: the shared index table 0x0067C6B8.
    uint16_t* get_faces_in_box(const maths::BBox* box, int* count, uint16_t* buffer, int buffer_size);
    // 0x004160E0: get_faces_in_box's walk.
    void gather_faces(const HitTreeNode* node, const maths::BBox* box);
    // 0x004163B0: whether `point` is behind every face of the leaf it falls in, counted against all faces (the
    // second argument unused).
    bool is_point_behind_faces(const maths::Vec4* point, int unused);
    // 0x004161D0: its walk (always true).
    bool count_faces_behind(const HitTreeNode* node, const maths::Vec4* point, int unused);

    // +0x3C as the line tests read it: a float.
    float rejection_scale() const
    {
        float scale;
        std::memcpy(&scale, &unknown3c_, 4);
        return scale;
    }

    uint32_t face_vertex(int face, int corner) const
    {
        return small_faces_ ? faces_[face * 8 + 4 + corner]
                            : reinterpret_cast<const uint16_t*>(faces_ + face * 10 + 4)[corner];
    }
};
static_assert(offsetof(TriMeshSource, compressed_) == 0x0B, "layout must match the original");
static_assert(offsetof(TriMeshSource, lower_limit_) == 0x10, "layout must match the original");
static_assert(offsetof(TriMeshSource, vertices_) == 0x30, "layout must match the original");
static_assert(sizeof(TriMeshSource) == 0x40, "layout must match the original");

} // namespace render

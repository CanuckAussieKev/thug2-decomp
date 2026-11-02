#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "engine/core/checked_index.h"

#include "engine/maths/shape_tests.h"
#include "engine/maths/vec4.h"
#include "formats/collision_format.h"

namespace render {

// A node of a collision geometry's BSP tree (8 bytes; an array of them, made with new[] - a count before the
// first). +0: the low 2 bits the split axis (0 x, 1 y, 2 z) or 3 for a leaf; the rest (>> 2, signed) the split
// position in sixteenths. A leaf has its face count at +2 and a pointer to its face indices (u16) at +4. A
// split node has its two children at +4 (the low 2 bits masked off): the one at +0 is the "less" side (below
// the split) unless bit 0 is set, which swaps them.
struct HitTreeNode {
    // The fields other code reads and sets.
    [[nodiscard]] int32_t axis_tag() const { return axis_tag_; }
    void set_axis_tag(int32_t value) { axis_tag_ = value; }
    [[nodiscard]] uint32_t chain_link() const { return chain_link_; }
    void set_chain_link(uint32_t value) { chain_link_ = value; }

private:
    int32_t axis_tag_{0};
    uint32_t chain_link_{0};

public:

    // 0x00414D70 (not a function in Ghidra: new[]'s constructor): zeroed.
    HitTreeNode();

    [[nodiscard]] bool is_end_node() const { return (axis_tag_ & 3) == 3; }
    [[nodiscard]] int get_axis() const { return axis_tag_ & 3; }
    [[nodiscard]] int get_split() const { return axis_tag_ >> 2; }
    void set_split(int value) { axis_tag_ = (value << 2) | (axis_tag_ & 3); }
    [[nodiscard]] uint16_t leaf_face_count() const { return static_cast<uint16_t>(static_cast<uint32_t>(axis_tag_) >> 16); }
    [[nodiscard]] uint16_t* get_leaf_faces() const { return collision_format::tree_leaf_faces(chain_link_); }
    // The leaf's face indices as a run of its face count.
    [[nodiscard]] std::span<uint16_t> leaf_face_run() const { return {get_leaf_faces(), leaf_face_count()}; }
    [[nodiscard]] HitTreeNode* get_children() const { return collision_format::tree_children<HitTreeNode>(chain_link_); }
    // The two children of a split node.
    [[nodiscard]] std::span<HitTreeNode, 2> child_pair() const { return std::span<HitTreeNode, 2>(get_children(), 2); }
    // A leaf of `count` faces listed at `faces`.
    void make_leaf(uint16_t count, uint16_t* faces)
    {
        axis_tag_ = 3 | static_cast<int32_t>(static_cast<uint32_t>(count) << 16);
        chain_link_ = collision_format::tree_link_to(faces);
    }
    [[nodiscard]] HitTreeNode* less_child() const { return &core::at(child_pair(), (chain_link_ & 1) ? 1 : 0); }
    [[nodiscard]] HitTreeNode* greater_child() const { return &core::at(child_pair(), (chain_link_ & 1) ? 0 : 1); }

    // 0x00415C90 / 0x00415CE0: the nodes in this subtree / the faces in its leaves.
    [[nodiscard]] int tally_nodes() const;
    [[nodiscard]] int count_faces() const;
    // 0x00415D30: the lowest of the leaves' face pointers (where their shared array starts).
    [[nodiscard]] uint16_t* first_face_list() const;
    // 0x00415D80: the splits moved by `delta` (each rounded down to a sixteenth, toward zero).
    void get_slide(const maths::Vec4* delta);
    // 0x00415E00: turned by `quarter_turns` (1-3) quarter turns about Y around `centre` (moved there and back
    // when `move_to_centre`): x and z splits swap axis, some negated (their sides swapped).
    void rotate_y(const maths::Vec4* centre, int quarter_turns, bool move_to_centre);
    // 0x00415F30: scaled by `scale` around `centre` (moved there and back when `move_to_centre`).
    void scale_tool(const maths::Vec4* centre, const maths::Vec4* scale, bool move_to_centre);
    // 0x00416A10: a copy of the tree and its face indices (the argument unused); null if it has no nodes.
    [[nodiscard]] HitTreeNode* replicate(int unused) const;
};
static_assert(sizeof(HitTreeNode) == 8, "the collision file's BSP node (collision_format.h), used in place");

// A tree's nodes as new[] made them (0x00414D70 each), the block before the first holding their count - and the
// block freed from there (the original's new[] kept the count in the word before them).
HitTreeNode* new_tree_nodes(int count);
void free_tree_nodes(HitTreeNode* first);

// A collision object's geometry (render::TriMeshSource, from a .col file): its vertices (floats, or 16-bit offsets
// from its box's minimum in sixteenths when "compressed"), faces and BSP tree.
struct TriMeshSource {
    // The fields other code reads and sets.
    [[nodiscard]] uint32_t checksum() const { return checksum_; }
    void set_checksum(uint32_t value) { checksum_ = value; }
    void set_unknown04(uint16_t value) { unknown04_ = value; }
    [[nodiscard]] uint16_t num_vertices() const { return num_vertices_; }
    void set_num_vertices(uint16_t value) { num_vertices_ = value; }
    uint16_t& tri_count() { return tri_count_; }
    [[nodiscard]] uint16_t tri_count() const { return tri_count_; }
    void set_tri_count(uint16_t value) { tri_count_ = value; }
    [[nodiscard]] bool small_faces() const { return small_faces_; }
    void set_small_faces(bool value) { small_faces_ = value; }
    [[nodiscard]] bool compressed() const { return compressed_; }
    void set_compressed(bool value) { compressed_ = value; }
    [[nodiscard]] uint8_t* faces() const { return faces_; }
    void set_faces(uint8_t* value) { faces_ = value; }
    maths::BBox& bounds() { return bounds_; }
    [[nodiscard]] const maths::BBox& bounds() const { return bounds_; }
    [[nodiscard]] void* vertices() const { return vertices_; }
    void set_vertices(void* value) { vertices_ = value; }
    [[nodiscard]] HitTreeNode* bsp() const { return bsp_; }
    void set_bsp(HitTreeNode* value) { bsp_ = value; }
    [[nodiscard]] uint8_t* vertex_bytes() const { return vertex_bytes_; }
    void set_vertex_bytes(uint8_t* value) { vertex_bytes_ = value; }
    [[nodiscard]] uint32_t unknown3c() const { return unknown3c_; }
    void set_unknown3c(uint32_t value) { unknown3c_ = value; }

private:
    uint32_t checksum_{}; // +0x00 the sector's name
    uint16_t unknown04_{};
    uint16_t num_vertices_{};   // +0x06
    uint16_t tri_count_{};      // +0x08 (also the size of a sector object's own buffer)
    bool small_faces_{};        // +0x0A faces of 8 bytes (else 10)
    bool compressed_{};         // +0x0B the vertices are 16-bit
    uint8_t* faces_{};          // +0x0C each: u16 flags, u16 terrain, the three vertex indices (u8 / u16)
    maths::BBox bounds_;      // +0x10 the box (min, max)
    void* vertices_{};        // +0x30 float[3] each, or uint16_t[3] when compressed
    HitTreeNode* bsp_{};      // +0x34
    uint8_t* vertex_bytes_{}; // +0x38 one byte per vertex (copied and freed with the geometry), or null

public:
    // NOT ORIGINAL - those bytes as a run of num_vertices_.
    [[nodiscard]] std::span<const uint8_t> vertex_byte_run() const { return {vertex_bytes_, num_vertices_}; }
private:
    uint32_t unknown3c_{};    // +0x3C a float: the face-rejection scale (cleared when turned or scaled; a movable

public:
                            // triangle object keeps a copy)

    // NOT ORIGINAL - the stored vertices as runs of floats / 16-bit words (three each, num_vertices_ vertices).
    [[nodiscard]] std::span<float> float_vertices() const
    {
        return {static_cast<float*>(vertices_), static_cast<size_t>(num_vertices_) * 3};
    }
    [[nodiscard]] std::span<uint16_t> packed_vertices() const
    {
        return {static_cast<uint16_t*>(vertices_), static_cast<size_t>(num_vertices_) * 3};
    }

    // 0x0040B230: vertex `index` (w = 0).
    [[nodiscard]] maths::Vec4 read_stored_vertex(int index) const;
    // 0x0040B2D0: the same into `out`.
    void read_stored_vertex(int index, maths::Vec4* out) const;
    // 0x00414EE0: vertex `index` set (compressed: in sixteenths from the box's minimum, toward zero).
    void write_stored_vertex(int index, const maths::Vec4* position);
    // 0x00414F90 (0x00415C00 jumps to it): every vertex into `out` (w = 0).
    void get_stored_vertex_array(std::span<maths::Vec4> out) const;
    // 0x00415C10: every vertex set from `positions`, the box made from them first.
    void choose_raw_verts(std::span<const maths::Vec4> positions);
    // 0x00414D80: face `face`'s normal ((v1 - v0) x (v2 - v0), normalised unless zero; w = 0).
    [[nodiscard]] maths::Vec4 get_triangle_normal(int face) const;
    // 0x00415B70: every field copied (the arrays shared). Returns this.
    TriMeshSource* copy_from(const TriMeshSource* other);
    // 0x00416BF0: a copy with its own vertices, faces, vertex bytes and tree (`bsp_argument` passed to the tree's
    // Clone). Null when `need_vertices` and there are none, or an array couldn't be allocated.
    [[nodiscard]] TriMeshSource* replicate(int bsp_argument, bool need_vertices) const;
    // 0x00416970: the arrays freed, the tree deleted.
    void destruct();
    // 0x00416080: the tree and its face indices freed (false if there was none).
    bool delete_bsp();
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
    std::span<const uint16_t> get_faces_in_box(const maths::BBox* box, std::span<uint16_t> buffer);
    // 0x004160E0: get_faces_in_box's walk.
    void gather_faces(const HitTreeNode* node, const maths::BBox* box);
    // 0x004163B0: whether `point` is behind every face of the leaf it falls in, counted against all faces (the
    // second argument unused).
    bool is_point_behind_faces(const maths::Vec4* point, int unused);
    // 0x004161D0: its walk (always true).
    bool count_faces_behind(const HitTreeNode* node, const maths::Vec4* point, int unused);

    // +0x3C as the line tests read it: a float.
    [[nodiscard]] float rejection_scale() const
    {
        auto scale = std::bit_cast<float>(unknown3c_);
        return scale;
    }

    // The faces (collision_format::FaceTable: flags, terrain, vertex indices).
    [[nodiscard]] collision_format::FaceTable face_table() const
    {
        return {std::span<uint8_t>(faces_, static_cast<size_t>(tri_count_) * (small_faces_ ? 8 : 10)), small_faces_};
    }
    [[nodiscard]] uint32_t face_vertex(int face, int corner) const { return face_table().vertex(face, corner); }
    [[nodiscard]] uint16_t face_flags(int face) const { return face_table().flags(face); }
    [[nodiscard]] uint16_t face_terrain(int face) const { return face_table().terrain(face); }
    void set_face_flags(int face, uint16_t flags) { face_table().set_flags(face, flags); }
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(TriMeshSource, num_vertices_) == 0x06 && offsetof(TriMeshSource, tri_count_) == 0x08 &&
                          offsetof(TriMeshSource, small_faces_) == 0x0A && offsetof(TriMeshSource, compressed_) == 0x0B &&
                          offsetof(TriMeshSource, faces_) == 0x0C && offsetof(TriMeshSource, bounds_) == 0x10 &&
                          offsetof(TriMeshSource, vertices_) == 0x30 && offsetof(TriMeshSource, bsp_) == 0x34 &&
                          offsetof(TriMeshSource, vertex_bytes_) == 0x38 &&
                          sizeof(TriMeshSource) == collision_format::kObjectSize,
                      "the collision file's object record (collision_format.h)");
    }
};
// The collision file's object record, used in place (formats/collision_format.h): its layout is the file's.

} // namespace render

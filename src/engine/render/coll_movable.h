#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/render/coll_obj.h"

namespace render {

extern void** const kScaledVtable; // 0x00645B58: CollScaled (coll_movable.cpp)

struct CollisionHit;
struct CollRect;

// A movable collision object (0xA4 bytes, vtable 0x00645B00; a car, a level object...): its world position and
// orientation, and the orientation's inverse (the transpose).
class HitMovingMesh : public HitMesh {
public:
    // 0x00411FA0: at the origin, unrotated (returns this).
    HitMovingMesh* make_new();
    // 0x00412660: a copy (returns this).
    HitMovingMesh* construct_copy(const HitMovingMesh* other);
    // 0x004121A0: out of the caches' movables, the base.
    void destruct();
    // 0x004135B0
    HitMovingMesh* scalar_delete(uint8_t flags);
    // 0x00412070 (slot 6; 0x00412FF0 jumps to it): the rotation rows (0-2) taken, the inverse made from the
    // whole matrix.
    void change_facing(const maths::Mat4* orientation);
    // 0x00412050 / 0x00412060 (slots 7, 8)
    maths::Vec4* get_absolute_point();
    maths::Mat4* fetch_facing();
    // 0x00412230 (slot 9): the geometry's vertex `index` rotated and placed (w = the position's).
    maths::Vec4 read_vertex_point(int index) const;
    // 0x004122C0: `box` in the object's space (its corners moved back and turned by the inverse).
    void box_to_local(const maths::BBox* box, maths::BBox* out) const;
    // 0x0040AE50 / 0x0040AE40 (slots 10, 11)
    int get_value_a0() const;
    void set_value_a0(int value);

    maths::Vec4 anchor_point_;      // +0x10
    maths::Mat4 orientation_;   // +0x20
    maths::Mat4 inverse_;       // +0x60 the orientation's transpose
    int value_a0_;               // +0xA0
};
static_assert(sizeof(HitMovingMesh) == 0xA4 && offsetof(HitMovingMesh, inverse_) == 0x60, "layout must match");

// 0x004120D0: this = `source` transposed (in place when they're the same: 0x00412150). Returns this.
struct MatrixTranspose : maths::Mat4 {
    maths::Mat4* transpose_from(const maths::Mat4* source);
    void transpose_in_place();
};

// A movable box (0xD0 bytes, vtable 0x00645BC0): one set of face flags and terrain for every face.
class HitMovingBox : public HitMovingMesh {
public:
    // 0x00412550: the default box (-50, -20, -90) .. (30, 36, 90) (returns this).
    HitMovingBox* make_new();
    // 0x00413700: a copy (returns this).
    HitMovingBox* construct_copy(const HitMovingBox* other);
    // 0x004125F0 / 0x004136D0
    void destruct();
    HitMovingBox* scalar_delete(uint8_t flags);
    // 0x00412600 (slot 4): the geometry and its box (a null geometry crashes, as in the original).
    void assign_shape_data(TriMeshSource* geometry);
    // 0x0040AE60 / 0x0040AE70 (slots 2, 3)
    uint32_t get_triangle_attrs(int face) const;
    uint16_t get_face_terrain(int face) const;
    // 0x00414BE0 (slot 12): a copy (no geometry, flag 0x8000; a failed allocation crashes, as in the original).
    HitMovingBox* replicate(int argument);
    // 0x004137B0 (slot 14): the line (world space) against the box, in the box's space; a hit is offered to
    // `hit` (report_hit) with its point and normal back in world space. The direction and the line's box unused.
    bool collide_with_line(const maths::Seg* line, const maths::Vec4* direction, CollisionHit* hit,
                         const maths::BBox* line_box);
    // 0x004126E0 (slot 19): the box drawn where the object is (0x0048D090; the arguments unused).
    void draw_diagnostic(int a, int b);

    maths::BBox box_;            // +0xA4
    uint32_t poly_attr_bits_;       // +0xC4
    uint16_t surface_type_;         // +0xC8
    uint8_t unknownca_[4];     // +0xCA (copied, not otherwise used here)
    uint8_t padce_[2];
};
static_assert(sizeof(HitMovingBox) == 0xD0 && offsetof(HitMovingBox, poly_attr_bits_) == 0xC4, "layout must match");

// A scaled, rotated and placed collision object (0xA0 bytes, vtable 0x00645B58), with the inverse transform.
class CollScaled : public HitMesh {
public:
    // 0x004123C0: unscaled, unrotated, at the origin (returns this).
    CollScaled* make_new();
    // 0x00413F80: a copy (returns this).
    CollScaled* construct_copy(const CollScaled* other);
    // 0x00412460 / 0x004135E0
    void destruct();
    CollScaled* scalar_delete(uint8_t flags);
    // 0x00413610 / 0x00413640 / 0x004136A0 (slots 5, 6, 24): the position / rotation rows / scale, then the
    // inverse made again.
    void place_at_absolute_point(const maths::Vec4* position);
    void change_facing(const maths::Mat4* orientation);
    void change_scale(const maths::Vec4* scale);
    // 0x00412490: the inverse: the rotation's transpose divided by the scale, the position turned back.
    void update_inverse();
    // 0x00410A30: `line` (both ends, w included) through the inverse - into the object's space.
    void line_to_local(const maths::Seg* line, maths::Seg* out) const;
    // 0x00410AA0: `rect` into the object's space: its corner and the far ends of its sides through the inverse,
    // the sides measured from the new corner.
    void rect_to_local(const CollRect* rect, CollRect* out) const;
    // 0x004109C0 / 0x00412060 / 0x00412050 (slots 7, 8, 25)
    maths::Vec4* get_absolute_point();
    // 0x004109D0 (slot 9): vertex `index` scaled (w the scale's w), through the matrix.
    maths::Vec4 read_vertex_point(int index) const;

    maths::Vec4 size_factor_;        // +0x10
    maths::Mat4 basis_frame_;       // +0x20 (row 3, +0x50, the position)
    maths::Mat4 inverse_;      // +0x60 (row 3, +0x90, the position turned back)
};
static_assert(sizeof(CollScaled) == 0xA0 && offsetof(CollScaled, inverse_) == 0x60, "layout must match");

} // namespace Nx

#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "base/maths/shape_tests.h"
#include "base/maths/mat4.h"
#include "engine/render/coll_obj.h"
#include <gsl/pointers>

namespace objects {
class GameEntity;
}
namespace render {

struct CollisionHit;
struct CollRect;

constexpr std::size_t kHitMovingMeshBytes = 0xA4;
constexpr std::size_t kHitMovingMeshInverseOffset = 0x60;

// A movable collision object (0xA4 bytes, the original's vtable 0x00645B00; a car, a level object...): its world
// position and orientation, and the orientation's inverse (the transpose).
class HitMovingMesh : public HitMesh {
public:
    // 0x004121A0 (0x004135B0 the deleting destructor): out of the caches' movables, the base.
    ~HitMovingMesh() override;

    // 0x004171B0 (slot 5; folded with ProbeRay::change_finish): the position.
    void place_at_absolute_point(const maths::Vec4* position) override { anchor_point_ = *position; }
    // 0x00412070 (slot 6; 0x00412FF0 jumps to it): the rotation rows (0-2) taken, the inverse made from the
    // whole matrix.
    void change_facing(const maths::Mat4* orientation) override;
    // 0x00412050 / 0x00412060 (slots 7, 8)
    maths::Vec4* get_absolute_point() override;
    maths::Mat4* fetch_facing() override;
    // 0x00412230 (slot 9): the geometry's vertex `index` rotated and placed (w = the position's).
    [[nodiscard]] maths::Vec4 read_vertex_point(int index) const override;
    // 0x004122C0: `line` in the object's space (its ends moved back and turned by the inverse).
    void segment_to_local(const maths::Seg* line, maths::Seg* out) const;
    // 0x0040AE50 / 0x0040AE40 (slots 10, 11)
    [[nodiscard]] objects::GameEntity* get_owner() const override { return owner_; }
    void set_owner(objects::GameEntity* owner) override { owner_ = owner; }

    // The fields other code reads and sets.
    [[nodiscard]] const maths::Vec4& anchor_point() const { return anchor_point_; }
    void set_anchor_point(const maths::Vec4& value) { anchor_point_ = value; }
    maths::Mat4& orientation() { return orientation_; }
    [[nodiscard]] const maths::Mat4& orientation() const { return orientation_; }

private:
    maths::Vec4 anchor_point_; // +0x10
    maths::Mat4 orientation_{}; // +0x20
    maths::Mat4 inverse_{};     // +0x60 the orientation's transpose
    objects::GameEntity* owner_{};  // +0xA0

protected:
    // 0x00411FA0: at the origin, unrotated.
    HitMovingMesh();
    // 0x00412660: a copy.
    HitMovingMesh(const HitMovingMesh& other);

public:
    HitMovingMesh& operator=(const HitMovingMesh&) = delete;
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(sizeof(HitMovingMesh) == kHitMovingMeshBytes && offsetof(HitMovingMesh, inverse_) == kHitMovingMeshInverseOffset, "layout must match");
    }
};

// 0x004120D0: this = `source` transposed (in place when they're the same: 0x00412150). Returns this.
struct MatrixTranspose : maths::Mat4 {
    maths::Mat4* transpose_from(const maths::Mat4* source);
    void transpose_in_place();
};

constexpr std::size_t kHitMovingBoxBytes = 0xD0;
constexpr std::size_t kHitMovingBoxPolyAttrBitsOffset = 0xC4;

// A movable box (0xD0 bytes, the original's vtable 0x00645BC0): one set of face flags and terrain for every face.
class HitMovingBox final : public HitMovingMesh {
public:
    // 0x00412550: the default box (-50, -20, -90) .. (30, 36, 90).
    HitMovingBox();
    // 0x00413700: a copy.
    HitMovingBox(const HitMovingBox& other);
    HitMovingBox& operator=(const HitMovingBox&) = delete;
    // 0x004125F0 (0x004136D0 the deleting destructor)
    ~HitMovingBox() override = default;

    [[nodiscard]] HitMeshKind kind() const override { return HitMeshKind::kMovingBox; }
    // 0x00412600 (slot 4): the geometry and its box (a null geometry crashes, as in the original).
    void assign_shape_data(TriMeshSource* geometry) override;
    // 0x0040AE60 / 0x0040AE70 (slots 2, 3)
    [[nodiscard]] FaceFlag get_triangle_attrs(int face) const override;
    [[nodiscard]] uint16_t get_face_terrain(int face) const override;
    // 0x00414BE0 (slot 12): a copy (no geometry, flag 0x8000; a failed allocation crashes, as in the original).
    gsl::owner<HitMovingBox*> replicate(int argument) override;
    // (slot 13: the shared "true", folded with SaveCard::always_true)
    bool inside_bounds(const maths::BBox* /*box*/) override { return true; }
    // 0x004137B0 (slot 14): the line (world space) against the box, in the box's space; a hit is offered to
    // `hit` (report_hit) with its point and normal back in world space. The direction and the line's box unused.
    bool collide_with_line(const maths::Seg* line, const maths::Vec4* direction, CollisionHit* hit,
                           const maths::BBox* line_box) override;
    // 0x004126E0 (slot 19): the box drawn where the object is (0x0048D090; the arguments unused).
    void draw_diagnostic(int a, int b) override;

    // The fields other code reads and sets.
    maths::BBox& box() { return box_; }
    [[nodiscard]] const maths::BBox& box() const { return box_; }
    void set_poly_attr_bits(uint32_t value) { poly_attr_bits_ = value; }
    void set_surface_type(uint16_t value) { surface_type_ = value; }

private:
    maths::BBox box_;         // +0xA4
    uint32_t poly_attr_bits_{0}; // +0xC4
    uint16_t surface_type_{0};   // +0xC8
    std::array<uint8_t, 4> unknownca_{};     // +0xCA (copied, not otherwise used here)
    std::array<uint8_t, 2> padce_{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(sizeof(HitMovingBox) == kHitMovingBoxBytes && offsetof(HitMovingBox, poly_attr_bits_) == kHitMovingBoxPolyAttrBitsOffset, "layout must match");
    }
};

constexpr std::size_t kCollScaledBytes = 0xA0;
constexpr std::size_t kCollScaledInverseOffset = 0x60;

// A scaled, rotated and placed collision object (0xA0 bytes, the original's vtable 0x00645B58), with the inverse
// transform. (Its slot 25, 0x00412050 - the scale's address, folded with HitMovingMesh::get_absolute_point - was never
// called.)
class CollScaled : public HitMesh {
public:
    // 0x00412460 (0x004135E0 the deleting destructor): only the base's body.
    ~CollScaled() override = default;

    // 0x00413610 / 0x00413640 (slots 5, 6): the position / rotation rows, then the inverse made again.
    void place_at_absolute_point(const maths::Vec4* position) override;
    void change_facing(const maths::Mat4* orientation) override;
    // 0x004136A0 (slot 24): the scale, then the inverse made again.
    virtual void change_scale(const maths::Vec4* scale);
    // 0x00412490: the inverse: the rotation's transpose divided by the scale, the position turned back.
    void update_inverse();
    // 0x00410A30: `line` (both ends, w included) through the inverse - into the object's space.
    void line_to_local(const maths::Seg* line, maths::Seg* out) const;
    // 0x00410AA0: `rect` into the object's space: its corner and the far ends of its sides through the inverse,
    // the sides measured from the new corner.
    void rect_to_local(const CollRect* rect, CollRect* out) const;
    // 0x004109C0 (slot 7)
    maths::Vec4* get_absolute_point() override;
    // 0x00412060 (slot 8; folded with HitMovingMesh::fetch_facing): the matrix.
    maths::Mat4* fetch_facing() override { return &basis_frame_; }
    // 0x004109D0 (slot 9): vertex `index` scaled (w the scale's w), through the matrix.
    [[nodiscard]] maths::Vec4 read_vertex_point(int index) const override;

    // The fields other code reads and sets.
    [[nodiscard]] const maths::Vec4& size_factor() const { return size_factor_; }
    void set_size_factor(const maths::Vec4& value) { size_factor_ = value; }
    maths::Mat4& basis_frame() { return basis_frame_; }
    [[nodiscard]] const maths::Mat4& basis_frame() const { return basis_frame_; }

private:
    maths::Vec4 size_factor_; // +0x10
    maths::Mat4 basis_frame_{}; // +0x20 (row 3, +0x50, the position)
    maths::Mat4 inverse_{};     // +0x60 (row 3, +0x90, the position turned back)

protected:
    // 0x004123C0: unscaled, unrotated, at the origin.
    CollScaled();
    // 0x00413F80: a copy.
    CollScaled(const CollScaled& other);

public:
    CollScaled& operator=(const CollScaled&) = delete;
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(sizeof(CollScaled) == kCollScaledBytes && offsetof(CollScaled, inverse_) == kCollScaledInverseOffset, "layout must match");
    }
};

} // namespace render

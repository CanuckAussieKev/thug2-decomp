// Movable collision objects: the movable base, the movable box and the scaled object.

#include "engine/render/coll_movable.h"

#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_tri.h"
#include "engine/core/stubs.h"
#include "engine/render/coll_obj.h"

namespace render {

namespace {

void set_identity(maths::Mat4& m)
{
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x0048D090: a box drawn with a transform (debug rendering; not rewritten yet).
void draw_box(const maths::BBox* box, const maths::Mat4* transform, uint32_t colour, int flag)
{
    (void)box;
    stubs::ignore_arguments3(reinterpret_cast<int>(transform), static_cast<int>(colour), flag);
}

} // namespace

// 0x00411FA0
HitMovingMesh::HitMovingMesh() : anchor_point_{0.0f, 0.0f, 0.0f, 1.0f}
{
    set_identity(orientation_);
    set_identity(inverse_);
}

// 0x00412660
HitMovingMesh::HitMovingMesh(const HitMovingMesh& other) = default;

// 0x004121A0
HitMovingMesh::~HitMovingMesh() { remove_movable_from_caches(this); }

// 0x00412070
void HitMovingMesh::change_facing(const maths::Mat4* orientation)
{
    orientation_.row[0] = orientation->row[0];
    orientation_.row[1] = orientation->row[1];
    orientation_.row[2] = orientation->row[2];
    static_cast<MatrixTranspose*>(&inverse_)->transpose_from(&orientation_);
}

// 0x00412050
maths::Vec4* HitMovingMesh::get_absolute_point() { return &anchor_point_; }

// 0x00412060
maths::Mat4* HitMovingMesh::fetch_facing() { return &orientation_; }

// 0x00412230
maths::Vec4 HitMovingMesh::read_vertex_point(int index) const
{
    maths::Vec4 raw = geometry_->read_stored_vertex(index);
    raw.w = 0.0f;
    maths::Vec4 turned{};
    orientation_.rotate_vector(turned, raw);
    return {turned.x + anchor_point_.x, turned.y + anchor_point_.y, turned.z + anchor_point_.z,
            turned.w + anchor_point_.w};
}

// 0x004122C0
void HitMovingMesh::box_to_local(const maths::BBox* box, maths::BBox* out) const
{
    *out = *box;
    out->min.x -= anchor_point_.x;
    out->min.y -= anchor_point_.y;
    out->min.z -= anchor_point_.z;
    out->min.w -= anchor_point_.w;
    out->max.x -= anchor_point_.x;
    out->max.y -= anchor_point_.y;
    out->max.z -= anchor_point_.z;
    out->max.w -= anchor_point_.w;
    maths::Vec4 turned{};
    out->min = inverse_.rotate_vector(turned, out->min);
    out->max = inverse_.rotate_vector(turned, out->max);
}

// 0x004120D0
maths::Mat4* MatrixTranspose::transpose_from(const maths::Mat4* source)
{
    if (source == this) {
        transpose_in_place();
        return this;
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c)
            (&row[r].x)[c] = (&source->row[c].x)[r];
    }
    return this;
}

// 0x00412150
void MatrixTranspose::transpose_in_place()
{
    for (int r = 0; r < 4; ++r) {
        for (int c = r + 1; c < 4; ++c) {
            const float swap = (&row[r].x)[c];
            (&row[r].x)[c] = (&row[c].x)[r];
            (&row[c].x)[r] = swap;
        }
    }
}

// 0x00412550
HitMovingBox::HitMovingBox()
{
    box_.min = {-50.0f, -20.0f, -90.0f, 1.0f};
    box_.max = {30.0f, 36.0f, 90.0f, 1.0f};
}

// 0x00413700
HitMovingBox::HitMovingBox(const HitMovingBox& other)
    : HitMovingMesh(other), box_(other.box_), poly_attr_bits_(other.poly_attr_bits_), surface_type_(other.surface_type_)
{
    for (int i = 0; i < 4; ++i)
        unknownca_[i] = other.unknownca_[i];
}

// 0x00412600
void HitMovingBox::assign_shape_data(TriMeshSource* geometry)
{
    geometry_ = geometry;
    if (geometry)
        checksum_ = geometry->checksum_;
    box_.min = geometry->lower_limit_;
    box_.max = geometry->upper_limit_;
}

// 0x0040AE60
uint32_t HitMovingBox::get_triangle_attrs(int /*face*/) const { return poly_attr_bits_; }

// 0x0040AE70
uint16_t HitMovingBox::get_face_terrain(int /*face*/) const { return surface_type_; }

// 0x00414BE0
HitMovingBox* HitMovingBox::replicate(int)
{
    auto* copy = new HitMovingBox(*this);
    copy->flag_bits_ |= 0x8000;
    copy->geometry_ = nullptr;
    return copy;
}

// 0x004126E0
void HitMovingBox::draw_diagnostic(int /*a*/, int /*b*/)
{
    maths::Mat4 transform = orientation_;
    transform.row[3] = anchor_point_;
    draw_box(&box_, &transform, 0x80808080, 1);
}

// 0x004123C0
CollScaled::CollScaled() : size_factor_{1.0f, 1.0f, 1.0f, 0.0f}
{
    set_identity(basis_frame_);
    set_identity(inverse_);
}

// 0x00413F80
CollScaled::CollScaled(const CollScaled& other) = default;

// 0x00413610
void CollScaled::place_at_absolute_point(const maths::Vec4* position)
{
    basis_frame_.row[3] = *position;
    update_inverse();
}

// 0x00413640
void CollScaled::change_facing(const maths::Mat4* orientation)
{
    basis_frame_.row[0] = orientation->row[0];
    basis_frame_.row[1] = orientation->row[1];
    basis_frame_.row[2] = orientation->row[2];
    update_inverse();
}

// 0x004136A0
void CollScaled::change_scale(const maths::Vec4* scale)
{
    size_factor_ = *scale;
    update_inverse();
}

// 0x00410A30
void CollScaled::line_to_local(const maths::Seg* line, maths::Seg* out) const
{
    maths::Vec4 moved{};
    inverse_.xform(moved, line->start);
    out->start = moved;
    inverse_.xform(moved, line->end);
    out->end = moved;
}

// 0x00412490 (each 1 / scale kept unrounded for its row's three products)
void CollScaled::update_inverse()
{
    for (int r = 0; r < 3; ++r) {
        const double inverse = 1.0 / (&size_factor_.x)[r];
        (&inverse_.row[0].x)[r] = static_cast<float>(inverse * basis_frame_.row[r].x);
        (&inverse_.row[1].x)[r] = static_cast<float>(inverse * basis_frame_.row[r].y);
        (&inverse_.row[2].x)[r] = static_cast<float>(inverse * basis_frame_.row[r].z);
    }
    maths::Vec4& back = inverse_.row[3];
    back.x = -basis_frame_.row[3].x;
    back.y = -basis_frame_.row[3].y;
    back.z = -basis_frame_.row[3].z;
    maths::Vec4 turned{};
    back = inverse_.rotate_vector(turned, back);
}

// 0x004109C0
maths::Vec4* CollScaled::get_absolute_point() { return &basis_frame_.row[3]; }

// 0x004109D0
maths::Vec4 CollScaled::read_vertex_point(int index) const
{
    const maths::Vec4 raw = geometry_->read_stored_vertex(index);
    const maths::Vec4 scaled{raw.x * size_factor_.x, raw.y * size_factor_.y, raw.z * size_factor_.z, size_factor_.w};
    maths::Vec4 out{};
    basis_frame_.xform(out, scaled);
    return out;
}

} // namespace render

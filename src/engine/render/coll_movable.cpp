// Movable collision objects: the movable base, the movable box and the scaled object.

#include "engine/render/coll_movable.h"

#include <cstdint>
#include <new>

#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_tri.h"
#include "engine/core/stubs.h"
#include "engine/render/coll_obj.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include <gsl/assert>

namespace render {

// The default box of a moving box, around a skater.
constexpr float kBoxMinX = 50.0f;
constexpr float kBoxMinY = 20.0f;
constexpr float kBoxMinZ = 90.0f;
constexpr float kBoxMaxX = 30.0f;
constexpr float kBoxMaxY = 36.0f;
constexpr float kBoxMaxZ = 90.0f;
constexpr uint32_t kDiagnosticBoxColour = 0x80808080u;

namespace {

void set_identity(maths::Mat4& m)
{
    core::at(m.row, 0) = {.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    core::at(m.row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f};
    core::at(m.row, 2) = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
    core::at(m.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
}

// 0x0048D090: a box drawn with a transform (debug rendering; not rewritten yet).
void draw_box(const maths::BBox* box, const maths::Mat4* transform, uint32_t colour, int flag)
{
    (void)box;
    stubs::ignore_arguments3(transform, static_cast<int>(colour), flag);
}

} // namespace

// 0x00411FA0
HitMovingMesh::HitMovingMesh() : anchor_point_{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}
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
    core::at(orientation_.row, 0) = core::at(orientation->row, 0);
    core::at(orientation_.row, 1) = core::at(orientation->row, 1);
    core::at(orientation_.row, 2) = core::at(orientation->row, 2);
    core::down_cast<MatrixTranspose>(&inverse_)->transpose_from(&orientation_);
}

// 0x00412050
maths::Vec4* HitMovingMesh::get_absolute_point() { return &anchor_point_; }

// 0x00412060
maths::Mat4* HitMovingMesh::fetch_facing() { return &orientation_; }

// 0x00412230
maths::Vec4 HitMovingMesh::read_vertex_point(int index) const
{
    maths::Vec4 raw = geometry()->read_stored_vertex(index);
    raw.w = 0.0f;
    maths::Vec4 turned{};
    orientation_.rotate_vector(turned, raw);
    return {.x = turned.x + anchor_point_.x, .y = turned.y + anchor_point_.y, .z = turned.z + anchor_point_.z,
            .w = turned.w + anchor_point_.w};
}

// 0x004122C0
void HitMovingMesh::segment_to_local(const maths::Seg* line, maths::Seg* out) const
{
    *out = *line;
    out->start.x -= anchor_point_.x;
    out->start.y -= anchor_point_.y;
    out->start.z -= anchor_point_.z;
    out->start.w -= anchor_point_.w;
    out->end.x -= anchor_point_.x;
    out->end.y -= anchor_point_.y;
    out->end.z -= anchor_point_.z;
    out->end.w -= anchor_point_.w;
    maths::Vec4 turned{};
    out->start = inverse_.rotate_vector(turned, out->start);
    out->end = inverse_.rotate_vector(turned, out->end);
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
            core::at(row, r)[c] = core::at(source->row, c)[r];
    }
    return this;
}

// 0x00412150
void MatrixTranspose::transpose_in_place()
{
    for (int r = 0; r < 4; ++r) {
        for (int c = r + 1; c < 4; ++c) {
            const float swap = core::at(row, r)[c];
            core::at(row, r)[c] = core::at(row, c)[r];
            core::at(row, c)[r] = swap;
        }
    }
}

// 0x00412550
HitMovingBox::HitMovingBox()
{
    box_.min = {.x = -kBoxMinX, .y = -kBoxMinY, .z = -kBoxMinZ, .w = 1.0f};
    box_.max = {.x = kBoxMaxX, .y = kBoxMaxY, .z = kBoxMaxZ, .w = 1.0f};
}

// 0x00413700
HitMovingBox::HitMovingBox(const HitMovingBox& other)
    : HitMovingMesh(other), box_(other.box_), poly_attr_bits_(other.poly_attr_bits_), surface_type_(other.surface_type_)
{
    for (int i = 0; i < 4; ++i)
        core::at(unknownca_, i) = core::at(other.unknownca_, i);
}

// 0x00412600
void HitMovingBox::assign_shape_data(TriMeshSource* geometry)
{
    set_geometry(geometry);
    if (geometry)
        assign_checksum(geometry->checksum());
    Expects(geometry != nullptr);
    box_.min = geometry->bounds().min;
    box_.max = geometry->bounds().max;
}

// 0x0040AE60
FaceFlag HitMovingBox::get_triangle_attrs(int /*face*/) const { return static_cast<FaceFlag>(poly_attr_bits_); }

// 0x0040AE70
uint16_t HitMovingBox::get_face_terrain(int /*face*/) const { return surface_type_; }

// 0x00414BE0
gsl::owner<HitMovingBox*> HitMovingBox::replicate(int)
{
    gsl::owner<render::HitMovingBox*> copy{memory::make_owned<HitMovingBox>(*this)};
    copy->flag_bits() |= HitMesh::kOwnsGeometry;
    copy->set_geometry(nullptr);
    return copy;
}

// 0x004126E0
void HitMovingBox::draw_diagnostic(int /*a*/, int /*b*/)
{
    maths::Mat4 transform = orientation();
    core::at(transform.row, 3) = anchor_point();
    draw_box(&box_, &transform, kDiagnosticBoxColour, 1);
}

// 0x004123C0
CollScaled::CollScaled() : size_factor_{.x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 0.0f}
{
    set_identity(basis_frame_);
    set_identity(inverse_);
}

// 0x00413F80
CollScaled::CollScaled(const CollScaled& other) = default;

// 0x00413610
void CollScaled::place_at_absolute_point(const maths::Vec4* position)
{
    core::at(basis_frame_.row, 3) = *position;
    update_inverse();
}

// 0x00413640
void CollScaled::change_facing(const maths::Mat4* orientation)
{
    core::at(basis_frame_.row, 0) = core::at(orientation->row, 0);
    core::at(basis_frame_.row, 1) = core::at(orientation->row, 1);
    core::at(basis_frame_.row, 2) = core::at(orientation->row, 2);
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
        const double inverse = 1.0 / size_factor_[r];
        core::at(inverse_.row, 0)[r] = static_cast<float>(inverse * core::at(basis_frame_.row, r).x);
        core::at(inverse_.row, 1)[r] = static_cast<float>(inverse * core::at(basis_frame_.row, r).y);
        core::at(inverse_.row, 2)[r] = static_cast<float>(inverse * core::at(basis_frame_.row, r).z);
    }
    maths::Vec4& back = core::at(inverse_.row, 3);
    back.x = -core::at(basis_frame_.row, 3).x;
    back.y = -core::at(basis_frame_.row, 3).y;
    back.z = -core::at(basis_frame_.row, 3).z;
    maths::Vec4 turned{};
    back = inverse_.rotate_vector(turned, back);
}

// 0x004109C0
maths::Vec4* CollScaled::get_absolute_point() { return &core::at(basis_frame_.row, 3); }

// 0x004109D0
maths::Vec4 CollScaled::read_vertex_point(int index) const
{
    const maths::Vec4 raw = geometry()->read_stored_vertex(index);
    const maths::Vec4 scaled{.x = raw.x * size_factor_.x, .y = raw.y * size_factor_.y, .z = raw.z * size_factor_.z, .w = size_factor_.w};
    maths::Vec4 out{};
    basis_frame_.xform(out, scaled);
    return out;
}

} // namespace render

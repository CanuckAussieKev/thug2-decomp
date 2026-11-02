// Triangle collision objects: a level sector's, an instance of one, a movable one.

#include "engine/render/coll_static.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/render/coll_tri.h"
#include "engine/render/coll_world.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_movable.h"
#include "engine/sys/startup_init.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace render {

namespace {
maths::Vec4 g_shared_origin; // the origin and orientation every static object reports
maths::Mat4 g_shared_orientation;

// The geometry's own methods: 0x00416BF0 a copy, 0x00416400 moved.
TriMeshSource* clone_geometry(TriMeshSource* geometry, int a, int b) { return geometry->replicate(a, b != 0); }
void translate_geometry(TriMeshSource* geometry, const maths::Vec4* delta) { geometry->get_slide(delta); }
// 0x00414D60 through our stub (the original's is the same empty function).
void draw_geometry(TriMeshSource* geometry, const maths::Mat4* transform, int a, int b)
{
    draw_geometry_diagnostic(geometry, transform, a, b, 1);
}

} // namespace

// 0x00410BF0
uint32_t HitMesh::read_face_attrs(int face) const
{
    return geometry_->face_flags(face);
}

// 0x00411750
uint16_t HitMesh::read_face_terrain(int face) const
{
    return geometry_->face_terrain(face);
}

// 0x00412760
StaticCollider::~StaticCollider()
{
    if ((flag_bits_ & kOwnsGeometry) && octets_) {
        memory::free_malloc(octets_);
        octets_ = nullptr;
    }
}

// 0x004139A0
HitMesh* StaticCollider::replicate(int argument)
{
    const bool instance = static_cast<uint8_t>(argument) != 0;
    TriMeshSource* geometry = instance ? geometry_ : clone_geometry(geometry_, 0, 1);
    if (!geometry)
        return nullptr;
    gsl::owner<CollStaticInstance*> copy_instance = nullptr;
    gsl::owner<StaticCollider*> copy = nullptr;
    HitMesh* made = nullptr;
    if (instance) {
        copy_instance = new CollStaticInstance();
        made = copy_instance;
    } else {
        copy = new StaticCollider();
        made = copy;
    }
    // (a failed allocation crashes here, as in the original)
    made->checksum_ = checksum_;
    made->flag_bits_ = flag_bits_;
    made->visit_stamp_ = visit_stamp_;
    made->geometry_ = geometry_;
    if (copy_instance) {
        copy_instance->set_shared_geometry(geometry);
    } else if (copy) {
        copy->flag_bits_ |= kOwnsGeometry;
        copy->geometry_ = geometry;
        if (!instance && octets_) {
            const uint32_t size = geometry_->tri_count_;
            memory::push_mem_profile("Collision face-rejection optimization");
            memory::enter_heap_scope(memory::bottom_up_heap());
            copy->octets_ = static_cast<uint8_t*>(memory::alloc_block(size));
            std::memcpy(copy->octets_, octets_, size);
            memory::leave_heap_scope();
            memory::pop_mem_profile();
        } else {
            copy->octets_ = nullptr;
        }
    }
    return made;
}

// 0x00410C20
void StaticCollider::place_at_absolute_point(const maths::Vec4* position)
{
    const maths::Vec4* current = get_absolute_point();
    const maths::Vec4 delta{position->x - current->x, position->y - current->y, position->z - current->z,
                            position->w - current->w};
    translate_geometry(geometry_, &delta);
}

// 0x004123A0
const maths::Vec4* StaticCollider::get_absolute_point() { return &g_shared_origin; }

// 0x004123B0
const maths::Mat4* StaticCollider::fetch_facing() { return &g_shared_orientation; }

// 0x00410980
maths::Vec4 StaticCollider::read_vertex_point(int index) const
{
    const maths::Vec4 raw = geometry_->read_stored_vertex(index);
    return {raw.x, raw.y, raw.z, 1.0f};
}

// 0x00410C70
bool StaticCollider::inside_bounds(const maths::BBox* box)
{
    return box->intersects(geometry_->bounds_);
}

// 0x00412750
const maths::BBox* StaticCollider::get_world_bounds()
{
    return &geometry_->bounds_;
}

// 0x00412840
CollStaticInstance::~CollStaticInstance()
{
    if ((flag_bits_ & kOwnsGeometry) && octets_) {
        memory::free_malloc(octets_);
        octets_ = nullptr;
    }
}

// 0x00414CD0
gsl::owner<CollStaticInstance*> CollStaticInstance::replicate(int /*argument*/)
{
    auto copy = memory::make_owned<CollStaticInstance>(*this);
    copy->set_shared_geometry(geometry_); // (on a null copy too: a crash, as in the original)
    return copy;
}

// 0x00411480
void CollStaticInstance::set_shared_geometry(TriMeshSource* geometry)
{
    flag_bits_ |= kOwnsGeometry | kSharedGeometry;
    geometry_ = geometry;
    world_box_.min = geometry->bounds_.min;
    world_box_.max = geometry->bounds_.max;
    octets_ = nullptr;
}

// 0x00413F00
void CollStaticInstance::place_at_absolute_point(const maths::Vec4* position)
{
    core::at(basis_frame_.row, 3) = *position;
    update_inverse();
    recompute_world_bounds();
}

// 0x00413F30
void CollStaticInstance::change_facing(const maths::Mat4* orientation)
{
    CollScaled::change_facing(orientation);
    recompute_world_bounds();
}

// 0x00413F50
void CollStaticInstance::change_scale(const maths::Vec4* scale)
{
    size_factor_ = *scale;
    update_inverse();
    recompute_world_bounds();
}

// 0x004113B0
void CollStaticInstance::recompute_world_bounds()
{
    world_box_.min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    world_box_.max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    const int count = geometry_->num_vertices_;
    for (int i = 0; i < count; ++i) {
        maths::Vec4 raw{};
        geometry_->read_stored_vertex(i, &raw);
        const maths::Vec4 scaled{raw.x * size_factor_.x, raw.y * size_factor_.y, raw.z * size_factor_.z,
                                 raw.w * size_factor_.w};
        maths::Vec4 placed{};
        basis_frame_.carry_point_through(placed, scaled);
        world_box_.append_spot(placed);
    }
}

// 0x00411460
bool CollStaticInstance::inside_bounds(const maths::BBox* box) { return box->intersects(world_box_); }

// 0x00412830
maths::BBox* CollStaticInstance::get_world_bounds() { return &world_box_; }

// 0x00411330 (the position compared exactly; NaN is unequal)
void CollStaticInstance::scale_about(const maths::Vec4* position, const maths::Vec4* scale)
{
    const maths::Vec4& at = core::at(basis_frame_.row, 3);
    if (!(position->x == at.x) || !(position->y == at.y) || !(position->z == at.z))
        return;
    const maths::Vec4 scaled{scale->x * size_factor_.x, scale->y * size_factor_.y, scale->z * size_factor_.z,
                             scale->w * size_factor_.w};
    change_scale(&scaled);
}

// 0x00412F50
void CollStaticInstance::draw_diagnostic(int a, int b)
{
    maths::Mat4 transform = basis_frame_;
    transform.scale_rows(size_factor_);
    draw_geometry(geometry_, &transform, a, b);
}

// 0x00413000
HitMovingTriangles::HitMovingTriangles(TriMeshSource* geometry)
{
    assign_shape_data(geometry);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    unknownd0_ = geometry->unknown3c_;
    box_dirty_ = true;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x00413080
HitMovingTriangles::HitMovingTriangles(const HitMovingTriangles& other)
    : HitMovingMesh(other), radius_(other.radius_), box_dirty_(other.box_dirty_), box_(other.box_),
      unknowncc_(other.unknowncc_), unknownd0_(other.unknownd0_)
{}

// 0x00414400
gsl::owner<HitMovingTriangles*> HitMovingTriangles::replicate(int argument)
{
    auto copy = memory::make_owned<HitMovingTriangles>(*this);
    copy->flag_bits_ |= kOwnsGeometry; // (a failed allocation crashes here, as in the original)
    copy->geometry_ = clone_geometry(geometry_, argument, 0);
    return copy;
}

// 0x00411780 (a null geometry crashes, as in the original)
void HitMovingTriangles::assign_shape_data(TriMeshSource* geometry)
{
    geometry_ = geometry;
    if (geometry)
        checksum_ = geometry->checksum_;
    radius_ = 0.0f;
    for (int i = 0; std::cmp_less(i, geometry->num_vertices_); ++i) {
        const maths::Vec4 v = geometry_->read_stored_vertex(i);
        const double length = std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) +
                                        static_cast<double>(v.z) * v.z);
        if (length > radius_)
            radius_ = static_cast<float>(length);
        geometry = geometry_;
    }
    box_dirty_ = true;
}

// 0x00412F90
void HitMovingTriangles::place_at_absolute_point(const maths::Vec4* position)
{
    anchor_point_ = *position;
    box_dirty_ = true;
}

// 0x00411810
bool HitMovingTriangles::inside_bounds(const maths::BBox* box)
{
    if (box_dirty_) {
        update_b_box();
        box_dirty_ = false;
    }
    return box->intersects(box_);
}

// 0x00412FC0
maths::BBox* HitMovingTriangles::get_local_bounds()
{
    if (box_dirty_) {
        update_b_box();
        box_dirty_ = false;
    }
    return &box_;
}

// 0x0040B390
void HitMovingTriangles::update_b_box()
{
    box_.min = {anchor_point_.x - radius_, anchor_point_.y - radius_, anchor_point_.z - radius_, 1.0f};
    box_.max = {radius_ + anchor_point_.x, anchor_point_.y + radius_, anchor_point_.z + radius_, 1.0f};
}

// 0x00413130
void HitMovingTriangles::draw_diagnostic(int a, int b)
{
    maths::Mat4 transform = orientation_;
    core::at(transform.row, 3) = anchor_point_;
    draw_geometry(geometry_, &transform, a, b);
}

// 0x00414D60
void draw_geometry_diagnostic(const TriMeshSource* /*geometry*/, const maths::Mat4* /*transform*/, int /*a*/, int /*b*/,
                              int /*c*/)
{
}

// 0x0040AE80
void StaticCollider::rotate_y(const maths::Vec4* centre, int quarter_turns)
{
    geometry_->rotate_y(centre, quarter_turns);
}

// 0x0040AE90
void StaticCollider::scale_about(const maths::Vec4* centre, const maths::Vec4* scale)
{
    geometry_->scale_tool(centre, scale);
}

// 0x0040AEB0
void HitMesh::add_occlusion_polys() { geometry_->add_occlusion_polys(); }

// 0x0040AEC0
bool HitMesh::is_point_behind_faces(const maths::Vec4* point, int unused)
{
    return geometry_->is_point_behind_faces(point, unused);
}

// 0x00414990 (a failed allocation of the box crashes when there are geometries, as in the original)
HitMesh* build_dynamic_hit_mesh(int type, TriMeshSource* geometries, int count, objects::GameEntity* owner)
{
    gsl::owner<HitMesh*> made = nullptr;
    if (type == 1) {
        auto box = memory::make_owned_value<HitMovingBox>();
        if (count >= 1) {
            maths::BBox bounds;
            for (int i = 0; i < count; ++i) {
                bounds.append_spot(geometries[i].bounds_.min);
                bounds.append_spot(geometries[i].bounds_.max);
            }
            box->box_ = bounds;
        }
        made = box;
    } else if (type == 4) {
        if (count == 1) {
            made = new HitMovingTriangles(geometries);
        } else if (count > 1) {
            auto multi = memory::make_owned_value<HitMeshGroup>();
            for (int i = 0; i < count; ++i)
                multi->attach_child(new HitMovingTriangles(&geometries[i]));
            made = multi;
        } else {
            return nullptr;
        }
    } else {
        return nullptr;
    }
    if (made) {
        made->set_owner(owner);
        register_movable(made); // (the original also pushes `owner`, which register_movable doesn't use)
    }
    return made;
}

} // namespace render

namespace render {

// 0x00640860 (a static initialiser): the shared orientation starts as the rotation of no angles.
void construct_shared_orientation() { g_shared_orientation.set_from_euler_angles(0.0f, 0.0f, 0.0f); }

} // namespace render

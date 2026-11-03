// Triangle collision objects: a level sector's, an instance of one, a movable one.

#include "engine/render/coll_static.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <utility>

#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "engine/render/coll_world.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_movable.h"
#include "engine/sys/startup_init.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include <gsl/pointers>

namespace render {

namespace {
// NOT ORIGINAL grouping - the origin and orientation every static object reports (made by the start-up initialisers).
// Made on first use, never destroyed (shared_placement()).
struct SharedPlacement {
    maths::Vec4 shared_origin_{}; // the origin and orientation every static object reports
    maths::Mat4 shared_orientation_{};
};

SharedPlacement& shared_placement()
{
    static core::NoDestroy<SharedPlacement> state;
    return state.get();
}

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
    if ((flag_bits() & kOwnsGeometry) && octets_) {
        memory::free_malloc(octets_);
        octets_ = nullptr;
    }
}

// 0x004139A0
HitMesh* StaticCollider::replicate(int argument)
{
    const bool instance = static_cast<uint8_t>(argument) != 0;
    TriMeshSource* geometry = instance ? this->geometry() : clone_geometry(this->geometry(), 0, 1);
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
    made->assign_checksum(checksum());
    made->flag_bits() = flag_bits();
    made->set_visit_stamp(visit_stamp());
    made->set_geometry(this->geometry());
    if (copy_instance) {
        copy_instance->set_shared_geometry(geometry);
    } else if (copy) {
        copy->flag_bits() |= kOwnsGeometry;
        copy->set_geometry(geometry);
        if (!instance && octets_) {
            const uint32_t size = this->geometry()->tri_count();
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
    const maths::Vec4 delta{.x = position->x - current->x, .y = position->y - current->y, .z = position->z - current->z,
                            .w = position->w - current->w};
    translate_geometry(geometry(), &delta);
}

// 0x004123A0
const maths::Vec4* StaticCollider::get_absolute_point() { return &shared_placement().shared_origin_; }

// 0x004123B0
const maths::Mat4* StaticCollider::fetch_facing() { return &shared_placement().shared_orientation_; }

// 0x00410980
maths::Vec4 StaticCollider::read_vertex_point(int index) const
{
    const maths::Vec4 raw = geometry()->read_stored_vertex(index);
    return {.x = raw.x, .y = raw.y, .z = raw.z, .w = 1.0f};
}

// 0x00410C70
bool StaticCollider::inside_bounds(const maths::BBox* box)
{
    return box->intersects(geometry()->bounds());
}

// 0x00412750
const maths::BBox* StaticCollider::get_world_bounds()
{
    return &geometry()->bounds();
}

// 0x00412840
CollStaticInstance::~CollStaticInstance()
{
    if ((flag_bits() & kOwnsGeometry) && octets_) {
        memory::free_malloc(octets_);
        octets_ = nullptr;
    }
}

// 0x00414CD0
gsl::owner<CollStaticInstance*> CollStaticInstance::replicate(int /*argument*/)
{
    auto copy = memory::make_owned<CollStaticInstance>(*this);
    copy->set_shared_geometry(geometry()); // (on a null copy too: a crash, as in the original)
    return copy;
}

// 0x00411480
void CollStaticInstance::set_shared_geometry(TriMeshSource* geometry)
{
    flag_bits() |= kOwnsGeometry | kSharedGeometry;
    set_geometry(geometry);
    world_box_.min = geometry->bounds().min;
    world_box_.max = geometry->bounds().max;
    octets_ = nullptr;
}

// 0x00413F00
void CollStaticInstance::place_at_absolute_point(const maths::Vec4* position)
{
    core::at(basis_frame().row, 3) = *position;
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
    set_size_factor(*scale);
    update_inverse();
    recompute_world_bounds();
}

// 0x004113B0
void CollStaticInstance::recompute_world_bounds()
{
    world_box_.min = {.x = kEmptyBoxExtent, .y = kEmptyBoxExtent, .z = kEmptyBoxExtent, .w = 1.0f};
    world_box_.max = {.x = -kEmptyBoxExtent, .y = -kEmptyBoxExtent, .z = -kEmptyBoxExtent, .w = 1.0f};
    const int count = geometry()->num_vertices();
    for (int i = 0; i < count; ++i) {
        maths::Vec4 raw{};
        geometry()->read_stored_vertex(i, &raw);
        const maths::Vec4 scaled{.x = raw.x * size_factor().x, .y = raw.y * size_factor().y, .z = raw.z * size_factor().z,
                                 .w = raw.w * size_factor().w};
        maths::Vec4 placed{};
        basis_frame().carry_point_through(placed, scaled);
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
    const maths::Vec4& at = core::at(basis_frame().row, 3);
    if (!(position->x == at.x) || !(position->y == at.y) || !(position->z == at.z))
        return;
    const maths::Vec4 scaled{.x = scale->x * size_factor().x, .y = scale->y * size_factor().y, .z = scale->z * size_factor().z,
                             .w = scale->w * size_factor().w};
    change_scale(&scaled);
}

// 0x00412F50
void CollStaticInstance::draw_diagnostic(int a, int b)
{
    maths::Mat4 transform = basis_frame();
    transform.scale_rows(size_factor());
    draw_geometry(geometry(), &transform, a, b);
}

// 0x00413000
HitMovingTriangles::HitMovingTriangles(TriMeshSource* geometry) : unknownd0_(geometry->unknown3c())
{
    assign_shape_data(geometry);
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
    copy->flag_bits() |= kOwnsGeometry; // (a failed allocation crashes here, as in the original)
    copy->set_geometry(clone_geometry(geometry(), argument, 0));
    return copy;
}

// 0x00411780 (a null geometry crashes, as in the original)
void HitMovingTriangles::assign_shape_data(TriMeshSource* geometry)
{
    set_geometry(geometry);
    if (geometry)
        assign_checksum(geometry->checksum());
    radius_ = 0.0f;
    for (int i = 0; std::cmp_less(i, geometry->num_vertices()); ++i) {
        const maths::Vec4 v = this->geometry()->read_stored_vertex(i);
        const double length = std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) +
                                        static_cast<double>(v.z) * v.z);
        if (length > radius_)
            radius_ = static_cast<float>(length);
        geometry = this->geometry();
    }
    box_dirty_ = true;
}

// 0x00412F90
void HitMovingTriangles::place_at_absolute_point(const maths::Vec4* position)
{
    set_anchor_point(*position);
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
    box_.min = {.x = anchor_point().x - radius_, .y = anchor_point().y - radius_, .z = anchor_point().z - radius_, .w = 1.0f};
    box_.max = {.x = radius_ + anchor_point().x, .y = anchor_point().y + radius_, .z = anchor_point().z + radius_, .w = 1.0f};
}

// 0x00413130
void HitMovingTriangles::draw_diagnostic(int a, int b)
{
    maths::Mat4 transform = orientation();
    core::at(transform.row, 3) = anchor_point();
    draw_geometry(geometry(), &transform, a, b);
}

// 0x00414D60
void draw_geometry_diagnostic(const TriMeshSource* /*geometry*/, const maths::Mat4* /*transform*/, int /*a*/, int /*b*/,
                              int /*c*/)
{
}

// 0x0040AE80
void StaticCollider::rotate_y(const maths::Vec4* centre, int quarter_turns)
{
    geometry()->rotate_y(centre, quarter_turns);
}

// 0x0040AE90
void StaticCollider::scale_about(const maths::Vec4* centre, const maths::Vec4* scale)
{
    geometry()->scale_tool(centre, scale);
}

// 0x0040AEB0
void HitMesh::add_occlusion_polys() { geometry_->add_occlusion_polys(); }

// 0x0040AEC0
bool HitMesh::is_point_behind_faces(const maths::Vec4* point, int unused)
{
    return geometry_->is_point_behind_faces(point, unused);
}

// 0x00414990 (a failed allocation of the box crashes when there are geometries, as in the original)
gsl::owner<HitMesh*> build_dynamic_hit_mesh(int type, TriMeshSource* geometry_array, int count, objects::GameEntity* owner)
{
    const std::span<TriMeshSource> geometries(geometry_array, geometry_array ? static_cast<size_t>(count) : 0);
    gsl::owner<HitMesh*> made = nullptr;
    if (type == 1) {
        auto box = memory::make_owned_value<HitMovingBox>();
        if (count >= 1) {
            maths::BBox bounds;
            for (int i = 0; i < count; ++i) {
                bounds.append_spot(core::at(geometries, i).bounds().min);
                bounds.append_spot(core::at(geometries, i).bounds().max);
            }
            box->box() = bounds;
        }
        made = box;
    } else if (type == 4) {
        if (count == 1) {
            made = new HitMovingTriangles(geometry_array);
        } else if (count > 1) {
            auto multi = memory::make_owned_value<HitMeshGroup>();
            for (int i = 0; i < count; ++i)
                multi->attach_child(new HitMovingTriangles(&core::at(geometries, i)));
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
void construct_shared_orientation() { shared_placement().shared_orientation_.set_from_euler_angles(0.0f, 0.0f, 0.0f); }

} // namespace render

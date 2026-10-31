// Triangle collision objects: a level sector's, an instance of one, a movable one.

#include "engine/render/coll_static.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/render/coll_tri.h"
#include "engine/render/coll_world.h"
#include "engine/core/stubs.h"
#include "engine/core/game_vtables.h"
#include "engine/core/code_address.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_movable.h"
#include "engine/sys/startup_init.h"

namespace render {

maths::Vec4 g_shared_origin; // the origin and orientation every static object reports
maths::Mat4 g_shared_orientation;

namespace {

static VtableSlot g_instance_vtable_slots[] = {&render::CollStaticInstance::scalar_delete,
                                               stubs::return_true,
                                               &render::CollFaceAccess::get_triangle_attrs,
                                               &render::CollFaceAccess::get_face_terrain,
                                               &render::HitMesh::assign_shape_data,
                                               &render::CollStaticInstance::place_at_absolute_point,
                                               &render::CollStaticInstance::change_facing,
                                               &render::CollScaled::get_absolute_point,
                                               &render::HitMovingMesh::fetch_facing,
                                               &render::CollScaled::read_vertex_point,
                                               stubs::return_zero,
                                               stubs::ignore_argument,
                                               &render::CollStaticInstance::replicate,
                                               &render::CollStaticInstance::inside_bounds,
                                               &render::CollStaticInstance::collide_with_line,
                                               &render::CollStaticInstance::collide_with_rect,
                                               &render::StaticCollider::collide_with_sphere,
                                               &render::StaticCollider::collide_with_capsule,
                                               &render::CollGeometryForwarders::is_point_behind_faces,
                                               &render::CollStaticInstance::draw_diagnostic,
                                               stubs::return_zero,
                                               &render::CollStaticInstance::get_world_bounds,
                                               stubs::ignore_arguments2,
                                               &render::CollStaticInstance::rescale_if_at,
                                               &render::CollStaticInstance::change_scale,
                                               &render::HitMovingMesh::get_absolute_point};
void** const kInstanceVtable = reinterpret_cast<void**>(g_instance_vtable_slots);

// The geometry's own methods: 0x00416BF0 a copy, 0x00416400 moved.
TriMeshSource* clone_geometry(TriMeshSource* geometry, int a, int b) { return geometry->replicate(a, b != 0); }
void translate_geometry(TriMeshSource* geometry, const maths::Vec4* delta) { geometry->get_slide(delta); }
// 0x00414D60 through our stub (the original's is the same empty function).
void draw_geometry(TriMeshSource* geometry, const maths::Mat4* transform, int a, int b)
{
    reinterpret_cast<CollGeometryStubs*>(geometry)->draw_diagnostic(transform, a, b, 1);
}
const uint8_t* get_face(const TriMeshSource* geometry, int face)
{
    return geometry->faces_ + face * (geometry->small_faces_ ? 8 : 10);
}

} // namespace

// 0x00410BF0
uint32_t CollFaceAccess::get_triangle_attrs(int face) const
{
    return *reinterpret_cast<const uint16_t*>(get_face(geometry_, face));
}

// 0x00411750
uint16_t CollFaceAccess::get_face_terrain(int face) const
{
    return *reinterpret_cast<const uint16_t*>(get_face(geometry_, face) + 2);
}

// 0x00412730 (not a function in Ghidra: the array constructor's element constructor)
StaticCollider* StaticCollider::make_new()
{
    flag_bits_ = 0;
    geometry_ = nullptr;
    vtable_ = vt::kVtblStaticColl;
    octets_ = nullptr;
    unknown14_ = 0;
    return this;
}

// 0x00412760
void StaticCollider::destruct()
{
    vtable_ = vt::kVtblStaticColl;
    if ((flag_bits_ & kOwnsGeometry) && octets_) {
        memory::free_malloc(octets_);
        octets_ = nullptr;
    }
    destroy_base();
}

// 0x004139A0
HitMesh* StaticCollider::replicate(bool instance)
{
    TriMeshSource* geometry = instance ? geometry_ : clone_geometry(geometry_, 0, 1);
    if (!geometry)
        return nullptr;
    CollStaticInstance* copy_instance = nullptr;
    StaticCollider* copy = nullptr;
    HitMesh* made;
    if (instance) {
        void* memory = memory::alloc(sizeof(CollStaticInstance));
        copy_instance = memory ? static_cast<CollStaticInstance*>(memory)->make_new() : nullptr;
        made = copy_instance;
    } else {
        auto* memory = static_cast<StaticCollider*>(memory::alloc(sizeof(StaticCollider)));
        if (memory) {
            memory->flag_bits_ = 0;
            memory->geometry_ = nullptr;
            memory->vtable_ = vt::kVtblStaticColl;
            memory->octets_ = nullptr;
            memory->unknown14_ = 0;
        }
        copy = memory;
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
            uint32_t size = geometry_->tri_count_;
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
    const auto* current = get_call<const maths::Vec4*>(7);
    maths::Vec4 delta{position->x - current->x, position->y - current->y, position->z - current->z,
                      position->w - current->w};
    translate_geometry(geometry_, &delta);
}

// 0x004123A0
const maths::Vec4* StaticCollider::get_absolute_point() const { return &g_shared_origin; }

// 0x004123B0
const maths::Mat4* StaticCollider::fetch_facing() const { return &g_shared_orientation; }

// 0x00410980
maths::Vec4 StaticCollider::read_vertex_point(int index) const
{
    maths::Vec4 raw = geometry_->read_stored_vertex(index);
    return {raw.x, raw.y, raw.z, 1.0f};
}

// 0x00410C70
bool StaticCollider::inside_bounds(const maths::BBox* box) const
{
    return box->intersects(*reinterpret_cast<const maths::BBox*>(&geometry_->lower_limit_));
}

// 0x00412750
const maths::BBox* StaticCollider::get_geometry_b_box() const
{
    return reinterpret_cast<const maths::BBox*>(&geometry_->lower_limit_);
}

// 0x004127C0
CollStaticInstance* CollStaticInstance::make_new()
{
    CollScaled::make_new();
    vtable_ = kInstanceVtable;
    new (&world_box_) maths::BBox();
    octets_ = nullptr;
    unknownc4_ = 0;
    return this;
}

// 0x00414C30
CollStaticInstance* CollStaticInstance::construct_copy(const CollStaticInstance* other)
{
    static_cast<CollScaled*>(this)->construct_copy(other);
    vtable_ = kInstanceVtable;
    world_box_ = other->world_box_;
    octets_ = other->octets_;
    unknownc4_ = other->unknownc4_;
    return this;
}

// 0x00412840
void CollStaticInstance::destruct()
{
    vtable_ = kInstanceVtable;
    if ((flag_bits_ & kOwnsGeometry) && octets_) {
        memory::free_malloc(octets_);
        octets_ = nullptr;
    }
    destroy_base();
}

// 0x00413ED0
CollStaticInstance* CollStaticInstance::scalar_delete(uint8_t flags)
{
    destruct();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00414CD0
CollStaticInstance* CollStaticInstance::replicate(int /*argument*/)
{
    void* memory = memory::alloc(sizeof(CollStaticInstance));
    CollStaticInstance* copy = memory ? static_cast<CollStaticInstance*>(memory)->construct_copy(this) : nullptr;
    copy->set_shared_geometry(geometry_); // (on a null copy too: a crash, as in the original)
    return copy;
}

// 0x00411480
void CollStaticInstance::set_shared_geometry(TriMeshSource* geometry)
{
    flag_bits_ |= kOwnsGeometry | kSharedGeometry;
    geometry_ = geometry;
    world_box_.min = geometry->lower_limit_;
    world_box_.max = geometry->upper_limit_;
    octets_ = nullptr;
}

// 0x00413F00
void CollStaticInstance::place_at_absolute_point(const maths::Vec4* position)
{
    basis_frame_.row[3] = *position;
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
    int count = geometry_->num_vertices_;
    for (int i = 0; i < count; ++i) {
        maths::Vec4 raw;
        geometry_->read_stored_vertex(i, &raw);
        maths::Vec4 scaled{raw.x * size_factor_.x, raw.y * size_factor_.y, raw.z * size_factor_.z,
                           raw.w * size_factor_.w};
        maths::Vec4 placed;
        basis_frame_.carry_point_through(placed, scaled);
        world_box_.append_spot(placed);
    }
}

// 0x00411460
bool CollStaticInstance::inside_bounds(const maths::BBox* box) const { return box->intersects(world_box_); }

// 0x00412830
maths::BBox* CollStaticInstance::get_world_bounds() { return &world_box_; }

// 0x00411330 (the position compared exactly; NaN is unequal)
void CollStaticInstance::rescale_if_at(const maths::Vec4* position, const maths::Vec4* scale)
{
    const maths::Vec4& at = basis_frame_.row[3];
    if (!(position->x == at.x) || !(position->y == at.y) || !(position->z == at.z))
        return;
    maths::Vec4 scaled{scale->x * size_factor_.x, scale->y * size_factor_.y, scale->z * size_factor_.z,
                       scale->w * size_factor_.w};
    get_call<void>(24, &scaled); // change_scale
}

// 0x00412F50
void CollStaticInstance::draw_diagnostic(int a, int b)
{
    maths::Mat4 transform = basis_frame_;
    transform.scale_rows(size_factor_);
    draw_geometry(geometry_, &transform, a, b);
}

// 0x00413000
HitMovingTriangles* HitMovingTriangles::make_new(TriMeshSource* geometry)
{
    HitMovingMesh::make_new();
    vtable_ = vt::kVtblMovingMeshColl;
    new (&box_) maths::BBox();
    assign_shape_data(geometry);
    unknownd0_ = geometry->unknown3c_;
    box_dirty_ = true;
    return this;
}

// 0x00413080
HitMovingTriangles* HitMovingTriangles::construct_copy(const HitMovingTriangles* other)
{
    HitMovingMesh::construct_copy(other);
    vtable_ = vt::kVtblMovingMeshColl;
    radius_ = other->radius_;
    box_dirty_ = other->box_dirty_;
    box_ = other->box_;
    unknowncc_ = other->unknowncc_;
    unknownd0_ = other->unknownd0_;
    return this;
}

// 0x00413070
void HitMovingTriangles::destruct()
{
    vtable_ = vt::kVtblMovingMeshColl;
    HitMovingMesh::destruct();
}

// 0x004143D0
HitMovingTriangles* HitMovingTriangles::scalar_delete(uint8_t flags)
{
    destruct();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00414400
HitMovingTriangles* HitMovingTriangles::replicate(int argument)
{
    void* memory = memory::alloc(sizeof(HitMovingTriangles));
    HitMovingTriangles* copy = memory ? static_cast<HitMovingTriangles*>(memory)->construct_copy(this) : nullptr;
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
    for (int i = 0; i < geometry->num_vertices_; ++i) {
        maths::Vec4 v = geometry_->read_stored_vertex(i);
        double length = std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) +
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
    transform.row[3] = anchor_point_;
    draw_geometry(geometry_, &transform, a, b);
}

// 0x00414D60
void CollGeometryStubs::draw_diagnostic(const maths::Mat4* /*transform*/, int /*a*/, int /*b*/, int /*c*/) {}

// 0x0040AE80
void CollGeometryForwarders::rotate_y(const maths::Vec4* centre, int quarter_turns)
{
    geometry_->rotate_y(centre, quarter_turns);
}

// 0x0040AE90
void CollGeometryForwarders::scale_tool(const maths::Vec4* centre, const maths::Vec4* scale)
{
    geometry_->scale_tool(centre, scale);
}

// 0x0040AEA0
void CollGeometryForwarders::draw_diagnostic(int a, int b) { stubs::ignore_arguments2(a, b); }

// 0x0040AEB0
void CollGeometryForwarders::add_occlusion_polys() { geometry_->add_occlusion_polys(); }

// 0x0040AEC0
bool CollGeometryForwarders::is_point_behind_faces(const maths::Vec4* point, int unused)
{
    return geometry_->is_point_behind_faces(point, unused);
}

// 0x00414990 (a failed allocation of the box crashes when there are geometries, as in the original)
HitMesh* build_dynamic_hit_mesh(int type, TriMeshSource* geometries, int count, int value)
{
    HitMesh* made;
    if (type == 1) {
        void* memory = memory::alloc(sizeof(HitMovingBox));
        HitMovingBox* box = memory ? static_cast<HitMovingBox*>(memory)->make_new() : nullptr;
        if (count >= 1) {
            maths::BBox bounds;
            for (int i = 0; i < count; ++i) {
                bounds.append_spot(geometries[i].lower_limit_);
                bounds.append_spot(geometries[i].upper_limit_);
            }
            box->box_ = bounds;
        }
        made = box;
    } else if (type == 4) {
        if (count == 1) {
            void* memory = memory::alloc(sizeof(HitMovingTriangles));
            made = memory ? static_cast<HitMovingTriangles*>(memory)->make_new(geometries) : nullptr;
        } else if (count > 1) {
            void* memory = memory::alloc(sizeof(HitMeshGroup));
            HitMeshGroup* multi = memory ? static_cast<HitMeshGroup*>(memory)->make_new() : nullptr;
            for (int i = 0; i < count; ++i) {
                void* part = memory::alloc(sizeof(HitMovingTriangles));
                multi->attach_child(part ? static_cast<HitMovingTriangles*>(part)->make_new(&geometries[i]) : nullptr);
            }
            made = multi;
        } else {
            return nullptr;
        }
    } else {
        return nullptr;
    }
    if (made) {
        made->get_call<void>(11, value);
        register_movable(made); // (the original also pushes `value`, which register_movable doesn't use)
    }
    return made;
}

} // namespace render

namespace render {

// 0x00640860 (a static initialiser): the shared orientation starts as the rotation of no angles.
void construct_shared_orientation() { g_shared_orientation.set_from_euler_angles(0.0f, 0.0f, 0.0f); }

} // namespace render

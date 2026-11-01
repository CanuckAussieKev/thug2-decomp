#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/render/coll_movable.h"
#include "engine/render/coll_obj.h"

namespace render {

struct CollisionHit;
struct CollRect;
struct RectHits;
struct CollSphere;
struct SphereHits;
struct CapsuleHits;

// The triangle collision objects: a level sector's (in world space), an instance of one (scaled and placed) and a
// movable one. Their faces come from the geometry: face flags (u16 at face +0) and terrain (u16 at +2), faces of
// 8 bytes when the geometry's "small faces" byte (+0x0A) is set, else 10.

// A level sector's collision (0x18 bytes, the original's vtable 0x00645C18): the geometry in world space; +0x10 its
// "face-rejection optimization" buffer (as many bytes as the geometry has faces), copied by Clone. A scene's are an
// array new (0x0049CE00 the vector deleting destructor).
class StaticCollider final : public HitMesh {
public:
    StaticCollider(const StaticCollider&) = delete;
    StaticCollider& operator=(const StaticCollider&) = delete;
    // 0x00412730 (as `new StaticCollider[n]` builds each): no flags, geometry or buffer, +0x14 0 (the checksum and
    // the grid mark left as they were).
    StaticCollider() = default;
    // 0x00412760: the buffer freed if this object owns it, the base.
    ~StaticCollider() override;

    [[nodiscard]] HitMeshKind kind() const override { return HitMeshKind::kStatic; }
    // (slot 1: the shared "true")
    bool is_triangle_hit() override { return true; }
    // (slots 2, 3)
    [[nodiscard]] uint32_t get_triangle_attrs(int face) const override { return read_face_attrs(face); }
    [[nodiscard]] uint16_t get_face_terrain(int face) const override { return read_face_terrain(face); }
    // 0x004139A0 (slot 12; not a function in Ghidra): an instance (a CollStaticInstance sharing the geometry) or
    // a copy (with a copy of the geometry - 0x00416BF0 - and of the buffer, on the bottom-up heap), by the low byte
    // of `instance`. Null if the geometry couldn't be copied.
    HitMesh* replicate(int instance) override;
    // 0x00410C20 (slot 5): the geometry moved by the difference (0x00416400).
    void place_at_absolute_point(const maths::Vec4* position) override;
    // (slot 6: the shared "ignore one argument")
    void change_facing(const maths::Mat4* /*orientation*/) override {}
    // 0x004123A0 / 0x004123B0 (slots 7, 8): the shared origin (0x00691914) and orientation (0x00691928).
    const maths::Vec4* get_absolute_point() override;
    const maths::Mat4* fetch_facing() override;
    // 0x00410980 (slot 9): the vertex, w = 1.
    [[nodiscard]] maths::Vec4 read_vertex_point(int index) const override;
    // 0x00413B50 (slot 14): the line against each face of the geometry in the line's box (get_faces_in_box) that
    // isn't rejected - its rejection byte (the buffer's for 10-byte faces, the face's last byte for 8-byte ones)
    // below the line's distance from the geometry's minimum corner times the rejection scale (none at all past
    // 255) -, each hit offered to `hit` (report_hit). Whether any was taken.
    bool collide_with_line(const maths::Seg* line, const maths::Vec4* direction, CollisionHit* hit,
                           const maths::BBox* line_box) override;
    // 0x00410C70 (slot 13) / 0x00412750 (slot 21): the geometry's box.
    bool inside_bounds(const maths::BBox* box) override;
    const maths::BBox* get_world_bounds() override;
    // 0x00410C90 (slot 15): the rect against each face of the geometry in `box` (not rejected by its flags): each
    // crossing (rect_hits_triangle) a hit - its ends' triangle edges, the face's normal (when it has one), the
    // checksum and flag 0x40. True as soon as the list is full and another is found; else whether it has any.
    bool collide_with_rect(const CollRect* rect, const maths::BBox* box, RectHits* hits) override;
    // 0x004110B0 (slot 16, an instance's too: its faces are taken unplaced): the sphere (moving when `hits` has a
    // sweep) against each face in `box` not rejected by its flags (sphere_hits_triangle / swept_sphere_hits_triangle):
    // each touch a hit (with its time when moving). True as soon as one is found with the list full or in mode 2.
    bool collide_with_sphere(const CollSphere* sphere, const maths::BBox* box, SphereHits* hits) override
    {
        return collide_faces_with_sphere(sphere, box, hits);
    }
    // 0x004114E0 (slot 17, an instance's too): the standing capsule against each face in `box` not rejected by its
    // flags (capsule_hits_triangle): each touch a hit. Nothing when `hits` has a sweep. True as soon as one is found
    // with the list full or in mode 2; else whether the list has any.
    bool collide_with_capsule(const maths::Capsule* capsule, const maths::BBox* box, CapsuleHits* hits) override
    {
        return collide_faces_with_capsule(capsule, box, hits);
    }
    // 0x0040AEA0 (slot 19): the geometry's debug rendering (0x0048DCD0, compiled out: the arguments ignored).
    void draw_diagnostic(int /*a*/, int /*b*/) override {}
    // 0x0040AE80 / 0x0040AE90 (slots 22, 23): the geometry turned / scaled.
    void rotate_y(const maths::Vec4* centre, int quarter_turns) override;
    void scale_about(const maths::Vec4* centre, const maths::Vec4* scale) override;
    // 0x00410BC0: the face-rejection buffer taken from `block` (8-byte faces keep theirs: nothing taken) and the
    // geometry's rejection scale kept (+0x14). The block's next free byte.
    uint8_t* take_buffer(uint8_t* block, const TriMeshSource* geometry);

    uint8_t* octets_{nullptr}; // +0x10
    int unknown14_{0};         // +0x14
};
static_assert(sizeof(StaticCollider) == 0x18, "layout must match the original");

// An instance of a sector's collision (0xC8 bytes, the original's vtable 0x00645C78): scaled, rotated and placed;
// its world box.
class CollStaticInstance final : public CollScaled {
public:
    // 0x004127C0
    CollStaticInstance() = default;
    // 0x00414C30: a copy.
    CollStaticInstance(const CollStaticInstance& other) = default;
    CollStaticInstance& operator=(const CollStaticInstance&) = delete;
    // 0x00412840 (0x00413ED0 the deleting destructor)
    ~CollStaticInstance() override;

    [[nodiscard]] HitMeshKind kind() const override { return HitMeshKind::kStaticInstance; }
    // (slot 1: the shared "true")
    bool is_triangle_hit() override { return true; }
    // (slots 2, 3)
    [[nodiscard]] uint32_t get_triangle_attrs(int face) const override { return read_face_attrs(face); }
    [[nodiscard]] uint16_t get_face_terrain(int face) const override { return read_face_terrain(face); }
    // 0x00414CD0 (slot 12): a copy sharing the geometry.
    CollStaticInstance* replicate(int argument) override;
    // 0x00411480: the geometry shared (flags 0xC000) and its box taken.
    void set_shared_geometry(TriMeshSource* geometry);
    // 0x00413F00 / 0x00413F30 (not a function in Ghidra) / 0x00413F50 (slots 5, 6, 24): set, the inverse and the
    // world box updated.
    void place_at_absolute_point(const maths::Vec4* position) override;
    void change_facing(const maths::Mat4* orientation) override;
    void change_scale(const maths::Vec4* scale) override;
    // 0x004113B0: the world box from every vertex scaled and placed.
    void recompute_world_bounds();
    // 0x00413FF0 (slot 14): as the sector's, in the instance's space (no rejection); the point (scaled, placed)
    // and normal (turned) back in world space.
    bool collide_with_line(const maths::Seg* line, const maths::Vec4* direction, CollisionHit* hit,
                           const maths::BBox* line_box) override;
    // 0x00411460 (slot 13) / 0x00412830 (slot 21)
    bool inside_bounds(const maths::BBox* box) override;
    maths::BBox* get_world_bounds() override;
    // 0x004128B0 (slot 15): as the sector's, with the rect in the instance's space (its box from the rect's
    // corners; `box` unused); each hit's points scaled and placed, its normals turned back into world space.
    bool collide_with_rect(const CollRect* rect, const maths::BBox* box, RectHits* hits) override;
    // (slots 16, 17: the sector's bodies - the faces taken unplaced)
    bool collide_with_sphere(const CollSphere* sphere, const maths::BBox* box, SphereHits* hits) override
    {
        return collide_faces_with_sphere(sphere, box, hits);
    }
    bool collide_with_capsule(const maths::Capsule* capsule, const maths::BBox* box, CapsuleHits* hits) override
    {
        return collide_faces_with_capsule(capsule, box, hits);
    }
    // 0x00411330 (slot 23): rescaled by `scale` times the current scale, if `position` is where it is already (the
    // instance isn't turned: slot 22 is the shared "ignore two arguments").
    void scale_about(const maths::Vec4* position, const maths::Vec4* scale) override;
    // 0x00412F50 (slot 19): the geometry drawn with the scaled matrix (0x00414D60).
    void draw_diagnostic(int a, int b) override;

    maths::BBox world_box_; // +0xA0
    uint8_t* octets_{nullptr}; // +0xC0
    int unknownc4_{0};         // +0xC4
};
static_assert(sizeof(CollStaticInstance) == 0xC8 && offsetof(CollStaticInstance, world_box_) == 0xA0, "layout");

// A movable triangle object (0xD4 bytes, the original's vtable 0x00645CE0): its bounding sphere's radius and the box
// around it.
class HitMovingTriangles final : public HitMovingMesh {
public:
    // 0x00413000
    explicit HitMovingTriangles(TriMeshSource* geometry);
    // 0x00413080: a copy.
    HitMovingTriangles(const HitMovingTriangles& other);
    HitMovingTriangles& operator=(const HitMovingTriangles&) = delete;
    // 0x00413070 (0x004143D0 the deleting destructor)
    ~HitMovingTriangles() override = default;

    [[nodiscard]] HitMeshKind kind() const override { return HitMeshKind::kMovingTriangles; }
    // (slot 1: the shared "true")
    bool is_triangle_hit() override { return true; }
    // (slots 2, 3)
    [[nodiscard]] uint32_t get_triangle_attrs(int face) const override { return read_face_attrs(face); }
    [[nodiscard]] uint16_t get_face_terrain(int face) const override { return read_face_terrain(face); }
    // 0x00414400 (slot 12): a copy with a copy of the geometry.
    HitMovingTriangles* replicate(int argument) override;
    // 0x00411780 (slot 4): the geometry and the radius from its vertices.
    void assign_shape_data(TriMeshSource* geometry) override;
    // 0x00412F90 (slot 5; not a function in Ghidra)
    void place_at_absolute_point(const maths::Vec4* position) override;
    // 0x00414460 (slot 14): as the sector's, in the object's space (rejection only for 8-byte faces); the point
    // and normal turned and placed back.
    bool collide_with_line(const maths::Seg* line, const maths::Vec4* direction, CollisionHit* hit,
                           const maths::BBox* line_box) override;
    // 0x00411810 (slot 13) / 0x00412FC0 (slot 20): the box (made again if the object moved).
    bool inside_bounds(const maths::BBox* box) override;
    maths::BBox* get_local_bounds() override;
    // 0x0040B390: the box around the sphere.
    void update_b_box();
    // 0x00413130 (slot 19)
    void draw_diagnostic(int a, int b) override;

    float radius_{}; // +0xA4
    bool box_dirty_; // +0xA8
    uint8_t pada9_[3]{};
    maths::BBox box_;    // +0xAC
    uint32_t unknowncc_{}; // +0xCC
    uint32_t unknownd0_; // +0xD0 the geometry's +0x3C
};
static_assert(sizeof(HitMovingTriangles) == 0xD4 && offsetof(HitMovingTriangles, box_) == 0xAC,
              "layout must match the original");

// Whether `object` (a sector's collision object, or null) is a sector's own (NOT ORIGINAL: the level editor's check,
// which compared the vtable).
inline bool is_static_collider(const void* object)
{
    return object && static_cast<const HitMesh*>(object)->kind() == HitMeshKind::kStatic;
}

// 0x00414990 (cdecl): a movable collision object made from `count` geometries (consecutive in memory): type 1 a box
// around them all, type 4 a movable triangle object (several: a multi-object of them); null for other types or
// no geometry. `owner` is set (set_owner) and the object registered with it (0x004177B0).
HitMesh* build_dynamic_hit_mesh(int type, TriMeshSource* geometries, int count, int owner);

// 0x00414D60: the geometry's debug rendering (compiled out: returns).
struct CollGeometryStubs {
    void draw_diagnostic(const maths::Mat4* transform, int a, int b, int c);
};

} // namespace render

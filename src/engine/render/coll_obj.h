#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "formats/collision_format.h"
#include <gsl/pointers>

namespace objects {
class GameEntity;
}
namespace render {

// Values the collision queries share.
constexpr float kEmptyBoxExtent = 10000000.0F; // an empty box starts with min at +this and max at -this on each axis
constexpr float kQueryBoxMargin = 0.5F;        // a query's box is grown by this each way before it is matched
constexpr int kUnlistedFlags = 6;              // an object with any of these flag bits is left out of the queries
// A collision face's flags (the 16 bits each face stores) - the game's probes, the editor and the rider read them.
using FaceFlag = collision_format::FaceFlag;
constexpr FaceFlag kNoFaceFlags = FaceFlag::kNone; // no ignore / require mask: every face counts
// What a hit record keeps of the face's flags beside them: 0x40 when it is a trigger, else 0.
[[nodiscard]] constexpr uint8_t trigger_marker(FaceFlag flags) { return static_cast<uint8_t>(core::to_underlying(flags & FaceFlag::kTrigger)); }
// Masks of them a probe passes over (ignore) or looks for (require).
constexpr FaceFlag kDefaultIgnoredFaceFlags = FaceFlag::kNotCollidable; // a fresh probe's, the skater's, peds' and vehicles'
constexpr FaceFlag kCameraIgnoredFaceFlags = FaceFlag::kCameraPasses | FaceFlag::kNotCollidable; // the faces the cameras' probes pass through
constexpr FaceFlag kTriggerVolumeFaceFlags = FaceFlag::kTrigger | FaceFlag::kNotCollidable;      // a trigger or not collidable: a trigger volume's faces
constexpr std::size_t kFaceQueryBufferSize = 0x800; // the face indices one query lists

// The geometry a collision object's faces come from (render::TriMeshSource; its methods are rewritten later).
struct TriMeshSource;
struct CollisionHit;
struct CollRect;
struct RectHits;
struct CollSphere;
struct SphereHits;
struct CapsuleHits;

// What a collision object is (NOT ORIGINAL: the level editor told them apart by their vtables).
enum class HitMeshKind : uint8_t {
    kGroup,           // HitMeshGroup
    kMovingBox,       // HitMovingBox
    kMovingTriangles, // HitMovingTriangles
    kStatic,          // StaticCollider
    kStaticInstance,  // CollStaticInstance
};

constexpr std::size_t kHitMeshBytes = 0x10;

// The base of the collision objects (0x10 bytes; the original's vtable 0x00645A30). The virtual functions keep the
// original's slot order (slot 18, the same in every table, is the plain is_point_behind_faces; 24 is CollScaled's):
//   0 the destructor, 1 is_triangle_hit, 2 get_triangle_attrs(face), 3 get_face_terrain(face), 4 assign_shape_data,
//   5 place_at_absolute_point, 6 change_facing, 7 get_absolute_point, 8 fetch_facing, 9 read_vertex_point, 10 / 11
//   get_owner / set_owner, 12 replicate, 13 inside_bounds, 14-17 collide_with_line / rect / sphere / capsule,
//   19 draw_diagnostic, 20 get_local_bounds, 21 get_world_bounds, 22 rotate_y, 23 scale_about.
// Allocated from the current heap (the scene's static objects with an array new: the count before them).
class HitMesh : public memory::HeapObject {
public:
    using Flags = uint16_t;
    static constexpr int kOwnsGeometry = 0x8000; // the geometry is deleted with the object (unless kSharedGeometry)
    static constexpr int kSharedGeometry = 0x4000;

    static void* operator new[](std::size_t size) noexcept { return memory::alloc(static_cast<unsigned int>(size)); }
    static void operator delete[](void* memory) { memory::dealloc(memory); }

    HitMesh& operator=(const HitMesh&) = delete;
    // 0x00413390 (the body inlined in every destructor): the geometry deleted if it's owned and not shared.
    virtual ~HitMesh();

    // NOT ORIGINAL: what this object is.
    [[nodiscard]] virtual HitMeshKind kind() const = 0;
    // (slot 1) Whether its hits are triangles (the base's: no).
    virtual bool is_triangle_hit() { return false; }
    // (slots 2, 3) A face's flags and terrain.
    [[nodiscard]] virtual FaceFlag get_triangle_attrs(int face) const = 0;
    [[nodiscard]] virtual uint16_t get_face_terrain(int face) const = 0;
    // 0x0040B370 (slot 4): the geometry, and its checksum (the sector's name) as the object's.
    virtual void assign_shape_data(TriMeshSource* geometry);
    // (slots 5-8) Where it is and its orientation.
    virtual void place_at_absolute_point(const maths::Vec4* position) = 0;
    virtual void change_facing(const maths::Mat4* orientation) = 0;
    virtual const maths::Vec4* get_absolute_point() = 0;
    virtual const maths::Mat4* fetch_facing() = 0;
    // (slot 9) The geometry's vertex `index` in the world.
    [[nodiscard]] virtual maths::Vec4 read_vertex_point(int index) const = 0;
    // (slots 10, 11; the base's: 0 and nothing) The object it belongs to (a movable's: its GameEntity).
    [[nodiscard]] virtual objects::GameEntity* get_owner() const { return nullptr; }
    virtual void set_owner(objects::GameEntity* /*owner*/) {}
    // (slot 12) A copy: `argument` the original's (a sector's: an instance rather than a copy).
    virtual HitMesh* replicate(int argument) = 0;
    // (slot 13) Whether it may touch `box`.
    virtual bool inside_bounds(const maths::BBox* box) = 0;
    // (slot 14) The line against it: a hit offered to `hit` (report_hit). Whether any was taken.
    virtual bool collide_with_line(const maths::Seg* line, const maths::Vec4* direction, CollisionHit* hit,
                                   const maths::BBox* line_box) = 0;
    // (slots 15-17; the movables' and the multi-object's: nothing) A rect, a sphere, a standing capsule against it.
    virtual bool collide_with_rect(const CollRect* /*rect*/, const maths::BBox* /*box*/, RectHits* /*hits*/)
    {
        return false;
    }
    virtual bool collide_with_sphere(const CollSphere* /*sphere*/, const maths::BBox* /*box*/, SphereHits* /*hits*/)
    {
        return false;
    }
    virtual bool collide_with_capsule(const maths::Capsule* /*capsule*/, const maths::BBox* /*box*/,
                                      CapsuleHits* /*hits*/)
    {
        return false;
    }
    // (slot 19) The debug drawing (the arguments unused).
    virtual void draw_diagnostic(int a, int b) = 0;
    // (slot 20; the base's: null) Its box in its own space, if it has one.
    virtual const maths::BBox* get_local_bounds() { return nullptr; }
    // (slot 21, the sectors' and their instances': their box in the world - what the grid sees) NOT ORIGINAL base
    // body: the other kinds' tables end at slot 20.
    virtual const maths::BBox* get_world_bounds() { return nullptr; }
    // (slots 22, 23, the sectors' and their instances'; NOT ORIGINAL base bodies, as slot 21's) Turned by quarter
    // turns about y / scaled, about `centre`.
    virtual void rotate_y(const maths::Vec4* /*centre*/, int /*quarter_turns*/) {}
    virtual void scale_about(const maths::Vec4* /*centre*/, const maths::Vec4* /*scale*/) {}

    // 0x0040FEC0: the checksum, the geometry's too (if it has one).
    void set_checksum(uint32_t checksum);
    // 0x0040AEC0 (slot 18 of every collision class): TriMeshSource::is_point_behind_faces.
    bool is_point_behind_faces(const maths::Vec4* point, int unused);
    // 0x0040AEB0: the geometry's occlusion polygons (TriMeshSource::add_occlusion_polys).
    void add_occlusion_polys();

    // The fields other code reads and sets.
    [[nodiscard]] uint32_t checksum() const { return checksum_; }
    void assign_checksum(uint32_t value) { checksum_ = value; }
    uint16_t& flag_bits() { return flag_bits_; }
    [[nodiscard]] uint16_t flag_bits() const { return flag_bits_; }
    [[nodiscard]] uint8_t visit_stamp() const { return visit_stamp_; }
    void set_visit_stamp(uint8_t value) { visit_stamp_ = value; }
    TriMeshSource*& geometry() { return geometry_; }
    [[nodiscard]] TriMeshSource* geometry() const { return geometry_; }
    void set_geometry(TriMeshSource* value) { geometry_ = value; }

private:
    uint32_t checksum_{};   // +0x04
    uint16_t flag_bits_{0}; // +0x08
    uint8_t visit_stamp_{}; // +0x0A the grid queries' mark (0x00693998): already listed this query
    uint8_t pad0b_{};
    TriMeshSource* geometry_{nullptr}; // +0x0C

protected:
    // No flags or geometry (the checksum and the grid mark left as they were).
    HitMesh() = default;
    // The base's copy: checksum, flags, grid mark and geometry.
    HitMesh(const HitMesh& other)
        : checksum_(other.checksum_), flag_bits_(other.flag_bits_), visit_stamp_(other.visit_stamp_),
          geometry_(other.geometry_)
    {}

    // The triangle objects' slots 2 / 3 (sectors, instances, movable triangles): the face's flags / terrain from the
    // geometry (0x00410BF0 / 0x00411750).
    [[nodiscard]] FaceFlag read_face_attrs(int face) const;
    [[nodiscard]] uint16_t read_face_terrain(int face) const;
    // 0x004110B0 / 0x004114E0 (a sector's slots 16 / 17, an instance's too: the faces taken as they are, unplaced):
    // see StaticCollider::collide_with_sphere / collide_with_capsule.
    bool collide_faces_with_sphere(const CollSphere* sphere, const maths::BBox* box, SphereHits* hits);
    bool collide_faces_with_capsule(const maths::Capsule* capsule, const maths::BBox* box, CapsuleHits* hits);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(sizeof(HitMesh) == kHitMeshBytes && offsetof(HitMesh, checksum_) == 4, "layout must match the original");
    }
};

// (The lists of collision objects: lists::Elem<HitMesh>'s destructors 0x004131D0 / 0x00411880, the heads' constructor
// 0x00413180 and scalar deleting destructor 0x004131A0 - memory::ListNode's make_head / uncouple / destroy.)

constexpr std::size_t kHitMeshGroupBytes = 0x3C;
constexpr std::size_t kHitMeshGroupBoxOffset = 0x14;

// A collision object made of others (0x3C bytes, the original's vtable 0x00645AA8): its children in a list; most
// calls are passed to each; its box is theirs together (recomputed when a child moves).
class HitMeshGroup final : public HitMesh {
public:
    // 0x004133E0: no children.
    HitMeshGroup();
    // 0x00411D10: a copy (sharing the children's list).
    HitMeshGroup(const HitMeshGroup& other);
    HitMeshGroup& operator=(const HitMeshGroup&) = delete;
    // 0x00411B10 (0x00413460 the deleting destructor): the children deleted (each child and its node), the list, out
    // of the caches, the base.
    ~HitMeshGroup() override;

    [[nodiscard]] HitMeshKind kind() const override { return HitMeshKind::kGroup; }
    // 0x00413490 (slot 12): a copy owning its geometry flag, with a clone of each child.
    gsl::owner<HitMeshGroup*> replicate(int argument) override;
    // 0x00411BE0: `child` onto the end of the list.
    void attach_child(gsl::owner<HitMesh*> child);

    // 0x00411D90 (slot 1): whether every child is (stops asking at the first that isn't).
    bool is_triangle_hit() override;
    // (slot 2, the shared "return 0" stub) / 0x0040AE30 (slot 3): 0.
    [[nodiscard]] FaceFlag get_triangle_attrs(int /*face*/) const override { return FaceFlag::kNone; }
    [[nodiscard]] uint16_t get_face_terrain(int /*face*/) const override { return 0; }
    // 0x00411CC0 / 0x00411C30 (slots 4, 5): passed to each child; the box to be recomputed. 0x00411C80 (slot 6):
    // passed to each child.
    void assign_shape_data(TriMeshSource* geometry) override;
    void place_at_absolute_point(const maths::Vec4* position) override;
    void change_facing(const maths::Mat4* orientation) override;
    // 0x00411F60 / 0x00411F80 (slots 7, 8): the first child's (crashes with none, as in the original).
    const maths::Vec4* get_absolute_point() override;
    const maths::Mat4* fetch_facing() override;
    // 0x00410960 (slot 9): a zero vector.
    [[nodiscard]] maths::Vec4 read_vertex_point(int index) const override;
    // 0x0040AE20 / 0x0040AE10 (slots 10, 11)
    [[nodiscard]] objects::GameEntity* get_owner() const override { return owner_; }
    void set_owner(objects::GameEntity* owner) override { owner_ = owner; }
    // 0x00411E80 (slot 13): false only if the box is known and doesn't touch `box`.
    bool inside_bounds(const maths::BBox* box) override;
    // 0x00411EC0 (slot 14): each child asked; whether any hit.
    bool collide_with_line(const maths::Seg* line, const maths::Vec4* direction, CollisionHit* hit,
                           const maths::BBox* line_box) override;
    // 0x00411F20 (slot 19): passed to each child.
    void draw_diagnostic(int a, int b) override;
    // 0x00411E50 (slot 20): the box (recomputed if a child moved; null if a child has none).
    maths::BBox* get_local_bounds() override;
    // 0x00411DD0: the box made from the children's.
    void recompute_b_box();

    // The fields other code reads and sets.
    [[nodiscard]] memory::ListNode* sub_nodes() const { return sub_nodes_; }
    void set_dirty(bool value) { dirty_ = value; }

private:
    memory::ListNode* sub_nodes_; // +0x10 the head (lists::Foremost<HitMesh>)
    maths::BBox box_;             // +0x14
    bool dirty_{true};            // +0x34
    bool box_valid_{};            // +0x35
    std::array<uint8_t, 2> pad36{};
    objects::GameEntity* owner_{}; // +0x38
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(sizeof(HitMeshGroup) == kHitMeshGroupBytes && offsetof(HitMeshGroup, box_) == kHitMeshGroupBoxOffset, "layout must match the original");
    }
};

} // namespace render

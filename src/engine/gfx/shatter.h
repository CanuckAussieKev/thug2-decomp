#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "engine/memory/mem.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"

#include "base/lists/object_set.h"

// Shattering on PC (Nx engine; the Shatter script command): a mesh broken into triangles that fly apart - each
// shatter (0x40 bytes) keeps its triangles' vertices and their positions, and the shatters are in a table
// (0x007004B8: lists::ObjectSet).
namespace render {

constexpr float kDefaultGravity = 128.0F;

struct MeshRef;
struct MaterialRef;
class PcGeom;

constexpr std::size_t kShatterRotationsOffset = 0x10;
constexpr std::size_t kShatterTriangleCountOffset = 0x24;
constexpr std::size_t kShatterBytes = 0x2C;

// The engine's shatter (0x2C bytes, vtable 0x00649308; PcShatter extends it): per triangle its centre, velocity
// and turn a step; the settings the Shatter command gave when it was made (engine_effects.h, change_breakup_settings).
class Shatter : public memory::HeapObject {
public:
    // 0x004912C0: for `count` triangles (their arrays from the default heap), with the settings.
    explicit Shatter(int count);
    // 0x00490E80 (0x00491380 the scalar deleting destructor, vtable slot 0 - its only one): the arrays freed.
    virtual ~Shatter();
    Shatter(const Shatter&) = delete;
    Shatter& operator=(const Shatter&) = delete;

    // The fields other code reads and sets.
    [[nodiscard]] float lifetime() const { return lifetime_; }
    void set_lifetime(float value) { lifetime_ = value; }
    [[nodiscard]] uint16_t triangle_count() const { return triangle_count_; }

private:
    maths::Vec4* centres;    // +0x04
    maths::Vec4* velocities; // +0x08
    uint32_t unknown0c_{};
    maths::Mat4* rotations;   // +0x10
    float lifetime_;           // +0x14: seconds left
    float gravity{kDefaultGravity};    // +0x18 (128)
    float floor;              // +0x1C: the height the triangles bounce at
    float bounce;             // +0x20: the speed kept in a bounce
    uint16_t triangle_count_; // +0x24
    uint16_t pad26{};
    uint32_t unknown28; // +0x28 (the settings' checksum 0x007004C0)

public:

    // A shatter's step: which piece and how far.
    struct ShatterStep {
        int index{};
        float step{};
    };

    // 0x004913D0: triangle `index` moved a step; below the floor and falling, it bounces (the height mirrored, the
    // speed turned up and scaled) and gets a new random turn (about a random axis, up to 0.1); then gravity.
    void get_step(ShatterStep shatter_step) const;

    // The per-triangle arrays as runs of `triangle_count_`.
    [[nodiscard]] std::span<maths::Vec4> centre_run() const { return {centres, triangle_count_}; }
    [[nodiscard]] std::span<maths::Vec4> velocity_run() const { return {velocities, triangle_count_}; }
    [[nodiscard]] std::span<maths::Mat4> rotation_run() const { return {rotations, triangle_count_}; }
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(Shatter, rotations) == kShatterRotationsOffset && offsetof(Shatter, triangle_count_) == kShatterTriangleCountOffset &&
                          sizeof(Shatter) == kShatterBytes,
                      "layout must match the original");
    }
};

constexpr std::size_t kPcShatterPositionsOffset = 0x2C;
constexpr std::size_t kPcShatterVerticesOffset = 0x3C;
constexpr std::size_t kPcShatterBytes = 0x40;

// A shatter (0x40 bytes, vtable 0x0064A4AC; the engine's shatter - per triangle its centre, velocity and rotation, the
// settings it was made with - and the triangles' vertices).
class PcShatter final : public Shatter {
public:
    PcShatter(const PcShatter&) = delete;
    PcShatter& operator=(const PcShatter&) = delete;
    // 0x004B8120: for `count` triangles of `mesh`.
    PcShatter(int count, MeshRef* mesh, uint32_t own_material);
    // 0x004B8FA0 (0x004B9A30 the scalar deleting destructor): its vertices, material and positions freed, then the
    // engine's.
    ~PcShatter() override;

    // The fields other code reads and sets.
    [[nodiscard]] MeshRef* mesh() const { return mesh_; }
    [[nodiscard]] uint8_t* vertices() const { return vertices_; }

private:
    // The constructor's step after the vertices: the positions, and the material of its own when asked for.
    void make_positions_and_material(int count);

    float* positions{};      // +0x2C: three floats per triangle
    MaterialRef* material{}; // +0x30: a material of its own when asked for
    uint32_t own_material_; // +0x34
    MeshRef* mesh_;          // +0x38: what it was made from (its material, vertex size and format)
    uint8_t* vertices_;      // +0x3C: three per triangle
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(PcShatter, positions) == kPcShatterPositionsOffset && offsetof(PcShatter, vertices_) == kPcShatterVerticesOffset &&
                          sizeof(PcShatter) == kPcShatterBytes,
                      "layout must match the original");
    }
};

// 0x004B80B0: a shatter's triangles drawn with its mesh's material, shader and format (counted in the frame's
// draw counts).
void draw_shatter(PcShatter* shatter);
// 0x004B9550: every shatter taken out of the table and destroyed.
void kill_all_shatters();
// 0x004B95E0: a step of a shatter's flight: each triangle turned about its centre by its rotation (its normals too,
// when the mesh has them), the engine's step (0x004913D0: the centre moved) and the vertices put back about it.
void update_shatter(PcShatter* shatter, float step);
// 0x004BA7A0: a geom's meshes shattered - their triangles (split while big; at least three indices) flung from the
// shatter centre (0x007004D0 and its scale 0x0067DDF4: their velocities from it at a random speed of up to
// 0x007004CC more), each turning about a random axis by a random angle up to 0.1 a step - each mesh a shatter put in
// the table.
void shatter_geom(PcGeom* geom);

} // namespace render

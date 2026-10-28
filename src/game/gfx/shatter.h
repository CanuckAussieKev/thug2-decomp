#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

// Shattering on PC (Nx engine; the Shatter script command): a mesh broken into triangles that fly apart - each
// shatter (0x40 bytes) keeps its triangles' vertices and their positions, and the shatters are in a table
// (0x007004B8, Lst::HashTable).
namespace Nx {

struct sMesh;

// The engine's shatter (0x2C bytes, vtable 0x00649308; CPcShatter extends it): per triangle its centre, velocity
// and turn a step; the settings the Shatter command gave when it was made (engine_effects.h, SetShatterParams).
struct CShatter {
    void** vtable;
    Mth::Vector* centres;    // +0x04
    Mth::Vector* velocities; // +0x08
    uint32_t unknown0C;
    Mth::Matrix* rotations;  // +0x10
    float lifetime;          // +0x14: seconds left
    float gravity;           // +0x18 (128)
    float floor;             // +0x1C: the height the triangles bounce at
    float bounce;            // +0x20: the speed kept in a bounce
    uint16_t triangleCount;  // +0x24
    uint16_t pad26;
    uint32_t unknown28;      // +0x28 (the settings' checksum 0x007004C0)

    // 0x004912C0: construct for `count` triangles (their arrays from the default heap), with the settings. Returns
    // this.
    CShatter* Construct(int count);
    // 0x00490E80: the destructor's body (the arrays freed).
    void DestroyBody();
    // 0x00491380 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CShatter* Destroy(unsigned int flags);
    // 0x004913D0: triangle `index` moved a step; below the floor and falling, it bounces (the height mirrored, the
    // speed turned up and scaled) and gets a new random turn (about a random axis, up to 0.1); then gravity.
    void Step(int index, float step);
};
static_assert(offsetof(CShatter, rotations) == 0x10 && offsetof(CShatter, triangleCount) == 0x24 &&
                  sizeof(CShatter) == 0x2C,
              "layout must match the original");

// A shatter (vtable 0x0064A4AC; the engine's shatter - constructor 0x004912C0, destructor body 0x00490E80 - for its
// first 0x2C bytes: per triangle its centre, velocity and rotation, the settings it was made with).
struct CPcShatter {
    void** vtable;
    Mth::Vector* centres;    // +0x04
    Mth::Vector* velocities; // +0x08
    uint32_t unknown0C;
    Mth::Matrix* rotations;  // +0x10: each triangle's turn a step
    uint8_t settings14[0x10];
    uint16_t triangleCount;  // +0x24
    uint8_t unknown26[6];
    float* positions;   // +0x2C: three floats per triangle
    uint8_t* material;  // +0x30: a material of its own (0x110 bytes) when asked for
    uint32_t ownMaterial; // +0x34
    sMesh* mesh;        // +0x38: what it was made from (its material, vertex size and format)
    uint8_t* vertices;  // +0x3C: three per triangle

    // 0x004B8120: construct for `count` triangles of `mesh`. Returns this.
    CPcShatter* Construct(int count, sMesh* mesh, uint32_t ownMaterial);
    // 0x004B8FA0: the destructor's body.
    void DestroyBody();
    // 0x004B9A30 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcShatter* Destroy(unsigned int flags);
};
static_assert(offsetof(CPcShatter, positions) == 0x2C && offsetof(CPcShatter, vertices) == 0x3C &&
                  sizeof(CPcShatter) == 0x40,
              "layout must match the original");

// 0x004B80B0: a shatter's triangles drawn with its mesh's material, shader and format (counted in the frame's
// draw counts).
void __cdecl DrawShatter(CPcShatter* shatter);
// 0x004B9550: every shatter taken out of the table and destroyed.
void __cdecl KillAllShatters();
// 0x004B95E0: a step of a shatter's flight: each triangle turned about its centre by its rotation (its normals too,
// when the mesh has them), the engine's step (0x004913D0: the centre moved) and the vertices put back about it.
void __cdecl UpdateShatter(CPcShatter* shatter, float step);
// 0x004BA7A0: a geom's meshes shattered - their triangles (split while big; at least three indices) flung from the
// shatter centre (0x007004D0 and its scale 0x0067DDF4: their velocities from it at a random speed of up to
// 0x007004CC more), each turning about a random axis by a random angle up to 0.1 a step - each mesh a shatter put in
// the table.
void __cdecl ShatterGeom(uint8_t* geom);

} // namespace Nx

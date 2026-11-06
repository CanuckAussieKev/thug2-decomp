#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "base/core/layout_32bit.h"
#include "engine/memory/mem.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"

// The engine's shatter (Nx engine; the Shatter script command): a mesh broken into triangles that fly apart, each turning
// and falling - the part the engine ages and steps (engine_effects.cpp); the PC shatter over it keeps the triangles'
// vertices (shatter.h, the graphics backend's).
namespace render {

constexpr float kDefaultGravity = 128.0F;

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
        static_assert(core::layout_32bit(offsetof(Shatter, rotations) == kShatterRotationsOffset && offsetof(Shatter, triangle_count_) == kShatterTriangleCountOffset &&
                          sizeof(Shatter) == kShatterBytes),
                      "layout must match the original");
    }
};

} // namespace render

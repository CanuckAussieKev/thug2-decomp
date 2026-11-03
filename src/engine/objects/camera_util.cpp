// The cameras' shared helpers: see camera_util.h. From the disassembly of 0x004E4340-0x004E4857.

#include "engine/objects/camera_util.h"

#include <cmath>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {

// The collision feeler (render::ProbeRay): its line, the hit's fraction of it and point; what it hit held by a smart
// pointer (let go of by its destructor).
class Feeler {
public:
    Feeler() = default;
    ~Feeler() = default;
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    // 0x004170F0: faces with any of `skip` (or without all of `need`) are passed through.
    void set_excluded(uint32_t skip, uint32_t need)
    {
        as_feeler()->set_excluded({.ignore = static_cast<uint16_t>(skip), .require = static_cast<uint16_t>(need)});
    }
    // 0x004171D0
    void change_strand(const maths::Vec4& start, const maths::Vec4& end) { as_feeler()->change_strand(&start, &end); }
    // 0x004172F0
    bool get_hit_shape(int a, int b) { return as_feeler()->get_hit_shape(a != 0, b != 0) != 0; }
    // 0x00417710: the line set, then get_hit_shape.
    bool get_hit_shape(const maths::Vec4& start, const maths::Vec4& end, int a)
    {
        return as_feeler()->get_collision_along(&start, &end, a != 0) != 0;
    }
    [[nodiscard]] const maths::Vec4& kickoff() const { return ray_.line().start; }
    [[nodiscard]] const maths::Vec4& get_end() const { return ray_.line().end; }
    [[nodiscard]] float get_fraction() const { return ray_.reach(); }
    [[nodiscard]] const maths::Vec4& get_point() const { return ray_.contact_pos(); }

private:
    render::ProbeRay* as_feeler() { return &ray_; }
    render::ProbeRay ray_;
};

// 0x0042BBF0: x, y and z scaled to `length` (left alone at length 0).
void set_length(maths::Vec4& v, float length) { v.set_length(length); }

// 0x00402550's maths (each component in double precision; w 0).
maths::Vec4 perpendicular(const maths::Vec4& a, const maths::Vec4& b)
{
    maths::Vec4 out{};
    out.x = static_cast<float>(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y);
    out.y = static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z);
    out.z = static_cast<float>(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x);
    out.w = 0.0f;
    return out;
}

constexpr float kMinimumDistance = 13.899999618530273f; // 0x0064B2CC
constexpr float kSideProbe = 8.0f;

} // namespace

// 0x004E4340
double frame_blend(float rate, float seconds)
{
    const double scaled = static_cast<double>(seconds) * 60.0f * rate;
    return scaled / ((1.0f - static_cast<double>(rate)) + scaled);
}

// 0x004E4360
void avoid_camera_collisions(maths::Vec4* position, maths::Mat4* matrix, const maths::Vec4* target,
                             const maths::Vec4* focus, bool sideways, bool refocus)
{
    maths::Vec4& pos = *position;
    // The direction from the target, normalised (z unrounded until then); w's difference as it is.
    const float dw = pos.w - target->w;
    const double dz = static_cast<double>(pos.z) - target->z;
    const float dy = pos.y - target->y;
    const float dx = pos.x - target->x;
    float dir_x = dx, dir_y = dy, dir_z = static_cast<float>(dz);
    const double length = std::sqrt((static_cast<double>(dx) * dx + static_cast<double>(dy) * dy) + dz * dz);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        dir_x = static_cast<float>(dx * inverse);
        dir_y = static_cast<float>(dy * inverse);
        dir_z = static_cast<float>(dz * inverse);
    }
    Feeler feeler;
    feeler.set_excluded(0x90, 0);
    // From the target to 2 beyond the camera.
    maths::Vec4 beyond{};
    beyond.x = static_cast<float>(dir_x + dir_x) + pos.x;
    beyond.y = static_cast<float>(dir_y + dir_y) + pos.y;
    beyond.z = static_cast<float>(dir_z + dir_z) + pos.z;
    beyond.w = static_cast<float>(static_cast<double>(dw) + dw + pos.w);
    feeler.change_strand(*target, beyond);
    if (feeler.get_hit_shape(1, 0)) {
        const maths::Vec4& s = feeler.kickoff();
        const maths::Vec4& e = feeler.get_end();
        const double lx = static_cast<double>(e.x) - s.x;
        const double ly = static_cast<double>(e.y) - s.y;
        const double lz = static_cast<double>(e.z) - s.z;
        auto line_length = static_cast<float>(std::sqrt((lx * lx + ly * ly) + lz * lz));
        double fraction = feeler.get_fraction();
        if (fraction * line_length < kMinimumDistance)
            fraction = static_cast<double>(kMinimumDistance) / line_length;
        const double pull = fraction * line_length - 2.0f;
        auto pw = static_cast<float>(dw * pull);
        auto pz = static_cast<float>(dir_z * pull);
        auto py = static_cast<float>(dir_y * pull);
        auto px = static_cast<float>(pull * dir_x);
        pos.x = px + target->x;
        pos.y = py + target->y;
        pos.z = pz + target->z;
        pos.w = pw + target->w;
    }
    if (!sideways)
        return;
    // Off a wall 8 to the right, else to the left.
    const maths::Vec4& right = core::at(matrix->row, 0);
    maths::Vec4 probe = {.x = right.x, .y = 0.0f, .z = right.z, .w = 0.0f};
    set_length(probe, kSideProbe);
    probe.x += pos.x;
    probe.y += pos.y;
    probe.z += pos.z;
    probe.w += pos.w;
    bool hit = feeler.get_hit_shape(pos, probe, 1);
    if (hit) {
        const maths::Vec4& p = feeler.get_point();
        probe.x -= p.x;
        probe.y -= p.y;
        probe.z -= p.z;
        probe.w -= p.w;
        pos.x -= probe.x;
        pos.y -= probe.y;
        pos.z -= probe.z;
        pos.w -= probe.w;
    } else {
        probe = {.x = -right.x, .y = 0.0f, .z = -right.z, .w = 0.0f};
        set_length(probe, kSideProbe);
        probe.x += pos.x;
        probe.y += pos.y;
        probe.z += pos.z;
        probe.w += pos.w;
        hit = feeler.get_hit_shape(pos, probe, 1);
        if (hit) {
            const maths::Vec4& p = feeler.get_point();
            const double ox = static_cast<double>(probe.x) - p.x;
            const double oy = static_cast<double>(probe.y) - p.y;
            const float oz = probe.z - p.z;
            const float ow = probe.w - p.w;
            pos.x = static_cast<float>(pos.x - ox);
            pos.y = static_cast<float>(pos.y - oy);
            pos.z -= oz;
            pos.w -= ow;
        }
    }
    if (!refocus || !hit)
        return;
    // Look from the new position at the focus.
    maths::Vec4& at = core::at(matrix->row, 2);
    maths::Vec4& up = core::at(matrix->row, 1);
    maths::Vec4& across = core::at(matrix->row, 0);
    at = {.x = focus->x - pos.x, .y = focus->y - pos.y, .z = focus->z - pos.z, .w = 1.0f};
    at.make_unit();
    across = perpendicular(up, at);
    across.make_unit();
    up = perpendicular(at, across);
    up.make_unit();
    across = {.x = -across.x, .y = -across.y, .z = -across.z, .w = -across.w};
    at = {.x = -at.x, .y = -at.y, .z = -at.z, .w = -at.w};
    across.w = 0.0f;
    up.w = 0.0f;
    at.w = 0.0f;
    core::at(matrix->row, 3).w = 1.0f;
}

} // namespace objects

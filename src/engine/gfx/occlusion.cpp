// Occlusion polygons and view culling: see occlusion.h. The plane maths follows the original's x87 code: some
// intermediate values stay in double precision (in registers), the rest are rounded to floats where it stored them.

#include "base/core/within.h"
#include "engine/gfx/occlusion.h"
#include "platform/d3dx.h"

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <array>
#include <span>

#include "engine/gfx/render_camera.h"
#include "base/maths/vec4.h"
#include "platform/crt.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/view_setup.h"
#include "base/maths/mat4.h"

namespace render {

constexpr std::size_t kOccluderPolyCount = 512;
constexpr std::size_t kOccluderCount = 8;
constexpr int kFrustumPlanes = 5;

namespace {
// NOT ORIGINAL grouping - occlusion.cpp's state (the original's globals). Made on first use, never destroyed
// (occlusion_state()).
struct OcclusionState {
    std::array<OccluderPolygon, kOccluderPolyCount> occluder_quads_{};
    std::array<Occluder, kOccluderCount> occluders_{};
    uint32_t occlusion_poly_count_{};
    uint32_t occlusion_next_poly_{};
    int occlusion_frame_{};
    uint32_t occluder_count_{};
};

OcclusionState& occlusion_state()
{
    static core::NoDestroy<OcclusionState> state;
    return state.get();
}

} // namespace

namespace {

std::array<OccluderPolygon, kOccluderPolyCount>& polys() { return occlusion_state().occluder_quads_; }
uint32_t& poly_count() { return occlusion_state().occlusion_poly_count_; }
uint32_t& next_poly() { return occlusion_state().occlusion_next_poly_; } // where add_occluders carries on
int& tick_cycle() { return occlusion_state().occlusion_frame_; }
std::array<Occluder, kOccluderCount>& occluder_array() { return occlusion_state().occluders_; }
uint32_t& occluder_count() { return occlusion_state().occluder_count_; }
constexpr uint32_t kOccluderLimit = 8;

// The object's transform for culling (set_world_transform), or none (empty): only its position row is used here.
std::span<const float> world_transform()
{
    return renderer().current_world_transform() ? std::span<const float>(renderer().current_world_transform(), maths::kMatrixFloats) : std::span<const float>();
}
// The view matrix (0x00786BD0) and the view's clip values.
const void* view_matrix_data() { return renderer().view_matrix().data(); }
float near_plane_depth() { return renderer().render_camera().near_z_; }
float far_clip() { return renderer().render_camera().far_z_; }
// The frustum's side planes as (cos, sin) pairs: horizontal 0x00786E20 / 0x00786E28, vertical 0x00786E24 / 0x00786E2C.
float side_cos() { return renderer().view_shape().sin_x_; }
float vertical_cos() { return renderer().view_shape().sin_y_; }
float side_sin() { return renderer().view_shape().cos_x_; }
float vertical_sin() { return renderer().view_shape().cos_y_; }

// D3DXVec3Transform (linked library code; stdcall): (x, y, z, 1) x matrix.
void xform(float* out, const float* point, const void* matrix) { d3dx::vec3_transform(out, point, matrix); }

// The C runtime's qsort (the game's, so equal occluders end up in the same order).
void get_qsort(void* base, uint32_t count, uint32_t size, int (*compare)(const void*, const void*))
{
    crt::get_qsort(base, count, size, compare);
}

// How each of the original's (inlined) plane calculations rounds.
enum class LengthOrder : uint8_t { kXzy, kZyx }; // the order the squares are added in
enum class DotOrder : uint8_t { kZyx, kXyz };    // ... and the products of the plane's distance

// A plane through `point` with the normal (v x u) normalised, where u = u_end - origin (kept in double precision)
// and v = vEnd - origin (x and y rounded to floats, z kept).
void set_plane(std::array<float, 4>& plane, std::span<const float> origin, std::span<const float> u_end,
               std::span<const float> v_end, LengthOrder length_order, bool exact_inverse_for_x,
               std::span<const float> point, DotOrder dot_order)
{
    const double u0 = static_cast<double>(core::at(u_end, 0)) - core::at(origin, 0);
    const double u1 = static_cast<double>(core::at(u_end, 1)) - core::at(origin, 1);
    const double u2 = static_cast<double>(core::at(u_end, 2)) - core::at(origin, 2);
    auto v0 = static_cast<float>(static_cast<double>(core::at(v_end, 0)) - core::at(origin, 0));
    auto v1 = static_cast<float>(static_cast<double>(core::at(v_end, 1)) - core::at(origin, 1));
    const double v2 = static_cast<double>(core::at(v_end, 2)) - core::at(origin, 2);
    auto nx = static_cast<float>((v2 * u1) - (v1 * u2));
    auto ny = static_cast<float>((u2 * v0) - (v2 * u0));
    auto nz = static_cast<float>((v1 * u0) - (u1 * v0));
    const double xx = static_cast<double>(nx) * nx;
    const double yy = static_cast<double>(ny) * ny;
    const double zz = static_cast<double>(nz) * nz;
    const double length = std::sqrt(length_order == LengthOrder::kXzy ? (xx + zz) + yy : (zz + yy) + xx);
    double x = nx;
    float y = ny;
    float z = nz;
    if (length > 0.0) {
        const double inverse = 1.0 / static_cast<float>(length);
        auto inverse_float = static_cast<float>(inverse);
        x = (exact_inverse_for_x ? inverse : inverse_float) * static_cast<double>(nx);
        y = static_cast<float>(static_cast<double>(inverse_float) * ny);
        z = static_cast<float>(static_cast<double>(inverse_float) * nz);
    }
    const double distance =
        dot_order == DotOrder::kZyx
            ? ((static_cast<double>(z) * core::at(point, 2)) + (static_cast<double>(y) * core::at(point, 1))) +
                  (x * core::at(point, 0))
            : ((x * core::at(point, 0)) + (static_cast<double>(y) * core::at(point, 1))) +
                  (static_cast<double>(z) * core::at(point, 2));
    core::at(plane, 0) = static_cast<float>(x);
    core::at(plane, 1) = y;
    core::at(plane, 2) = z;
    core::at(plane, 3) = static_cast<float>(distance);
}

// The view tests shared by sphere_outside_view and sphere_in_view, on the centre in view space: in front of the near
// plane and inside the four side planes.
bool inside_frustum(const std::array<float, 4>& view, float radius)
{
    if (static_cast<double>(radius) - core::at(view, 2) < near_plane_depth()) {
        return false;
    }
    const double side_z = static_cast<double>(side_cos()) * core::at(view, 2);
    const double side_x = static_cast<double>(side_sin()) * core::at(view, 0);
    if (side_z - side_x > radius || radius < side_z + side_x) {
        return false;
    }
    const double vertical_z = static_cast<double>(vertical_cos()) * core::at(view, 2);
    const double vertical_y = static_cast<double>(vertical_sin()) * core::at(view, 1);
    return !(vertical_y + vertical_z > radius || radius < vertical_z - vertical_y);
}

} // namespace

// 0x004D8BB0
int compare_occluders(const void* a, const void* b)
{
    // (qsort hands out const pointers; the occluders are the array's own)
    Occluder* first = mutable_within(occluder_array().data(), static_cast<const Occluder*>(a));
    Occluder* second = mutable_within(occluder_array().data(), static_cast<const Occluder*>(b));
    if (first->poly->enabled == 0U) {
        core::at(first->hidden, 0) = 0;
    }
    if (second->poly->enabled == 0U) {
        core::at(second->hidden, 0) = 0;
    }
    const int first_hidden0 = core::at(first->hidden, 0);
    const int second_hidden0 = core::at(second->hidden, 0);
    if (first->poly->enabled == 0U) {
        core::at(first->hidden, 1) = 0;
    }
    if (second->poly->enabled == 0U) {
        core::at(second->hidden, 1) = 0;
    }
    const int first_sum = first_hidden0 + core::at(first->hidden, 1);
    const int second_sum = second_hidden0 + core::at(second->hidden, 1);
    if (first_sum < second_sum) {
        return 1;
    }
    return first_sum <= second_sum ? 0 : -1;
}

// 0x004D8C10
void sort_occluders()
{
    const uint32_t count = occluder_count();
    if (count == 0) {
        return;
    }
    get_qsort(occluder_array().data(), count, sizeof(Occluder), &compare_occluders);
    for (int i = static_cast<int>(occluder_count()) - 1; i >= 0; --i) {
        Occluder& occluder = core::at(occluder_array(), i);
        if (core::at(occluder.hidden, 1) + core::at(occluder.hidden, 0) == 0) {
            // Hid nothing: its polygon is free again (the list is sorted, so it's at the end).
            occluder.poly->in_use_ = 0;
            --occluder_count();
        } else {
            core::at(occluder.hidden, tick_cycle()) = 0;
        }
    }
}

// 0x004D8C90
void activate_occluder_poly(uint32_t checksum, bool enabled)
{
    for (uint32_t i = 0; i < poly_count(); ++i) {
        if (core::at(polys(), i).checksum == checksum) {
            core::at(polys(), i).enabled = static_cast<uint8_t>(enabled);
        }
    }
}

// 0x004D8CC0
void clear_occluder_polys()
{
    occluder_count() = 0;
    poly_count() = 0;
    next_poly() = 0;
}

// 0x004D8CE0
void add_occluders()
{
    const uint32_t count = poly_count();
    if (count == 0) {
        return;
    }
    uint32_t next = next_poly();
    uint32_t occluders = occluder_count();
    uint32_t added = 0;
    for (uint32_t looked = 0;;) {
        OccluderPolygon& poly = core::at(polys(), next++);
        if ((poly.in_use_ == 0U) && (poly.enabled != 0U)) {
            if (occluders < kOccluderLimit) {
                Occluder& occluder = core::at(occluder_array(), occluders++);
                occluder.poly = &poly;
                poly.in_use_ = 1;
                core::at(occluder.hidden, 0) = 0;
                core::at(occluder.hidden, 1) = 0;
            }
            ++added; // (counted even when there's no room)
        }
        ++looked;
        if (next >= count) {
            next = 0;
        }
        if (looked >= count || added >= 4) {
            break;
        }
    }
    occluder_count() = occluders;
    next_poly() = next;
}

// 0x004D8D70
void insert_occluder_poly(const maths::Vec4* p0, const maths::Vec4* p1, const maths::Vec4* p2, const maths::Vec4* p3,
                          uint32_t checksum)
{
    OccluderPolygon& poly = core::at(polys(), poly_count());
    poly.in_use_ = 0;
    poly.enabled = 1;
    poly.checksum = checksum;
    const std::array<const maths::Vec4*, 4> corners = {{p0, p1, p2, p3}};
    for (int i = 0; i < 4; ++i) {
        core::at(core::at(poly.corners, i), 0) = core::at(corners, i)->x;
        core::at(core::at(poly.corners, i), 1) = core::at(corners, i)->y;
        core::at(core::at(poly.corners, i), 2) = core::at(corners, i)->z;
        core::at(core::at(poly.corners, i), 3) = core::at(corners, i)->w;
    }
    // The normal: (p1 - p0) x (p3 - p0), the first component of p3 - p0 kept in double precision.
    const double a0 = static_cast<double>(p3->x) - p0->x;
    auto a1 = static_cast<float>(static_cast<double>(p3->y) - p0->y);
    auto a2 = static_cast<float>(static_cast<double>(p3->z) - p0->z);
    auto b0 = static_cast<float>(static_cast<double>(p1->x) - p0->x);
    auto b1 = static_cast<float>(static_cast<double>(p1->y) - p0->y);
    auto b2 = static_cast<float>(static_cast<double>(p1->z) - p0->z);
    core::at(poly.normal, 0) = static_cast<float>((static_cast<double>(b1) * a2) - (static_cast<double>(b2) * a1));
    core::at(poly.normal, 1) = static_cast<float>((b2 * a0) - (static_cast<double>(a2) * b0));
    core::at(poly.normal, 2) = static_cast<float>((static_cast<double>(b0) * a1) - (b1 * a0));
    core::at(poly.normal, 3) = 0.0F;
    maths::Vec4 normal = {
        .x = core::at(poly.normal, 0), .y = core::at(poly.normal, 1), .z = core::at(poly.normal, 2), .w = 0.0F};
    normal.make_unit();
    poly.normal = {normal.x, normal.y, normal.z, normal.w};
    ++poly_count();
}

// 0x004D8ED0
void collect_occluders(const maths::Vec4* eye, int frame)
{
    tick_cycle() = frame;
    sort_occluders();
    add_occluders();
    const std::span<const float> eye_position(eye->data(), 4);
    using L = LengthOrder;
    using D = DotOrder;
    for (uint32_t i = 0; i < occluder_count(); ++i) {
        Occluder& occluder = core::at(occluder_array(), i);
        const OccluderPolygon& poly = *occluder.poly;
        const std::span<const float> p0 = core::at(poly.corners, 0);
        const std::span<const float> p1 = core::at(poly.corners, 1);
        const std::span<const float> p2 = core::at(poly.corners, 2);
        const std::span<const float> p3 = core::at(poly.corners, 3);
        // Which side of the polygon the eye is on decides the corners' order.
        const double facing =
            (((static_cast<double>(core::at(eye_position, 2)) - core::at(p0, 2)) * core::at(poly.normal, 2)) +
             ((static_cast<double>(core::at(eye_position, 1)) - core::at(p0, 1)) * core::at(poly.normal, 1))) +
            ((static_cast<double>(core::at(eye_position, 0)) - core::at(p0, 0)) * core::at(poly.normal, 0));
        if (facing >= 0.0) {
            set_plane(core::at(occluder.planes, 0), p0, p2, p3, L::kXzy, false, p0, D::kZyx);
            set_plane(core::at(occluder.planes, 1), eye_position, p0, p1, L::kXzy, false, eye_position, D::kXyz);
            set_plane(core::at(occluder.planes, 2), eye_position, p2, p3, L::kXzy, false, eye_position, D::kXyz);
            set_plane(core::at(occluder.planes, 3), eye_position, p1, p2, L::kXzy, false, eye_position, D::kXyz);
            set_plane(core::at(occluder.planes, 4), eye_position, p3, p0, L::kZyx, false, eye_position, D::kXyz);
        } else { // (also when it's not a number)
            set_plane(core::at(occluder.planes, 0), p0, p3, p2, L::kXzy, false, p0, D::kZyx);
            set_plane(core::at(occluder.planes, 1), eye_position, p1, p0, L::kZyx, true, eye_position, D::kXyz);
            set_plane(core::at(occluder.planes, 2), eye_position, p3, p2, L::kZyx, false, eye_position, D::kXyz);
            set_plane(core::at(occluder.planes, 3), eye_position, p2, p1, L::kXzy, false, eye_position, D::kXyz);
            set_plane(core::at(occluder.planes, 4), eye_position, p0, p3, L::kXzy, false, eye_position, D::kXyz);
        }
    }
}

bool& occlusion_off()
{
    static bool off = false;
    return off;
}

// 0x004D9AC0
bool sphere_occluded(const maths::Vec4* centre, SphereTest sphere_test)
{
    const auto radius = sphere_test.radius;
    const auto weight = sphere_test.weight;
    if (occlusion_off()) { // (NOT ORIGINAL: the level editor's switch)
        return false;
    }
    double x = centre->x;
    double y = centre->y;
    double z = centre->z;
    if (const auto world = world_transform(); !world.empty()) {
        x = static_cast<double>(core::at(world, maths::kMatTx)) + centre->x;
        y = static_cast<double>(core::at(world, maths::kMatTy)) + centre->y;
        z = static_cast<double>(core::at(world, maths::kMatTz)) + centre->z;
    }
    const float limit = -radius;
    for (uint32_t i = 0; i < occluder_count(); ++i) {
        Occluder& occluder = core::at(occluder_array(), i);
        int plane = 0;
        for (; plane < kFrustumPlanes; ++plane) {
            const std::array<float, 4>& plane_coefficients = core::at(occluder.planes, plane);
            const double distance = (((z * core::at(plane_coefficients, 2)) + (x * core::at(plane_coefficients, 0))) +
                                     (y * core::at(plane_coefficients, 1))) -
                                    core::at(plane_coefficients, 3);
            if (distance >= limit) { // (not a number counts as behind)
                break;
            }
        }
        if (plane == kFrustumPlanes) {
            core::at(occluder.hidden, tick_cycle()) += weight;
            return true;
        }
    }
    return false;
}

// 0x004DB570
int sphere_outside_view(const maths::Vec4* centre, float radius)
{
    std::array<float, 4> view{};
    xform(view.data(), centre->data(), view_matrix_data());
    return inside_frustum(view, radius) ? 0 : 1;
}

// 0x004DB790
bool sphere_in_view(const maths::Vec4* centre, float radius)
{
    std::array<float, 4> moved{};
    const float* point = centre->data();
    if (const auto world = world_transform(); !world.empty()) {
        core::at(moved, 0) = core::at(world, maths::kMatTx) + centre->x;
        core::at(moved, 1) = core::at(world, maths::kMatTy) + centre->y;
        core::at(moved, 2) = core::at(world, maths::kMatTz) + centre->z;
        point = moved.data();
    }
    std::array<float, 4> view{};
    xform(view.data(), point, view_matrix_data());
    view_setup().set_last_sphere_distance(static_cast<float>(-static_cast<double>(core::at(view, 2)) - radius));
    return inside_frustum(view, radius);
}

// 0x004DB6D0
bool beyond_clip_distance(const void* bounds)
{
    if (!view_setup().distance_clipping()) {
        return false;
    }
    const std::span<const float, 5> sphere(static_cast<const float*>(bounds), 5);
    const float radius = core::at(sphere, 4);
    std::array<float, 3> centre = {{core::at(sphere, 1), core::at(sphere, 2), core::at(sphere, 3)}};
    std::array<float, 3> moved{};
    const float* point = centre.data();
    if (const auto world = world_transform(); !world.empty()) {
        core::at(moved, 0) = core::at(centre, 0) + core::at(world, maths::kMatTx);
        core::at(moved, 1) = core::at(centre, 1) + core::at(world, maths::kMatTy);
        core::at(moved, 2) = core::at(centre, 2) + core::at(world, maths::kMatTz);
        point = moved.data();
    }
    std::array<float, 4> view{};
    xform(view.data(), point, view_matrix_data());
    const double distance = std::sqrt(((static_cast<double>(core::at(view, 2)) * core::at(view, 2)) +
                                       (static_cast<double>(core::at(view, 1)) * core::at(view, 1))) +
                                      (static_cast<double>(core::at(view, 0)) * core::at(view, 0)));
    return distance - radius > far_clip();
}

} // namespace render

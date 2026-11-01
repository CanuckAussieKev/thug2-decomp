// Test views: teleport standing, the camera lock (frontend/test_view.h). NOT ORIGINAL.

#include "frontend/test_view.h"

#include <cmath>
#include <cstdint>
#include <array>

#include "engine/gfx/view_cam.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "engine/sys/mlp.h"
#include "platform/run_log.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace frontend::test_view {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kProbeAbove = 12.0f;   // the ground ray starts this far above the point...
constexpr float kProbeBelow = 4000.0f; // ...and ends this far below it

Vec3 perpendicular(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len_of(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 scale_tool(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }

struct Lock {
    bool on;
    Vec3 pos, right, up, back, drift;
    uint32_t frame; // (the game frame it was locked at)
} g_lock;

maths::Vec4 get_row(Vec3 v, float w) { return {v.x, v.y, v.z, w}; }

// The local skater (the skate module's 0x00566520; null with none).
objects::Rider* local_skater()
{
    auto* skate = modules::g_skate_module;
    return skate ? skate->get_home_rider() : nullptr;
}

// The game's collision ray (render::ProbeRay: static collision, the nearest hit) from `from` to `to`.
bool ground_below(Vec3 point, float* y)
{
    // (the game's memory layout, as level_editor_carry.cpp's first_hit: static collision only, nothing to release)
    render::ProbeRay ray_storage;
    render::ProbeRay* feeler = &ray_storage;
    const maths::Vec4 start = {point.x, point.y + kProbeAbove, point.z, 1.0f};
    const maths::Vec4 end = {point.x, point.y - kProbeBelow, point.z, 1.0f};
    if (!feeler->get_collision_along(&start, &end, false))
        return false;
    *y = feeler->contact_pos_.y;
    return true;
}

// TeleportSkaterStanding pos=(x, y, z) [heading=<degrees>]
bool cfunc_teleport_skater_standing(script::Struct* params, script::Script*)
{
    script::VectorValue pos = {};
    if (!params || !params->fetch_vec("pos", &pos, 0)) {
        get_log("TeleportSkaterStanding: pos=(x, y, z) needed");
        return false;
    }
    float heading = 0.0f;
    const bool has_heading = params->fetch_float("heading", &heading, 0);
    Vec3 at = {};
    const std::string why = jump_to({pos.x, pos.y, pos.z}, has_heading, heading, &at);
    if (!why.empty()) {
        get_log("TeleportSkaterStanding: %s", why.c_str());
        return false;
    }
    return true;
}

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

bool camera_rows(Vec3 direction, Vec3* right, Vec3* up, Vec3* back)
{
    const float length = len_of(direction);
    if (!(length > 1e-6f))
        return false;
    const Vec3 b = scale_tool(direction, -1.0f / length);
    Vec3 r = perpendicular({0.0f, 1.0f, 0.0f}, b);
    const float rl = len_of(r);
    if (!(rl > 1e-4f))
        return false;
    r = scale_tool(r, 1.0f / rl);
    Vec3 u = perpendicular(b, r);
    u = scale_tool(u, 1.0f / len_of(u));
    *right = r;
    *up = u;
    *back = b;
    return true;
}

void standing_rows(float heading_degrees, Vec3* right, Vec3* up, Vec3* at)
{
    const double h = static_cast<double>(heading_degrees) * kPi / 180.0;
    auto s = static_cast<float>(std::sin(h)), c = static_cast<float>(std::cos(h));
    *at = {s, 0.0f, c};
    *up = {0.0f, 1.0f, 0.0f};
    *right = perpendicular(*up, *at); // (cos, 0, -sin): heading 0 gives the identity
}

// --- the game ---------------------------------------------------------------------------------------------------

std::string jump_to(Vec3 position, bool has_heading, float heading_degrees, Vec3* out_position, bool on_board)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return "no local skater";
    if (!has_heading) {
        const maths::Vec4& at = core::at(skater->basis_frame_.row, 2);
        heading_degrees =
            static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)) * 180.0 / kPi);
    }
    float ground = 0.0f;
    const bool grounded = ground_below(position, &ground);
    if (grounded)
        position.y = ground;
    Vec3 right{}, up{}, at{};
    standing_rows(heading_degrees, &right, &up, &at);
    // (the skater's own custom restart kept and put back: only the restart's code is borrowed)
    const maths::Vec4 saved_pos = skater->custom_restart_pos_;
    const maths::Mat4 saved_matrix = skater->custom_restart_matrix_;
    const bool saved_walking = skater->custom_restart_walking_;
    skater->custom_restart_pos_ = get_row(position, 1.0f);
    core::at(skater->custom_restart_matrix_.row, 0) = get_row(right, 0.0f);
    core::at(skater->custom_restart_matrix_.row, 1) = get_row(up, 0.0f);
    core::at(skater->custom_restart_matrix_.row, 2) = get_row(at, 0.0f);
    core::at(skater->custom_restart_matrix_.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    skater->custom_restart_walking_ = !on_board;
    skater->skip_to_custom_restart();
    skater->custom_restart_pos_ = saved_pos;
    skater->custom_restart_matrix_ = saved_matrix;
    skater->custom_restart_walking_ = saved_walking;
    get_log("test view: skater teleported %s to (%.1f, %.1f, %.1f) heading %.1f degrees%s",
            on_board ? "on its board" : "standing", position.x, position.y, position.z, heading_degrees,
            grounded ? " (on the ground below)" : " (no ground below: as given)");
    if (out_position)
        *out_position = position;
    return {};
}

bool lock_camera(Vec3 position, Vec3 direction, Vec3 drift)
{
    Vec3 right{}, up{}, back{};
    if (!camera_rows(direction, &right, &up, &back))
        return false;
    g_lock = {true, position, right, up, back, drift, frame_loop::frames_run()};
    get_log("test view: camera locked at (%.1f, %.1f, %.1f) looking along (%.3f, %.3f, %.3f), drifting (%.3f, %.3f, "
            "%.3f) a "
            "frame",
            position.x, position.y, position.z, -back.x, -back.y, -back.z, drift.x, drift.y, drift.z);
    return true;
}

void unlock_camera()
{
    if (g_lock.on)
        get_log("test view: camera unlocked");
    g_lock.on = false;
}

bool camera_locked() { return g_lock.on; }

void place_camera(gfx::ViewCam* camera, int viewport)
{
    if (!g_lock.on || !camera || viewport != 0)
        return;
    auto frames = static_cast<float>(frame_loop::frames_run() - g_lock.frame);
    camera->at_point_ = get_row({g_lock.pos.x + g_lock.drift.x * frames, g_lock.pos.y + g_lock.drift.y * frames,
                                 g_lock.pos.z + g_lock.drift.z * frames},
                                1.0f);
    core::at(camera->basis_frame_.row, 0) = get_row(g_lock.right, 0.0f);
    core::at(camera->basis_frame_.row, 1) = get_row(g_lock.up, 0.0f);
    core::at(camera->basis_frame_.row, 2) = get_row(g_lock.back, 0.0f);
}

void register_script_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {"TeleportSkaterStanding", &cfunc_teleport_skater_standing},
    });
    script::install_script_functions(kFunctions.data(), sizeof(kFunctions) / sizeof(core::at(kFunctions, 0)));
}

} // namespace frontend::test_view

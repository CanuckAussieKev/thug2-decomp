// Test views: teleport standing, the camera lock (frontend/test_view.h). NOT ORIGINAL.

#include "frontend/test_view.h"

#include <cmath>
#include <cstdint>
#include <array>
#include <string>
#include <optional>
#include <numbers>

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
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"

namespace frontend::test_view {

namespace {

constexpr float kMinDirectionLength = 1e-6f;
constexpr float kMinRightLength = 1e-4f;
constexpr double kDegreesPerHalfTurn = 180.0;

constexpr float kProbeAbove = 12.0f;   // the ground ray starts this far above the point...
constexpr float kProbeBelow = 4000.0f; // ...and ends this far below it

Vec3 perpendicular(Vec3 a, Vec3 b)
{
    return {.x = (a.y * b.z) - (a.z * b.y), .y = (a.z * b.x) - (a.x * b.z), .z = (a.x * b.y) - (a.y * b.x)};
}
float len_of(Vec3 v) { return std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z)); }
Vec3 scale_tool(Vec3 v, float s) { return {.x = v.x * s, .y = v.y * s, .z = v.z * s}; }

struct Lock {
    bool on;
    Vec3 pos, right, up, back, drift;
    uint32_t frame; // (the game frame it was locked at)
};

Lock& lock()
{
    static core::NoDestroy<Lock> state;
    return state.get();
}

maths::Vec4 get_row(Vec3 v, float w) { return {.x = v.x, .y = v.y, .z = v.z, .w = w}; }

// The local skater (the skate module's 0x00566520; null with none).
objects::Rider* local_skater()
{
    auto* skate = modules::Skate::sole();
    return skate ? skate->get_home_rider() : nullptr;
}

// The game's collision ray (render::ProbeRay: static collision, the nearest hit) from `from` to `to`.
bool ground_below(Vec3 point, float* y)
{
    // (the game's memory layout, as level_editor_carry.cpp's first_hit: static collision only, nothing to release)
    render::ProbeRay ray_storage;
    render::ProbeRay* feeler = &ray_storage;
    const maths::Vec4 start = {.x = point.x, .y = point.y + kProbeAbove, .z = point.z, .w = 1.0f};
    const maths::Vec4 end = {.x = point.x, .y = point.y - kProbeBelow, .z = point.z, .w = 1.0f};
    if (!feeler->get_collision_along(&start, &end, false))
        return false;
    *y = feeler->contact_pos().y;
    return true;
}

// TeleportSkaterStanding pos=(x, y, z) [heading=<degrees>]
bool cfunc_teleport_skater_standing(script::Struct* params, script::Script* /*script*/)
{
    script::VectorValue pos = {};
    if (!params || !params->fetch_vec("pos", &pos, 0)) {
        get_log("TeleportSkaterStanding: pos=(x, y, z) needed");
        return false;
    }
    float heading = 0.0f;
    const bool has_heading = params->fetch_float("heading", &heading, 0);
    Vec3 at = {};
    const std::string why = jump_to({.x = pos.x, .y = pos.y, .z = pos.z}, has_heading, heading, &at);
    if (!why.empty()) {
        get_log("TeleportSkaterStanding: %s", why.c_str());
        return false;
    }
    return true;
}

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

std::optional<CameraRows> camera_rows(Vec3 direction)
{
    const float length = len_of(direction);
    if (!(length > kMinDirectionLength))
        return std::nullopt;
    const Vec3 b = scale_tool(direction, -1.0f / length);
    Vec3 r = perpendicular({.x = 0.0f, .y = 1.0f, .z = 0.0f}, b);
    const float rl = len_of(r);
    if (!(rl > kMinRightLength))
        return std::nullopt;
    r = scale_tool(r, 1.0f / rl);
    Vec3 u = perpendicular(b, r);
    u = scale_tool(u, 1.0f / len_of(u));
    return CameraRows{.right = r, .up = u, .back = b};
}

void standing_rows(float heading_degrees, Vec3* right, Vec3* up, Vec3* at)
{
    const double h = static_cast<double>(heading_degrees) * std::numbers::pi / 180.0;
    auto s = static_cast<float>(std::sin(h));
    auto c = static_cast<float>(std::cos(h));
    *at = {.x = s, .y = 0.0f, .z = c};
    *up = {.x = 0.0f, .y = 1.0f, .z = 0.0f};
    *right = perpendicular(*up, *at); // (cos, 0, -sin): heading 0 gives the identity
}

// --- the game ---------------------------------------------------------------------------------------------------

std::string jump_to(Vec3 position, bool has_heading, float heading_degrees, Vec3* out_position, bool on_board)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return "no local skater";
    if (!has_heading) {
        const maths::Vec4& at = core::at(skater->basis_frame().row, 2);
        heading_degrees =
            static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)) * kDegreesPerHalfTurn / std::numbers::pi);
    }
    float ground = 0.0f;
    const bool grounded = ground_below(position, &ground);
    if (grounded)
        position.y = ground;
    Vec3 right{};
    Vec3 up{};
    Vec3 at{};
    standing_rows(heading_degrees, &right, &up, &at);
    // (the skater's own custom restart kept and put back: only the restart's code is borrowed)
    const maths::Vec4 saved_pos = skater->custom_restart_pos();
    const maths::Mat4 saved_matrix = skater->custom_restart_matrix();
    const bool saved_walking = skater->custom_restart_walking();
    skater->set_custom_restart_pos(get_row(position, 1.0f));
    core::at(skater->custom_restart_matrix().row, 0) = get_row(right, 0.0f);
    core::at(skater->custom_restart_matrix().row, 1) = get_row(up, 0.0f);
    core::at(skater->custom_restart_matrix().row, 2) = get_row(at, 0.0f);
    core::at(skater->custom_restart_matrix().row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    skater->set_custom_restart_walking(!on_board);
    skater->skip_to_custom_restart();
    skater->set_custom_restart_pos(saved_pos);
    skater->set_custom_restart_matrix(saved_matrix);
    skater->set_custom_restart_walking(saved_walking);
    get_log("test view: skater teleported %s to (%.1f, %.1f, %.1f) heading %.1f degrees%s",
            on_board ? "on its board" : "standing", position.x, position.y, position.z, heading_degrees,
            grounded ? " (on the ground below)" : " (no ground below: as given)");
    if (out_position)
        *out_position = position;
    return {};
}

bool lock_camera(const CameraHold& hold)
{
    const Vec3 position = hold.position;
    const Vec3 drift = hold.drift;
    const std::optional<CameraRows> rows = camera_rows(hold.direction);
    if (!rows)
        return false;
    const Vec3 right = rows->right;
    const Vec3 up = rows->up;
    const Vec3 back = rows->back;
    lock() = {.on = true, .pos = position, .right = right, .up = up, .back = back, .drift = drift, .frame = frame_loop::frames_run()};
    get_log("test view: camera locked at (%.1f, %.1f, %.1f) looking along (%.3f, %.3f, %.3f), drifting (%.3f, %.3f, "
            "%.3f) a "
            "frame",
            position.x, position.y, position.z, -back.x, -back.y, -back.z, drift.x, drift.y, drift.z);
    return true;
}

void unlock_camera()
{
    if (lock().on)
        get_log("test view: camera unlocked");
    lock().on = false;
}

bool camera_locked() { return lock().on; }

void place_camera(gfx::ViewCam* camera, int viewport)
{
    if (!lock().on || !camera || viewport != 0)
        return;
    auto frames = static_cast<float>(frame_loop::frames_run() - lock().frame);
    camera->set_at_point(get_row({.x = lock().pos.x + (lock().drift.x * frames),
                                  .y = lock().pos.y + (lock().drift.y * frames),
                                  .z = lock().pos.z + (lock().drift.z * frames)},
                                 1.0f));
    core::at(camera->basis_frame().row, 0) = get_row(lock().right, 0.0f);
    core::at(camera->basis_frame().row, 1) = get_row(lock().up, 0.0f);
    core::at(camera->basis_frame().row, 2) = get_row(lock().back, 0.0f);
}

void register_script_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {.name = "TeleportSkaterStanding", .function = &cfunc_teleport_skater_standing},
    });
    script::install_script_functions(kFunctions);
}

} // namespace frontend::test_view

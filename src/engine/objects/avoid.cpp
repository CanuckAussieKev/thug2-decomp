// The Avoid component (GameObjects::ObstacleDodgeModule): see avoid.h. From the disassembly of 0x004E3270-0x004E3F50 (the
// member functions 0x004E37D0, 0x004E3810, 0x004E3BD0 and 0x004E3EC0 aren't Ghidra functions).

#include "engine/objects/path_follower.h"
#include "engine/objects/avoid.h"
#include "platform/d3dx.h"
#include "game/rider/rider.h"
#include "game/skate/skate_module.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gsl/pointers>
#include <array>

#include "engine/core/dbg.h"
#include "engine/maths/mat4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/motion_path.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/maths/vec4.h"

namespace objects {

std::array<script::MemberFunctionEntry, 9>& avoid_member_functions()
{
    static std::array<script::MemberFunctionEntry, 9> avoid_member_functions{{
        {.callsign_ = 0x6157CBC0, .owner_kind_ = 0x6EB050ED}, {.callsign_ = 0xD2AA4D2D, .owner_kind_ = 0x6EB050ED}, {.callsign_ = 0x41226942, .owner_kind_ = 0x6EB050ED}, {.callsign_ = 0x3C7F8F3E, .owner_kind_ = 0x6EB050ED},
        {.callsign_ = 0x6A4BCA91, .owner_kind_ = 0x6EB050ED}, {.callsign_ = 0x3FB422E8, .owner_kind_ = 0x6EB050ED}, {.callsign_ = 0x539A32DC, .owner_kind_ = 0x6EB050ED}, {.callsign_ = 0x53BC3B4E, .owner_kind_ = 0x6EB050ED},
        {.callsign_ = 0, .owner_kind_ = 0x6EB050ED}}};
    return avoid_member_functions;
}

namespace {

constexpr uint32_t kAvoid = 0x6EB050ED;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kMotion = 0x0A015E17;
constexpr uint32_t kJumpLeft = 0x733DA756, kJumpRight = 0xA7A0DD72, kJumpBack = 0x64948109, kJumpForward = 0x4DE2C9E8;

constexpr float kQuarterTurn = 0.7853981852531433f; // 0x006453F8 (pi / 4)
constexpr float kFullTurn = 6.2831854820251465f;    // 0x0064545C
constexpr float kToQuarters = 0.6366197466850281f;  // 0x00645488 (2 / pi)
constexpr float kToRadians = 0.01745329238474369f;  // 0x00645464
constexpr float kToDegrees = 57.2957763671875f;     // 0x00645468

// The quarter an angle falls in (0-4; 4 is ahead again), as the original's x87 sum and _ftol2.
int get_quarter(float angle)
{
    double value = static_cast<double>(angle) + kQuarterTurn;
    if (value < 0.0)
        value += kFullTurn;
    // _ftol2 converts to 64 bits and the low half is used: not a number or out of range (the integer
    // indefinite, 0x8000000000000000) gives 0 - ahead.
    const double quarters = value * kToQuarters;
    if (!(std::fabs(quarters) < 9.2e18))
        return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(quarters))));
}

// The local skater (modules::Skate 0x007CE478's 0x00566520; not rewritten yet).
GameEntity* local_skater() { return static_cast<GameEntity*>(modules::Skate::sole()->get_home_rider()); }

// The animation blender's (component 0x7149B753).
AnimBlenderComponent* get_blender(ObjectModule* blender) { return core::down_cast<AnimBlenderComponent>(blender); }
void play_anim(ObjectModule* blender, uint32_t anim, int unknown)
{
    get_blender(blender)->play_animation(anim, static_cast<uint32_t>(unknown));
}
float clip_length(ObjectModule* blender, uint32_t anim) { return get_blender(blender)->fetch_value(anim); }
// (each channel's 0x0047DC40 with these: any_channel - its result unused here)
void flip_anims(ObjectModule* blender, uint32_t object_id, bool flip, int a, int b)
{
    get_blender(blender)->any_channel(object_id, flip ? 1u : 0u, static_cast<uint32_t>(a), static_cast<uint32_t>(b));
}
bool anim_complete(ObjectModule* blender) { return get_blender(blender)->is_clip_done(); }
// The motion component's (0x0A015E17) path follower, 0x00423510 (or null).
PathFollower* motion_path(ObjectModule* motion)
{
    return core::down_cast<KinematicsModule>(motion)->get_path_follower();
}
uint32_t& motion_flags(ObjectModule* motion) { return core::down_cast<KinematicsModule>(motion)->motion_flags2(); }

// `out` = `in` (all four) times `m`, summed as 0x0040AF60 does.
void xform(const maths::Mat4& m, maths::Vec4& out, const maths::Vec4& in) { m.xform(out, in); }
// D3DXMatrixMultiply (linked library code).
void d3dx_multiply(maths::Mat4& out, const maths::Mat4& a, const maths::Mat4& b)
{
    d3dx::matrix_multiply(&out, &a, &b);
}

// The collision feeler (render::ProbeRay): constructed by 0x00417760, its line tested by 0x004172F0; what it hit is
// held by a smart pointer, let go of by its destructor.
class Feeler {
public:
    Feeler() = default;
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    ~Feeler() = default;
    maths::Vec4& kickoff() { return ray_.line().start; }
    maths::Vec4& get_end() { return ray_.line().end; }
    bool get_hit_shape(int a, int b) { return ray_.get_hit_shape(a != 0, b != 0) != 0; }

private:
    render::ProbeRay ray_;
};

// The angle of `v` about the y row of `m` from its at row, in the original's degrees or radians.
float angle_in_plane(const maths::Mat4& m, const maths::Vec4& v) { return maths::get_angle_in_plane(m, v, 2, 1); }

} // namespace

// 0x004E3270
float avoid_distance(float angle, script::Struct* params)
{
    float distance = 0.0f;
    switch (get_quarter(angle)) {
    case 1:
        params->fetch_float("distLeft", &distance, 1);
        break;
    case 2:
        params->fetch_float("distBack", &distance, 1);
        break;
    case 3:
        params->fetch_float("distRight", &distance, 1);
        break;
    default:
        params->fetch_float("distForward", &distance, 1);
        break;
    }
    return distance;
}

// 0x004E3930
bool line_collides(const maths::Vec4& start, const maths::Vec4& end)
{
    Feeler feeler;
    feeler.kickoff() = start;
    feeler.get_end() = end;
    return feeler.get_hit_shape(1, 0);
}

// 0x004E39E0
gsl::owner<ObjectModule*> ObstacleDodgeModule::fabricate() { return new ObstacleDodgeModule; }

// 0x004E3310
void ObstacleDodgeModule::publish_method_table()
{
    script::register_member_function_table(avoid_member_functions());
}

// 0x004E3660
ObstacleDodgeModule::ObstacleDodgeModule() { set_kind_tag(kAvoid); }

// 0x004E3330 (0x004E36C0 the deleting one)
ObstacleDodgeModule::~ObstacleDodgeModule() = default;

// 0x004E34C0
float ObstacleDodgeModule::side_away_from_skater()
{
    const GameEntity* skater = local_skater();
    // The skater's heading (its at row) flat and normalised; across it (x up), as the original's cross product
    // with (0, 1, 0) - the zero terms multiplied out.
    const maths::Vec4& at = core::at(skater->basis_frame().row, 2);
    double x = at.x, z = at.z, y = 0.0;
    auto length = static_cast<float>(std::sqrt(z * z + x * x));
    if (length > 0.0f) {
        const double exact_inverse = 1.0 / length; // (x by the unrounded inverse, z and y by the rounded one)
        auto inverse = static_cast<float>(exact_inverse);
        x *= exact_inverse;
        y = static_cast<double>(inverse) * 0.0f;
        z *= inverse;
    }
    const double zero_y = y * 0.0f;
    auto side_x = static_cast<float>(zero_y - z);
    auto side_y = static_cast<float>(z * 0.0f - x * 0.0f);
    auto side_z = static_cast<float>(x - zero_y);
    maths::Vec4 side = {.x = side_x, .y = side_y, .z = side_z, .w = 0.0f};
    // Which of the object's two sides is further from the skater (the first one's x and y unrounded).
    const maths::Vec4& pos = entity()->at_point();
    const double a_x = static_cast<double>(side_x) + pos.x;
    const double a_y = static_cast<double>(side_y) + pos.y;
    const float a_z = side_z + pos.z;
    const float b_x = pos.x - side_x;
    const float b_y = pos.y - side_y;
    const float b_z = pos.z - side_z;
    const maths::Vec4& s = skater->at_point();
    auto d1x = static_cast<float>(s.x - a_x);
    const double d1y = s.y - a_y;
    const double d1z = static_cast<double>(s.z) - a_z;
    const double d2x = static_cast<double>(s.x) - b_x;
    const double d2y = static_cast<double>(s.y) - b_y;
    const double d2z = static_cast<double>(s.z) - b_z;
    const double first = (d1z * d1z + d1y * d1y) + static_cast<double>(d1x) * d1x;
    const double second = (d2z * d2z + d2y * d2y) + d2x * d2x;
    if (first < second) {
        side.x = -side_x;
        side.z = -side_z;
    }
    return static_cast<float>(static_cast<double>(angle_in_plane(entity()->display_matrix(), side)) * kToDegrees);
}

// 0x004E3A40
bool ObstacleDodgeModule::try_jump(const maths::Mat4& start, maths::Mat4& out, JumpReach jump_reach)
{
    const auto angle = jump_reach.angle;
    const auto distance = jump_reach.distance;
    const auto max_distance = jump_reach.max_distance_;
    out = start;
    maths::Mat4 turn{};
    d3dx_multiply(out, maths::make_yaw_matrix(turn, static_cast<float>(static_cast<double>(angle) * kToRadians)), out);
    const maths::Vec4 probe = {.x = 0.0f, .y = 10.0f, .z = distance + 15.0f, .w = 1.0f};
    maths::Vec4 probe_end;
    xform(out, probe_end, probe);
    if (line_collides(core::at(start.row, 3), probe_end))
        return false;
    const maths::Vec4 landing = {.x = 0.0f, .y = 0.0f, .z = distance, .w = 1.0f};
    maths::Vec4 end;
    xform(out, end, landing);
    core::at(out.row, 3) = end;
    const maths::Vec4& s = local_skater()->at_point();
    const double dx = static_cast<double>(end.x) - s.x;
    const double dy = static_cast<double>(end.y) - s.y;
    const double dz = static_cast<double>(end.z) - s.z;
    const double from_skater = std::sqrt((dz * dz + dy * dy) + dx * dx);
    if (from_skater < 30.0) {
        debug::printf("Too close to skater %f!\n", from_skater);
        return false;
    }
    const double hx = static_cast<double>(end.x) - herd_point_.x;
    const double hz = static_cast<double>(end.z) - herd_point_.z;
    return !(std::sqrt(hz * hz + hx * hx) >= max_distance); // (not a number: within, as the original's FCOMP)
}

// 0x004E3340
void ObstacleDodgeModule::play_jump(float angle, script::Struct* params)
{
    // The checksum is read into the parameters' own argument slot: without one it's the pointer's value.
    uint32_t anim = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    switch (get_quarter(angle)) {
    case 0:
    case 4:
        params->read_hash_value(kJumpForward, &anim, 1);
        break;
    case 1:
    case 3: {
        // The side's own animation, or the other side's flipped.
        const uint32_t own = get_quarter(angle) == 1 ? kJumpLeft : kJumpRight;
        const uint32_t other = get_quarter(angle) == 1 ? kJumpRight : kJumpLeft;
        const bool flip = !params->read_hash_value(own, &anim, 0);
        if (flip)
            params->read_hash_value(other, &anim, 1);
        if (ObjectModule* blender = entity()->fetch_module(kAnimBlender))
            flip_anims(blender, entity()->ident(), flip, 0, 1);
        break;
    }
    case 2:
        params->read_hash_value(kJumpBack, &anim, 1);
        break;
    default:
        return;
    }
    if (ObjectModule* blender = entity()->fetch_module(kAnimBlender)) {
        play_anim(blender, anim, 0);
        lasts_for_ = clip_length(blender, anim);
    }
}

// 0x004E3BD0
bool ObstacleDodgeModule::get_jump(script::Struct* params, script::Script* /*script*/)
{
    // From where it stands, as its display matrix shows it.
    maths::Mat4 start = entity()->display_matrix();
    core::at(start.row, 3) = entity()->at_point();
    float max_distance = 100.0f;
    params->fetch_float("maxHerdDistance", &max_distance, 0);
    maths::Mat4 end{};
    const float side = side_away_from_skater();
    bool found = try_jump(start, end, {.angle = side, .distance = avoid_distance(side, params), .max_distance_ = max_distance});
    if (!found) {
        auto other = static_cast<float>(static_cast<double>(side) + 180.0f);
        found = try_jump(start, end, {.angle = other, .distance = avoid_distance(other, params), .max_distance_ = max_distance});
    }
    if (!found) {
        auto back = static_cast<float>(static_cast<double>(side) - 90.0f);
        found = try_jump(start, end, {.angle = back, .distance = avoid_distance(back, params), .max_distance_ = max_distance});
    }
    if (!found) {
        // Away from the herd point (this angle in radians, as the original passes it).
        const maths::Vec4& pos = core::at(start.row, 3);
        const maths::Vec4 away = {.x = herd_point_.x - pos.x, .y = herd_point_.y - pos.y, .z = herd_point_.z - pos.z,
                                  .w = herd_point_.w - pos.w};
        const float angle = angle_in_plane(entity()->basis_frame(), away);
        found = try_jump(start, end, {.angle = angle, .distance = avoid_distance(angle, params), .max_distance_ = max_distance});
    }
    if (!found)
        return false;
    // The landing keeps the start's rows; the slerp from the start to it.
    const maths::Vec4 landing = core::at(end.row, 3);
    end = start;
    core::at(end.row, 3) = landing;
    slerp_.set_matrices(start, end);
    progress_ = 0.0f;
    const maths::Vec4& pos = core::at(start.row, 3);
    const maths::Vec4 move = {.x = landing.x - pos.x, .y = landing.y - pos.y, .z = landing.z - pos.z, .w = landing.w - pos.w};
    play_jump(angle_in_plane(entity()->basis_frame(), move), params);
    return true;
}

// 0x004E36F0
bool ObstacleDodgeModule::debug_box(script::Struct* params, script::Script* /*script*/)
{
    float distance = 0.0f; // (read into the parameters' own argument slot: without one it keeps the pointer's bits)
    std::memcpy(&distance, static_cast<const void*>(&params), sizeof(distance));
    params->fetch_float("dist", &distance, 1);
    return false;
}

// 0x004E3EC0
bool ObstacleDodgeModule::step_jump(script::Struct* /*params*/, script::Script* /*script*/)
{
    progress_ = static_cast<float>(frame_clock::get_slomo_frame_seconds() * 60.0f * lasts_for_ * 0.01666666753590107f +
                                   progress_);
    maths::Mat4 now{};
    slerp_.fetch_mat(now, progress_);
    entity()->at_point() = core::at(now.row, 3);
    entity()->basis_frame() = now;
    core::at(entity()->basis_frame().row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    return true;
}

// 0x004E37D0
bool ObstacleDodgeModule::jump_finished(script::Struct* /*params*/, script::Script* /*script*/)
{
    if (!(progress_ >= 1.0f))
        return false;
    return anim_complete(entity()->fetch_module(kAnimBlender));
}

// 0x004E3810
bool ObstacleDodgeModule::store_position(script::Struct* /*params*/, script::Script* /*script*/)
{
    herd_point_ = entity()->at_point();
    return true;
}

// 0x004E3850
bool ObstacleDodgeModule::store_target(script::Struct* /*params*/, script::Script* /*script*/)
{
    const maths::Vec4* point = &entity()->at_point();
    ObjectModule* motion = entity()->fetch_module(kMotion);
    if (motion && motion_path(motion))
        point = &motion_path(motion)->end_point(); // (the original's +0x74: the node headed for)
    aim_point_ = *point;
    return true;
}

// 0x004E38C0
bool ObstacleDodgeModule::hold_motion(script::Struct* /*params*/, script::Script* /*script*/)
{
    uint32_t& flags = motion_flags(entity()->fetch_module(kMotion));
    flags |= 4;
    motion_held_ = flags != 0;
    return true;
}

// 0x004E38F0
bool ObstacleDodgeModule::restore_motion(script::Struct* /*params*/, script::Script* /*script*/)
{
    if (ObjectModule* motion = entity()->fetch_module(kMotion)) {
        if (motion_held_)
            motion_flags(motion) |= 4;
        else
            motion_flags(motion) &= ~4u;
    }
    return true;
}

} // namespace objects

// The skater's non-local network logic component (GameObjects::RiderRemoteSync): see nonlocal_net_logic.h.
// The movement code follows the original's x87 maths: products and sums kept unrounded (double, at the game's 53-bit
// precision) until the original stores them as floats.

#include "game/rider/nonlocal_net_logic.h"
#include "engine/core/enum_ops.h"

#include <cmath>
#include <cstdint>
#include <gsl/pointers>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/shadow_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/looping_sound.h"
#include "game/rider/rider.h"
#include "platform/stopwatch.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/light_manager.h"
#include "game/skate/game_mode.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/skater_state.h"
#include "game/rider/state_history.h"
#include "engine/objects/model_component.h"
#include "engine/gfx/model.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include "engine/core/checksums.h"
#include "game/rider/core_physics.h"
#include "engine/render/coll_obj.h"
#include "engine/maths/trig.h"

namespace objects {

namespace {
constexpr double kHalf = 0.5;

constexpr uint32_t kSkaterNonLocalNetLogic = 0x850EEF66;
constexpr uint32_t kSkaterStateHistory = 0x18223FD6, kSkaterState = 0x43686585, kSkaterEndRun = 0xB1D5A3E1,
                   kAnimBlender = 0x7149B753, kSkaterFlipAndRotate = 0x488AF07A, kModel = 0x286A8D26,
                   kShadow = 0x8A897DD2, kSkaterLoopingSound = 0xB5F1E9D2;
// Physics and script values
constexpr uint32_t kGroundSnapUp = 0xE4D79235, kGroundSnapDown = 0x899BA3D0, kWallNonSkatableAngle = 0x3EEDE4D3,
                   kGroundStickAngleForward = 0x4138283E, kFirstForwardCollisionHeight = 0xD4205C9B,
                   kFirstForwardCollisionLength = 0x20102726, kWallBounceDontSlowAngle = 0x1483FD01,
                   kWallBounceAngleMultiplier = 0xE07EE1A9;
// Scripts
constexpr uint32_t kSkaterId = 0x5B24FAAA, kControlType = 0x81CFF663, kStartDrivingScript = 0x7853F8C4,
                   kStopDrivingScript = 0x0F02E44F;
constexpr uint32_t kNetFirefight = 0xBFF33600, kFirefight = 0x3D6D444F;

constexpr float kRadiansPerDegree = 0.01745329238474369F; // 0x00645464
constexpr float kHalfPi = 1.5707963705062866F;            // 0x00645484
constexpr float kLightingRise = 0.001F;                   // 0x006461D8 (of the up row)
constexpr float kLightingAbove = 8.0F;                    // 0x006461D4
constexpr float kLightingBelow = 4800.0F;                 // 0x006461D0
constexpr float kSnapBelow = -200.0F;                     // 0x0064A258
constexpr float kOutOfGround = 0.001F;                    // 0x006461D8
constexpr float kWallOut = 6.0F;                          // 0x00646284
constexpr float kBounceReach = 240.0F;                    // 0x00646558
constexpr float kLightTime = 0.02F;                       // 0x3CA3D70A
constexpr float kSpeedToSoundFraction = 0.001F;           // 0x006461D8

constexpr int kStateAir = 1, kStateLip = 3, kStateRail = 4;

// A physics value of the skater (GameObjects::read_tuning_float, asserting).
double get_physics(GameEntity* skater, uint32_t name) { return read_tuning_float(checksums::Name{name}, 1, skater); }

// m = rotation * m (D3DXMatrixMultiply, linked library code).
void turn_by(maths::Mat4& matrix, const maths::Mat4& rotation) { maths::matrix_multiply(&matrix, &rotation, &matrix); }

} // namespace

// 0x005147F0
gsl::owner<ObjectModule*> RiderRemoteSync::fabricate() { return new RiderRemoteSync; }

// 0x005144E0
RiderRemoteSync::RiderRemoteSync()
{
    set_kind_tag(kSkaterNonLocalNetLogic);
    play_pos_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    last_play_pos_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
}

// 0x005144B0 (0x00514550 the deleting one)
RiderRemoteSync::~RiderRemoteSync() = default;

// 0x00514580
void RiderRemoteSync::seal()
{
    past_frames_ = core::down_cast<RiderFrameLog>(entity()->fetch_module(kSkaterStateHistory));
    pose_ = core::down_cast<RiderPoseState>(entity()->fetch_module(kSkaterState));
    run_over_ = entity()->fetch_module(kSkaterEndRun);
    anim_blender_ = entity()->fetch_module(kAnimBlender);
    flip_and_rotate_ = entity()->fetch_module(kSkaterFlipAndRotate);
    mesh_set_ = core::down_cast<MeshInstanceModule>(entity()->fetch_module(kModel));
    shadow_ = entity()->fetch_module(kShadow);
}

// 0x005144D0
void RiderRemoteSync::reset_playback()
{
    sync_frame_ = 0;
    last_event_time_ = 0;
}

// 0x00514610
void RiderRemoteSync::update_ground_lighting()
{
    render::ProbeRay feeler;
    const maths::Vec4& pos = entity()->at_point();
    const maths::Vec4& up = core::at(entity()->basis_frame().row, 1);
    maths::Vec4& start = feeler.line().start;
    maths::Vec4& end = feeler.line().end;
    start = pos;
    end = pos;
    // (from just above the skater: a thousandth of the up row, then 8 up; down 4800)
    start.x = static_cast<float>((static_cast<double>(up.x) * kLightingRise) + start.x);
    start.y = static_cast<float>((static_cast<double>(up.y) * kLightingRise) + start.y);
    start.z = static_cast<float>((static_cast<double>(up.z) * kLightingRise) + start.z);
    start.w = static_cast<float>((static_cast<double>(up.w) * kLightingRise) + start.w);
    start.y = static_cast<float>(static_cast<double>(start.y) + kLightingAbove);
    end.y = static_cast<float>(static_cast<double>(end.y) - kLightingBelow);
    if (feeler.get_hit_shape(true, false) == 0) {
        return;
    }
    if (pose_->cur_phase() != kStateRail && feeler.is_hit_triangle_collision()) {
        // The model's light from the face's vertex values (the model's light object, its +0x2C - read through the
        // folded getter 0x00532EB0; else the global).
        const render::RenderModel* model = mesh_set_->mesh_instance().get();
        void* light = model->lights_ptr().get();
        if (light) {
            static_cast<render::ModelLights*>(light)->move_brightness({.target = static_cast<float>(feeler.get_hit_vertex_value()), .step = kLightTime});
        } else {
            render::set_world_brightness(static_cast<float>(feeler.get_hit_vertex_value()));
        }
    }
    // The shadow at the hit.
    core::down_cast<GroundShadowModule>(shadow_)->change_place(&feeler.contact_pos());
    core::down_cast<GroundShadowModule>(shadow_)->set_normal(&feeler.surface_dir());
}

// 0x00514850
void RiderRemoteSync::settle_onto_floor()
{
    render::ProbeRay feeler;
    GameEntity* skater = entity();
    const maths::Vec4& up = core::at(skater->basis_frame().row, 1);
    const maths::Vec4& pos = skater->at_point();
    // From Physics_Ground_Snap_Up above (the product unrounded) to 200 below (rounded first).
    const double snap_up = get_physics(skater, kGroundSnapUp);
    maths::Vec4 start;
    maths::Vec4 end;
    start.x = static_cast<float>((snap_up * up.x) + pos.x);
    start.y = static_cast<float>((snap_up * up.y) + pos.y);
    start.z = static_cast<float>((snap_up * up.z) + pos.z);
    start.w = static_cast<float>((snap_up * up.w) + pos.w);
    auto down_w = static_cast<float>(static_cast<double>(up.w) * kSnapBelow);
    auto down_z = static_cast<float>(static_cast<double>(up.z) * kSnapBelow);
    auto down_y = static_cast<float>(static_cast<double>(up.y) * kSnapBelow);
    auto down_x = static_cast<float>(static_cast<double>(up.x) * kSnapBelow);
    end.x = static_cast<float>(static_cast<double>(down_x) + pos.x);
    end.y = static_cast<float>(static_cast<double>(down_y) + pos.y);
    end.z = static_cast<float>(static_cast<double>(down_z) + pos.z);
    end.w = static_cast<float>(static_cast<double>(down_w) + pos.w);
    feeler.change_strand(&start, &end);
    feeler.set_excluded({.ignore = render::kDefaultIgnoredFaceFlags, .require = render::kNoFaceFlags});
    if (feeler.get_hit_shape(true, false) == 0) {
        return;
    }

    const maths::Vec4& point = feeler.contact_pos();
    const maths::Vec4& normal = feeler.surface_dir();
    const double dz = static_cast<double>(pos.z) - point.z;
    const double dy = static_cast<double>(pos.y) - point.y;
    const double dx = static_cast<double>(pos.x) - point.x;
    auto distance = static_cast<float>(std::sqrt(((dx * dx) + (dy * dy)) + (dz * dz)));
    // How far above the hit the skater is, along its up row.
    auto height = static_cast<float>(((dy * up.y) + (dz * up.z)) + (dx * up.x));

    // (skatable, vert, not "not skatable" and not wallride - all in the low byte, which is all the original tests)
    const auto flags = feeler.hit().face_flags_;
    const bool skatable =
        core::any(flags & render::FaceFlag::kSkatable) && !core::any(flags & render::FaceFlag::kNotSkatable) && core::any(flags & render::FaceFlag::kVert) && !core::any(flags & render::FaceFlag::kWall);
    if (skatable &&
        !(std::sin(static_cast<double>(script::fetch_float(kWallNonSkatableAngle)) * kRadiansPerDegree) > normal.y)) {
        // Ground: unless it turns the skater's heading too far from its last ground's, close enough to snap to.
        const maths::Vec4 ground = normal;
        const maths::Vec4& at_row = core::at(skater->basis_frame().row, 2);
        maths::Vec4 on_ground = at_row;
        auto facing = static_cast<float>(
            ((static_cast<double>(normal.x) * at_row.x) + (static_cast<double>(normal.z) * at_row.z)) +
            (static_cast<double>(at_row.y) * normal.y));
        maths::Vec4 on_last = at_row;
        on_ground.align_to_plane(ground);
        on_last.align_to_plane(surface_up_);
        auto turn = static_cast<float>(
            ((static_cast<double>(on_last.x) * on_ground.x) + (static_cast<double>(on_last.z) * on_ground.z)) +
            (static_cast<double>(on_last.y) * on_ground.y));
        auto stick = static_cast<float>(std::cos(get_physics(skater, kGroundStickAngleForward) * kRadiansPerDegree));
        if (facing > 0.0F && turn > 0.0F && turn < stick) {
            return;
        }
        if (height > 0.0F) {
            // Above it: only within this frame's move times the slope between the two grounds (at least
            // Physics_Ground_Snap_Down).
            const float angle = std::acos(turn);
            const maths::Vec4 move = skater->at_point() - last_pos_;
            const float slope = std::tan(angle);
            // (its length unrounded, as Vec4::len_of's x87 code leaves it)
            const double length =
                std::sqrt(((static_cast<double>(move.x) * move.x) + (static_cast<double>(move.y) * move.y)) +
                          (static_cast<double>(move.z) * move.z));
            auto reach = static_cast<float>(length * slope);
            const double snap_down = get_physics(skater, kGroundSnapDown);
            if (reach < snap_down) {
                reach = static_cast<float>(snap_down);
            }
            if (distance > reach) {
                return;
            }
        }
        surface_up_ = ground;
        core::at(skater->basis_frame().row, 1) = ground;
        skater->basis_frame().square_up_axes(1);
        skater->at_point() = point;
        return;
    }
    // Not ground: if the skater is (nearly) at or below it, out by one along its normal.
    if (height < kOutOfGround) {
        skater->at_point().x = static_cast<float>(static_cast<double>(normal.x) + point.x);
        skater->at_point().y = static_cast<float>(static_cast<double>(point.y) + normal.y);
        skater->at_point().z = static_cast<float>(static_cast<double>(normal.z) + point.z);
        skater->at_point().w = static_cast<float>(static_cast<double>(point.w) + normal.w);
    }
}

// 0x00514D10
double RiderRemoteSync::turn_towards(maths::Vec4 dir, float* turn)
{
    const maths::Vec4& right = core::at(entity()->basis_frame().row, 0);
    auto cosine = static_cast<float>(((static_cast<double>(dir.z) * right.z) + (static_cast<double>(dir.y) * right.y)) +
                                     (static_cast<double>(dir.x) * right.x));
    if (cosine > 1.0F) {
        cosine = 1.0F;
    } else if (cosine < -1.0F) {
        cosine = -1.0F;
    }
    const double exact = std::acos(static_cast<double>(cosine));
    auto angle = static_cast<float>(exact);
    if (exact > kHalfPi) { // (the unrounded angle compared; the rounded one adjusted)
        angle = static_cast<float>(static_cast<double>(angle) - maths::kPi);
    }
    const double rotation = static_cast<double>(script::fetch_float(kWallBounceAngleMultiplier)) * angle;
    *turn = static_cast<float>(rotation);
    // The velocity turned about y (sine / cosine unrounded), then the matrix by the rounded turn.
    const double turn_sine = std::sin(rotation);
    const double turn_cosine = std::cos(rotation);
    maths::Vec4& vel = entity()->vel();
    const double vx = vel.x;
    const double vz = vel.z;
    vel.x = static_cast<float>((turn_sine * vz) + (turn_cosine * vx));
    vel.z = static_cast<float>(-(vx * turn_sine) + (turn_cosine * vz));
    maths::Mat4 rotate{};
    maths::make_yaw_matrix(rotate, *turn);
    turn_by(entity()->basis_frame(), rotate);
    return angle;
}

// 0x00514E20
void RiderRemoteSync::deflect_from_wall(const maths::Vec4* normal, const maths::Vec4* point)
{
    GameEntity* skater = entity();
    auto away_z = static_cast<float>(static_cast<double>(skater->at_point().z) - last_pos_.z);
    auto away_y = static_cast<float>(static_cast<double>(skater->at_point().y) - last_pos_.y);
    auto away_x = static_cast<float>(static_cast<double>(skater->at_point().x) - last_pos_.x);
    render::ProbeRay feeler;
    // The collision height up the up row (the products rounded).
    const double height = get_physics(skater, kFirstForwardCollisionHeight);
    const maths::Vec4& up = core::at(skater->basis_frame().row, 1);
    auto raise_w = static_cast<float>(height * up.w);
    auto raise_z = static_cast<float>(height * up.z);
    auto raise_y = static_cast<float>(height * up.y);
    auto raise_x = static_cast<float>(height * up.x);
    float turn = 0.0F;
    auto angle = static_cast<float>(turn_towards(*normal, &turn));
    // Slowed by how far past Wall_Bounce_Dont_Slow_Angle it turned (to nothing at a right angle).
    const double dont_slow = static_cast<double>(script::fetch_float(kWallBounceDontSlowAngle)) * kRadiansPerDegree;
    const double magnitude = std::fabs(static_cast<double>(angle));
    if (magnitude > dont_slow) {
        const double keep = 1.0 - ((magnitude - dont_slow) / (kHalfPi - dont_slow));
        maths::Vec4& vel = skater->vel();
        vel.x = static_cast<float>(keep * vel.x);
        vel.y = static_cast<float>(keep * vel.y);
        vel.z = static_cast<float>(keep * vel.z);
        vel.w = static_cast<float>(keep * vel.w);
    }
    // Back at the wall's point, the height below it, 6 out along the normal.
    maths::Vec4& pos = skater->at_point();
    pos.x = static_cast<float>(static_cast<double>(point->x) - raise_x);
    pos.y = static_cast<float>(static_cast<double>(point->y) - raise_y);
    pos.z = static_cast<float>(static_cast<double>(point->z) - raise_z);
    pos.w = static_cast<float>(static_cast<double>(point->w) - raise_w);
    pos.x = static_cast<float>((static_cast<double>(normal->x) * kWallOut) + pos.x);
    pos.y = static_cast<float>((static_cast<double>(normal->y) * kWallOut) + pos.y);
    pos.z = static_cast<float>((static_cast<double>(normal->z) * kWallOut) + pos.z);
    pos.w = static_cast<float>((static_cast<double>(normal->w) * kWallOut) + pos.w);

    // A feeler from twice the height up, along the velocity as far as the skater went this frame (then the
    // collision length further).
    const double moved = std::sqrt(((static_cast<double>(away_x) * away_x) + (static_cast<double>(away_y) * away_y)) +
                                   (static_cast<double>(away_z) * away_z));
    maths::Vec4 dir = skater->vel();
    const double speed = std::sqrt(((static_cast<double>(dir.z) * dir.z) + (static_cast<double>(dir.y) * dir.y)) +
                                   (static_cast<double>(dir.x) * dir.x));
    if (speed > 0.0) {
        const double inverse = 1.0 / speed;
        dir.x = static_cast<float>(dir.x * inverse);
        dir.y = static_cast<float>(dir.y * inverse);
        dir.z = static_cast<float>(dir.z * inverse);
    }
    auto ahead_w = static_cast<float>(dir.w * moved);
    auto ahead_z = static_cast<float>(dir.z * moved);
    auto ahead_y = static_cast<float>(dir.y * moved);
    auto ahead_x = static_cast<float>(dir.x * moved);
    maths::Vec4 from;
    from.x = static_cast<float>((static_cast<double>(raise_x) + pos.x) + raise_x);
    from.y = static_cast<float>((static_cast<double>(raise_y) + pos.y) + raise_y);
    from.z = static_cast<float>((static_cast<double>(raise_z) + pos.z) + raise_z);
    from.w = static_cast<float>((static_cast<double>(raise_w) + pos.w) + raise_w);
    const double length = get_physics(skater, kFirstForwardCollisionLength);
    auto reach_w = static_cast<float>(dir.w * length);
    auto reach_z = static_cast<float>(dir.z * length);
    auto reach_y = static_cast<float>(dir.y * length);
    auto reach_x = static_cast<float>(length * dir.x);
    auto base_y = static_cast<float>(static_cast<double>(raise_y) + pos.y);
    auto base_x = static_cast<float>(static_cast<double>(raise_x) + pos.x);
    auto mid_w = static_cast<float>((static_cast<double>(raise_w) + pos.w) + ahead_w);
    auto mid_z = static_cast<float>((static_cast<double>(raise_z) + pos.z) + ahead_z);
    auto mid_y = static_cast<float>(static_cast<double>(base_y) + ahead_y);
    auto mid_x = static_cast<float>(static_cast<double>(base_x) + ahead_x);
    maths::Vec4 to;
    const double to_w = static_cast<double>(mid_w) + reach_w;
    const double to_z = static_cast<double>(mid_z) + reach_z;
    to.y = static_cast<float>(static_cast<double>(mid_y) + reach_y);
    to.x = static_cast<float>(static_cast<double>(mid_x) + reach_x);
    to.z = static_cast<float>(to_z);
    to.w = static_cast<float>(to_w);
    const double ex = static_cast<double>(from.x) - to.x;
    const double ey = static_cast<double>(from.y) - to.y;
    const double ez = static_cast<double>(from.z) - to_z;
    if (std::sqrt(((ex * ex) + (ey * ey)) + (ez * ez)) > kBounceReach) {
        return;
    }
    feeler.change_strand(&from, &to);
    feeler.set_excluded({.ignore = render::kDefaultIgnoredFaceFlags, .require = render::kNoFaceFlags});
    if ((feeler.get_hit_shape(true, false) == 0) || (skater->is_local_skater() == 0U)) {
        return;
    }
    // Another wall: turned round by what's left of a half turn, and halved.
    const double around = static_cast<double>(maths::kPi) - turn;
    auto sine = static_cast<float>(std::sin(around));
    auto cosine = static_cast<float>(std::cos(around));
    maths::Vec4& vel = skater->vel();
    const double vx = vel.x;
    const double vz = vel.z;
    const double nx = (vx * cosine) + (vz * sine);
    const double nz = -(vx * sine) + (vz * cosine);
    vel.x = static_cast<float>(nx);
    vel.z = static_cast<float>(nz);
    maths::Mat4 rotate{};
    maths::make_yaw_matrix(rotate, static_cast<float>(around));
    turn_by(skater->basis_frame(), rotate);
    vel.x = static_cast<float>(vel.x * kHalf);
    vel.y = static_cast<float>(vel.y * kHalf);
    vel.z = static_cast<float>(vel.z * kHalf);
    vel.w = static_cast<float>(vel.w * kHalf);
}

// 0x00517270
void RiderRemoteSync::tick_frame()
{
    auto* net_manager = online::net_game_state().online_hub();
    frame_seconds_ = static_cast<float>(frame_clock::get_frame_seconds());
    const int state = pose_->cur_phase();
    const bool was_lip = state == kStateLip;
    if (state != kStateAir) {
        // (on the ground the state's two vectors follow the up row)
        pose_->set_drawn_normal(core::at(entity()->basis_frame().row, 1));
        pose_->floor_normal() = core::at(entity()->basis_frame().row, 1);
    }
    update_ground_lighting();
    last_pos_ = entity()->at_point();
    entity()->old_pos() = entity()->at_point();
    interpolate();
    // Extrapolated when this machine plays (not observing or waiting) in a game that wants it.
    auto* player = static_cast<online::PlayerRecord*>(net_manager->get_own_player());
    if ((player->is_observer() == 0) && (player->is_scouting_level() == 0)) {
        void* game_mode = static_cast<void*>(goals::game_mode());
        const bool extrapolate = online::net_game_state().online_hub()->players_can_collide() ||
                                 static_cast<goals::MatchRules*>(game_mode)->get_name_key() == kNetFirefight ||
                                 static_cast<goals::MatchRules*>(game_mode)->get_name_key() == kFirefight;
        if (extrapolate && past_frames_->state_count() >= kHistory) {
            if (pose_->cur_phase() == kStateRail) {
                extrapolate_on_rail();
            } else {
                get_extrapolate();
                settle_onto_floor();
            }
        }
    }
    play_history_events();

    // Out of a lip straight up (as the finalize physics component's).
    if (was_lip && pose_->cur_phase() != kStateLip) {
        pose_->set_lip_straight_up(0);
        if (pose_->cur_phase() == kStateAir && std::fabs(entity()->vel().x) < 1.0F &&
            std::fabs(entity()->vel().z) < 1.0F) {
            pose_->set_lip_straight_up(1);
        }
    }
    if (pose_->cur_phase() != kStateAir) {
        pose_->set_lip_straight_up(0);
    }

    // Driving in or out: the scripts told (with the vehicle's control type when it starts).
    const uint8_t driving = pose_->in_vehicle();
    const uint32_t steer_kind = past_frames_->vehicle_control_type();
    if (((driving != 0U) && (steer_kind != 0U)) || ((driving == 0U) && (was_driving_ != 0U))) {
        gsl::owner<script::Struct*> params = script::spawn_param_bag();
        if (params) {
            params = new (params) script::Struct;
        }
        params->put_name_key(checksums::Name{kSkaterId}, entity()->ident());
        uint32_t script = kStopDrivingScript;
        if (driving != 0U) {
            params->put_name_key(checksums::Name{kControlType}, steer_kind);
            script = kStartDrivingScript;
        }
        script::launch_script(script, params, nullptr, false, nullptr);
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
    was_driving_ = pose_->in_vehicle();

    // The looping sound: on while skating, its speed and state.
    auto* sound = core::down_cast<RiderRollingAudio>(entity()->fetch_module(kSkaterLoopingSound));
    if (pose_->physics_mode() != objects::RiderPoseState::PhysicsMode::kSkating) {
        if (sound->on() != 0U) {
            sound->hold_off(true);
            sound->set_on(0);
        }
        return;
    }
    if (sound->on() != 1) {
        sound->hold_off(false);
        sound->set_on(1);
    }
    const double vz = entity()->vel().z;
    const double vx = entity()->vel().x;
    sound->set_pace_ratio(static_cast<float>(std::sqrt((vx * vx) + (vz * vz)) * kSpeedToSoundFraction));
    uint8_t changed = sound->sound_changed();
    changed |= sound->cur_phase() != pose_->cur_phase() ? 1 : 0;
    sound->set_cur_phase(pose_->cur_phase());
    changed |= sound->surface_type() != pose_->surface_type() ? 1 : 0;
    sound->sound_changed() = changed;
    sound->set_surface_type(pose_->surface_type());
    sound->set_no_rail_sound(pose_->flag(PoseFlag::kBailing).on);
    changed |= sound->sliding() != pose_->flag(PoseFlag::kSlide).on ? 1 : 0;
    sound->sound_changed() = changed;
    sound->set_sliding(pose_->flag(PoseFlag::kSlide).on);
}

} // namespace objects

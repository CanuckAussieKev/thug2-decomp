// The skater's non-local network logic component (GameObjects::RiderRemoteSync): see nonlocal_net_logic.h.
// The movement code follows the original's x87 maths: products and sums kept unrounded (double, at the game's 53-bit
// precision) until the original stores them as floats.

#include "game/rider/nonlocal_net_logic.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
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
#include "game/rider/skater_x87.h"
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

namespace objects {

namespace {

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

constexpr float kRadiansPerDegree = 0.01745329238474369f; // 0x00645464
constexpr float kHalfPi = 1.5707963705062866f;            // 0x00645484
constexpr float kPi = 3.1415927410125732f;                // 0x0064BFA4
constexpr float kLightingRise = 0.001f;                   // 0x006461D8 (of the up row)
constexpr float kLightingAbove = 8.0f;                    // 0x006461D4
constexpr float kLightingBelow = 4800.0f;                 // 0x006461D0
constexpr float kSnapBelow = -200.0f;                     // 0x0064A258
constexpr float kOutOfGround = 0.001f;                    // 0x006461D8
constexpr float kWallOut = 6.0f;                          // 0x00646284
constexpr float kBounceReach = 240.0f;                    // 0x00646558
constexpr float kLightTime = 0.02f;                       // 0x3CA3D70A
constexpr float kSpeedToSoundFraction = 0.001f;           // 0x006461D8

constexpr int kStateAir = 1, kStateLip = 3, kStateRail = 4;
// Face flags (as the level editor names them)
constexpr uint16_t kFaceSkatable = 0x1, kFaceNotSkatable = 0x2, kFaceWallride = 0x4, kFaceVert = 0x8;

template <typename T>
T& prop(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// A physics value of the skater (GameObjects::read_tuning_float, asserting).
double get_physics(GameEntity* skater, uint32_t name) { return read_tuning_float(name, 1, skater); }

// m = rotation * m (D3DXMatrixMultiply, linked library code).
void turn_by(maths::Mat4& m, const maths::Mat4& rotation) { d3dx::matrix_multiply(&m, &rotation, &m); }

// The C runtime's acos (0x00623630) as its x87 code computes it: atan2(sqrt((1 + x)(1 - x)), x).
double x87_acos(double x)
{
    double result;
    __asm {
        fld x
        fld1
        fadd st, st(1)
        fld1
        fsub st, st(2)
        fmulp st(1), st
        fsqrt
        fxch st(1)
        fpatan
        fstp result
    }
    return result;
}

} // namespace

// 0x005147F0
ObjectModule* __cdecl RiderRemoteSync::fabricate() { return new RiderRemoteSync; }

// 0x005144E0
RiderRemoteSync::RiderRemoteSync()
{
    past_frames_ = nullptr;
    pose_ = nullptr;
    run_over_ = nullptr;
    anim_blender_ = nullptr;
    flip_and_rotate_ = nullptr;
    mesh_set_ = nullptr;
    shadow_ = nullptr;
    play_idx_ = 0;
    kind_tag_ = kSkaterNonLocalNetLogic;
    play_pos_ = {0.0f, 0.0f, 0.0f, 1.0f};
    last_play_pos_ = {0.0f, 0.0f, 0.0f, 1.0f};
    updates_ = 0;
    last_event_time_ = 0;
}

// 0x005144B0 (0x00514550 the deleting one)
RiderRemoteSync::~RiderRemoteSync() {}

// 0x00514580
void RiderRemoteSync::seal()
{
    past_frames_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterStateHistory));
    pose_ = static_cast<RiderPoseState*>(entity_->fetch_module(kSkaterState));
    run_over_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterEndRun));
    anim_blender_ = entity_->fetch_module(kAnimBlender);
    flip_and_rotate_ = entity_->fetch_module(kSkaterFlipAndRotate);
    mesh_set_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kModel));
    shadow_ = entity_->fetch_module(kShadow);
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
    feeler.make_new();
    const maths::Vec4& pos = entity_->at_point_;
    const maths::Vec4& up = entity_->basis_frame_.row[1];
    maths::Vec4& start = feeler.line_.start;
    maths::Vec4& end = feeler.line_.end;
    start = pos;
    end = pos;
    // (from just above the skater: a thousandth of the up row, then 8 up; down 4800)
    start.x = static_cast<float>(static_cast<double>(up.x) * kLightingRise + start.x);
    start.y = static_cast<float>(static_cast<double>(up.y) * kLightingRise + start.y);
    start.z = static_cast<float>(static_cast<double>(up.z) * kLightingRise + start.z);
    start.w = static_cast<float>(static_cast<double>(up.w) * kLightingRise + start.w);
    start.y = static_cast<float>(static_cast<double>(start.y) + kLightingAbove);
    end.y = static_cast<float>(static_cast<double>(end.y) - kLightingBelow);
    if (!feeler.get_hit_shape(true, false))
        return;
    if (pose_->cur_phase_ != kStateRail && feeler.is_hit_triangle_collision()) {
        // The model's light from the face's vertex values (the model's light object, its +0x2C - read through the
        // folded getter 0x00532EB0; else the global).
        void* model = prop<void*>(mesh_set_, 0x208);
        void* light = prop<void*>(model, 0x2C);
        if (light)
            static_cast<render::ModelLights*>(light)->move_brightness(static_cast<float>(feeler.get_hit_vertex_value()),
                                                                      kLightTime);
        else
            render::set_world_brightness(static_cast<float>(feeler.get_hit_vertex_value()));
    }
    // The shadow at the hit.
    static_cast<GroundShadowModule*>(shadow_)->change_place(&feeler.contact_pos_);
    static_cast<GroundShadowModule*>(shadow_)->set_normal(&feeler.surface_dir_);
}

// 0x00514850
void RiderRemoteSync::settle_onto_floor()
{
    render::ProbeRay feeler;
    feeler.make_new();
    GameEntity* skater = entity_;
    const maths::Vec4& up = skater->basis_frame_.row[1];
    const maths::Vec4& pos = skater->at_point_;
    // From Physics_Ground_Snap_Up above (the product unrounded) to 200 below (rounded first).
    double snap_up = get_physics(skater, kGroundSnapUp);
    maths::Vec4 start, end;
    start.x = static_cast<float>(snap_up * up.x + pos.x);
    start.y = static_cast<float>(snap_up * up.y + pos.y);
    start.z = static_cast<float>(snap_up * up.z + pos.z);
    start.w = static_cast<float>(snap_up * up.w + pos.w);
    float down_w = static_cast<float>(static_cast<double>(up.w) * kSnapBelow);
    float down_z = static_cast<float>(static_cast<double>(up.z) * kSnapBelow);
    float down_y = static_cast<float>(static_cast<double>(up.y) * kSnapBelow);
    float down_x = static_cast<float>(static_cast<double>(up.x) * kSnapBelow);
    end.x = static_cast<float>(static_cast<double>(down_x) + pos.x);
    end.y = static_cast<float>(static_cast<double>(down_y) + pos.y);
    end.z = static_cast<float>(static_cast<double>(down_z) + pos.z);
    end.w = static_cast<float>(static_cast<double>(down_w) + pos.w);
    feeler.change_strand(&start, &end);
    feeler.set_excluded(0x10, 0);
    if (!feeler.get_hit_shape(true, false))
        return;

    const maths::Vec4& point = feeler.contact_pos_;
    const maths::Vec4& normal = feeler.surface_dir_;
    double dz = static_cast<double>(pos.z) - point.z;
    double dy = static_cast<double>(pos.y) - point.y;
    double dx = static_cast<double>(pos.x) - point.x;
    float distance = static_cast<float>(std::sqrt((dx * dx + dy * dy) + dz * dz));
    // How far above the hit the skater is, along its up row.
    float height = static_cast<float>((dy * up.y + dz * up.z) + dx * up.x);

    // (only its low byte is tested: skatable, vert, not "not skatable" and not wallride)
    uint8_t flags = static_cast<uint8_t>(feeler.hit_.face_flags_);
    bool skatable =
        (flags & kFaceSkatable) && !(flags & kFaceNotSkatable) && (flags & kFaceVert) && !(flags & kFaceWallride);
    if (skatable && !(skater::x87_sin(static_cast<double>(script::fetch_float(kWallNonSkatableAngle)) *
                                      kRadiansPerDegree) > normal.y)) {
        // Ground: unless it turns the skater's heading too far from its last ground's, close enough to snap to.
        const maths::Vec4 ground = normal;
        const maths::Vec4& at_row = skater->basis_frame_.row[2];
        maths::Vec4 on_ground = at_row;
        float facing =
            static_cast<float>((static_cast<double>(normal.x) * at_row.x + static_cast<double>(normal.z) * at_row.z) +
                               static_cast<double>(at_row.y) * normal.y);
        maths::Vec4 on_last = at_row;
        on_ground.align_to_plane(ground);
        on_last.align_to_plane(surface_up_);
        float turn = static_cast<float>(
            (static_cast<double>(on_last.x) * on_ground.x + static_cast<double>(on_last.z) * on_ground.z) +
            static_cast<double>(on_last.y) * on_ground.y);
        float stick =
            static_cast<float>(skater::x87_cos(get_physics(skater, kGroundStickAngleForward) * kRadiansPerDegree));
        if (facing > 0.0f && turn > 0.0f && turn < stick)
            return;
        if (height > 0.0f) {
            // Above it: only within this frame's move times the slope between the two grounds (at least
            // Physics_Ground_Snap_Down).
            float angle = static_cast<float>(x87_acos(turn));
            maths::Vec4 move = skater->at_point_ - last_pos_;
            float slope = static_cast<float>(skater::x87_tan(angle));
            // (its length unrounded, as Vec4::len_of's x87 code leaves it)
            double length = std::sqrt((static_cast<double>(move.x) * move.x + static_cast<double>(move.y) * move.y) +
                                      static_cast<double>(move.z) * move.z);
            float reach = static_cast<float>(length * slope);
            double snap_down = get_physics(skater, kGroundSnapDown);
            if (reach < snap_down)
                reach = static_cast<float>(snap_down);
            if (distance > reach)
                return;
        }
        surface_up_ = ground;
        skater->basis_frame_.row[1] = ground;
        skater->basis_frame_.square_up_axes(1);
        skater->at_point_ = point;
        return;
    }
    // Not ground: if the skater is (nearly) at or below it, out by one along its normal.
    if (height < kOutOfGround) {
        skater->at_point_.x = static_cast<float>(static_cast<double>(normal.x) + point.x);
        skater->at_point_.y = static_cast<float>(static_cast<double>(point.y) + normal.y);
        skater->at_point_.z = static_cast<float>(static_cast<double>(normal.z) + point.z);
        skater->at_point_.w = static_cast<float>(static_cast<double>(point.w) + normal.w);
    }
}

// 0x00514D10
double RiderRemoteSync::turn_towards(maths::Vec4 dir, float* turn)
{
    const maths::Vec4& right = entity_->basis_frame_.row[0];
    float cosine = static_cast<float>((static_cast<double>(dir.z) * right.z + static_cast<double>(dir.y) * right.y) +
                                      static_cast<double>(dir.x) * right.x);
    if (cosine > 1.0f)
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    double exact = x87_acos(cosine);
    float angle = static_cast<float>(exact);
    if (exact > kHalfPi) // (the unrounded angle compared; the rounded one adjusted)
        angle = static_cast<float>(static_cast<double>(angle) - kPi);
    double rotation = static_cast<double>(script::fetch_float(kWallBounceAngleMultiplier)) * angle;
    *turn = static_cast<float>(rotation);
    // The velocity turned about y (sine / cosine unrounded), then the matrix by the rounded turn.
    double s = skater::x87_sin(rotation), c = skater::x87_cos(rotation);
    maths::Vec4& vel = entity_->vel_;
    double vx = vel.x, vz = vel.z;
    vel.x = static_cast<float>(s * vz + c * vx);
    vel.z = static_cast<float>(-(vx * s) + c * vz);
    maths::Mat4 rotate;
    maths::make_yaw_matrix(rotate, *turn);
    turn_by(entity_->basis_frame_, rotate);
    return angle;
}

// 0x00514E20
void RiderRemoteSync::deflect_from_wall(const maths::Vec4* normal, const maths::Vec4* point)
{
    GameEntity* skater = entity_;
    float away_z = static_cast<float>(static_cast<double>(skater->at_point_.z) - last_pos_.z);
    float away_y = static_cast<float>(static_cast<double>(skater->at_point_.y) - last_pos_.y);
    float away_x = static_cast<float>(static_cast<double>(skater->at_point_.x) - last_pos_.x);
    render::ProbeRay feeler;
    feeler.make_new();
    // The collision height up the up row (the products rounded).
    double height = get_physics(skater, kFirstForwardCollisionHeight);
    const maths::Vec4& up = skater->basis_frame_.row[1];
    float raise_w = static_cast<float>(height * up.w);
    float raise_z = static_cast<float>(height * up.z);
    float raise_y = static_cast<float>(height * up.y);
    float raise_x = static_cast<float>(height * up.x);
    float turn;
    float angle = static_cast<float>(turn_towards(*normal, &turn));
    // Slowed by how far past Wall_Bounce_Dont_Slow_Angle it turned (to nothing at a right angle).
    double dont_slow = static_cast<double>(script::fetch_float(kWallBounceDontSlowAngle)) * kRadiansPerDegree;
    double magnitude = std::fabs(static_cast<double>(angle));
    if (magnitude > dont_slow) {
        double keep = 1.0 - (magnitude - dont_slow) / (kHalfPi - dont_slow);
        maths::Vec4& vel = skater->vel_;
        vel.x = static_cast<float>(keep * vel.x);
        vel.y = static_cast<float>(keep * vel.y);
        vel.z = static_cast<float>(keep * vel.z);
        vel.w = static_cast<float>(keep * vel.w);
    }
    // Back at the wall's point, the height below it, 6 out along the normal.
    maths::Vec4& pos = skater->at_point_;
    pos.x = static_cast<float>(static_cast<double>(point->x) - raise_x);
    pos.y = static_cast<float>(static_cast<double>(point->y) - raise_y);
    pos.z = static_cast<float>(static_cast<double>(point->z) - raise_z);
    pos.w = static_cast<float>(static_cast<double>(point->w) - raise_w);
    pos.x = static_cast<float>(static_cast<double>(normal->x) * kWallOut + pos.x);
    pos.y = static_cast<float>(static_cast<double>(normal->y) * kWallOut + pos.y);
    pos.z = static_cast<float>(static_cast<double>(normal->z) * kWallOut + pos.z);
    pos.w = static_cast<float>(static_cast<double>(normal->w) * kWallOut + pos.w);

    // A feeler from twice the height up, along the velocity as far as the skater went this frame (then the
    // collision length further).
    double moved = std::sqrt((static_cast<double>(away_x) * away_x + static_cast<double>(away_y) * away_y) +
                             static_cast<double>(away_z) * away_z);
    maths::Vec4 dir = skater->vel_;
    double speed = std::sqrt((static_cast<double>(dir.z) * dir.z + static_cast<double>(dir.y) * dir.y) +
                             static_cast<double>(dir.x) * dir.x);
    if (speed > 0.0) {
        double inverse = 1.0 / speed;
        dir.x = static_cast<float>(dir.x * inverse);
        dir.y = static_cast<float>(dir.y * inverse);
        dir.z = static_cast<float>(dir.z * inverse);
    }
    float ahead_w = static_cast<float>(dir.w * moved);
    float ahead_z = static_cast<float>(dir.z * moved);
    float ahead_y = static_cast<float>(dir.y * moved);
    float ahead_x = static_cast<float>(dir.x * moved);
    maths::Vec4 from;
    from.x = static_cast<float>((static_cast<double>(raise_x) + pos.x) + raise_x);
    from.y = static_cast<float>((static_cast<double>(raise_y) + pos.y) + raise_y);
    from.z = static_cast<float>((static_cast<double>(raise_z) + pos.z) + raise_z);
    from.w = static_cast<float>((static_cast<double>(raise_w) + pos.w) + raise_w);
    double length = get_physics(skater, kFirstForwardCollisionLength);
    float reach_w = static_cast<float>(dir.w * length);
    float reach_z = static_cast<float>(dir.z * length);
    float reach_y = static_cast<float>(dir.y * length);
    float reach_x = static_cast<float>(length * dir.x);
    float base_y = static_cast<float>(static_cast<double>(raise_y) + pos.y);
    float base_x = static_cast<float>(static_cast<double>(raise_x) + pos.x);
    float mid_w = static_cast<float>((static_cast<double>(raise_w) + pos.w) + ahead_w);
    float mid_z = static_cast<float>((static_cast<double>(raise_z) + pos.z) + ahead_z);
    float mid_y = static_cast<float>(static_cast<double>(base_y) + ahead_y);
    float mid_x = static_cast<float>(static_cast<double>(base_x) + ahead_x);
    maths::Vec4 to;
    double to_w = static_cast<double>(mid_w) + reach_w;
    double to_z = static_cast<double>(mid_z) + reach_z;
    to.y = static_cast<float>(static_cast<double>(mid_y) + reach_y);
    to.x = static_cast<float>(static_cast<double>(mid_x) + reach_x);
    to.z = static_cast<float>(to_z);
    to.w = static_cast<float>(to_w);
    double ex = static_cast<double>(from.x) - to.x, ey = static_cast<double>(from.y) - to.y,
           ez = static_cast<double>(from.z) - to_z;
    if (std::sqrt((ex * ex + ey * ey) + ez * ez) > kBounceReach)
        return;
    feeler.change_strand(&from, &to);
    feeler.set_excluded(0x10, 0);
    if (!feeler.get_hit_shape(true, false) || !skater->is_local_skater_)
        return;
    // Another wall: turned round by what's left of a half turn, and halved.
    double around = static_cast<double>(kPi) - turn;
    float s = static_cast<float>(skater::x87_sin(around));
    float c = static_cast<float>(skater::x87_cos(around));
    maths::Vec4& vel = skater->vel_;
    double vx = vel.x, vz = vel.z;
    double nx = vx * c + vz * s;
    double nz = -(vx * s) + vz * c;
    vel.x = static_cast<float>(nx);
    vel.z = static_cast<float>(nz);
    maths::Mat4 rotate;
    maths::make_yaw_matrix(rotate, static_cast<float>(around));
    turn_by(skater->basis_frame_, rotate);
    vel.x = static_cast<float>(vel.x * 0.5);
    vel.y = static_cast<float>(vel.y * 0.5);
    vel.z = static_cast<float>(vel.z * 0.5);
    vel.w = static_cast<float>(vel.w * 0.5);
}

// 0x00517270
void RiderRemoteSync::tick_frame()
{
    auto* net_manager = online::g_online_hub;
    frame_seconds_ = static_cast<float>(frame_clock::get_frame_seconds());
    int state = pose_->cur_phase_;
    bool was_lip = state == kStateLip;
    if (state != kStateAir) {
        // (on the ground the state's two vectors follow the up row)
        pose_->drawn_normal_ = entity_->basis_frame_.row[1];
        pose_->floor_normal_ = entity_->basis_frame_.row[1];
    }
    update_ground_lighting();
    last_pos_ = entity_->at_point_;
    entity_->old_pos_ = entity_->at_point_;
    interpolate();
    // Extrapolated when this machine plays (not observing or waiting) in a game that wants it.
    auto* player = static_cast<online::PlayerRecord*>(net_manager->get_own_player());
    if (!player->is_observer() && !player->is_scouting_level()) {
        void* game_mode = static_cast<void*>(goals::g_game_mode);
        bool extrapolate = online::g_online_hub->players_can_collide() ||
                           static_cast<goals::MatchRules*>(game_mode)->get_name_key() == kNetFirefight ||
                           static_cast<goals::MatchRules*>(game_mode)->get_name_key() == kFirefight;
        if (extrapolate && prop<int>(past_frames_, 0x18) >= kHistory) {
            if (pose_->cur_phase_ == kStateRail) {
                extrapolate_on_rail();
            } else {
                get_extrapolate();
                settle_onto_floor();
            }
        }
    }
    play_history_events();

    // Out of a lip straight up (as the finalize physics component's).
    if (was_lip && pose_->cur_phase_ != kStateLip) {
        pose_->lip_straight_up_ = 0;
        if (pose_->cur_phase_ == kStateAir && std::fabs(entity_->vel_.x) < 1.0f && std::fabs(entity_->vel_.z) < 1.0f)
            pose_->lip_straight_up_ = 1;
    }
    if (pose_->cur_phase_ != kStateAir)
        pose_->lip_straight_up_ = 0;

    // Driving in or out: the scripts told (with the vehicle's control type when it starts).
    uint8_t driving = pose_->in_vehicle_;
    uint32_t steer_kind = prop<uint32_t>(past_frames_, 0x1104);
    if ((driving && steer_kind) || (!driving && was_driving_)) {
        script::Struct* params = script::spawn_param_bag();
        if (params)
            params = params->prepare_all();
        params->put_name_key(kSkaterId, entity_->ident_);
        uint32_t script = kStopDrivingScript;
        if (driving) {
            params->put_name_key(kControlType, steer_kind);
            script = kStartDrivingScript;
        }
        script::launch_script(script, params, nullptr, false, nullptr);
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
    was_driving_ = pose_->in_vehicle_;

    // The looping sound: on while skating, its speed and state.
    auto* sound = static_cast<RiderRollingAudio*>(entity_->fetch_module(kSkaterLoopingSound));
    if (pose_->physics_mode_ != 0) {
        if (sound->on_) {
            sound->hold_off(true);
            sound->on_ = 0;
        }
        return;
    }
    if (sound->on_ != 1) {
        sound->hold_off(false);
        sound->on_ = 1;
    }
    double vz = entity_->vel_.z, vx = entity_->vel_.x;
    sound->set_pace_ratio(static_cast<float>(std::sqrt(vx * vx + vz * vz) * kSpeedToSoundFraction));
    uint8_t changed = sound->sound_changed_;
    changed |= sound->cur_phase_ != pose_->cur_phase_ ? 1 : 0;
    sound->cur_phase_ = pose_->cur_phase_;
    changed |= sound->surface_type_ != pose_->surface_type_ ? 1 : 0;
    sound->sound_changed_ = changed;
    sound->surface_type_ = pose_->surface_type_;
    sound->no_rail_sound_ = pose_->flag(PoseFlag::kBailing).on;
    changed |= sound->sliding_ != pose_->flag(PoseFlag::kSlide).on ? 1 : 0;
    sound->sound_changed_ = changed;
    sound->sliding_ = pose_->flag(PoseFlag::kSlide).on;
}

} // namespace objects

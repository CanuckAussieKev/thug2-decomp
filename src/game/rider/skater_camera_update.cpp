// The skater camera component's frame (0x004FA860): where it looks (built from the skater's state), slerped
// toward that, then placed behind and above the skater and kept clear of the world.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/gfx/view_cam.h"
#include "platform/d3dx.h"
#include "engine/maths/mat4.h"
#include "engine/maths/rotation_blend.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/camera_util.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/rider.h"
#include "game/rider/skater_camera_component.h"
#include "platform/stopwatch.h"
#include "game/rider/core_physics.h"
#include "engine/objects/movable_contact.h"
#include "game/rider/skater_state.h"
#include "game/rider/physics_control.h"
#include "engine/objects/camera_component.h"
#include "game/rider/balance_trick.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {
float g_balance_roll_scale{-0.00012783174f};
float g_air_tilt_rate{0.011635667f};
float g_tilt_recovery{0.046542667f};
float g_lip_turn;

constexpr uint32_t kCamera = 0xC4E311FA;       // Camera (a component)
constexpr uint32_t kCorePhysics = 0x5BD63B29;  // SkaterCorePhysics (a component)
constexpr uint32_t kBalanceTrick = 0x589E5FB3; // SkaterBalanceTrick (a component)
constexpr uint32_t kHideFlagBit = 0x16B8E4CB;  // (a script value: the skater's script flag hidden by a lip view)

// The game's constants (and tunable globals).
constexpr float kTwoPi = 6.2831854820251465f; // 0x0064545C
constexpr float kTurnedAngle = 0.02f;         // 0x006492BC
constexpr float kSlowHorizontal = 0.1f;       // 0x00645D74
constexpr float kAirLookDown = 0.7f;          // 0x0064B2C8
constexpr float kGroundLookDown = 0.8f;       // 0x0064BB44
constexpr float kFramesPerSecond = 60.0f;     // 0x00645EA0
constexpr uint32_t kAirTiltMax = 0x3EB2B94E;  // (float bits: about 0.349, 20 degrees)
constexpr float kAirTiltMaxValue = 0.3490700125694275f;
constexpr float kAligned = 0.9999f;            // 0x0064B7F4
constexpr float kCosineDecay = 0.9998f;        // 0x0064BB40
constexpr float kLipBack = 3.0f;               // 0x006467A8
constexpr float kLipReach = 72.0f;             // 0x00646244
constexpr float kHalfPi = 1.5707963705062866f; // 0x3FC90FDB
float& balance_roll_scale() { return g_balance_roll_scale; }
float& air_tilt_rate() { return g_air_tilt_rate; }
float& tilt_recovery() { return g_tilt_recovery; }
float& lip_turn() { return g_lip_turn; }

bool vert_air(const objects::RiderPoseState* state)
{
    return state->flag(PoseFlag::kVertAir).on && !state->flag(PoseFlag::kLeftVert).on &&
           !state->flag(PoseFlag::kAcidDrop).on;
}

maths::Mat4* skater_display_matrix(GameEntity* skater) { return core::down_cast<Rider>(skater)->get_draw_matrix(); }
// D3DXMatrixMultiply (the library's, 0x005BFA8C): out = a * b.
void get_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// a x b (the original's cross product, inlined: each component a difference of two products) into `out`; w 0.
void perpendicular(maths::Vec4& out, const maths::Vec4& a, const maths::Vec4& b)
{
    const double x = static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y;
    const double y = static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z;
    const double z = static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x;
    out.w = 0.0f;
    out.x = static_cast<float>(x);
    out.y = static_cast<float>(y);
    out.z = static_cast<float>(z);
}

// The rows' agreement: (z * z' + y * y') + x * x'.
double dot_zyx(const maths::Vec4& a, const maths::Vec4& b)
{
    return (static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y) + static_cast<double>(a.x) * b.x;
}

} // namespace

// 0x004FA860
void RiderChaseView::tick_frame()
{
    auto* skater = rider_ref_;
    if (!skater)
        return;
    const RiderBoardPhysics* control = control_;
    if (control && control->pose_->in_vehicle_) {
        // Driving: the camera object not updated.
        entity_->composite_flags_ |= 1;
        return;
    }
    auto* camera_component = entity_->fetch_module(kCamera);
    bool instant = false;
    if ((rider_ref_->composite_flags_ >> 3) & 1) {
        instant = true;
        instant_frames_ = 3;
    }
    if (instant_frames_ > 0) {
        --instant_frames_;
        instant = true;
    }
    auto seconds = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    update_lerps();
    gfx::ViewCam* camera = core::down_cast<ViewpointModule>(camera_component)->view_camera_.get();
    camera->remember_prev_pos();
    maths::Mat4& view = camera_component->entity_->basis_frame_;
    std::memcpy(&view, orientation_.data(), sizeof(orientation_));
    const float last_facing_z = core::at(view.row, 2).z;
    const float last_facing_y = core::at(view.row, 2).y;
    const float last_facing_x = core::at(view.row, 2).x;
    const objects::RiderPoseState* state = state_ref_;
    const int physics_state = state->cur_phase_;
    auto* core = core::down_cast<skater::SkaterPhysicsModule>(rider_ref_->fetch_module(kCorePhysics));
    bool turned = false;

    // The orientation to look along: the skater's (its display matrix), or on a wall ride and while +0x2EC holds
    // it, the last one; a wall ride notes how far the wall's facing turned.
    maths::Mat4 orient{};
    if (physics_state != 2 && (!core || !core->point_rail_spin_)) {
        wall_ride_frames_ = 0;
        skater = rider_ref_;
        orient = skater->is_local_skater_ ? *skater_display_matrix(skater) : skater->display_matrix_;
        skater_matrix_ = orient;
    } else {
        orient = skater_matrix_;
        if (physics_state == 2) {
            skater = rider_ref_;
            const maths::Vec4& facing =
                skater->is_local_skater_ ? core::at(skater_display_matrix(skater)->row, 2) : core::at(skater->display_matrix_.row, 2);
            if (wall_ride_frames_ == 0) {
                maths::Vec4& wall = wall_facing_;
                wall = facing;
                wall.y = 0.0f;
                wall.make_unit();
            } else {
                maths::Vec4 flat(facing.x, 0.0f, facing.z, facing.w);
                flat.make_unit();
                const maths::Vec4& wall = wall_facing_;
                double was = std::atan2(static_cast<double>(wall.z), static_cast<double>(wall.x));
                double now = std::atan2(static_cast<double>(flat.z), static_cast<double>(flat.x));
                if (was < 0.0)
                    was += kTwoPi;
                if (now < 0.0)
                    now += kTwoPi;
                if (std::fabs(now - was) > kTurnedAngle)
                    turned = true;
            }
            ++wall_ride_frames_;
        }
    }

    // The roll: on a rail on the skater's model, the balance's lean.
    double roll = 0.0;
    if (physics_state == 4 && rider_ref_->is_local_skater_) {
        auto* balance = core::down_cast<RiderTeeterMoves>(rider_ref_->fetch_module(kBalanceTrick));
        roll = static_cast<double>(balance_roll_scale()) * balance->grind_.lean_;
    }
    float& roll_now = roll_;
    if (roll_now != roll) {
        if (instant)
            roll_now = static_cast<float>(roll);
        else
            roll_now = static_cast<float>((roll - roll_now) * look_rate_ + roll_now);
    }

    // The up for the offsets: the orientation's (off the ground), else the state's ground normal (+0xF8).
    const maths::Vec4 up = physics_state != 0 ? core::at(orient.row, 1) : state->drawn_normal_;

    // The facing from the velocity (or straight down).
    skater = rider_ref_;
    const float velocity_z = skater->vel_.z;
    const float velocity_x = skater->vel_.x;
    const bool look_down = get_look_down();
    if (!(control && control->is_halted_)) {
        const double horizontal =
            std::sqrt(static_cast<double>(velocity_x) * velocity_x + static_cast<double>(velocity_z) * velocity_z);
        bool along = false;
        if (horizontal > kSlowHorizontal)
            along = !look_down;
        else
            along = !look_down && state_ref_->flag(PoseFlag::kAcidDrop).on;
        const bool skip = !(horizontal > kSlowHorizontal) && !look_down && !state_ref_->flag(PoseFlag::kAcidDrop).on;
        if (!skip) {
            maths::Vec4& right = core::at(orient.row, 0);
            maths::Vec4& up_row = core::at(orient.row, 1);
            maths::Vec4& facing = core::at(orient.row, 2);
            if (along) {
                skater = rider_ref_;
                facing = skater->vel_;
                objects::RiderPoseState* s = state_ref_;
                if (s->flag(PoseFlag::kAcidDrop).on) {
                    facing.x = 0.0f;
                    facing.z = 0.0f;
                    facing += s->drop_velocity_;
                }
                facing.make_unit();
                double ux = 0.0, uy = 0.0, uz = 0.0, uw = 0.0;
                double k = 0.0;
                if (physics_state == 1) {
                    ux = 0.0, uy = 1.0, uz = 0.0, uw = 1.0;
                    if (s->flag(PoseFlag::kAcidDrop).on && facing.y < 0.0f)
                        k = static_cast<float>(
                            ((static_cast<double>(facing.z) * 0.0 + static_cast<double>(facing.x) * 0.0) + facing.y) *
                            kAirLookDown);
                    else
                        k = facing.y;
                    up_row = maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
                } else if (physics_state == 2) {
                    ux = 0.0, uy = 1.0, uz = 0.0, uw = 1.0;
                    k = facing.y;
                } else {
                    const maths::Vec4& n = s->floor_normal_;
                    ux = n.x, uy = n.y, uz = n.z, uw = n.w;
                    k = static_cast<float>(((ux * facing.x + uy * facing.y) + uz * facing.z) * kGroundLookDown);
                }
                // The facing less its part along that up.
                auto kw = static_cast<float>(uw * k);
                auto kz = static_cast<float>(uz * k);
                const double ky = uy * k;
                const double kx = ux * k;
                facing.x = static_cast<float>(facing.x - kx);
                facing.y = static_cast<float>(facing.y - ky);
                facing.z -= kz;
                facing.w -= kw;
                facing.make_unit();
            } else {
                facing = maths::Vec4(0.0f, -1.0f, 0.0f, 1.0f);
                FreeLookModule* look_around = free_look_module_;
                if (!look_around->locked_ && !look_around->turning_ && physics_state != 3) {
                    look_around->lean_angle_ = 0.0f;
                    free_look_module_->heading_ = 0.0f;
                }
            }
            perpendicular(right, up_row, facing);
            right.make_unit();
            perpendicular(up_row, facing, right);
            up_row.make_unit();
        }
    }
    if (rider_ref_->is_local_skater_ && state_ref_->flag(PoseFlag::kFromAcidDrop).on) {
        auto* physics = core::down_cast<skater::SkaterPhysicsModule>(rider_ref_->fetch_module(kCorePhysics));
        orient = physics->drop_matrix_;
    }

    // The tilt in the air: up to 20 degrees, back to level after.
    float& air_tilt = air_tilt_;
    bool wall_ride = false;
    if (physics_state == 1) {
        const double tilt = frame_clock::get_slomo_frame_seconds() * kFramesPerSecond * air_tilt_rate() + air_tilt;
        air_tilt = static_cast<float>(tilt);
        if (tilt > kAirTiltMaxValue)
            air_tilt = std::bit_cast<float>(kAirTiltMax);
    } else {
        if (air_tilt > 0.0f) {
            const double tilt = air_tilt - frame_clock::get_slomo_frame_seconds() * kFramesPerSecond * tilt_recovery();
            air_tilt = static_cast<float>(tilt);
            if (tilt < 0.0)
                air_tilt = 0.0f;
        } else if (air_tilt < 0.0f) {
            const double tilt = frame_clock::get_slomo_frame_seconds() * kFramesPerSecond * tilt_recovery() + air_tilt;
            air_tilt = static_cast<float>(tilt);
            if (tilt > 0.0)
                air_tilt = 0.0f;
        }
        wall_ride = physics_state == 2;
    }
    maths::Mat4 rotation{};
    FreeLookModule* look_around = free_look_module_;
    bool lip_tilted = false;
    if (!wall_ride) {
        const float heading = look_around->mode_heading_ + look_around->heading_;
        get_multiply(&orient, &maths::make_yaw_matrix(rotation, heading), &orient);
        if (physics_state == 3) {
            get_multiply(&orient, &maths::make_pitch_matrix(rotation, lip_trick_tilt_ + air_tilt_), &orient);
            lip_tilted = true;
        }
    } else {
        if (look_around) {
            look_around->heading_ = 0.0f;
            free_look_module_->locked_ = false;
        }
        if (wallride_offset_angle_ != 0.0f && turned) {
            skater = rider_ref_;
            const maths::Vec4 world_up(0.0f, 1.0f, 0.0f, 1.0f);
            maths::Vec4 across = maths::cross_vec(core::at(skater->basis_frame_.row, 1), world_up);
            across.make_unit();
            const double side = (static_cast<double>(across.z) * core::at(skater->basis_frame_.row, 2).z +
                                 static_cast<double>(across.y) * core::at(skater->basis_frame_.row, 2).y) +
                                static_cast<double>(core::at(skater->basis_frame_.row, 2).x) * across.x;
            const float sign = side < 0.0 ? 1.0f : -1.0f; // (-1 when facing along it, or not a number)
            orient.yaw_locally(static_cast<float>(static_cast<double>(sign) * wallride_offset_angle_));
        }
    }
    if (!lip_tilted) {
        const objects::RiderPoseState* s = state_ref_;
        if (!vert_air(s))
            get_multiply(&orient, &maths::make_pitch_matrix(rotation, lean_angle_ + air_tilt_), &orient);
    }
    if (physics_state != 2 || lip_tilted) {
        const float tilt = free_look_module_->lean_angle_;
        get_multiply(&orient, &maths::make_pitch_matrix(rotation, tilt), &orient);
    }

    // On a lip: looking along it (a quarter turn), or - with something in the way that way - the other way.
    if (physics_state == 3) {
        lip_turn() = kHalfPi;
        int& lip = lip_state_;
        if (lip == 0) {
            orient.rotate_y(kHalfPi);
            render::ProbeRay feeler;
            maths::Vec4 side(core::at(orient.row, 2).x, 0.0f, core::at(orient.row, 2).z, 1.0f);
            side.make_unit();
            const maths::Vec4& position = rider_ref_->at_point_;
            const maths::Vec4& facing = core::at(orient.row, 2);
            const double back_w = static_cast<double>(facing.w) * kLipBack;
            const double back_z = static_cast<double>(facing.z) * kLipBack;
            auto back_y = static_cast<float>(facing.y * static_cast<double>(kLipBack));
            auto back_x = static_cast<float>(facing.x * static_cast<double>(kLipBack));
            auto start_w = static_cast<float>(position.w - back_w);
            const double start_z = position.z - back_z;
            const float origin_coord_y = position.y - back_y;
            const maths::Vec4 start(position.x - back_x, origin_coord_y, static_cast<float>(start_z), start_w);
            const double reach_w = static_cast<double>(side.w) * kLipReach;
            auto reach_z = static_cast<float>(side.z * static_cast<double>(kLipReach));
            auto reach_y = static_cast<float>(side.y * static_cast<double>(kLipReach));
            auto reach_x = static_cast<float>(side.x * static_cast<double>(kLipReach));
            const maths::Vec4 end(start.x - reach_x, start.y - reach_y, static_cast<float>(start_z) - reach_z,
                                  static_cast<float>(start_w - reach_w));
            feeler.set_excluded(0x90, 0);
            if (feeler.get_collision_along(&start, &end, true)) {
                const float first_distance = feeler.reach_;
                orient.rotate_y(lip_turn() * -2.0f);
                maths::Vec4 other(core::at(orient.row, 2).x, 0.0f, core::at(orient.row, 2).z, 1.0f);
                other.make_unit();
                const maths::Vec4 back = rider_ref_->at_point_ - core::at(orient.row, 2) * kLipBack;
                const maths::Vec4 other_end = back - other * kLipReach;
                if (!feeler.get_collision_along(&back, &other_end, true) ||
                    !(feeler.reach_ >= first_distance || std::isnan(feeler.reach_))) {
                    lip = 2;
                } else {
                    orient.rotate_y(lip_turn() * 2.0f);
                    lip = 1;
                }
            } else {
                lip = 1;
            }
        } else if (lip == 1) {
            orient.rotate_y(kHalfPi);
        } else {
            orient.rotate_y(-kHalfPi);
        }
    } else {
        lip_state_ = 0;
    }
    // Blocked on a lip: the skater's script flag (bit 0x16B8E4CB) set, else cleared.
    {
        auto* s = rider_ref_;
        if (s && lip_state_ == 2)
            s->flag_bits_ |= 1u << (script::fetch_int_value(kHideFlagBit) & 31); // (SHL: the count's low 5 bits)
        else
            rider_ref_->flag_bits_ &= ~(1u << (script::fetch_int_value(kHideFlagBit) & 31));
    }

    // The view looking along the orientation (right and at reversed), slerped from the camera's.
    maths::Mat4 goal{};
    goal.set_from_euler_angles(0.0f, 0.0f, 0.0f);
    core::at(goal.row, 0) = maths::Vec4(-core::at(orient.row, 0).x, -core::at(orient.row, 0).y, -core::at(orient.row, 0).z, 0.0f);
    core::at(goal.row, 1) = maths::Vec4(core::at(orient.row, 1).x, core::at(orient.row, 1).y, core::at(orient.row, 1).z, 0.0f);
    core::at(goal.row, 2) = maths::Vec4(-core::at(orient.row, 2).x, -core::at(orient.row, 2).y, -core::at(orient.row, 2).z, 0.0f);
    core::at(goal.row, 3) = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    core::at(view.row, 3) = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    if (instant)
        view = goal;
    auto* slerp = arc_blender_;
    const bool aligned =
        (static_cast<double>(core::at(goal.row, 0).x) * core::at(view.row, 0).x + static_cast<double>(core::at(goal.row, 0).y) * core::at(view.row, 0).y +
         static_cast<double>(core::at(goal.row, 0).z) * core::at(view.row, 0).z) > kAligned &&
        dot_zyx(core::at(goal.row, 1), core::at(view.row, 1)) > kAligned && dot_zyx(core::at(goal.row, 2), core::at(view.row, 2)) > kAligned;
    if (!aligned) {
        slerp->set_matrices(view, goal);
        const maths::Mat4 from = view;
        view = goal;
        if (vert_air(state_ref_)) {
            slerp->fetch_mat(view, static_cast<float>(frame_blend(vert_air_slerp_, seconds)));
            settle_time_ = 1.0f / 6.0f;
        } else if (state_ref_->cur_phase_ == 0 && settle_time_ >= seconds) {
            settle_time_ = static_cast<float>(settle_time_ - static_cast<double>(seconds));
            slerp->fetch_mat(view, static_cast<float>(frame_blend(vert_air_landed_slerp_, seconds)));
        } else {
            settle_time_ = 0.0f;
            slerp->fetch_mat(view, static_cast<float>(frame_blend(slerp_, seconds)));
            // How far the facing turned this frame: a sharper turn than the last slows the slerp from the last view.
            const double turn = (static_cast<double>(last_facing_z) * core::at(view.row, 2).z +
                                 static_cast<double>(last_facing_x) * core::at(view.row, 2).x) +
                                static_cast<double>(last_facing_y) * core::at(view.row, 2).y;
            const double limit = static_cast<double>(turn_cosine_) * kCosineDecay;
            if (turn >= limit || turn != turn) {
                turn_cosine_ = static_cast<float>(turn);
            } else {
                auto rate = static_cast<float>(turn / limit * slerp_);
                slerp->set_matrices(from, goal);
                slerp->fetch_mat(view, static_cast<float>(frame_blend(rate, seconds)));
                turn_cosine_ = static_cast<float>(turn_cosine_ * static_cast<double>(kCosineDecay));
            }
        }
    }
    std::memcpy(orientation_.data(), &view, sizeof(orientation_));

    // Where: the target, behind along the view and above along the up; looking at it from above the skater.
    maths::Vec4 target{};
    fetch_goal(&target, instant);
    float height = 0.0f, distance = 0.0f;
    if (instant) {
        const float rate = zoom_ease_rate_;
        zoom_ease_rate_ = 1.0f;
        get_zoomed_offsets(&height, &distance);
        zoom_ease_rate_ = rate;
    } else {
        get_zoomed_offsets(&height, &distance);
    }
    {
        const maths::Vec4& at = core::at(view.row, 2);
        const double bx = static_cast<double>(distance) * at.x;
        const double by = static_cast<double>(distance) * at.y;
        auto bz = static_cast<float>(static_cast<double>(distance) * at.z);
        auto bw = static_cast<float>(static_cast<double>(distance) * at.w);
        target.x = static_cast<float>(target.x + bx);
        target.y = static_cast<float>(target.y + by);
        target.z += bz;
        target.w += bw;
    }
    maths::Vec4 lift{};
    {
        const double ux = static_cast<double>(up.x) * height;
        const double uy = static_cast<double>(height) * up.y;
        const double uz = static_cast<double>(height) * up.z;
        auto uw = static_cast<float>(static_cast<double>(height) * up.w);
        lift = maths::Vec4(static_cast<float>(ux), static_cast<float>(uy), static_cast<float>(uz), uw);
        target.x += lift.x;
        target.y = static_cast<float>(target.y + uy);
        target.z = static_cast<float>(target.z + uz);
        target.w += uw;
    }
    const maths::Vec4& position = rider_ref_->at_point_;
    const double eye_y = static_cast<double>(lift.y) + position.y;
    const double eye_z = static_cast<double>(lift.z) + position.z;
    const maths::Vec4 eye(lift.x + position.x, static_cast<float>(eye_y), static_cast<float>(eye_z),
                          lift.w + position.w);
    maths::Vec4 look{};
    look.w = 1.0f;
    look.x = eye.x - target.x;
    look.y = static_cast<float>(eye_y - target.y);
    look.z = static_cast<float>(eye_z - target.z);
    look.make_unit();
    maths::Vec4 look_up = core::at(view.row, 1);
    maths::Vec4 look_right{};
    perpendicular(look_right, look_up, look);
    look_right.make_unit();
    perpendicular(look_up, look, look_right);
    look_up.make_unit();
    if (roll_ != 0.0f) {
        look_up.rotate_about_axis(look, roll_);
        look_right.rotate_about_axis(look, roll_);
    }
    core::at(view.row, 0) = maths::Vec4(-look_right.x, -look_right.y, -look_right.z, 0.0f);
    core::at(view.row, 1) = maths::Vec4(look_up.x, look_up.y, look_up.z, 0.0f);
    core::at(view.row, 2) = maths::Vec4(-look.x, -look.y, -look.z, 0.0f);
    if (shake_left_ > 0.0f)
        apply_shake(&target, &view);

    // Kept clear of the world (unless riding something on a lip).
    bool avoid = true;
    if (rider_ref_->is_local_skater_) {
        auto* physics = core::down_cast<skater::SkaterPhysicsModule>(rider_ref_->fetch_module(kCorePhysics));
        if (physics->ride_->ride_ && physics_state == 3)
            avoid = false;
    }
    if (avoid) {
        const maths::Vec4& at = core::at(view.row, 2);
        auto bx = static_cast<float>(static_cast<double>(distance) * at.x);
        const double by = static_cast<double>(distance) * at.y;
        const double bz = static_cast<double>(distance) * at.z;
        auto bw = static_cast<float>(static_cast<double>(distance) * at.w);
        const maths::Vec4 focus(target.x - bx, static_cast<float>(target.y - by), static_cast<float>(target.z - bz),
                                target.w - bw);
        avoid_camera_collisions(&target, &view, &focus, &eye, true, true);
    }
    target.w = 1.0f;
    camera_component->entity_->at_point_ = target;
    if (instant)
        core::down_cast<ViewpointModule>(camera_component)->view_camera_->remember_prev_pos();
}

} // namespace objects

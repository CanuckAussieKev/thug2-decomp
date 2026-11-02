// The WalkCamera component (GameObjects::FootView): see walk_camera.h. From the disassembly of
// 0x00524C20-0x00525E3F (and 0x00525F70).

#include "game/rider/walk_camera.h"
#include "platform/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gsl/pointers>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/maths/mat4.h"
#include "engine/maths/rotation_blend.h"
#include "engine/maths/vec4.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/camera_util.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "game/rider/walk_physics.h"
#include "platform/stopwatch.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"
#include "engine/objects/camera_component.h"
#include "game/rider/skater_camera_component.h"
#include "engine/objects/input_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {

constexpr uint32_t kWalkCamera = 0xC7D37EC2;
constexpr uint32_t kWalk = 0x726E85AA, kSkaterPhysicsControl = 0xD9151D4D, kCameraLookAround = 0x81C7122D,
                   kCamera = 0xC4E311FA, kSkaterCamera = 0x5E43A604, kInput = 0x27D7CD28;
constexpr uint32_t kSkaterId = 0x4C48DA58; // (an object id; the name isn't recovered)
constexpr uint32_t kDrainQueueCmd = 0x73FEBD0F, kReset = 0xD1A485D1;
constexpr uint32_t kParameters = 0x1CDA02AE; // walk_camera_parameters
constexpr uint32_t kLockAngle = 0xB8DA8A73, kFullSlerpSpeed = 0xBCEF6DDA, kMinSlerpSpeed = 0x824349E5,
                   kDpadFullSlerpSpeed = 0x73DA1EC0, kDpadMinSlerpSpeed = 0x0DA5CAD3, kFacingControl = 0xF6F69DC8,
                   kMatrixSlerpRate = 0x0C39B639, kRunSlerpFactor = 0xD25348EB, kControlSlerpFactor = 0xB2DA3C87,
                   kFlushSlerpFactor = 0x7178E048;

constexpr float kDegrees = 0.01745329238474369f;  // 0x00645464
constexpr float kAligned = 0.9900000095367432f;   // 0x00645450
constexpr float kNearHeight = 3.0f;               // 0x006467A8
constexpr float kTiltOffset = 0.628000020980835f; // 0x0064BB00
constexpr float kTiltScale = 1.5923566818237305f; // 0x0064BAFC
constexpr float kTiltRange = 0.6000000238418579f; // 0x006463C4
constexpr float kTiltBase = 0.4000000059604645f;  // 0x0064BAF8

// D3DXMatrixMultiply (linked later; 0x005BFA8C).
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// The right stick's turn of the facing (0x00525468-): the sine rounded to float: x' = z * sin + cos * x,
// z' = -(sin * x) + z * cos.
void turn_by_stick(double angle, float* x, float* z)
{
    const double sine = static_cast<float>(std::sin(angle));
    const double cosine = std::cos(angle);
    const double old_x = *x, old_z = *z;
    *x = static_cast<float>(old_z * sine + cosine * old_x);
    *z = static_cast<float>(old_z * cosine - sine * old_x);
}

} // namespace

// 0x00524D70
maths::Vec4* lerp_by_range(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b, float t, float low, float high)
{
    const double dx = static_cast<double>(b->x) - a->x;
    const double dy = static_cast<double>(b->y) - a->y;
    const float dz = b->z - a->z;
    const float dw = b->w - a->w;
    auto f = static_cast<float>((static_cast<double>(t) - low) / (static_cast<double>(high) - low));
    auto sx = static_cast<float>(dx * f);
    const double sy = dy * f;
    const float sz = dz * f;
    const float sw = dw * f;
    const float x = sx + a->x;
    auto y = static_cast<float>(sy + a->y);
    const float z = sz + a->z;
    const float w = sw + a->w;
    out->w = w;
    out->x = x;
    out->y = y;
    out->z = z;
    return out;
}

// 0x00525DC0
gsl::owner<ObjectModule*> FootView::fabricate() { return new FootView; }

// (the constructor: inlined in fabricate in the original)
FootView::FootView()
{
    kind_tag_ = kWalkCamera;
    aim_point_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
}

// 0x00524C20 (0x00524F20 the deleting one)
FootView::~FootView() = default;

// 0x00524C50
void FootView::bind_rider(GameEntity* skater)
{
    if (!skater) {
        skater_ = nullptr;
        walk_ = nullptr;
        physics_control_ = nullptr;
        return;
    }
    skater_ = skater;
    walk_ = core::down_cast<FootMovement>(skater->fetch_module(kWalk));
    physics_control_ = core::down_cast<RiderBoardPhysics>(skater_->fetch_module(kSkaterPhysicsControl));
}

// 0x00524F50
void FootView::configure_from_data(script::Struct* params)
{
    uint32_t id = 0;
    params->read_hash_value(kSkaterId, &id, 1);
    bind_rider(core::down_cast<GameEntity>(object_hub_slot()->look_up_entity(id)));
    turn_cosine_ = 1.0f;
    zoom_ = 1.0f;
    orientation_ = entity_->basis_frame_;
}

// 0x00524FB0
void FootView::seal()
{
    free_look_ = core::down_cast<FreeLookModule>(entity_->fetch_module(kCameraLookAround));
    viewer_ = core::down_cast<ViewpointModule>(entity_->fetch_module(kCamera));
    skater_camera_ = core::down_cast<RiderChaseView>(entity_->fetch_module(kSkaterCamera));
}

// 0x00524C30
void FootView::reset_now()
{
    snap_next_ = true;
    tick_frame();
    snap_next_ = false;
}

// 0x005250A0
void FootView::apply_queued_change()
{
    flush_ = true;
    walk_->locked_to_heading_ = true;
    free_look_->heading_ = 0.0f;
    free_look_->lean_angle_ = 0.0f;
    free_look_->factor_ = 1.0f;
    free_look_->locked_ = false;
}

// 0x00525D40
int FootView::invoke_method_by_name(uint32_t name, script::Struct* /*params*/, script::Script* /*script*/)
{
    if (name == kDrainQueueCmd) {
        apply_queued_change();
        return 1;
    }
    if (name == kReset) {
        reset_now();
        return 1;
    }
    return 2;
}

// 0x00525F70
void FootView::change_forced_heading(const maths::Vec4* facing, float rate)
{
    override_ = true;
    forced_heading_ = *facing;
    override_rate_ = rate;
}

// 0x00525060
void FootView::save_state(WalkCameraState* state)
{
    state->orientation = orientation_;
    state->target = aim_point_;
    state->turn_cosine_ = turn_cosine_;
    state->zoom = zoom_;
}

// 0x00524FF0
void FootView::restore_state(const WalkCameraState* state)
{
    aim_point_ = state->target;
    orientation_ = state->orientation;
    turn_cosine_ = state->turn_cosine_;
    zoom_ = state->zoom;
    flush_ = false;
    walk_->locked_to_heading_ = false;
    instant_frames_ = 0;
    free_look_->heading_ = 0.0f;
    free_look_->locked_ = false;
    override_ = false;
}

// 0x00524EF0
float FootView::get_param(uint32_t name)
{
    float value = 0.0f; // (the original's is uninitialised: the value is asserted)
    script::read_sub_struct(kParameters)->fetch_float(name, &value, 1);
    return value;
}

// 0x005250E0
void FootView::fetch_goal(maths::Vec4* out, bool instant)
{
    if (instant) {
        aim_point_ = skater_->at_point_;
    } else {
        auto across = static_cast<float>(
            frame_blend(skater_camera_->lerp_xz_, static_cast<float>(frame_clock::get_slomo_frame_seconds())));
        auto up = static_cast<float>(
            frame_blend(skater_camera_->lerp_y_, static_cast<float>(frame_clock::get_slomo_frame_seconds())));
        const maths::Vec4& skater = skater_->at_point_;
        const float x = aim_point_.x;
        const float y = aim_point_.y;
        const float z = aim_point_.z;
        aim_point_.w = 1.0f;
        aim_point_.x = static_cast<float>((static_cast<double>(skater.x) - x) * across + x);
        aim_point_.y = static_cast<float>((static_cast<double>(skater.y) - y) * up + y);
        aim_point_.z = static_cast<float>((static_cast<double>(skater.z) - z) * across + z);
    }
    *out = aim_point_;
}

// 0x00524CB0
void FootView::get_zoomed_offsets(float* height, float* distance)
{
    float factor = 1.0f;
    uint32_t factor_bits = 0;
    std::memcpy(&factor_bits, &free_look_->factor_, 4);
    if (free_look_->turning_ && factor_bits != 0x3F800000)
        factor = free_look_->factor_;
    const double zoom = (static_cast<double>(factor) - zoom_) * skater_camera_->zoom_ease_rate_ + zoom_;
    zoom_ = static_cast<float>(zoom);
    const double behind = zoom * skater_camera_->behind_;
    *distance = static_cast<float>(behind);
    const float tilt = free_look_->lean_angle_;
    if (tilt < 0.0f)
        *distance = static_cast<float>(
            (((static_cast<double>(tilt) + kTiltOffset) * kTiltScale) * kTiltRange + kTiltBase) * behind);
    const float above = skater_camera_->above_;
    if (zoom_ < 1.0f)
        *height = static_cast<float>((static_cast<double>(above) - kNearHeight) * zoom_ + kNearHeight);
    else
        *height = above;
}

// 0x005251D0
void FootView::tick_frame()
{
    if (!skater_)
        return;
    if (physics_control_ && physics_control_->pose_->in_vehicle_) {
        entity_->composite_flags_ |= kCompositeFlagPaused;
        return;
    }
    if (skater_->composite_flags_ & kCompositeFlagTeleported)
        instant_frames_ = 3;
    bool instant = false;
    if (instant_frames_ > 0) {
        --instant_frames_;
        instant = true;
    } else {
        instant = snap_next_;
    }
    viewer_->view_camera_->remember_prev_pos();
    auto frame = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    const float turn = core::at(core::down_cast<PadInputModule>(entity_->fetch_module(kInput))->pad_primary_.scaled_stick_, 0);
    entity_->basis_frame_ = orientation_;

    // Where to look: along the camera's flat facing, swung round to the skater's.
    const maths::Vec4& at = core::at(entity_->basis_frame_.row, 2);
    maths::Vec4 facing = {.x = -at.x, .y = 0.0f, .z = -at.z, .w = -at.w};
    facing.make_unit();
    const maths::Vec4& skater_at = core::at(skater_->basis_frame_.row, 2);
    maths::Vec4 skater_facing = {.x = skater_at.x, .y = 0.0f, .z = skater_at.z, .w = skater_at.w};
    skater_facing.make_unit();
    if (flush_) {
        facing = skater_facing;
    } else {
        bool swing = false;
        if (turn == 0.0f) {
            auto cosine = static_cast<float>(static_cast<double>(skater_facing.x) * facing.x +
                                             static_cast<double>(skater_facing.z) * facing.z +
                                             static_cast<double>(skater_facing.y) * facing.y);
            if (!(std::cos(static_cast<double>(get_param(kLockAngle)) * kDegrees) > cosine)) {
                float full = 0.0f, least = 0.0f;
                if (!walk_->using_dpad()) {
                    full = get_param(kFullSlerpSpeed);
                    least = get_param(kMinSlerpSpeed);
                } else {
                    full = get_param(kDpadFullSlerpSpeed);
                    least = get_param(kDpadMinSlerpSpeed);
                }
                const maths::Vec4& velocity = skater_->vel_;
                const double speed = std::sqrt(static_cast<double>(velocity.x) * velocity.x +
                                               static_cast<double>(velocity.z) * velocity.z);
                auto speed_f = static_cast<float>(speed);
                if (!(speed < full)) {
                    swing = true;
                } else if (speed_f > least) {
                    maths::Vec4 lerped{};
                    facing = *lerp_by_range(&lerped, &facing, &skater_facing, speed_f, least, full);
                    facing.make_unit();
                }
            }
        }
        if (swing || snap_next_)
            facing = skater_facing;
    }
    if (!(turn == 0.0f) && !flush_)
        turn_by_stick(static_cast<double>(get_param(kFacingControl)) * turn, &facing.x, &facing.z);
    if (override_) {
        const float x = forced_heading_.x;
        facing.y = forced_heading_.y;
        facing.z = forced_heading_.z;
        facing.w = forced_heading_.w;
        const double heading = free_look_->heading_;
        auto s = static_cast<float>(std::sin(heading));
        auto c = static_cast<float>(std::cos(heading));
        const float z = facing.z;
        auto turned_z = static_cast<float>(-(static_cast<double>(s) * x) + static_cast<double>(z) * c);
        facing.x = static_cast<float>(static_cast<double>(z) * s + static_cast<double>(c) * x);
        facing.z = turned_z;
    }

    // The orientation that looks along it, tilted down; its right and at rows negated.
    maths::Mat4 look{};
    core::at(look.row, 0) = {.x = facing.z, .y = 0.0f, .z = -facing.x, .w = 1.0f};
    core::at(look.row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 1.0f};
    core::at(look.row, 2) = facing;
    core::at(look.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    maths::Mat4 tilt{};
    maths::make_pitch_matrix(tilt, free_look_->lean_angle_ + skater_camera_->lean_angle_);
    matrix_multiply(&look, &tilt, &look);
    core::at(look.row, 0) = {.x = -core::at(look.row, 0).x, .y = -core::at(look.row, 0).y, .z = -core::at(look.row, 0).z, .w = -core::at(look.row, 0).w};
    core::at(look.row, 2) = {.x = -core::at(look.row, 2).x, .y = -core::at(look.row, 2).y, .z = -core::at(look.row, 2).z, .w = -core::at(look.row, 2).w};
    const maths::Vec4 skater_up = core::at(skater_->basis_frame_.row, 1);
    maths::Mat4& matrix = entity_->basis_frame_;
    if (instant) {
        matrix = look;
    } else {
        auto aligned = [](const maths::Vec4& a, const maths::Vec4& b) {
            return static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y + static_cast<double>(a.x) * b.x >
                   kAligned;
        };
        if (aligned(core::at(look.row, 0), core::at(matrix.row, 0)) && aligned(core::at(look.row, 1), core::at(matrix.row, 1)) &&
            aligned(core::at(look.row, 2), core::at(matrix.row, 2))) {
            if (flush_) {
                flush_ = false;
                walk_->locked_to_heading_ = false;
            }
        } else {
            // Slerped toward it: at matrix_slerp_rate, faster running (and twice that hanging or climbing), with
            // the stick, flushing; an override's own rate.
            const maths::ArcRotationBlender slerp(matrix, look);
            float base = 0.0f;
            script::read_sub_struct(kParameters)->fetch_float(kMatrixSlerpRate, &base, 1);
            float rate = base;
            if (walk_->running_)
                rate = static_cast<float>(static_cast<double>(get_param(kRunSlerpFactor)) * base);
            const int state = walk_->state_;
            if (state == 4 || state == 3)
                rate = rate + rate;
            if (!(turn == 0.0f) && !flush_) {
                const float control = get_param(kControlSlerpFactor);
                rate = static_cast<float>(static_cast<double>(control) * std::fabs(turn) * rate);
            }
            if (flush_) {
                walk_->locked_to_heading_ = true;
                rate = static_cast<float>(static_cast<double>(get_param(kFlushSlerpFactor)) * rate);
            }
            if (override_) {
                if (flush_) {
                    const float flush = get_param(kFlushSlerpFactor);
                    rate = static_cast<float>(static_cast<double>(get_param(kMatrixSlerpRate)) * flush);
                } else {
                    rate = override_rate_;
                }
            }
            slerp.fetch_mat(matrix, static_cast<float>(frame_blend(rate, frame)));
            const maths::Vec4& now = core::at(matrix.row, 2);
            const maths::Vec4& before = core::at(orientation_.row, 2);
            turn_cosine_ =
                static_cast<float>(static_cast<double>(now.z) * before.z + static_cast<double>(now.y) * before.y +
                                   static_cast<double>(now.x) * before.x);
        }
    }
    orientation_ = matrix;

    // Where to be: behind and above the point followed, looking at a point above the skater.
    maths::Vec4 target{};
    fetch_goal(&target, instant);
    float height = 0.0f, distance = 0.0f;
    get_zoomed_offsets(&height, &distance);
    const double up_x = static_cast<double>(skater_up.x) * height;
    const double up_y = static_cast<double>(skater_up.y) * height;
    const double up_z = static_cast<double>(skater_up.z) * height;
    const double up_w = static_cast<double>(skater_up.w) * height;
    auto up_xf = static_cast<float>(up_x), up_zf = static_cast<float>(up_z), up_wf = static_cast<float>(up_w);
    const maths::Vec4& look_at = core::at(matrix.row, 2);
    const double back_x = static_cast<double>(distance) * look_at.x;
    const double back_y = static_cast<double>(distance) * look_at.y;
    const float back_z = distance * look_at.z;
    const float back_w = distance * look_at.w;
    auto step_x = static_cast<float>(back_x + up_xf);
    auto step_y = static_cast<float>(back_y + up_y);
    const double step_z = back_z + up_z;
    const double step_w = static_cast<double>(back_w) + up_wf;
    target.x = step_x + target.x;
    target.y = target.y + step_y;
    target.z = static_cast<float>(target.z + step_z);
    target.w = static_cast<float>(target.w + step_w);
    const maths::Vec4& skater_pos = skater_->at_point_;
    const maths::Vec4 focus = {.x = up_xf + skater_pos.x, .y = static_cast<float>(up_y + skater_pos.y), .z = up_zf + skater_pos.z,
                               .w = up_wf + skater_pos.w};

    maths::Mat4 aim{};
    maths::Vec4& aim_at = core::at(aim.row, 2);
    aim_at = {.x = focus.x - target.x, .y = focus.y - target.y, .z = focus.z - target.z, .w = focus.w - target.w};
    aim_at.make_unit();
    const maths::Vec4 up = core::at(matrix.row, 1);
    maths::Vec4& right = core::at(aim.row, 0);
    right = {.x = static_cast<float>(static_cast<double>(up.y) * aim_at.z - static_cast<double>(up.z) * aim_at.y),
             .y = static_cast<float>(static_cast<double>(up.z) * aim_at.x - static_cast<double>(up.x) * aim_at.z),
             .z = static_cast<float>(static_cast<double>(up.x) * aim_at.y - static_cast<double>(up.y) * aim_at.x), .w = 0.0f};
    right.make_unit();
    maths::Vec4& aim_up = core::at(aim.row, 1);
    aim_up = {.x = static_cast<float>(static_cast<double>(right.z) * aim_at.y - static_cast<double>(right.y) * aim_at.z),
              .y = static_cast<float>(static_cast<double>(right.x) * aim_at.z - static_cast<double>(right.z) * aim_at.x),
              .z = static_cast<float>(static_cast<double>(right.y) * aim_at.x - static_cast<double>(right.x) * aim_at.y),
              .w = 0.0f};
    aim_up.make_unit();
    core::at(matrix.row, 0) = {.x = -right.x, .y = -right.y, .z = -right.z, .w = -right.w};
    core::at(matrix.row, 1) = aim_up;
    core::at(matrix.row, 2) = {.x = -aim_at.x, .y = -aim_at.y, .z = -aim_at.z, .w = -aim_at.w};
    core::at(matrix.row, 0).w = 0.0f;
    core::at(matrix.row, 1).w = 0.0f;
    core::at(matrix.row, 2).w = 0.0f;
    core::at(matrix.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};

    // The collision check pulls the camera toward the point `distance` ahead of it (and the walk's offset).
    const double ahead_x = static_cast<double>(distance) * core::at(matrix.row, 2).x;
    const double ahead_y = static_cast<double>(distance) * core::at(matrix.row, 2).y;
    const float ahead_z = distance * core::at(matrix.row, 2).z;
    const float ahead_w = distance * core::at(matrix.row, 2).w;
    const maths::Vec4 clear = {.x = static_cast<float>(target.x - ahead_x), .y = static_cast<float>(target.y - ahead_y),
                               .z = target.z - ahead_z, .w = target.w - ahead_w};
    maths::Vec4 offset{};
    walk_->get_camera_offset(&offset);
    const maths::Vec4 pull = {.x = clear.x + offset.x, .y = clear.y + offset.y, .z = clear.z + offset.z, .w = clear.w + offset.w};
    avoid_camera_collisions(&target, &matrix, &pull, &focus, true, true);
    target.w = 1.0f;
    entity_->at_point_ = target;
    if (instant)
        viewer_->view_camera_->remember_prev_pos();
    skater_camera_->skater_matrix_ = matrix;
}

} // namespace objects

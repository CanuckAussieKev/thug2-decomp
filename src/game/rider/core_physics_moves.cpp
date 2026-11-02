// The skater core physics component: the gravity, a vert target to land on, the speed override, wall push-offs,
// turning round when going backwards, flip spins and stepping up (0x004FEE10-0x00500012).
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/rider.h"
#include "game/rider/trick_component.h"
#include "engine/gfx/model_utils.h"
#include "game/skate/skate_module.h"
#include "engine/net/gamenet_manager.h"
#include "game/rider/balance_trick.h"
#include "game/rider/skater_state.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <bit>

namespace skater {

using namespace physics;

namespace {
int& grind_counter()
{
    static int grind_counter{};
    return grind_counter;
}

// Physics values, stats, script values and scripts (names not recovered).
constexpr uint32_t kOverrideStat = 0x2EACDDB3;
constexpr uint32_t kOverrideMaxFactor = 0xC6B38BE0;
constexpr uint32_t kOverrideRunDown = 0xF9B006AA;
constexpr uint32_t kGravity = 0xFAA40754;
constexpr uint32_t kGravityScale = 0xC31CA696;
constexpr uint32_t kGravityScaleVert = 0x441C38A0;
constexpr uint32_t kMoonCheat = 0x9C8C6DF1;
constexpr uint32_t kMoonGravity = 0x0EC128F0;
constexpr uint32_t kFallScale = 0xBE33A41E;
constexpr uint32_t kPushOffWait = 0x0017D543;
constexpr uint32_t kPushOffAngle = 0x1483FD01;
constexpr uint32_t kPushOffEvent = 0x4C03635B;
constexpr uint32_t kPushOffSlowing = 0x1112FB1C;
constexpr uint32_t kPushOffSpeed = 0xB78542C2;
constexpr uint32_t kBackwardsSpeed = 0x2BF71EEB;
constexpr uint32_t kTurnRoundScript = 0x3A8D6AB2;
constexpr uint32_t kNoTurnRoundScript = 0xB1C84B5A;
constexpr uint32_t kBackwardsScript = 0xA496AEBD;
constexpr uint32_t kJumpReleaseEvent = 0x8FFEFB28;
constexpr uint32_t kFlipSpins = 0xB75D73E5;
constexpr uint32_t kSpinButton1 = 0x24715AF5;
constexpr uint32_t kSpinButton2 = 0xBD780B4F;
constexpr uint32_t kSpinX = 0x0AFFD09D;
constexpr uint32_t kSpinY = 0x7DF8E00B;
constexpr uint32_t kSpinZ = 0xE4F1B1B1;
constexpr uint32_t kSpinScript = 0xD8FA65D7;
constexpr uint32_t kStepUpHeight = 0x786B3272;
constexpr uint32_t kTrickModuleKey = 0x270F56E1;  // Trick
constexpr uint32_t kModelModuleName = 0x286A8D26; // Model

// The balance tricks and boards for which going backwards isn't turned round (their checksums).
constexpr uint32_t kBalanceTrickA = 0xEF24413B;
constexpr uint32_t kBalanceTrickB = 0x0AC90769;
constexpr auto kBoards = std::to_array<uint32_t>({0xEF24413B, 0x89B7DBE4, 0xA84EF276, 0x2FD8E618, 0xEFF59585, 0xC692E824});

// The game's constants.
constexpr float kTargetFirst = 10.0f;            // 0x00646710
constexpr float kTargetLast = 500.0f;            // 0x0064BC80
constexpr float kTargetStep = 6.0f;              // 0x00646284
constexpr float kTargetDrop = 4000.0f;           // 0x0064B8F8
constexpr float kTargetLift = 400.0f;            // 0x00646288
constexpr float kTargetVertY = 0.707f;           // 0x006467BC
constexpr float kDestHeading = 0.95f;            // 0x0064BC7C
constexpr float kTargetOpposite = -0.866f;       // 0x00646960
constexpr float kOverrideStart = 1.25f;          // 0x00649AC4
constexpr float kAirBoostFactor = 1.1f;          // 0x0064A300
constexpr uint32_t kNoLimit = 0x60AD78EC;        // (float bits: about 1e20)
constexpr float kBarelyMoving = 0.00001f;        // 0x006453B8
constexpr float kDegrees = 0.01745329238474369f; // 0x00645464 (pi / 180)
constexpr float kPushOffStill = 0.001f;          // 0x006461D8
constexpr float kStepUpGround = 0.5f;            // 0x006453BC
constexpr float kStepUpLift = 0.1f;              // 0x00645D74
constexpr float kFlipSpinTime = 50.0f;

// The state component's flags.

// The pad's buttons (offsets into the input component).

double get_seconds(uint32_t milliseconds) { return static_cast<double>(milliseconds); }
objects::GameEntity* get_composite(objects::GameEntity* object) { return static_cast<objects::GameEntity*>(object); }

// 0x00487AD0 (cdecl): the least of three.
float least_of(float a, float b, float c) { return gfx::least_of(a, b, c); }

} // namespace

// 0x004FEE10
bool SkaterPhysicsModule::find_vert_target(const maths::Vec4* direction, const maths::Vec4* facing, bool* not_opposite,
                                           maths::Vec4* point, maths::Vec4* normal, bool* object_alive)
{
    render::ProbeRay feeler;
    render::HitMeshCache cache;
    maths::BBox box;
    // The box: from 10 out to 500 out, 4000 down.
    for (const float out : {kTargetFirst, kTargetLast}) {
        const maths::Vec4& position = entity_->at_point_;
        const double w = static_cast<double>(direction->w) * out;
        auto z = static_cast<float>(direction->z * static_cast<double>(out));
        auto y = static_cast<float>(direction->y * static_cast<double>(out));
        auto x = static_cast<float>(direction->x * static_cast<double>(out));
        maths::Vec4 corner(x + position.x, y + position.y, z + position.z, static_cast<float>(w + position.w));
        box.append_spot(corner);
        corner.y = static_cast<float>(corner.y - static_cast<double>(kTargetDrop));
        box.append_spot(corner);
    }
    cache.tick_frame(box);
    feeler.hit_cache_ = &cache;

    float distance = kTargetFirst;
    double along = 0.0;
    bool found = false;
    for (;;) {
        const maths::Vec4& position = entity_->at_point_;
        const double w = static_cast<double>(distance) * direction->w;
        auto z = static_cast<float>(static_cast<double>(distance) * direction->z);
        auto y = static_cast<float>(static_cast<double>(distance) * direction->y);
        auto x = static_cast<float>(static_cast<double>(distance) * direction->x);
        const float at_y = y + position.y;
        const maths::Vec4 at(x + position.x, at_y, z + position.z, static_cast<float>(w + position.w));
        feeler.line_.start = at;
        feeler.line_.end = at;
        feeler.line_.end.y = static_cast<float>(at_y - static_cast<double>(kTargetDrop));
        feeler.line_.start.y = static_cast<float>(at_y + static_cast<double>(kTargetLift));
        if (feeler.get_hit_shape(true, false) && (feeler.hit_.face_flags_ & 8) &&
            !(std::fabs(static_cast<double>(feeler.surface_dir_.y)) >= kTargetVertY ||
              std::isnan(feeler.surface_dir_.y))) {
            maths::Vec4 flat(feeler.surface_dir_.x, 0.0f, feeler.surface_dir_.z, feeler.surface_dir_.w);
            flat.make_unit();
            along = (static_cast<double>(flat.z) * facing->z + static_cast<double>(flat.y) * facing->y) +
                    static_cast<double>(flat.x) * facing->x;
            if (along <= kDestHeading) {
                found = true;
                break;
            }
        }
        const double next = static_cast<double>(distance) + kTargetStep;
        distance = static_cast<float>(next);
        if (!(next < kTargetLast))
            break;
    }
    if (!found) {
        return false;
    }
    *point = feeler.contact_pos_;
    *normal = feeler.surface_dir_;
    *object_alive = feeler.hit_object_alive();
    *not_opposite = static_cast<float>(along) > kTargetOpposite;
    return true;
}

// 0x004FF200
void SkaterPhysicsModule::update_speed_override()
{
    objects::GameEntity* object = object_of(this);
    float& factor = speed_override_;
    if (frame_began_in_drop_ && !state_->flag(PoseFlag::kAcidDrop).on && !state_->flag(PoseFlag::kBailing).on) {
        auto stat = static_cast<float>(stat_value(object, kOverrideStat));
        const double speed = length_unrounded(entity_->vel_);
        auto speed_rounded = static_cast<float>(speed);
        if (speed < static_cast<double>(stat) * kOverrideStart)
            return;
        const float most = tuning_float_lookup(kOverrideMaxFactor, object);
        const double over = static_cast<double>(speed_rounded) / stat;
        factor = over < most ? static_cast<float>(over) : most;
        limits_max_ = std::bit_cast<float>(kNoLimit);
        limits_max_max_ = std::bit_cast<float>(kNoLimit);
    } else if (speed_override_ == 1.0f) { // (1: off)
        return;
    }
    limits_time_ = -1.0f;
    const double left = factor - static_cast<double>(tuning_float_lookup(kOverrideRunDown, object)) * frame_length_;
    factor = static_cast<float>(left);
    if (left >= 1.0 || left != left) {
        auto stat = static_cast<float>(stat_value(object_of(this), kOverrideStat));
        auto max = static_cast<float>(static_cast<double>(stat) * factor);
        object = object_of(this);
        float limit = 0.0f;
        if (state_->cur_phase_ != 1) {
            auto speed = static_cast<float>(length_unrounded(object->vel_) * kAirBoostFactor);
            limit = least_of(max, speed, limits_max_);
        } else {
            const float current = limits_max_;
            limit = max < current ? max : current;
        }
        if (limit >= stat || limit != limit) {
            limits_max_ = limit;
            auto inverse = static_cast<float>(1.0 / stat);
            auto share =
                static_cast<float>(static_cast<double>(stat_value(object, kOverrideStat)) * inverse * limits_max_);
            int& counter = grind_counter();
            counter = (counter + 1) % 10;
            limits_max_max_ = share;
            // (Every tenth time: a line printed, compiled out - an inlined loop that runs once.)
            while (counter == 0) {
                debug::printf("Post-Transfer Speed Limit Overrides:  current / standard = %.2f\n",
                              static_cast<double>(inverse) * limits_max_);
                counter = (counter + 1) % 10;
            }
            return;
        }
    }
    factor = 1.0f;
    if (limits_time_ == -2.0f) // (never: it was just set to -1)
        return;
    limits_time_ = 0.0f;
}

// 0x004FF470
double SkaterPhysicsModule::get_gravity()
{
    objects::GameEntity* object = object_of(this);
    objects::RiderPoseState* flags = state_;
    const float gravity = tuning_float_lookup(kGravity, object);
    const bool vert = flags->flag(PoseFlag::kVertAir).on || flags->flag(PoseFlag::kAcidDrop).on;
    auto result = static_cast<float>(
        gravity / static_cast<double>(script::fetch_float(vert ? kGravityScaleVert : kGravityScale)));
    auto* career = modules::Skate::sole()->career_.get();
    if (career->is_code_on(kMoonCheat)) {
        career->set_global_flag(script::fetch_int_value(kMoonCheat));
        result = static_cast<float>(static_cast<double>(tuning_float_lookup(kMoonGravity, object)) * result);
        online::set_cheat_flag(true);
    }
    const float scale = script::fetch_float(kFallScale);
    if (scale != 0.0f && !(entity_->vel_.y > 0.0f) && entity_->vel_.y == entity_->vel_.y)
        return static_cast<double>(scale) * result;
    return result;
}

// 0x004FF590
void SkaterPhysicsModule::get_along_facing(maths::Vec4* velocity)
{
    const double length = length_unrounded(*velocity);
    if (!(length > kBarelyMoving))
        return;
    const double inverse = 1.0 / length;
    velocity->x = static_cast<float>(inverse * velocity->x);
    velocity->y = static_cast<float>(inverse * velocity->y);
    velocity->z = static_cast<float>(inverse * velocity->z);
    velocity->w = static_cast<float>(inverse * velocity->w);
    const maths::Vec4& facing = core::at(entity_->basis_frame_.row, 2);
    const double along = (static_cast<double>(facing.y) * velocity->y + static_cast<double>(facing.z) * velocity->z) +
                         static_cast<double>(facing.x) * velocity->x;
    const double scale = (along < 0.0 ? -1.0 : 1.0) * length;
    *velocity = facing;
    velocity->x = static_cast<float>(scale * velocity->x);
    velocity->y = static_cast<float>(scale * velocity->y);
    velocity->z = static_cast<float>(scale * velocity->z);
    velocity->w = static_cast<float>(scale * velocity->w);
}

// 0x004FF670
bool SkaterPhysicsModule::wall_push_off()
{
    if (!input_->pad_primary_.button_wedge_.held_down_)
        return false;
    auto since_last = static_cast<float>(get_seconds(get_since(last_push_off_)));
    if (script::fetch_float(kPushOffWait) > since_last)
        return false;
    const double angle = std::sin((script::fetch_float(kPushOffAngle) - 1.0) * kDegrees);
    const maths::Vec4& facing = core::at(entity_->basis_frame_.row, 2);
    const maths::Vec4& wall = feeler_.surface_dir_;
    const double into = (static_cast<double>(facing.z) * wall.z + static_cast<double>(facing.y) * wall.y) +
                        static_cast<double>(facing.x) * wall.x;
    if (-angle <= into)
        return false;
    auto since_start = static_cast<float>(get_seconds(get_since(wallplant_time_)));
    if (script::fetch_float(kPushOffWait) > since_start)
        return false;
    static_cast<objects::Entity*>(object_of(this))->post_own_event(kPushOffEvent, nullptr);
    if (state_->flag(PoseFlag::kCancelWallpush).on) {
        set_timed_flag(this, PoseFlag::kCancelWallpush, 0);
        return false;
    }
    maths::Vec4& velocity = entity_->vel_;
    const float bounce = maths::dot_vec(velocity, wall);
    velocity -= 2.0f * (bounce * wall);
    const double speed_unrounded = length_unrounded(entity_->vel_);
    auto speed = static_cast<float>(speed_unrounded);
    maths::Vec4& v = entity_->vel_;
    if (speed_unrounded > kPushOffStill) {
        auto slower = static_cast<float>(speed - static_cast<double>(script::fetch_float(kPushOffSlowing)));
        const float least = script::fetch_float(kPushOffSpeed);
        v *= static_cast<float>(objects::larger_of(least, slower) / static_cast<double>(speed));
    } else {
        v = -script::fetch_float(kPushOffSpeed) * core::at(entity_->basis_frame_.row, 2);
    }
    entity_->vel_.align_to_plane(floor_normal_);
    objects::GameEntity* object = object_of(this);
    core::at(object->basis_frame_.row, 2) = object->vel_;
    core::at(entity_->basis_frame_.row, 2).make_unit();
    core::at(entity_->basis_frame_.row, 1) = floor_normal_;
    object = object_of(this);
    core::at(object->basis_frame_.row, 0) = maths::cross_vec(core::at(object->basis_frame_.row, 1), core::at(object->basis_frame_.row, 2));
    display_matrix_ = entity_->basis_frame_;
    last_push_off_ = get_now();
    return true;
}

// 0x004FF920
void SkaterPhysicsModule::turn_round_if_backwards()
{
    objects::RiderPoseState* flags = state_;
    objects::GameEntity* object = object_of(this);
    if (flags->flag(PoseFlag::kBailing).on || flags->flag(PoseFlag::kSkitching).on || braking_)
        return;
    const maths::Vec4& v = object->vel_;
    const maths::Vec4& f = core::at(object->basis_frame_.row, 2);
    const double along =
        (static_cast<double>(v.z) * f.z + static_cast<double>(v.y) * f.y) + static_cast<double>(v.x) * f.x;
    if (!(along < 0.0))
        return;
    const double least = tuning_float_lookup(kBackwardsSpeed, object);
    if (!(least * least < length_sqr_unrounded(v)))
        return;
    bool special = on_vehicle_;
    if (!special) {
        const uint32_t trick = balance_->trick_;
        special = trick == kBalanceTrickA || trick == kBalanceTrickB;
    }
    if (special) {
        const uint32_t board = checksums::hash_from_text(trick_->trick_name_.data());
        bool plain = false;
        for (const uint32_t b : kBoards)
            plain = plain || board == b;
        if (!plain) {
            if (manual_turnaround_)
                return;
            manual_turnaround_ = 1;
            script::launch_script(kBackwardsScript, nullptr, static_cast<objects::Entity*>(object_of(this)), false,
                                  nullptr);
            return;
        }
    }
    maths::Vec4& facing = core::at(entity_->basis_frame_.row, 2);
    facing = -facing;
    maths::Vec4& right = core::at(entity_->basis_frame_.row, 0);
    right = -right;
    maths::Vec4& display_facing = core::at(display_matrix_.row, 2);
    display_facing = -display_facing;
    maths::Vec4& display_right = core::at(display_matrix_.row, 0);
    display_right = -display_right;
    if (!script::fetch_int_value(kNoTurnRoundScript))
        script::launch_script(kTurnRoundScript, nullptr, static_cast<objects::Entity*>(object_of(this)), false,
                              nullptr);
}

// 0x004FFB10
bool SkaterPhysicsModule::jump_released()
{
    objects::RiderPoseState* flags = state_;
    objects::ControlButton* button = &input_->pad_primary_.pos_x_;
    if (!flags->flag(PoseFlag::kCrouched).on || button->held_down_)
        return false;
    jump_charge_ms_ = static_cast<int>(get_since(static_cast<uint32_t>(flags->flag(PoseFlag::kCrouched).changed_at)));
    set_timed_flag(this, PoseFlag::kCrouched, 0);
    button->forget_let_go();
    static_cast<objects::Entity*>(object_of(this))->post_own_event(kJumpReleaseEvent, nullptr);
    return true;
}

// 0x004FFB90
void SkaterPhysicsModule::check_flip_spin()
{
    if (state_->flag(PoseFlag::kBailing).on)
        return;
    const objects::PadInputModule* input = input_;
    if (model_rotating_)
        return;
    if (!input->pad_primary_.button_round_.held_down_ && !input->pad_primary_.button_box_.held_down_)
        return;
    model_flipped_ = 0;
    auto* trick = core::down_cast<objects::MoveExecutor>(get_composite(object_of(this))->fetch_module(kTrickModuleKey));
    script::Array* spins = script::fetch_list(kFlipSpins);
    const int count = static_cast<int>(spins->extent_);
    script::Struct* spin = nullptr;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        spin = spins->struct_at(static_cast<uint32_t>(i));
        std::array<uint32_t, 3> sequence{}; // (an entry without the two buttons leaves them unset, as the original does)
        spin->read_hash_value(kSpinButton1, &core::at(sequence, 0), 0);
        spin->read_hash_value(kSpinButton2, &core::at(sequence, 1), 0);
        core::at(sequence, 2) = input->pad_primary_.button_round_.held_down_ ? 0x2B489A86u : 0x321C9756u;
        if (trick->check_spin_sequence(sequence, flip_spin_started_ != 0)) {
            found = true;
            break;
        }
    }
    if (!found)
        return;
    model_rotating_ = 1;
    model_rotation_left_ = kFlipSpinTime;
    no_rail_tricks_ = 1;
    flip_spin_started_ = 1;
    auto* model =
        core::down_cast<objects::MeshInstanceModule>(get_composite(object_of(this))->fetch_module(kModelModuleName));
    int x = 0, y = 0, z = 0;
    if (spin->fetch_int_value(kSpinX, &x, 0) && x)
        model->rotate_display(model_rotation_left_, 0.0f, static_cast<float>(x), 0, true, true, false, false);
    if (spin->fetch_int_value(kSpinY, &y, 0) && y) {
        if (y == 180 || y == -180)
            model_flipped_ = 1;
        model->rotate_display(model_rotation_left_, 0.0f, static_cast<float>(y), 0, true, false, true, false);
    }
    if (spin->fetch_int_value(kSpinZ, &z, 0) && z)
        model->rotate_display(model_rotation_left_, 0.0f, static_cast<float>(z), 0, true, false, false, true);
    script::launch_script(kSpinScript, spin, static_cast<objects::Entity*>(object_of(this)), false, nullptr);
}

// 0x004FFDB0
bool SkaterPhysicsModule::step_up(const maths::Vec4* point)
{
    if (state_->flag(PoseFlag::kBailing).on)
        return false;
    render::ProbeRay feeler;
    const float height = tuning_float_lookup(kStepUpHeight, object_of(this));
    const objects::GameEntity* object = object_of(this);
    const maths::Vec4& position = object->at_point_;
    // Down from above the higher of the skater and the point to the lower.
    feeler.line_.start = position;
    feeler.line_.start.y = static_cast<float>(static_cast<double>(position.y) + height);
    feeler.line_.end = position;
    if (point->y < position.y)
        feeler.line_.end.y = point->y;
    const double above = static_cast<double>(height) + point->y;
    if (above > feeler.line_.start.y)
        feeler.line_.start.y = static_cast<float>(above);
    if (!feeler.get_hit_shape(true, false))
        return false;
    if (!(feeler.surface_dir_.y > kStepUpGround))
        return false;
    // Across from above the point to above the hit (the normal added): nothing in the way.
    auto w = static_cast<float>(static_cast<double>(feeler.surface_dir_.w) + feeler.contact_pos_.w);
    auto z = static_cast<float>(static_cast<double>(feeler.surface_dir_.z) + feeler.contact_pos_.z);
    auto y = static_cast<float>(static_cast<double>(feeler.contact_pos_.y) + feeler.surface_dir_.y);
    auto x = static_cast<float>(static_cast<double>(feeler.surface_dir_.x) + feeler.contact_pos_.x);
    feeler.line_.start =
        maths::Vec4(point->x, static_cast<float>(static_cast<double>(point->y) + height), point->z, point->w);
    feeler.line_.end = maths::Vec4(x, static_cast<float>(static_cast<double>(y) + height), z, w);
    if (feeler.get_hit_shape(true, false))
        return false;
    maths::Vec4& moved = entity_->at_point_;
    moved = maths::Vec4(x, y, z, w);
    moved.y = static_cast<float>(static_cast<double>(moved.y) + kStepUpLift);
    return true;
}

} // namespace skater


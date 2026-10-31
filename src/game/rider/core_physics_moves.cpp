// The skater core physics component: the gravity, a vert target to land on, the speed override, wall push-offs,
// turning round when going backwards, flip spins and stepping up (0x004FEE10-0x00500012).
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>
#include <initializer_list>

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

namespace skater {

using namespace physics;

int g_grind_counter;

namespace {

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
constexpr uint32_t kTrickModuleKey = 0x270F56E1;      // Trick
constexpr uint32_t kModelModuleName = 0x286A8D26;      // Model

// The balance tricks and boards for which going backwards isn't turned round (their checksums).
constexpr uint32_t kBalanceTrickA = 0xEF24413B;
constexpr uint32_t kBalanceTrickB = 0x0AC90769;
constexpr uint32_t kBoards[] = {0xEF24413B, 0x89B7DBE4, 0xA84EF276, 0x2FD8E618, 0xEFF59585, 0xC692E824};

// The game's constants.
constexpr float kTargetFirst = 10.0f;        // 0x00646710
constexpr float kTargetLast = 500.0f;        // 0x0064BC80
constexpr float kTargetStep = 6.0f;          // 0x00646284
constexpr float kTargetDrop = 4000.0f;       // 0x0064B8F8
constexpr float kTargetLift = 400.0f;        // 0x00646288
constexpr float kTargetVertY = 0.707f;       // 0x006467BC
constexpr float kDestHeading = 0.95f;       // 0x0064BC7C
constexpr float kTargetOpposite = -0.866f;   // 0x00646960
constexpr float kOverrideStart = 1.25f;      // 0x00649AC4
constexpr float kAirBoostFactor = 1.1f;         // 0x0064A300
constexpr uint32_t kNoLimit = 0x60AD78EC;    // (float bits: about 1e20)
constexpr float kBarelyMoving = 0.00001f;    // 0x006453B8
constexpr float kDegrees = 0.01745329238474369f; // 0x00645464 (pi / 180)
constexpr float kPushOffStill = 0.001f;      // 0x006461D8
constexpr float kStepUpGround = 0.5f;        // 0x006453BC
constexpr float kStepUpLift = 0.1f;          // 0x00645D74
constexpr float kFlipSpinTime = 50.0f;

// The component's fields used here.
constexpr uint32_t kHitNormalField = 0x130;
constexpr uint32_t kOverrideOn = 0x255;
constexpr uint32_t kModelRotating = 0x2DB;
constexpr uint32_t kModelFlipped = 0x2DC;
constexpr uint32_t kModelRotationLeft = 0x2E0;
constexpr uint32_t kModelRotationFlag = 0x2E4;
constexpr uint32_t kOnVehicle = 0x2ED;
constexpr uint32_t kBackwardsScriptRun = 0x304;
constexpr uint32_t kBraking = 0x324;
constexpr uint32_t kLimitsTime = 0x350;
constexpr uint32_t kLimitsMax = 0x354;
constexpr uint32_t kLimitsMaxMax = 0x358;
constexpr uint32_t kJumpHeld = 0x378;
constexpr uint32_t kRailTricksOff = 0x3A4;
constexpr uint32_t kOverrideFactor = 0x3AC;
constexpr uint32_t kPushOffStart = 0x4E0;
constexpr uint32_t kLastPushOff = 0x4E4;
constexpr uint32_t kBalanceTrick = 0x1524;           // (+0x18 the trick)

// The state component's flags.
constexpr uint32_t kFlagJump = 0x20;
constexpr uint32_t kFlagVertAir = 0x30;
constexpr uint32_t kFlagBail = 0x70;
constexpr uint32_t kFlagAcidDrop = 0x78;
constexpr uint32_t kFlagSkitching = 0x88;
constexpr uint32_t kFlagCancelWallpush = 0xB8;

// The pad's buttons (offsets into the input component).
constexpr uint32_t kFlipButton1 = 0x160;
constexpr uint32_t kFlipButton2 = 0x180;
constexpr uint32_t kPushOffButton = 0x1A0;
constexpr uint32_t kJumpButton = 0x1C0;

double get_seconds(uint32_t milliseconds) { return static_cast<double>(milliseconds); }
objects::GameEntity* get_composite(void* object) { return static_cast<objects::GameEntity*>(object); }

// 0x00487AD0 (cdecl): the least of three.
float least_of(float a, float b, float c) { return gfx::least_of(a, b, c); }

} // namespace

// 0x004FEE10
bool SkaterPhysicsModule::find_vert_target(const maths::Vec4* direction, const maths::Vec4* facing, bool* not_opposite,
                                           maths::Vec4* point, maths::Vec4* normal, bool* object_alive)
{
    render::ProbeRay feeler;
    feeler.make_new();
    render::HitMeshCache cache;
    cache.make_new();
    maths::BBox box;
    // The box: from 10 out to 500 out, 4000 down.
    for (float out : {kTargetFirst, kTargetLast}) {
        const maths::Vec4& position = vector_at(object_of(this), kPosition);
        double w = static_cast<double>(direction->w) * out;
        float z = static_cast<float>(direction->z * static_cast<double>(out));
        float y = static_cast<float>(direction->y * static_cast<double>(out));
        float x = static_cast<float>(direction->x * static_cast<double>(out));
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
    do {
        const maths::Vec4& position = vector_at(object_of(this), kPosition);
        double w = static_cast<double>(distance) * direction->w;
        float z = static_cast<float>(static_cast<double>(distance) * direction->z);
        float y = static_cast<float>(static_cast<double>(distance) * direction->y);
        float x = static_cast<float>(static_cast<double>(distance) * direction->x);
        float at_y = y + position.y;
        maths::Vec4 at(x + position.x, at_y, z + position.z, static_cast<float>(w + position.w));
        feeler.line_.start = at;
        feeler.line_.end = at;
        feeler.line_.end.y = static_cast<float>(at_y - static_cast<double>(kTargetDrop));
        feeler.line_.start.y = static_cast<float>(at_y + static_cast<double>(kTargetLift));
        if (feeler.get_hit_shape(true, false) && (feeler.hit_.face_flags_ & 8) &&
            !(std::fabs(static_cast<double>(feeler.surface_dir_.y)) >= kTargetVertY ||
              feeler.surface_dir_.y != feeler.surface_dir_.y)) {
            maths::Vec4 flat(feeler.surface_dir_.x, 0.0f, feeler.surface_dir_.z, feeler.surface_dir_.w);
            flat.make_unit();
            along = (static_cast<double>(flat.z) * facing->z + static_cast<double>(flat.y) * facing->y) +
                    static_cast<double>(flat.x) * facing->x;
            if (along <= kDestHeading) {
                found = true;
                break;
            }
        }
        double next = static_cast<double>(distance) + kTargetStep;
        distance = static_cast<float>(next);
        if (!(next < kTargetLast))
            break;
    } while (true);
    if (!found) {
        cache.destruct();
        return false;
    }
    *point = feeler.contact_pos_;
    *normal = feeler.surface_dir_;
    *object_alive = feeler.hit_object_alive();
    *not_opposite = static_cast<float>(along) > kTargetOpposite;
    cache.destruct();
    return true;
}

// 0x004FF200
void SkaterPhysicsModule::update_speed_override()
{
    void* object = object_of(this);
    float& factor = get_at<float>(this, kOverrideFactor);
    if (get_at<uint8_t>(this, kOverrideOn) && !flags_of(this)[kFlagAcidDrop] && !flags_of(this)[kFlagBail]) {
        float stat = static_cast<float>(stat_value(object, kOverrideStat));
        double speed = length_unrounded(vector_at(object_of(this), kVelocity));
        float speed_rounded = static_cast<float>(speed);
        if (speed < static_cast<double>(stat) * kOverrideStart)
            return;
        float most = tuning_float_lookup(kOverrideMaxFactor, object);
        double over = static_cast<double>(speed_rounded) / stat;
        factor = over < most ? static_cast<float>(over) : most;
        get_at<uint32_t>(this, kLimitsMax) = kNoLimit;
        get_at<uint32_t>(this, kLimitsMaxMax) = kNoLimit;
    } else if (get_at<uint32_t>(this, kOverrideFactor) == 0x3F800000u) { // (1: off)
        return;
    }
    get_at<float>(this, kLimitsTime) = -1.0f;
    double left = factor - static_cast<double>(tuning_float_lookup(kOverrideRunDown, object)) * get_at<float>(this, kFrameLength);
    factor = static_cast<float>(left);
    if (left >= 1.0 || left != left) {
        float stat = static_cast<float>(stat_value(object_of(this), kOverrideStat));
        float max = static_cast<float>(static_cast<double>(stat) * factor);
        object = object_of(this);
        float limit;
        if (get_at<int>(flags_of(this), kState) != 1) {
            float speed = static_cast<float>(length_unrounded(vector_at(object, kVelocity)) * kAirBoostFactor);
            limit = least_of(max, speed, get_at<float>(this, kLimitsMax));
        } else {
            float current = get_at<float>(this, kLimitsMax);
            limit = max < current ? max : current;
        }
        if (limit >= stat || limit != limit) {
            get_at<float>(this, kLimitsMax) = limit;
            float inverse = static_cast<float>(1.0 / stat);
            float share = static_cast<float>(static_cast<double>(stat_value(object, kOverrideStat)) * inverse *
                                             get_at<float>(this, kLimitsMax));
            int& counter = g_grind_counter;
            counter = (counter + 1) % 10;
            get_at<float>(this, kLimitsMaxMax) = share;
            // (Every tenth time: a line printed, compiled out - an inlined loop that runs once.)
            while (counter == 0) {
                debug::printf("Post-Transfer Speed Limit Overrides:  current / standard = %.2f\n",
                            static_cast<double>(inverse) * get_at<float>(this, kLimitsMax));
                counter = (counter + 1) % 10;
            }
            return;
        }
    }
    factor = 1.0f;
    if (get_at<uint32_t>(this, kLimitsTime) == 0xC0000000u) // (never: it was just set to -1)
        return;
    get_at<float>(this, kLimitsTime) = 0.0f;
}

// 0x004FF470
double SkaterPhysicsModule::get_gravity()
{
    void* object = object_of(this);
    uint8_t* flags = flags_of(this);
    float gravity = tuning_float_lookup(kGravity, object);
    bool vert = flags[kFlagVertAir] || flags[kFlagAcidDrop];
    float result = static_cast<float>(gravity / static_cast<double>(script::fetch_float(vert ? kGravityScaleVert : kGravityScale)));
    auto* career = get_at<objects::RiderStoryProgress*>(reinterpret_cast<uint8_t*>(modules::g_skate_module), 0x20);
    if (career->is_code_on(kMoonCheat)) {
        career->set_global_flag(script::fetch_int_value(kMoonCheat));
        result = static_cast<float>(static_cast<double>(tuning_float_lookup(kMoonGravity, object)) * result);
        reinterpret_cast<uint8_t&>(online::g_cheat_flag) = 1;
    }
    float scale = script::fetch_float(kFallScale);
    if (scale != 0.0f && !(vector_at(object_of(this), kVelocity).y > 0.0f) &&
        vector_at(object_of(this), kVelocity).y == vector_at(object_of(this), kVelocity).y)
        return static_cast<double>(scale) * result;
    return result;
}

// 0x004FF590
void SkaterPhysicsModule::get_along_facing(maths::Vec4* velocity)
{
    double length = length_unrounded(*velocity);
    if (!(length > kBarelyMoving))
        return;
    double inverse = 1.0 / length;
    velocity->x = static_cast<float>(inverse * velocity->x);
    velocity->y = static_cast<float>(inverse * velocity->y);
    velocity->z = static_cast<float>(inverse * velocity->z);
    velocity->w = static_cast<float>(inverse * velocity->w);
    const maths::Vec4& facing = vector_at(object_of(this), kForward);
    double along = (static_cast<double>(facing.y) * velocity->y + static_cast<double>(facing.z) * velocity->z) +
                   static_cast<double>(facing.x) * velocity->x;
    double scale = (along < 0.0 ? -1.0 : 1.0) * length;
    *velocity = facing;
    velocity->x = static_cast<float>(scale * velocity->x);
    velocity->y = static_cast<float>(scale * velocity->y);
    velocity->z = static_cast<float>(scale * velocity->z);
    velocity->w = static_cast<float>(scale * velocity->w);
}

// 0x004FF670
bool SkaterPhysicsModule::wall_push_off()
{
    if (!input_of(this)[kPushOffButton])
        return false;
    float since_last = static_cast<float>(get_seconds(get_since(get_at<uint32_t>(this, kLastPushOff))));
    if (script::fetch_float(kPushOffWait) > since_last)
        return false;
    double angle = x87_sin((script::fetch_float(kPushOffAngle) - 1.0) * kDegrees);
    const maths::Vec4& facing = vector_at(object_of(this), kForward);
    const maths::Vec4& wall = vector_at(this, kHitNormalField);
    double into = (static_cast<double>(facing.z) * wall.z + static_cast<double>(facing.y) * wall.y) +
                  static_cast<double>(facing.x) * wall.x;
    if (-angle <= into)
        return false;
    float since_start = static_cast<float>(get_seconds(get_since(get_at<uint32_t>(this, kPushOffStart))));
    if (script::fetch_float(kPushOffWait) > since_start)
        return false;
    static_cast<objects::Entity*>(object_of(this))->post_own_event(kPushOffEvent, nullptr);
    if (flags_of(this)[kFlagCancelWallpush]) {
        set_timed_flag(this, kFlagCancelWallpush, 0);
        return false;
    }
    maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
    float bounce = maths::dot_vec(velocity, wall);
    velocity -= 2.0f * (bounce * wall);
    double speed_unrounded = length_unrounded(vector_at(object_of(this), kVelocity));
    float speed = static_cast<float>(speed_unrounded);
    maths::Vec4& v = vector_at(object_of(this), kVelocity);
    if (speed_unrounded > kPushOffStill) {
        float slower = static_cast<float>(speed - static_cast<double>(script::fetch_float(kPushOffSlowing)));
        float least = script::fetch_float(kPushOffSpeed);
        v *= static_cast<float>(objects::larger_of(least, slower) / static_cast<double>(speed));
    } else {
        v = -script::fetch_float(kPushOffSpeed) * vector_at(object_of(this), kForward);
    }
    vector_at(object_of(this), kVelocity).align_to_plane(vector_at(this, kFloorUpVector));
    void* object = object_of(this);
    vector_at(object, kForward) = vector_at(object, kVelocity);
    vector_at(object_of(this), kForward).make_unit();
    vector_at(object_of(this), kUp) = vector_at(this, kFloorUpVector);
    object = object_of(this);
    vector_at(object, kMatrix) = maths::cross_vec(vector_at(object, kUp), vector_at(object, kForward));
    get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
    get_at<int>(this, kLastPushOff) = get_now();
    return true;
}

// 0x004FF920
void SkaterPhysicsModule::turn_round_if_backwards()
{
    uint8_t* flags = flags_of(this);
    void* object = object_of(this);
    if (flags[kFlagBail] || flags[kFlagSkitching] || get_at<uint8_t>(this, kBraking))
        return;
    const maths::Vec4& v = vector_at(object, kVelocity);
    const maths::Vec4& f = vector_at(object, kForward);
    double along = (static_cast<double>(v.z) * f.z + static_cast<double>(v.y) * f.y) + static_cast<double>(v.x) * f.x;
    if (!(along < 0.0))
        return;
    double least = tuning_float_lookup(kBackwardsSpeed, object);
    if (!(least * least < length_sqr_unrounded(v)))
        return;
    bool special = get_at<uint8_t>(this, kOnVehicle);
    if (!special) {
        uint32_t trick = get_at<uint32_t>(get_at<void*>(this, kBalanceTrick), 0x18);
        special = trick == kBalanceTrickA || trick == kBalanceTrickB;
    }
    if (special) {
        uint32_t board = checksums::hash_from_text(get_at<char*>(this, kSkaterComponent) + 0x1331);
        bool plain = false;
        for (uint32_t b : kBoards)
            plain = plain || board == b;
        if (!plain) {
            if (get_at<uint8_t>(this, kBackwardsScriptRun))
                return;
            get_at<uint8_t>(this, kBackwardsScriptRun) = 1;
            script::launch_script(kBackwardsScript, nullptr, static_cast<objects::Entity*>(object_of(this)), false, nullptr);
            return;
        }
    }
    maths::Vec4& facing = vector_at(object_of(this), kForward);
    facing = -facing;
    maths::Vec4& right = vector_at(object_of(this), kMatrix);
    right = -right;
    maths::Vec4& display_facing = vector_at(this, kDisplayMatrix + 0x20);
    display_facing = -display_facing;
    maths::Vec4& display_right = vector_at(this, kDisplayMatrix);
    display_right = -display_right;
    if (!script::fetch_int_value(kNoTurnRoundScript))
        script::launch_script(kTurnRoundScript, nullptr, static_cast<objects::Entity*>(object_of(this)), false, nullptr);
}

// 0x004FFB10
bool SkaterPhysicsModule::jump_released()
{
    uint8_t* flags = flags_of(this);
    uint8_t* button = input_of(this) + kJumpButton;
    if (!flags[kFlagJump] || *button)
        return false;
    get_at<uint32_t>(this, kJumpHeld) = get_since(get_at<uint32_t>(flags, kFlagJump + 4));
    set_timed_flag(this, kFlagJump, 0);
    reinterpret_cast<objects::ControlButton*>(button)->forget_let_go();
    static_cast<objects::Entity*>(object_of(this))->post_own_event(kJumpReleaseEvent, nullptr);
    return true;
}

// 0x004FFB90
void SkaterPhysicsModule::check_flip_spin()
{
    if (flags_of(this)[kFlagBail])
        return;
    uint8_t* input = input_of(this);
    if (get_at<uint8_t>(this, kModelRotating))
        return;
    if (!input[kFlipButton1] && !input[kFlipButton2])
        return;
    get_at<uint8_t>(this, kModelFlipped) = 0;
    auto* trick = static_cast<objects::MoveExecutor*>(get_composite(object_of(this))->fetch_module(kTrickModuleKey));
    script::Array* spins = script::fetch_list(kFlipSpins);
    int count = static_cast<int>(spins->extent_);
    script::Struct* spin = nullptr;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        spin = reinterpret_cast<script::Struct*>(spins->fetch_part(static_cast<uint32_t>(i)));
        uint32_t sequence[3]; // (an entry without the two buttons leaves them unset, as the original does)
        spin->read_hash_value(kSpinButton1, &sequence[0], 0);
        spin->read_hash_value(kSpinButton2, &sequence[1], 0);
        sequence[2] = input[kFlipButton1] ? 0x2B489A86u : 0x321C9756u;
        if (trick->check_spin_sequence(sequence, get_at<uint8_t>(this, kModelRotationFlag) != 0)) {
            found = true;
            break;
        }
    }
    if (!found)
        return;
    get_at<uint8_t>(this, kModelRotating) = 1;
    get_at<float>(this, kModelRotationLeft) = kFlipSpinTime;
    get_at<uint8_t>(this, kRailTricksOff) = 1;
    get_at<uint8_t>(this, kModelRotationFlag) = 1;
    auto* model = static_cast<objects::MeshInstanceModule*>(get_composite(object_of(this))->fetch_module(kModelModuleName));
    int x = 0, y = 0, z = 0;
    if (spin->fetch_int_value(kSpinX, &x, 0) && x)
        model->rotate_display(get_at<float>(this, kModelRotationLeft), 0.0f, static_cast<float>(x), 0, true, true, false, false);
    if (spin->fetch_int_value(kSpinY, &y, 0) && y) {
        if (y == 180 || y == -180)
            get_at<uint8_t>(this, kModelFlipped) = 1;
        model->rotate_display(get_at<float>(this, kModelRotationLeft), 0.0f, static_cast<float>(y), 0, true, false, true, false);
    }
    if (spin->fetch_int_value(kSpinZ, &z, 0) && z)
        model->rotate_display(get_at<float>(this, kModelRotationLeft), 0.0f, static_cast<float>(z), 0, true, false, false, true);
    script::launch_script(kSpinScript, spin, static_cast<objects::Entity*>(object_of(this)), false, nullptr);
}

// 0x004FFDB0
bool SkaterPhysicsModule::step_up(const maths::Vec4* point)
{
    if (flags_of(this)[kFlagBail])
        return false;
    render::ProbeRay feeler;
    feeler.make_new();
    float height = tuning_float_lookup(kStepUpHeight, object_of(this));
    void* object = object_of(this);
    const maths::Vec4& position = vector_at(object, kPosition);
    // Down from above the higher of the skater and the point to the lower.
    feeler.line_.start = position;
    feeler.line_.start.y = static_cast<float>(static_cast<double>(position.y) + height);
    feeler.line_.end = position;
    if (point->y < position.y)
        feeler.line_.end.y = point->y;
    double above = static_cast<double>(height) + point->y;
    if (above > feeler.line_.start.y)
        feeler.line_.start.y = static_cast<float>(above);
    if (!feeler.get_hit_shape(true, false))
        return false;
    if (!(feeler.surface_dir_.y > kStepUpGround))
        return false;
    // Across from above the point to above the hit (the normal added): nothing in the way.
    float w = static_cast<float>(static_cast<double>(feeler.surface_dir_.w) + feeler.contact_pos_.w);
    float z = static_cast<float>(static_cast<double>(feeler.surface_dir_.z) + feeler.contact_pos_.z);
    float y = static_cast<float>(static_cast<double>(feeler.contact_pos_.y) + feeler.surface_dir_.y);
    float x = static_cast<float>(static_cast<double>(feeler.surface_dir_.x) + feeler.contact_pos_.x);
    feeler.line_.start = maths::Vec4(point->x, static_cast<float>(static_cast<double>(point->y) + height), point->z,
                                      point->w);
    feeler.line_.end = maths::Vec4(x, static_cast<float>(static_cast<double>(y) + height), z, w);
    if (feeler.get_hit_shape(true, false))
        return false;
    maths::Vec4& moved = vector_at(object_of(this), kPosition);
    moved = maths::Vec4(x, y, z, w);
    moved.y = static_cast<float>(static_cast<double>(moved.y) + kStepUpLift);
    return true;
}

} // namespace Skater

using skater::SkaterPhysicsModule;

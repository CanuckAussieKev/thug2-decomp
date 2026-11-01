// The skater's finalize-physics component (GameObjects::RiderPhysicsCommit): see finalize_physics.h.

#include "game/rider/finalize_physics.h"

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "game/rider/looping_sound.h"
#include "game/rider/rider.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "game/rider/skater_sound.h"
#include "engine/core/raw_view.h"

namespace objects {

namespace {

constexpr uint32_t kSkaterFinalizePhysics = 0x9B373E58;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kSkaterState = 0x43686585, kSkaterSound = 0x187574FA,
                   kSkaterLoopingSound = 0xB5F1E9D2;
constexpr uint32_t kSkaterMaxMaxSpeedStat = 0xCC5F87AA; // Skater_Max_Max_Speed_Stat

constexpr float kStillSpeed = 1.0f; // 0x006453A8: x and z speeds below this count as straight up

template <typename T>
T& prop(void* object, uint32_t offset)
{
    return *raw::as<T>(static_cast<uint8_t*>(object) + offset);
}

// The skater state component's state (+0x18), as the core physics' update dispatches on it (0x00510830): 0 ground,
// 1 air (do_air_physics), 2-5 its other physics (3: a lip trick, which sets the core physics' +0x256 for the frame).
constexpr int kStateAir = 1, kStateLip = 3;

// The skater's physics value `name` (as get_scaled_value).
double scaled_physics_value(GameEntity* skater, uint32_t name)
{
    return static_cast<Rider*>(skater)->get_scaled_physics(name);
}
// The looping sound component: the speed as a fraction of the top speed.
void set_looping_sound_speed(ObjectModule* sound, float fraction)
{
    static_cast<RiderRollingAudio*>(sound)->set_pace_ratio(fraction);
}

// `value` into `stored`, `changed` set if it differs.
template <typename T>
void get_note(T& stored, T value, uint8_t& changed)
{
    changed |= stored != value ? 1 : 0;
    stored = value;
}

} // namespace

// 0x00511280
memory::owner<ObjectModule*> RiderPhysicsCommit::fabricate() { return new RiderPhysicsCommit; }

// (the constructor: inlined in fabricate in the original)
RiderPhysicsCommit::RiderPhysicsCommit() { kind_tag_ = kSkaterFinalizePhysics; }

// 0x00511010 (0x00511020 the deleting one)
RiderPhysicsCommit::~RiderPhysicsCommit() = default;

// 0x00511050
void RiderPhysicsCommit::seal()
{
    core_physics_ = static_cast<skater::SkaterPhysicsModule*>(entity_->fetch_module(kSkaterCorePhysics));
    pose_ = static_cast<RiderPoseState*>(entity_->fetch_module(kSkaterState));
}

// 0x00511080
void RiderPhysicsCommit::tick_frame()
{
    skater::SkaterPhysicsModule* core = core_physics_;
    objects::RiderPoseState* state = pose_;
    // Out of a lip trick (the physics' +0x256: the frame began in one) straight up: into the air with hardly any speed
    // across the ground. Kept while in the air.
    if (core->frame_began_in_lip_ && state->cur_phase_ != kStateLip) {
        state->lip_straight_up_ = 0;
        if (state->cur_phase_ == kStateAir && std::fabs(entity_->vel_.x) < kStillSpeed &&
            std::fabs(entity_->vel_.z) < kStillSpeed)
            state->lip_straight_up_ = 1;
    }
    if (state->cur_phase_ != kStateAir)
        state->lip_straight_up_ = 0;
    state->drawn_normal_ = core->drawn_normal_;
    state->floor_normal_ = core->floor_normal_;

    // The matrices: orthonormal again about up; the physics' own replaced by the object's if that needed repair.
    entity_->basis_frame_.square_up_axes(1);
    maths::Mat4& physics_matrix = core->display_matrix_;
    physics_matrix.square_up_axes(1);
    if (entity_->basis_frame_.restore_right_angles())
        physics_matrix = static_cast<ObjectModule*>(core)->entity_->basis_frame_;
    entity_->display_matrix_ = physics_matrix;

    // The sounds: terrain and state.
    auto* sound = static_cast<RiderSfxPlayer*>(entity_->fetch_module(kSkaterSound));
    sound->sliding_ = pose_->flag(PoseFlag::kSlide).on;
    sound->surface_type_ = pose_->surface_type_;
    auto* looping = static_cast<RiderRollingAudio*>(entity_->fetch_module(kSkaterLoopingSound));
    const double x = entity_->vel_.x;
    const double z = entity_->vel_.z;
    auto speed = static_cast<float>(std::sqrt(x * x + z * z));
    auto fraction = static_cast<float>(speed / scaled_physics_value(entity_, kSkaterMaxMaxSpeedStat));
    set_looping_sound_speed(looping, fraction);
    looping->no_rail_sound_ = pose_->flag(PoseFlag::kBailing).on;
    uint8_t& changed = looping->sound_changed_;
    get_note(looping->sliding_, pose_->flag(PoseFlag::kSlide).on, changed);
    get_note(looping->surface_type_, pose_->surface_type_, changed);
    get_note(looping->cur_phase_, pose_->cur_phase_, changed);
}

} // namespace objects

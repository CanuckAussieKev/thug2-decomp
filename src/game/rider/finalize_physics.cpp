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
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
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
ObjectModule* __cdecl RiderPhysicsCommit::fabricate() { return new RiderPhysicsCommit; }

// (the constructor: inlined in fabricate in the original)
RiderPhysicsCommit::RiderPhysicsCommit()
{
    kind_tag_ = kSkaterFinalizePhysics;
    core_physics_ = nullptr;
    cur_phase_ = nullptr;
}

// 0x00511010 (0x00511020 the deleting one)
RiderPhysicsCommit::~RiderPhysicsCommit() {}

// 0x00511050
void RiderPhysicsCommit::seal()
{
    core_physics_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterCorePhysics));
    cur_phase_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterState));
}

// 0x00511080
void RiderPhysicsCommit::tick_frame()
{
    uint8_t* core = core_physics_;
    uint8_t* state = cur_phase_;
    // Out of a lip trick (the physics' +0x256: the frame began in one) straight up: into the air with hardly any speed
    // across the ground. Kept while in the air.
    if (prop<uint8_t>(core, 0x256) && prop<int>(state, 0x18) != kStateLip) {
        prop<uint8_t>(state, 0xF4) = 0;
        if (prop<int>(state, 0x18) == kStateAir && std::fabs(entity_->vel_.x) < kStillSpeed &&
            std::fabs(entity_->vel_.z) < kStillSpeed)
            prop<uint8_t>(state, 0xF4) = 1;
    }
    if (prop<int>(state, 0x18) != kStateAir)
        prop<uint8_t>(state, 0xF4) = 0;
    prop<maths::Vec4>(state, 0xF8) = prop<maths::Vec4>(core, 0x58);
    prop<maths::Vec4>(state, 0x108) = prop<maths::Vec4>(core, 0x68);

    // The matrices: orthonormal again about up; the physics' own replaced by the object's if that needed repair.
    entity_->basis_frame_.square_up_axes(1);
    maths::Mat4& physics_matrix = prop<maths::Mat4>(core, 0x18);
    physics_matrix.square_up_axes(1);
    if (entity_->basis_frame_.restore_right_angles())
        physics_matrix = reinterpret_cast<ObjectModule*>(core)->entity_->basis_frame_;
    entity_->display_matrix_ = physics_matrix;

    // The sounds: terrain and state.
    uint8_t* sound = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterSound));
    prop<uint8_t>(sound, 0x1E) = prop<uint8_t>(cur_phase_, 0x58);
    prop<uint8_t>(sound, 0x1C) = prop<uint8_t>(cur_phase_, 0xE1);
    ObjectModule* looping = entity_->fetch_module(kSkaterLoopingSound);
    double x = entity_->vel_.x, z = entity_->vel_.z;
    float speed = static_cast<float>(std::sqrt(x * x + z * z));
    float fraction = static_cast<float>(speed / scaled_physics_value(entity_, kSkaterMaxMaxSpeedStat));
    set_looping_sound_speed(looping, fraction);
    uint8_t* loop = reinterpret_cast<uint8_t*>(looping);
    prop<uint8_t>(loop, 0xFF) = prop<uint8_t>(cur_phase_, 0x70);
    uint8_t& changed = prop<uint8_t>(loop, 0xFD);
    get_note(prop<uint8_t>(loop, 0x100), prop<uint8_t>(cur_phase_, 0x58), changed);
    get_note(prop<uint8_t>(loop, 0x60), prop<uint8_t>(cur_phase_, 0xE1), changed);
    get_note(prop<int>(loop, 0x74), prop<int>(cur_phase_, 0x18), changed);
}

} // namespace objects

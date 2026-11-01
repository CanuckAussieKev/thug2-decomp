// The skater's looping sound component (GameObjects::RiderRollingAudio): see looping_sound.h.

#include "game/rider/looping_sound.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "game/rider/thaw_bmx.h"
#include "platform/stopwatch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "game/rider/physics_control.h"

namespace objects {

uint32_t g_looping_sound_member_functions[6]{0, 0xB5F1E9D2, 0, 0, 0, 0};

namespace {

constexpr uint32_t kSkaterLoopingSound = 0xB5F1E9D2;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D, kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kTurnOn = 0xEDE3935F, kTurnOff = 0x9731E193, kPlayAnim = 0x0B1E7291;
constexpr uint32_t kAnimTags = 0x5DB4115F, kWalkToSkateTransition = 0x910D77C1, kVolumeMult = 0xF1A99B27;
// The terrain sound tables
constexpr uint32_t kTableWheelroll = 0xDAFD1B8C, kTableGrind = 0x29A21517, kTableSlide = 0x81ECDCE5;

constexpr int kStateGround = RiderRollingAudio::kStateGround, kStateAir = RiderRollingAudio::kStateAir,
              kStateRail = RiderRollingAudio::kStateRail;

constexpr float kAirPitchFloor = 45.0f;      // 0x42340000
constexpr float kAirPitchFall = 18.3333340f; // 0x4192AAAB (a second)
constexpr float kPercent = 100.0f;           // 0x0064BF2C
constexpr float kChangeThreshold = 0.5f;     // 0x0064BF28 (volume or pitch: an update when it moved more)
constexpr float kCapsizedFraction = 0.5f;    // the core physics' +0x2EC

template <typename T>
T& prop(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

sound::AudioMixer* audio_mixer_handle() { return sound::g_audio_mixer; }

// The terrain sounds: a table by name, the terrain's entry in it copied into `entry` (false if it has none), the
// entry's volume for a percentage.
uint8_t sound_table(uint32_t name) { return surface_kind::get_table_index(name); }
bool terrain_sound(uint8_t* entry, int terrain, uint8_t table)
{
    return surface_kind::get_terrain_sound(reinterpret_cast<surface_kind::SoundInfo*>(entry),
                                           static_cast<uint8_t>(terrain), table, 0.0f);
}
double sound_volume(uint8_t* entry, float percent)
{
    return surface_kind::get_scaled_volume(reinterpret_cast<const surface_kind::SoundInfo*>(entry), percent, false);
}

uint32_t sound_name_key(uint8_t* entry) { return reinterpret_cast<const surface_kind::SoundInfo*>(entry)->checksum; }
float full_speed_pitch(uint8_t* entry) { return reinterpret_cast<const surface_kind::SoundInfo*>(entry)->max_pitch_; }
float rest_pitch(uint8_t* entry) { return reinterpret_cast<const surface_kind::SoundInfo*>(entry)->min_pitch_; }

// The air sound's entry (a terrain sound entry of its own: 0x84 bytes, its name, the sound's checksum, the pitches at
// full speed and at rest).
struct AirSoundEntry {
    char name[0x68];
    uint32_t checksum;
    float full_speed_pitch_;
    float rest_pitch_;
    uint8_t unused[0x10];
};
static_assert(sizeof(AirSoundEntry) == 0x84, "a terrain sound entry");
AirSoundEntry g_air_sound_entry{"wheels01", 0x24257432, 100.0f, 110.0f};
const uint8_t* air_sound() { return reinterpret_cast<const uint8_t*>(&g_air_sound_entry); }

} // namespace

// 0x00513990
LoopingSoundVolume* LoopingSoundVolume::get_assign(const sound::VolumeRef* other)
{
    std::memcpy(static_cast<sound::VolumeRef*>(this), other, sizeof(sound::VolumeRef));
    return this;
}

// 0x00513AD0
memory::owner<ObjectModule*> RiderRollingAudio::fabricate() { return new RiderRollingAudio; }

// 0x00513860
void RiderRollingAudio::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kSkaterLoopingSound),
                                           g_looping_sound_member_functions, 0x18);
}

// 0x005138E0
RiderRollingAudio::RiderRollingAudio() { kind_tag_ = kSkaterLoopingSound; }

// 0x00513AB0
void RiderRollingAudio::stop_sound()
{
    if (sound_id_) {
        audio_mixer_handle()->stop_sound(sound_id_);
        sound_id_ = 0;
    }
}

// 0x00513B30 (0x00513FA0 the deleting one)
RiderRollingAudio::~RiderRollingAudio()
{
    stop_sound();
    // (prev_levels_'s destructor: nothing)
}

// 0x00513880
void RiderRollingAudio::configure_from_data(script::Struct* params)
{
    air_pitch_fall_ = kAirPitchFall;
    air_pitch_floor_ = kAirPitchFloor;
    params->fetch_float(kVolumeMult, &volume_mult_, 0);
}

// 0x005138B0
void RiderRollingAudio::set_pace_ratio(float fraction)
{
    speed_ratio_ = fraction;
    if (fraction > 1.0f)
        speed_ratio_ = 1.0f;
}

// 0x00513970
void RiderRollingAudio::seal()
{
    physics_control_ = static_cast<RiderBoardPhysics*>(entity_->fetch_module(kSkaterPhysicsControl));
}

// 0x00513F60
void RiderRollingAudio::hold_off(bool suspend)
{
    ObjectModule::hold_off(suspend);
    if (suspend)
        stop_sound();
    else
        sound_changed_ = 1;
}

// 0x005139E0
int RiderRollingAudio::invoke_method_by_name(uint32_t name, script::Struct* /*params*/, script::Script* /*script*/)
{
    switch (name) {
    case kTurnOn:
        if (on_ != 1) {
            hold_off(false);
            on_ = 1;
        }
        return 1;
    case kTurnOff:
        if (on_) {
            hold_off(true);
            on_ = 0;
        }
        return 1;
    case kPlayAnim: {
        // Turned on again by the walk-to-skate animation (its animTags in the skater's tags).
        script::Struct* tags = entity_->tag_table_;
        const RiderPoseState* state = physics_control_->pose_;
        script::Struct* anim_tags = nullptr;
        if (tags && !state->in_vehicle_ && !state->physics_mode_ && tags->read_sub_struct(kAnimTags, &anim_tags, 0) &&
            anim_tags->has_bare_flag(kWalkToSkateTransition)) {
            hold_off(false);
            on_ = 1;
        }
        return 2;
    }
    default:
        return 2;
    }
}

// 0x00513BB0
void RiderRollingAudio::tick_frame()
{
    if (!on_) {
        hold_off(true);
        return;
    }
    LoopingSoundVolume volume;
    // Not original: THAW's BMX (game/rider/thaw_bmx.h; THAW 0x0046A097 / 0x0046A0D3) - on the bike its roll and grind
    // (BIKE_ROLL, BIKE_GRIND) on every terrain, nothing in the air or sliding; chosen every frame, and again from the
    // terrain once off the bike.
    int bike = -1;
    if (skater::thaw_bmx::is_switched_on()) {
        using skater::thaw_bmx::BikeSound;
        BikeSound kind = cur_phase_ == kStateGround                                 ? BikeSound::kRoll
                         : cur_phase_ == kStateRail && !sliding_ && !no_rail_sound_ ? BikeSound::kGrind
                                                                                    : BikeSound::kAir;
        bike = skater::thaw_bmx::bike_sound_of(entity_, kind, sound_);
        if (bike >= 0) {
            have_sound_ = static_cast<uint8_t>(bike);
            sound_changed_ = 1;
        }
    }
    // The sound for the state, chosen again when something changed.
    switch (bike >= 0 ? -1 : cur_phase_) {
    case kStateGround:
        if (sound_changed_) {
            have_sound_ = terrain_sound(sound_, surface_type_, sound_table(kTableWheelroll)) ? 1 : 0;
            sound_changed_ = 0;
        }
        break;
    case kStateAir:
        if (sound_changed_) {
            have_sound_ = 1;
            std::memcpy(sound_, air_sound(), sizeof(sound_));
            sound_changed_ = 0;
        }
        break;
    case kStateRail:
        if (no_rail_sound_) {
            have_sound_ = 0;
        } else {
            const uint32_t table = sliding_ ? kTableSlide : kTableGrind;
            if (sound_changed_) {
                have_sound_ = terrain_sound(sound_, surface_type_, sound_table(table)) ? 1 : 0;
                sound_changed_ = 0;
            }
        }
        break;
    default:
        break;
    }
    if (sound_id_ && (!have_sound_ || playing_checksum_ != sound_name_key(sound_))) {
        audio_mixer_handle()->stop_sound(sound_id_);
        sound_id_ = 0;
    }
    if (!have_sound_)
        return;

    // Its volume from where the skater is - stopped while the skater hasn't moved.
    sound::AudioMixer* sfx = audio_mixer_handle();
    if (entity_->at_point_ != last_pos_) {
        const float dropoff = sfx->get_dropoff_distance(sound_name_key(sound_));
        sfx->fetch_loudness(&volume, &entity_->at_point_, dropoff, 0, nullptr, nullptr);
    } else {
        sfx->stop_sound(sound_id_);
        sound_id_ = 0;
    }
    last_pos_ = entity_->at_point_;

    float pitch = 0.0f;
    if (cur_phase_ == kStateAir && sound_id_) {
        // In the air the playing sound's pitch falls (to the floor, then it's silent).
        if (lean_pitch_angle_ > air_pitch_floor_) {
            lean_pitch_angle_ = static_cast<float>(
                lean_pitch_angle_ - static_cast<double>(frame_clock::get_slomo_frame_seconds()) * air_pitch_fall_);
            pitch = lean_pitch_angle_;
        } else {
            volume.change_muted();
            pitch = lean_pitch_angle_;
        }
    } else {
        // By the speed (half when the core physics' +0x2EC is set).
        auto* core = static_cast<skater::SkaterPhysicsModule*>(entity_->fetch_module(kSkaterCorePhysics));
        if (core && core->point_rail_spin_)
            speed_ratio_ = kCapsizedFraction;
        if (speed_ratio_ > 0.0f) {
            auto percent = static_cast<float>(static_cast<double>(volume_mult_) * speed_ratio_ * kPercent);
            volume.percent_tweak(static_cast<float>(sound_volume(sound_, percent)));
            pitch =
                static_cast<float>((static_cast<double>(full_speed_pitch(sound_)) - rest_pitch(sound_)) * speed_ratio_ +
                                   rest_pitch(sound_));
            lean_pitch_angle_ = pitch;
        } else {
            volume.change_muted();
            pitch = 0.0f;
            lean_pitch_angle_ = 0.0f;
        }
    }

    if (volume.is_audio_off()) {
        stop_sound();
        return;
    }
    if (!sound_id_) {
        static_cast<LoopingSoundVolume*>(&prev_levels_)->get_assign(&volume);
        prev_pitch_value_ = pitch;
        sound_id_ = sfx->emit_sound(sound_name_key(sound_), &volume, pitch, 0, nullptr,
                                    static_cast<int>(reinterpret_cast<uintptr_t>(sound_)));
        if (sound_id_)
            playing_checksum_ = sound_name_key(sound_);
        return;
    }
    // Playing: updated when the volume (its first channel) or the pitch moved by more than a half.
    if (!force_update_ &&
        !(std::fabs(static_cast<double>(volume.lanes_[0]) - prev_levels_.lanes_[0]) > kChangeThreshold) &&
        !(std::fabs(static_cast<double>(pitch) - prev_pitch_value_) > kChangeThreshold))
        return;
    static_cast<LoopingSoundVolume*>(&prev_levels_)->get_assign(&volume);
    prev_pitch_value_ = pitch;
    force_update_ = 0;
    sfx->set_sound_params_scaled(sound_id_, &volume, pitch);
}

} // namespace objects

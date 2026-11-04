// The skater's looping sound component (GameObjects::RiderRollingAudio): see looping_sound.h.

#include "game/rider/looping_sound.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gsl/pointers>
#include <array>

#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
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
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {
namespace {
constexpr uint32_t kSkaterLoopingSoundName = 0xB5F1E9D2; // SkaterLoopingSound
} // namespace

std::array<script::MemberFunctionEntry, 1>& looping_sound_member_functions()
{
    static std::array<script::MemberFunctionEntry, 1> looping_sound_member_functions{{
        {.callsign_ = 0, .owner_kind_ = kSkaterLoopingSoundName}}};
    return looping_sound_member_functions;
}

namespace {

constexpr uint32_t kSkaterLoopingSound = 0xB5F1E9D2;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D, kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kTurnOn = 0xEDE3935F, kTurnOff = 0x9731E193, kPlayAnim = 0x0B1E7291;
constexpr uint32_t kAnimTags = 0x5DB4115F, kWalkToSkateTransition = 0x910D77C1, kVolumeMult = 0xF1A99B27;
// The terrain sound tables
constexpr uint32_t kTableWheelroll = 0xDAFD1B8C, kTableGrind = 0x29A21517, kTableSlide = 0x81ECDCE5;

constexpr int kStateGround = RiderRollingAudio::kStateGround, kStateAir = RiderRollingAudio::kStateAir,
              kStateRail = RiderRollingAudio::kStateRail;

constexpr float kAirPitchFloor = 45.0F;      // 0x42340000
constexpr float kAirPitchFall = 18.3333340F; // 0x4192AAAB (a second)
constexpr float kPercent = 100.0F;           // 0x0064BF2C
constexpr float kChangeThreshold = 0.5F;     // 0x0064BF28 (volume or pitch: an update when it moved more)
constexpr float kCapsizedFraction = 0.5F;    // the core physics' +0x2EC

sound::AudioMixer* audio_mixer_handle() { return sound::get_audio_mixer(); }

// The terrain sounds: a table by name, the terrain's entry in it copied into `entry` (false if it has none), the
// entry's volume for a percentage.
uint8_t sound_table(uint32_t name) { return surface_kind::get_table_index(name); }
bool terrain_sound(surface_kind::SoundInfo* entry, int terrain, uint8_t table)
{
    return surface_kind::get_terrain_sound(entry,
                                           {.terrain = static_cast<uint8_t>(terrain), .table = table, .value = 0.0F});
}
double sound_volume(const surface_kind::SoundInfo* entry, float percent)
{
    return surface_kind::get_scaled_volume(entry, percent, false);
}

uint32_t sound_name_key(const surface_kind::SoundInfo* entry) { return entry->checksum; }
float full_speed_pitch(const surface_kind::SoundInfo* entry) { return entry->max_pitch_; }
float rest_pitch(const surface_kind::SoundInfo* entry) { return entry->min_pitch_; }

// The air sound's entry (a terrain sound entry of its own: 0x84 bytes, its name, the sound's checksum, the pitches at
// full speed and at rest).
const surface_kind::SoundInfo kAirSoundEntry{"wheels01", 0x24257432, 100.0F, 110.0F};
const surface_kind::SoundInfo& air_sound() { return kAirSoundEntry; }

} // namespace

// 0x00513990
LoopingSoundVolume* LoopingSoundVolume::get_assign(const sound::VolumeRef* other)
{
    std::memcpy(static_cast<sound::VolumeRef*>(this), other, sizeof(sound::VolumeRef));
    return this;
}

// 0x00513AD0
gsl::owner<ObjectModule*> RiderRollingAudio::fabricate() { return new RiderRollingAudio; }

// 0x00513860
void RiderRollingAudio::publish_method_table()
{
    script::register_member_function_table(looping_sound_member_functions());
}

// 0x005138E0
RiderRollingAudio::RiderRollingAudio() { set_kind_tag(kSkaterLoopingSound); }

// 0x00513AB0
void RiderRollingAudio::stop_sound()
{
    if (sound_id_ != 0U) {
        sound::AudioMixer::stop_sound(sound_id_);
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
    if (fraction > 1.0F) {
        speed_ratio_ = 1.0F;
    }
}

// 0x00513970
void RiderRollingAudio::seal()
{
    physics_control_ = core::down_cast<RiderBoardPhysics>(entity()->fetch_module(kSkaterPhysicsControl));
}

// 0x00513F60
void RiderRollingAudio::hold_off(bool suspend)
{
    ObjectModule::hold_off(suspend);
    if (suspend) {
        stop_sound();
    } else {
        sound_changed_ = 1;
    }
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
        if (on_ != 0U) {
            hold_off(true);
            on_ = 0;
        }
        return 1;
    case kPlayAnim: {
        // Turned on again by the walk-to-skate animation (its animTags in the skater's tags).
        script::Struct* tags = entity()->tag_table();
        const RiderPoseState* state = physics_control_->pose();
        script::Struct* anim_tags = nullptr;
        if (tags && (state->in_vehicle() == 0U) &&
            state->physics_mode() == objects::RiderPoseState::PhysicsMode::kSkating &&
            tags->read_sub_struct(kAnimTags, &anim_tags, 0) && anim_tags->has_bare_flag(kWalkToSkateTransition)) {
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
    if (on_ == 0U) {
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
        BikeSound kind = cur_phase_ == kStateGround                                               ? BikeSound::kRoll
                         : cur_phase_ == kStateRail && (sliding_ == 0U) && (no_rail_sound_ == 0U) ? BikeSound::kGrind
                                                                                                  : BikeSound::kAir;
        bike = skater::thaw_bmx::bike_sound_of(entity(), kind, &sound_);
        if (bike >= 0) {
            have_sound_ = static_cast<uint8_t>(bike);
            sound_changed_ = 1;
        }
    }
    // The sound for the state, chosen again when something changed.
    switch (bike >= 0 ? -1 : cur_phase_) {
    case kStateGround:
        if (sound_changed_ != 0U) {
            have_sound_ = terrain_sound(&sound_, surface_type_, sound_table(kTableWheelroll)) ? 1 : 0;
            sound_changed_ = 0;
        }
        break;
    case kStateAir:
        if (sound_changed_ != 0U) {
            have_sound_ = 1;
            sound_ = air_sound();
            sound_changed_ = 0;
        }
        break;
    case kStateRail:
        if (no_rail_sound_ != 0U) {
            have_sound_ = 0;
        } else {
            const uint32_t table = (sliding_ != 0U) ? kTableSlide : kTableGrind;
            if (sound_changed_ != 0U) {
                have_sound_ = terrain_sound(&sound_, surface_type_, sound_table(table)) ? 1 : 0;
                sound_changed_ = 0;
            }
        }
        break;
    default:
        break;
    }
    if ((sound_id_ != 0U) && ((have_sound_ == 0U) || playing_checksum_ != sound_name_key(&sound_))) {
        sound::AudioMixer::stop_sound(sound_id_);
        sound_id_ = 0;
    }
    if (have_sound_ == 0U) {
        return;
    }

    // Its volume from where the skater is - stopped while the skater hasn't moved.
    const sound::AudioMixer* sfx = audio_mixer_handle();
    if (entity()->at_point() != last_pos_) {
        const float dropoff = sound::AudioMixer::get_dropoff_distance(sound_name_key(&sound_));
        sfx->fetch_loudness(&volume, &entity()->at_point(), {.dropoff = dropoff, .falloff = 0}, nullptr, nullptr);
    } else {
        sound::AudioMixer::stop_sound(sound_id_);
        sound_id_ = 0;
    }
    last_pos_ = entity()->at_point();

    float pitch = 0.0F;
    if (cur_phase_ == kStateAir && (sound_id_ != 0U)) {
        // In the air the playing sound's pitch falls (to the floor, then it's silent).
        if (lean_pitch_angle_ > air_pitch_floor_) {
            lean_pitch_angle_ =
                static_cast<float>(lean_pitch_angle_ - (frame_clock::get_slomo_frame_seconds() * air_pitch_fall_));
            pitch = lean_pitch_angle_;
        } else {
            volume.change_muted();
            pitch = lean_pitch_angle_;
        }
    } else {
        // By the speed (half when the core physics' +0x2EC is set).
        auto* core = core::down_cast<skater::SkaterPhysicsModule>(entity()->fetch_module(kSkaterCorePhysics));
        if (core && (core->point_rail_spin() != 0U)) {
            speed_ratio_ = kCapsizedFraction;
        }
        if (speed_ratio_ > 0.0F) {
            auto percent = static_cast<float>(static_cast<double>(volume_mult_) * speed_ratio_ * kPercent);
            volume.percent_tweak(static_cast<float>(sound_volume(&sound_, percent)));
            pitch = static_cast<float>(
                ((static_cast<double>(full_speed_pitch(&sound_)) - rest_pitch(&sound_)) * speed_ratio_) +
                rest_pitch(&sound_));
            lean_pitch_angle_ = pitch;
        } else {
            volume.change_muted();
            pitch = 0.0F;
            lean_pitch_angle_ = 0.0F;
        }
    }

    if (volume.is_audio_off()) {
        stop_sound();
        return;
    }
    if (sound_id_ == 0U) {
        core::down_cast<LoopingSoundVolume>(&prev_levels_)->get_assign(&volume);
        prev_pitch_value_ = pitch;
        sound_id_ = sound::AudioMixer::emit_sound(sound_name_key(&sound_), &volume, {.pitch = pitch, .unique_id_ = 0},
                                                  nullptr, 0); // (the original passed the entry's address: unused)
        if (sound_id_ != 0U) {
            playing_checksum_ = sound_name_key(&sound_);
        }
        return;
    }
    // Playing: updated when the volume (its first channel) or the pitch moved by more than a half.
    if ((force_update_ == 0U) &&
        !(std::fabs(static_cast<double>(core::at(volume.lanes(), 0)) - core::at(prev_levels_.lanes(), 0)) >
          kChangeThreshold) &&
        !(std::fabs(static_cast<double>(pitch) - prev_pitch_value_) > kChangeThreshold)) {
        return;
    }
    core::down_cast<LoopingSoundVolume>(&prev_levels_)->get_assign(&volume);
    prev_pitch_value_ = pitch;
    force_update_ = 0;
    sound::AudioMixer::set_sound_params_scaled(sound_id_, &volume, pitch);
}

} // namespace objects

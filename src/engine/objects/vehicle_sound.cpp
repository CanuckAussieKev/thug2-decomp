// The VehicleSound component: see vehicle_sound.h. From the disassembly of 0x0043C540-0x0043D2AF.

#include "engine/objects/vehicle_sound.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <gsl/pointers>
#include <gsl/assert>

#include "engine/memory/mem.h"
#include "engine/objects/entity.h"
#include "engine/objects/sound_component.h"
#include "engine/objects/vehicle_component.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "game/park_editing/park_editor.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/vehicle_sound.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"

namespace objects {

namespace {
constexpr uint32_t kEngineSound = 0x2A19E830; // engine_sound
constexpr uint32_t kTireSound = 0xE798FE80; // tire_sound
constexpr uint32_t kCollideSoundInfo = 0x36FA68D2; // collide_sound (letter case unknown)
constexpr uint32_t kEngineSoundName = 0x306F8A9F; // EngineSound
constexpr uint32_t kTireSoundName = 0x667FA96A; // TireSound
constexpr uint32_t kGears = 0x83273E6B; // Gears
constexpr uint32_t kUpshiftPoint = 0xFAD20901; // UpshiftPoint
constexpr uint32_t kDownshiftPoint = 0xFCF5C8A2; // DownshiftPoint
constexpr uint32_t kBottomRPM = 0x735B5CE4; // BottomRPM
constexpr uint32_t kTopRPM = 0xE59A89A6; // TopRPM
constexpr uint32_t kCollideSound = 0xBF8E0ACE; // CollideSound
constexpr uint32_t kCAGEngineSound = 0xF5406E1A; // CAGEngineSound
constexpr uint32_t kCAGTireSound = 0x060E93A2; // CAGTireSound
constexpr uint32_t kCAGCollideSound = 0x22995285; // CAGCollideSound
constexpr uint32_t kIdleEngineEffectiveSpeedFactor = 0xEC67BC97; // idle_engine_effective_speed_factor
constexpr uint32_t kPercentName = 0x9E497FC6; // Percent
constexpr uint32_t kStrength = 0x29461E27; // Strength
constexpr uint32_t kVehicleBodycollision = 0xC4A60423; // vehicle_bodycollision (letter case unknown)

constexpr uint32_t kVehicle = 0xE47F1B79;
constexpr uint32_t kSound = 0x07713C7B;
constexpr uint32_t kRates = 0x38EC37AC;       // PlayerVehicleSoundGlobalParameters
constexpr uint32_t kAllSettings = 0x3C2DA7F5; // PlayerVehicleSounds
constexpr uint32_t kParkEditorOff = 0xD443A2BC;

const float kNoWheel = std::bit_cast<float>(0x60AD78ECU); // 0x0064682C: 1e20
const float kToMph = std::bit_cast<float>(0x3D68BA2FU);   // 0x00646828: 1 / 17.6
const float kPercent = 100.0F;                            // 0x006463D4
const double kTwoTo32 = 4294967296.0;                     // 0x006453B4 (an unsigned clock difference)

sound::AudioMixer* audio_mixer_handle() { return sound::get_audio_mixer(); }
uint32_t unsigned_clock_offset() { return static_cast<uint32_t>(objects::clock_offset()); }

// A float of the global structure kRates (asserted).
float get_rate(uint32_t name)
{
    float value = 0.0F;
    script::read_sub_struct(kRates)->fetch_float(name, &value, 1);
    return value;
}
float get_setting(script::Struct* settings, uint32_t name)
{
    float value = 0.0F;
    settings->fetch_float(name, &value, 1);
    return value;
}

// The clock now (with its offset) less `then`, wrapping as the original's 32-bit sums do.
uint32_t get_since(int clock, int then)
{
    return static_cast<uint32_t>(clock) + (unsigned_clock_offset() - static_cast<uint32_t>(then));
}
int get_now(int clock) { return static_cast<int>(static_cast<uint32_t>(clock) + unsigned_clock_offset()); }
// An unsigned millisecond difference as a double (the original's FILD and its 2^32 fix for negatives).
double as_unsigned(uint32_t difference)
{
    const int value = static_cast<int>(difference);
    return value < 0 ? value + kTwoTo32 : value;
}

// A fraction kept to [0, 1] as the original does it: over 1 -> 1 (compared unrounded); then, rounded, under 0 -> 0.
float get_fraction(double value)
{
    if (value > 1.0) {
        return 1.0F;
    }
    auto rounded = static_cast<float>(value);
    return rounded < 0.0F ? 0.0F : rounded;
}

// The Sound component's 0x00433240 (not rewritten yet): the sound a structure describes played.
void play_sound_from_structure(ObjectModule* sound, script::Struct* description)
{
    core::down_cast<AudioEmitterModule>(sound)->emit_sound(description);
}

} // namespace

// 0x0043CFC0
gsl::owner<ObjectModule*> EngineAudioModule::fabricate() { return new EngineAudioModule; }

// 0x0043C590
EngineAudioModule::EngineAudioModule() { set_kind_tag(kType); }

// 0x0043C5F0 (0x0043D020 the deleting one)
EngineAudioModule::~EngineAudioModule()
{
    if (engine_sound_ != 0U) {
        sound::AudioMixer::stop_sound(engine_sound_);
        engine_sound_ = 0;
    }
    if (skid_sound_ != 0U) {
        sound::AudioMixer::stop_sound(skid_sound_);
        skid_sound_ = 0;
    }
}

// 0x0043C670
void EngineAudioModule::configure_from_data(script::Struct* /*params*/)
{
    park_editor_level_ = park_editing::ParkBuilderMode::sole()->editor_state() != kParkEditorOff;
}

// 0x0043D050
void EngineAudioModule::seal()
{
    vehicle_ptr_ = core::down_cast<DrivableModule>(entity()->fetch_module(kVehicle));
    sound_ptr_ = entity()->fetch_module(kSound);
    read_settings();
}

// 0x0043D290
void EngineAudioModule::tick_frame()
{
    if (!settings_) {
        return;
    }
    tick_engine();
    update_skid();
    update_collide_sound();
}

// 0x0043C540
void EngineAudioModule::fetch_diagnostics(script::Struct* info)
{
    info->put_name_key(checksums::Name{kEngineSound}, engine_sound_name_);
    info->put_name_key(checksums::Name{kTireSound}, skid_sound_name_);
    info->put_sub_struct(kCollideSoundInfo, collide_sound_);
    // (then the base's fetch_diagnostics, which is empty: 0x005BAB90)
}

// 0x0043CE70
void EngineAudioModule::read_settings()
{
    script::read_sub_struct(kAllSettings)->read_sub_struct(vehicle_ptr_->sounds_name(), &settings_, 1);
    settings_->read_hash_value(kEngineSoundName, &engine_sound_name_, 0);
    settings_->read_hash_value(kTireSoundName, &skid_sound_name_, 0);
    script::Array* gears = nullptr;
    settings_->fetch_list(kGears, &gears, 1);
    gear_count_ = static_cast<int>(gears->extent());
    for (int i = 0; i < gear_count_; ++i) {
        auto* gear = gears->struct_at(static_cast<uint32_t>(i));
        gear->fetch_float(kUpshiftPoint, &core::at(gear_steps_, i).up_at_, 1);
        gear->fetch_float(kDownshiftPoint, &core::at(gear_steps_, i).down_at_, 1);
        gear->fetch_float(kBottomRPM, &core::at(gear_steps_, i).low_rpm_, 1);
        gear->fetch_float(kTopRPM, &core::at(gear_steps_, i).high_rpm_, 1);
    }
    settings_->read_sub_struct(kCollideSound, &collide_sound_, 1);
    if (park_editor_level_) {
        script::Struct* park = script::read_sub_struct(kRates);
        park->read_hash_value(kCAGEngineSound, &engine_sound_name_, 1);
        park->read_hash_value(kCAGTireSound, &skid_sound_name_, 1);
        park->read_sub_struct(kCAGCollideSound, &collide_sound_, 1);
    }
}

// 0x0043C690
void EngineAudioModule::tick_engine()
{
    auto frame = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    const DrivableModule* vehicle = vehicle_ptr_;
    // The slowest driven wheel (always four looked at, the last first).
    float slowest = kNoWheel;
    for (int i = 3; i >= 0; --i) {
        const DrivableModule::Wheel& wheel = core::at(vehicle->get_wheels(), i);
        if (!wheel.drive) {
            continue;
        }
        auto speed = static_cast<float>(std::fabs(static_cast<double>(wheel.spin)) * wheel.radius);
        if (!(slowest < speed)) {
            slowest = speed;
        }
    }
    auto speed = static_cast<float>(slowest * kToMph);
    if (!(vehicle->throttle() || vehicle->get_reverse()) || vehicle->brake()) {
        speed = static_cast<float>(get_rate(kIdleEngineEffectiveSpeedFactor) * static_cast<double>(speed));
    }

    // The speed eased towards the wheels', each step limited by the rates (as the original compares them, a step
    // the wrong way is kept and one the right way isn't).
    const double follow = get_rate(0x02A7095B) * static_cast<double>(frame);
    const double eased = ((static_cast<double>(speed_) - speed) * follow) + speed;
    speed_ = static_cast<float>(eased);
    if (eased < speed) {
        const double step = (static_cast<double>(get_rate(0xCA5EFD91)) * frame) + speed_;
        if (!(step > speed_)) {
            speed_ = static_cast<float>(step);
        }
    } else {
        const double step = static_cast<double>(speed_) - (static_cast<double>(get_rate(0x77C8E7C8)) * frame);
        if (!(step < speed_)) {
            speed_ = static_cast<float>(step);
        }
    }

    // The gear: shifted at the gear's speeds while a wheel touches; back to the first after a while in the air.
    if (vehicle_ptr_->wheels_in_contact() != 0) {
        const int gear = gear_;
        if (gear < gear_count_ - 1 && speed_ > core::at(gear_steps_, gear).up_at_) {
            gear_ = gear + 1;
            shift_time_ = get_now(frame_clock::game_clock_elapsed());
        } else if (gear > 0 && speed_ < core::at(gear_steps_, gear).down_at_) {
            gear_ = gear - 1;
            shift_time_ = get_now(frame_clock::game_clock_elapsed());
        }
    } else {
        const float delay = get_rate(0xC0F451D4);
        if (vehicle_ptr_->air_time() > delay) {
            gear_ = 0;
        }
    }

    // Just after a shift the engine is quieter (from 0xBFA32092 of full, back to full over 0xFB6BD20E ms).
    const float shift_time = get_rate(0xFB6BD20E);
    const int shifted = shift_time_;
    const uint32_t since_shift = get_since(frame_clock::game_clock_elapsed(), shifted);
    const float quietest = get_rate(0xBFA32092);
    const double recovery = (as_unsigned(since_shift) / shift_time * (1.0 - quietest)) + quietest;
    const float loudness = recovery > 1.0 ? 1.0F : static_cast<float>(recovery);

    // The engine's speed: where the speed is between the last gear's shift point and this one's, across this gear's
    // pitch range; eased.
    const int gear = gear_;
    const double low = (gear != 0) ? core::at(gear_steps_, gear - 1).up_at_ : 0.0;
    const double across = (static_cast<double>(speed_) - low) / (static_cast<double>(core::at(gear_steps_, gear).up_at_) - low);
    auto target = static_cast<float>(
        ((static_cast<double>(core::at(gear_steps_, gear).high_rpm_) - core::at(gear_steps_, gear).low_rpm_) * across) +
        core::at(gear_steps_, gear).low_rpm_);
    const double ease = static_cast<double>(get_rate(0x3DD9C040)) * frame;
    rpm_ = static_cast<float>(((static_cast<double>(target) - rpm_) * ease) + rpm_);
    const float high_rpm = get_setting(settings_, 0x95DF9449);
    const float low_rpm = get_setting(settings_, 0x05AC84A3);
    const float rpm = get_fraction((static_cast<double>(rpm_) - low_rpm) / (static_cast<double>(high_rpm) - low_rpm));

    sound::VolumeRef volume;
    const sound::AudioMixer* sfx = audio_mixer_handle();
    sfx->fetch_loudness(&volume, &entity()->at_point(),
                        {.dropoff = sound::AudioMixer::get_dropoff_distance(engine_sound_name_), .falloff = 0}, nullptr,
                        nullptr);
    const float loudest = get_setting(settings_, 0x288B029D);
    const float quiet = get_setting(settings_, 0xB8F81277);
    volume.percent_tweak(static_cast<float>((((static_cast<double>(loudest) - quiet) * rpm) + quiet) * loudness));
    const float high_pitch = get_setting(settings_, 0x9F46344A);
    const float low_pitch = get_setting(settings_, 0x2660AF3B);
    auto pitch = static_cast<float>(((static_cast<double>(high_pitch) - low_pitch) * rpm) + low_pitch);
    if (engine_sound_ == 0U) {
        engine_sound_ =
            sound::AudioMixer::emit_sound(engine_sound_name_, &volume, {.pitch = pitch, .unique_id_ = 0}, nullptr, 0);
    } else {
        sound::AudioMixer::set_sound_params_scaled(engine_sound_, &volume, pitch);
    }
    // (VolumeRef's destructor is empty: 0x005480A0)
}

// 0x0043CBC0
void EngineAudioModule::update_skid()
{
    // The fastest slip of a sliding wheel (always four looked at, the last first), while braking.
    float slip = 0.0F;
    const DrivableModule* vehicle = vehicle_ptr_;
    if (vehicle->brake() || vehicle->get_handbrake()) {
        for (int i = 3; i >= 0; --i) {
            const DrivableModule::Wheel& wheel = core::at(vehicle->get_wheels(), i);
            const DrivableModule::WheelState state = wheel.state;
            const bool sliding =
                state == DrivableModule::WheelState::kStaticToDynamic || state == DrivableModule::WheelState::kDynamic ||
                ((state == DrivableModule::WheelState::kHandbrakeLocked || state == DrivableModule::WheelState::kHandbrakeThrottle) &&
                 wheel.slip > wheel.max_static_grip_vel_);
            if (sliding && !(slip > wheel.slip)) {
                slip = wheel.slip;
            }
        }
    }
    const float rate = get_rate(0x95FBE774);
    const double ease = frame_clock::get_slomo_frame_seconds() * rate;
    skid_ = static_cast<float>(((static_cast<double>(slip) - skid_) * ease) + skid_);
    const float full_slip = get_setting(settings_, 0x28F1AAD0);
    const float level = get_fraction(static_cast<double>(skid_) / full_slip);

    sound::VolumeRef volume;
    const sound::AudioMixer* sfx = audio_mixer_handle();
    sfx->fetch_loudness(&volume, &entity()->at_point(),
                        {.dropoff = sound::AudioMixer::get_dropoff_distance(skid_sound_name_), .falloff = 0}, nullptr,
                        nullptr);
    const float loudest = get_setting(settings_, 0x42EA5746);
    const float quiet = get_setting(settings_, 0x43E6CDF2);
    volume.percent_tweak(static_cast<float>(((static_cast<double>(loudest) - quiet) * level) + quiet));
    const float high_pitch = get_setting(settings_, 0xE68BE784);
    const float low_pitch = get_setting(settings_, 0x76F8F76E);
    auto pitch = static_cast<float>(((static_cast<double>(high_pitch) - low_pitch) * level) + low_pitch);
    if (!volume.is_audio_off()) {
        if (skid_sound_ == 0U) {
            skid_sound_ =
                sound::AudioMixer::emit_sound(skid_sound_name_, &volume, {.pitch = pitch, .unique_id_ = 0}, nullptr, 0);
        } else {
            sound::AudioMixer::set_sound_params_scaled(skid_sound_, &volume, pitch);
        }
    } else if (skid_sound_ != 0U) {
        sound::AudioMixer::stop_sound(skid_sound_);
        skid_sound_ = 0;
    }
}

// 0x0043D090
void EngineAudioModule::update_collide_sound()
{
    const int last = collide_time_;
    const float delay = get_rate(0x71B4B9FE);
    const uint32_t since = get_since(frame_clock::game_clock_elapsed(), last);
    if (as_unsigned(since) < delay) {
        return;
    }
    const float full_collision = get_setting(settings_, 0xCEA59D0B);
    auto strength = static_cast<float>(static_cast<double>(vehicle_ptr_->collision_impulse()) / full_collision);
    const float cutoff = get_rate(0xF6981837);
    if (!(strength > cutoff)) {
        return;
    }
    collide_sound_->push_float(checksums::Name{kPercentName}, static_cast<float>(strength * kPercent));
    play_sound_from_structure(sound_ptr_, collide_sound_);
    collide_time_ = get_now(frame_clock::game_clock_elapsed());

    // The skater told how hard (0-1). (Out of memory, the original adds to a null structure - kept.)
    gsl::owner<script::Struct*> memory = script::spawn_param_bag();
    script::Struct* event = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    const float level = strength > 1.0F ? 1.0F : std::max(strength, 0.0F);
    Expects(event != nullptr);
    event->push_float(checksums::Name{kStrength}, level);
    vehicle_ptr_->rider_ref()->post_own_event(kVehicleBodycollision, event);
    if (event) {
        event->get_clear();
        script::delete_struct(event);
    }
}

} // namespace objects

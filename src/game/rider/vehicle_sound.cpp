// The skater's vehicle sound component: its engine and skid sounds. See vehicle_sound.h.

#include "game/rider/vehicle_sound.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "game/park_editing/park_editor.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"

namespace objects {

uint32_t g_vehicle_sound_member_functions[24]{0xB8451886, 0x1859F772, 0, 0, 0, 0, 0x0F6E8120, 0x1859F772, 0, 0, 0, 0,
                                              0xF876FAE9, 0x1859F772, 0, 0, 0, 0, 0,          0x1859F772, 0, 0, 0, 0};

namespace {

constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kSound = 0x07713C7B;
constexpr uint32_t kInput = 0x27D7CD28;

constexpr uint32_t kRates = 0x38EC37AC;       // the global structure of rates and limits (and the park sounds)
constexpr uint32_t kAllSettings = 0x3C2DA7F5; // the global structure of vehicles' settings
constexpr uint32_t kParkEditorOff = 0xD443A2BC;

const float kSpeedScale = std::bit_cast<float>(0x40400000u); // 0x006467A8: 3
const float kToMph = std::bit_cast<float>(0x3D68BA2Fu);      // 0x00646828: 1 / 17.6
const double kTwoTo32 = 4294967296.0;                        // 0x006453B4 (an unsigned clock difference)

template <typename T>
T& prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

sound::AudioMixer* audio_mixer_handle() { return sound::g_audio_mixer; }

// The game clock (ms) with its offset, as the original reads them: the clock, then the offset.
int get_clock() { return frame_clock::game_clock_elapsed(); }
int clock_offset() { return g_clock_offset; }

// A float of the global structure kRates (asserted).
float get_rate(uint32_t name)
{
    float value;
    script::read_sub_struct(kRates)->fetch_float(name, &value, 1);
    return value;
}
float get_setting(script::Struct* settings, uint32_t name)
{
    float value;
    settings->fetch_float(name, &value, 1);
    return value;
}

// The object's speed as the original computes it (x, y, then z), unrounded, times 3.
double get_pace(const maths::Vec4& vel)
{
    double x = vel.x, y = vel.y, z = vel.z;
    return std::sqrt((x * x + y * y) + z * z) * kSpeedScale;
}

// The clock now (with its offset) less `then`, wrapping as the original's 32-bit sums do.
int get_since(int clock, int offset, int then)
{
    return static_cast<int>(static_cast<uint32_t>(clock) +
                            (static_cast<uint32_t>(offset) - static_cast<uint32_t>(then)));
}
int get_now(int clock, int offset)
{
    return static_cast<int>(static_cast<uint32_t>(clock) + static_cast<uint32_t>(offset));
}

// An unsigned millisecond difference as a double (the original's FILD and its 2^32 fix for negatives).
double Unsigned(int difference) { return difference < 0 ? difference + kTwoTo32 : difference; }

// A fraction kept to [0, 1] as both sounds do it: over 1 -> 1 (compared unrounded); then, rounded, under 0 -> 0.
float get_fraction(double value)
{
    if (value > 1.0)
        return 1.0f;
    float rounded = static_cast<float>(value);
    return rounded < 0.0f ? 0.0f : rounded;
}

} // namespace

// 0x0051B880
ObjectModule* __cdecl SkaterVehicleSoundComponent::fabricate() { return new SkaterVehicleSoundComponent; }

// 0x0051AD90
void __cdecl SkaterVehicleSoundComponent::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kType), g_vehicle_sound_member_functions, 0x18);
}

// 0x0051ADD0
SkaterVehicleSoundComponent::SkaterVehicleSoundComponent()
{
    sound_ = nullptr;
    settings_ = nullptr;
    park_editor_level_ = false;
    speed_ = 0.0f;
    gear_ = 0;
    shift_time_ = 0;
    unknownb4_ = 0;
    skid_ = 0.0f;
    unknownbc_ = 0;
    engine_sound_ = 0;
    skid_sound_ = 0;
    kind_tag_ = kType;
    settings_name_ = 0x1CA1FF20;
    silenced_ = true;
}

// 0x0051ADB0 (0x0051AE30 the deleting one)
SkaterVehicleSoundComponent::~SkaterVehicleSoundComponent() {}

// 0x0051AE60
void SkaterVehicleSoundComponent::configure_from_data(script::Struct*)
{
    park_editor_level_ = prop<uint32_t>(static_cast<void*>(park_editing::g_park_editor), 0x40) != kParkEditorOff;
}

// 0x0051B8E0
void SkaterVehicleSoundComponent::seal()
{
    core_physics_ = static_cast<skater::SkaterPhysicsModule*>(entity_->fetch_module(kSkaterCorePhysics));
    sound_ = entity_->fetch_module(kSound);
    input_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kInput));
    read_settings();
}

// 0x0051B920
void SkaterVehicleSoundComponent::tick_frame()
{
    if (!static_cast<bool>(core_physics_->on_vehicle_) || silenced_ || !settings_)
        return;
    tick_engine();
    update_skid();
    update_unused();
}

// 0x0051B670
void SkaterVehicleSoundComponent::read_settings()
{
    script::read_sub_struct(kAllSettings)->read_sub_struct(settings_name_, &settings_, 1);
    settings_->read_hash_value(0x306F8A9F, &engine_sound_name_, 0);
    settings_->read_hash_value(0x667FA96A, &skid_sound_name_, 0);
    script::Array* gears;
    settings_->fetch_list(0x83273E6B, &gears, 1);
    gear_count_ = static_cast<int>(gears->extent_);
    for (int i = 0; i < gear_count_; ++i) {
        auto* gear = reinterpret_cast<script::Struct*>(gears->fetch_part(static_cast<uint32_t>(i)));
        gear->fetch_float(0xFAD20901, &gear_steps_[i].up_at_, 1);
        gear->fetch_float(0xFCF5C8A2, &gear_steps_[i].down_at_, 1);
        gear->fetch_float(0x735B5CE4, &gear_steps_[i].low_rpm_, 1);
        gear->fetch_float(0xE59A89A6, &gear_steps_[i].high_rpm_, 1);
    }
    settings_->read_sub_struct(0xBF8E0ACE, &extra_, 1);
    if (park_editor_level_) {
        script::Struct* park = script::read_sub_struct(kRates);
        park->read_hash_value(0xF5406E1A, &engine_sound_name_, 1);
        park->read_hash_value(0x060E93A2, &skid_sound_name_, 1);
        park->read_sub_struct(0x22995285, &extra_, 1);
    }
}

// 0x0051AE80
void SkaterVehicleSoundComponent::tick_engine()
{
    float frame = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    float speed = static_cast<float>(get_pace(entity_->vel_) * kToMph);
    if (!prop<bool>(input_, 0x1C0) || prop<bool>(input_, 0x40))
        speed = static_cast<float>(get_rate(0xEC67BC97) * static_cast<double>(speed));

    // The speed eased towards the object's, each step limited by the rates (as the original compares them, a step
    // the wrong way is kept and one the right way isn't).
    double follow = get_rate(0x02A7095B);
    double eased = (static_cast<double>(speed_) - speed) * (follow * frame) + speed;
    speed_ = static_cast<float>(eased);
    if (eased < speed) {
        double step = static_cast<double>(speed_) + static_cast<double>(get_rate(0xCA5EFD91)) * frame;
        if (!(step > speed_))
            speed_ = static_cast<float>(step);
    } else {
        double step = static_cast<double>(speed_) - static_cast<double>(get_rate(0x77C8E7C8)) * frame;
        if (!(step < speed_))
            speed_ = static_cast<float>(step);
    }

    // The gear: back to the first a while after driving stopped, else shifted at the gear's speeds.
    if (core_physics_->state_->cur_phase_ == 0) {
        int last_driven = static_cast<int>(core_physics_->air_start_);
        float delay = get_rate(0xC0F451D4);
        int clock = get_clock();
        if (Unsigned(get_since(clock, clock_offset(), last_driven)) > delay)
            gear_ = 0;
    } else {
        int gear = gear_;
        if (gear < gear_count_ - 1 && speed_ > gear_steps_[gear].up_at_) {
            gear_ = gear + 1;
            shift_time_ = get_now(get_clock(), clock_offset());
        } else if (gear > 0 && speed_ < gear_steps_[gear].down_at_) {
            gear_ = gear - 1;
            shift_time_ = get_now(get_clock(), clock_offset());
        }
    }

    // Just after a shift the engine is quieter (from 0xBFA32092 of full, back to full over 0xFB6BD20E ms).
    float shift_time = get_rate(0xFB6BD20E);
    int shifted = shift_time_;
    int clock = get_clock();
    int since_shift = get_since(clock, clock_offset(), shifted);
    float quietest = get_rate(0xBFA32092);
    double recovery = Unsigned(since_shift) / shift_time * (1.0 - quietest) + quietest;
    float loudness = recovery > 1.0 ? 1.0f : static_cast<float>(recovery);

    // The engine's speed: where the speed is between the last gear's shift point and this one's, across this gear's
    // pitch range; eased.
    int gear = gear_;
    double low = gear ? gear_steps_[gear - 1].up_at_ : 0.0;
    double across = (static_cast<double>(speed_) - low) / (static_cast<double>(gear_steps_[gear].up_at_) - low);
    float target =
        static_cast<float>((static_cast<double>(gear_steps_[gear].high_rpm_) - gear_steps_[gear].low_rpm_) * across +
                           gear_steps_[gear].low_rpm_);
    double ease = static_cast<double>(get_rate(0x3DD9C040)) * frame;
    rpm_ = static_cast<float>((static_cast<double>(target) - rpm_) * ease + rpm_);
    float high_rpm = get_setting(settings_, 0x95DF9449);
    float low_rpm = get_setting(settings_, 0x05AC84A3);
    float rpm = get_fraction((static_cast<double>(rpm_) - low_rpm) / (static_cast<double>(high_rpm) - low_rpm));

    sound::VolumeRef volume;
    volume.prepare_all();
    sound::AudioMixer* sfx = audio_mixer_handle();
    sfx->fetch_loudness(&volume, &entity_->at_point_, sfx->get_dropoff_distance(engine_sound_name_), 0, nullptr,
                        nullptr);
    float loudest = get_setting(settings_, 0x288B029D);
    float quiet = get_setting(settings_, 0xB8F81277);
    volume.percent_tweak(static_cast<float>(((static_cast<double>(loudest) - quiet) * rpm + quiet) * loudness));
    float high_pitch = get_setting(settings_, 0x9F46344A);
    float low_pitch = get_setting(settings_, 0x2660AF3B);
    float pitch = static_cast<float>((static_cast<double>(high_pitch) - low_pitch) * rpm + low_pitch);
    if (!engine_sound_)
        engine_sound_ = sfx->emit_sound(engine_sound_name_, &volume, pitch, 0, nullptr, 0);
    else
        sfx->set_sound_params_scaled(engine_sound_, &volume, pitch);
    // (VolumeRef's destructor is empty: 0x005480A0)
}

// 0x0051B3E0
void SkaterVehicleSoundComponent::update_skid()
{
    float skid = 0.0f;
    if (prop<bool>(input_, 0x40) && core_physics_->state_->cur_phase_ == 0) {
        double speed = get_pace(entity_->vel_);
        if (!(0.0 > speed))
            skid = static_cast<float>(speed);
    }
    float rate = get_rate(0x95FBE774);
    double ease = frame_clock::get_slomo_frame_seconds() * rate;
    skid_ = static_cast<float>((static_cast<double>(skid) - skid_) * ease + skid_);
    float loudest_at = get_setting(settings_, 0x28F1AAD0);
    float level = get_fraction(static_cast<double>(skid_) / loudest_at);

    sound::VolumeRef volume;
    volume.prepare_all();
    sound::AudioMixer* sfx = audio_mixer_handle();
    sfx->fetch_loudness(&volume, &entity_->at_point_, sfx->get_dropoff_distance(skid_sound_name_), 0, nullptr, nullptr);
    float loudest = get_setting(settings_, 0x42EA5746);
    float quiet = get_setting(settings_, 0x43E6CDF2);
    volume.percent_tweak(static_cast<float>((static_cast<double>(loudest) - quiet) * level + quiet));
    float high_pitch = get_setting(settings_, 0xE68BE784);
    float low_pitch = get_setting(settings_, 0x76F8F76E);
    float pitch = static_cast<float>((static_cast<double>(high_pitch) - low_pitch) * level + low_pitch);
    if (!volume.is_audio_off()) {
        if (!skid_sound_)
            skid_sound_ = sfx->emit_sound(skid_sound_name_, &volume, pitch, 0, nullptr, 0);
        else
            sfx->set_sound_params_scaled(skid_sound_, &volume, pitch);
    } else if (skid_sound_) {
        sfx->stop_sound(skid_sound_);
        skid_sound_ = 0;
    }
}

// 0x0051B3B0
void SkaterVehicleSoundComponent::update_unused()
{
    get_rate(0x71B4B9FE);
    get_clock();
}

// 0x0051B7C0
bool SkaterVehicleSoundComponent::set_skater_vehicle_sounds(script::Struct* params, script::Script*)
{
    params->read_hash_value(0u, &settings_name_, 0);
    if (engine_sound_)
        audio_mixer_handle()->stop_sound(engine_sound_);
    engine_sound_ = 0;
    engine_sound_name_ = 0;
    skid_sound_name_ = 0;
    gear_count_ = 0;
    extra_ = nullptr;
    read_settings();
    silenced_ = false;
    return true;
}

// 0x0051B820
bool SkaterVehicleSoundComponent::no_skater_vehicle_sounds(script::Struct*, script::Script*)
{
    if (engine_sound_)
        audio_mixer_handle()->stop_sound(engine_sound_);
    engine_sound_ = 0;
    engine_sound_name_ = 0;
    skid_sound_name_ = 0;
    gear_count_ = 0;
    extra_ = nullptr;
    silenced_ = true;
    return true;
}

// 0x0051B860
bool SkaterVehicleSoundComponent::stop_skater_vehicle_sounds(script::Struct*, script::Script*)
{
    if (engine_sound_)
        audio_mixer_handle()->stop_sound(engine_sound_);
    return true;
}

} // namespace objects

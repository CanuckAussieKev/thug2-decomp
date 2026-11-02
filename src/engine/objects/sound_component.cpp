// The sound component (engine/objects/sound_component.h).

#include "engine/objects/sound_component.h"

#include <cstdint>
#include <cstring>
#include <gsl/pointers>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/viewport.h"
#include "engine/maths/maths.h"
#include "engine/maths/vec4.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/proximity.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "platform/stopwatch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/gfx/weather.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/checked_index.h"

namespace objects {

std::array<script::MemberFunctionEntry, 2> g_sound_component_member_functions{{
    {.callsign_ = 0xAFA20E18, .owner_kind_ = 0x07713C7B}, {.callsign_ = 0, .owner_kind_ = 0x07713C7B}}};

namespace {

constexpr uint32_t kObjAdjustSound = 0x3E5E8023;
constexpr uint32_t kObjStopSound = 0x4E4132D6;
constexpr uint32_t kObjSetSound = 0xE8347171;

constexpr float kPercent = 0.009999999776482582f;            // 0x0064544C
constexpr float kFramesPerSecond = 60.0f;                    // 0x00645EA0
constexpr float kInchesPerFoot = 12.0f;                      // 0x0064546C
constexpr float kDefaultDropoff = 1020.0f;                   // 0x0064655C
constexpr float kHearingMargin = 240.0f;                     // 0x00646558
constexpr float kThousand = 1000.0f;                         // 0x006453C0
constexpr float kRetryPerDistance = -0.0016666667070239782f; // 0x00646554
constexpr uint32_t kRetryDelay = 0x14D;                      // (ms)
constexpr uint32_t kWaitTime = 2000;                         // (ms)

sound::AudioMixer* audio_mixer_handle() { return sound::g_audio_mixer.get(); }

// The camera nearest to `position` (0x004A09E0; its distance through `distance`) and a camera's position (0x00485A00).
void* nearest_camera(const maths::Vec4* position, float* distance)
{
    return render::fetch_nearest_camera(position, distance);
}
const maths::Vec4* viewer_location(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_location(); }

// The CRT's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int ftol(double x) { return static_cast<int>(static_cast<int64_t>(x)); }

// The play parameters play_object_sound takes.
using sound::SoundParams;

} // namespace

// 0x00433100
gsl::owner<ObjectModule*> AudioEmitterModule::fabricate() { return new AudioEmitterModule; }

// 0x00432C80
void AudioEmitterModule::publish_method_table()
{
    script::register_member_function_table(g_sound_component_member_functions);
}

// 0x00432D90
AudioEmitterModule::AudioEmitterModule()
{
    kind_tag_ = kType;
    at_point_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    last_pos_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
}

// 0x00432DF0 (0x00433160 the deleting one)
AudioEmitterModule::~AudioEmitterModule() { audio_mixer_handle()->stop_object_sounds(this); }

// 0x00432CA0
void AudioEmitterModule::configure_from_data(script::Struct* params)
{
    uint32_t node = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    if (params->read_hash_value(0x08470F2E, &node, 0))  // ProximNode
        proxim_ = proxim_get_node(node);
}

// 0x004335D0
void AudioEmitterModule::tick_frame()
{
    last_pos_ = at_point_;
    at_point_ = entity_->at_point_;
    update_waiting_sounds();
}

// 0x00433190
int AudioEmitterModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    if (name == kObjAdjustSound) {
        adjust_sound(params, script);
        return 1;
    }
    if (name == kObjStopSound) {
        uint32_t sound = 0; // (read into the name's argument slot, cleared first)
        params->read_hash_value(0u, &sound, 0);
        audio_mixer_handle()->halt_object_sound(this, sound);
        if (core::at(waiting_, 0).props.checksum == sound)
            core::at(waiting_, 0).props.checksum = 0;
        if (core::at(waiting_, 1).props.checksum == sound)
            core::at(waiting_, 1).props.checksum = 0;
        return 1;
    }
    if (name == kObjSetSound) {
        assign_sound_cue(params);
        return 1;
    }
    return 2;
}

// 0x00432D40
void AudioEmitterModule::keep_looping_sound(const sound::PlaybackProps* props, uint32_t time)
{
    for (WaitingSound& waiting : waiting_) {
        if (waiting.props.checksum)
            continue;
        // (the original copies 0x30 bytes - 4 past the props - then overwrites them with the time)
        waiting.props = *props;
        waiting.time = time + kWaitTime;
        return;
    }
}

// 0x00432E50
maths::Vec4* AudioEmitterModule::get_sound_position(maths::Vec4* out, void* camera)
{
    if (spewer_ && camera) {
        spewer_->nearest_point_on(out, viewer_location(camera));
        return out;
    }
    *out = at_point_;
    return out;
}

// 0x00432EB0
maths::Vec4* AudioEmitterModule::get_previous_sound_position(maths::Vec4* out, void* camera)
{
    if (spewer_ && camera) {
        spewer_->nearest_point_on(out, viewer_location(camera));
        return out;
    }
    *out = last_pos_;
    return out;
}

// 0x00432F10
bool AudioEmitterModule::get_sound_extent(void* camera, maths::Vec4* out)
{
    if (!camera || !proxim_)
        return false;
    maths::Vec4 nearest{};
    *out = *proxim_->nearest_point_on(&nearest, viewer_location(camera));
    return true;
}

// 0x00432F70
void AudioEmitterModule::update_props(sound::PlaybackProps* props)
{
    if (props->pitch3 != props->pitch) { // (not a number: moved too)
        auto step = static_cast<float>(frame_clock::get_slomo_frame_seconds() * kFramesPerSecond *
                                       props->unknown20);
        props->pitch = maths::keep_within(props->pitch3, props->pitch, step);
    }
    if (props->volume3 != props->volume) {
        auto step = static_cast<float>(frame_clock::get_slomo_frame_seconds() * kFramesPerSecond *
                                       props->unknown24);
        props->volume = maths::keep_within(props->volume3, props->volume, step);
    }
}

// 0x00433480
void AudioEmitterModule::update_waiting_sounds()
{
    const uint32_t now = frame_clock::get_time();
    for (WaitingSound& waiting : waiting_) {
        sound::PlaybackProps& props = waiting.props;
        if (!props.checksum)
            continue;
        update_props(&props);
        if (waiting.time > now)
            continue;
        float distance = 0.0f; // (no camera: the original's is whatever its stack held)
        const void* camera = nearest_camera(&at_point_, &distance);
        // (a dropoff of exactly 0 means the default)
        const float dropoff = props.dropoff == 0.0f ? kDefaultDropoff : props.dropoff;
        if (camera && static_cast<double>(kHearingMargin) + dropoff > distance) {
            SoundParams params = {.pitch = props.pitch2, .volume = props.volume2, .dropoff = props.dropoff, .falloff = static_cast<int>(props.falloff)};
            const uint32_t id = audio_mixer_handle()->play_object_sound(props.checksum, &params, this, false, true);
            if (!id)
                continue;
            if (sound::PlaybackProps* playing = audio_mixer_handle()->get_positional_props(this, id, false)) {
                // Faded in from silence to its volume, at a hundredth of it a frame.
                playing->volume3 = props.volume3;
                playing->pitch3 = props.pitch3;
                playing->unknown24 = playing->volume3 * kPercent;
                playing->unknown20 = props.unknown20;
                playing->volume = 0.0f;
            }
            props.checksum = 0;
        } else {
            // Looked at again later: the further beyond hearing, the later.
            const int sooner = ftol((static_cast<double>(distance) - dropoff) * kThousand * kRetryPerDistance);
            waiting.time = now - static_cast<uint32_t>(sooner) + kRetryDelay;
        }
    }
}

// 0x00433240
void AudioEmitterModule::emit_sound(script::Struct* params)
{
    uint32_t sound = 0;
    float volume = 100.0f, pitch = 100.0f, dropoff = 0.0f;
    uint32_t emitter = 0;
    bool keep_placement = false;
    int falloff = 0;
    params->read_hash_value(0u, &sound, 0);
    params->fetch_float(0xF6A36814, &volume, 0); // vol
    params->fetch_float(0xD8604126, &pitch, 0);  // pitch
    if (params->has_entry_named(0x9E497FC6)) {   // Percent: between the min and max
        float min_volume = 0.0f, max_volume = 100.0f, min_pitch = 100.0f, max_pitch = 100.0f, percent = 0.0f;
        params->fetch_float(0x0693DAAF, &max_volume, 0); // maxVol
        params->fetch_float(0x4391992D, &min_volume, 0); // floor_loudness_
        params->fetch_float(0xFA3E14C5, &max_pitch, 0);  // maxPitch
        params->fetch_float(0x1C5EBB24, &min_pitch, 0);  // minPitch
        params->fetch_float(0x9E497FC6, &percent, 1);    // Percent
        const double fraction = static_cast<double>(percent) * kPercent;
        volume = static_cast<float>((static_cast<double>(max_volume) - min_volume) * fraction + min_volume);
        pitch = static_cast<float>((static_cast<double>(max_pitch) - min_pitch) * fraction + min_pitch);
    }
    if (params->fetch_float(0xFF2020EC, &dropoff, 0)) // dropoff (feet)
        dropoff = dropoff * kInchesPerFoot;
    uint32_t function = 0;                                 // (the original's slot holds Percent's value, or garbage)
    if (params->read_hash_value(0x0C6AC50A, &function, 0)) // dropoff_function
        falloff = sound::falloff_from_checksum(function);
    if (params->has_bare_flag(0xBB39837F)) // NoPosUpdate
        keep_placement = true;
    if (params->read_hash_value(0x8A7132CE, &emitter, 0)) // emitter
        spewer_ = emitter_get_node(emitter);
    else
        spewer_ = nullptr;
    if (!sound) {
        // The sound set for a type (Obj_SetSound). (Read into the parameters' own argument slot.)
        int type = -1;
        params->fetch_int_value("Type", &type, 0);
        if (type == -1 || type > 8)
            return;
        sound = core::at(sound_cues_, type); // (type 8 reads the first waiting sound's checksum, as the original)
        if (!sound)
            return;
    }
    // (here the original calls its slot 13, an empty stub taking the sound, volume and pitch: nothing)
    SoundParams play = {.pitch = pitch, .volume = volume, .dropoff = dropoff, .falloff = falloff};
    audio_mixer_handle()->play_object_sound(sound, &play, this, keep_placement, false);
}

// 0x00432FF0
void AudioEmitterModule::adjust_sound(script::Struct* params, script::Script* /*script*/)
{
    float loudness_percent = 100.0f, pitch_percent = 100.0f;
    // (read into the parameters' own argument slot: none, the pointer's value)
    uint32_t sound = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    params->read_hash_value(0u, &sound, 0);
    sound::PlaybackProps* props = audio_mixer_handle()->get_positional_props(this, sound, true);
    if (!props) {
        for (WaitingSound& waiting : waiting_) {
            if (waiting.props.checksum == sound) {
                props = &waiting.props;
                break;
            }
        }
        if (!props)
            return;
    }
    if (params->fetch_float(0x330F3415, &loudness_percent, 0)) { // loudness_percent
        props->volume3 = static_cast<float>(static_cast<double>(loudness_percent) * props->volume2 * kPercent);
        if (!params->fetch_float(0xDAA9A3C2, (&props->unknown24), 0)) // volumeStep
            props->volume = props->volume3;
    }
    if (params->fetch_float(0xEE616C13, &pitch_percent, 0)) { // pitchPercent
        props->pitch3 = static_cast<float>(static_cast<double>(pitch_percent) * props->pitch2 * kPercent);
        if (!params->fetch_float(0x7B090774, (&props->unknown20), 0)) // pitchStep
            props->pitch = props->pitch3;
    }
}

// 0x00432CE0
void AudioEmitterModule::assign_sound_cue(script::Struct* params)
{
    int type = -1;
    uint32_t sound = 0;
    params->fetch_int_value(0x7321A8D6, &type, 0); // type
    params->read_hash_value(0u, &sound, 0);
    if (type != -1 && sound && type <= 8)
        core::at(sound_cues_, type) = sound; // (8 is the first waiting sound's checksum; a negative type writes before the slots)
}

// 0x00433630
bool AudioEmitterModule::obj_play_sound(script::Struct* params, script::Script* /*script*/)
{
    emit_sound(params);
    return true;
}

} // namespace objects

// The sound effects manager's positional sounds and the PC driver's voices (0x0047B8E0-0x0047D41E). See sfx.h.

#include "engine/gfx/viewport.h"
#include "engine/sound/sfx.h"
#include "platform/mute_audio.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include <share.h>

#include <windows.h>

#include <mmsystem.h>
#include <dsound.h>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "formats/wave_format.h"
#include "formats/byte_stream.h"
#include "frontend/data_overlay.h"
#include "engine/gfx/view_cam.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/sound_component.h"
#include "engine/audio/music_player.h"
#include "engine/audio/music_api.h"
#include "engine/script/param_struct.h"
#include "engine/sys/game_config.h"
#include "platform/platform_system.h"
#include "platform/stopwatch.h"
#include "engine/util/unique_handle.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/sound/sound_state.h"
#include "engine/gfx/renderer.h"
#include "engine/core/checksums.h"

namespace sound {

namespace {

constexpr int kMaxPlaying = 48;
constexpr int kMaxPositional = 128;
constexpr uint32_t kDebugA = 0x6D2E270E;
constexpr uint32_t kDebugB = 0xD7BB618D;

std::array<PlayingSound, 48>& get_playing() { return sound_state().playing_sounds(); }
PositionalSound*& positional_head() { return sound_state().positional_head(); }
int& positional_count() { return sound_state().positional_count(); }
std::array<SoundWaveSlot, 256>& permanent_waves() { return sound_state().permanent_waves(); }
int& permanent_count() { return sound_state().permanent_count(); }
float& default_dropoff() { return sound_state().default_dropoff(); }
bool quiet_misses() { return game_config::from_cd(); }
memory::HeapUnique<AudioMixer>& sole() { return sound_state().audio_mixer(); }

// NOT ORIGINAL grouping - the DirectSound voices and their counts. Made on first use, never destroyed (voice_state()).
struct VoiceState {
    float voice_volume_{100.0f};
    uint32_t sfx_references_{};
    void* listener_{};
    std::array<Voice, 48> voices_{};
    int playing_voices_{};
    bool has_3d_{};
    int voice_count_{};
    int last_voice_{};
};

VoiceState& voice_state()
{
    static core::NoDestroy<VoiceState> state;
    return state.get();
}
// DirectSound's GUIDs, as the exe's data has them (dsound.lib's): IID_IDirectSound3DBuffer, IID_IDirectSound3DListener,
// and DS3DALG_DEFAULT (GUID_NULL: the default 3D algorithm).
const GUID kIidSound3dBuffer{.Data1 = 0x279AFA86, .Data2 = 0x4981, .Data3 = 0x11CE, .Data4 = {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};
const GUID kIidSound3dListener{.Data1 = 0x279AFA84, .Data2 = 0x4981, .Data3 = 0x11CE, .Data4 = {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};
const GUID kDefault3dAlgorithm{};

uint32_t& ref_tally() { return voice_state().sfx_references_; }

// The driver's state.
std::array<Voice, 48>& get_voices() { return voice_state().voices_; }
int& playing_voices() { return voice_state().playing_voices_; }
bool& has_3d() { return voice_state().has_3d_; }
int& voice_count() { return voice_state().voice_count_; }
int& last_voice() { return voice_state().last_voice_; }
void*& get_listener() { return voice_state().listener_; }
void* input_port() { return audio::get_sound_device(); }
float voice_volume() { return voice_state().voice_volume_; }

// The DirectSound objects the voices hold as void* (the original's layout).
IDirectSound* sound_device() { return static_cast<IDirectSound*>(input_port()); }
IDirectSoundBuffer* buffer_of(void* buffer) { return static_cast<IDirectSoundBuffer*>(buffer); }
IDirectSound3DBuffer* buffer_3d_of(void* buffer) { return static_cast<IDirectSound3DBuffer*>(buffer); }
IDirectSound3DListener* listener() { return static_cast<IDirectSound3DListener*>(get_listener()); }

// Cameras (not rewritten yet).
void* nearest_camera(const maths::Vec4* position, float* distance)
{
    return render::fetch_nearest_camera(position, distance);
}
void* viewport_camera(int viewport) { return render::obtain_active_lens(viewport); }
const maths::Vec4* viewer_location(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_location(); }
const maths::Mat4* cam_orientation(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_mat(); }
// The objects' sound parts: the "object" the sound manager is given is the object's sound component.
double get_extent(void* extent, const maths::Vec4* from)
{
    return maths::get_distance(*static_cast<const maths::Vec4*>(extent), *from);
}
bool object_sound_extent(objects::AudioEmitterModule* object, void* camera, void* out)
{
    return object->get_sound_extent(camera, static_cast<maths::Vec4*>(out));
}
maths::Vec4* object_sound_position(objects::AudioEmitterModule* object, maths::Vec4* out, void* camera)
{
    return object->get_sound_position(out, camera);
}
maths::Vec4* object_sound_previous(objects::AudioEmitterModule* object, maths::Vec4* out, void* camera)
{
    return object->get_previous_sound_position(out, camera);
}
void keep_looping_sound(objects::AudioEmitterModule* object, PlaybackProps* props, uint32_t time)
{
    object->keep_looping_sound(props, time);
}
void note_sound_update(objects::AudioEmitterModule* object, PlaybackProps* props) { object->update_props(props); }
const maths::Vec4* object_position(objects::AudioEmitterModule* object) { return &object->at_point(); }

// The CRT's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int ftol(double x) { return static_cast<int>(static_cast<int64_t>(x)); }

void get_report_missing()
{
    if (!quiet_misses())
        script::fetch_int_value(kDebugA);
}

// A positional sound's sound stopped (when it's playing) - as the original, the slot is found by unique id first.
void stop_positional_voice(AudioMixer* manager, const PositionalSound* entry)
{
    if (no_sound() || entry->unique_id_ == 0)
        return;
    for (int i = 0; i < kMaxPlaying; ++i) {
        if (core::at(get_playing(), i).unique_id_ != entry->unique_id_)
            continue;
        if (is_voice_playing(i) && !no_sound()) {
            const int index = manager->get_playing_index(entry->unique_id_);
            if (index >= 0)
                stop_voice(index);
        }
        return;
    }
}
void uncouple(PositionalSound* entry)
{
    if (entry->next)
        entry->next->prev = entry->prev;
    if (entry->prev)
        entry->prev->next = entry->next;
    else
        positional_head() = entry->next;
    --positional_count();
}

} // namespace

// 0x0047B8E0
void AudioMixer::doppler_shift(float* pitch, const maths::Vec4* position, const maths::Vec4* previous,
                               float frame_seconds, void* camera)
{
    if (no_sound())
        return;
    if (!camera) {
        camera = nearest_camera(position, nullptr);
        if (!camera)
            return;
    }
    const maths::Vec4* camera_at = &static_cast<gfx::ViewCam*>(camera)->old_pos(); // (its +0x4C, as the original)
    const float px = camera_at->x - previous->x;
    const float py = camera_at->y - previous->y;
    const float pz = camera_at->z - previous->z;
    const maths::Vec4* listener = viewer_location(camera);
    const double nx = static_cast<double>(listener->x) - position->x;
    const double ny = static_cast<double>(listener->y) - position->y;
    const double nz = static_cast<double>(listener->z) - position->z;
    const double now = std::sqrt(nz * nz + ny * ny + nx * nx);
    const double before =
        std::sqrt(static_cast<double>(pz) * pz + static_cast<double>(py) * py + static_cast<double>(px) * px);
    auto change = static_cast<float>(now - before);
    auto speed =
        static_cast<float>(std::fabs(static_cast<double>(change)) * (frame_clock::get_slomo_frame_seconds() * 60.0f));
    const double limit = frame_clock::get_slomo_frame_seconds() * 60.0f * 360.0f;
    if (limit < speed) // (equal or unordered: shifted)
        return;
    const double p = *pitch;
    *pitch = static_cast<float>(p - change * p / (static_cast<double>(frame_seconds) * 12320.0f));
}

// 0x0047BA00
PlaybackProps* AudioMixer::get_positional_props(objects::AudioEmitterModule* object, uint32_t id, bool by_checksum)
{
    for (const PositionalSound* entry = positional_head(); entry; entry = entry->next) {
        const uint32_t key = by_checksum ? entry->checksum : entry->unique_id_;
        if (entry->object == object && key == id) {
            int index = 0;
            if (!is_sound_playing(entry->unique_id_, &index))
                return nullptr;
            return &core::at(get_playing(), index).props;
        }
    }
    return nullptr;
}

// 0x0047BAB0
bool AudioMixer::is_unique_id_free(uint32_t id)
{
    if (!no_sound() && id != 0) {
        for (int i = 0; i < kMaxPlaying; ++i) {
            if (core::at(get_playing(), i).unique_id_ == id) {
                if (is_voice_playing(i))
                    return false;
                break;
            }
        }
    }
    for (const PositionalSound* entry = positional_head(); entry; entry = entry->next) {
        if (entry->unique_id_ == id)
            return false;
    }
    return true;
}

// 0x0047BB20
uint32_t AudioMixer::fetch_distinct_id(uint32_t id)
{
    while (!is_unique_id_free(id))
        ++id;
    return id;
}

// 0x0047BB50
void AudioMixer::stop_object_sounds(objects::AudioEmitterModule* object)
{
    for (PositionalSound* entry = positional_head(); entry;) {
        PositionalSound* next = entry->next;
        if (entry->object == object) {
            entry->in_use_ = 0;
            stop_positional_voice(this, entry);
            uncouple(entry);
        }
        entry = next;
    }
}

// 0x0047BC30
void AudioMixer::halt_object_sound(objects::AudioEmitterModule* object, uint32_t checksum)
{
    for (PositionalSound* entry = positional_head(); entry;) {
        PositionalSound* next = entry->next;
        if (entry->object == object && (checksum == 0 || entry->checksum == checksum)) {
            entry->in_use_ = 0;
            stop_positional_voice(this, entry);
            uncouple(entry);
        }
        entry = next;
    }
}

// 0x0047BD20
uint32_t AudioMixer::emit_sound(uint32_t checksum, VolumeRef* volume, PitchId pitch_id, const SoundParams* params,
                                int /*unused*/)
{
    auto pitch = pitch_id.pitch;
    const auto unique_id = pitch_id.unique_id_;
    if (no_sound())
        return 0;
    SoundWaveSlot* entry = fetch_wave_slot(checksum);
    if (!entry)
        return 0;
    const uint32_t id = unique_id ? unique_id : checksum;
    if (!quiet_misses() && script::fetch_int_value(kDebugB) != 0 && !params)
        is_unique_id_free(id); // (a debug check whose report is compiled out)
    adjust_volume_and_pitch(volume, &pitch, entry);
    const int voice = play_voice(entry, volume, pitch);
    if (voice == -1)
        return 0;
    PlayingSound& sound = core::at(get_playing(), voice);
    sound.checksum = id;
    sound.unique_id_ = fetch_distinct_id(id);
    sound.entry = entry;
    init_playback_props(&sound.props, entry, params);
    if (!quiet_misses())
        script::fetch_int_value(kDebugB);
    return sound.unique_id_;
}

// 0x0047BE10
bool AudioMixer::fetch_loudness(VolumeRef* volume, const maths::Vec4* position, float dropoff, int falloff,
                                void* camera, void* extent)
{
    if (no_sound())
        return true;
    volume->change_muted();
    if (is_frozen())
        return true;
    if (!camera) {
        camera = nearest_camera(position, nullptr);
        if (!camera)
            return true;
    }
    const maths::Vec4* listener = viewer_location(camera);
    const double dx = static_cast<double>(listener->x) - position->x;
    const double dy = static_cast<double>(listener->y) - position->y;
    const double dz = static_cast<double>(listener->z) - position->z;
    auto distance = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
    if (extent)
        dropoff = static_cast<float>(get_extent(extent, viewer_location(camera)) + distance);
    else if (dropoff == 0.0f)
        dropoff = default_dropoff();
    if (!(distance < dropoff) && !std::isnan(distance) && !std::isnan(dropoff)) // (unordered: on, as nearer)
        return static_cast<double>(distance) * 1.05f <= dropoff;
    auto t = static_cast<float>(static_cast<double>(distance) / dropoff);
    float level = 0.0f;
    switch (falloff) {
    case 0:
        level = static_cast<float>(((1.0f - static_cast<double>(t) * t) + (1.0f - static_cast<double>(t)) * 3.0f) *
                                   0.25f * 100.0f);
        break;
    case 1:
        level = static_cast<float>((1.0f - static_cast<double>(t)) * 100.0f);
        break;
    case 2: {
        const double left = 1.0f - static_cast<double>(t);
        level = static_cast<float>(left * left * 100.0f);
        break;
    }
    case 3:
        level = static_cast<float>(std::sqrt(1.0f - static_cast<double>(t)) * 100.0f);
        break;
    default:
        break;
    }
    volume->set_kind_tag(VolumeRef::kType3d);
    const float magnitude = std::fabs(level);
    if (std::fabs(volume->find_peak_channel()) < magnitude) {
        // which way the sound is: in front of the camera, to its side
        maths::Vec4 to_sound = *position - *viewer_location(camera);
        to_sound.make_unit();
        const maths::Vec4 front = -core::at(cam_orientation(camera)->row, 0);
        const double facing = static_cast<double>(front.z) * to_sound.z + static_cast<double>(front.y) * to_sound.y +
                              static_cast<double>(front.x) * to_sound.x;
        const double side = (facing + 1.0f) * 0.5f;
        auto right = static_cast<float>((1.0f - side * side) * level);
        const double left = (1.0f - (1.0f - side) * (1.0f - side)) * level;
        if (volume->kind_tag() == VolumeRef::kTypePositional) {
            // (never: the type was just set to 3D)
            bool louder = false;
            if (left > core::at(volume->lanes(), 0)) {
                core::at(volume->lanes(), 0) = static_cast<float>(left);
                louder = true;
            }
            if (right > core::at(volume->lanes(), 1)) {
                core::at(volume->lanes(), 1) = right;
                louder = true;
            }
            if (louder) {
                const maths::Vec4 behind = -core::at(cam_orientation(camera)->row, 2);
                const double ahead = static_cast<double>(behind.z) * to_sound.z +
                                     static_cast<double>(behind.y) * to_sound.y +
                                     static_cast<double>(behind.x) * to_sound.x;
                if (ahead < 0.0)
                    core::at(volume->lanes(), 0) = core::at(volume->lanes(), 0) * -1.0f;
            }
        } else {
            if (left > core::at(volume->lanes(), 0))
                core::at(volume->lanes(), 0) = static_cast<float>(left);
            if (right > core::at(volume->lanes(), 1))
                core::at(volume->lanes(), 1) = right;
        }
    }
    volume->set_dropoff(dropoff);
    volume->anchor_point() = *position;
    return true;
}

// 0x0047C140
uint32_t AudioMixer::play_object_sound(uint32_t checksum, const SoundParams* params, objects::AudioEmitterModule* object,
                                       bool not_positional, bool force)
{
    if (no_sound())
        return 0;
    SoundWaveSlot* entry = fetch_wave_slot(checksum);
    if (!entry)
        return 0;
    VolumeRef volume;
    const maths::Vec4 at = *object_position(object);
    void* camera = nearest_camera(&at, nullptr);
    if (camera) {
        std::array<uint8_t, 0x10> extent_data = {};
        void* extent = object_sound_extent(object, camera, extent_data.data()) ? extent_data.data() : nullptr;
        const float dropoff = params->dropoff != 0.0f ? params->dropoff : entry->dropoff;
        const int falloff = params->falloff;
        maths::Vec4 heard{};
        fetch_loudness(&volume, object_sound_position(object, &heard, camera), dropoff, falloff, camera, extent);
    } else {
        volume.change_muted();
    }
    if (volume.is_audio_off() && !force) {
        if (not_positional || !entry->looping)
            return 0;
        const uint32_t id = fetch_distinct_id(checksum);
        PlaybackProps props{};
        init_playback_props(&props, entry, params);
        keep_looping_sound(object, &props, frame_clock::get_time());
        return id;
    }
    volume.percent_tweak(params->volume);
    const uint32_t id = emit_sound(checksum, &volume, {.pitch = params->pitch, .unique_id_ = 0}, params, 0);
    get_report_missing();
    if (!not_positional) {
        add_positional_sound(id, checksums::Name{checksum}, object);
        get_report_missing();
    }
    return id;
}

// 0x0047C360
bool AudioMixer::update_positional_sound(objects::AudioEmitterModule* object, PlayingSound* sound, uint32_t time)
{
    PlaybackProps* props = &sound->props;
    const float dropoff = props->dropoff == 0.0f ? default_dropoff() : props->dropoff;
    const maths::Vec4 at = *object_position(object);
    float distance = 0.0f;
    void* camera = nearest_camera(&at, &distance);
    if (!camera)
        return true;
    std::array<uint8_t, 0x10> extent_data{};
    void* extent = nullptr;
    if (object_sound_extent(object, camera, extent_data.data())) {
        extent = extent_data.data();
    } else if (sound->unknown38 <= time) {
        if (static_cast<double>(dropoff) + 480.0f <= distance) {
            // well out of range: stopped (a looping one kept by the object to start again)
            if (sound->entry->looping)
                keep_looping_sound(object, props, time);
            return false;
        }
        sound->unknown38 = time + 2000;
    }
    float pitch = 100.0f;
    VolumeRef volume;
    const AudioMixer* manager = sole().get();
    volume.set_kind_tag(manager ? manager->fallback_channel_mode_ : VolumeRef::kTypeStereo);
    volume.change_muted();
    if (extent || distance < dropoff) {
        maths::Vec4 heard{};
        const maths::Vec4* position = object_sound_position(object, &heard, camera);
        const maths::Vec4 here = *position;
        if (sound->entry->flags & 1) {
            auto frame_seconds = static_cast<float>(frame_clock::get_slomo_frame_seconds());
            maths::Vec4 previous{};
            doppler_shift(&pitch, &here, object_sound_previous(object, &previous, camera), frame_seconds, camera);
        }
        fetch_loudness(&volume, &here, dropoff, static_cast<int>(props->falloff), camera, extent);
    }
    get_report_missing();
    return set_sound_params_scaled(sound->unique_id_, &volume, pitch);
}

// 0x0047C560
void AudioMixer::tick_positional_sounds()
{
    const uint32_t time = frame_clock::get_time();
    for (PositionalSound* entry = positional_head(); entry;) {
        PositionalSound* next = entry->next;
        int index = 0;
        if (!is_sound_playing(entry->unique_id_, &index)) {
            entry->in_use_ = 0;
            uncouple(entry);
        } else {
            PlayingSound* sound = &core::at(get_playing(), index);
            note_sound_update(entry->object, &sound->props);
            if (!update_positional_sound(entry->object, sound, time)) {
                entry->in_use_ = 0;
                stop_positional_voice(this, entry);
                uncouple(entry);
            }
        }
        entry = next;
    }
}

// 0x0047BA60
void create_manager(bool create)
{
    if (!sole() && create) {
        void* memory = memory::alloc_zeroed(8, nullptr, 1);
        if (memory) {
            sole() = memory::HeapUnique<AudioMixer>(memory::construct_in<AudioMixer>(memory));
            ++ref_tally();
            return;
        }
        sole() = nullptr;
    }
    ++ref_tally();
}

// 0x0047C6C0
void release_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    if (AudioMixer* manager = sole().release()) { // (no destructor: swept up, its block given back)
        if (!no_sound())
            manager->sweep_up();
        memory::Hub::get_pull()->dealloc(manager);
    }
    ref_tally() = 0;
}

// 0x0047C720
int fetch_idle_voice(PcWave* wave)
{
    auto last = static_cast<uint32_t>(last_voice());
    auto count = static_cast<uint32_t>(voice_count());
    int chosen = static_cast<int>((last + 1) % count);
    for (int i = 0; i < ints::as_signed(count); ++i) {
        const int voice = static_cast<int>((last + i) % count);
        if (core::at(get_voices(), voice).playing)
            continue;
        if (core::at(get_voices(), voice).wave == wave) {
            last_voice() = voice;
            return voice;
        }
        chosen = voice;
    }
    if (chosen >= 0)
        last_voice() = chosen;
    return chosen;
}

// 0x0047C790
int group_count() { return -1; }

// 0x0047C7A0
void stop_voice(int voice)
{
    if (void* buffer = core::at(get_voices(), voice).buffer)
        buffer_of(buffer)->Stop();
}

// 0x0047C7C0
void set_voice_volume(float volume) { voice_state().voice_volume_ = volume; }

namespace {
// NOT ORIGINAL grouping - the pitch and file name of the wave loading. Made on first use, never destroyed
// (wave_load_state()).
struct WaveLoadState {
    float voice_pitch_{1.0f};
    std::array<char, 256> wave_file_name_{{"data\\sounds\\"}};
};

WaveLoadState& wave_load_state()
{
    static core::NoDestroy<WaveLoadState> state;
    return state.get();
}
} // namespace

// 0x0047C7D0
void set_voice_pitch(float pitch) { wave_load_state().voice_pitch_ = pitch; }

// 0x0047C7E0
int is_voice_playing(int voice) { return core::at(get_voices(), voice).playing && core::at(get_voices(), voice).buffer ? 1 : 0; }

// 0x0047C810
void update_voice_states()
{
    for (int i = 0; i < voice_count(); ++i) {
        Voice& voice = core::at(get_voices(), i);
        if (!voice.playing || !voice.buffer)
            continue;
        DWORD status = 0;
        if (buffer_of(voice.buffer)->GetStatus(&status) == DS_OK && !(status & DSBSTATUS_PLAYING)) {
            --playing_voices();
            voice.playing = false;
        }
    }
}

// 0x0047CA50
void init_voices(AudioMixer* manager)
{
    if (manager)
        manager->set_fallback_channel_mode(VolumeRef::kTypeStereo);
    for (int i = 0; i < kMaxPlaying; ++i) {
        core::at(get_voices(), i).buffer = nullptr;
        core::at(get_voices(), i).buffer_3d_ = nullptr;
        core::at(get_voices(), i).wave = nullptr;
        core::at(get_voices(), i).playing = false;
    }
    static_assert(sizeof(DSCAPS) == 0x60);
    DSCAPS caps{};
    caps.dwSize = sizeof(caps);
    const HRESULT result = sound_device()->GetCaps(&caps);
    has_3d() = false;
    if (result != DS_OK) {
        voice_count() = 0x30;
        return;
    }
    has_3d() = caps.dwFreeHw3DAllBuffers > 0;
    const uint32_t mixing = caps.dwFreeHwMixingAllBuffers;
    if (mixing <= 0x10) {
        voice_count() = 0x30;
    } else {
        voice_count() = static_cast<int>(mixing - 4);
        if (voice_count() > 0x30)
            voice_count() = 0x30;
    }
    if (!has_3d())
        return;
    // the primary buffer's listener
    static_assert(sizeof(DSBUFFERDESC) == 0x24);
    DSBUFFERDESC desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    IDirectSoundBuffer* primary = nullptr;
    sound_device()->CreateSoundBuffer(&desc, &primary, nullptr);
    primary->QueryInterface(kIidSound3dListener, &get_listener());
    listener()->SetPosition(0.0f, 0.0f, 0.0f, DS3D_IMMEDIATE);
    primary->Release();
}

// 0x0047CBD0
void stop_all_voices()
{
    audio::stop_music();
    audio::halt_streams(-1);
    for (int i = 0; i < voice_count(); ++i) {
        if (void* buffer = core::at(get_voices(), i).buffer)
            buffer_of(buffer)->Stop();
    }
}

// 0x0047CC10
void free_permanent_waves()
{
    stop_all_voices();
    for (int i = 0; i < permanent_count(); ++i) {
        if (PcWave* wave = core::at(permanent_waves(), i).wave) {
            if (wave->data)
                memory::Hub::get_pull()->dealloc(wave->data);
            memory::Hub::get_pull()->dealloc(wave);
        }
        core::at(permanent_waves(), i).wave = nullptr;
    }
    permanent_count() = 0;
}

// 0x0047C870
bool read_in(PcWave& pc_wave, const char* file_name, uint32_t flags)
{
    // Not original: the data overlay's copy first (thug2-decomp-data\sounds\..., frontend/data_overlay.h - THAW's
    // BMX sounds, tools/pipeline/thaw_bmx/convert.ts).
    std::array<char, 260> overlay{};
    if (frontend::data_overlay::get_overlay_path(file_name, overlay))
        file_name = overlay.data();
    util::UniqueFile file(_fsopen(file_name, "rb", _SH_DENYNO)); // (fopen)
    if (!file)
        return false;
    // Not original - the whole file read and its chunks parsed through wave_format.h (the original read the format
    // chunk whole into this object - a chunk past 0x44 bytes wrote past it -, decoded from no data when there was no
    // data chunk, and decoded the format's bytes-a-second's worth of samples whatever the data held): a file cut
    // short, without its format or data, or with less data than it decodes is not loaded.
    std::vector<uint8_t> contents;
    std::fseek(file.get(), 0, SEEK_END);
    const long length = std::ftell(file.get());
    std::fseek(file.get(), 0, SEEK_SET);
    if (length > 0) {
        contents.resize(static_cast<size_t>(length));
        contents.resize(std::fread(contents.data(), 1, contents.size(), file.get()));
    }
    file.reset();
    util::ByteReader reader(contents.data(), contents.size());
    wave_format::Wave wave;
    const bool read = wave_format::read_wave(reader, &wave);
    if (!read || wave_format::sample_bytes_needed(wave.bytes_per_second()) > wave.sample_bytes()) {
        debug::printf("sound %s damaged: cut short, no format or data, or less data than it decodes\n", file_name);
        return false;
    }
    std::memcpy(&pc_wave, wave.format().data(), wave_format::kFormatSize); // (the WAVEFORMATEX at its start)
    pc_wave.unknown12 = 0x40;
    pc_wave.looping = wave.looping();
    uint32_t size = pc_wave.bytes_per_second_;
    pc_wave.bytes_per_second_ = pc_wave.bits_per_sample_ * pc_wave.samples_per_second_ >> 3;
    void* converted = memory::alloc(size);
    os_layer::decode_adpcm(std::span<int16_t>(static_cast<int16_t*>(converted), size >> 1),
                           std::span<const uint8_t>(wave.samples(), wave.sample_bytes()));
    pc_wave.data = static_cast<uint8_t*>(converted);
    pc_wave.data_size_ = size;
    pc_wave.desc_size_ = 0;
    pc_wave.desc_flags_ = 0;
    pc_wave.desc_bytes_ = 0;
    pc_wave.desc_reserved_ = 0;
    pc_wave.desc_format_ = nullptr;
    std::memset(pc_wave.desc_3d_algorithm_.data(), 0, sizeof(pc_wave.desc_3d_algorithm_));
    pc_wave.desc_size_ = 0x24;
    pc_wave.desc_flags_ = flags;
    pc_wave.desc_format_ = &pc_wave;
    if (pc_wave.block_align_)
        size -= size % pc_wave.block_align_;
    pc_wave.desc_bytes_ = size;
    return true;
}

// 0x0047CC70
bool load_wave(const char* path, uint32_t /*checksum*/, SoundWaveSlot* entry, int load_type, int /*unused*/)
{
    // data\sounds\<path>.snd, in a static buffer (0x0067DB10)
    auto& file_name = wave_load_state().wave_file_name_;
    const size_t length = std::strlen(path);
    // NOLINTNEXTLINE(bugprone-not-null-terminated-result): the terminator follows with the next copy
    std::memcpy(std::span(file_name).subspan(12).data(), path, length);
    std::memcpy(std::span(file_name).subspan(12 + length).data(), ".snd", 5);
    auto* wave = static_cast<PcWave*>(memory::alloc(sizeof(PcWave)));
    if (wave) {
        wave->data_size_ = 0;
        wave->data = nullptr;
    }
    const bool loaded = read_in(*wave, file_name.data(), 0x400E0);
    if (loaded) {
        entry->wave = wave;
        entry->looping = wave->looping;
        entry->not_streamed_ = load_type == 0;
        return true;
    }
    OutputDebugStringA(file_name.data());
    OutputDebugStringA("\n");
    entry->wave = nullptr;
    entry->looping = false;
    entry->not_streamed_ = false;
    if (wave) {
        if (wave->data)
            memory::Hub::get_pull()->dealloc(wave->data);
        memory::Hub::get_pull()->dealloc(wave);
    }
    return false;
}

// 0x0047CD70
void apply_voice_settings(int index, VolumeRef* volume, float pitch)
{
    Voice& voice = core::at(get_voices(), index);
    if (!voice.buffer)
        return;
    if (has_3d() && volume->kind_tag() == VolumeRef::kType3d && voice.buffer_3d_) {
        // 3D: the sound placed relative to the camera hearing it, the listener facing the viewport's way
        void* camera = nearest_camera(&volume->anchor_point(), nullptr);
        void* viewport = viewport_camera(0);
        maths::Vec4 relative = volume->anchor_point() - *viewer_location(camera);
        if (camera != viewport && viewport) {
            const maths::Vec4 front = -core::at(cam_orientation(viewport)->row, 2);
            listener()->SetOrientation(front.x, front.y, front.z, 0.0f, -1.0f, 0.0f, DS3D_DEFERRED);
            const maths::Vec4 heard = -core::at(cam_orientation(camera)->row, 2);
            const maths::Vec4 seen = -core::at(cam_orientation(viewport)->row, 2);
            const double angle = maths::read_turn_angle(heard, seen);
            relative.rotate_about_y(static_cast<float>(angle * 0.017453292f));
        } else {
            const maths::Vec4 front = -core::at(cam_orientation(camera)->row, 2);
            listener()->SetOrientation(front.x, front.y, front.z, 0.0f, -1.0f, 0.0f, DS3D_DEFERRED);
        }
        const float range = volume->dropoff();
        IDirectSound3DBuffer* buffer_3d = buffer_3d_of(voice.buffer_3d_);
        buffer_3d->SetMaxDistance(range, DS3D_DEFERRED);
        buffer_3d->SetMinDistance(90.0f, DS3D_DEFERRED);
        buffer_3d->SetPosition(relative.x, relative.y, relative.z, DS3D_DEFERRED);
        int attenuation = -10000;
        const double level = static_cast<double>(voice_volume()) * 0.0099999998f;
        if (level > 0.0) {
            attenuation = ftol(std::log(level) * 14.42695045f) * 100;
            if (attenuation < -10000)
                attenuation = -10000;
            else if (attenuation > 0)
                attenuation = 0;
        }
        const bool audible = core::at(volume->lanes(), 0) > 0.0f || core::at(volume->lanes(), 1) > 0.0f;
        buffer_of(voice.buffer)
            ->SetVolume(output_attenuation(audible ? attenuation : -10000)); // (NOT ORIGINAL: mute_audio)
        if (get_listener())
            listener()->CommitDeferredSettings();
        return;
    }
    // stereo: the louder channel's level and the balance between the two as decibels
    int pan = 0;
    int attenuation = -10000;
    const float left = core::at(volume->lanes(), 0);
    const float right = core::at(volume->lanes(), 1);
    if (left > 0.0f || right > 0.0f) {
        double louder = 0.0;
        double ratio = 0.0;
        if (!(left < right)) {
            louder = left;
            ratio = static_cast<double>(left) / right;
        } else {
            louder = right;
            ratio = static_cast<double>(right) / left;
        }
        pan = ftol(std::log10(ratio) * 1000.0f);
        const double level = louder * voice_volume() * 0.0099999998f;
        if (level > 0.0) {
            attenuation = ftol(std::log10(level * 0.0099999998f) * 2000.0f);
            if (attenuation < -10000)
                attenuation = -10000;
            else if (attenuation > 0)
                attenuation = 0;
        }
    }
    buffer_of(voice.buffer)->SetVolume(output_attenuation(attenuation)); // (NOT ORIGINAL: mute_audio)
    buffer_of(voice.buffer)->SetPan(pan);
    if (pitch == 0.0f)
        return;
    int frequency = 0;
    if (pitch == 100.0f) {
        frequency = voice.base_frequency_;
    } else {
        frequency = ftol(static_cast<double>(static_cast<uint32_t>(voice.base_frequency_)) * pitch * 0.0099999998f);
        if (frequency > 200000)
            frequency = 200000;
        else if (frequency < 100)
            frequency = 100;
    }
    if (frequency != voice.frequency) {
        voice.frequency = frequency;
        if (static_cast<bool>(render::renderer().frequency_setting_allowed()))
            buffer_of(voice.buffer)->SetFrequency(static_cast<DWORD>(frequency));
    }
}

// 0x0047D130
int play_voice(const SoundWaveSlot* entry, VolumeRef* volume, float pitch)
{
    PcWave* wave = entry->wave;
    if (!wave)
        return -1;
    const int index = fetch_idle_voice(wave);
    if (index < 0)
        return -1;
    Voice& voice = core::at(get_voices(), index);
    if (voice.playing && voice.buffer)
        buffer_of(voice.buffer)->Stop();
    if (wave != voice.wave) {
        if (voice.buffer_3d_)
            buffer_3d_of(voice.buffer_3d_)->Release();
        voice.buffer_3d_ = nullptr;
        if (voice.buffer)
            buffer_of(voice.buffer)->Release();
        voice.buffer = nullptr;
        voice.wave = nullptr;
        const bool three_d = has_3d() && volume->kind_tag() == VolumeRef::kType3d;
        wave->desc_flags_ = three_d ? 0x400B0 : 0x400E0;
        std::memcpy(wave->desc_3d_algorithm_.data(), &kDefault3dAlgorithm, 16);
        // (the wave's buffer description, sfx.h; its format the wave's first fields - the original pointed
        // DirectSound at them in place)
        WAVEFORMATEX format{};
        format.wFormatTag = wave->format_tag_;
        format.nChannels = wave->channels;
        format.nSamplesPerSec = wave->samples_per_second_;
        format.nAvgBytesPerSec = wave->bytes_per_second_;
        format.nBlockAlign = wave->block_align_;
        format.wBitsPerSample = wave->bits_per_sample_;
        format.cbSize = wave->extra_size_;
        DSBUFFERDESC desc{};
        desc.dwSize = wave->desc_size_;
        desc.dwFlags = wave->desc_flags_;
        desc.dwBufferBytes = wave->desc_bytes_;
        desc.dwReserved = wave->desc_reserved_;
        desc.lpwfxFormat = &format;
        std::memcpy(&desc.guid3DAlgorithm, wave->desc_3d_algorithm_.data(), sizeof(desc.guid3DAlgorithm));
        IDirectSoundBuffer* created = nullptr;
        const HRESULT made = sound_device()->CreateSoundBuffer(&desc, &created, nullptr);
        voice.buffer = created;
        if (made != DS_OK)
            return -1;
        if (has_3d() && volume->kind_tag() == VolumeRef::kType3d)
            created->QueryInterface(kIidSound3dBuffer, &voice.buffer_3d_);
        void* first = nullptr;
        DWORD first_bytes = 0;
        void* second = nullptr;
        DWORD second_bytes = 0;
        if (created->Lock(0, wave->desc_bytes_, &first, &first_bytes, &second, &second_bytes, DSBLOCK_ENTIREBUFFER) ==
            DS_OK) {
            std::memcpy(first, wave->data, first_bytes);
            if (second)
                std::memcpy(second, std::span<const uint8_t>(wave->data, wave->desc_bytes_).subspan(first_bytes).data(), second_bytes);
            created->Unlock(first, first_bytes, second, second_bytes);
        }
        voice.wave = wave;
    }
    voice.base_frequency_ = static_cast<int>(wave->samples_per_second_);
    voice.frequency = 0;
    apply_voice_settings(index, volume, pitch);
    buffer_of(voice.buffer)->SetCurrentPosition(0);
    const HRESULT result =
        buffer_of(voice.buffer)->Play(0, 0, entry->looping ? DSBPLAY_LOOPING : 0u);
    ++playing_voices();
    voice.playing = true;
    return result == 0 ? index : -1;
}

// 0x0047D390
void silence_voices()
{
    VolumeRef volume;
    volume.change_muted();
    for (int i = 0; i < voice_count(); ++i) {
        if (core::at(get_voices(), i).playing && core::at(get_voices(), i).buffer)
            apply_voice_settings(i, &volume, 0.0f);
    }
    // (then the volume's destructor: empty)
}

} // namespace sound

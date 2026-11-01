// The sound effects manager's positional sounds and the PC driver's voices (0x0047B8E0-0x0047D41E). See sfx.h.

#include "engine/gfx/viewport.h"
#include "engine/sound/sfx.h"
#include "engine/sound/mute_audio.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <share.h>

#include <windows.h>

#include "engine/core/manager.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/sound/wave_format.h"
#include "engine/util/byte_stream.h"
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
#include "engine/gfx/device.h"
#include "engine/util/unique_handle.h"

namespace sound {

namespace {

constexpr int kMaxPlaying = 48;
constexpr int kMaxPositional = 128;
constexpr uint32_t kDebugA = 0x6D2E270E;
constexpr uint32_t kDebugB = 0xD7BB618D;

PlayingSound* get_playing() { return g_playing_sounds; }
PositionalSound*& positional_head() { return g_positional_head; }
int& positional_count() { return g_positional_count; }
SoundWaveSlot* permanent_waves() { return g_permanent_waves; }
int& permanent_count() { return g_permanent_count; }
float& default_dropoff() { return g_default_dropoff; }
bool quiet_misses() { return game_config::g_from_cd; }
AudioMixer*& sole() { return g_audio_mixer; }

float g_voice_volume{100.0f};
uint32_t g_sfx_references;
void* g_listener;
// DirectSound's GUIDs, as the exe's data has them (dsound.lib's): IID_IDirectSound3DBuffer, IID_IDirectSound3DListener,
// and DS3DALG_DEFAULT (GUID_NULL: the default 3D algorithm).
GUID g_iid_sound_3d_buffer{0x279AFA86, 0x4981, 0x11CE, {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};
GUID g_iid_sound_3d_listener{0x279AFA84, 0x4981, 0x11CE, {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};
GUID g_default_3d_algorithm;
Voice g_voices[48];
int g_playing_voices;
bool g_has_3d;
int g_voice_count;
int g_last_voice;

uint32_t& ref_tally() { return g_sfx_references; }

// The driver's state.
Voice* get_voices() { return g_voices; }
int& playing_voices() { return g_playing_voices; }
bool& has_3d() { return g_has_3d; }
int& voice_count() { return g_voice_count; }
int& last_voice() { return g_last_voice; }
void*& get_listener() { return g_listener; }
void* input_port() { return audio::g_sound_device; }
float voice_volume() { return g_voice_volume; }

// COM calls by vtable slot.
template <typename R, typename... A>
R com(void* object, int offset, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__stdcall*)(void*, A...)>(vtable[offset / 4])(object, args...);
}

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
const maths::Vec4* object_position(objects::AudioEmitterModule* object) { return &object->at_point_; }

// The CRT's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int ftol(double x) { return static_cast<int>(static_cast<int64_t>(x)); }

// The FPU's logarithms (FYL2X with FLDLG2 / FLDLN2), unrounded.
double x87_log10(double x)
{
    double result;
    __asm {
        fldlg2
        fld x
        fyl2x
        fstp result
    }
    return result;
}
double x87_ln(double x)
{
    double result;
    __asm {
        fldln2
        fld x
        fyl2x
        fstp result
    }
    return result;
}

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
        if (get_playing()[i].unique_id_ != entry->unique_id_)
            continue;
        if (is_voice_playing(i) && !no_sound()) {
            int index = manager->get_playing_index(entry->unique_id_);
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
    const maths::Vec4* camera_at = &static_cast<gfx::ViewCam*>(camera)->old_pos_; // (its +0x4C, as the original)
    float px = camera_at->x - previous->x;
    float py = camera_at->y - previous->y;
    float pz = camera_at->z - previous->z;
    const maths::Vec4* listener = viewer_location(camera);
    double nx = static_cast<double>(listener->x) - position->x;
    double ny = static_cast<double>(listener->y) - position->y;
    double nz = static_cast<double>(listener->z) - position->z;
    double now = std::sqrt(nz * nz + ny * ny + nx * nx);
    double before =
        std::sqrt(static_cast<double>(pz) * pz + static_cast<double>(py) * py + static_cast<double>(px) * px);
    float change = static_cast<float>(now - before);
    float speed =
        static_cast<float>(std::fabs(static_cast<double>(change)) * (frame_clock::get_slomo_frame_seconds() * 60.0f));
    double limit = frame_clock::get_slomo_frame_seconds() * 60.0f * 360.0f;
    if (limit < speed) // (equal or unordered: shifted)
        return;
    double p = *pitch;
    *pitch = static_cast<float>(p - change * p / (static_cast<double>(frame_seconds) * 12320.0f));
}

// 0x0047BA00
PlaybackProps* AudioMixer::get_positional_props(objects::AudioEmitterModule* object, uint32_t id, bool by_checksum)
{
    for (PositionalSound* entry = positional_head(); entry; entry = entry->next) {
        uint32_t key = by_checksum ? entry->checksum : entry->unique_id_;
        if (entry->object == object && key == id) {
            int index;
            if (!is_sound_playing(entry->unique_id_, &index))
                return nullptr;
            return &get_playing()[index].props;
        }
    }
    return nullptr;
}

// 0x0047BAB0
bool AudioMixer::is_unique_id_free(uint32_t id)
{
    if (!no_sound() && id != 0) {
        for (int i = 0; i < kMaxPlaying; ++i) {
            if (get_playing()[i].unique_id_ == id) {
                if (is_voice_playing(i))
                    return false;
                break;
            }
        }
    }
    for (PositionalSound* entry = positional_head(); entry; entry = entry->next) {
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
uint32_t AudioMixer::emit_sound(uint32_t checksum, VolumeRef* volume, float pitch, uint32_t unique_id, void* params,
                                int /*unused*/)
{
    if (no_sound())
        return 0;
    SoundWaveSlot* entry = fetch_wave_slot(checksum);
    if (!entry)
        return 0;
    uint32_t id = unique_id ? unique_id : checksum;
    if (!quiet_misses() && script::fetch_int_value(kDebugB) != 0 && !params)
        is_unique_id_free(id); // (a debug check whose report is compiled out)
    adjust_volume_and_pitch(volume, &pitch, entry);
    int voice = play_voice(&entry->wave, volume, pitch);
    if (voice == -1)
        return 0;
    PlayingSound& sound = get_playing()[voice];
    sound.checksum = id;
    sound.unique_id_ = fetch_distinct_id(id);
    sound.entry = entry;
    init_playback_props(&sound.props, entry, static_cast<PlaybackProps*>(params));
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
    double dx = static_cast<double>(listener->x) - position->x;
    double dy = static_cast<double>(listener->y) - position->y;
    double dz = static_cast<double>(listener->z) - position->z;
    float distance = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
    if (extent)
        dropoff = static_cast<float>(get_extent(extent, viewer_location(camera)) + distance);
    else if (dropoff == 0.0f)
        dropoff = default_dropoff();
    if (!(distance < dropoff) && !std::isnan(distance) && !std::isnan(dropoff)) // (unordered: on, as nearer)
        return static_cast<double>(distance) * 1.05f <= dropoff;
    float t = static_cast<float>(static_cast<double>(distance) / dropoff);
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
        double left = 1.0f - static_cast<double>(t);
        level = static_cast<float>(left * left * 100.0f);
        break;
    }
    case 3:
        level = static_cast<float>(std::sqrt(1.0f - static_cast<double>(t)) * 100.0f);
        break;
    default:
        break;
    }
    volume->kind_tag_ = VolumeRef::kType3d;
    float magnitude = std::fabs(level);
    if (std::fabs(volume->find_peak_channel()) < magnitude) {
        // which way the sound is: in front of the camera, to its side
        maths::Vec4 to_sound = *position - *viewer_location(camera);
        to_sound.make_unit();
        maths::Vec4 front = -cam_orientation(camera)->row[0];
        double facing = static_cast<double>(front.z) * to_sound.z + static_cast<double>(front.y) * to_sound.y +
                        static_cast<double>(front.x) * to_sound.x;
        double side = (facing + 1.0f) * 0.5f;
        float right = static_cast<float>((1.0f - side * side) * level);
        double left = (1.0f - (1.0f - side) * (1.0f - side)) * level;
        if (volume->kind_tag_ == VolumeRef::kTypePositional) {
            // (never: the type was just set to 3D)
            bool louder = false;
            if (left > volume->lanes_[0]) {
                volume->lanes_[0] = static_cast<float>(left);
                louder = true;
            }
            if (right > volume->lanes_[1]) {
                volume->lanes_[1] = right;
                louder = true;
            }
            if (louder) {
                maths::Vec4 behind = -cam_orientation(camera)->row[2];
                double ahead = static_cast<double>(behind.z) * to_sound.z + static_cast<double>(behind.y) * to_sound.y +
                               static_cast<double>(behind.x) * to_sound.x;
                if (ahead < 0.0)
                    volume->lanes_[0] = volume->lanes_[0] * -1.0f;
            }
        } else {
            if (left > volume->lanes_[0])
                volume->lanes_[0] = static_cast<float>(left);
            if (right > volume->lanes_[1])
                volume->lanes_[1] = right;
        }
    }
    volume->dropoff_ = dropoff;
    volume->anchor_point_ = *position;
    return true;
}

// 0x0047C140
uint32_t AudioMixer::play_object_sound(uint32_t checksum, void* params, objects::AudioEmitterModule* object,
                                       bool not_positional, bool force)
{
    if (no_sound())
        return 0;
    SoundWaveSlot* entry = fetch_wave_slot(checksum);
    if (!entry)
        return 0;
    auto* given = static_cast<float*>(params); // {pitch, volume, dropoff, falloff}
    VolumeRef volume;
    maths::Vec4 at = *object_position(object);
    void* camera = nearest_camera(&at, nullptr);
    if (camera) {
        uint8_t extent_data[0x10] = {};
        void* extent = object_sound_extent(object, camera, extent_data) ? extent_data : nullptr;
        float dropoff = given[2] != 0.0f ? given[2] : entry->dropoff;
        int falloff = reinterpret_cast<int*>(params)[3];
        maths::Vec4 heard;
        fetch_loudness(&volume, object_sound_position(object, &heard, camera), dropoff, falloff, camera, extent);
    } else {
        volume.change_muted();
    }
    if (volume.is_audio_off() && !force) {
        if (not_positional || !entry->looping)
            return 0;
        uint32_t id = fetch_distinct_id(checksum);
        PlaybackProps props;
        init_playback_props(&props, entry, static_cast<PlaybackProps*>(params));
        keep_looping_sound(object, &props, frame_clock::get_time());
        return id;
    }
    volume.percent_tweak(given[1]);
    uint32_t id = emit_sound(checksum, &volume, given[0], 0, params, 0);
    get_report_missing();
    if (!not_positional) {
        add_positional_sound(id, checksum, object);
        get_report_missing();
    }
    return id;
}

// 0x0047C360
bool AudioMixer::update_positional_sound(objects::AudioEmitterModule* object, PlayingSound* sound, uint32_t time)
{
    PlaybackProps* props = &sound->props;
    float dropoff = props->dropoff == 0.0f ? default_dropoff() : props->dropoff;
    maths::Vec4 at = *object_position(object);
    float distance;
    void* camera = nearest_camera(&at, &distance);
    if (!camera)
        return true;
    uint8_t extent_data[0x10];
    void* extent = nullptr;
    if (object_sound_extent(object, camera, extent_data)) {
        extent = extent_data;
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
    AudioMixer* manager = sole();
    volume.kind_tag_ = manager ? manager->fallback_channel_mode_ : VolumeRef::kTypeStereo;
    volume.change_muted();
    if (extent || distance < dropoff) {
        maths::Vec4 heard;
        const maths::Vec4* position = object_sound_position(object, &heard, camera);
        maths::Vec4 here = *position;
        if (sound->entry->flags & 1) {
            float frame_seconds = static_cast<float>(frame_clock::get_slomo_frame_seconds());
            maths::Vec4 previous;
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
    uint32_t time = frame_clock::get_time();
    for (PositionalSound* entry = positional_head(); entry;) {
        PositionalSound* next = entry->next;
        int index;
        if (!is_sound_playing(entry->unique_id_, &index)) {
            entry->in_use_ = 0;
            uncouple(entry);
        } else {
            PlayingSound* sound = &get_playing()[index];
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
            sole() = new (memory) AudioMixer;
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
    if (AudioMixer* manager = sole()) {
        if (!no_sound())
            manager->sweep_up();
        memory::Hub::get_pull()->dealloc(manager);
    }
    sole() = nullptr;
    ref_tally() = 0;
}

// 0x0047C720
int fetch_idle_voice(PcWave* wave)
{
    uint32_t last = static_cast<uint32_t>(last_voice());
    uint32_t count = static_cast<uint32_t>(voice_count());
    int chosen = static_cast<int>((last + 1) % count);
    for (int i = 0; i < static_cast<int>(count); ++i) {
        int voice = static_cast<int>((last + i) % count);
        if (get_voices()[voice].playing)
            continue;
        if (get_voices()[voice].wave == wave) {
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
    if (void* buffer = get_voices()[voice].buffer)
        com<long>(buffer, 0x48); // Stop
}

// 0x0047C7C0
void set_voice_volume(float volume) { g_voice_volume = volume; }

float g_voice_pitch{1.0f};
char g_wave_file_name[256]{"data\\sounds\\"};

// 0x0047C7D0
void set_voice_pitch(float pitch) { g_voice_pitch = pitch; }

// 0x0047C7E0
int is_voice_playing(int voice) { return get_voices()[voice].playing && get_voices()[voice].buffer ? 1 : 0; }

// 0x0047C810
void update_voice_states()
{
    for (int i = 0; i < voice_count(); ++i) {
        Voice& voice = get_voices()[i];
        if (!voice.playing || !voice.buffer)
            continue;
        DWORD status;
        if (com<long>(voice.buffer, 0x24, &status) == 0 && !(status & 1)) { // GetStatus: not playing
            --playing_voices();
            voice.playing = false;
        }
    }
}

// 0x0047CA50
void init_voices(AudioMixer* manager)
{
    if (manager)
        manager->fallback_channel_mode_ = VolumeRef::kTypeStereo;
    for (int i = 0; i < kMaxPlaying; ++i) {
        get_voices()[i].buffer = nullptr;
        get_voices()[i].buffer_3d_ = nullptr;
        get_voices()[i].wave = nullptr;
        get_voices()[i].playing = false;
    }
    uint32_t caps[0x18] = {};
    caps[0] = 0x60;
    long result = com<long>(input_port(), 0x10, caps); // GetCaps
    has_3d() = false;
    if (result != 0) {
        voice_count() = 0x30;
        return;
    }
    has_3d() = caps[0x38 / 4] > 0;    // free 3D hardware buffers
    uint32_t mixing = caps[0x20 / 4]; // free hardware mixing buffers
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
    uint32_t desc[9] = {};
    desc[0] = 0x24;
    desc[1] = 0x11; // DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D
    void* primary = nullptr;
    com<long>(input_port(), 0xC, desc, &primary, nullptr);            // CreateSoundBuffer
    com<long>(primary, 0, &g_iid_sound_3d_listener, &get_listener()); // IDirectSound3DListener
    com<long>(get_listener(), 0x38, 0.0f, 0.0f, 0.0f, 0u);            // change_place
    com<long>(primary, 8);                                            // Release
}

// 0x0047CBD0
void stop_all_voices()
{
    audio::stop_music();
    audio::halt_streams(-1);
    for (int i = 0; i < voice_count(); ++i) {
        if (void* buffer = get_voices()[i].buffer)
            com<long>(buffer, 0x48); // Stop
    }
}

// 0x0047CC10
void free_permanent_waves()
{
    stop_all_voices();
    for (int i = 0; i < permanent_count(); ++i) {
        if (PcWave* wave = permanent_waves()[i].wave) {
            if (wave->data)
                memory::Hub::get_pull()->dealloc(wave->data);
            memory::Hub::get_pull()->dealloc(wave);
        }
        permanent_waves()[i].wave = nullptr;
    }
    permanent_count() = 0;
}

// 0x0047C870
bool PcWave::read_in(const char* file_name, uint32_t flags)
{
    // Not original: the data overlay's copy first (thug2-decomp-data\sounds\..., frontend/data_overlay.h - THAW's
    // BMX sounds, tools/pipeline/thaw_bmx/convert.ts).
    char overlay[260];
    if (frontend::data_overlay::get_overlay_path(file_name, overlay))
        file_name = overlay;
    util::UniqueFile file(_fsopen(file_name, "rb", _SH_DENYNO)); // (fopen)
    if (!file)
        return false;
    // Not original - the whole file read and its chunks parsed through wave_format.h (the original read the format
    // chunk whole into this object - a chunk past 0x44 bytes wrote past it -, decoded from no data when there was no
    // data chunk, and decoded the format's bytes-a-second's worth of samples whatever the data held): a file cut
    // short, without its format or data, or with less data than it decodes is not loaded.
    std::vector<uint8_t> contents;
    std::fseek(file.get(), 0, SEEK_END);
    long length = std::ftell(file.get());
    std::fseek(file.get(), 0, SEEK_SET);
    if (length > 0) {
        contents.resize(static_cast<size_t>(length));
        contents.resize(std::fread(contents.data(), 1, contents.size(), file.get()));
    }
    file.reset();
    util::ByteReader reader(contents.data(), contents.size());
    wave_format::Wave wave;
    bool read = wave_format::read_wave(reader, &wave);
    if (!read || wave_format::sample_bytes_needed(wave.bytes_per_second()) > wave.sample_bytes) {
        debug::printf("sound %s damaged: cut short, no format or data, or less data than it decodes\n", file_name);
        return false;
    }
    std::memcpy(this, wave.format, wave_format::kFormatSize); // (the WAVEFORMATEX at its start)
    unknown12 = 0x40;
    looping = wave.looping;
    uint32_t size = bytes_per_second_;
    bytes_per_second_ = bits_per_sample_ * samples_per_second_ >> 3;
    auto* converted = static_cast<uint8_t*>(memory::alloc(size));
    os_layer::decode_adpcm(reinterpret_cast<int16_t*>(converted), wave.samples, static_cast<int>(size) >> 1);
    data = converted;
    data_size_ = size;
    desc_size_ = 0;
    desc_flags_ = 0;
    desc_bytes_ = 0;
    desc_reserved_ = 0;
    desc_format_ = nullptr;
    std::memset(desc_3d_algorithm_, 0, sizeof(desc_3d_algorithm_));
    desc_size_ = 0x24;
    desc_flags_ = flags;
    desc_format_ = this;
    if (block_align_)
        size -= size % block_align_;
    desc_bytes_ = size;
    return true;
}

// 0x0047CC70
bool load_wave(const char* path, uint32_t /*checksum*/, PcWave** slot, int load_type, int /*unused*/)
{
    // data\sounds\<path>.snd, in a static buffer (0x0067DB10)
    char* file_name = g_wave_file_name;
    size_t length = std::strlen(path);
    std::memcpy(file_name + 12, path, length);
    std::memcpy(file_name + 12 + length, ".snd", 5);
    auto* wave = static_cast<PcWave*>(memory::alloc(sizeof(PcWave)));
    if (wave) {
        wave->data_size_ = 0;
        wave->data = nullptr;
    }
    bool loaded = wave->read_in(file_name, 0x400E0);
    if (loaded) {
        slot[0] = wave;
        reinterpret_cast<uint8_t*>(slot)[4] = wave->looping;
        reinterpret_cast<uint8_t*>(slot)[5] = load_type == 0;
        return true;
    }
    OutputDebugStringA(file_name);
    OutputDebugStringA("\n");
    slot[0] = nullptr;
    reinterpret_cast<uint8_t*>(slot)[4] = 0;
    reinterpret_cast<uint8_t*>(slot)[5] = 0;
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
    Voice& voice = get_voices()[index];
    if (!voice.buffer)
        return;
    if (has_3d() && volume->kind_tag_ == VolumeRef::kType3d && voice.buffer_3d_) {
        // 3D: the sound placed relative to the camera hearing it, the listener facing the viewport's way
        void* camera = nearest_camera(&volume->anchor_point_, nullptr);
        void* viewport = viewport_camera(0);
        maths::Vec4 relative = volume->anchor_point_ - *viewer_location(camera);
        if (camera != viewport && viewport) {
            maths::Vec4 front = -cam_orientation(viewport)->row[2];
            com<long>(get_listener(), 0x34, front.x, front.y, front.z, 0.0f, -1.0f, 0.0f, 1u); // change_facing
            maths::Vec4 heard = -cam_orientation(camera)->row[2];
            maths::Vec4 seen = -cam_orientation(viewport)->row[2];
            double angle = maths::read_turn_angle(heard, seen);
            relative.rotate_about_y(static_cast<float>(angle * 0.017453292f));
        } else {
            maths::Vec4 front = -cam_orientation(camera)->row[2];
            com<long>(get_listener(), 0x34, front.x, front.y, front.z, 0.0f, -1.0f, 0.0f, 1u); // change_facing
        }
        float range = volume->dropoff_;
        com<long>(voice.buffer_3d_, 0x40, range, 1u);                              // SetMaxDistance
        com<long>(voice.buffer_3d_, 0x44, 90.0f, 1u);                              // SetMinDistance
        com<long>(voice.buffer_3d_, 0x4C, relative.x, relative.y, relative.z, 1u); // change_place
        int attenuation = -10000;
        double level = static_cast<double>(voice_volume()) * 0.0099999998f;
        if (level > 0.0) {
            attenuation = ftol(x87_ln(level) * 14.42695045f) * 100;
            if (attenuation < -10000)
                attenuation = -10000;
            else if (attenuation > 0)
                attenuation = 0;
        }
        bool audible = volume->lanes_[0] > 0.0f || volume->lanes_[1] > 0.0f;
        com<long>(voice.buffer, 0x3C,
                  output_attenuation(audible ? attenuation : -10000)); // change_loudness (NOT ORIGINAL: mute_audio)
        if (get_listener())
            com<long>(get_listener(), 0x44); // CommitDeferredSettings
        return;
    }
    // stereo: the louder channel's level and the balance between the two as decibels
    int pan = 0;
    int attenuation = -10000;
    float left = volume->lanes_[0];
    float right = volume->lanes_[1];
    if (left > 0.0f || right > 0.0f) {
        double louder;
        double ratio;
        if (!(left < right)) {
            louder = left;
            ratio = static_cast<double>(left) / right;
        } else {
            louder = right;
            ratio = static_cast<double>(right) / left;
        }
        pan = ftol(x87_log10(ratio) * 1000.0f);
        double level = louder * voice_volume() * 0.0099999998f;
        if (level > 0.0) {
            attenuation = ftol(x87_log10(level * 0.0099999998f) * 2000.0f);
            if (attenuation < -10000)
                attenuation = -10000;
            else if (attenuation > 0)
                attenuation = 0;
        }
    }
    com<long>(voice.buffer, 0x3C, output_attenuation(attenuation)); // change_loudness (NOT ORIGINAL: mute_audio)
    com<long>(voice.buffer, 0x40, pan);                             // SetPan
    if (pitch == 0.0f)
        return;
    int frequency;
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
        if (static_cast<bool>(render::g_set_frequency_allowed))
            com<long>(voice.buffer, 0x44, frequency); // SetFrequency
    }
}

// 0x0047D130
int play_voice(PcWave** slot, VolumeRef* volume, float pitch)
{
    PcWave* wave = *slot;
    if (!wave)
        return -1;
    int index = fetch_idle_voice(wave);
    if (index < 0)
        return -1;
    Voice& voice = get_voices()[index];
    if (voice.playing && voice.buffer)
        com<long>(voice.buffer, 0x48); // Stop
    if (wave != voice.wave) {
        if (voice.buffer_3d_)
            com<long>(voice.buffer_3d_, 8); // Release
        voice.buffer_3d_ = nullptr;
        if (voice.buffer)
            com<long>(voice.buffer, 8);
        voice.buffer = nullptr;
        voice.wave = nullptr;
        bool three_d = has_3d() && volume->kind_tag_ == VolumeRef::kType3d;
        wave->desc_flags_ = three_d ? 0x400B0 : 0x400E0;
        std::memcpy(wave->desc_3d_algorithm_, &g_default_3d_algorithm, 16);
        if (com<long>(input_port(), 0xC, &wave->desc_size_, &voice.buffer, nullptr) != 0) // CreateSoundBuffer
            return -1;
        if (has_3d() && volume->kind_tag_ == VolumeRef::kType3d)
            com<long>(voice.buffer, 0, &g_iid_sound_3d_buffer, &voice.buffer_3d_);
        void* first;
        DWORD first_bytes;
        void* second;
        DWORD second_bytes;
        if (com<long>(voice.buffer, 0x2C, 0u, wave->desc_bytes_, &first, &first_bytes, &second, &second_bytes, 2u) ==
            0) { // Lock (the entire buffer)
            std::memcpy(first, wave->data, first_bytes);
            if (second)
                std::memcpy(second, wave->data + first_bytes, second_bytes);
            com<long>(voice.buffer, 0x4C, first, first_bytes, second, second_bytes); // Unlock
        }
        voice.wave = wave;
    }
    voice.base_frequency_ = static_cast<int>(wave->samples_per_second_);
    voice.frequency = 0;
    apply_voice_settings(index, volume, pitch);
    com<long>(voice.buffer, 0x34, 0u); // SetCurrentPosition
    long result = com<long>(voice.buffer, 0x30, 0u, 0u, reinterpret_cast<uint8_t*>(slot)[4] ? 1u : 0u); // Play
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
        if (get_voices()[i].playing && get_voices()[i].buffer)
            apply_voice_settings(i, &volume, 0.0f);
    }
    // (then the volume's destructor: empty)
}

} // namespace sound

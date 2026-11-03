// Streams: starting, preloading and pausing the streams on the three channels, their frame amplitudes.

#include "engine/audio/stream_commands.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/stubs.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/stream_component.h"
#include "engine/audio/music_player.h"
#include "engine/audio/music_api.h"
#include "engine/audio/stream.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/sys/game_config.h"
#include "platform/stopwatch.h"
#include "engine/gfx/viewport.h"
#include "engine/core/checked_index.h"
#include "engine/audio/audio_state.h"
#include "engine/core/no_destroy.h"

namespace audio {

namespace {

bool sound_on() { return music_state().sound_device() && game_config::hardware() != 2; }
bool& stream_header_loaded() { return music_state().streams_on(); }
bool streams_on() { return sound_on() && stream_header_loaded(); }

// NOT ORIGINAL grouping - the last stream started and the streams' hold / block / pause flags. Made on first use, never
// destroyed (stream_command_state()).
struct StreamCommandState {
    int last_stream_{};
    bool streams_held_{};
    bool streams_blocked_{};
    bool streams_paused_before_{};
    std::array<StreamAmpCurve, 1> frame_amps_{}; // (one slot; a checksum of 0 marks it free)
};

StreamCommandState& stream_command_state()
{
    static core::NoDestroy<StreamCommandState> state;
    return state.get();
}

bool& streaming_halted() { return stream_command_state().streams_held_; }
bool& streams_paused_before() { return stream_command_state().streams_paused_before_; }
// Set: no new streams play (nothing in the game sets it).
bool streams_blocked() { return stream_command_state().streams_blocked_; }

std::array<Stream, 3>& get_streams() { return music_state().streams(); }
constexpr int kStreams = 3;
// The stream components that started each stream (null: none); they note the stream's id.
std::array<objects::AudioStreamModule*, 3>& get_owners() { return music_state().stream_owners(); }
uint32_t& owner_id(objects::AudioStreamModule* owner, int stream) { return core::at(owner->streams(), stream); }
// The stream PlayStream started last.
int& last_stream() { return stream_command_state().last_stream_; }

std::array<StreamAmpCurve, 1>& frame_amps() { return stream_command_state().frame_amps_; }
constexpr int kFrameAmps = 1;

// The platform's streams (stream.h): whether a channel is free (stream_status: 1), pause (streams: 0), stop a
// channel, preload (the platform's PlayStream: loaded, not started) or play (load_stream) a stream on one.
int channel_status(int channel) { return stream_status(channel); }
void platform_pause(bool pause, int music) { pause_streams(pause, music); }
void stop_channel(int channel) { halt_stream(channel, 1); } // (the second argument is unused)
bool preload_on_channel(uint32_t checksum, int channel, int stopped) { return play_stream(checksum, channel, stopped); }
bool play_on_channel(uint32_t checksum, int channel, const void* volume, float pitch, int stopped, int unknown,
                     int option)
{
    // (the pitch and `stopped` reach two unused arguments; `unknown` is the preload flag, `option` the plays)
    return load_stream(checksum, channel, static_cast<const StreamGain*>(volume), std::bit_cast<int>(pitch), stopped,
                       unknown != 0, option);
}
// StreamAmpCurve's load (FamData::ReadIn: data\fam\<checksum>.fam into the slot).
bool read_amp_curve(StreamAmpCurve* slot, uint32_t checksum)
{
    return read_in(*slot, checksum);
}
// A script integer that's read and ignored: what used it is compiled out (debug output).
void read_debug_flag() { script::fetch_int_value(0xD7BB618Du); }

// The sound effects manager (0x006F85A8) and its volumes (sound::VolumeRef, 0x2C bytes; the first two channels are
// set below). VolumeRef's destructor is empty.
sound::AudioMixer* sound_manager() { return sound::get_audio_mixer(); }
using Loudness = sound::VolumeRef;
void construct_volume(Loudness* volume) { new (volume) sound::VolumeRef; }
void change_muted(Loudness* volume) { volume->change_muted(); }
float peak_channel_index(Loudness* volume) { return volume->find_peak_channel(); }
inline void destroy_volume(Loudness*) {} // (0x005480A0)
// Whether sound is off (the manager's is_frozen).
bool sound_off_requested(sound::AudioMixer* manager) { return manager->is_frozen(); }
bool loudness_from_position(sound::AudioMixer* manager, Loudness* volume, const maths::Vec4* position, float dropoff,
                            int falloff_curve, void* emitter, maths::Vec4* emitter_direction)
{
    return manager->fetch_loudness(volume, position, dropoff, falloff_curve, emitter, emitter_direction);
}
// The emitter nearest to a position (0x004A09E0, not rewritten yet; its distance through the second argument),
// and the stream component's position for it and its emitter's extent.
void* nearest_emitter(const maths::Vec4* position) { return render::fetch_nearest_camera(position, nullptr); }
maths::Vec4* sound_position(void* component, maths::Vec4* out, void* emitter)
{
    return static_cast<objects::AudioStreamModule*>(component)->get_stream_position(out, emitter);
}
bool get_emitter_direction(void* component, void* emitter, maths::Vec4* out)
{
    return static_cast<objects::AudioStreamModule*>(component)->get_stream_extent(emitter, out);
}

// A new stream's record: volume 100, dropoff 1020, one name (the stream's own).
void set_up_stream(Stream& stream, uint32_t checksum, int option)
{
    stream.dropoff = 1020.0f;
    stream.volume = 100.0f;
    stream.name_position_ = 0;
    stream.name_count_ = 1;
    stream.option = option;
    core::at(stream.names, 0).checksum = checksum;
}

} // namespace

// 0x00451010
void freeze_stream(int pause)
{
    if (!streams_on())
        return;
    if (pause == -1) {
        if (streams_paused_before())
            return;
        pause = 0;
    }
    if (pause == 0) {
        read_debug_flag();
        streaming_halted() = false;
        platform_pause(false, 0);
    } else if (pause == 1) {
        read_debug_flag();
        streams_paused_before() = streaming_halted();
        streaming_halted() = true;
        platform_pause(true, 0);
    }
}

// 0x00451620
bool load_stream_header(const char* name)
{
    if (!streams_on())
        return false;
    stream_header_loaded() = stubs::return_true(); // (0x005B9760 with the name: the PS2's file check)
    (void)name;
    return stream_header_loaded();
}

// 0x00451660
bool is_stream_playing(uint32_t id) { return streams_on() && find_playing_stream(id) >= 0; }

// 0x004516A0
StreamAmpCurve* load_stream_frame_amp(uint32_t checksum)
{
    for (int i = 0; i < kFrameAmps; ++i)
        if (core::at(frame_amps(), i).name == 0)
            return read_amp_curve(&core::at(frame_amps(), i), checksum) ? &core::at(frame_amps(), i) : nullptr;
    return nullptr;
}

// 0x004516E0
StreamAmpCurve* fetch_stream_amp_curve(uint32_t checksum)
{
    for (int i = 0; i < kFrameAmps; ++i)
        if (core::at(frame_amps(), i).name == checksum)
            return &core::at(frame_amps(), i);
    return nullptr;
}

// 0x00451720
bool free_stream_frame_amp(uint32_t checksum)
{
    StreamAmpCurve* slot = fetch_stream_amp_curve(checksum);
    if (!slot || slot->name == 0)
        return false;
    slot->name = 0;
    slot->size = 0;
    return true;
}

// 0x00451770
int start_stream(uint32_t checksum, const void* volume, float pitch, int option, int priority, uint32_t name,
                 bool preload)
{
    if (!streams_on())
        return -1;
    uint32_t oldest = core::at(get_streams(), 0).begin_frame_;
    int lowest = core::at(get_streams(), 0).priority;
    int channel = 0;
    const uint32_t id = free_stream_id(name);
    int free = -1;
    for (int i = 0; i < kStreams; ++i) {
        if (channel_status(i) == 1) {
            free = i;
            break;
        }
        const Stream& stream = core::at(get_streams(), i);
        if (stream.priority < lowest || (stream.priority == lowest && stream.begin_frame_ < oldest)) {
            lowest = stream.priority;
            oldest = stream.begin_frame_;
            channel = i;
        }
    }
    bool started = false;
    if (free >= 0) {
        channel = free;
        started = preload ? preload_on_channel(checksum, channel, 0)
                          : play_on_channel(checksum, channel, volume, pitch, 0, 0, option);
    } else {
        if (priority < lowest)
            return -1;
        stop_channel(channel);
        started = preload ? preload_on_channel(checksum, channel, 1)
                          : play_on_channel(checksum, channel, volume, pitch, 1, 0, option);
    }
    if (!started)
        return -1;
    read_debug_flag();
    Stream& stream = core::at(get_streams(), channel);
    stream.name = name;
    stream.id = id;
    stream.channel = channel;
    stream.priority = priority;
    stream.begin_frame_ = static_cast<uint32_t>(frame_clock::get_draw_frame()); // (the low half)
    stream.listener = fetch_stream_amp_curve(checksum);
    return channel;
}

// 0x00451960
uint32_t play_stream(uint32_t checksum, const void* volume, float pitch, int option, int priority, uint32_t id)
{
    if (!streams_on() || streams_blocked() || streaming_halted())
        return 0;
    if (!id)
        id = checksum;
    stream_id_finished(id); // (its answer unused)
    const int channel = start_stream(checksum, volume, pitch, option, priority, id, false);
    if (channel == -1)
        return 0;
    Stream& stream = core::at(get_streams(), channel);
    set_up_stream(stream, checksum, option);
    core::at(stream.names, 0).frame_amp_ = fetch_stream_amp_curve(checksum);
    last_stream() = channel;
    return stream.id;
}

// 0x00451A50
uint32_t prime_stream(uint32_t checksum, int priority)
{
    if (!streams_on())
        return 0;
    Loudness volume;
    construct_volume(&volume);
    change_muted(&volume);
    uint32_t id = 0;
    const int channel = start_stream(checksum, &volume, 0.0f, 0, priority, checksum, true);
    if (channel != -1) {
        Stream& stream = core::at(get_streams(), channel);
        set_up_stream(stream, checksum, 0);
        core::at(stream.names, 0).frame_amp_ = fetch_stream_amp_curve(checksum);
        id = stream.id;
    }
    destroy_volume(&volume);
    return id;
}

// 0x00451B80
bool add_stream(uint32_t stream, uint32_t name, void* owner)
{
    if (!streams_on())
        return false;
    int found = -1;
    if (owner) {
        for (int i = 0; i < kStreams; ++i) {
            if (core::at(get_owners(), i) == owner && core::at(get_streams(), i).name == stream &&
                channel_status(core::at(get_streams(), i).channel) != 1) {
                found = i;
                break;
            }
        }
        if (found < 0)
            return false;
    } else {
        found = find_playing_stream(stream);
        if (found < 0)
            return false;
    }
    Stream& playing = core::at(get_streams(), found);
    const int n = playing.name_count_++;
    core::at(playing.names, n).checksum = name;
    core::at(playing.names, n).frame_amp_ = fetch_stream_amp_curve(name);
    return true;
}

// 0x00451E20
uint32_t play_stream_at_object(objects::AudioStreamModule* component, uint32_t checksum, float dropoff, float volume, float pitch, int option,
                               int priority, int use_pos_info, int falloff_curve, uint32_t id)
{
    sound::AudioMixer* manager = sound_manager();
    if (sound_off_requested(manager) || !streams_on())
        return 0;
    Loudness stream_volume;
    construct_volume(&stream_volume);
    change_muted(&stream_volume);
    if (use_pos_info) {
        const objects::GameEntity* object = component->entity();
        void* emitter = nearest_emitter(&object->at_point());
        bool audible = true;
        if (emitter) {
            maths::Vec4 direction{};
            maths::Vec4* towards = get_emitter_direction(component, emitter, &direction) ? &direction : nullptr;
            maths::Vec4 position{};
            audible = loudness_from_position(manager, &stream_volume, sound_position(component, &position, emitter),
                                             dropoff, falloff_curve, emitter, towards);
        } else {
            change_muted(&stream_volume);
        }
        if (std::fabs(peak_channel_index(&stream_volume)) < 2.0f) { // (NaN: on)
            read_debug_flag();
            if (!audible) {
                destroy_volume(&stream_volume);
                return 0;
            }
        }
    } else {
        std::memcpy(&core::at(stream_volume.lanes(), 0), &volume, 4);
        std::memcpy(&core::at(stream_volume.lanes(), 1), &volume, 4);
    }
    const uint32_t playing = play_stream(checksum, &stream_volume, pitch, option, priority, id);
    if (playing) {
        const int last = last_stream();
        if (core::at(get_owners(), last)) {
            owner_id(core::at(get_owners(), last), last) = 0;
            core::at(get_owners(), last) = nullptr;
        }
        core::at(get_owners(), last) = component;
        owner_id(component, last) = playing;
        if (volume != 100.0f) {
            for (int i = 0; i < kStreams; ++i) {
                if (core::at(get_streams(), i).id == playing) {
                    core::at(get_streams(), i).dropoff = dropoff;
                    core::at(get_streams(), i).volume =
                        static_cast<float>(static_cast<double>(core::at(get_streams(), i).volume) * volume * 0.01f);
                }
            }
        }
        for (int i = 0; i < kStreams; ++i) {
            if (core::at(get_streams(), i).id != playing)
                continue;
            if (use_pos_info) {
                core::at(get_streams(), i).use_pos_info_ = 1;
                core::at(get_streams(), i).falloff_curve_ = falloff_curve;
            } else {
                core::at(get_streams(), i).use_pos_info_ = 0;
            }
        }
    }
    destroy_volume(&stream_volume);
    return playing;
}

} // namespace audio

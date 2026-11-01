// The PC's streamed audio: loading, starting, stopping and the volumes of the music and the three streams.

#include "engine/audio/stream.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

#include "engine/core/checksums.h"
#include "engine/gfx/movie.h"
#include "engine/sound/sfx.h"
#include "engine/sys/game_config.h"
#include "platform/crt.h"

namespace audio {

namespace {

HANDLE stream_lock() { return g_stream_lock; }
HANDLE get_wad_file() { return g_wad_file; }
WadEntry* wad_entries() { return g_wad_entries; }

uint32_t g_last_music_track;

uint32_t& last_music_track() { return g_last_music_track; }
std::array<uint32_t, 3> g_last_streams;
uint32_t* last_streams() { return g_last_streams.data(); }
// Where a user soundtrack's track is read to, and each stream's buffer (0x14000 bytes each).
std::array<uint8_t, 0x14000> g_user_track_buffer;
void* user_track_buffer() { return g_user_track_buffer.data(); }
std::array<uint8_t, 3 * 0x14000> g_stream_buffers;
void* stream_buffer(int slot) { return g_stream_buffers.data() + slot * 0x14000; }

// 0x005B6250: the root data paths are under ("THUG2\" or "", by 0x007CEFAD; not rewritten yet).
const char* data_root() { return game_config::get_bootstrap_prefix(); }
// The sound effects manager (a reference-counted singleton) and its main volume (the original calls it as a
// method of the manager; it reads a global).
void create_sound_manager() { sound::create_manager(false); }
void release_sound_manager() { sound::release_manager(); }
float sound_volume() { return sound::fetch_master_loudness(); }

void bink_goto(render::Bink* bink, uint32_t frame, int flags)
{
    crt::library_function<void(__stdcall*)(render::Bink*, uint32_t, int)>("binkw32.dll", "_BinkGoto@12")(bink, frame, flags);
}

// The C runtime's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

constexpr float kPercent = 0.01f;
constexpr float kFifth = 0.2f;

void freeze_stream(StreamSlot& slot)
{
    if (slot.wad)
        slot.wad->get_freeze(1);
    else if (slot.bink)
        slot.bink->get_freeze(1);
}

// The flag the "start" calls set and loading clears (nothing on PC reads it).
void set_started(StreamSlot& slot, bool started)
{
    if (slot.wad)
        slot.wad->unknown5f_ = started;
    else if (slot.bink)
        slot.bink->unknown6b_ = started;
}

// Freed now if it may go, else asked to stop (update_streams frees it later).
void halt(StreamSlot& slot)
{
    WaitForSingleObject(stream_lock(), INFINITE);
    if (slot.get_playing()) {
        const bool ready = slot.wad ? slot.wad->ready_to_free() : slot.bink->ready_to_free();
        if (ready)
            slot.dealloc();
        else if (slot.wad)
            slot.wad->stop_requested_ = true;
        else if (slot.bink)
            slot.bink->stop_requested_ = true;
    }
    ReleaseMutex(stream_lock());
}

} // namespace

// The original calls 0x0047C790, a linker-folded `return -1` (also sound::group_count).
Hdl open_user_track(int, int) { return INVALID_HANDLE_VALUE; }

// --- The music ---------------------------------------------------------------------------------------------

// 0x00453C20
bool load_music_track(uint32_t checksum)
{
    StreamSlot& music = music_slot();
    if (music.get_playing())
        return false;
    WaitForSingleObject(stream_lock(), INFINITE);
    music.fabricate(1, false, 1);
    std::array<char, 256> path{};
    std::snprintf(path.data(), sizeof(path), "%s%s%.8x.bik", data_root(), R"(data\streams\music\)", checksum);
    const Result result = music.bink ? music.bink->unseal(path.data()) : -1;
    if (music.wad)
        music.wad->change_loudness(music.volume);
    else if (music.bink)
        music.bink->change_loudness(music.volume);
    if (result != 0) {
        music.dealloc();
        ReleaseMutex(stream_lock());
        return false;
    }
    if (music.paused) {
        music.paused = true;
        freeze_stream(music);
    }
    ReleaseMutex(stream_lock());
    return true;
}

// 0x00453D70
bool load_music_track_by_name(const char* path)
{
    if (const char* slash = std::strrchr(path, '\\'))
        path = slash + 1;
    if (const char* slash = std::strrchr(path, '/'))
        path = slash + 1;
    return load_music_track(checksums::hash_from_text(path));
}

// 0x00453DB0
bool start_track_playback(uint32_t checksum)
{
    last_music_track() = checksum;
    if (!load_music_track(checksum))
        return false;
    set_started(music_slot(), false);
    return true;
}

// 0x00453BF0
bool start_preloaded_music()
{
    if (!music_slot().get_playing())
        return false;
    set_started(music_slot(), true);
    update_streams();
    return true;
}

// 0x00453850
bool play_user_track(int soundtrack, int track)
{
    StreamSlot& music = music_slot();
    if (music.get_playing())
        return false;
    music.fabricate(0, false, 1);
    Hdl file = open_user_track(soundtrack, track);
    if (file == INVALID_HANDLE_VALUE || !music.wad)
        return false;
    if (music.wad->unseal(file, user_track_buffer()) != 0)
        return false;
    if (music.paused) {
        music.paused = true;
        freeze_stream(music);
    }
    return true;
}

// 0x00453AA0
void platform_stop_music() { halt(music_slot()); }

// 0x00453400
int platform_set_music_volume(float volume)
{
    WaitForSingleObject(stream_lock(), INFINITE);
    StreamSlot& music = music_slot();
    if (music.get_playing()) {
        music.volume = volume;
        if (music.wad)
            music.wad->change_loudness(volume);
        else if (music.bink)
            music.bink->change_loudness(volume);
    }
    ReleaseMutex(stream_lock());
    return 1;
}

// 0x00453470
void sync_music_to_time(float seconds)
{
    if (!music_slot().get_playing())
        return;
    WaitForSingleObject(stream_lock(), INFINITE);
    render::Bink* bink = music_slot().bink->bink_; // (a WadStream, or a paused Bink stream, would crash here)
    const int frame =
        ftol(static_cast<double>(bink->frame_rate_) * seconds / static_cast<double>(bink->frame_rate_div_));
    uint32_t off = bink->frame_num_ - static_cast<uint32_t>(frame);
    if (static_cast<int>(off) < 0)
        off = 0u - off;
    if (static_cast<int>(off) > 5)
        bink_goto(bink, static_cast<uint32_t>(frame), 1);
    ReleaseMutex(stream_lock());
}

// --- The sound streams -------------------------------------------------------------------------------------

// 0x00453DF0
bool change_stream_loudness(const StreamGain* volume, int slot_index)
{
    WaitForSingleObject(stream_lock(), INFINITE);
    StreamSlot& slot = stream_slots()[slot_index];
    if (slot.get_playing()) {
        create_sound_manager();
        switch (volume->type) {
        case 2: {
            // Five channels; the slot's volume is their mean (the last added unrounded, as the original's x87
            // still held it).
            std::array<float, 5> level{};
            double last = 0;
            for (int i = 0; i < 5; ++i) {
                const double scaled = static_cast<double>(sound_volume()) * volume->level[i] * kPercent;
                level[i] = static_cast<float>(scaled);
                last = scaled;
            }
            const double sum = last + level[3] + level[2] + level[1] + level[0];
            slot.volume = static_cast<float>(sum * kFifth);
            if (slot.wad)
                slot.wad->set_channel_volumes(level[0], level[1], level[2], level[3], level[4]);
            else if (slot.bink)
                slot.bink->set_channel_volumes(level[0], level[1], level[2], level[3], level[4]);
            break;
        }
        case 0:
        case 1:
        case 3: {
            // Left and right (the original has this twice: through StreamSlot::set_volume_and_pan for 1, inline
            // for 0 and 3).
            auto left = static_cast<float>(static_cast<double>(sound_volume()) * volume->level[0] * kPercent);
            auto right = static_cast<float>(static_cast<double>(sound_volume()) * volume->level[1] * kPercent);
            slot.volume = left;
            slot.set_volume_and_pan(left, right);
            break;
        }
        default:
            break;
        }
        release_sound_manager();
    }
    ReleaseMutex(stream_lock());
    return true;
}

// 0x00454080
bool load_stream(uint32_t checksum, int slot_index, const StreamGain* volume, int, int, bool preload, int plays)
{
    const int entry = find_wad_entry(checksum);
    if (entry < 0)
        return false;
    const uint32_t offset = wad_entries()[entry].offset;
    const uint32_t size = wad_entries()[entry].size;
    if (stream_status(slot_index) != 1)
        return false;
    WaitForSingleObject(stream_lock(), INFINITE);
    StreamSlot& slot = stream_slots()[slot_index];
    slot.fabricate(1, true, plays);
    if (preload)
        set_started(slot, false);
    std::array<char, 256> path{};
    std::snprintf(path.data(), sizeof(path), "%s%s", data_root(), R"(data\streams\pcm\pcm.wad)");
    if (slot.unseal(checksum, path.data(), get_wad_file(), offset, size, stream_buffer(slot_index)) != 0) {
        slot.dealloc();
        ReleaseMutex(stream_lock());
        return false;
    }
    if (volume)
        change_stream_loudness(volume, slot_index);
    ReleaseMutex(stream_lock());
    return true;
}

// 0x00454200
bool play_stream(uint32_t checksum, int slot, int unused)
{
    last_streams()[slot] = checksum;
    if (!load_stream(checksum, slot, nullptr, 0, unused, false, 1))
        return false;
    set_started(stream_slots()[slot], false);
    return true;
}

// 0x00454270
bool start_preloaded_stream(int slot, const StreamGain* volume)
{
    if (!stream_slots()[slot].get_playing())
        return false;
    set_started(stream_slots()[slot], true);
    change_stream_loudness(volume, slot);
    return true;
}

// 0x00453B30
void halt_stream(int slot, int) { halt(stream_slots()[slot]); }

// 0x00453BD0
void halt_streams()
{
    for (int i = 0; i < kStreamSlots; ++i)
        halt_stream(i, 0);
}

} // namespace audio

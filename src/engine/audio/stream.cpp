// The PC's streamed audio: the music's and the three streams' slots, the thread servicing them, pcm.wad's
// index, pausing, freeing what finished, and the (absent) user soundtracks.

#include "engine/audio/stream.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "engine/core/mem.h"
#include "engine/audio/stream_commands.h"
#include "engine/sys/startup_init.h"

namespace audio {

void* g_wad_file{INVALID_HANDLE_VALUE};
WadEntry* g_wad_entries;
void* g_stream_lock; // a mutex (HANDLE), made at start-up

namespace {

// The lock the game thread and the stream thread take around the streams (a mutex made at start-up).
HANDLE stream_lock() { return g_stream_lock; }

int g_next_overlapped;
uint32_t g_wad_entry_count;
HANDLE g_thread_handle;
bool g_stop_thread;
bool g_soundtracks_counted;
int g_soundtrack_count;

HANDLE& thread_handle() { return g_thread_handle; }
bool& stop_thread() { return g_stop_thread; }

// pcm.wad (opened for streaming) and pcm.dat's index of it.
HANDLE& get_wad_file() { return g_wad_file; }
WadEntry*& wad_entries() { return g_wad_entries; }
uint32_t& wad_entry_count() { return g_wad_entry_count; }

// The ring of OVERLAPPED blocks streams read with, and the next one to try.
OVERLAPPED g_overlapped[256];
OVERLAPPED* get_overlapped() { return g_overlapped; }
constexpr int kOverlapSlots = 256;
int& next_overlapped() { return g_next_overlapped; }

// The user soundtracks (0x4C bytes each at 0x006EE024: the track count, then the name at +8).
bool& soundtracks_counted() { return g_soundtracks_counted; }
int& soundtrack_count() { return g_soundtrack_count; }
// (Never filled on PC - the count stays 0 - so the slots are only the original's room: 99 of them before the next variable.)
constexpr int kSoundtrackSize = 0x4C, kSoundtrackSlots = 99;
uint8_t g_soundtracks[kSoundtrackSlots * kSoundtrackSize];
uint8_t* get_soundtrack(int index) { return g_soundtracks + index * kSoundtrackSize; }

constexpr uint32_t kPending = 0x8000000Au; // E_PENDING

// The BinkStream constructor and its open of a pcm.wad entry (written over BinkStreamView, stream_commands.h).
BinkStream* construct_bink_stream(void* memory, bool unknown, int plays)
{
    return reinterpret_cast<BinkStream*>(static_cast<BinkStreamView*>(memory)->make_new(unknown, plays));
}
Result open_bink_from_wad(BinkStream* bink, uint32_t checksum, const char* path, uint32_t offset, uint32_t size)
{
    return reinterpret_cast<BinkStreamView*>(bink)->unseal(checksum, path, static_cast<long>(offset), size);
}

// Whether the slot's stream uses this OVERLAPPED block.
bool uses_overlapped(const StreamSlot& slot, const OVERLAPPED* overlapped)
{
    if (slot.wad)
        return slot.wad->async_ctl_ == overlapped;
    if (slot.bink)
        return slot.bink->async_ctl_ == overlapped;
    return false;
}

// A stream that finished (and isn't pending) or was asked to stop and may go.
template <typename Stream>
bool get_over(Stream* stream)
{
    if (stream->finished_ && stream->get_standing() != kPending)
        return true;
    return stream->stop_requested_ && stream->ready_to_free();
}
bool get_over(const StreamSlot& slot)
{
    if (slot.wad)
        return get_over(slot.wad);
    if (slot.bink)
        return get_over(slot.bink);
    return false;
}

bool prefetch_complete(const StreamSlot& slot)
{
    if (slot.wad)
        return slot.wad->prefetch_complete();
    if (slot.bink)
        return slot.bink->prefetch_complete();
    return true;
}

} // namespace

StreamSlot g_stream_slots[3];

StreamSlot* stream_slots() { return g_stream_slots; }

StreamSlot g_music_slot;

StreamSlot& music_slot() { return g_music_slot; }

// --- StreamSlot --------------------------------------------------------------------------------------------

// 0x00453500
void StreamSlot::fabricate(int type, bool unknown, int plays)
{
    if (type == 0) {
        void* memory = memory::alloc_zeroed(sizeof(WadStream));
        wad = memory ? static_cast<WadStream*>(memory)->make_new(unknown) : nullptr;
    } else if (type == 1) {
        void* memory = memory::alloc_zeroed(sizeof(BinkStream));
        bink = memory ? construct_bink_stream(memory, unknown, plays) : nullptr;
    }
}

// 0x00453090
Result StreamSlot::unseal(uint32_t checksum, const char* path, Hdl wad_file, uint32_t offset, uint32_t size,
                          void* buffer)
{
    if (wad)
        return wad->open_from_wad(wad_file, offset, size, buffer);
    if (bink)
        return open_bink_from_wad(bink, checksum, path, offset, size);
    return -1;
}

// 0x004530E0
void StreamSlot::set_volume_and_pan(float left, float right)
{
    if (wad)
        wad->set_volume_and_pan(left, right);
    else if (bink)
        bink->set_volume_and_pan(left, right);
}

// 0x004538F0
void StreamSlot::dealloc()
{
    if (WadStream* stream = wad) {
        stream->destruct();
        memory::dealloc(stream);
        wad = nullptr;
    }
    if (BinkStream* stream = bink) {
        stream->destruct();
        memory::dealloc(stream);
        bink = nullptr;
    }
}

// --- The stream thread -------------------------------------------------------------------------------------

namespace {

void update_slot(StreamSlot& slot)
{
    if (slot.wad)
        slot.wad->tick_frame();
    else if (slot.bink)
        slot.bink->tick_frame();
}

} // namespace

// 0x00453100
unsigned long __stdcall stream_thread(void*)
{
    while (!stop_thread()) {
        if (WaitForSingleObject(stream_lock(), INFINITE) != WAIT_TIMEOUT) {
            update_slot(music_slot());
            for (int i = 0; i < kStreamSlots; ++i)
                update_slot(stream_slots()[i]);
            ReleaseMutex(stream_lock());
        }
        Sleep(5);
    }
    return 0;
}

// 0x004531A0
void __cdecl stop_stream_thread()
{
    if (HANDLE thread = thread_handle()) {
        stop_thread() = true;
        WaitForSingleObject(thread, INFINITE);
        thread_handle() = nullptr;
    }
}

// 0x004535E0
_OVERLAPPED* __cdecl new_overlapped()
{
    for (;;) {
        // (loops for ever if all 256 are in use)
        OVERLAPPED* overlapped = &get_overlapped()[next_overlapped()];
        int next = next_overlapped() + 1;
        next_overlapped() = next >= kOverlapSlots ? 0 : next;
        if (uses_overlapped(music_slot(), overlapped))
            continue;
        bool used = false;
        for (int i = 0; i < kStreamSlots && !used; ++i)
            used = uses_overlapped(stream_slots()[i], overlapped);
        if (used)
            continue;
        std::memset(overlapped, 0, sizeof(*overlapped));
        return overlapped;
    }
}

// 0x00453670
void __cdecl init_platform()
{
    std::memset(&music_slot(), 0, sizeof(StreamSlot));
    std::memset(stream_slots(), 0, kStreamSlots * sizeof(StreamSlot));
    user_soundtrack_count();
    get_wad_file() = CreateFileA("data\\streams\\pcm\\pcm.wad", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, nullptr);
    HANDLE index = CreateFileA("data\\streams\\pcm\\pcm.dat", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (index != INVALID_HANDLE_VALUE) {
        DWORD read;
        ReadFile(index, &wad_entry_count(), 4, &read, nullptr);
        wad_entries() = static_cast<WadEntry*>(memory::alloc(wad_entry_count() * sizeof(WadEntry)));
        ReadFile(index, wad_entries(), wad_entry_count() * sizeof(WadEntry), &read, nullptr);
        CloseHandle(index);
    }
    // Sorted by checksum for find_wad_entry (an exchange sort).
    WadEntry* entries = wad_entries();
    for (uint32_t i = 0; i < wad_entry_count(); ++i) {
        for (uint32_t j = i + 1; j < wad_entry_count(); ++j) {
            if (entries[i].checksum > entries[j].checksum) {
                WadEntry swap = entries[i];
                entries[i] = entries[j];
                entries[j] = swap;
            }
        }
    }
    if (!thread_handle()) {
        DWORD id = 0;
        thread_handle() = CreateThread(nullptr, 0, stream_thread, nullptr, 0, &id);
        SetThreadPriority(thread_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
    }
}

// --- pcm.wad's index ---------------------------------------------------------------------------------------

// 0x004533A0
int find_wad_entry(uint32_t checksum)
{
    int low = 0;
    int high = static_cast<int>(wad_entry_count()) - 1;
    while (low <= high) {
        int middle = (low + high) / 2;
        uint32_t found = wad_entries()[middle].checksum;
        if (found < checksum)
            low = middle + 1;
        else if (found == checksum)
            return middle;
        else
            high = middle - 1;
    }
    return -1;
}

// 0x004533E0
uint32_t __cdecl stream_exists(uint32_t checksum, int music)
{
    if (music)
        return 0;
    return find_wad_entry(checksum) != -1 ? checksum : 0;
}

// --- State -------------------------------------------------------------------------------------------------

// 0x00453250
int __cdecl music_status() { return music_slot().get_playing() ? 2 : 1; }

// 0x00453270
int __cdecl stream_status(int slot)
{
    int first = slot == -1 ? 0 : slot;
    int end = slot == -1 ? kStreamSlots : slot + 1;
    for (int i = first; i < end; ++i)
        if (!stream_slots()[i].get_playing())
            return 1;
    return 2;
}

// 0x00453220
bool __cdecl music_preload_done() { return prefetch_complete(music_slot()); }

// 0x004531D0
bool __cdecl stream_preload_done(int slot) { return prefetch_complete(stream_slots()[slot]); }

// 0x004532C0
void __cdecl pause_streams(bool pause, int which)
{
    if (which == 1) {
        StreamSlot& music = music_slot();
        music.paused = pause;
        if (music.wad)
            music.wad->get_freeze(pause ? 1 : 0);
        else if (music.bink)
            music.bink->get_freeze(pause ? 1 : 0);
        return;
    }
    for (int i = 0; i < kStreamSlots; ++i) {
        StreamSlot& slot = stream_slots()[i];
        if (!slot.get_playing())
            continue;
        if (slot.wad)
            slot.wad->get_freeze(pause ? 1 : 0);
        else if (slot.bink)
            slot.bink->get_freeze(pause ? 1 : 0);
        slot.paused = pause;
    }
}

// 0x00453940
int __cdecl update_streams()
{
    WaitForSingleObject(stream_lock(), INFINITE);
    if (get_over(music_slot()))
        music_slot().dealloc();
    for (int i = 0; i < kStreamSlots; ++i)
        if (get_over(stream_slots()[i]))
            stream_slots()[i].dealloc();
    ReleaseMutex(stream_lock());
    return 0;
}

// --- User soundtracks --------------------------------------------------------------------------------------

// 0x004542D0
int __cdecl user_soundtrack_count()
{
    if (!soundtracks_counted()) {
        soundtracks_counted() = true;
        soundtrack_count() = 0; // (none on PC)
    }
    return soundtrack_count();
}

// 0x00454300
const wchar_t* __cdecl user_soundtrack_name(int index)
{
    return index < soundtrack_count() ? reinterpret_cast<const wchar_t*>(get_soundtrack(index) + 8) : nullptr;
}

// 0x00454320
int __cdecl user_soundtrack_tracks(int index)
{
    return index < soundtrack_count() ? *reinterpret_cast<int*>(get_soundtrack(index)) : 0;
}

} // namespace audio

namespace audio {

// 0x006409D0 (a static initialiser): the streams' lock, a mutex nobody owns.
void create_stream_lock() { g_stream_lock = CreateMutexA(nullptr, FALSE, nullptr); }

} // namespace audio

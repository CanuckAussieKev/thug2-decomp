// The PC's streamed audio: the music's and the three streams' slots, the thread servicing them, pcm.wad's
// index, pausing, freeing what finished, and the (absent) user soundtracks.

#include "engine/audio/stream.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <array>

#include "engine/core/checked_index.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "formats/stream_index_format.h"
#include "formats/byte_stream.h"
#include "engine/sys/startup_init.h"
#include "engine/core/no_destroy.h"
#include "engine/audio/audio_state.h"
#include <gsl/pointers>

namespace audio {

WadState& wad_state()
{
    static core::NoDestroy<WadState> state;
    return state.get();
}

namespace {

// The lock the game thread and the stream thread take around the streams (a mutex made at start-up).
HANDLE stream_lock() { return wad_state().stream_lock(); }

// (Never filled on PC - the count stays 0 - so the slots are only the original's room: 99 of them before the next variable.)
// A user soundtrack (0x4C bytes): its track count and name (none on PC).
struct UserSoundtrack {
    int tracks;
    uint32_t unknown4;
    std::array<wchar_t, 0x22> name;
};
constexpr int kSoundtrackSlots = 99;

// NOT ORIGINAL grouping - the stream thread, the OVERLAPPED ring, the user soundtracks and the stream slots. Made on
// first use, never destroyed (stream_io_state()).
struct StreamIoState {
    int next_overlapped_{};
    uint32_t wad_entry_count_{};
    HANDLE thread_handle_{};
    bool stop_thread_{};
    bool soundtracks_counted_{};
    int soundtrack_count_{};
    // The ring of OVERLAPPED blocks streams read with, and the next one to try.
    std::array<OVERLAPPED, 256> overlapped_{};
    std::array<UserSoundtrack, kSoundtrackSlots> soundtracks_{};
    std::array<StreamSlot, 3> stream_slots_{};
    StreamSlot music_slot_{};
};

StreamIoState& stream_io_state()
{
    static core::NoDestroy<StreamIoState> state;
    return state.get();
}

HANDLE& thread_handle() { return stream_io_state().thread_handle_; }
bool& stop_thread() { return stream_io_state().stop_thread_; }

// pcm.wad (opened for streaming) and pcm.dat's index of it.
HANDLE& get_wad_file() { return wad_state().wad_file(); }
WadEntry*& wad_entries() { return wad_state().wad_entries(); }
uint32_t& wad_entry_count() { return stream_io_state().wad_entry_count_; }

std::array<OVERLAPPED, 256>& get_overlapped() { return stream_io_state().overlapped_; }
constexpr int kOverlapSlots = 256;
int& next_overlapped() { return stream_io_state().next_overlapped_; }

// The user soundtracks (0x4C bytes each at 0x006EE024: the track count, then the name at +8).
bool& soundtracks_counted() { return stream_io_state().soundtracks_counted_; }
int& soundtrack_count() { return stream_io_state().soundtrack_count_; }
UserSoundtrack& get_soundtrack(int index) { return core::at(stream_io_state().soundtracks_, index); }

constexpr uint32_t kPending = 0x8000000Au; // E_PENDING

// The BinkStream constructor and its open of a pcm.wad entry (written over BinkStreamView, stream_commands.h).
gsl::owner<BinkStream*> construct_bink_stream(void* memory, bool unknown, int plays)
{
    return new (memory) BinkStream(unknown, plays);
}
Result open_bink_from_wad(BinkStream* bink, uint32_t checksum, const char* path, uint32_t offset, uint32_t size)
{
    return bink->unseal(checksum, path, static_cast<long>(offset), size);
}

// Whether the slot's stream uses this OVERLAPPED block.
bool uses_overlapped(const StreamSlot& slot, const OVERLAPPED* overlapped)
{
    if (slot.wad())
        return slot.wad()->async_ctl() == overlapped;
    if (slot.bink())
        return slot.bink()->async_ctl() == overlapped;
    return false;
}

// A stream that finished (and isn't pending) or was asked to stop and may go.
template <typename Stream>
bool get_over(Stream* stream)
{
    if (stream->finished() && stream->get_standing() != kPending)
        return true;
    return stream->stop_requested() && stream->ready_to_free();
}
bool get_over(const StreamSlot& slot)
{
    if (slot.wad())
        return get_over(slot.wad());
    if (slot.bink())
        return get_over(slot.bink());
    return false;
}

bool prefetch_complete(const StreamSlot& slot)
{
    if (slot.wad())
        return slot.wad()->prefetch_complete();
    if (slot.bink())
        return slot.bink()->prefetch_complete();
    return true;
}

} // namespace

std::array<StreamSlot, 3>& stream_slots() { return stream_io_state().stream_slots_; }
std::span<WadEntry> wad_entry_run() { return {wad_state().wad_entries(), stream_io_state().wad_entry_count_}; }

namespace {
} // namespace

StreamSlot& music_slot() { return stream_io_state().music_slot_; }

// --- StreamSlot --------------------------------------------------------------------------------------------

// 0x00453500
void StreamSlot::fabricate(int type, bool unknown, int plays)
{
    if (type == 0) {
        void* memory = memory::alloc_zeroed(sizeof(WadStream));
        wad_ = memory ? memory::construct_in<WadStream>(memory, unknown) : nullptr;
    } else if (type == 1) {
        void* memory = memory::alloc_zeroed(sizeof(BinkStream));
        bink_ = memory ? construct_bink_stream(memory, unknown, plays) : nullptr;
    }
}

// 0x00453090
Result StreamSlot::unseal(uint32_t checksum, const char* path, Hdl wad_file, uint32_t offset, uint32_t size,
                          void* buffer)
{
    if (wad_)
        return wad_->open_from_wad(wad_file, offset, size, buffer);
    if (bink_)
        return open_bink_from_wad(bink_, checksum, path, offset, size);
    return -1;
}

// 0x004530E0
void StreamSlot::set_volume_and_pan(float left, float right)
{
    if (wad_)
        wad_->set_volume_and_pan(left, right);
    else if (bink_)
        bink_->set_volume_and_pan(left, right);
}

// 0x004538F0
void StreamSlot::dealloc()
{
    if (WadStream* stream = wad_) {
        stream->destruct();
        memory::dealloc(stream);
        wad_ = nullptr;
    }
    if (BinkStream* stream = bink_) {
        stream->destruct();
        memory::dealloc(stream);
        bink_ = nullptr;
    }
}

// --- The stream thread -------------------------------------------------------------------------------------

namespace {

void update_slot(StreamSlot& slot)
{
    if (slot.wad())
        slot.wad()->tick_frame();
    else if (slot.bink())
        slot.bink()->tick_frame();
}

} // namespace

// 0x00453100
unsigned long __stdcall stream_thread(void*)
{
    while (!stop_thread()) {
        if (WaitForSingleObject(stream_lock(), INFINITE) != WAIT_TIMEOUT) {
            update_slot(music_slot());
            for (int i = 0; i < kStreamSlots; ++i)
                update_slot(core::at(stream_slots(), i));
            ReleaseMutex(stream_lock());
        }
        Sleep(5);
    }
    return 0;
}

// 0x004531A0
void stop_stream_thread()
{
    if (HANDLE thread = thread_handle()) {
        stop_thread() = true;
        WaitForSingleObject(thread, INFINITE);
        thread_handle() = nullptr;
    }
}

// 0x004535E0
_OVERLAPPED* new_overlapped()
{
    for (;;) {
        // (loops for ever if all 256 are in use)
        OVERLAPPED* overlapped = &core::at(get_overlapped(), next_overlapped());
        const int next = next_overlapped() + 1;
        next_overlapped() = next >= kOverlapSlots ? 0 : next;
        if (uses_overlapped(music_slot(), overlapped))
            continue;
        bool used = false;
        for (int i = 0; i < kStreamSlots && !used; ++i)
            used = uses_overlapped(core::at(stream_slots(), i), overlapped);
        if (used)
            continue;
        std::memset(overlapped, 0, sizeof(*overlapped));
        return overlapped;
    }
}

// 0x00453670
void init_platform()
{
    std::memset(&music_slot(), 0, sizeof(StreamSlot));
    std::memset(stream_slots().data(), 0, kStreamSlots * sizeof(StreamSlot));
    user_soundtrack_count();
    get_wad_file() = CreateFileA(R"(data\streams\pcm\pcm.wad)", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, nullptr);
    HANDLE index = CreateFileA(R"(data\streams\pcm\pcm.dat)", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (index != INVALID_HANDLE_VALUE) {
        // Not original - the index read whole and through stream_index_format.h: a count past the entries the file
        // holds is cut to them (the original took it as it came and read short, the rest of the table left as it
        // was), and an entry outside pcm.wad is dropped (it would be streamed from past the file).
        const DWORD size = GetFileSize(index, nullptr);
        std::vector<uint8_t> contents(size == INVALID_FILE_SIZE ? 0 : size);
        DWORD read = 0;
        if (!contents.empty() && !ReadFile(index, contents.data(), size, &read, nullptr))
            read = 0;
        CloseHandle(index);
        contents.resize(read);
        util::ByteReader reader(contents.data(), contents.size());
        uint32_t count = 0;
        if (!stream_index_format::read_count(reader, &count)) {
            debug::printf("pcm.dat damaged: %u entries, %u fit\n", count,
                          static_cast<uint32_t>(reader.remaining() / stream_index_format::kEntrySize));
            count = static_cast<uint32_t>(reader.remaining() / stream_index_format::kEntrySize);
            reader = util::ByteReader(contents.data(), contents.size());
            reader.skip(4);
        }
        LARGE_INTEGER wad_size{};
        if (get_wad_file() == INVALID_HANDLE_VALUE || !GetFileSizeEx(get_wad_file(), &wad_size))
            wad_size.QuadPart = 0;
        wad_entries() = static_cast<WadEntry*>(memory::alloc(count * sizeof(WadEntry)));
        const std::span<WadEntry> table(wad_entries(), count);
        wad_entry_count() = 0;
        for (uint32_t i = 0; i < count; ++i) {
            stream_index_format::Entry entry;
            stream_index_format::read_entry(reader, &entry);
            if (!stream_index_format::within(entry, static_cast<uint64_t>(wad_size.QuadPart))) {
                debug::printf("pcm.dat: stream %08x outside pcm.wad, dropped\n", entry.checksum);
                continue;
            }
            core::at(table, wad_entry_count()++) = WadEntry{.checksum = entry.checksum, .offset = entry.offset, .size = entry.size};
        }
    }
    // Sorted by checksum for find_wad_entry (an exchange sort).
    const std::span<WadEntry> entries = wad_entry_run();
    for (uint32_t i = 0; i < wad_entry_count(); ++i) {
        for (uint32_t j = i + 1; j < wad_entry_count(); ++j) {
            if (core::at(entries, i).checksum > core::at(entries, j).checksum)
                std::swap(core::at(entries, i), core::at(entries, j));
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
        const int middle = (low + high) / 2;
        const uint32_t found = core::at(wad_entry_run(), middle).checksum;
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
uint32_t stream_exists(uint32_t checksum, int music)
{
    if (music)
        return 0;
    return find_wad_entry(checksum) != -1 ? checksum : 0;
}

// --- State -------------------------------------------------------------------------------------------------

// 0x00453250
int music_status() { return music_slot().get_playing() ? 2 : 1; }

// 0x00453270
int stream_status(int slot)
{
    const int first = slot == -1 ? 0 : slot;
    const int end = slot == -1 ? kStreamSlots : slot + 1;
    for (int i = first; i < end; ++i)
        if (!core::at(stream_slots(), i).get_playing())
            return 1;
    return 2;
}

// 0x00453220
bool music_preload_done() { return prefetch_complete(music_slot()); }

// 0x004531D0
bool stream_preload_done(int slot) { return prefetch_complete(core::at(stream_slots(), slot)); }

// 0x004532C0
void pause_streams(bool pause, int which)
{
    if (which == 1) {
        StreamSlot& music = music_slot();
        music.set_paused(pause);
        if (music.wad())
            music.wad()->get_freeze(pause ? 1 : 0);
        else if (music.bink())
            music.bink()->get_freeze(pause ? 1 : 0);
        return;
    }
    for (int i = 0; i < kStreamSlots; ++i) {
        StreamSlot& slot = core::at(stream_slots(), i);
        if (!slot.get_playing())
            continue;
        if (slot.wad())
            slot.wad()->get_freeze(pause ? 1 : 0);
        else if (slot.bink())
            slot.bink()->get_freeze(pause ? 1 : 0);
        slot.set_paused(pause);
    }
}

// 0x00453940
int update_streams()
{
    WaitForSingleObject(stream_lock(), INFINITE);
    if (get_over(music_slot()))
        music_slot().dealloc();
    for (int i = 0; i < kStreamSlots; ++i)
        if (get_over(core::at(stream_slots(), i)))
            core::at(stream_slots(), i).dealloc();
    ReleaseMutex(stream_lock());
    return 0;
}

// --- User soundtracks --------------------------------------------------------------------------------------

// 0x004542D0
int user_soundtrack_count()
{
    if (!soundtracks_counted()) {
        soundtracks_counted() = true;
        soundtrack_count() = 0; // (none on PC)
    }
    return soundtrack_count();
}

// 0x00454300
const wchar_t* user_soundtrack_name(int index)
{
    return index < soundtrack_count() ? get_soundtrack(index).name.data() : nullptr;
}

// 0x00454320
int user_soundtrack_tracks(int index)
{
    return index < soundtrack_count() ? get_soundtrack(index).tracks : 0;
}

} // namespace audio

namespace audio {

// 0x006409D0 (a static initialiser): the streams' lock, a mutex nobody owns.
void create_stream_lock() { wad_state().set_stream_lock(CreateMutexA(nullptr, FALSE, nullptr)); }

} // namespace audio

// The PC's streams are Bink files (sound only), opened from memory: the cache of stream files read into memory,
// and the Bink stream's constructor and open (its other methods, 0x00452D20 on, are with the platform's streams).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <gsl/pointers>

#include "engine/audio/stream_commands.h"

#include "engine/core/dbg.h"
#include "engine/audio/stream.h"
#include "engine/audio/stream_files.h"
#include "platform/crt.h"
#include "platform/bink.h"
#include "engine/core/checked_index.h"
#include "engine/core/mem.h"
#include "engine/audio/audio_state.h"
#include "engine/core/no_destroy.h"

namespace audio {

StreamFileState& stream_file_state()
{
    static core::NoDestroy<StreamFileState> state;
    return state.get();
}

namespace {

std::array<CachedStreamFile, 12>& stream_files() { return stream_file_state().stream_files(); }
constexpr int kStreamFiles = 12;

// Whether the stream files' table is set up (NOT ORIGINAL: a function-local static, was a global).
bool& stream_files_ready()
{
    static bool ready = false;
    return ready;
}
uint32_t& stream_file_uses() { return stream_file_state().stream_file_uses(); }

// The game's C runtime (its heap and files; until the runtime is linked).
gsl::owner<FILE*> crt_fopen(const char* path, const char* mode) { return crt::get_fopen(path, mode); }
int crt_fseek(FILE* file, long offset, int origin) { return crt::get_fseek(file, offset, origin); }
size_t crt_fread(void* buffer, size_t size, size_t count, FILE* file)
{
    return crt::get_fread(buffer, size, count, file);
}
int crt_fclose(gsl::owner<FILE*> file) { return crt::get_fclose(file); }
gsl::owner<void*> get_crt_malloc(size_t size) { return crt::alloc_block(size); }
void get_crt_free(gsl::owner<void*> block) { crt::dealloc(block); }

constexpr uint32_t kBinkFromMemory = 0x04000000;
constexpr long kInvalidArg = static_cast<long>(0x80070057); // E_INVALIDARG
bool& bink_sound_set() { return stream_file_state().bink_sound_set(); }

// The next of the platform's 256 OVERLAPPED blocks (0x14 bytes each, at 0x0069CBB8) not in use.
void* next_stream_buffer() { return new_overlapped(); }

} // namespace

// 0x00452A90
int load_stream_file(uint32_t key, const char* path, long offset, size_t size)
{
    if (!stream_files_ready()) {
        for (int i = 0; i < kStreamFiles; ++i)
            core::at(stream_files(), i) = {.key = 0xFFFFFFFFu, .data = nullptr, .last_use_ = 0, .users = 0};
        stream_files_ready() = true;
    }
    const uint32_t now = ++stream_file_uses();
    int slot = -1;
    bool found_free = false;
    uint32_t longest = 0;
    for (int i = 0; i < kStreamFiles; ++i) {
        CachedStreamFile& file = core::at(stream_files(), i);
        if (file.key == key) {
            file.last_use_ = now;
            ++file.users;
            return i;
        }
        if (!file.data) { // (the last free slot wins)
            found_free = true;
            slot = i;
        } else if (!found_free && file.users == 0 && now - file.last_use_ > longest) {
            longest = now - file.last_use_;
            slot = i;
        }
    }
    if (slot == -1)
        return -1;
    gsl::owner<FILE*> in = crt_fopen(path, "rb"); // "rb"
    if (!in)
        return -1;
    CachedStreamFile& file = core::at(stream_files(), slot);
    if (file.data)
        get_crt_free(memory::owned_element(file.data)); // (each slot's own)
    ++file.users;
    file.data = nullptr;
    file.last_use_ = stream_file_uses();
    crt_fseek(in, offset, SEEK_SET);
    gsl::owner<void*> block = get_crt_malloc(size);
    file.data = block;
    crt_fread(file.data, size, 1, in);
    file.key = key;
    crt_fclose(in);
    return slot;
}

// 0x00452C00
BinkStream::BinkStream(bool unknown19, int plays) : unknown19_(unknown19), plays_(plays)
{
    core::at(status_words_, 9) = 0; // (+0x58)
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    async_ctl_ = static_cast<_OVERLAPPED*>(next_stream_buffer());
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x00452C60
long BinkStream::unseal(uint32_t key, const char* path, long offset, size_t size)
{
    if (!bink_sound_set()) {
        if (!bink::use_direct_sound(music_state().sound_device()))
            debug::printf("Bink can't use DSound\n"); // "Bink can't use DSound\n"
        bink_sound_set() = true;
    }
    const int slot = load_stream_file(key, path, offset, size);
    if (slot < 0)
        return kInvalidArg;
    const void* data = core::at(stream_files(), slot).data;
    render::Bink* bink = bink::open(data, kBinkFromMemory);
    bink_ = bink;
    if (!bink)
        return kInvalidArg;
    bink::set_video_on_off(bink, 0);
    // (the file's first four bytes, "BIKi")
    std::memcpy(from_where_.data(), core::at(stream_files(), slot).data, sizeof(uint32_t));
    wad_entry_ = slot;
    from_handle_ = true;
    return 0;
}

} // namespace audio

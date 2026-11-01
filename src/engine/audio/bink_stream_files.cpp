// The PC's streams are Bink files (sound only), opened from memory: the cache of stream files read into memory,
// and the Bink stream's constructor and open (its other methods, 0x00452D20 on, are with the platform's streams).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

#include "engine/audio/stream_commands.h"

#include "engine/core/dbg.h"
#include "engine/audio/music_player.h"
#include "engine/audio/stream.h"
#include "engine/audio/stream_files.h"
#include "platform/crt.h"
#include "platform/bink.h"
#include "engine/core/checked_index.h"

namespace audio {

bool g_bink_sound_set;
uint32_t g_stream_file_uses;
std::array<CachedStreamFile, 12> g_stream_files;

namespace {

CachedStreamFile* stream_files() { return g_stream_files.data(); }
constexpr int kStreamFiles = 12;

bool g_stream_files_ready;

bool& stream_files_ready() { return g_stream_files_ready; }
uint32_t& stream_file_uses() { return g_stream_file_uses; }

// The game's C runtime (its heap and files; until the runtime is linked).
FILE* crt_fopen(const char* path, const char* mode) { return crt::get_fopen(path, mode); }
int crt_fseek(FILE* file, long offset, int origin) { return crt::get_fseek(file, offset, origin); }
size_t crt_fread(void* buffer, size_t size, size_t count, FILE* file)
{
    return crt::get_fread(buffer, size, count, file);
}
int crt_fclose(FILE* file) { return crt::get_fclose(file); }
void* get_crt_malloc(size_t size) { return crt::alloc_block(size); }
void get_crt_free(void* block) { crt::dealloc(block); }

constexpr uint32_t kBinkFromMemory = 0x04000000;
constexpr long kInvalidArg = static_cast<long>(0x80070057); // E_INVALIDARG
bool& bink_sound_set() { return g_bink_sound_set; }

// The next of the platform's 256 OVERLAPPED blocks (0x14 bytes each, at 0x0069CBB8) not in use.
void* next_stream_buffer() { return new_overlapped(); }

} // namespace

// 0x00452A90
int load_stream_file(uint32_t key, const char* path, long offset, size_t size)
{
    if (!stream_files_ready()) {
        for (int i = 0; i < kStreamFiles; ++i)
            stream_files()[i] = {0xFFFFFFFFu, nullptr, 0, 0};
        stream_files_ready() = true;
    }
    const uint32_t now = ++stream_file_uses();
    int slot = -1;
    bool found_free = false;
    uint32_t longest = 0;
    for (int i = 0; i < kStreamFiles; ++i) {
        CachedStreamFile& file = stream_files()[i];
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
    FILE* in = crt_fopen(path, "rb"); // "rb"
    if (!in)
        return -1;
    CachedStreamFile& file = stream_files()[slot];
    if (file.data)
        get_crt_free(file.data);
    ++file.users;
    file.data = nullptr;
    file.last_use_ = stream_file_uses();
    crt_fseek(in, offset, SEEK_SET);
    file.data = get_crt_malloc(size);
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
        if (!bink::use_direct_sound(g_sound_device))
            debug::printf("Bink can't use DSound\n"); // "Bink can't use DSound\n"
        bink_sound_set() = true;
    }
    const int slot = load_stream_file(key, path, offset, size);
    if (slot < 0)
        return kInvalidArg;
    const void* data = stream_files()[slot].data;
    render::Bink* bink = bink::open(data, kBinkFromMemory);
    bink_ = bink;
    if (!bink)
        return kInvalidArg;
    bink::set_video_on_off(bink, 0);
    // (the file's first four bytes, "BIKi")
    std::memcpy(from_where_.data(), stream_files()[slot].data, sizeof(uint32_t));
    wad_entry_ = slot;
    from_handle_ = true;
    return 0;
}

} // namespace audio

// The PC's streams are Bink files (sound only), opened from memory: the cache of stream files read into memory,
// and the Bink stream's constructor and open (its other methods, 0x00452D20 on, are with the platform's streams).

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "game/pcm/stream_commands.h"

#include "game/core/dbg.h"
#include "game/pcm/music.h"
#include "game/pcm/stream.h"
#include "game/pcm/stream_files.h"
#include "game/sys/crt.h"

namespace Pcm {

bool g_binkSoundSet;
uint32_t g_streamFileUses;
CachedStreamFile g_streamFiles[12];

namespace {

CachedStreamFile* StreamFiles() { return g_streamFiles; }
constexpr int kStreamFiles = 12;

bool g_streamFilesReady;

bool& StreamFilesReady() { return g_streamFilesReady; }
uint32_t& StreamFileUses() { return g_streamFileUses; }

// The game's C runtime (its heap and files; until the runtime is linked).
FILE* CrtFopen(const char* path, const char* mode) { return Crt::Fopen(path, mode); }
int CrtFseek(FILE* file, long offset, int origin) { return Crt::Fseek(file, offset, origin); }
size_t CrtFread(void* buffer, size_t size, size_t count, FILE* file) { return Crt::Fread(buffer, size, count, file); }
int CrtFclose(FILE* file) { return Crt::Fclose(file); }
void* CrtMalloc(size_t size) { return Crt::Malloc(size); }
void CrtFree(void* block) { Crt::Free(block); }

// The Bink SDK (binkw32.dll), through the game's import table.
int BinkSetSoundSystem(void* open, void* param)
{
    return reinterpret_cast<int(__stdcall*)(void*, void*)>(Crt::LibraryFunction("binkw32.dll", "_BinkSetSoundSystem@8"))(open, param);
}
// the function, passed to the above
void* BinkOpenDirectSound() { return reinterpret_cast<void*>(Crt::LibraryFunction("binkw32.dll", "_BinkOpenDirectSound@4")); }
void* BinkOpen(const void* source, uint32_t flags)
{
    return reinterpret_cast<void*(__stdcall*)(const void*, uint32_t)>(Crt::LibraryFunction("binkw32.dll", "_BinkOpen@8"))(source, flags);
}
void BinkSetVideoOnOff(void* bink, int on)
{
    reinterpret_cast<int(__stdcall*)(void*, int)>(Crt::LibraryFunction("binkw32.dll", "_BinkSetVideoOnOff@8"))(bink, on);
}
constexpr uint32_t kBinkFromMemory = 0x04000000;
constexpr long kInvalidArg = static_cast<long>(0x80070057); // E_INVALIDARG
bool& BinkSoundSet() { return g_binkSoundSet; }

// The next of the platform's 256 OVERLAPPED blocks (0x14 bytes each, at 0x0069CBB8) not in use.
void* NextStreamBuffer() { return NewOverlapped(); }

} // namespace

// 0x00452A90
int __cdecl LoadStreamFile(uint32_t key, const char* path, long offset, size_t size)
{
    if (!StreamFilesReady()) {
        for (int i = 0; i < kStreamFiles; ++i)
            StreamFiles()[i] = {0xFFFFFFFFu, nullptr, 0, 0};
        StreamFilesReady() = true;
    }
    uint32_t now = ++StreamFileUses();
    int slot = -1;
    bool foundFree = false;
    uint32_t longest = 0;
    for (int i = 0; i < kStreamFiles; ++i) {
        CachedStreamFile& file = StreamFiles()[i];
        if (file.key == key) {
            file.lastUse = now;
            ++file.users;
            return i;
        }
        if (!file.data) { // (the last free slot wins)
            foundFree = true;
            slot = i;
        } else if (!foundFree && file.users == 0 && now - file.lastUse > longest) {
            longest = now - file.lastUse;
            slot = i;
        }
    }
    if (slot == -1)
        return -1;
    FILE* in = CrtFopen(path, "rb"); // "rb"
    if (!in)
        return -1;
    CachedStreamFile& file = StreamFiles()[slot];
    if (file.data)
        CrtFree(file.data);
    ++file.users;
    file.data = nullptr;
    file.lastUse = StreamFileUses();
    CrtFseek(in, offset, SEEK_SET);
    file.data = CrtMalloc(size);
    CrtFread(file.data, size, 1, in);
    file.key = key;
    CrtFclose(in);
    return slot;
}

// 0x00452C00
BinkStreamView* BinkStreamView::Construct(bool unknown19, int unknown24)
{
    At<int>(0xC) = -1; // the stream file's slot
    At<uint8_t>(0x10) = 0;
    At<uint8_t>(0x11) = 0;
    At<void*>(0x28) = nullptr; // the Bink
    At<uint32_t>(0x1A8) = 0;
    At<uint32_t>(0x2C) = 0;
    At<uint32_t>(0x1AC) = 0;
    At<int>(0) = -1;
    At<uint32_t>(4) = 0;
    At<bool>(0x19) = unknown19;
    At<uint8_t>(0x6B) = 1;
    At<int>(0x24) = unknown24;
    At<uint32_t>(0x58) = 0;
    At<uint8_t>(0x61) = 0;
    At<uint8_t>(0x60) = 0;
    At<void*>(8) = NextStreamBuffer();
    return this;
}

// 0x00452C60
long BinkStreamView::Open(uint32_t key, const char* path, long offset, size_t size)
{
    if (!BinkSoundSet()) {
        if (!BinkSetSoundSystem(BinkOpenDirectSound(), g_soundDevice))
            Dbg::Printf("Bink can't use DSound\n"); // "Bink can't use DSound\n"
        BinkSoundSet() = true;
    }
    int slot = LoadStreamFile(key, path, offset, size);
    if (slot < 0)
        return kInvalidArg;
    void* data = StreamFiles()[slot].data;
    void* bink = BinkOpen(data, kBinkFromMemory);
    At<void*>(0x28) = bink;
    if (!bink)
        return kInvalidArg;
    BinkSetVideoOnOff(bink, 0);
    // (the file's first four bytes, "BIKi"; unaligned)
    At<uint32_t>(0x89) = *static_cast<const uint32_t*>(StreamFiles()[slot].data);
    At<int>(0xC) = slot;
    At<uint8_t>(0x88) = 1;
    return 0;
}

} // namespace Pcm

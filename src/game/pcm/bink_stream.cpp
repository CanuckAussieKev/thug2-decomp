// The PC's streams are Bink files (sound only), opened from memory: the cache of stream files read into memory,
// and the Bink stream's constructor and open (its other methods, 0x00452D20 on, are with the platform's streams).

#include <cstdint>
#include <cstdio>

#include "game/pcm/stream.h"

#include "game/core/dbg.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Pcm {

namespace {

// A stream file read into memory (12, at 0x0069CAD8): its key (-1: none), the data, the use count when it was last
// asked for, and how many streams use it (only unused ones are reused).
struct CachedStreamFile {
    uint32_t key;
    void* data;
    uint32_t lastUse;
    int users;
};
static_assert(sizeof(CachedStreamFile) == 16, "layout must match the original");
CachedStreamFile* StreamFiles() { return reinterpret_cast<CachedStreamFile*>(0x0069CAD8); }
constexpr int kStreamFiles = 12;
bool& StreamFilesReady() { return GameVariable<bool>(0x0069CBA0); }
uint32_t& StreamFileUses() { return GameVariable<uint32_t>(0x0069CB9C); }

// The game's C runtime (its heap and files; until the runtime is linked).
FILE* CrtFopen(const char* path, const char* mode) { return GameFunction<FILE*(__cdecl*)(const char*, const char*)>(0x006241E8)(path, mode); }
int CrtFseek(FILE* file, long offset, int origin) { return GameFunction<int(__cdecl*)(FILE*, long, int)>(0x006240D2)(file, offset, origin); }
size_t CrtFread(void* buffer, size_t size, size_t count, FILE* file)
{
    return GameFunction<size_t(__cdecl*)(void*, size_t, size_t, FILE*)>(0x00623F3E)(buffer, size, count, file);
}
int CrtFclose(FILE* file) { return GameFunction<int(__cdecl*)(FILE*)>(0x00623E04)(file); }
void* CrtMalloc(size_t size) { return GameFunction<void*(__cdecl*)(size_t)>(0x00624031)(size); }
void CrtFree(void* block) { GameFunction<void(__cdecl*)(void*)>(0x0062411B)(block); }

// The Bink SDK (binkw32.dll), through the game's import table until it's linked.
template <typename F>
F Import(uintptr_t slot) { return GameVariable<F>(slot); }
int BinkSetSoundSystem(void* open, void* param) { return Import<int(__stdcall*)(void*, void*)>(0x00645304)(open, param); }
void* BinkOpenDirectSound() { return GameVariable<void*>(0x00645300); } // the function, passed to the above
void* BinkOpen(const void* source, uint32_t flags) { return Import<void*(__stdcall*)(const void*, uint32_t)>(0x006452E8)(source, flags); }
void BinkSetVideoOnOff(void* bink, int on) { Import<int(__stdcall*)(void*, int)>(0x006452FC)(bink, on); }
constexpr uint32_t kBinkFromMemory = 0x04000000;
constexpr long kInvalidArg = static_cast<long>(0x80070057); // E_INVALIDARG
bool& BinkSoundSet() { return GameVariable<bool>(0x0069CB99); }

// 0x004535E0: the next of the platform's 256 stream buffers (0x14 bytes each, at 0x0069CBB8) not in use (not
// rewritten yet).
void* NextStreamBuffer() { return GameFunction<void*(__cdecl*)()>(0x004535E0)(); }

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
    FILE* in = CrtFopen(path, reinterpret_cast<const char*>(0x00647344)); // "rb"
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
        if (!BinkSetSoundSystem(BinkOpenDirectSound(), GameVariable<void*>(0x007D6A38)))
            Dbg::Printf(reinterpret_cast<const char*>(0x006472D4)); // "Bink can't use DSound\n"
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

REPLACE(0x00452A90, Pcm::LoadStreamFile);
REPLACE(0x00452C00, &Pcm::BinkStreamView::Construct);
REPLACE(0x00452C60, &Pcm::BinkStreamView::Open);

#include "game/file/pip.h"

#include <cstring>

#include "game/core/crc.h"
#include "game/file/file.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/shadow.h"

namespace Pip {

namespace {

constexpr int kMaxLoadedFiles = 200;
constexpr int kMaxPreFiles = 100;

LoadedFile* LoadedFiles() { return GameVariable<LoadedFile[kMaxLoadedFiles]>(0x007CF198); }
// Each loaded .pre archive's memory starts with the archive's name (see PreHeader).
char** PreFiles() { return GameVariable<char* [kMaxPreFiles]>(0x007CFE18); }

// The memory manager functions Pip loads files with (not rewritten yet): allocate from the current heap,
// give back the unused end of a block, free.
void* AllocateFileMemory(unsigned int size) { return GameFunction<void*(__cdecl*)(unsigned int)>(0x005BBAE0)(size); }
void ShrinkFileMemory(unsigned int size, void* memory)
{
    GameFunction<void*(__cdecl*)(unsigned int, void*)>(0x005BBB30)(size, memory);
}
void FreeFileMemory(void* memory) { GameFunction<void(__cdecl*)(void*)>(0x005BBB60)(memory); }

// Buffers are allocated in whole 2 KB sectors.
unsigned int SectorAligned(unsigned int size) { return (size + 0x7FF) & ~0x7FFu; }

// The pre-file manager (not rewritten yet): finds files inside .pre archives registered with it.
void* PreManagerLoadFile(const char* name, int* size)
{
    void* manager = GameVariable<void*>(0x007D0FCC);
    return GameFunction<void*(__thiscall*)(void*, const char*, int*, int)>(0x005B8750)(manager, name, size, 0);
}

// Loading from a CD cache: both hooks are stubs in the PC version (size 0, load fails).
int CachedFileSize(const char* name) { return GameFunction<int(__cdecl*)(const char*)>(0x005BA1D0)(name); }
bool LoadCachedFile(const char* name, void* buffer)
{
    return GameFunction<bool(__cdecl*)(const char*, void*)>(0x005B9E50)(name, buffer);
}

// The asynchronous file loader (not rewritten yet).
struct AsyncFile;
AsyncFile* AsyncOpen(const char* name)
{
    return GameFunction<AsyncFile*(__cdecl*)(const char*, int, int)>(0x005B6C60)(name, 0, 100);
}
int AsyncGetFileSize(AsyncFile* file) { return GameFunction<int(__thiscall*)(AsyncFile*)>(0x005B63A0)(file); }
void AsyncSetCallback(AsyncFile* file, void* callback, void* arg0, void* arg1)
{
    reinterpret_cast<int*>(file)[5] = 8; // priority?
    GameFunction<void(__thiscall*)(AsyncFile*, void*, void*, void*)>(0x005B6380)(file, callback, arg0, arg1);
}
void AsyncRead(AsyncFile* file, void* buffer, int size)
{
    GameFunction<void(__thiscall*)(AsyncFile*, void*, int, int)>(0x005B6740)(file, buffer, 1, size);
}
void AsyncClose(AsyncFile* file) { GameFunction<void(__cdecl*)(AsyncFile*)>(0x005B68A0)(file); }

// A loaded .pre archive: its name, padded to 16 bytes, then the archive's own header and its files.
struct PreHeader {
    int totalSize;
    int version;
    int numFiles;
};

PreHeader* HeaderOf(char* pre)
{
    return reinterpret_cast<PreHeader*>(pre + ((std::strlen(pre) + 0x10) & ~0xFu));
}

PreEntry* FirstEntry(PreHeader* header) { return reinterpret_cast<PreEntry*>(header + 1); }

PreEntry* NextEntry(PreEntry* entry)
{
    uint32_t stored = static_cast<uint32_t>(entry->compressedSize ? entry->compressedSize : entry->size);
    uintptr_t after_name = (reinterpret_cast<uintptr_t>(entry) + entry->nameSize + 0x1F) & ~uintptr_t(0xF);
    return reinterpret_cast<PreEntry*>(after_name + ((stored + 3) & ~3u));
}

} // namespace

// 0x005B7180
PreEntry* FindPreEntry(uint32_t checksum)
{
    for (int i = 0; i < kMaxPreFiles; ++i) {
        char* pre = PreFiles()[i];
        if (!pre)
            continue;
        PreHeader* header = HeaderOf(pre);
        PreEntry* entry = FirstEntry(header);
        for (int n = 0; n < header->numFiles; ++n, entry = NextEntry(entry)) {
            if (entry->checksum == checksum)
                return entry;
        }
    }
    return nullptr;
}

// 0x005B7C00 (PartyMod's script patching wraps calls to this, and calls it by address)
void* Load(const char* name, bool async, void* callback, void* callback_arg0, void* callback_arg1)
{
    uint32_t checksum = Crc::GenerateCRCFromString(name);

    // Already loaded?
    if (PreEntry* entry = FindPreEntry(checksum)) {
        ++entry->useCount;
        auto after_name = (reinterpret_cast<uintptr_t>(entry) + entry->nameSize + 0x1F) & ~uintptr_t(0xF);
        return reinterpret_cast<void*>(after_name);
    }
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        LoadedFile& file = LoadedFiles()[i];
        if (file.data && file.checksum == checksum) {
            ++file.refCount;
            return file.data;
        }
    }

    // The original assumes a free slot exists.
    LoadedFile* slot = nullptr;
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        if (!LoadedFiles()[i].data) {
            slot = &LoadedFiles()[i];
            break;
        }
    }

    void* data;
    int size;
    if (async) {
        AsyncFile* file = AsyncOpen(name);
        size = AsyncGetFileSize(file);
        data = AllocateFileMemory(SectorAligned(static_cast<unsigned int>(size)));
        if (callback)
            AsyncSetCallback(file, callback, callback_arg0, callback_arg1);
        AsyncRead(file, data, size);
        AsyncClose(file);
        if (data)
            ShrinkFileMemory(static_cast<unsigned int>(size), data);
    } else {
        size = 0;
        data = PreManagerLoadFile(name, &size);
        if (!data) {
            size = CachedFileSize(name);
            if (size != 0) {
                data = AllocateFileMemory(SectorAligned(static_cast<unsigned int>(size)));
                if (!LoadCachedFile(name, data)) {
                    FreeFileMemory(data);
                    data = nullptr;
                }
            } else {
                File::Handle file = File::Open(name, "rb");
                size = static_cast<int>(File::GetFileSize(file));
                data = AllocateFileMemory(SectorAligned(static_cast<unsigned int>(size)));
                File::Read(data, 1, size, file);
                File::Close(file);
            }
            if (data)
                ShrinkFileMemory(static_cast<unsigned int>(size), data);
        }
    }

    *slot = {data, checksum, size, 1};
    return data;
}

// 0x005B7200
void Unload(uint32_t checksum)
{
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        LoadedFile& file = LoadedFiles()[i];
        if (!file.data || file.checksum != checksum)
            continue;
        if (file.refCount != 0)
            --file.refCount;
        if (file.refCount == 0) {
            FreeFileMemory(file.data);
            file = {};
        }
    }
    if (PreEntry* entry = FindPreEntry(checksum)) {
        if (entry->useCount != 0)
            --entry->useCount;
    }
}

// 0x005B7270
void Unload(const char* name)
{
    Unload(Crc::GenerateCRCFromString(name));
}

// 0x005B7290
int GetFileSize(uint32_t checksum)
{
    for (int i = 0; i < kMaxLoadedFiles; ++i) {
        const LoadedFile& file = LoadedFiles()[i];
        if (file.data && file.checksum == checksum)
            return file.size;
    }
    PreEntry* entry = FindPreEntry(checksum);
    return entry ? entry->size : 0;
}

// 0x005B72E0
int GetFileSize(const char* name)
{
    return GetFileSize(Crc::GenerateCRCFromString(name));
}

// 0x005B7130
bool UnloadPre(const char* name)
{
    bool unloaded = false;
    for (int i = 0; i < kMaxPreFiles; ++i) {
        char*& pre = PreFiles()[i];
        if (pre && _stricmp(pre, name) == 0) {
            FreeFileMemory(pre);
            pre = nullptr;
            unloaded = true;
        }
    }
    return unloaded;
}

// 0x005B7300
const char* GetNextLoadedPre(const char* previous)
{
    bool found_previous = previous == nullptr;
    for (int i = 0; i < kMaxPreFiles; ++i) {
        const char* pre = PreFiles()[i];
        if (!pre)
            continue;
        if (found_previous)
            return pre;
        if (_stricmp(pre, previous) == 0)
            found_previous = true;
    }
    return nullptr;
}

// 0x005B7370
bool PreFileIsInUse(const char* name)
{
    for (int i = 0; i < kMaxPreFiles; ++i) {
        char* pre = PreFiles()[i];
        if (!pre || _stricmp(pre, name) != 0)
            continue;
        PreHeader* header = HeaderOf(pre);
        PreEntry* entry = FirstEntry(header);
        for (int n = 0; n < header->numFiles; ++n, entry = NextEntry(entry)) {
            if (entry->useCount != 0)
                return true;
        }
    }
    return false;
}

} // namespace Pip

static_assert(sizeof(Pip::PreEntry) == 16 && sizeof(Pip::LoadedFile) == 16, "layout must match the original");

REPLACE(0x005B7130, Pip::UnloadPre);
REPLACE_SHADOW(0x005B7180, Pip::FindPreEntry);
REPLACE(0x005B7200, static_cast<void (*)(uint32_t)>(&Pip::Unload));
REPLACE(0x005B7270, static_cast<void (*)(const char*)>(&Pip::Unload));
REPLACE_SHADOW(0x005B7290, static_cast<int (*)(uint32_t)>(&Pip::GetFileSize));
REPLACE_SHADOW(0x005B72E0, static_cast<int (*)(const char*)>(&Pip::GetFileSize));
REPLACE_SHADOW(0x005B7300, Pip::GetNextLoadedPre);
REPLACE(0x005B7C00, Pip::Load);
REPLACE_SHADOW(0x005B7370, Pip::PreFileIsInUse);

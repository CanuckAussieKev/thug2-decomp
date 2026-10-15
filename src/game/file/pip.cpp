#include "game/file/pip.h"

#include <cstring>

#include "game/core/crc.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Pip {

namespace {

constexpr int kMaxLoadedFiles = 200;
constexpr int kMaxPreFiles = 100;

LoadedFile* LoadedFiles() { return GameVariable<LoadedFile[kMaxLoadedFiles]>(0x007CF198); }
// Each loaded .pre archive's memory starts with the archive's name (see PreHeader).
char** PreFiles() { return GameVariable<char* [kMaxPreFiles]>(0x007CFE18); }

// Releases memory from the allocator Pip loads files with (memory manager, not rewritten yet).
void FreeFileMemory(void* memory) { GameFunction<void(__cdecl*)(void*)>(0x005BBB60)(memory); }

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
REPLACE(0x005B7180, Pip::FindPreEntry);
REPLACE(0x005B7200, static_cast<void (*)(uint32_t)>(&Pip::Unload));
REPLACE(0x005B7270, static_cast<void (*)(const char*)>(&Pip::Unload));
REPLACE(0x005B7290, static_cast<int (*)(uint32_t)>(&Pip::GetFileSize));
REPLACE(0x005B72E0, static_cast<int (*)(const char*)>(&Pip::GetFileSize));
REPLACE(0x005B7300, Pip::GetNextLoadedPre);
REPLACE(0x005B7370, Pip::PreFileIsInUse);

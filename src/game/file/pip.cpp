#include "game/file/pip.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/file/async.h"
#include "game/file/file.h"
#include "game/file/pre.h"
#include "game/front/data_overlay.h"
#include "game/script/params.h"
#include "game/util/lzss.h"
#include "game/gfx/loading_console.h"
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

void* AllocateFileMemory(unsigned int size) { return Mem::Malloc(size); }
void ShrinkFileMemory(unsigned int size, void* memory) { Mem::ReallocateShrink(size, memory); }
void FreeFileMemory(void* memory) { Mem::FreeMalloc(memory); }

// Buffers are allocated in whole 2 KB sectors.
unsigned int SectorAligned(unsigned int size) { return (size + 0x7FF) & ~0x7FFu; }

// The pre manager instance finds files inside .pre archives registered with it. (Like the original, this
// assumes it exists.)
void* PreManagerLoadFile(const char* name, int* size)
{
    return Pre::Instance()->LoadFile(name, size, nullptr);
}

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

// Archives as stored on disk: the name follows the header directly, the data the name, padded to 4 bytes.
// (Loaded archives are repacked with 16-byte aligned data - see NextEntry.)
PreEntry* NextRawEntry(PreEntry* entry)
{
    uint32_t stored = static_cast<uint32_t>(entry->compressedSize ? entry->compressedSize : entry->size);
    return reinterpret_cast<PreEntry*>(reinterpret_cast<uint8_t*>(entry + 1) + entry->nameSize + ((stored + 3) & ~3u));
}

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
    // Not original - the data overlay (game/front/data_overlay.h): a file of the data overlay is read from it,
    // before any archive (synchronously, through File::Open, which opens the overlay's file).
    // Original behaviour: archives first, then the cache and the data folder.
    char overlay_path[260];
    bool overlay = Front::DataOverlay::OverlayPath(name, overlay_path);

    // Already loaded?
    if (PreEntry* entry = overlay ? nullptr : FindPreEntry(checksum)) {
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
    if (async && !overlay) {
        File::AsyncFile* file = File::AsyncOpen(name);
        size = File::AsyncGetFileSize(file);
        data = AllocateFileMemory(SectorAligned(static_cast<unsigned int>(size)));
        if (callback) {
            File::AsyncSetCallbackMask(file, 1u << File::kAsyncRead); // call back when the read completes
            File::AsyncSetCallback(file, reinterpret_cast<File::AsyncCallback>(callback), callback_arg0,
                                   callback_arg1);
        }
        File::AsyncRead(file, data, size);
        File::AsyncClose(file);
        if (data)
            ShrinkFileMemory(static_cast<unsigned int>(size), data);
        Gfx::LoadingConsole::NoteFile("read (async)", name, size); // (not original: the loading console)
    } else {
        size = 0;
        data = overlay ? nullptr : PreManagerLoadFile(name, &size);
        if (data)
            Gfx::LoadingConsole::NoteFile("unpack", name, size); // (not original: the loading console)
        if (!data) {
            size = overlay ? 0 : File::CachedFileSize(name);
            if (size != 0) {
                data = AllocateFileMemory(SectorAligned(static_cast<unsigned int>(size)));
                if (!File::LoadCachedFile(name, data)) {
                    FreeFileMemory(data);
                    data = nullptr;
                }
                Gfx::LoadingConsole::NoteFile("read (cache)", name, size); // (not original: the loading console)
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

// 0x005B7B00: load `name` into `buffer` (or a new block if null), from a .pre archive or from disk, and set
// `*size` (if given) to its size. Returns the data, or null if it couldn't be loaded.
void* LoadFileInto(const char* name, int* size, void* buffer)
{
    int file_size = 0;
    // Not original - the data overlay: a file of it is read from there (File::Open), not an archive.
    // Original behaviour: archives first, then the cache and the data folder.
    char overlay_path[260];
    bool overlay = Front::DataOverlay::OverlayPath(name, overlay_path);
    void* data = overlay ? nullptr : Pre::Instance()->LoadFile(name, &file_size, static_cast<uint8_t*>(buffer));
    if (data)
        Gfx::LoadingConsole::NoteFile("unpack", name, file_size); // (not original: the loading console)
    if (!data) {
        file_size = overlay ? 0 : File::CachedFileSize(name);
        if (file_size != 0) {
            data = buffer ? buffer : AllocateFileMemory(SectorAligned(static_cast<unsigned int>(file_size)));
            if (!File::LoadCachedFile(name, data)) {
                if (!buffer)
                    FreeFileMemory(data);
                data = nullptr;
            }
        } else {
            File::Handle file = File::Open(name, "rb");
            file_size = static_cast<int>(File::GetFileSize(file));
            data = buffer ? buffer : AllocateFileMemory(SectorAligned(static_cast<unsigned int>(file_size)));
            File::Read(data, 1, file_size, file);
            File::Close(file);
        }
        if (!buffer && data)
            ShrinkFileMemory(static_cast<unsigned int>(file_size), data);
    }
    if (size)
        *size = file_size;
    return data;
}

// 0x005B75A0
void LoadPre(const char* name)
{
    for (int i = 0; i < kMaxPreFiles; ++i) {
        if (PreFiles()[i] && _stricmp(PreFiles()[i], name) == 0)
            return; // already loaded
    }
    int slot = -1;
    for (int i = 0; i < kMaxPreFiles; ++i) {
        if (!PreFiles()[i]) {
            slot = i;
            break;
        }
    }

    char path[500];
    std::snprintf(path, sizeof(path), "pre\\%s", name);
    // The block starts with the archive's name, padded to 16 bytes.
    unsigned int prefix = static_cast<unsigned int>((std::strlen(name) + 0x10) & ~size_t(0xF));
    size_t length = std::strlen(path);

    // French and German: a complete localised archive (.prf/.prd) if there is one, else a patch for the
    // English archive (.ppf/.ppd).
    uint32_t* patch = nullptr;
    bool patched = false;
    uint8_t* block = nullptr;
    unsigned int file_size = 0, allocated = 0;
    bool loaded = false;
    char letter = GameVariable<int>(0x007CEFA0) == 3 ? 'f' : GameVariable<int>(0x007CEFA0) == 5 ? 'd' : '\0';
    if (letter) {
        path[length - 1] = letter;
        if (!File::Exist(path)) {
            path[length - 2] = 'p';
            if (File::Exist(path)) {
                File::Handle file = File::Open(path, "rb");
                file_size = File::GetFileSize(file);
                patch = static_cast<uint32_t*>(Mem::New(file_size));
                File::Read(patch, 1, static_cast<int>(file_size), file);
                File::Close(file);
                path[length - 2] = 'r';
                path[length - 1] = 'e';
                patched = true;
                if (file_size != 0) {
                    // A leftover from the console versions' disc cache: always fails on PC, and the original
                    // then carries on patching through a null block - it crashes here. (Patch files aren't
                    // shipped with the PC version.)
                    allocated = ((file_size + 0x7FF) & ~0x7FFu) + prefix;
                    block = static_cast<uint8_t*>(AllocateFileMemory(allocated));
                    std::memcpy(block, name, std::strlen(name) + 1);
                    if (!File::LoadCachedFile(path, block + prefix)) {
                        FreeFileMemory(block);
                        block = nullptr;
                    }
                    loaded = true;
                }
            } else {
                path[length - 2] = 'r';
                path[length - 1] = 'e';
            }
        }
    }

    if (!loaded) {
        File::Handle file = File::Open(path, "rb");
        file_size = File::GetFileSize(file);
        unsigned int needed = patched && patch[0] >= file_size ? patch[0] : file_size;
        allocated = ((needed + 0x7FF) & ~0x7FFu) + prefix;
        block = static_cast<uint8_t*>(AllocateFileMemory(allocated));
        std::memcpy(block, name, std::strlen(name) + 1);
        File::Read(block + prefix, 1, static_cast<int>(file_size), file);
        File::Close(file);
    }

    if (patched) {
        // Through uintptr_t: on the path above `block` can be null, as in the original.
        auto data = reinterpret_cast<uintptr_t>(block) + prefix;
        if (allocated > file_size)
            std::memset(reinterpret_cast<void*>(data + file_size), 0, allocated - file_size - prefix);
        const auto* p = reinterpret_cast<const uint8_t*>(patch + 2);
        for (uint32_t i = 0; i < patch[1]; ++i) {
            uint32_t offset = reinterpret_cast<const uint32_t*>(p)[0];
            uint32_t count = reinterpret_cast<const uint32_t*>(p)[1];
            p += 8;
            std::memcpy(reinterpret_cast<void*>(data + offset), p, count);
            p += count;
        }
        Mem::Free(patch);
    }

    // Repack: every file stored uncompressed, its data 16-byte aligned after its header and name, so files
    // can be used in place. First the new size (plus slack), then the copy.
    auto* old_header = reinterpret_cast<PreHeader*>(block + ((std::strlen(reinterpret_cast<char*>(block)) + 0x10) &
                                                             ~size_t(0xF)));
    int count = old_header->numFiles;
    uintptr_t new_size = prefix + sizeof(PreHeader);
    {
        auto* entry = reinterpret_cast<PreEntry*>(old_header + 1);
        for (int n = count; n != 0; --n) {
            new_size = ((entry->nameSize + new_size + 0x1F) & ~uintptr_t(0xF)) + ((entry->size + 3) & ~3u);
            entry = NextRawEntry(entry);
        }
    }
    auto* repacked = static_cast<uint8_t*>(AllocateFileMemory(static_cast<unsigned int>(new_size + 0xC00)));
    for (unsigned int i = 0; static_cast<int>(i) < static_cast<int>(prefix); ++i)
        repacked[i] = block[i];

    auto* new_header = reinterpret_cast<PreHeader*>(repacked + prefix);
    new_header->numFiles = old_header->numFiles;
    new_header->version = old_header->version;
    auto* from = reinterpret_cast<PreEntry*>(old_header + 1);
    auto* to = reinterpret_cast<PreEntry*>(new_header + 1);
    for (int n = count; n != 0; --n) {
        to->size = from->size;
        to->checksum = from->checksum;
        to->compressedSize = 0;
        to->useCount = 0;
        to->nameSize = from->nameSize;
        std::memcpy(to + 1, from + 1, from->nameSize);

        PreEntry* next_from = NextRawEntry(from);
        const uint8_t* source = reinterpret_cast<const uint8_t*>(from + 1) + from->nameSize;
        auto* target = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(to) + to->nameSize + 0x1F) &
                                                  ~uintptr_t(0xF));
        if (from->compressedSize) {
            uint8_t* end = Lzss::Decode(source, target, from->compressedSize);
            for (uint32_t pad = static_cast<uint32_t>(to->size); pad & 3; ++pad)
                *end++ = 0;
        } else {
            std::memcpy(target, source, ((static_cast<uint32_t>(from->size) + 3) >> 2) * 4);
        }
        to = NextEntry(to);
        from = next_from;
    }
    new_header->totalSize = static_cast<int>(reinterpret_cast<uint8_t*>(to) - reinterpret_cast<uint8_t*>(new_header));
    FreeFileMemory(block);
    // With all 100 slots taken the original writes to slot -1 (the last loaded file's reference count).
    (&PreFiles()[0])[slot] = reinterpret_cast<char*>(repacked);
}

// --- script functions -----------------------------------------------------------------------------------

namespace {

constexpr uint32_t kHeapParam = 0x42D6DD1E;    // LoadPipPre ... heap = <name>
constexpr uint32_t kHeapTopDown = 0x477FC6DE;  // the two heap names it accepts
constexpr uint32_t kHeapBottomUp = 0xC80BF12D;

const char* PreName(Script::CStruct* params)
{
    const char* name = nullptr;
    Script::GetString(params, 0, &name, true);
    return name;
}

} // namespace

// 0x005B7A60: LoadPipPre <name> [heap = ...]
bool __cdecl CFunc_LoadPipPre(Script::CStruct* params, Script::CScript* /*script*/)
{
    const char* name = PreName(params);
    uint32_t heap = 0;
    Script::GetChecksum(params, kHeapParam, &heap, false);
    if (heap == kHeapTopDown)
        Mem::PushContext(Mem::TopDownHeap());
    else if (heap == kHeapBottomUp)
        Mem::PushContext(Mem::BottomUpHeap());
    else
        heap = 0;
    LoadPre(name);
    if (heap)
        Mem::PopContext();
    return true;
}

// 0x005B7410: UnLoadPipPre <name>
bool __cdecl CFunc_UnLoadPipPre(Script::CStruct* params, Script::CScript* /*script*/)
{
    return UnloadPre(PreName(params));
}

// 0x005B7430: DumpPipPreStatus [ShowPreedFiles] [ShowUnPreedFiles] [ShowOnlyOpenFiles] - debug output
bool __cdecl CFunc_DumpPipPreStatus(Script::CStruct* params, Script::CScript* /*script*/)
{
    bool show_files = Script::ContainsFlag(params, "ShowPreedFiles");
    bool show_loose_files = Script::ContainsFlag(params, "ShowUnPreedFiles");
    bool only_open = Script::ContainsFlag(params, "ShowOnlyOpenFiles");
    Dbg::Printf("Currently loaded pre files:\n");
    for (int i = 0; i < kMaxPreFiles; ++i) {
        char* pre = PreFiles()[i];
        if (!pre)
            continue;
        Dbg::Printf("### %s ###\n", pre);
        if (!show_files)
            continue;
        PreHeader* header = HeaderOf(pre);
        PreEntry* entry = FirstEntry(header);
        for (int n = header->numFiles; n > 0; --n, entry = NextEntry(entry)) {
            if (!only_open || entry->useCount != 0) {
                Dbg::Printf("Usage:%d  File: %s\n", entry->useCount, reinterpret_cast<const char*>(entry + 1));
                for (volatile int wait = 0; wait < 100000; ++wait) {
                    // the original pauses after each line (presumably so the debug output keeps up)
                }
            }
        }
    }
    if (show_loose_files) {
        Dbg::Printf("Currently loaded un-preed files:\n");
        for (int i = 0; i < kMaxLoadedFiles; ++i) {
            const LoadedFile& file = LoadedFiles()[i];
            if (file.data)
                Dbg::Printf("Usage:%d  File: %s\n", file.refCount, Script::FindChecksumNameOrUnknown(file.checksum));
        }
    }
    return true;
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

// 0x005B70F0
FileTables* FileTables::Construct()
{
    for (int i = 0; i < kMaxPreFiles; ++i)
        PreFiles()[i] = nullptr;
    for (int i = 0; i < kMaxLoadedFiles; ++i)
        LoadedFiles()[i] = {};
    return this;
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

REPLACE_SOURCE_ONLY(0x005B70F0, &Pip::FileTables::Construct); // (not a Ghidra function; a static initialiser's)
REPLACE(0x005B7130, Pip::UnloadPre);
REPLACE_SHADOW(0x005B7180, Pip::FindPreEntry);
REPLACE(0x005B7200, static_cast<void (*)(uint32_t)>(&Pip::Unload));
REPLACE(0x005B7270, static_cast<void (*)(const char*)>(&Pip::Unload));
REPLACE_SHADOW(0x005B7290, static_cast<int (*)(uint32_t)>(&Pip::GetFileSize));
REPLACE_SHADOW(0x005B72E0, static_cast<int (*)(const char*)>(&Pip::GetFileSize));
REPLACE_SHADOW(0x005B7300, Pip::GetNextLoadedPre);
REPLACE(0x005B7C00, Pip::Load);
REPLACE_SHADOW(0x005B7370, Pip::PreFileIsInUse);
REPLACE(0x005B75A0, Pip::LoadPre);
REPLACE(0x005B7B00, Pip::LoadFileInto);
REPLACE(0x005B7A60, Pip::CFunc_LoadPipPre);
REPLACE(0x005B7410, Pip::CFunc_UnLoadPipPre);
REPLACE(0x005B7430, Pip::CFunc_DumpPipPreStatus);

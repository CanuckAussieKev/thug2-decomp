#include "game/file/archive.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/file/async.h"
#include "game/file/file_load.h"
#include "game/lst/key_map.h"
#include "game/pcm/music_api.h"
#include "game/script/params.h"
#include "game/script/param_struct.h"
#include "game/sys/game_config.h"
#include "game/sys/stopwatch.h"
#include "game/gfx/loading_console.h"

namespace Pre {

static_assert(sizeof(ArchiveFile::FileInfo) == 0x14, "allocated as 0x14 bytes by the original");
static_assert(offsetof(ArchiveFile, m_openCount) == 0x10 && offsetof(ArchiveFile, m_bottomUpHeap) == 0x14 &&
                  sizeof(ArchiveFile) == 0x18,
              "layout must match the original");

namespace {

constexpr int kRingSize = 4096;
constexpr int kMaxMatch = 18;

uint8_t g_ring[kRingSize];
ArchiveManager* g_managerSlot;
ArchiveManager* g_instanceSlot;
int g_instanceRefs;

uint8_t* Ring() { return g_ring; }

[[noreturn]] void Hang()
{
    // The original loops forever here (its assertion in release builds).
    for (;;)
        Sleep(INFINITE);
}

MemSys::Arena* FileHeap(bool bottom_up) { return bottom_up ? MemSys::BottomUpHeap() : MemSys::TopDownHeap(); }

// An entry header in the archive block; the name (name_size bytes) and then the data follow it.
struct TableRow {
    int size;
    int compressed_size;
    int16_t name_size; // read as a signed 16-bit value
    uint16_t use_count;
    uint32_t checksum;
};

void CopyBytes(void* out, const void* in, unsigned int count) { std::memcpy(out, in, count); }

} // namespace

// 0x005B7DF0
void Decode(const uint8_t* in, uint8_t* out, int size)
{
    if (size < 0 || size >= 0x2000000)
        Hang();
    uint8_t* ring = Ring();
    std::memset(ring, ' ', kRingSize - kMaxMatch);
    int r = kRingSize - kMaxMatch;
    unsigned int flags = 0;
    for (;;) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            if (size == 0)
                return;
            flags = *in++ | 0xFF00u;
            --size;
        }
        if (flags & 1) {
            // Literal byte.
            if (size == 0)
                return;
            uint8_t c = *in++;
            --size;
            *out++ = c;
            ring[r] = c;
            r = (r + 1) & (kRingSize - 1);
        } else {
            // Reference: 12-bit position, 4-bit length - 3. (Like the original, only checks for one byte left.)
            if (size == 0)
                return;
            int position = in[0] | ((in[1] & 0xF0) << 4);
            int last = (in[1] & 0x0F) + 2;
            in += 2;
            size -= 2;
            for (int k = 0; k <= last; ++k) {
                uint8_t c = ring[(position + k) & (kRingSize - 1)];
                *out++ = c;
                ring[r] = c;
                r = (r + 1) & (kRingSize - 1);
            }
        }
    }
}

// 0x005B8820
ArchiveFile::ArchiveFile(uint8_t* block, bool bottom_up)
{
    m_bottomUpHeap = bottom_up;
    void* memory = MemSys::Alloc(sizeof(ListKit::KeyMap));
    m_files = memory ? new (memory) ListKit::KeyMap(4) : nullptr;
    m_block = block;
    m_entryCount = *reinterpret_cast<int*>(block + 8);

    auto* entry = reinterpret_cast<TableRow*>(block + 12);
    for (int i = 0; i < m_entryCount; ++i) {
        int stored = entry->compressed_size ? entry->compressed_size : entry->size;
        auto* info = static_cast<FileInfo*>(MemSys::Carve(sizeof(FileInfo), 1, FileHeap(m_bottomUpHeap)));
        const char* name = reinterpret_cast<const char*>(entry + 1);
        if (m_files->FetchEntry(Checksums::HashFromText(name))) {
            MemSys::Dealloc(info); // the same name twice: the first one wins
        } else {
            uint32_t checksum = Checksums::HashFromText(name);
            m_files->StoreEntry(checksum, info);
            info->data = reinterpret_cast<uint8_t*>(entry + 1) + entry->name_size;
            info->unpacked = nullptr;
            info->position = 0;
            info->compressedSize = entry->compressed_size;
            info->size = entry->size;
        }
        entry = reinterpret_cast<TableRow*>(reinterpret_cast<uint8_t*>(entry) + ((stored + 3) & ~3) +
                                         entry->name_size + sizeof(TableRow));
    }
    m_activeFile = nullptr;
    m_openCount = 0;
}

// 0x005B9410 (the scalar deleting destructor: frees `this` too when bit 0 of its argument is set)
ArchiveFile::~ArchiveFile()
{
    MemSys::Dealloc(m_block);
    m_files->RunHook([](void* info, void*) { MemSys::Dealloc(info); }, nullptr);
    m_files->EmptyTable();
    if (ListKit::KeyMap* files = m_files) {
        files->~KeyMap();
        MemSys::Dealloc(files);
    }
}

// 0x005B8230
uint8_t* ArchiveFile::ReadEntryFromArchive(const char* name, int* size, uint8_t* out)
{
    auto* info = static_cast<FileInfo*>(m_files->FetchEntry(Checksums::HashFromText(name)));
    if (!info)
        return nullptr;
    *size = info->size;
    if (!out)
        out = static_cast<uint8_t*>(MemSys::AllocBlock(static_cast<unsigned int>(info->size)));
    if (info->unpacked)
        CopyBytes(out, info->unpacked, static_cast<unsigned int>(info->size));
    else if (info->compressedSize == 0)
        CopyBytes(out, info->data, static_cast<unsigned int>(info->size));
    else
        Decode(info->data, out, info->compressedSize);
    return out;
}

// 0x005B8310
uint8_t* ArchiveFile::FindEntryByKey(FileInfo* handle)
{
    m_files->m_iterBucket = -1;
    m_files->m_iterItem = nullptr;
    while (m_files->StepIterator() && m_files->m_iterItem) {
        auto* info = static_cast<FileInfo*>(m_files->m_iterItem->value);
        if (!info)
            break;
        if (info->data && info == handle) {
            m_activeFile = info;
            return info->compressedSize ? info->unpacked : info->data;
        }
    }
    return nullptr;
}

// 0x005B8A00
ArchiveFile::FileInfo* ArchiveFile::FindEntryInArchive(const char* name)
{
    auto* info = static_cast<FileInfo*>(m_files->FetchEntry(Checksums::HashFromText(name)));
    if (!info)
        return nullptr;
    FindEntryByKey(info);
    if (!m_activeFile->unpacked && m_activeFile->compressedSize != 0) {
        MemSys::PushMemProfile(name);
        m_activeFile->unpacked = static_cast<uint8_t*>(
            MemSys::Carve(static_cast<unsigned int>(m_activeFile->size), 1, FileHeap(m_bottomUpHeap)));
        MemSys::PopMemProfile();
        Decode(m_activeFile->data, m_activeFile->unpacked, m_activeFile->compressedSize);
    }
    return info;
}

// 0x005B7F00
int ArchiveFile::TakeIn(void* out, unsigned int count)
{
    FileInfo* file = m_activeFile;
    unsigned int left = static_cast<unsigned int>(file->size - file->position);
    unsigned int n = count <= left ? count : left;
    const uint8_t* source = file->compressedSize ? file->unpacked : file->data;
    CopyBytes(out, source + file->position, n);
    file->position += static_cast<int>(n);
    return static_cast<int>(n);
}

// 0x005B7F60
int ArchiveFile::SeekTo(int offset, int origin)
{
    FileInfo* file = m_activeFile;
    int previous = file->position;
    if (origin == 0)
        file->position = offset;
    else if (origin == 1)
        file->position += offset;
    else if (origin == 2)
        file->position = file->size - offset;
    else
        return -1;
    if (file->position >= 0 && file->position <= file->size)
        return 0;
    file->position = previous;
    return -1;
}

// --- ArchiveManager ---------------------------------------------------------------------------------------------

static_assert(offsetof(ArchiveManager, m_pres) == 0x10 && offsetof(ArchiveManager, m_holdArchiveLoads) == 0x14 &&
                  offsetof(ArchiveManager, m_pending) == 0x18 && offsetof(ArchiveManager, m_numPending) == 0x238 &&
                  sizeof(ArchiveManager) == 0x23C,
              "layout must match the original");

namespace {

ArchiveManager*& ManagerSlot() { return g_managerSlot; } // set by the constructor
ArchiveManager*& InstanceSlot() { return g_instanceSlot; } // the one CreateManager made
int& InstanceRefs() { return g_instanceRefs; }
bool& Handled() { return FileLoad::g_handledByArchive; } // see FileLoad:: - "this call was ours"
int Language() { return GameConfig::g_language; }

// Names inside archives use backslashes. The original copies into a 128-byte buffer with no limit; this
// stops at the buffer's size.
void NormaliseName(char (&out)[128], const char* name)
{
    size_t i = 0;
    for (; name[i] && i < sizeof(out) - 1; ++i)
        out[i] = name[i] == '/' ? '\\' : name[i];
    out[i] = '\0';
}

// Calls `visit` for each loaded archive, in table order, until it returns something non-null (returned), or
// the table runs out. Uses the table's own iterator, like the original.
template <typename Visit>
auto ForEachPre(ListKit::KeyMap* pres, Visit visit) -> decltype(visit(static_cast<ArchiveFile*>(nullptr)))
{
    pres->m_iterBucket = -1;
    pres->m_iterItem = nullptr;
    while (pres->StepIterator() && pres->m_iterItem && pres->m_iterItem->value) {
        if (auto result = visit(static_cast<ArchiveFile*>(pres->m_iterItem->value)))
            return result;
    }
    return {};
}

} // namespace

ArchiveManager* Hub() { return ManagerSlot(); }
ArchiveManager* Sole() { return InstanceSlot(); }

// 0x005B8D20
ArchiveManager::ArchiveManager()
{
    void* memory = MemSys::Alloc(sizeof(ListKit::KeyMap));
    m_pres = memory ? new (memory) ListKit::KeyMap(4) : nullptr;
    ManagerSlot() = this;
    m_activeHandle = nullptr;
    m_activeData = nullptr;
    m_missingHandle = nullptr;
    m_numPending = 0;
}

// 0x005B8E90 (returns the instance in EAX: the caller keeps it)
ArchiveManager* CreateManager(bool create)
{
    if (!InstanceSlot() && create) {
        void* memory = MemSys::AllocZeroed(sizeof(ArchiveManager), nullptr, 1);
        InstanceSlot() = memory ? new (memory) ArchiveManager() : nullptr;
    }
    ++InstanceRefs();
    return InstanceSlot();
}

// 0x005B9510
void DestroyManager()
{
    if (InstanceRefs() > 1) {
        --InstanceRefs();
        return;
    }
    if (ArchiveManager* manager = InstanceSlot()) {
        if (ListKit::KeyMap* pres = manager->m_pres) {
            pres->~KeyMap();
            MemSys::Dealloc(pres);
        }
        MemSys::Dealloc(manager);
    }
    InstanceSlot() = nullptr;
    InstanceRefs() = 0;
}

namespace {

// The script global block_pre_loading (Scripts::FetchIntValue by name - not decompiled yet).
bool PreLoadingBlocked()
{
    return Scripts::FetchIntValue("block_pre_loading") != 0;
}
// Always false in this build; only does debugging work when the debug flag is set.
bool SkipSynchronousLoad() { return AudioStream::DiscModeActive(); }
int TimeMilliseconds() { return static_cast<int>(FrameClock::GetTime()); }
bool DebugChecks() { return GameConfig::g_fromCD; }

MemSys::Arena* LoadHeap(bool bottom_up) { return bottom_up ? MemSys::BottomUpHeap() : MemSys::TopDownHeap(); }

// A localised archive patch (.ppf/.ppd): the patched archive's size, a record count, then the records -
// {offset, length, `length` bytes to write at that offset}.
void ApplyPatch(uint8_t* archive, const uint32_t* patch)
{
    const auto* p = reinterpret_cast<const uint8_t*>(patch + 2);
    for (uint32_t i = 0; i < patch[1]; ++i) {
        uint32_t offset = reinterpret_cast<const uint32_t*>(p)[0];
        uint32_t length = reinterpret_cast<const uint32_t*>(p)[1];
        p += 8;
        std::memcpy(archive + offset, p, length);
        p += length;
    }
}

} // namespace

// 0x005B8E70 (the async loader's callback for archives; not marked as a function in Ghidra)
void __cdecl OnAsyncPreLoaded(FileLoad::AsyncFile* file, int event, int size, void* manager, void* data)
{
    if (event == 3) // finished
        static_cast<ArchiveManager*>(manager)->FinishLoad(reinterpret_cast<int>(file), static_cast<uint8_t*>(data), size);
}

// 0x005B8EE0
void ArchiveManager::LoadPre(const char* name, bool async, bool report_missing, bool bottom_up)
{
    m_holdArchiveLoads = PreLoadingBlocked();
    if (m_holdArchiveLoads)
        return;
    if (!FileLoad::AsyncLoadingAvailable())
        async = false;
    if (!async && SkipSynchronousLoad())
        return;

    char path[256];
    std::snprintf(path, sizeof(path), "pre\\%s", name);
    if (std::strrchr(name, '.') && std::strlen(path) > 4)
        path[std::strlen(path) - 1] = 'x'; // .pre -> .prx
    int start = TimeMilliseconds();

    if (async) {
        if (FileLoad::AsyncFile* file = FileLoad::AsyncOpen(path)) {
            PendingLoad& pending = m_pending[m_numPending];
            // The original copies with no limit; the field holds 63 characters.
            strncpy_s(pending.name, name, _TRUNCATE);
            pending.handle = reinterpret_cast<int>(file);
            ++m_numPending;
            int size = FileLoad::AsyncGetFileSize(file);
            Gfx2D::LoadingConsole::NoteFile("read (async)", path, size); // (not original: the loading console)
            MemSys::PushMemProfile(path);
            void* data = MemSys::Carve(static_cast<unsigned int>(size), 1, LoadHeap(bottom_up));
            MemSys::PopMemProfile();
            FileLoad::AsyncSetCallback(file, OnAsyncPreLoaded, this, data);
            FileLoad::StartBackgroundRead(file, data, size);
            return;
        }
        // Couldn't start an async load: load it now instead.
    }

    uint8_t* data;
    int size = FileLoad::CachedFileSize(path);
    if (size != 0) {
        MemSys::PushMemProfile(path);
        data = static_cast<uint8_t*>(MemSys::Carve(static_cast<unsigned int>(size), 1, LoadHeap(bottom_up)));
        MemSys::PopMemProfile();
        if (!FileLoad::LoadCachedFile(path, data))
            DebugOut::Printf("pre file %s failed to load quickly.\n", path);
    } else {
        // French and German: a complete localised archive (.prf/.prd) if there is one, else a patch for the
        // English archive (.ppf/.ppd).
        size_t length = std::strlen(path);
        uint32_t* patch = nullptr;
        bool patched = false;
        char letter = Language() == 3 ? 'f' : Language() == 5 ? 'd' : '\0';
        if (letter) {
            path[length - 1] = letter;
            if (!FileLoad::IsPresent(path)) {
                path[length - 2] = 'p';
                if (FileLoad::IsPresent(path)) {
                    FileLoad::Hdl file = FileLoad::Unseal(path, "rb");
                    uint32_t patch_size = FileLoad::GetFileSize(file);
                    patch = static_cast<uint32_t*>(MemSys::Alloc(patch_size));
                    FileLoad::TakeIn(patch, 1, static_cast<int>(patch_size), file);
                    FileLoad::Shut(file);
                    patched = true;
                }
                path[length - 2] = 'r'; // back to .pre (opened as .prx)
                path[length - 1] = 'e';
            }
        }

        FileLoad::Hdl file = FileLoad::Unseal(path, "rb");
        if (!file && (report_missing || DebugChecks())) {
            DebugOut::Printf("couldn't open %s.  Make sure the, if this is a level PRE file, that the name in "
                        "levels.q matches the file on disk\n",
                        path);
            return;
        }
        uint32_t file_size = FileLoad::GetFileSize(file);
        MemSys::PushMemProfile(path);
        size = static_cast<int>(file_size);
        uint32_t allocated = file_size;
        if (patched && file_size <= patch[0])
            allocated = patch[0]; // room for the patched archive
        data = static_cast<uint8_t*>(MemSys::Carve(allocated, 1, LoadHeap(bottom_up)));
        MemSys::PopMemProfile();
        FileLoad::TakeIn(data, 1, static_cast<int>(file_size), file);
        if (DebugChecks() && file_size != *reinterpret_cast<uint32_t*>(data))
            DebugOut::Printf("%s has incorrect file size\n", path);
        FileLoad::Shut(file);
        if (patched) {
            if (allocated > file_size)
                std::memset(data + file_size, 0, allocated - file_size);
            ApplyPatch(data, patch);
            MemSys::Dealloc(patch);
        }
    }

    DebugOut::Printf("load time for file %s size %d is %d ms\n", name, size, TimeMilliseconds() - start);
    void* memory = MemSys::AllocZeroed(sizeof(ArchiveFile), LoadHeap(bottom_up), 1);
    ArchiveFile* pre = memory ? new (memory) ArchiveFile(data, bottom_up) : nullptr;
    m_pres->StoreEntry(Checksums::HashFromText(name), pre);
}

// 0x005B8750
uint8_t* ArchiveManager::IngestFile(const char* name, int* size, uint8_t* out)
{
    char path[128];
    NormaliseName(path, name);
    return ForEachPre(m_pres, [&](ArchiveFile* pre) { return pre->ReadEntryFromArchive(path, size, out); });
}

// 0x005B8390
bool ArchiveManager::IsFilePresent(const char* name)
{
    char path[128];
    NormaliseName(path, name);
    return ForEachPre(m_pres, [&](ArchiveFile* pre) {
        return pre->m_files->FetchEntry(Checksums::HashFromText(path)) != nullptr;
    });
}

// 0x005B8AD0
ArchiveFile::FileInfo* ArchiveManager::OpenFile(const char* name)
{
    char path[128];
    NormaliseName(path, name);
    // "...scripts.pre" -> "...scripts.pr<language letter>"
    size_t length = std::strlen(path);
    if (length > 11 && std::strcmp(path + length - 11, "scripts.pre") == 0) {
        static const char kLetters[] = {'f', 's', 'd', 'i'}; // languages 3 to 6
        int language = Language();
        path[length - 1] = language >= 3 && language <= 6 ? kLetters[language - 3] : 'e';
    }
    return ForEachPre(m_pres, [&](ArchiveFile* pre) -> ArchiveFile::FileInfo* {
        ArchiveFile::FileInfo* handle = pre->FindEntryInArchive(path);
        if (!handle)
            return nullptr;
        m_activePre = pre;
        m_activeHandle = handle;
        m_activeData = pre->FindEntryByKey(handle);
        return handle;
    });
}

// 0x005B84A0
uint8_t* ArchiveManager::FindEntryInArchive(ArchiveFile::FileInfo* handle)
{
    if (m_missingHandle == handle)
        return nullptr;
    if (m_activeHandle == handle)
        return m_activeData;
    uint8_t* data = ForEachPre(m_pres, [&](ArchiveFile* pre) -> uint8_t* {
        uint8_t* found = pre->FindEntryByKey(handle);
        if (found) {
            m_activePre = pre;
            m_activeHandle = handle;
            m_activeData = found;
        }
        return found;
    });
    if (!data)
        m_missingHandle = handle;
    return data;
}

// 0x005B8540
bool ArchiveManager::IsLoaded(const char* name)
{
    return m_pres->FetchEntry(Checksums::HashFromText(name)) != nullptr;
}

// 0x005B7FD0
bool ArchiveManager::IsLoadFinished(const char* name)
{
    for (int i = 0; i < m_numPending; ++i) {
        if (std::strcmp(m_pending[i].name, name) == 0)
            return false;
    }
    return true;
}

// 0x005B8DB0
void ArchiveManager::FinishLoad(int handle, uint8_t* data, int /*size*/)
{
    int i = 0;
    while (i < m_numPending && m_pending[i].handle != handle)
        ++i;
    if (i == m_numPending)
        return;
    void* memory = MemSys::AllocZeroed(sizeof(ArchiveFile), MemSys::TopDownHeap(), 1);
    ArchiveFile* pre = memory ? new (memory) ArchiveFile(data, false) : nullptr;
    m_pres->StoreEntry(Checksums::HashFromText(m_pending[i].name), pre);
    --m_numPending;
    m_pending[i] = m_pending[m_numPending]; // the last pending load takes this one's place
}

// 0x005B9570
void ArchiveManager::UnloadPre(const char* name, bool only_if_loaded)
{
    if (m_holdArchiveLoads)
        return;
    auto* pre = static_cast<ArchiveFile*>(m_pres->FetchEntry(Checksums::HashFromText(name)));
    if (!pre && only_if_loaded)
        return;
    m_pres->EraseEntry(Checksums::HashFromText(name));
    if (pre) {
        pre->~ArchiveFile();
        MemSys::Dealloc(pre);
    }
}

// --- the archive side of FileLoad:: -----------------------------------------------------------------------

// 0x005B85A0
bool IsPresent(const char* name)
{
    Handled() = Hub()->IsFilePresent(name);
    return Handled();
}

// 0x005B8C70
ArchiveFile::FileInfo* Unseal(const char* name, const char* mode, bool count_open)
{
    ArchiveFile::FileInfo* handle = Hub()->OpenFile(name);
    if (!handle) {
        Handled() = false;
        return nullptr;
    }
    if (count_open)
        ++Hub()->m_activePre->m_openCount;
    // Archives are read-only: any mode other than r/b gives the file up (and leaves it to FileLoad::).
    for (const char* c = mode; *c; ++c) {
        if (*c != 'r' && *c != 'b') {
            ArchiveFile* pre = Hub()->m_activePre;
            if (pre->m_activeFile->unpacked)
                MemSys::Dealloc(pre->m_activeFile->unpacked);
            pre->m_activeFile->unpacked = nullptr;
            if (count_open)
                --pre->m_openCount;
            Handled() = false;
            return nullptr;
        }
    }
    Hub()->m_activePre->m_activeFile->position = 0;
    Handled() = true;
    return handle;
}

// 0x005B85D0
int Shut(ArchiveFile::FileInfo* file, bool count_open)
{
    if (!Hub()->FindEntryInArchive(file)) {
        Handled() = false;
        return 0;
    }
    ArchiveFile* pre = Hub()->m_activePre;
    if (pre->m_activeFile->unpacked)
        MemSys::Dealloc(pre->m_activeFile->unpacked);
    pre->m_activeFile->unpacked = nullptr;
    if (count_open)
        --pre->m_openCount;
    Handled() = true;
    return 0;
}

// 0x005B8630
int TakeIn(void* buffer, int size, int count, ArchiveFile::FileInfo* file)
{
    if (!Hub()->FindEntryInArchive(file)) {
        Handled() = false;
        return 0;
    }
    Handled() = true;
    return Hub()->m_activePre->TakeIn(buffer, static_cast<unsigned int>(size * count));
}

// 0x005B8680
int EmitOut(const void* /*buffer*/, int /*size*/, int /*count*/, ArchiveFile::FileInfo* file)
{
    Hub()->FindEntryInArchive(file);
    Handled() = false;
    return 0;
}

// 0x005B86A0
int SeekTo(ArchiveFile::FileInfo* file, int offset, int origin)
{
    if (!Hub()->FindEntryInArchive(file)) {
        Handled() = false;
        return 0;
    }
    Handled() = true;
    return Hub()->m_activePre->SeekTo(offset, origin);
}

// 0x005B86E0
int GetFileSize(ArchiveFile::FileInfo* file)
{
    if (!Hub()->FindEntryInArchive(file)) {
        Handled() = false;
        return 0;
    }
    Handled() = true;
    ArchiveFile::FileInfo* active = Hub()->m_activePre->m_activeFile;
    return active ? active->size : 0;
}

// --- script functions -----------------------------------------------------------------------------------

namespace {

// Flag names in the scripts' LoadPreFile calls (checksums; the names themselves aren't in the exe).
constexpr uint32_t kFlagBottomUpHeap = 0xA44C770F;
constexpr uint32_t kFlagReportMissing = 0x3D92465E;
constexpr uint32_t kFlagAsync = 0x90E07C79;

const char* ArchiveName(Scripts::CStruct* params)
{
    const char* name = nullptr;
    Scripts::FetchText(params, 0, &name, true);
    return name;
}

} // namespace

// 0x005B9470: LoadPreFile <name> [flags]
bool __cdecl CFunc_LoadPreFile(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    const char* name = ArchiveName(params);
    ArchiveManager* manager = Sole();
    bool bottom_up = Scripts::HasBareFlag(params, kFlagBottomUpHeap);
    bool report_missing = Scripts::HasBareFlag(params, kFlagReportMissing);
    bool async = Scripts::HasBareFlag(params, kFlagAsync);
    MemSys::EnterHeapScope(bottom_up ? MemSys::BottomUpHeap() : MemSys::TopDownHeap());
    manager->LoadPre(name, async, report_missing, bottom_up);
    MemSys::LeaveHeapScope();
    return true;
}

// 0x005B9600: UnloadPreFile <name> [dont_assert]
bool __cdecl CFunc_UnloadPreFile(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    ArchiveManager* manager = Sole();
    const char* name = ArchiveName(params);
    manager->UnloadPre(name, Scripts::HasBareFlag(params, "dont_assert"));
    return true;
}

// 0x005B8160: IsLoadPreFinished <name>
bool __cdecl CFunc_IsLoadPreFinished(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    ArchiveManager* manager = Sole();
    return manager->IsLoadFinished(ArchiveName(params));
}

// 0x005B8190: AllLoadPreFinished
bool __cdecl CFunc_AllLoadPreFinished(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    return Sole()->m_numPending == 0;
}

// 0x005B81B0: WaitLoadPre <name>
bool __cdecl CFunc_WaitLoadPre(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    ArchiveManager* manager = Sole();
    const char* name = ArchiveName(params);
    while (!manager->IsLoadFinished(name))
        FileLoad::AsyncWaitForIO();
    return true;
}

// 0x005B8200: WaitAllLoadPre
bool __cdecl CFunc_WaitAllLoadPre(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    ArchiveManager* manager = Sole();
    while (manager->m_numPending != 0)
        FileLoad::AsyncWaitForIO();
    return true;
}

// 0x005B8720: InPreFile <name> - whether the archive is loaded
bool __cdecl CFunc_InPreFile(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    return Sole()->IsLoaded(ArchiveName(params));
}

} // namespace Pre

namespace {

struct PreFileAdapter {
    Pre::ArchiveFile* MakeNew(uint8_t* block, bool bottom_up) { return new (this) Pre::ArchiveFile(block, bottom_up); }
    Pre::ArchiveFile* Teardown(unsigned int flags)
    {
        auto* self = reinterpret_cast<Pre::ArchiveFile*>(this);
        self->~ArchiveFile();
        if (flags & 1)
            MemSys::Dealloc(self);
        return self;
    }
};

struct PreMgrAdapter {
    Pre::ArchiveManager* MakeNew() { return new (this) Pre::ArchiveManager(); }
};

} // namespace

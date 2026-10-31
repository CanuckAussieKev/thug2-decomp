#include "game/file/pre.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/file/async.h"
#include "game/file/file.h"
#include "game/lst/hashtable.h"
#include "game/pcm/music_api.h"
#include "game/script/params.h"
#include "game/script/struct.h"
#include "game/sys/game_config.h"
#include "game/sys/timer.h"
#include "game/gfx/loading_console.h"

namespace Pre {

static_assert(sizeof(PreFile::FileInfo) == 0x14, "allocated as 0x14 bytes by the original");
static_assert(offsetof(PreFile, m_openCount) == 0x10 && offsetof(PreFile, m_bottomUpHeap) == 0x14 &&
                  sizeof(PreFile) == 0x18,
              "layout must match the original");

namespace {

constexpr int kRingSize = 4096;
constexpr int kMaxMatch = 18;

uint8_t g_ring[kRingSize];
PreMgr* g_managerSlot;
PreMgr* g_instanceSlot;
int g_instanceRefs;

uint8_t* Ring() { return g_ring; }

[[noreturn]] void Hang()
{
    // The original loops forever here (its assertion in release builds).
    for (;;)
        Sleep(INFINITE);
}

Mem::Heap* FileHeap(bool bottom_up) { return bottom_up ? Mem::BottomUpHeap() : Mem::TopDownHeap(); }

// An entry header in the archive block; the name (name_size bytes) and then the data follow it.
struct Entry {
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
PreFile::PreFile(uint8_t* block, bool bottom_up)
{
    m_bottomUpHeap = bottom_up;
    void* memory = Mem::New(sizeof(Lst::HashTable));
    m_files = memory ? new (memory) Lst::HashTable(4) : nullptr;
    m_block = block;
    m_numFiles = *reinterpret_cast<int*>(block + 8);

    auto* entry = reinterpret_cast<Entry*>(block + 12);
    for (int i = 0; i < m_numFiles; ++i) {
        int stored = entry->compressed_size ? entry->compressed_size : entry->size;
        auto* info = static_cast<FileInfo*>(Mem::Allocate(sizeof(FileInfo), 1, FileHeap(m_bottomUpHeap)));
        const char* name = reinterpret_cast<const char*>(entry + 1);
        if (m_files->GetItem(Crc::GenerateCRCFromString(name))) {
            Mem::Free(info); // the same name twice: the first one wins
        } else {
            uint32_t checksum = Crc::GenerateCRCFromString(name);
            m_files->PutItem(checksum, info);
            info->data = reinterpret_cast<uint8_t*>(entry + 1) + entry->name_size;
            info->unpacked = nullptr;
            info->position = 0;
            info->compressedSize = entry->compressed_size;
            info->size = entry->size;
        }
        entry = reinterpret_cast<Entry*>(reinterpret_cast<uint8_t*>(entry) + ((stored + 3) & ~3) +
                                         entry->name_size + sizeof(Entry));
    }
    m_activeFile = nullptr;
    m_openCount = 0;
}

// 0x005B9410 (the scalar deleting destructor: frees `this` too when bit 0 of its argument is set)
PreFile::~PreFile()
{
    Mem::Free(m_block);
    m_files->HandleCallback([](void* info, void*) { Mem::Free(info); }, nullptr);
    m_files->FlushAllItems();
    if (Lst::HashTable* files = m_files) {
        files->~HashTable();
        Mem::Free(files);
    }
}

// 0x005B8230
uint8_t* PreFile::LoadContainedFile(const char* name, int* size, uint8_t* out)
{
    auto* info = static_cast<FileInfo*>(m_files->GetItem(Crc::GenerateCRCFromString(name)));
    if (!info)
        return nullptr;
    *size = info->size;
    if (!out)
        out = static_cast<uint8_t*>(Mem::Malloc(static_cast<unsigned int>(info->size)));
    if (info->unpacked)
        CopyBytes(out, info->unpacked, static_cast<unsigned int>(info->size));
    else if (info->compressedSize == 0)
        CopyBytes(out, info->data, static_cast<unsigned int>(info->size));
    else
        Decode(info->data, out, info->compressedSize);
    return out;
}

// 0x005B8310
uint8_t* PreFile::GetContainedFileByHandle(FileInfo* handle)
{
    m_files->m_iterBucket = -1;
    m_files->m_iterItem = nullptr;
    while (m_files->IterateNext() && m_files->m_iterItem) {
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
PreFile::FileInfo* PreFile::GetContainedFile(const char* name)
{
    auto* info = static_cast<FileInfo*>(m_files->GetItem(Crc::GenerateCRCFromString(name)));
    if (!info)
        return nullptr;
    GetContainedFileByHandle(info);
    if (!m_activeFile->unpacked && m_activeFile->compressedSize != 0) {
        Mem::PushMemProfile(name);
        m_activeFile->unpacked = static_cast<uint8_t*>(
            Mem::Allocate(static_cast<unsigned int>(m_activeFile->size), 1, FileHeap(m_bottomUpHeap)));
        Mem::PopMemProfile();
        Decode(m_activeFile->data, m_activeFile->unpacked, m_activeFile->compressedSize);
    }
    return info;
}

// 0x005B7F00
int PreFile::Read(void* out, unsigned int count)
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
int PreFile::Seek(int offset, int origin)
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

// --- PreMgr ---------------------------------------------------------------------------------------------

static_assert(offsetof(PreMgr, m_pres) == 0x10 && offsetof(PreMgr, m_blockPreLoading) == 0x14 &&
                  offsetof(PreMgr, m_pending) == 0x18 && offsetof(PreMgr, m_numPending) == 0x238 &&
                  sizeof(PreMgr) == 0x23C,
              "layout must match the original");

namespace {

PreMgr*& ManagerSlot() { return g_managerSlot; } // set by the constructor
PreMgr*& InstanceSlot() { return g_instanceSlot; } // the one CreateManager made
int& InstanceRefs() { return g_instanceRefs; }
bool& Handled() { return File::g_handledByArchive; } // see File:: - "this call was ours"
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
auto ForEachPre(Lst::HashTable* pres, Visit visit) -> decltype(visit(static_cast<PreFile*>(nullptr)))
{
    pres->m_iterBucket = -1;
    pres->m_iterItem = nullptr;
    while (pres->IterateNext() && pres->m_iterItem && pres->m_iterItem->value) {
        if (auto result = visit(static_cast<PreFile*>(pres->m_iterItem->value)))
            return result;
    }
    return {};
}

} // namespace

PreMgr* Manager() { return ManagerSlot(); }
PreMgr* Instance() { return InstanceSlot(); }

// 0x005B8D20
PreMgr::PreMgr()
{
    void* memory = Mem::New(sizeof(Lst::HashTable));
    m_pres = memory ? new (memory) Lst::HashTable(4) : nullptr;
    ManagerSlot() = this;
    m_activeHandle = nullptr;
    m_activeData = nullptr;
    m_missingHandle = nullptr;
    m_numPending = 0;
}

// 0x005B8E90 (returns the instance in EAX: the caller keeps it)
PreMgr* CreateManager(bool create)
{
    if (!InstanceSlot() && create) {
        void* memory = Mem::Calloc(sizeof(PreMgr), nullptr, 1);
        InstanceSlot() = memory ? new (memory) PreMgr() : nullptr;
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
    if (PreMgr* manager = InstanceSlot()) {
        if (Lst::HashTable* pres = manager->m_pres) {
            pres->~HashTable();
            Mem::Free(pres);
        }
        Mem::Free(manager);
    }
    InstanceSlot() = nullptr;
    InstanceRefs() = 0;
}

namespace {

// The script global block_pre_loading (Script::GetInteger by name - not decompiled yet).
bool PreLoadingBlocked()
{
    return Script::GetInteger("block_pre_loading") != 0;
}
// Always false in this build; only does debugging work when the debug flag is set.
bool SkipSynchronousLoad() { return Pcm::UsingCD(); }
int TimeMilliseconds() { return static_cast<int>(Tmr::GetTime()); }
bool DebugChecks() { return GameConfig::g_fromCD; }

Mem::Heap* LoadHeap(bool bottom_up) { return bottom_up ? Mem::BottomUpHeap() : Mem::TopDownHeap(); }

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
void __cdecl OnAsyncPreLoaded(File::AsyncFile* file, int event, int size, void* manager, void* data)
{
    if (event == 3) // finished
        static_cast<PreMgr*>(manager)->FinishLoad(reinterpret_cast<int>(file), static_cast<uint8_t*>(data), size);
}

// 0x005B8EE0
void PreMgr::LoadPre(const char* name, bool async, bool report_missing, bool bottom_up)
{
    m_blockPreLoading = PreLoadingBlocked();
    if (m_blockPreLoading)
        return;
    if (!File::AsyncLoadingAvailable())
        async = false;
    if (!async && SkipSynchronousLoad())
        return;

    char path[256];
    std::snprintf(path, sizeof(path), "pre\\%s", name);
    if (std::strrchr(name, '.') && std::strlen(path) > 4)
        path[std::strlen(path) - 1] = 'x'; // .pre -> .prx
    int start = TimeMilliseconds();

    if (async) {
        if (File::AsyncFile* file = File::AsyncOpen(path)) {
            PendingLoad& pending = m_pending[m_numPending];
            // The original copies with no limit; the field holds 63 characters.
            strncpy_s(pending.name, name, _TRUNCATE);
            pending.handle = reinterpret_cast<int>(file);
            ++m_numPending;
            int size = File::AsyncGetFileSize(file);
            Gfx::LoadingConsole::NoteFile("read (async)", path, size); // (not original: the loading console)
            Mem::PushMemProfile(path);
            void* data = Mem::Allocate(static_cast<unsigned int>(size), 1, LoadHeap(bottom_up));
            Mem::PopMemProfile();
            File::AsyncSetCallback(file, OnAsyncPreLoaded, this, data);
            File::AsyncRead(file, data, size);
            return;
        }
        // Couldn't start an async load: load it now instead.
    }

    uint8_t* data;
    int size = File::CachedFileSize(path);
    if (size != 0) {
        Mem::PushMemProfile(path);
        data = static_cast<uint8_t*>(Mem::Allocate(static_cast<unsigned int>(size), 1, LoadHeap(bottom_up)));
        Mem::PopMemProfile();
        if (!File::LoadCachedFile(path, data))
            Dbg::Printf("pre file %s failed to load quickly.\n", path);
    } else {
        // French and German: a complete localised archive (.prf/.prd) if there is one, else a patch for the
        // English archive (.ppf/.ppd).
        size_t length = std::strlen(path);
        uint32_t* patch = nullptr;
        bool patched = false;
        char letter = Language() == 3 ? 'f' : Language() == 5 ? 'd' : '\0';
        if (letter) {
            path[length - 1] = letter;
            if (!File::Exist(path)) {
                path[length - 2] = 'p';
                if (File::Exist(path)) {
                    File::Handle file = File::Open(path, "rb");
                    uint32_t patch_size = File::GetFileSize(file);
                    patch = static_cast<uint32_t*>(Mem::New(patch_size));
                    File::Read(patch, 1, static_cast<int>(patch_size), file);
                    File::Close(file);
                    patched = true;
                }
                path[length - 2] = 'r'; // back to .pre (opened as .prx)
                path[length - 1] = 'e';
            }
        }

        File::Handle file = File::Open(path, "rb");
        if (!file && (report_missing || DebugChecks())) {
            Dbg::Printf("couldn't open %s.  Make sure the, if this is a level PRE file, that the name in "
                        "levels.q matches the file on disk\n",
                        path);
            return;
        }
        uint32_t file_size = File::GetFileSize(file);
        Mem::PushMemProfile(path);
        size = static_cast<int>(file_size);
        uint32_t allocated = file_size;
        if (patched && file_size <= patch[0])
            allocated = patch[0]; // room for the patched archive
        data = static_cast<uint8_t*>(Mem::Allocate(allocated, 1, LoadHeap(bottom_up)));
        Mem::PopMemProfile();
        File::Read(data, 1, static_cast<int>(file_size), file);
        if (DebugChecks() && file_size != *reinterpret_cast<uint32_t*>(data))
            Dbg::Printf("%s has incorrect file size\n", path);
        File::Close(file);
        if (patched) {
            if (allocated > file_size)
                std::memset(data + file_size, 0, allocated - file_size);
            ApplyPatch(data, patch);
            Mem::Free(patch);
        }
    }

    Dbg::Printf("load time for file %s size %d is %d ms\n", name, size, TimeMilliseconds() - start);
    void* memory = Mem::Calloc(sizeof(PreFile), LoadHeap(bottom_up), 1);
    PreFile* pre = memory ? new (memory) PreFile(data, bottom_up) : nullptr;
    m_pres->PutItem(Crc::GenerateCRCFromString(name), pre);
}

// 0x005B8750
uint8_t* PreMgr::LoadFile(const char* name, int* size, uint8_t* out)
{
    char path[128];
    NormaliseName(path, name);
    return ForEachPre(m_pres, [&](PreFile* pre) { return pre->LoadContainedFile(path, size, out); });
}

// 0x005B8390
bool PreMgr::FileExists(const char* name)
{
    char path[128];
    NormaliseName(path, name);
    return ForEachPre(m_pres, [&](PreFile* pre) {
        return pre->m_files->GetItem(Crc::GenerateCRCFromString(path)) != nullptr;
    });
}

// 0x005B8AD0
PreFile::FileInfo* PreMgr::OpenFile(const char* name)
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
    return ForEachPre(m_pres, [&](PreFile* pre) -> PreFile::FileInfo* {
        PreFile::FileInfo* handle = pre->GetContainedFile(path);
        if (!handle)
            return nullptr;
        m_activePre = pre;
        m_activeHandle = handle;
        m_activeData = pre->GetContainedFileByHandle(handle);
        return handle;
    });
}

// 0x005B84A0
uint8_t* PreMgr::GetContainedFile(PreFile::FileInfo* handle)
{
    if (m_missingHandle == handle)
        return nullptr;
    if (m_activeHandle == handle)
        return m_activeData;
    uint8_t* data = ForEachPre(m_pres, [&](PreFile* pre) -> uint8_t* {
        uint8_t* found = pre->GetContainedFileByHandle(handle);
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
bool PreMgr::IsLoaded(const char* name)
{
    return m_pres->GetItem(Crc::GenerateCRCFromString(name)) != nullptr;
}

// 0x005B7FD0
bool PreMgr::IsLoadFinished(const char* name)
{
    for (int i = 0; i < m_numPending; ++i) {
        if (std::strcmp(m_pending[i].name, name) == 0)
            return false;
    }
    return true;
}

// 0x005B8DB0
void PreMgr::FinishLoad(int handle, uint8_t* data, int /*size*/)
{
    int i = 0;
    while (i < m_numPending && m_pending[i].handle != handle)
        ++i;
    if (i == m_numPending)
        return;
    void* memory = Mem::Calloc(sizeof(PreFile), Mem::TopDownHeap(), 1);
    PreFile* pre = memory ? new (memory) PreFile(data, false) : nullptr;
    m_pres->PutItem(Crc::GenerateCRCFromString(m_pending[i].name), pre);
    --m_numPending;
    m_pending[i] = m_pending[m_numPending]; // the last pending load takes this one's place
}

// 0x005B9570
void PreMgr::UnloadPre(const char* name, bool only_if_loaded)
{
    if (m_blockPreLoading)
        return;
    auto* pre = static_cast<PreFile*>(m_pres->GetItem(Crc::GenerateCRCFromString(name)));
    if (!pre && only_if_loaded)
        return;
    m_pres->FlushItem(Crc::GenerateCRCFromString(name));
    if (pre) {
        pre->~PreFile();
        Mem::Free(pre);
    }
}

// --- the archive side of File:: -----------------------------------------------------------------------

// 0x005B85A0
bool Exist(const char* name)
{
    Handled() = Manager()->FileExists(name);
    return Handled();
}

// 0x005B8C70
PreFile::FileInfo* Open(const char* name, const char* mode, bool count_open)
{
    PreFile::FileInfo* handle = Manager()->OpenFile(name);
    if (!handle) {
        Handled() = false;
        return nullptr;
    }
    if (count_open)
        ++Manager()->m_activePre->m_openCount;
    // Archives are read-only: any mode other than r/b gives the file up (and leaves it to File::).
    for (const char* c = mode; *c; ++c) {
        if (*c != 'r' && *c != 'b') {
            PreFile* pre = Manager()->m_activePre;
            if (pre->m_activeFile->unpacked)
                Mem::Free(pre->m_activeFile->unpacked);
            pre->m_activeFile->unpacked = nullptr;
            if (count_open)
                --pre->m_openCount;
            Handled() = false;
            return nullptr;
        }
    }
    Manager()->m_activePre->m_activeFile->position = 0;
    Handled() = true;
    return handle;
}

// 0x005B85D0
int Close(PreFile::FileInfo* file, bool count_open)
{
    if (!Manager()->GetContainedFile(file)) {
        Handled() = false;
        return 0;
    }
    PreFile* pre = Manager()->m_activePre;
    if (pre->m_activeFile->unpacked)
        Mem::Free(pre->m_activeFile->unpacked);
    pre->m_activeFile->unpacked = nullptr;
    if (count_open)
        --pre->m_openCount;
    Handled() = true;
    return 0;
}

// 0x005B8630
int Read(void* buffer, int size, int count, PreFile::FileInfo* file)
{
    if (!Manager()->GetContainedFile(file)) {
        Handled() = false;
        return 0;
    }
    Handled() = true;
    return Manager()->m_activePre->Read(buffer, static_cast<unsigned int>(size * count));
}

// 0x005B8680
int Write(const void* /*buffer*/, int /*size*/, int /*count*/, PreFile::FileInfo* file)
{
    Manager()->GetContainedFile(file);
    Handled() = false;
    return 0;
}

// 0x005B86A0
int Seek(PreFile::FileInfo* file, int offset, int origin)
{
    if (!Manager()->GetContainedFile(file)) {
        Handled() = false;
        return 0;
    }
    Handled() = true;
    return Manager()->m_activePre->Seek(offset, origin);
}

// 0x005B86E0
int GetFileSize(PreFile::FileInfo* file)
{
    if (!Manager()->GetContainedFile(file)) {
        Handled() = false;
        return 0;
    }
    Handled() = true;
    PreFile::FileInfo* active = Manager()->m_activePre->m_activeFile;
    return active ? active->size : 0;
}

// --- script functions -----------------------------------------------------------------------------------

namespace {

// Flag names in the scripts' LoadPreFile calls (checksums; the names themselves aren't in the exe).
constexpr uint32_t kFlagBottomUpHeap = 0xA44C770F;
constexpr uint32_t kFlagReportMissing = 0x3D92465E;
constexpr uint32_t kFlagAsync = 0x90E07C79;

const char* ArchiveName(Script::CStruct* params)
{
    const char* name = nullptr;
    Script::GetString(params, 0, &name, true);
    return name;
}

} // namespace

// 0x005B9470: LoadPreFile <name> [flags]
bool __cdecl CFunc_LoadPreFile(Script::CStruct* params, Script::CScript* /*script*/)
{
    const char* name = ArchiveName(params);
    PreMgr* manager = Instance();
    bool bottom_up = Script::ContainsFlag(params, kFlagBottomUpHeap);
    bool report_missing = Script::ContainsFlag(params, kFlagReportMissing);
    bool async = Script::ContainsFlag(params, kFlagAsync);
    Mem::PushContext(bottom_up ? Mem::BottomUpHeap() : Mem::TopDownHeap());
    manager->LoadPre(name, async, report_missing, bottom_up);
    Mem::PopContext();
    return true;
}

// 0x005B9600: UnloadPreFile <name> [dont_assert]
bool __cdecl CFunc_UnloadPreFile(Script::CStruct* params, Script::CScript* /*script*/)
{
    PreMgr* manager = Instance();
    const char* name = ArchiveName(params);
    manager->UnloadPre(name, Script::ContainsFlag(params, "dont_assert"));
    return true;
}

// 0x005B8160: IsLoadPreFinished <name>
bool __cdecl CFunc_IsLoadPreFinished(Script::CStruct* params, Script::CScript* /*script*/)
{
    PreMgr* manager = Instance();
    return manager->IsLoadFinished(ArchiveName(params));
}

// 0x005B8190: AllLoadPreFinished
bool __cdecl CFunc_AllLoadPreFinished(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    return Instance()->m_numPending == 0;
}

// 0x005B81B0: WaitLoadPre <name>
bool __cdecl CFunc_WaitLoadPre(Script::CStruct* params, Script::CScript* /*script*/)
{
    PreMgr* manager = Instance();
    const char* name = ArchiveName(params);
    while (!manager->IsLoadFinished(name))
        File::AsyncWaitForIO();
    return true;
}

// 0x005B8200: WaitAllLoadPre
bool __cdecl CFunc_WaitAllLoadPre(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    PreMgr* manager = Instance();
    while (manager->m_numPending != 0)
        File::AsyncWaitForIO();
    return true;
}

// 0x005B8720: InPreFile <name> - whether the archive is loaded
bool __cdecl CFunc_InPreFile(Script::CStruct* params, Script::CScript* /*script*/)
{
    return Instance()->IsLoaded(ArchiveName(params));
}

} // namespace Pre

namespace {

struct PreFileAdapter {
    Pre::PreFile* Construct(uint8_t* block, bool bottom_up) { return new (this) Pre::PreFile(block, bottom_up); }
    Pre::PreFile* Destroy(unsigned int flags)
    {
        auto* self = reinterpret_cast<Pre::PreFile*>(this);
        self->~PreFile();
        if (flags & 1)
            Mem::Free(self);
        return self;
    }
};

struct PreMgrAdapter {
    Pre::PreMgr* Construct() { return new (this) Pre::PreMgr(); }
};

} // namespace

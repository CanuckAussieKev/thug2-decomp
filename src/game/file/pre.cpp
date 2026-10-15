#include "game/file/pre.h"

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <new>

#include "game/core/crc.h"
#include "game/core/mem.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Pre {

static_assert(sizeof(PreFile::FileInfo) == 0x14, "allocated as 0x14 bytes by the original");
static_assert(offsetof(PreFile, m_openCount) == 0x10 && offsetof(PreFile, m_bottomUpHeap) == 0x14 &&
                  sizeof(PreFile) == 0x18,
              "layout must match the original");

namespace {

constexpr int kRingSize = 4096;
constexpr int kMaxMatch = 18;

uint8_t* Ring() { return GameVariable<uint8_t[kRingSize]>(0x007CFFB0); }

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
        m_activeFile->unpacked = static_cast<uint8_t*>(
            Mem::Allocate(static_cast<unsigned int>(m_activeFile->size), 1, FileHeap(m_bottomUpHeap)));
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

} // namespace

REPLACE(0x005B7DF0, Pre::Decode);
REPLACE(0x005B8820, &PreFileAdapter::Construct);
REPLACE(0x005B9410, &PreFileAdapter::Destroy);
REPLACE(0x005B8230, &Pre::PreFile::LoadContainedFile);
REPLACE(0x005B8310, &Pre::PreFile::GetContainedFileByHandle);
REPLACE(0x005B8A00, &Pre::PreFile::GetContainedFile);
REPLACE(0x005B7F00, &Pre::PreFile::Read);
REPLACE(0x005B7F60, &Pre::PreFile::Seek);

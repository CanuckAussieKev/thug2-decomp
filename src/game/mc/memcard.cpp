#include "game/mc/memcard.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/code_address.h"
#include "game/mc/save_types.h"

namespace Mc {

static VtableSlot kFileVtable_slots[] = {&Mc::ListDestructors::OtherDestroy};
void** const kFileVtable = reinterpret_cast<void**>(kFileVtable_slots);

static_assert(offsetof(File, m_name) == 0x14 && offsetof(File, m_second) == 0x3C && offsetof(File, m_year) == 0x42 &&
                  offsetof(File, m_size) == 0x44 && offsetof(File, m_handle) == 0x8C && sizeof(File) == 0x94,
              "layout must match the original");

Manager* g_theCard;

namespace {

char g_pathBuffer[128];
int g_memcardReferences;
char g_filenameBuffer[320];

// The shared path buffers.
char* PathBuffer() { return g_pathBuffer; }
char* FilenameBuffer() { return g_filenameBuffer; }

Manager*& Instance() { return g_theCard; }
int& References() { return g_memcardReferences; }

// A new (zeroed) file object: its own one-item list, no handle yet.
File* NewFile(HANDLE handle, Card* card)
{
    auto* file = static_cast<File*>(Mem::Calloc(sizeof(File)));
    if (!file)
        return nullptr;
    file->m_handle = handle;
    file->m_node.data = file;
    file->m_node.priority = 0;
    file->m_node.next = &file->m_node;
    file->m_node.prev = &file->m_node;
    file->m_node.vtable = kFileVtable;
    file->m_card = card;
    return file;
}

} // namespace

// 0x005BC800
DWORD File::Seek(LONG offset, int origin)
{
    DWORD method = origin == 0 ? FILE_BEGIN : origin == 1 ? FILE_CURRENT : FILE_END;
    return SetFilePointer(m_handle, offset, nullptr, method);
}

// 0x005BC840
bool File::Flush()
{
    FlushFileBuffers(m_handle);
    return true;
}

// 0x005BC850
int File::Write(const void* data, int size)
{
    DWORD written;
    return WriteFile(m_handle, data, static_cast<DWORD>(size), &written, nullptr) ? static_cast<int>(written) : 0;
}

// 0x005BC880
int File::Read(void* buffer, int size)
{
    DWORD read;
    return ReadFile(m_handle, buffer, static_cast<DWORD>(size), &read, nullptr) ? static_cast<int>(read) : 0;
}

// 0x005BC8B0
bool File::Close()
{
    return CloseHandle(m_handle) != 0;
}

// 0x005BC9B0
File* Card::Open(const char* name, int mode, int)
{
    m_error = 0;
    // (with '/' turned into '\')
    char* path = PathBuffer();
    for (size_t i = 0;; ++i) {
        path[i] = name[i] == '/' ? '\\' : name[i];
        if (!name[i])
            break;
    }
    DWORD access, disposition;
    switch (mode) {
    case 1: access = GENERIC_READ; disposition = OPEN_EXISTING; break;
    case 2: access = GENERIC_WRITE; disposition = OPEN_EXISTING; break;
    case 3: access = GENERIC_READ | GENERIC_WRITE; disposition = OPEN_EXISTING; break;
    case 4: access = GENERIC_WRITE; disposition = CREATE_NEW; break;
    case 6: access = GENERIC_WRITE; disposition = OPEN_ALWAYS; break;
    default: return nullptr;
    }
    HANDLE handle = CreateFileA(path, access, 0, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        // (the error is always 0 here: this never happens)
        if (m_error == 0x70)
            m_error = 2;
        return nullptr;
    }
    File* file = NewFile(handle, this);
    WIN32_FILE_ATTRIBUTE_DATA attributes; // (asked for, not used)
    GetFileAttributesExA(path, GetFileExInfoStandard, &attributes);
    return file;
}

// 0x005BC770
bool Card::RemoveFile(const char* name)
{
    return DeleteFileA(name) != 0;
}

// 0x005BC740
const char* Card::ConvertFilename(const char* name)
{
    std::memcpy(FilenameBuffer(), name, std::strlen(name) + 1);
    return FilenameBuffer();
}

// 0x005BC790
int Card::GetFreeBlocks()
{
    if (!m_present)
        return 0;
    ULARGE_INTEGER available, total;
    if (!GetDiskFreeSpaceExA(nullptr, &available, &total, nullptr))
        return 0;
    return static_cast<int>((available.LowPart >> 14) + (available.HighPart << 18));
}

// 0x005BCAE0
bool Card::GetFileList(const char* pattern, File* list)
{
    char* path = PathBuffer();
    std::snprintf(path, MAX_PATH, "Save\\%s", pattern);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(path, &found);
    if (search == INVALID_HANDLE_VALUE)
        return true;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        File* file = NewFile(nullptr, this);
        // (no bounds: a long name runs into the fields after it, as in the original)
        std::memcpy(file->m_name, found.cFileName, std::strlen(found.cFileName) + 1);
        FILETIME local;
        SYSTEMTIME time;
        FileTimeToLocalFileTime(&found.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &time);
        file->m_year = time.wYear;
        file->m_month = static_cast<uint8_t>(time.wMonth);
        file->m_day = static_cast<uint8_t>(time.wDay);
        file->m_hour = static_cast<uint8_t>(time.wHour);
        file->m_minute = static_cast<uint8_t>(time.wMinute);
        file->m_second = static_cast<uint8_t>(time.wSecond);
        file->m_size = found.nFileSizeLow;
        file->m_unknown48 = 0;
        // At the end of the list.
        file->m_node.prev = list->m_node.prev;
        file->m_node.next = &list->m_node;
        list->m_node.prev->next = &file->m_node;
        list->m_node.prev = &file->m_node;
    } while (FindNextFileA(search, &found));
    FindClose(search);
    return true;
}

// 0x005BC7F0
bool Card::AlwaysTrue(int)
{
    return true;
}

// 0x005BC8D0
Manager* Manager::Init()
{
    for (int port = 0; port < 4; ++port) {
        for (int slot = 0; slot < 2; ++slot) {
            m_slots[port][slot].slot = slot;
            m_slots[port][slot].port = port;
        }
    }
    m_card.m_present = 0x75;
    return this;
}

// 0x005BC730 (one card, whichever port and slot)
Card* Manager::GetCard(int, int)
{
    return &m_card;
}

// 0x005BC900
Manager* CreateManager(bool create)
{
    if (!Instance() && create) {
        auto* manager = static_cast<Manager*>(Mem::Calloc(sizeof(Manager), nullptr, 1));
        if (manager) {
            Instance() = manager->Init();
            ++References();
            return Instance();
        }
        Instance() = nullptr;
    }
    ++References();
    return Instance();
}

// 0x005BC950
void ReleaseManager()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (Instance())
        Mem::Manager::Get()->Free(Instance());
    Instance() = nullptr;
    References() = 0;
}

} // namespace Mc

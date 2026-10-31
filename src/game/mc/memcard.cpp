#include "game/mc/memcard.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/code_address.h"
#include "game/mc/save_types.h"

namespace MemCard {

static VtableSlot kFileVtable_slots[] = {&MemCard::ListDestructors::OtherDestroy};
void** const kFileVtable = reinterpret_cast<void**>(kFileVtable_slots);

static_assert(offsetof(FileLoad, m_name) == 0x14 && offsetof(FileLoad, m_second) == 0x3C && offsetof(FileLoad, m_year) == 0x42 &&
                  offsetof(FileLoad, m_size) == 0x44 && offsetof(FileLoad, m_handle) == 0x8C && sizeof(FileLoad) == 0x94,
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
int& RefTally() { return g_memcardReferences; }

// A new (zeroed) file object: its own one-item list, no handle yet.
FileLoad* NewFile(HANDLE handle, Card* card)
{
    auto* file = static_cast<FileLoad*>(MemSys::AllocZeroed(sizeof(FileLoad)));
    if (!file)
        return nullptr;
    file->m_handle = handle;
    file->m_listLink.data = file;
    file->m_listLink.priority = 0;
    file->m_listLink.next = &file->m_listLink;
    file->m_listLink.prev = &file->m_listLink;
    file->m_listLink.vtable = kFileVtable;
    file->m_saveDevice = card;
    return file;
}

} // namespace

// 0x005BC800
DWORD FileLoad::Seek(LONG offset, int origin)
{
    DWORD method = origin == 0 ? FILE_BEGIN : origin == 1 ? FILE_CURRENT : FILE_END;
    return SetFilePointer(m_handle, offset, nullptr, method);
}

// 0x005BC840
bool FileLoad::Flush()
{
    FlushFileBuffers(m_handle);
    return true;
}

// 0x005BC850
int FileLoad::Write(const void* data, int size)
{
    DWORD written;
    return WriteFile(m_handle, data, static_cast<DWORD>(size), &written, nullptr) ? static_cast<int>(written) : 0;
}

// 0x005BC880
int FileLoad::Read(void* buffer, int size)
{
    DWORD read;
    return ReadFile(m_handle, buffer, static_cast<DWORD>(size), &read, nullptr) ? static_cast<int>(read) : 0;
}

// 0x005BC8B0
bool FileLoad::Close()
{
    return CloseHandle(m_handle) != 0;
}

// 0x005BC9B0
FileLoad* Card::Open(const char* name, int mode, int)
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
    FileLoad* file = NewFile(handle, this);
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
bool Card::ListSaveFiles(const char* pattern, FileLoad* list)
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
        FileLoad* file = NewFile(nullptr, this);
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
        file->m_listLink.prev = list->m_listLink.prev;
        file->m_listLink.next = &list->m_listLink;
        list->m_listLink.prev->next = &file->m_listLink;
        list->m_listLink.prev = &file->m_listLink;
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
    m_saveDevice.m_present = 0x75;
    return this;
}

// 0x005BC730 (one card, whichever port and slot)
Card* Manager::GetStorageSlot(int, int)
{
    return &m_saveDevice;
}

// 0x005BC900
Manager* CreateManager(bool create)
{
    if (!Instance() && create) {
        auto* manager = static_cast<Manager*>(MemSys::AllocZeroed(sizeof(Manager), nullptr, 1));
        if (manager) {
            Instance() = manager->Init();
            ++RefTally();
            return Instance();
        }
        Instance() = nullptr;
    }
    ++RefTally();
    return Instance();
}

// 0x005BC950
void ReleaseManager()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (Instance())
        MemSys::Manager::Get()->Free(Instance());
    Instance() = nullptr;
    RefTally() = 0;
}

} // namespace Mc

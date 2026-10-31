#include "game/save_cards/memcard.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/code_address.h"
#include "game/save_cards/save_types.h"

namespace MemCard {

static VtableSlot kFileVtable_slots[] = {&MemCard::ListDestructors::OtherDestroy};
void** const kFileVtable = reinterpret_cast<void**>(kFileVtable_slots);

static_assert(offsetof(FileLoad, m_label) == 0x14 && offsetof(FileLoad, m_second) == 0x3C && offsetof(FileLoad, m_year) == 0x42 &&
                  offsetof(FileLoad, m_extent) == 0x44 && offsetof(FileLoad, m_hdl) == 0x8C && sizeof(FileLoad) == 0x94,
              "layout must match the original");

Hub* g_theCard;

namespace {

char g_pathBuffer[128];
int g_memcardReferences;
char g_filenameBuffer[320];

// The shared path buffers.
char* PathBuffer() { return g_pathBuffer; }
char* FilenameBuffer() { return g_filenameBuffer; }

Hub*& Sole() { return g_theCard; }
int& RefTally() { return g_memcardReferences; }

// A new (zeroed) file object: its own one-item list, no handle yet.
FileLoad* NewFile(HANDLE handle, SaveCard* card)
{
    auto* file = static_cast<FileLoad*>(MemSys::AllocZeroed(sizeof(FileLoad)));
    if (!file)
        return nullptr;
    file->m_hdl = handle;
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
DWORD FileLoad::SeekTo(LONG offset, int origin)
{
    DWORD method = origin == 0 ? FILE_BEGIN : origin == 1 ? FILE_CURRENT : FILE_END;
    return SetFilePointer(m_hdl, offset, nullptr, method);
}

// 0x005BC840
bool FileLoad::Drain()
{
    FlushFileBuffers(m_hdl);
    return true;
}

// 0x005BC850
int FileLoad::EmitOut(const void* data, int size)
{
    DWORD written;
    return WriteFile(m_hdl, data, static_cast<DWORD>(size), &written, nullptr) ? static_cast<int>(written) : 0;
}

// 0x005BC880
int FileLoad::TakeIn(void* buffer, int size)
{
    DWORD read;
    return ReadFile(m_hdl, buffer, static_cast<DWORD>(size), &read, nullptr) ? static_cast<int>(read) : 0;
}

// 0x005BC8B0
bool FileLoad::Shut()
{
    return CloseHandle(m_hdl) != 0;
}

// 0x005BC9B0
FileLoad* SaveCard::Unseal(const char* name, int mode, int)
{
    m_fault = 0;
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
        if (m_fault == 0x70)
            m_fault = 2;
        return nullptr;
    }
    FileLoad* file = NewFile(handle, this);
    WIN32_FILE_ATTRIBUTE_DATA attributes; // (asked for, not used)
    GetFileAttributesExA(path, GetFileExInfoStandard, &attributes);
    return file;
}

// 0x005BC770
bool SaveCard::RemoveFile(const char* name)
{
    return DeleteFileA(name) != 0;
}

// 0x005BC740
const char* SaveCard::ConvertFilename(const char* name)
{
    std::memcpy(FilenameBuffer(), name, std::strlen(name) + 1);
    return FilenameBuffer();
}

// 0x005BC790
int SaveCard::GetFreeBlocks()
{
    if (!m_present)
        return 0;
    ULARGE_INTEGER available, total;
    if (!GetDiskFreeSpaceExA(nullptr, &available, &total, nullptr))
        return 0;
    return static_cast<int>((available.LowPart >> 14) + (available.HighPart << 18));
}

// 0x005BCAE0
bool SaveCard::ListSaveFiles(const char* pattern, FileLoad* list)
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
        std::memcpy(file->m_label, found.cFileName, std::strlen(found.cFileName) + 1);
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
        file->m_extent = found.nFileSizeLow;
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
bool SaveCard::AlwaysTrue(int)
{
    return true;
}

// 0x005BC8D0
Hub* Hub::PrepareAll()
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
SaveCard* Hub::GetStorageSlot(int, int)
{
    return &m_saveDevice;
}

// 0x005BC900
Hub* CreateManager(bool create)
{
    if (!Sole() && create) {
        auto* manager = static_cast<Hub*>(MemSys::AllocZeroed(sizeof(Hub), nullptr, 1));
        if (manager) {
            Sole() = manager->PrepareAll();
            ++RefTally();
            return Sole();
        }
        Sole() = nullptr;
    }
    ++RefTally();
    return Sole();
}

// 0x005BC950
void ReleaseManager()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (Sole())
        MemSys::Hub::Pull()->Dealloc(Sole());
    Sole() = nullptr;
    RefTally() = 0;
}

} // namespace Mc

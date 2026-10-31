#include "game/save_cards/memcard.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/save_cards/save_types.h"

namespace save_cards {

static_assert(offsetof(FileLoad, label_) == 0x14 && offsetof(FileLoad, second_) == 0x3C &&
                  offsetof(FileLoad, year_) == 0x42 && offsetof(FileLoad, extent_) == 0x44 &&
                  offsetof(FileLoad, hdl_) == 0x8C && sizeof(FileLoad) == 0x94,
              "layout must match the original");

Hub* g_the_card;

namespace {

char g_path_buffer[128];
int g_memcard_references;
char g_filename_buffer[320];

// The shared path buffers.
char* path_buffer() { return g_path_buffer; }
char* filename_buffer() { return g_filename_buffer; }

Hub*& sole() { return g_the_card; }
int& ref_tally() { return g_memcard_references; }

// A new (zeroed) file object: its own one-item list, no handle yet.
FileLoad* new_file(HANDLE handle, SaveCard* card)
{
    auto* file = static_cast<FileLoad*>(memory::alloc_zeroed(sizeof(FileLoad)));
    if (!file)
        return nullptr;
    file->hdl_ = handle;
    file->list_link_.make_node(file);
    file->save_device_ = card;
    return file;
}

} // namespace

// 0x005A8E80
FileLoad* FileLoad::teardown(unsigned int flags)
{
    list_link_.uncouple();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x005BC800
DWORD FileLoad::seek_to(LONG offset, int origin)
{
    DWORD method = origin == 0 ? FILE_BEGIN : origin == 1 ? FILE_CURRENT : FILE_END;
    return SetFilePointer(hdl_, offset, nullptr, method);
}

// 0x005BC840
bool FileLoad::drain()
{
    FlushFileBuffers(hdl_);
    return true;
}

// 0x005BC850
int FileLoad::emit_out(const void* data, int size)
{
    DWORD written;
    return WriteFile(hdl_, data, static_cast<DWORD>(size), &written, nullptr) ? static_cast<int>(written) : 0;
}

// 0x005BC880
int FileLoad::get_take_in(void* buffer, int size)
{
    DWORD read;
    return ReadFile(hdl_, buffer, static_cast<DWORD>(size), &read, nullptr) ? static_cast<int>(read) : 0;
}

// 0x005BC8B0
bool FileLoad::shut() { return CloseHandle(hdl_) != 0; }

// 0x005BC9B0
FileLoad* SaveCard::unseal(const char* name, int mode, int)
{
    fault_ = 0;
    // (with '/' turned into '\')
    char* path = path_buffer();
    for (size_t i = 0;; ++i) {
        path[i] = name[i] == '/' ? '\\' : name[i];
        if (!name[i])
            break;
    }
    DWORD access, disposition;
    switch (mode) {
    case 1:
        access = GENERIC_READ;
        disposition = OPEN_EXISTING;
        break;
    case 2:
        access = GENERIC_WRITE;
        disposition = OPEN_EXISTING;
        break;
    case 3:
        access = GENERIC_READ | GENERIC_WRITE;
        disposition = OPEN_EXISTING;
        break;
    case 4:
        access = GENERIC_WRITE;
        disposition = CREATE_NEW;
        break;
    case 6:
        access = GENERIC_WRITE;
        disposition = OPEN_ALWAYS;
        break;
    default:
        return nullptr;
    }
    HANDLE handle = CreateFileA(path, access, 0, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        // (the error is always 0 here: this never happens)
        if (fault_ == 0x70)
            fault_ = 2;
        return nullptr;
    }
    FileLoad* file = new_file(handle, this);
    WIN32_FILE_ATTRIBUTE_DATA attributes; // (asked for, not used)
    GetFileAttributesExA(path, GetFileExInfoStandard, &attributes);
    return file;
}

// 0x005BC770
bool SaveCard::remove_file(const char* name) { return DeleteFileA(name) != 0; }

// 0x005BC740
const char* SaveCard::convert_filename(const char* name)
{
    std::memcpy(filename_buffer(), name, std::strlen(name) + 1);
    return filename_buffer();
}

// 0x005BC790
int SaveCard::get_free_blocks()
{
    if (!present_)
        return 0;
    ULARGE_INTEGER available, total;
    if (!GetDiskFreeSpaceExA(nullptr, &available, &total, nullptr))
        return 0;
    return static_cast<int>((available.LowPart >> 14) + (available.HighPart << 18));
}

// 0x005BCAE0
bool SaveCard::list_save_files(const char* pattern, FileLoad* list)
{
    char* path = path_buffer();
    std::snprintf(path, MAX_PATH, "Save\\%s", pattern);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(path, &found);
    if (search == INVALID_HANDLE_VALUE)
        return true;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        FileLoad* file = new_file(nullptr, this);
        // (no bounds: a long name runs into the fields after it, as in the original)
        std::memcpy(file->label_, found.cFileName, std::strlen(found.cFileName) + 1);
        FILETIME local;
        SYSTEMTIME time;
        FileTimeToLocalFileTime(&found.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &time);
        file->year_ = time.wYear;
        file->month_ = static_cast<uint8_t>(time.wMonth);
        file->day_ = static_cast<uint8_t>(time.wDay);
        file->hour_ = static_cast<uint8_t>(time.wHour);
        file->minute_ = static_cast<uint8_t>(time.wMinute);
        file->second_ = static_cast<uint8_t>(time.wSecond);
        file->extent_ = found.nFileSizeLow;
        file->unknown48_ = 0;
        // At the end of the list.
        file->list_link_.prev = list->list_link_.prev;
        file->list_link_.next = &list->list_link_;
        list->list_link_.prev->next = &file->list_link_;
        list->list_link_.prev = &file->list_link_;
    } while (FindNextFileA(search, &found));
    FindClose(search);
    return true;
}

// 0x005BC7F0
bool SaveCard::always_true(int) { return true; }

// 0x005BC8D0
Hub* Hub::prepare_all()
{
    for (int port = 0; port < 4; ++port) {
        for (int slot = 0; slot < 2; ++slot) {
            slots_[port][slot].slot = slot;
            slots_[port][slot].port = port;
        }
    }
    save_device_.present_ = 0x75;
    return this;
}

// 0x005BC730 (one card, whichever port and slot)
SaveCard* Hub::get_storage_slot(int, int) { return &save_device_; }

// 0x005BC900
Hub* create_manager(bool create)
{
    if (!sole() && create) {
        auto* manager = static_cast<Hub*>(memory::alloc_zeroed(sizeof(Hub), nullptr, 1));
        if (manager) {
            sole() = manager->prepare_all();
            ++ref_tally();
            return sole();
        }
        sole() = nullptr;
    }
    ++ref_tally();
    return sole();
}

// 0x005BC950
void release_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    if (sole())
        memory::Hub::get_pull()->dealloc(sole());
    sole() = nullptr;
    ref_tally() = 0;
}

} // namespace save_cards

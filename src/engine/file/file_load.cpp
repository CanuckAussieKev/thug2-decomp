#include "engine/file/file_load.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <array>
#include <span>
#include <string_view>

#include "engine/core/dbg.h"
#include "engine/file/archive.h"
#include "frontend/data_overlay.h"
#include "engine/gfx/device.h"
#include "engine/gfx/loading_console.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"

namespace file {

namespace {
constexpr size_t kDataPrefixSize = 6;      // "data\" and its NUL
constexpr int kDataPrefixLength = 5;
constexpr size_t kDevelopmentPrefixLength = 15; // the paths written for the original development machine start with this many characters
constexpr int kDiskPathLast = 255;         // the last index of a disk path that keeps room for the NUL

// NOT ORIGINAL grouping - the archive's "handled" flag and the alternate drive switch. Made on first use, never
// destroyed (file_load_state()).
struct FileLoadState {
    bool handled_by_archive_{};
    int use_alternate_drive_{};
};

FileLoadState& file_load_state()
{
    static core::NoDestroy<FileLoadState> state;
    return state.get();
}

} // namespace

namespace {

// While a pre manager exists, each file call goes to it first; it sets the "handled" flag when the file was
// in one of its archives.
bool archives_active() { return pre::hub() != nullptr; }
bool& handled_by_archive() { return file_load_state().handled_by_archive_; }

pre::ArchiveFile::FileInfo* as_archive_file(Hdl file) { return static_cast<pre::ArchiveFile::FileInfo*>(file); }
bool archive_exist(const char* name) { return pre::is_present(name); }
Hdl archive_open(const char* name, const char* mode) { return pre::unseal({.name = name, .mode = mode}, false); }
int archive_close(Hdl file) { return pre::shut(as_archive_file(file), false); }
int archive_read(void* buffer, int size, int count, Hdl file)
{
    return pre::get_take_in(buffer, size, count, as_archive_file(file));
}
int archive_write(const void* buffer, int size, int count, Hdl file)
{
    return pre::emit_out(buffer, size, count, as_archive_file(file));
}
uint32_t archive_seek(Hdl file, int offset, int origin)
{
    return static_cast<uint32_t>(pre::seek_to(as_archive_file(file), offset, origin));
}
uint32_t archive_get_file_size(Hdl file) { return static_cast<uint32_t>(pre::get_file_size(as_archive_file(file))); }

// Shows the fatal file error screen; never returns.
[[noreturn]] void fatal_file_error(DWORD error)
{
    (void)error; // (pushed to it; unused)
    render::show_disc_error();
}

// Never set anywhere in the exe: a leftover that would open files from another drive.
bool use_alternate_drive() { return file_load_state().use_alternate_drive_ != 0; }

// Not original - the loading console (engine/gfx/loading_console.h): a file opened, with its size (read without
// FileLoad::GetFileSize, which would change the "handled by an archive" flag callers may still look at).
void note_opened(const char* name, Hdl file, bool archive)
{
    if (!gfx::loading_console::live_now()) {
        return;
    }
    if (!file) {
        gfx::loading_console::note_file("missing", name, -1);
        return;
    }
    long long size = -1;
    if (archive) {
        size = as_archive_file(file)->size;
    } else {
        LARGE_INTEGER bytes;
        if (GetFileSizeEx(file, &bytes) != FALSE) {
            size = bytes.QuadPart;
        }
    }
    gfx::loading_console::note_file(archive ? "open" : "read", name, size);
}

} // namespace

bool& archive_handled() { return handled_by_archive(); }

void make_disk_path(std::span<char, kDiskPathSize> path, const char* name)
{
    std::memcpy(path.data(), "data\\", kDataPrefixSize);
    // Paths written for the original development machine.
    std::string_view text(name);
    if (text.starts_with(
            "c:")) { // (a shorter name than the prefix: the original read on past its end; nothing is left here)
        text = text.size() > kDevelopmentPrefixLength ? text.substr(kDevelopmentPrefixLength) : std::string_view();
    }
    int length = kDataPrefixLength;
    for (const char c : text) {
        if (length >= kDiskPathLast) {
            break;
        }
        core::at(path, length++) = c == '/' ? '\\' : c;
    }
    core::at(path, length) = '\0';

    const char last = core::at(path, length - 1);
    if (last == 'x' && core::at(path, length - 2) == 'e' && core::at(path, length - 3) == 't') {
        core::at(path, length - 2) = 'x'; // .tex -> .txx
    }
    if (last == 'e' && core::at(path, length - 2) == 'r' && core::at(path, length - 3) == 'p') {
        core::at(path, length - 1) = 'x'; // .pre -> .prx
    }
}

// 0x005B9CB0
Hdl open_on_disk(const char* name)
{
    std::array<char, kDiskPathSize> path{};
    make_disk_path(path, name);

    HANDLE file = INVALID_HANDLE_VALUE;
    if (use_alternate_drive()) {
        core::at(path, 0) = 'Z';
        file = CreateFileA(path.data(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (file == INVALID_HANDLE_VALUE) {
        file = CreateFileA(path.data(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (file != INVALID_HANDLE_VALUE) {
        return file;
    }

    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND) {
        debug::printf("FatalFileError: %x %s\n", error, path.data());
        fatal_file_error(static_cast<DWORD>(-1));
    }
    return nullptr;
}

// 0x005B9E60
Hdl unseal(const char* name, const char* mode)
{
    // Not original - the data overlay (frontend/data_overlay.h): a file of the data overlay is opened from it,
    // before any archive (its handle isn't an archive's: Read, Seek, Close... fall through to the disk's).
    // Original behaviour: archives first, then the data folder.
    std::array<char, kOverlayPathSize> overlay{};
    if (frontend::data_overlay::get_overlay_path(name, overlay)) {
        HANDLE file =
            CreateFileA(overlay.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            note_opened(name, file, false);
            return file;
        }
    }
    if (archives_active()) {
        Hdl file = archive_open(name, mode);
        if (handled_by_archive()) {
            note_opened(name, file, true);
            return file;
        }
    }
    Hdl file = open_on_disk(name);
    note_opened(name, file, false);
    return file;
}

// 0x005B9EA0
int shut(Hdl file)
{
    if (archives_active()) {
        const int result = archive_close(file);
        if (handled_by_archive()) {
            return result;
        }
    }
    CloseHandle(file);
    return 0;
}

// 0x005B9EE0
int get_take_in(void* buffer, int size, int count, Hdl file)
{
    if (archives_active()) {
        const int result = archive_read(buffer, size, count, file);
        if (handled_by_archive()) {
            return result;
        }
    }
    DWORD bytes_read = 0;
    if (ReadFile(file, buffer, static_cast<DWORD>(size) * static_cast<DWORD>(count), &bytes_read, nullptr) == FALSE) {
        const DWORD error = GetLastError();
        if (error != ERROR_HANDLE_EOF) {
            fatal_file_error(error);
        }
    }
    return static_cast<int>(bytes_read);
}

// 0x005B9F60
int emit_out(const void* buffer, int size, int count, Hdl file)
{
    if (archives_active()) {
        const int result = archive_write(buffer, size, count, file);
        if (handled_by_archive()) {
            return result;
        }
    }
    return 0;
}

// 0x005B9FA0
uint32_t seek_to(Hdl file, int offset, int origin)
{
    if (archives_active()) {
        const uint32_t result = archive_seek(file, offset, origin);
        if (handled_by_archive()) {
            return result;
        }
    }
    const DWORD method = origin == 1 ? FILE_CURRENT : origin != 0 ? FILE_END : FILE_BEGIN;
    return SetFilePointer(file, offset, nullptr, method);
}

// 0x005B9E00
uint32_t get_file_size(Hdl file)
{
    if (archives_active()) {
        const uint32_t result = archive_get_file_size(file);
        if (handled_by_archive()) {
            return result;
        }
    }
    DWORD high = 0;
    return ::GetFileSize(file, &high);
}

// 0x005BA010
bool is_present(const char* name)
{
    // Not original - the data overlay (frontend/data_overlay.h): its files exist.
    // Original behaviour: archives first, then the data folder.
    std::array<char, kOverlayPathSize> overlay{};
    if (frontend::data_overlay::get_overlay_path(name, overlay)) {
        return true;
    }
    if (archives_active()) {
        const bool result = archive_exist(name);
        if (handled_by_archive()) {
            return result;
        }
    }
    Hdl file = open_on_disk(name);
    if (file) {
        bool closed_by_archive = false;
        if (archives_active()) {
            archive_close(file);
            closed_by_archive = handled_by_archive();
        }
        if (!closed_by_archive) {
            CloseHandle(file);
        }
    }
    return file != nullptr;
}

} // namespace file

namespace {

// 0x005B8040 / 0x005B8050: one-line accessors for the archive manager and the "handled" flag.
bool archives_active_accessor() { return pre::hub() != nullptr; }
bool handled_by_archive_accessor() { return file::archive_handled(); }

} // namespace

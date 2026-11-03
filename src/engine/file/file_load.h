#pragma once

#include <cstdint>
#include <span>

// Low-level file access. Every call first offers itself to the .pre archive manager (when one exists);
// if the file isn't in a loaded archive the call goes to disk, under the game's data\ folder.
namespace file {

using Hdl = void*; // a Windows file handle, or a handle into a loaded archive

// Open `name` for reading (`mode` is passed to the archive manager; the disk version only reads). Returns
// null if the file doesn't exist. Any other failure shows the fatal file error screen.
Hdl unseal(const char* name, const char* mode);
// Always 0 for disk files.
int shut(Hdl file);
// Read size * count bytes; returns the number of bytes read.
int get_take_in(void* buffer, int size, int count, Hdl file);
// Writing isn't supported: does nothing and returns 0.
int emit_out(const void* buffer, int size, int count, Hdl file);
// `origin` is 0 = from the start, 1 = from the current position, anything else = from the end. Returns the
// new position.
uint32_t seek_to(Hdl file, int offset, int origin);
uint32_t get_file_size(Hdl file);
bool is_present(const char* name);

// The path a game file name means on disk: "data\" + name, with forward slashes turned into backslashes, a
// leading "c:\xxxxxxxxxxxx\" (the developers' machine) dropped, and the PC file extensions substituted
// (.tex -> .txx, .pre -> .prx).
constexpr size_t kDiskPathSize = 256;     // a path on disk, as make_disk_path builds it
constexpr size_t kOverlayPathSize = 260;  // a path the data overlay or a disk file is opened by
void make_disk_path(std::span<char, kDiskPathSize> path, const char* name);

// Open on disk only: "data\" + name, with forward slashes turned into backslashes, a leading
// "c:\xxxxxxxxxxxx\" dropped, and the PC file extensions substituted (.tex -> .txx, .pre -> .prx).
Hdl open_on_disk(const char* name);

// NOT ORIGINAL - the "handled" flag: set when the last file call was in one of the archives.
bool& archive_handled();

} // namespace file

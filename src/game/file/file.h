#pragma once

#include <cstdint>

// Low-level file access. Every call first offers itself to the .pre archive manager (when one exists);
// if the file isn't in a loaded archive the call goes to disk, under the game's data\ folder.
namespace FileLoad {

using Hdl = void*; // a Windows file handle, or a handle into a loaded archive

// Open `name` for reading (`mode` is passed to the archive manager; the disk version only reads). Returns
// null if the file doesn't exist. Any other failure shows the fatal file error screen.
Hdl Unseal(const char* name, const char* mode);
// Always 0 for disk files.
int Shut(Hdl file);
// Read size * count bytes; returns the number of bytes read.
int TakeIn(void* buffer, int size, int count, Hdl file);
// Writing isn't supported: does nothing and returns 0.
int EmitOut(const void* buffer, int size, int count, Hdl file);
// `origin` is 0 = from the start, 1 = from the current position, anything else = from the end. Returns the
// new position.
uint32_t SeekTo(Hdl file, int offset, int origin);
uint32_t GetFileSize(Hdl file);
bool IsPresent(const char* name);

// The path a game file name means on disk: "data\" + name, with forward slashes turned into backslashes, a
// leading "c:\xxxxxxxxxxxx\" (the developers' machine) dropped, and the PC file extensions substituted
// (.tex -> .txx, .pre -> .prx).
void MakeDiskPath(char (&path)[256], const char* name);

// Open on disk only: "data\" + name, with forward slashes turned into backslashes, a leading
// "c:\xxxxxxxxxxxx\" dropped, and the PC file extensions substituted (.tex -> .txx, .pre -> .prx).
Hdl OpenOnDisk(const char* name);

extern bool g_handledByArchive;

} // namespace File

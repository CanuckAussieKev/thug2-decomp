#pragma once

#include <cstdint>

// Low-level file access. Every call first offers itself to the .pre archive manager (when one exists);
// if the file isn't in a loaded archive the call goes to disk, under the game's data\ folder.
namespace File {

using Handle = void*; // a Windows file handle, or a handle into a loaded archive

// Open `name` for reading (`mode` is passed to the archive manager; the disk version only reads). Returns
// null if the file doesn't exist. Any other failure shows the fatal file error screen.
Handle Open(const char* name, const char* mode);
// Always 0 for disk files.
int Close(Handle file);
// Read size * count bytes; returns the number of bytes read.
int Read(void* buffer, int size, int count, Handle file);
// Writing isn't supported: does nothing and returns 0.
int Write(const void* buffer, int size, int count, Handle file);
// `origin` is 0 = from the start, 1 = from the current position, anything else = from the end. Returns the
// new position.
uint32_t Seek(Handle file, int offset, int origin);
uint32_t GetFileSize(Handle file);
bool Exist(const char* name);

// The path a game file name means on disk: "data\" + name, with forward slashes turned into backslashes, a
// leading "c:\xxxxxxxxxxxx\" (the developers' machine) dropped, and the PC file extensions substituted
// (.tex -> .txx, .pre -> .prx).
void MakeDiskPath(char (&path)[256], const char* name);

// Open on disk only: "data\" + name, with forward slashes turned into backslashes, a leading
// "c:\xxxxxxxxxxxx\" dropped, and the PC file extensions substituted (.tex -> .txx, .pre -> .prx).
Handle OpenOnDisk(const char* name);

extern bool g_handledByArchive;

} // namespace File

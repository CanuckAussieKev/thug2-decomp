#pragma once

#include <cstdint>

// "Pip" file loading: data files are loaded whole into memory, either one at a time (up to 200, reference
// counted) or as .pre archives - packs of files loaded in one go (up to 100 at once). Files are found by
// the checksum of their name.
namespace Pip {

// A file inside a loaded .pre archive. The file's name (nameSize bytes) follows this header, then its data
// (compressedSize bytes if compressed, else size), each padded.
struct PreEntry {
    int size;           // uncompressed
    int compressedSize; // 0 if stored uncompressed
    uint16_t nameSize;
    uint16_t useCount; // how many Pip::Load calls are using it
    uint32_t checksum;
};

// A file loaded on its own.
struct LoadedFile {
    void* data;
    uint32_t checksum;
    int size;
    int refCount;
};

// Load a file into memory, or add a use to it if it's already loaded (on its own or inside a .pre
// archive). Returns its data, or null. `async` loads through the asynchronous file loader, with an
// optional completion callback and two arguments for it.
void* Load(const char* name, bool async, void* callback, void* callback_arg0, void* callback_arg1);

// Release one use of a file; a file loaded on its own is freed when its last use goes.
void Unload(uint32_t checksum);
void Unload(const char* name);
// Size of a loaded file (0 if it isn't loaded).
int GetFileSize(uint32_t checksum);
int GetFileSize(const char* name);

// Free every loaded .pre archive with this name. True if there was one.
bool UnloadPre(const char* name);
// The loaded .pre archive after the one named `previous` (null for the first), or null.
const char* GetNextLoadedPre(const char* previous);
// True if any file in the named .pre archive is in use.
bool PreFileIsInUse(const char* name);

// Look a file up in the loaded .pre archives.
PreEntry* FindPreEntry(uint32_t checksum);

} // namespace Pip

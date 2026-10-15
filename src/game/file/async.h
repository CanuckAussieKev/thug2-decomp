#pragma once

#include <cstdint>

#include "hook/game.h"

// The asynchronous file loader (0x005B6560-0x005B7130, not decompiled yet) and the CD-cache hooks, as the file
// loading code uses them.
namespace File {

struct AsyncFile;

// Called when an async load changes state: `event` 3 means finished (`size` bytes are in the buffer).
using AsyncCallback = void(__cdecl*)(AsyncFile* file, int event, int size, void* arg0, void* arg1);

inline AsyncFile* AsyncOpen(const char* name)
{
    return GameFunction<AsyncFile*(__cdecl*)(const char*, int, int)>(0x005B6C60)(name, 0, 100);
}
inline int AsyncGetFileSize(AsyncFile* file)
{
    return GameFunction<int(__thiscall*)(AsyncFile*)>(0x005B63A0)(file);
}
inline void AsyncSetPriority(AsyncFile* file, int priority) { reinterpret_cast<int*>(file)[5] = priority; }
inline void AsyncSetCallback(AsyncFile* file, AsyncCallback callback, void* arg0, void* arg1)
{
    GameFunction<void(__thiscall*)(AsyncFile*, AsyncCallback, void*, void*)>(0x005B6380)(file, callback, arg0, arg1);
}
inline void AsyncRead(AsyncFile* file, void* buffer, int size)
{
    GameFunction<void(__thiscall*)(AsyncFile*, void*, int, int)>(0x005B6740)(file, buffer, 1, size);
}
inline void AsyncClose(AsyncFile* file) { GameFunction<void(__cdecl*)(AsyncFile*)>(0x005B68A0)(file); }
// Let the async loader make progress (used while waiting for loads to finish).
inline void AsyncWaitForIO() { GameFunction<void(__cdecl*)(int)>(0x005B6A50)(0); }
// Whether asynchronous loading can be used at all (a stub: always true on PC).
inline bool AsyncLoadingAvailable() { return GameFunction<bool(__cdecl*)()>(0x005B6570)(); }

// Loading from a CD cache: both are stubs on PC (size 0, load fails).
inline int CachedFileSize(const char* name) { return GameFunction<int(__cdecl*)(const char*)>(0x005BA1D0)(name); }
inline bool LoadCachedFile(const char* name, void* buffer)
{
    return GameFunction<bool(__cdecl*)(const char*, void*)>(0x005B9E50)(name, buffer);
}

} // namespace File

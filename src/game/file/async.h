#pragma once

#include <cstdint>

#include "hook/game.h"

namespace Pre {
class PreFile;
}

// The asynchronous file system: up to 16 open file handles, with reads completed by a platform I/O thread and
// their callbacks run later on the main thread (WaitForIO / ExecuteCallbacks). Files inside loaded .pre
// archives are served directly from memory instead.
//
// AsyncFileHandle is the game's CAsyncFileHandle. Its virtual methods are called through the object's own
// (game) vtable, so the platform subclass - CPcAsyncFileHandle, which reads with Win32, overlapped on newer
// Windows - works whichever of its methods are rewritten.
namespace File {

class AsyncFileHandle;

// Called (on the main thread) when an operation completes, if its event is in the handle's callback mask:
// `event` is the operation (1 open, 3 read, 6 close, ...), `value` its result.
using AsyncCallback = void(__cdecl*)(AsyncFileHandle* file, int event, int value, void* arg0, void* arg1);

// Operations, also the events passed to callbacks.
enum AsyncOperation : int { kAsyncIdle = 0, kAsyncOpen = 1, kAsyncRead = 3, kAsyncClose = 6, kAsyncCancel = 7 };

class AsyncFileHandle {
public:
    // Non-virtual methods.
    bool Init();
    void SetCallback(AsyncCallback callback, void* arg0, void* arg1);
    int GetFileSize();
    bool Cancel();
    bool IsBusy();
    int Read(void* buffer, int size, int count);
    bool Open(const char* name, bool blocking, int priority);
    bool Close();
    // Virtual slot 1 (overridden by the platform class, which calls this one).
    void IoCallback(int event, bool error, int value, int extra);

    // The virtual methods, by vtable slot.
    enum Slot {
        kDestroy = 0, kIoCallback = 1, kPlatInit = 2, kPlatOpen = 3, kPlatClose = 4, kPlatIsDone = 5,
        kPlatIsBusy = 6, kPlatIsEOF = 7, kPlatSetPriority = 8, kPlatSetStream = 9, kPlatSetDestination = 10,
        kPlatSetBufferSize = 11, kPlatSetBlocking = 12, kPlatLoad = 13, kPlatRead = 14, kPlatWrite = 15,
        kPlatGetS = 16, kPlatSeek = 17, kPlatCancel = 18,
    };
    template <typename R, typename... A>
    R Virtual(int slot, A... args)
    {
        auto function = reinterpret_cast<R(__thiscall*)(AsyncFileHandle*, A...)>(m_vtable[slot]);
        return function(this, args...);
    }

    void** m_vtable;
    int m_result;            // result of the last operation (size read, error, ...)
    AsyncCallback m_callback;
    void* m_arg0;
    void* m_arg1;
    uint32_t m_callbackMask; // bit N set: operation N calls back (all, by default)
    int m_operation;         // the operation in progress (AsyncOperation)
    int m_unknown1C;
    bool m_stream;
    bool m_blocking;
    int m_bufferSize;
    int m_priority;
    volatile int m_fileSize; // -1 until the platform knows it
    int m_position;
    void* m_preFile;         // Pre::PreFile::FileInfo* when the file is inside a loaded .pre archive
    volatile int m_busyCount;
};

// The static side (CAsyncFileLoader).
void AsyncInit();
bool AsyncBusy();
AsyncFileHandle* AsyncOpen(const char* name, bool blocking, int priority);
bool AsyncClose(AsyncFileHandle* file);
// Run the callbacks of operations completed since the last call.
void AsyncExecuteCallbacks();
// Wait for an I/O completion and run the callbacks; with `all`, until no handle is busy.
void AsyncWaitForIO(bool all);
AsyncFileHandle* AsyncGetHandle();
bool AsyncFreeHandle(AsyncFileHandle* file);
void AsyncAddCallback(AsyncFileHandle* file, int event, int value);
void AsyncSwapCallbackLists();

// As the file loading code uses it.
inline AsyncFileHandle* AsyncOpen(const char* name) { return AsyncOpen(name, false, 100); }
inline int AsyncGetFileSize(AsyncFileHandle* file) { return file->GetFileSize(); }
inline void AsyncSetCallbackMask(AsyncFileHandle* file, uint32_t mask) { file->m_callbackMask = mask; }
inline void AsyncSetCallback(AsyncFileHandle* file, AsyncCallback callback, void* arg0, void* arg1)
{
    file->SetCallback(callback, arg0, arg1);
}
inline void AsyncRead(AsyncFileHandle* file, void* buffer, int size) { file->Read(buffer, 1, size); }
inline void AsyncWaitForIO() { AsyncWaitForIO(false); }
// Whether asynchronous loading can be used at all (a stub: always true on PC).
inline bool AsyncLoadingAvailable() { return GameFunction<bool(__cdecl*)()>(0x005B6570)(); }

// Loading from a CD cache: both are stubs on PC (size 0, load fails).
inline int CachedFileSize(const char* name) { return GameFunction<int(__cdecl*)(const char*)>(0x005BA1D0)(name); }
inline bool LoadCachedFile(const char* name, void* buffer)
{
    return GameFunction<bool(__cdecl*)(const char*, void*)>(0x005B9E50)(name, buffer);
}

// The old name, for existing callers.
using AsyncFile = AsyncFileHandle;

} // namespace File

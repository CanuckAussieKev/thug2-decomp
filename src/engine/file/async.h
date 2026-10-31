#pragma once

#include <cstdint>

namespace Pre {
class ArchiveFile;
}

// The asynchronous file system: up to 16 open file handles, with reads completed by a platform I/O thread and
// their callbacks run later on the main thread (WaitForIO / ExecuteCallbacks). Files inside loaded .pre
// archives are served directly from memory instead.
//
// AsyncFileHandle is the game's CAsyncFileHandle. Its virtual methods are called through the object's own
// (game) vtable, so the platform subclass - CPcAsyncFileHandle, which reads with Win32, overlapped on newer
// Windows - works whichever of its methods are rewritten.
namespace FileLoad {

class AsyncFileHandle;

// Called (on the main thread) when an operation completes, if its event is in the handle's callback mask:
// `event` is the operation (1 open, 3 read, 6 close, ...), `value` its result.
using IoCompletionHook = void(__cdecl*)(AsyncFileHandle* file, int event, int value, void* arg0, void* arg1);

// Operations, also the events passed to callbacks.
enum AsyncOperation : int { kAsyncIdle = 0, kAsyncOpen = 1, kOpReadFile = 3, kAsyncClose = 6, kAsyncCancel = 7 };

class AsyncFileHandle {
public:
    // Non-virtual methods.
    bool PrepareAll();
    void ChangeHook(IoCompletionHook callback, void* arg0, void* arg1);
    int GetFileSize();
    bool Cancel();
    bool IsOccupied();
    int TakeIn(void* buffer, int size, int count);
    bool Unseal(const char* name, bool blocking, int priority);
    bool Shut();
    // Virtual slot 1 (overridden by the platform class, which calls this one).
    void IoDoneHandler(int event, bool error, int value, int extra);

    // The virtual methods, by vtable slot.
    enum Bay {
        kDestroy = 0, kSlotNotify = 1, kSlotInit = 2, kSlotOpen = 3, kSlotClose = 4, kSlotFinished = 5,
        kSlotBusy = 6, kSlotAtEnd = 7, kSlotPriority = 8, kSlotStreamMode = 9, kSlotTarget = 10,
        kSlotBufSize = 11, kSlotBlockMode = 12, kSlotLoadAll = 13, kSlotRead = 14, kSlotWrite = 15,
        kSlotReadLine = 16, kSlotSeek = 17, kPlatCancel = 18,
    };
    template <typename R, typename... A>
    R Virtual(int slot, A... args)
    {
        auto function = reinterpret_cast<R(__thiscall*)(AsyncFileHandle*, A...)>(m_vtable[slot]);
        return function(this, args...);
    }

    void** m_vtable;
    int m_outcomeVal;            // result of the last operation (size read, error, ...)
    IoCompletionHook m_callback;
    void* m_arg0;
    void* m_arg1;
    uint32_t m_callbackMask; // bit N set: operation N calls back (all, by default)
    int m_operation;         // the operation in progress (AsyncOperation)
    int m_unknown1C;
    bool m_feed;
    bool m_isBlocking;
    int m_bufferSize;
    int m_rank;
    volatile int m_payloadBytes; // -1 until the platform knows it
    int m_anchorPoint;
    void* m_preFile;         // Pre::ArchiveFile::FileInfo* when the file is inside a loaded .pre archive
    volatile int m_inFlightOps;
};

// What the CAsyncFileHandle constructor does (defaults, base vtable, the base class's platform init).
void ConstructAsyncFileHandle(AsyncFileHandle* file);
// The PC handle's platform methods, in vtable slot order where they override (for tests).
struct PcAsyncMethods {
    void* onIoDone;
    void* init;
    void* open;
    void* close;
    void* is_done;
    void* is_busy;
    void* is_eof;
    void* load;
    void* read;
};
PcAsyncMethods PcAsyncFileHandleMethods();
// Whether overlapped I/O is used (chosen at start-up; Windows NT 6 and later).
bool& AsyncUseOverlappedIo();

// The engine module whose per-frame task polls the handles and runs the callbacks: created (or another
// reference taken) by CreateAsyncLoaderModule, released by DestroyAsyncLoaderModule.
struct AsyncLoaderModule;
AsyncLoaderModule* CreateAsyncLoaderModule(bool create);
void DestroyAsyncLoaderModule();
void AsyncPollHandles();

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
inline void AsyncSetCallback(AsyncFileHandle* file, IoCompletionHook callback, void* arg0, void* arg1)
{
    file->ChangeHook(callback, arg0, arg1);
}
inline void StartBackgroundRead(AsyncFileHandle* file, void* buffer, int size) { file->TakeIn(buffer, 1, size); }
inline void AsyncWaitForIO() { AsyncWaitForIO(false); }
// Whether asynchronous loading can be used at all (a stub: always true on PC).
// (0x005B6570: a jump to the folded stub returning true)
inline bool AsyncLoadingAvailable() { return true; }

// Loading from a CD cache: both are stubs on PC (size 0, load fails) - the folded stubs at 0x005BA1D0 (returns 0)
// and 0x005B9E50 (returns false).
inline int CachedFileSize(const char*) { return 0; }
inline bool LoadCachedFile(const char*, void*) { return false; }

// The old name, for existing callers.
using AsyncFileRef = AsyncFileHandle;

} // namespace File

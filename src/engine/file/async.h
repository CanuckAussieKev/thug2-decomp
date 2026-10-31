#pragma once

#include <cstdint>

namespace pre {
class ArchiveFile;
}

// The asynchronous file system: up to 16 open file handles, with reads completed by a platform I/O thread and
// their callbacks run later on the main thread (WaitForIO / ExecuteCallbacks). Files inside loaded .pre
// archives are served directly from memory instead.
//
// AsyncFileHandle is the game's CAsyncFileHandle. Its virtual methods are called through the object's own
// (game) vtable, so the platform subclass - CPcAsyncFileHandle, which reads with Win32, overlapped on newer
// Windows - works whichever of its methods are rewritten.
namespace file {

class AsyncFileHandle;

// Called (on the main thread) when an operation completes, if its event is in the handle's callback mask:
// `event` is the operation (1 open, 3 read, 6 close, ...), `value` its result.
using IoCompletionHook = void(__cdecl*)(AsyncFileHandle* file, int event, int value, void* arg0, void* arg1);

// Operations, also the events passed to callbacks.
enum AsyncOperation : int { kAsyncIdle = 0, kAsyncOpen = 1, kOpReadFile = 3, kAsyncClose = 6, kAsyncCancel = 7 };

class AsyncFileHandle {
public:
    // Non-virtual methods.
    bool prepare_all();
    void change_hook(IoCompletionHook callback, void* arg0, void* arg1);
    int get_file_size();
    bool get_cancel();
    bool is_occupied();
    int get_take_in(void* buffer, int size, int count);
    bool unseal(const char* name, bool blocking, int priority);
    bool shut();
    // Virtual slot 1 (overridden by the platform class, which calls this one).
    void io_done_handler(int event, bool error, int value, int extra);

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
        auto function = reinterpret_cast<R(__thiscall*)(AsyncFileHandle*, A...)>(vtable_[slot]);
        return function(this, args...);
    }

    void** vtable_;
    int outcome_val_;            // result of the last operation (size read, error, ...)
    IoCompletionHook callback_;
    void* arg0_;
    void* arg1_;
    uint32_t callback_mask_; // bit N set: operation N calls back (all, by default)
    int operation_;         // the operation in progress (AsyncOperation)
    int unknown1c_;
    bool feed_;
    bool is_blocking_;
    int buffer_size_;
    int rank_;
    volatile int payload_bytes_; // -1 until the platform knows it
    int anchor_point_;
    void* pre_file_;         // Pre::ArchiveFile::FileInfo* when the file is inside a loaded .pre archive
    volatile int in_flight_ops_;
};

// What the CAsyncFileHandle constructor does (defaults, base vtable, the base class's platform init).
void construct_async_file_handle(AsyncFileHandle* file);
// The PC handle's platform methods, in vtable slot order where they override (for tests).
struct PcAsyncMethods {
    void* on_io_done_;
    void* init;
    void* open;
    void* close;
    void* is_done_;
    void* is_busy_;
    void* is_eof_;
    void* load;
    void* read;
};
PcAsyncMethods pc_async_file_handle_methods();
// Whether overlapped I/O is used (chosen at start-up; Windows NT 6 and later).
bool& async_use_overlapped_io();

// The engine module whose per-frame task polls the handles and runs the callbacks: created (or another
// reference taken) by create_async_loader_module, released by destroy_async_loader_module.
struct AsyncLoaderModule;
AsyncLoaderModule* create_async_loader_module(bool create);
void destroy_async_loader_module();
void async_poll_handles();

// The static side (CAsyncFileLoader).
void async_init();
bool async_busy();
AsyncFileHandle* async_open(const char* name, bool blocking, int priority);
bool async_close(AsyncFileHandle* file);
// Run the callbacks of operations completed since the last call.
void async_execute_callbacks();
// Wait for an I/O completion and run the callbacks; with `all`, until no handle is busy.
void async_wait_for_io(bool all);
AsyncFileHandle* async_get_handle();
bool async_free_handle(AsyncFileHandle* file);
void async_add_callback(AsyncFileHandle* file, int event, int value);
void async_swap_callback_lists();

// As the file loading code uses it.
inline AsyncFileHandle* async_open(const char* name) { return async_open(name, false, 100); }
inline int async_get_file_size(AsyncFileHandle* file) { return file->get_file_size(); }
inline void async_set_callback_mask(AsyncFileHandle* file, uint32_t mask) { file->callback_mask_ = mask; }
inline void async_set_callback(AsyncFileHandle* file, IoCompletionHook callback, void* arg0, void* arg1)
{
    file->change_hook(callback, arg0, arg1);
}
inline void start_background_read(AsyncFileHandle* file, void* buffer, int size) { file->get_take_in(buffer, 1, size); }
inline void async_wait_for_io() { async_wait_for_io(false); }
// Whether asynchronous loading can be used at all (a stub: always true on PC).
// (0x005B6570: a jump to the folded stub returning true)
inline bool async_loading_available() { return true; }

// Loading from a CD cache: both are stubs on PC (size 0, load fails) - the folded stubs at 0x005BA1D0 (returns 0)
// and 0x005B9E50 (returns false).
inline int cached_file_size(const char*) { return 0; }
inline bool load_cached_file(const char*, void*) { return false; }

// The old name, for existing callers.
using AsyncFileRef = AsyncFileHandle;

} // namespace File

#pragma once

#include <memory>
#include <cstdint>

#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/sys/game_module.h"

namespace pre {
class ArchiveFile;
}

// The asynchronous file system: up to 16 open file handles, with reads completed by a platform I/O thread and
// their callbacks run later on the main thread (WaitForIO / ExecuteCallbacks). Files inside loaded .pre
// archives are served directly from memory instead.
//
// AsyncFileHandle is the handle every platform shares; its virtual os_* methods are the
// platform's side - debug stubs in the base, Win32 reads (overlapped on newer Windows) in PcAsyncFileHandle.
namespace file {

class AsyncFileHandle;

// Called (on the main thread) when an operation completes, if its event is in the handle's callback mask:
// `event` is the operation (1 open, 3 read, 6 close, ...), `value` its result.
using IoCompletionHook = void (*)(AsyncFileHandle* file, int event, int value, void* arg0, void* arg1);

// Operations, also the events passed to callbacks.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
enum AsyncOperation : uint8_t { kAsyncIdle = 0, kAsyncOpen = 1, kOpReadFile = 3, kAsyncClose = 6, kAsyncCancel = 7 };

class AsyncFileHandle : public memory::HeapObject {
public:
    // 0x005B69A0: the defaults, then the base's platform init (a debug stub).
    AsyncFileHandle();
    // 0x005B66C0 / 0x005B6370
    virtual ~AsyncFileHandle() = default;
    AsyncFileHandle(const AsyncFileHandle&) = delete;
    AsyncFileHandle& operator=(const AsyncFileHandle&) = delete;

    // Non-virtual methods: each serves a file inside a loaded .pre archive itself, else asks the platform.
    bool prepare_all();
    void change_hook(IoCompletionHook callback, void* arg0, void* arg1);
    int get_file_size();
    bool get_cancel();
    bool is_occupied();
    int get_take_in(void* buffer, int size, int count);
    bool unseal(const char* name, bool blocking, int priority);
    bool shut();

    // Virtual slot 1 (0x005B69F0): an operation finished - its callback queued, or it's counted done. The platform's
    // override keeps the size and position up to date first.
    virtual void io_done_handler(int event, bool error, int value, int extra);
    // The platform's side (slots 2-7, 13, 14 and 18; the base's print "STUB: CAsyncFileHandle::<name>" and fail).
    // The original's slots 8-12 and 15-17 (SetPriority, SetStream, SetDestination, SetBufferSize, SetBlocking,
    // Write, GetS, Seek) are never called: the PC's are do-nothing stubs, so they're left out.
    virtual bool os_open_setup();                                 // [2] Init
    virtual bool os_open_file(const char* name);                  // [3] Open
    virtual bool os_close_file();                                 // [4] Close
    virtual bool os_read_finished();                              // [5] IsDone: whether the I/O in flight has finished
    virtual bool os_read_pending();                               // [6] IsBusy
    virtual bool os_at_end();                                     // [7] IsEOF
    virtual int os_load_whole(void* buffer);                      // [13] Load: the whole file
    virtual int os_read_chunk(void* buffer, int size, int count); // [14] Read
    virtual bool os_cancel();                                     // [18] Cancel (the PC uses the base's)

    // The fields other code reads and sets.
    [[nodiscard]] int outcome_val() const { return outcome_val_; }
    void set_outcome_val(int value) { outcome_val_ = value; }
    [[nodiscard]] const IoCompletionHook& callback() const { return callback_; }
    void set_callback(const IoCompletionHook& value) { callback_ = value; }
    [[nodiscard]] void* arg0() const { return arg0_; }
    void set_arg0(void* value) { arg0_ = value; }
    [[nodiscard]] void* arg1() const { return arg1_; }
    void set_arg1(void* value) { arg1_ = value; }
    uint32_t& callback_mask() { return callback_mask_; }
    [[nodiscard]] uint32_t callback_mask() const { return callback_mask_; }
    [[nodiscard]] int operation() const { return operation_; }
    void set_operation(int value) { operation_ = value; }
    void set_unknown1c(int value) { unknown1c_ = value; }
    void set_feed(bool value) { feed_ = value; }
    [[nodiscard]] bool is_blocking() const { return is_blocking_; }
    void set_is_blocking(bool value) { is_blocking_ = value; }
    void set_buffer_size(int value) { buffer_size_ = value; }
    void set_rank(int value) { rank_ = value; }
    [[nodiscard]] int payload_bytes() const { return payload_bytes_; }
    void set_payload_bytes(int value) { payload_bytes_ = value; }
    int& anchor_point() { return anchor_point_; }
    [[nodiscard]] int anchor_point() const { return anchor_point_; }
    void set_pre_file(void* value) { pre_file_ = value; }
    volatile int& in_flight_ops() { return in_flight_ops_; }
    [[nodiscard]] int in_flight_ops() const { return in_flight_ops_; }

private:
    int outcome_val_{}; // result of the last operation (size read, error, ...)
    IoCompletionHook callback_{};
    void* arg0_{};
    void* arg1_{};
    uint32_t callback_mask_{}; // bit N set: operation N calls back (all, by default)
    int operation_{};          // the operation in progress (AsyncOperation)
    int unknown1c_{};
    bool feed_{};
    bool is_blocking_{};
    int buffer_size_{};
    int rank_{};
    volatile int payload_bytes_{}; // -1 until the platform knows it
    int anchor_point_{};
    void* pre_file_{}; // Pre::ArchiveFile::FileInfo* when the file is inside a loaded .pre archive
    volatile int in_flight_ops_{};

    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(AsyncFileHandle, callback_mask_) == 0x14 && offsetof(AsyncFileHandle, is_blocking_) == 0x21 &&
                          offsetof(AsyncFileHandle, payload_bytes_) == 0x2C &&
                          offsetof(AsyncFileHandle, in_flight_ops_) == 0x38,
                      "layout must match the original");
    }
};

// Whether overlapped I/O is used (chosen at start-up; Windows NT 6 and later).
bool& async_use_overlapped_io();

// The engine module whose per-frame task polls the handles and runs the callbacks (vtable 0x00658158): created (or
// another reference taken) by create_async_loader_module, released by destroy_async_loader_module.
class AsyncLoaderModule final : public modules::PartUnit {
public:
    AsyncLoaderModule(const AsyncLoaderModule&) = delete;
    AsyncLoaderModule& operator=(const AsyncLoaderModule&) = delete;
    // 0x005B6BC0
    AsyncLoaderModule();
    // 0x005B6660 / 0x005B6940
    ~AsyncLoaderModule() override { task_.reset(); }
    // 0x005B6970: the module starts - its task joins the logic tasks.
    void start_callback() override;
    // (the original's slot is the skate module's stop, 0x00564C50 - folded: the task at +0x14 out of its list)
    void stop_callback() override { task_->drop_out(); }

private:
    std::unique_ptr<tasks::Task<AsyncLoaderModule>> task_; // +0x14 running async_loader_task
};
static_assert(sizeof(AsyncLoaderModule) == 0x18, "allocated as 0x18 bytes by the original");
AsyncLoaderModule* create_async_loader_module(bool create);
void destroy_async_loader_module();
void async_poll_handles();

// The static side: the pool of handles and the callbacks.
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
inline void async_set_callback_mask(AsyncFileHandle* file, uint32_t mask) { file->callback_mask() = mask; }
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

} // namespace file

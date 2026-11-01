#include "engine/file/async.h"

#include <memory>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/file/file_load.h"
#include "engine/file/archive.h"
#include "frontend/data_overlay.h"
#include "engine/sys/game_module.h"
#include "engine/assets/resource_registry.h"
#include "engine/sys/mlp.h"
#include "engine/core/owner.h"
#include "platform/compiler.h"

namespace file {

static_assert(offsetof(AsyncFileHandle, callback_mask_) == 0x14 && offsetof(AsyncFileHandle, is_blocking_) == 0x21 &&
                  offsetof(AsyncFileHandle, payload_bytes_) == 0x2C &&
                  offsetof(AsyncFileHandle, in_flight_ops_) == 0x38,
              "layout must match the original");

namespace {

constexpr int kMaxHandles = 16;

AsyncFileHandle* g_pool[kMaxHandles];
int g_handles_in_use;
int g_pending_reads;
bool g_io_completed;
int g_current_list;
memory::owner<AsyncLoaderModule*> g_loader_module;
uint32_t g_loader_module_refs;
bool g_use_overlapped_io;

// Shared with the platform I/O thread: plain (volatile) accesses, as in the original.
AsyncFileHandle** slab_bank() { return g_pool; }
int& handles_in_use() { return g_handles_in_use; }
volatile int& pending_io_counter() { return g_pending_reads; }
volatile bool& io_completed() { return g_io_completed; }

// Completed operations waiting for their callbacks: two lists, one filled while the other is run.
struct PendingCallback {
    AsyncFileHandle* file;
    int event;
    int value;
};

PendingCallback g_pending_callbacks[2][kMaxHandles];
int g_callback_counts[2];

PendingCallback (&callback_lists())[2][kMaxHandles] { return g_pending_callbacks; }
int (&callback_counts())[2] { return g_callback_counts; }
int& current_list() { return g_current_list; }

// The handle fields every allocation resets (as the original's Init and get_file_handle do).
void reset_fields(AsyncFileHandle* file)
{
    file->payload_bytes_ = -1;
    file->anchor_point_ = -1;
    file->callback_mask_ = 0xFFFFFFFF;
    file->in_flight_ops_ = 0;
    file->callback_ = nullptr;
    file->arg0_ = nullptr;
    file->arg1_ = nullptr;
    file->operation_ = kAsyncIdle;
    file->unknown1c_ = 0;
    file->feed_ = false;
    file->is_blocking_ = true;
    file->buffer_size_ = 0x10000;
    file->rank_ = 100;
    file->outcome_val_ = 0;
    file->pre_file_ = nullptr;
}

// Report an operation served from a .pre archive, as the platform does when its I/O completes.
void complete_from_archive(AsyncFileHandle* file)
{
    pending_io_counter() = pending_io_counter() + 1;
    file->in_flight_ops_ = file->in_flight_ops_ + 1;
    file->io_done_handler(file->operation_, false, file->outcome_val_, 0);
}

} // namespace

// The platform (PC) side, below.
static void platform_init();
static DWORD WINAPI async_io_thread(void*);

// 0x005B69A0 (the constructor: in it the base's own init runs, a debug stub - as the original's call through the
// base's table)
AsyncFileHandle::AsyncFileHandle()
{
    reset_fields(this);
    os_open_setup();
}

// --- CAsyncFileHandle ----------------------------------------------------------------------------------

// 0x005B66F0 (Init: back to defaults, then the platform's init)
bool AsyncFileHandle::prepare_all()
{
    reset_fields(this);
    return os_open_setup();
}

// 0x005B6380
void AsyncFileHandle::change_hook(IoCompletionHook callback, void* arg0, void* arg1)
{
    callback_ = callback;
    arg0_ = arg0;
    arg1_ = arg1;
}

// 0x005B63A0
int AsyncFileHandle::get_file_size()
{
    if (pre_file_) {
        outcome_val_ = pre::get_file_size(static_cast<pre::ArchiveFile::FileInfo*>(pre_file_));
        return outcome_val_;
    }
    while (payload_bytes_ < 0) {
        // the platform's open sets it (on the I/O thread when overlapped)
    }
    return payload_bytes_;
}

// 0x005B63D0
bool AsyncFileHandle::get_cancel()
{
    operation_ = kAsyncCancel;
    if (pre_file_) {
        outcome_val_ = -1;
        return true;
    }
    return os_cancel();
}

// 0x005B6730
bool AsyncFileHandle::is_occupied() { return in_flight_ops_ > 0; }

// 0x005B6740
int AsyncFileHandle::get_take_in(void* buffer, int size, int count)
{
    operation_ = kOpReadFile;
    if (pre_file_) {
        outcome_val_ = pre::get_take_in(buffer, size, count, static_cast<pre::ArchiveFile::FileInfo*>(pre_file_));
        complete_from_archive(this);
        return outcome_val_;
    }
    return os_read_chunk(buffer, size, count);
}

// 0x005B67B0
bool AsyncFileHandle::unseal(const char* name, bool blocking, int priority)
{
    rank_ = priority;
    is_blocking_ = blocking;
    operation_ = kAsyncOpen;
    // Not original - the data overlay (frontend/data_overlay.h): a file of the data overlay isn't taken from an
    // archive (the platform's open reads the overlay's).
    // Original behaviour: a loaded archive's copy first.
    char overlay[260];
    if (pre::hub() && !frontend::data_overlay::get_overlay_path(name, overlay)) {
        pre_file_ = pre::unseal(name, "rb", true);
        if (pre_file_) {
            outcome_val_ = 1;
            is_blocking_ = true;
            payload_bytes_ = static_cast<pre::ArchiveFile::FileInfo*>(pre_file_)->size;
            complete_from_archive(this);
            return outcome_val_ != 0;
        }
    }
    return os_open_file(name);
}

// 0x005B6840
bool AsyncFileHandle::shut()
{
    operation_ = kAsyncClose;
    if (pre_file_) {
        outcome_val_ = pre::shut(static_cast<pre::ArchiveFile::FileInfo*>(pre_file_), true);
        complete_from_archive(this);
        return outcome_val_ != 0;
    }
    return os_close_file();
}

// 0x005B69F0
void AsyncFileHandle::io_done_handler(int event, bool error, int value, int /*extra*/)
{
    outcome_val_ = value;
    io_completed() = true;
    if (callback_ && !error && (callback_mask_ & (1u << (event & 31)))) {
        async_add_callback(this, event, value); // counted done when the callback has run
        return;
    }
    pending_io_counter() = pending_io_counter() - 1;
    // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): the original's, as written
    if ((in_flight_ops_ = in_flight_ops_ - 1) == 0)
        operation_ = kAsyncIdle;
}

// --- the loader (static) --------------------------------------------------------------------------------

// 0x005B6560
void async_init()
{
    handles_in_use() = 0;
    platform_init();
}

// 0x005B6580
bool async_busy() { return pending_io_counter() > 0; }

// 0x005B6590
bool async_free_handle(AsyncFileHandle* file)
{
    for (int i = 0; i < kMaxHandles; ++i) {
        if (slab_bank()[i] == file) {
            // Swap it with the last handle in use.
            int last = --handles_in_use();
            slab_bank()[i] = slab_bank()[last];
            slab_bank()[last] = file;
            return true;
        }
    }
    return false;
}

// 0x005B65D0
void async_add_callback(AsyncFileHandle* file, int event, int value)
{
    int list = current_list();
    int index = callback_counts()[list]++;
    callback_lists()[list][index] = {file, event, value};
}

// 0x005B9770 (platform: start filling the other list)
void async_swap_callback_lists()
{
    current_list() ^= 1;
    io_completed() = false;
}

// 0x005B68C0
void async_execute_callbacks()
{
    async_swap_callback_lists();
    int list = current_list() ^ 1; // the one that was being filled
    int count = callback_counts()[list];
    for (int i = 0; i < count; ++i) {
        const PendingCallback& pending = callback_lists()[list][i];
        AsyncFileHandle* file = pending.file;
        file->callback_(file, pending.event, pending.value, file->arg0_, file->arg1_);
        pending_io_counter() = pending_io_counter() - 1;
        // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): the original's, as written
        if ((file->in_flight_ops_ = file->in_flight_ops_ - 1) == 0)
            file->operation_ = kAsyncIdle;
    }
    callback_counts()[list] = 0;
}

// 0x005B6A50
void async_wait_for_io(bool all)
{
    for (;;) {
        debug::printf("CAsyncFileLoader waiting for io completion: busy count %d completion %d\n", pending_io_counter(),
                      io_completed());
        while (!io_completed()) {
        }
        debug::printf("CAsyncFileLoader got completion: busy count %d completion %d\n", pending_io_counter(),
                      io_completed());
        async_execute_callbacks();
        if (!all)
            return;
        int busy = 0;
        while (busy < kMaxHandles && slab_bank()[busy]->in_flight_ops_ <= 0)
            ++busy;
        if (busy == kMaxHandles)
            return;
        debug::printf("CAsyncFileLoader still needs to wait for file handle %d\n", busy);
    }
}

// 0x005B6AE0
AsyncFileHandle* async_get_handle()
{
    int first_free = handles_in_use();
    if (first_free >= kMaxHandles)
        return nullptr;
    // The next handle may still be finishing an operation: use a later idle one instead, or wait.
    if (slab_bank()[first_free]->in_flight_ops_ > 0) {
        int idle = first_free + 1;
        while (idle < kMaxHandles && slab_bank()[idle]->in_flight_ops_ > 0)
            ++idle;
        if (idle < kMaxHandles) {
            AsyncFileHandle* swap = slab_bank()[idle];
            slab_bank()[idle] = slab_bank()[first_free];
            slab_bank()[first_free] = swap;
        } else {
            AsyncFileHandle* busy = slab_bank()[first_free];
            do {
                async_execute_callbacks();
                first_free = handles_in_use();
            } while (busy->in_flight_ops_ > 0);
        }
    }
    AsyncFileHandle* file = slab_bank()[first_free];
    reset_fields(file);
    file->os_open_setup();
    return slab_bank()[handles_in_use()++];
}

// 0x005B6C60
AsyncFileHandle* async_open(const char* name, bool blocking, int priority)
{
    AsyncFileHandle* file = async_get_handle();
    if (!file)
        return nullptr;
    if (file->unseal(name, blocking, priority))
        return file;
    async_free_handle(file);
    return nullptr;
}

// 0x005B68A0
bool async_close(AsyncFileHandle* file)
{
    bool result = file->shut();
    async_free_handle(file);
    return result;
}

} // namespace file

namespace file {

namespace {

bool stub_bool(const char* message)
{
    debug::printf(message);
    return false;
}
int stub_int(const char* message)
{
    debug::printf(message);
    return 0;
}

} // namespace

// The base's platform methods: debug stubs, each printing its name.
bool AsyncFileHandle::os_open_setup() { return stub_bool("STUB: CAsyncFileHandle::Init\n"); }
bool AsyncFileHandle::os_open_file(const char*) { return stub_bool("STUB: CAsyncFileHandle::Open\n"); }
bool AsyncFileHandle::os_close_file() { return stub_bool("STUB: CAsyncFileHandle::Close\n"); }
bool AsyncFileHandle::os_read_finished() { return stub_bool("STUB: CAsyncFileHandle::IsDone\n"); }
bool AsyncFileHandle::os_read_pending() { return stub_bool("STUB: CAsyncFileHandle::IsBusy\n"); }
bool AsyncFileHandle::os_at_end() { return stub_bool("STUB: CAsyncFileHandle::IsEOF\n"); }
int AsyncFileHandle::os_load_whole(void*) { return stub_int("STUB: CAsyncFileHandle::Load\n"); }
int AsyncFileHandle::os_read_chunk(void*, int, int) { return stub_int("STUB: CAsyncFileHandle::Read\n"); }
bool AsyncFileHandle::os_cancel() { return stub_bool("STUB: CAsyncFileHandle::Cancel\n"); }

} // namespace file

// --- PcAsyncFileHandle: the PC platform handle (vtable 0x006583D8) --------------------------------------

namespace file {

namespace {

constexpr int kErrorIoPending = 997;    // ERROR_IO_PENDING
constexpr int kErrorHandleEof = 38;     // ERROR_HANDLE_EOF
constexpr DWORD kStatusPending = 0x103; // OVERLAPPED.Internal while the I/O is in progress

// Overlapped (truly asynchronous, unbuffered) reads are used on Windows NT 6 (Vista) and later.
bool& use_overlapped_io() { return g_use_overlapped_io; }

class PcAsyncFileHandle final : public AsyncFileHandle {
public:
    // 0x005B9B80's construction: the base's, then no staging buffer and no file.
    PcAsyncFileHandle()
    {
        unknown7c_ = 0;
        destination_ = nullptr;
        staging_bytes_ = nullptr;
        file_ref_ = INVALID_HANDLE_VALUE;
    }
    // 0x005B97B0 / 0x005B9640
    ~PcAsyncFileHandle() override = default;

    void io_done_handler(int event, bool error, int value, int extra) override;
    bool os_open_setup() override;
    bool os_open_file(const char* name) override;
    bool os_close_file() override;
    bool os_read_finished() override;
    bool os_read_pending() override;
    bool os_at_end() override;
    int os_load_whole(void* buffer) override;
    int os_read_chunk(void* buffer, int size, int count) override;

    uint8_t unknown3c_[0x7C - 0x3C];
    int unknown7c_;
    void* destination_; // where a read through staging_bytes_ goes
    void* staging_bytes_;
    HANDLE file_ref_;
    OVERLAPPED async_ctl_;
};

} // namespace
static_assert(offsetof(PcAsyncFileHandle, file_ref_) == 0x88 && sizeof(PcAsyncFileHandle) == 0xA0,
              "layout must match the original");

// 0x005B9950 (keeps the size and position up to date, then the base class's)
void PcAsyncFileHandle::io_done_handler(int event, bool error, int value, int extra)
{
    switch (event) {
    case kAsyncIdle:
        return;
    case kAsyncOpen:
        if (value >= 0 && !error)
            payload_bytes_ = extra;
        break;
    case 2:
    case kOpReadFile:
        if (!error && staging_bytes_ && value > 0)
            std::memcpy(destination_, staging_bytes_, static_cast<size_t>(value));
        [[fallthrough]];
    case 4:
        if (value >= 0) {
            if (!error)
                anchor_point_ += value;
            if (staging_bytes_) {
                memory::dealloc(staging_bytes_);
                destination_ = nullptr;
                staging_bytes_ = nullptr;
            }
        }
        break;
    case 5:
        if (value >= 0 && !error)
            anchor_point_ = value;
        break;
    default:
        break;
    }
    AsyncFileHandle::io_done_handler(event, error, value, extra);
}

// 0x005B9650 (returns false, as the original does - EAX is 0 when it returns)
bool PcAsyncFileHandle::os_open_setup()
{
    unknown7c_ = 0;
    destination_ = nullptr;
    staging_bytes_ = nullptr;
    async_ctl_.Offset = 0;
    async_ctl_.OffsetHigh = 0;
    async_ctl_.hEvent = nullptr;
    return false;
}

// 0x005B97E0
bool PcAsyncFileHandle::os_open_file(const char* name)
{
    anchor_point_ = 0;
    char path[260];
    // Not original - the data overlay: a file of it is opened there.
    // Original behaviour: the data folder's file.
    if (!frontend::data_overlay::get_overlay_path(name, path))
        make_disk_path(reinterpret_cast<char (&)[256]>(path), name);
    pending_io_counter() = pending_io_counter() + 1;
    in_flight_ops_ = in_flight_ops_ + 1;
    bool overlapped = use_overlapped_io();
    file_ref_ =
        CreateFileA(path, GENERIC_READ, overlapped ? FILE_SHARE_READ : 0, nullptr, OPEN_EXISTING,
                    overlapped ? FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING : FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ref_ == INVALID_HANDLE_VALUE)
        return false; // (the busy counts stay raised, as in the original)
    payload_bytes_ = static_cast<int>(::GetFileSize(file_ref_, nullptr));
    payload_bytes_ = (payload_bytes_ + 0x7FF) & ~0x7FF; // whole 2 KB sectors (unbuffered reads)
    io_done_handler(operation_, false, reinterpret_cast<int>(file_ref_), static_cast<int>(payload_bytes_));
    return true;
}

// 0x005B9AF0
bool PcAsyncFileHandle::os_close_file()
{
    if (file_ref_ == INVALID_HANDLE_VALUE)
        return false;
    pending_io_counter() = pending_io_counter() + 1;
    in_flight_ops_ = in_flight_ops_ + 1;
    CloseHandle(file_ref_);
    file_ref_ = INVALID_HANDLE_VALUE;
    io_done_handler(operation_, false, 0, 0);
    return true;
}

// 0x005B9680 (called by the I/O thread while the handle is busy)
bool PcAsyncFileHandle::os_read_finished()
{
    if (!use_overlapped_io()) {
        outcome_val_ = 0;
        return true;
    }
    bool done = false;
    if (outcome_val_ == kErrorIoPending) {
        done = async_ctl_.Internal != kStatusPending;
        if (done) {
            DWORD bytes = 0;
            GetOverlappedResult(file_ref_, &async_ctl_, &bytes, FALSE);
            outcome_val_ = 0;
            io_done_handler(operation_, false, static_cast<int>(bytes), 0);
        }
    }
    return done;
}

// 0x005B9700
bool PcAsyncFileHandle::os_read_pending() { return outcome_val_ == kErrorIoPending; }

// 0x005B9710
bool PcAsyncFileHandle::os_at_end() { return outcome_val_ == kErrorHandleEof; }

// 0x005B9720: read the whole file from the start
int PcAsyncFileHandle::os_load_whole(void* buffer)
{
    async_ctl_.Offset = 0;
    async_ctl_.OffsetHigh = 0;
    async_ctl_.hEvent = nullptr;
    return os_read_chunk(buffer, 1, get_file_size());
}

// 0x005B9A40
int PcAsyncFileHandle::os_read_chunk(void* buffer, int size, int count)
{
    auto length = static_cast<DWORD>((size * count + 0x7FF) & ~0x7FF); // whole sectors
    pending_io_counter() = pending_io_counter() + 1;
    in_flight_ops_ = in_flight_ops_ + 1;
    DWORD bytes = 0;
    if (!ReadFile(file_ref_, buffer, length, &bytes, use_overlapped_io() ? &async_ctl_ : nullptr)) {
        outcome_val_ = static_cast<int>(GetLastError());
        if (outcome_val_ == kErrorIoPending && is_blocking_) {
            while (!os_read_finished()) {
            }
        }
        return 0;
    }
    // Finished at once: counted done here, without a callback (as in the original).
    pending_io_counter() = pending_io_counter() - 1;
    in_flight_ops_ = in_flight_ops_ - 1;
    return 0;
}

// 0x005B9B40: the I/O thread - every 66 ms, lets each busy handle check whether its I/O has finished
DWORD WINAPI async_io_thread(void*)
{
    for (;;) {
        Sleep(66);
        for (int i = 0; i < kMaxHandles; ++i) {
            AsyncFileHandle* file = slab_bank()[i];
            if (file && file->is_occupied())
                file->os_read_finished();
        }
    }
}

// 0x005B9B80: create the 16 PC handles (or reset them), start the I/O thread, choose overlapped I/O
void platform_init()
{
    for (int i = 0; i < kMaxHandles; ++i) {
        if (slab_bank()[i]) {
            slab_bank()[i]->prepare_all();
            continue;
        }
        slab_bank()[i] = new PcAsyncFileHandle;
    }
    CreateThread(nullptr, 0x4000, async_io_thread, nullptr, 0, nullptr);

    OSVERSIONINFOA version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    THUG2_MSVC_WARNING(suppress : 4996) // GetVersionExA: deprecated, but what the original asks
    if (GetVersionExA(&version))
        use_overlapped_io() = version.dwPlatformId == VER_PLATFORM_WIN32_NT && version.dwMajorVersion > 5;
}

bool& async_use_overlapped_io() { return use_overlapped_io(); }

} // namespace file

// --- the async file loader module ------------------------------------------------------------------------
// An engine module (modules::PartUnit) whose per-frame task polls the platform handles and runs the completed I/O
// callbacks. One instance, reference counted (0x007CF190 / 0x007CF194).

namespace file {

namespace {

AsyncLoaderModule*& loader_module() { return g_loader_module; }
uint32_t& loader_module_refs() { return g_loader_module_refs; }

void register_logic_task(tasks::SchedulerTask* task)
{
    // The main loop manager's logic task list.
    assets::g_main_loop_manager->logic_tasks_.queue_job(task);
}

} // namespace

// 0x005B9780: give each busy handle the chance to complete (also done by the I/O thread)
void async_poll_handles()
{
    for (int i = 0; i < kMaxHandles; ++i) {
        AsyncFileHandle* file = slab_bank()[i];
        if (file && file->is_occupied())
            file->os_read_finished();
    }
}

// 0x005B6990: the module's task, every frame
static void async_loader_task(tasks::Task<AsyncLoaderModule>* /*task*/)
{
    async_poll_handles();
    async_execute_callbacks();
}

// 0x005B6CA0: returns the module (null if it doesn't exist) - the start-up code keeps it
AsyncLoaderModule* create_async_loader_module(bool create)
{
    if (!loader_module() && create) {
        loader_module() = new AsyncLoaderModule;
    }
    ++loader_module_refs();
    return loader_module();
}

// 0x005B6620
void destroy_async_loader_module()
{
    if (loader_module_refs() > 1) {
        --loader_module_refs();
        return;
    }
    delete loader_module();
    loader_module() = nullptr;
    loader_module_refs() = 0;
}

// 0x005B6BC0
AsyncLoaderModule::AsyncLoaderModule()
{
    task_ = std::make_unique<tasks::Task<AsyncLoaderModule>>(&async_loader_task, this, 0);
}

// 0x005B6970
void AsyncLoaderModule::start_callback() { register_logic_task(task_.get()); }

} // namespace file

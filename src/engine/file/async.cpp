#include "engine/file/async.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <span>

#include "engine/core/dbg.h"
#include "engine/memory/mem.h"
#include "engine/memory/task_list.h"
#include "engine/file/file_load.h"
#include "engine/file/archive.h"
#include "frontend/data_overlay.h"
#include "engine/assets/resource_registry.h"
#include "engine/sys/mlp.h"
#include <gsl/pointers>
#include <array>
#include "platform/compiler.h"
#include "platform/overlapped.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"

namespace file {

namespace {

constexpr int kMaxHandles = 16;
constexpr uint32_t kAllBits = 0xFFFFFFFF;    // the callback mask that lets every event call back
constexpr int kDefaultBufferBytes = 0x10000;
constexpr int kEventBitMask = 31;            // an event number's bit in the callback mask
constexpr int kEventSeek = 5;                // io_done_handler's event that sets the anchor point
constexpr int kSectorMask = 0x7FF;           // reads are in whole 2 KB sectors (unbuffered I/O)
constexpr int kIoPollMs = 66;                // the I/O thread's period
constexpr int kIoThreadStackBytes = 0x4000;
constexpr int kWindowsXpMajorVersion = 5;    // overlapped I/O only after it
constexpr size_t kGapStart = 0x3C;           // PcAsyncFileHandle: the bytes not understood
constexpr size_t kGapEnd = 0x7C;
constexpr size_t kFileRefOffset = 0x88;
constexpr size_t kPcHandleBytes = 0xA0;

// Completed operations waiting for their callbacks: two lists, one filled while the other is run.
struct PendingCallback {
    AsyncFileHandle* file;
    int event;
    int value;
};

// NOT ORIGINAL grouping - the asynchronous files: the handles, the reads pending (shared with the platform I/O thread),
// the loader module and the callbacks waiting. Made on first use, never destroyed (async_state()).
struct AsyncState {
    std::array<AsyncFileHandle*, kMaxHandles> pool_{};
    int handles_in_use_{};
    int pending_reads_{};
    bool io_completed_{};
    int current_list_{};
    std::unique_ptr<AsyncLoaderModule> loader_module_;
    uint32_t loader_module_refs_{};
    bool use_overlapped_io_{};
    std::array<std::array<PendingCallback, kMaxHandles>, 2> pending_callbacks_{};
    std::array<int, 2> callback_counts_{};
};

AsyncState& async_state()
{
    static core::NoDestroy<AsyncState> state;
    return state.get();
}

// Shared with the platform I/O thread: plain (volatile) accesses, as in the original.
std::array<AsyncFileHandle*, kMaxHandles>& slab_bank() { return async_state().pool_; }
int& handles_in_use() { return async_state().handles_in_use_; }
volatile int& pending_io_counter() { return async_state().pending_reads_; }
volatile bool& io_completed() { return async_state().io_completed_; }

std::array<std::array<PendingCallback, kMaxHandles>, 2>& callback_lists() { return async_state().pending_callbacks_; }
std::array<int, 2>& callback_counts() { return async_state().callback_counts_; }
int& current_list() { return async_state().current_list_; }

// The handle fields every allocation resets (as the original's Init and get_file_handle do).
void reset_fields(AsyncFileHandle* file)
{
    file->set_payload_bytes(-1);
    file->anchor_point() = -1;
    file->callback_mask() = kAllBits;
    file->in_flight_ops() = 0;
    file->set_callback(nullptr);
    file->set_arg0(nullptr);
    file->set_arg1(nullptr);
    file->set_operation(kAsyncIdle);
    file->set_unknown1c(0);
    file->set_feed(false);
    file->set_is_blocking(true);
    file->set_buffer_size(kDefaultBufferBytes);
    file->set_rank(file::kDefaultReadRank);
    file->set_outcome_val(0);
    file->set_pre_file(nullptr);
}

// Report an operation served from a .pre archive, as the platform does when its I/O completes.
void complete_from_archive(AsyncFileHandle* file)
{
    pending_io_counter() = pending_io_counter() + 1;
    file->in_flight_ops() = file->in_flight_ops() + 1;
    file->io_done_handler(file->operation(), false, file->outcome_val(), 0);
}

// The platform (PC) side, below.
void platform_init();
DWORD WINAPI async_io_thread(void* /*unused*/);
} // namespace

// 0x005B69A0 (the constructor: in it the base's own init runs, a debug stub - as the original's call through the
// base's table)
AsyncFileHandle::AsyncFileHandle()
{
    reset_fields(this);
    AsyncFileHandle::os_open_setup(); // (the base's own, by name: a constructor calls no override)
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
bool AsyncFileHandle::is_occupied() const { return in_flight_ops_ > 0; }

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
    std::array<char, kOverlayPathSize> overlay{};
    if (pre::hub() && !frontend::data_overlay::get_overlay_path(name, overlay)) {
        pre_file_ = pre::unseal({.name = name, .mode = "rb"}, true);
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
    if (callback_ && !error && ((callback_mask_ & (1U << (event & kEventBitMask))) != 0U)) {
        async_add_callback(this, event, value); // counted done when the callback has run
        return;
    }
    pending_io_counter() = pending_io_counter() - 1;
    in_flight_ops_ = in_flight_ops_ - 1;
    if (in_flight_ops_ == 0) {
        operation_ = kAsyncIdle;
    }
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
        if (core::at(slab_bank(), i) == file) {
            // Swap it with the last handle in use.
            const int last = --handles_in_use();
            core::at(slab_bank(), i) = core::at(slab_bank(), last);
            core::at(slab_bank(), last) = file;
            return true;
        }
    }
    return false;
}

// 0x005B65D0
void async_add_callback(AsyncFileHandle* file, int event, int value)
{
    const int list = current_list();
    const int index = core::at(callback_counts(), list)++;
    core::at(core::at(callback_lists(), list), index) = {.file = file, .event = event, .value = value};
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
    const int list = current_list() ^ 1; // the one that was being filled
    const int count = core::at(callback_counts(), list);
    for (int i = 0; i < count; ++i) {
        const PendingCallback& pending = core::at(core::at(callback_lists(), list), i);
        AsyncFileHandle* file = pending.file;
        file->callback()(file, pending.event, pending.value, file->arg0(), file->arg1());
        pending_io_counter() = pending_io_counter() - 1;
        file->in_flight_ops() = file->in_flight_ops() - 1;
        if (file->in_flight_ops() == 0) {
            file->set_operation(kAsyncIdle);
        }
    }
    core::at(callback_counts(), list) = 0;
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
        if (!all) {
            return;
        }
        int busy = 0;
        while (busy < kMaxHandles && core::at(slab_bank(), busy)->in_flight_ops() <= 0) {
            ++busy;
        }
        if (busy == kMaxHandles) {
            return;
        }
        debug::printf("CAsyncFileLoader still needs to wait for file handle %d\n", busy);
    }
}

// 0x005B6AE0
AsyncFileHandle* async_get_handle()
{
    int first_free = handles_in_use();
    if (first_free >= kMaxHandles) {
        return nullptr;
    }
    // The next handle may still be finishing an operation: use a later idle one instead, or wait.
    if (core::at(slab_bank(), first_free)->in_flight_ops() > 0) {
        int idle = first_free + 1;
        while (idle < kMaxHandles && core::at(slab_bank(), idle)->in_flight_ops() > 0) {
            ++idle;
        }
        if (idle < kMaxHandles) {
            AsyncFileHandle* swap = core::at(slab_bank(), idle);
            core::at(slab_bank(), idle) = core::at(slab_bank(), first_free);
            core::at(slab_bank(), first_free) = swap;
        } else {
            const AsyncFileHandle* busy = core::at(slab_bank(), first_free);
            for (bool again = true; again; again = busy->in_flight_ops() > 0) {
                async_execute_callbacks();
                first_free = handles_in_use();
            }
        }
    }
    AsyncFileHandle* file = core::at(slab_bank(), first_free);
    reset_fields(file);
    file->os_open_setup();
    return core::at(slab_bank(), handles_in_use()++);
}

// 0x005B6C60
AsyncFileHandle* async_open(const char* name, bool blocking, int priority)
{
    AsyncFileHandle* file = async_get_handle();
    if (!file) {
        return nullptr;
    }
    if (file->unseal(name, blocking, priority)) {
        return file;
    }
    async_free_handle(file);
    return nullptr;
}

// 0x005B68A0
bool async_close(AsyncFileHandle* file)
{
    const bool result = file->shut();
    async_free_handle(file);
    return result;
}

} // namespace file

namespace file {

namespace {

bool stub_bool(const char* message)
{
    debug::printf_text(message);
    return false;
}
int stub_int(const char* message)
{
    debug::printf_text(message);
    return 0;
}

} // namespace

// The base's platform methods: debug stubs, each printing its name.
bool AsyncFileHandle::os_open_setup() { return stub_bool("STUB: CAsyncFileHandle::Init\n"); }
bool AsyncFileHandle::os_open_file(const char* /*unused*/) { return stub_bool("STUB: CAsyncFileHandle::Open\n"); }
bool AsyncFileHandle::os_close_file() { return stub_bool("STUB: CAsyncFileHandle::Close\n"); }
bool AsyncFileHandle::os_read_finished() { return stub_bool("STUB: CAsyncFileHandle::IsDone\n"); }
bool AsyncFileHandle::os_read_pending() { return stub_bool("STUB: CAsyncFileHandle::IsBusy\n"); }
bool AsyncFileHandle::os_at_end() { return stub_bool("STUB: CAsyncFileHandle::IsEOF\n"); }
int AsyncFileHandle::os_load_whole(void* /*unused*/) { return stub_int("STUB: CAsyncFileHandle::Load\n"); }
int AsyncFileHandle::os_read_chunk(void* /*unused*/, int /*unused*/, int /*unused*/)
{
    return stub_int("STUB: CAsyncFileHandle::Read\n");
}
bool AsyncFileHandle::os_cancel() { return stub_bool("STUB: CAsyncFileHandle::Cancel\n"); }

} // namespace file

// --- PcAsyncFileHandle: the PC platform handle (vtable 0x006583D8) --------------------------------------

namespace file {

namespace {

constexpr int kErrorIoPending = 997;    // ERROR_IO_PENDING
constexpr int kErrorHandleEof = 38;     // ERROR_HANDLE_EOF
constexpr DWORD kStatusPending = 0x103; // OVERLAPPED.Internal while the I/O is in progress

// Overlapped (truly asynchronous, unbuffered) reads are used on Windows NT 6 (Vista) and later.
bool& use_overlapped_io() { return async_state().use_overlapped_io_; }

class PcAsyncFileHandle final : public AsyncFileHandle {
public:
    // 0x005B9B80's construction: the base's, then no staging buffer and no file.
    PcAsyncFileHandle() = default;
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

private:
    std::array<uint8_t, kGapEnd - kGapStart> unknown3c_{};
    int unknown7c_{0};
    void* destination_{nullptr}; // where a read through staging_bytes_ goes
    void* staging_bytes_{nullptr};
    HANDLE file_ref_{INVALID_HANDLE_VALUE};
    OVERLAPPED async_ctl_{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(PcAsyncFileHandle, file_ref_) == kFileRefOffset && sizeof(PcAsyncFileHandle) == kPcHandleBytes,
                      "layout must match the original");
    }
};

} // namespace

// 0x005B9950 (keeps the size and position up to date, then the base class's)
void PcAsyncFileHandle::io_done_handler(int event, bool error, int value, int extra)
{
    switch (event) {
    case kAsyncIdle:
        return;
    case kAsyncOpen:
        if (value >= 0 && !error) {
            set_payload_bytes(extra);
        }
        break;
    case 2:
    case kOpReadFile:
        if (!error && staging_bytes_ && value > 0) {
            std::memcpy(destination_, staging_bytes_, static_cast<size_t>(value));
        }
        [[fallthrough]];
    case 4:
        if (value >= 0) {
            if (!error) {
                anchor_point() += value;
            }
            if (staging_bytes_) {
                memory::dealloc(staging_bytes_);
                destination_ = nullptr;
                staging_bytes_ = nullptr;
            }
        }
        break;
    case kEventSeek:
        if (value >= 0 && !error) {
            anchor_point() = value;
        }
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
    async_ctl_ = platform::read_from(async_ctl_, 0, nullptr);
    return false;
}

// 0x005B97E0
bool PcAsyncFileHandle::os_open_file(const char* name)
{
    anchor_point() = 0;
    std::array<char, kOverlayPathSize> path{};
    // Not original - the data overlay: a file of it is opened there.
    // Original behaviour: the data folder's file.
    if (!frontend::data_overlay::get_overlay_path(name, path)) {
        make_disk_path(std::span<char, kOverlayPathSize>(path).first<kDiskPathSize>(), name);
    }
    pending_io_counter() = pending_io_counter() + 1;
    in_flight_ops() = in_flight_ops() + 1;
    const bool overlapped = use_overlapped_io();
    file_ref_ =
        CreateFileA(path.data(), GENERIC_READ, overlapped ? FILE_SHARE_READ : 0, nullptr, OPEN_EXISTING,
                    overlapped ? FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING : FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ref_ == INVALID_HANDLE_VALUE) {
        return false; // (the busy counts stay raised, as in the original)
    }
    set_payload_bytes(static_cast<int>(::GetFileSize(file_ref_, nullptr)));
    set_payload_bytes((payload_bytes() + kSectorMask) & ~kSectorMask); // whole 2 KB sectors (unbuffered reads)
    // (the original passed the handle's value as the result: only checked for a sign, and the next operation sets it)
    io_done_handler(operation(), false, 0, payload_bytes());
    return true;
}

// 0x005B9AF0
bool PcAsyncFileHandle::os_close_file()
{
    if (file_ref_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    pending_io_counter() = pending_io_counter() + 1;
    in_flight_ops() = in_flight_ops() + 1;
    CloseHandle(file_ref_);
    file_ref_ = INVALID_HANDLE_VALUE;
    io_done_handler(operation(), false, 0, 0);
    return true;
}

// 0x005B9680 (called by the I/O thread while the handle is busy)
bool PcAsyncFileHandle::os_read_finished()
{
    if (!use_overlapped_io()) {
        set_outcome_val(0);
        return true;
    }
    bool done = false;
    if (outcome_val() == kErrorIoPending) {
        done = async_ctl_.Internal != kStatusPending;
        if (done) {
            DWORD bytes = 0;
            GetOverlappedResult(file_ref_, &async_ctl_, &bytes, FALSE);
            set_outcome_val(0);
            io_done_handler(operation(), false, static_cast<int>(bytes), 0);
        }
    }
    return done;
}

// 0x005B9700
bool PcAsyncFileHandle::os_read_pending() { return outcome_val() == kErrorIoPending; }

// 0x005B9710
bool PcAsyncFileHandle::os_at_end() { return outcome_val() == kErrorHandleEof; }

// 0x005B9720: read the whole file from the start
int PcAsyncFileHandle::os_load_whole(void* buffer)
{
    async_ctl_ = platform::read_from(async_ctl_, 0, nullptr);
    return os_read_chunk(buffer, 1, get_file_size());
}

// 0x005B9A40
int PcAsyncFileHandle::os_read_chunk(void* buffer, int size, int count)
{
    auto length = static_cast<DWORD>(((size * count) + kSectorMask) & ~kSectorMask); // whole sectors
    pending_io_counter() = pending_io_counter() + 1;
    in_flight_ops() = in_flight_ops() + 1;
    DWORD bytes = 0;
    if (ReadFile(file_ref_, buffer, length, &bytes, use_overlapped_io() ? &async_ctl_ : nullptr) == FALSE) {
        set_outcome_val(static_cast<int>(GetLastError()));
        if (outcome_val() == kErrorIoPending && is_blocking()) {
            while (!os_read_finished()) {
            }
        }
        return 0;
    }
    // Finished at once: counted done here, without a callback (as in the original).
    pending_io_counter() = pending_io_counter() - 1;
    in_flight_ops() = in_flight_ops() - 1;
    return 0;
}

namespace {
// 0x005B9B40: the I/O thread - every 66 ms, lets each busy handle check whether its I/O has finished
DWORD WINAPI async_io_thread(void* /*unused*/)
{
    for (;;) {
        Sleep(kIoPollMs);
        for (int i = 0; i < kMaxHandles; ++i) {
            AsyncFileHandle* file = core::at(slab_bank(), i);
            if (file && file->is_occupied()) {
                file->os_read_finished();
            }
        }
    }
}

// 0x005B9B80: create the 16 PC handles (or reset them), start the I/O thread, choose overlapped I/O
void platform_init()
{
    for (int i = 0; i < kMaxHandles; ++i) {
        if (core::at(slab_bank(), i)) {
            core::at(slab_bank(), i)->prepare_all();
            continue;
        }
        gsl::owner<PcAsyncFileHandle*> handle{
            memory::make_owned<PcAsyncFileHandle>()}; // (the pool's, for the game's life)
        core::at(slab_bank(), i) = handle;
    }
    CreateThread(nullptr, kIoThreadStackBytes, async_io_thread, nullptr, 0, nullptr);

    OSVERSIONINFOA version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    THUG2_MSVC_WARNING(suppress : 4996) // GetVersionExA: deprecated, but what the original asks
    if (GetVersionExA(&version) != FALSE) {
        use_overlapped_io() = version.dwPlatformId == VER_PLATFORM_WIN32_NT && version.dwMajorVersion > kWindowsXpMajorVersion;
    }
}
} // namespace

bool& async_use_overlapped_io() { return use_overlapped_io(); }

} // namespace file

// --- the async file loader module ------------------------------------------------------------------------
// An engine module (modules::PartUnit) whose per-frame task polls the platform handles and runs the completed I/O
// callbacks. One instance, reference counted (0x007CF190 / 0x007CF194).

namespace file {

namespace {

std::unique_ptr<AsyncLoaderModule>& loader_module() { return async_state().loader_module_; }
uint32_t& loader_module_refs() { return async_state().loader_module_refs_; }

void register_logic_task(tasks::SchedulerTask* task)
{
    // The main loop manager's logic task list.
    assets::get_main_loop_manager()->logic_tasks().queue_job(task);
}

} // namespace

// 0x005B9780: give each busy handle the chance to complete (also done by the I/O thread)
void async_poll_handles()
{
    for (int i = 0; i < kMaxHandles; ++i) {
        AsyncFileHandle* file = core::at(slab_bank(), i);
        if (file && file->is_occupied()) {
            file->os_read_finished();
        }
    }
}

namespace {
// 0x005B6990: the module's task, every frame
void async_loader_task(tasks::Task<AsyncLoaderModule>* /*task*/)
{
    async_poll_handles();
    async_execute_callbacks();
}
} // namespace

// 0x005B6CA0: returns the module (null if it doesn't exist) - the start-up code keeps it
AsyncLoaderModule* create_async_loader_module(bool create)
{
    if (!loader_module() && create) {
        loader_module() = std::make_unique<AsyncLoaderModule>();
    }
    ++loader_module_refs();
    return loader_module().get();
}

// 0x005B6620
void destroy_async_loader_module()
{
    if (loader_module_refs() > 1) {
        --loader_module_refs();
        return;
    }
    memory::destroy_then_clear(loader_module());
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

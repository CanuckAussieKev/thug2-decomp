#include "engine/file/async.h"
#include "engine/net/task_vtable.h"

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
#include "engine/core/code_address.h"
#include "game/skate/skate_module.h"
#include "engine/core/stubs.h"
#include "engine/assets/resource_registry.h"

namespace {
extern void** const kAsyncFileHandleAdapterVtable;
extern void** const kAsyncLoaderModuleMethodsVtable;
extern void** const kPcAsyncFileHandleAdapterVtable;
} // namespace

namespace file {

static_assert(offsetof(AsyncFileHandle, callback_mask_) == 0x14 && offsetof(AsyncFileHandle, is_blocking_) == 0x21 &&
                  offsetof(AsyncFileHandle, payload_bytes_) == 0x2C && offsetof(AsyncFileHandle, in_flight_ops_) == 0x38,
              "layout must match the original");

namespace {

constexpr int kMaxHandles = 16;

AsyncFileHandle* g_pool[kMaxHandles];
int g_handles_in_use;
int g_pending_reads;
bool g_io_completed;
int g_current_list;
AsyncLoaderModule* g_loader_module;
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

PendingCallback (&callback_lists())[2][kMaxHandles]
{
    return g_pending_callbacks;
}
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
    file->Virtual<void>(AsyncFileHandle::kSlotNotify, file->operation_, false, file->outcome_val_, 0);
}

} // namespace

// The platform (PC) side, below.
void platform_init();
DWORD WINAPI async_io_thread(void*);

// The CAsyncFileHandle constructor's work (0x005B69A0).
void construct_base(AsyncFileHandle* file)
{
    reset_fields(file);
    file->vtable_ = kAsyncFileHandleAdapterVtable;
    // The base class's platform init, through the base vtable (a debug stub).
    reinterpret_cast<bool(__thiscall*)(AsyncFileHandle*)>(file->vtable_[AsyncFileHandle::kSlotInit])(file);
}

// --- CAsyncFileHandle ----------------------------------------------------------------------------------

// 0x005B66F0 (Init: back to defaults, then the platform's init)
bool AsyncFileHandle::prepare_all()
{
    reset_fields(this);
    return Virtual<bool>(kSlotInit);
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
    return Virtual<bool>(kPlatCancel);
}

// 0x005B6730
bool AsyncFileHandle::is_occupied()
{
    return in_flight_ops_ > 0;
}

// 0x005B6740
int AsyncFileHandle::get_take_in(void* buffer, int size, int count)
{
    operation_ = kOpReadFile;
    if (pre_file_) {
        outcome_val_ = pre::get_take_in(buffer, size, count, static_cast<pre::ArchiveFile::FileInfo*>(pre_file_));
        complete_from_archive(this);
        return outcome_val_;
    }
    return Virtual<int>(kSlotRead, buffer, size, count);
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
    return Virtual<bool>(kSlotOpen, name);
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
    return Virtual<bool>(kSlotClose);
}

// 0x005B69F0 (virtual on_io_done_: an operation finished - queue its callback, or count it done)
void AsyncFileHandle::io_done_handler(int event, bool error, int value, int /*extra*/)
{
    outcome_val_ = value;
    io_completed() = true;
    if (callback_ && !error && (callback_mask_ & (1u << (event & 31)))) {
        async_add_callback(this, event, value); // counted done when the callback has run
        return;
    }
    pending_io_counter() = pending_io_counter() - 1;
    if ((in_flight_ops_ = in_flight_ops_ - 1) == 0)
        operation_ = kAsyncIdle;
}

// --- CAsyncFileLoader (static) -------------------------------------------------------------------------

// 0x005B6560
void async_init()
{
    handles_in_use() = 0;
    platform_init();
}

// 0x005B6580
bool async_busy()
{
    return pending_io_counter() > 0;
}

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
        debug::printf("CAsyncFileLoader got completion: busy count %d completion %d\n", pending_io_counter(), io_completed());
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
    file->Virtual<bool>(AsyncFileHandle::kSlotInit);
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

} // namespace File

namespace {

using file::AsyncFileHandle;

// 0x005B69A0 (constructor) and 0x005B66C0 (scalar deleting destructor) of CAsyncFileHandle
struct AsyncFileHandleAdapter {
    AsyncFileHandle* make_new()
    {
        auto* file = reinterpret_cast<AsyncFileHandle*>(this);
        file::construct_base(file);
        return file;
    }
    AsyncFileHandle* teardown(unsigned int flags)
    {
        auto* file = reinterpret_cast<AsyncFileHandle*>(this);
        file->vtable_ = kAsyncFileHandleAdapterVtable;
        if (flags & 1)
            memory::dealloc(file);
        return file;
    }
};

// The base class's platform methods: debug stubs, each printing its name (vtable slots 2-18).
struct BaseStubs {
    bool prepare_all() { return get_stub("STUB: CAsyncFileHandle::Init\n"); }
    bool unseal(const char*) { return get_stub("STUB: CAsyncFileHandle::Open\n"); }
    bool shut() { return get_stub("STUB: CAsyncFileHandle::Close\n"); }
    bool is_finished() { return get_stub("STUB: CAsyncFileHandle::IsDone\n"); }
    bool is_occupied() { return get_stub("STUB: CAsyncFileHandle::IsBusy\n"); }
    bool is_eof() { return get_stub("STUB: CAsyncFileHandle::IsEOF\n"); }
    void change_rank(int) { debug::printf("STUB: CAsyncFileHandle::SetPriority\n"); }
    void set_stream(bool) { debug::printf("STUB: CAsyncFileHandle::SetStream\n"); }
    void set_destination(int) { debug::printf("STUB: CAsyncFileHandle::SetDestination\n"); }
    void set_buffer_size(int) { debug::printf("STUB: CAsyncFileHandle::SetBufferSize\n"); }
    void set_blocking(bool) { debug::printf("STUB: CAsyncFileHandle::SetBlocking\n"); }
    int read_in(void*) { return stub_int("STUB: CAsyncFileHandle::Load\n"); }
    int get_take_in(void*, int, int) { return stub_int("STUB: CAsyncFileHandle::Read\n"); }
    int emit_out(const void*, int, int) { return stub_int("STUB: CAsyncFileHandle::Write\n"); }
    int get_s(char*, int) { return stub_int("STUB: CAsyncFileHandle::GetS\n"); }
    int seek_to(int, int) { return stub_int("STUB: CAsyncFileHandle::Seek\n"); }
    bool get_cancel() { return get_stub("STUB: CAsyncFileHandle::Cancel\n"); }

    static bool get_stub(const char* message)
    {
        debug::printf(message);
        return false;
    }
    static int stub_int(const char* message)
    {
        debug::printf(message);
        return 0;
    }
};

} // namespace

// --- CPcAsyncFileHandle: the PC platform handle (vtable 0x006583D8) -------------------------------------

namespace file {

namespace {

constexpr int kErrorIoPending = 997;   // ERROR_IO_PENDING
constexpr int kErrorHandleEof = 38;    // ERROR_HANDLE_EOF
constexpr DWORD kStatusPending = 0x103; // OVERLAPPED.Internal while the I/O is in progress

// Overlapped (truly asynchronous, unbuffered) reads are used on Windows NT 6 (Vista) and later.
bool& use_overlapped_io() { return g_use_overlapped_io; }

} // namespace

struct PcAsyncFileHandle : AsyncFileHandle {
    // Platform methods (vtable slots).
    void pc_io_callback(int event, bool error, int value, int extra);
    bool os_open_setup();
    bool os_open_file(const char* name);
    bool os_close_file();
    bool os_read_finished();
    bool os_read_pending();
    bool os_at_end();
    int os_load_whole(void* buffer);
    int os_read_chunk(void* buffer, int size, int count);

    uint8_t unknown3c_[0x7C - 0x3C];
    int unknown7c_;
    void* destination_; // where a read through staging_bytes_ goes
    void* staging_bytes_;
    HANDLE file_ref_;
    OVERLAPPED async_ctl_;
};
static_assert(offsetof(PcAsyncFileHandle, file_ref_) == 0x88 && sizeof(PcAsyncFileHandle) == 0xA0,
              "layout must match the original");

// 0x005B9950 (virtual on_io_done_: keeps the size and position up to date, then the base class's)
void PcAsyncFileHandle::pc_io_callback(int event, bool error, int value, int extra)
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
    io_done_handler(event, error, value, extra);
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
        make_disk_path(reinterpret_cast<char(&)[256]>(path), name);
    pending_io_counter() = pending_io_counter() + 1;
    in_flight_ops_ = in_flight_ops_ + 1;
    bool overlapped = use_overlapped_io();
    file_ref_ = CreateFileA(path, GENERIC_READ, overlapped ? FILE_SHARE_READ : 0, nullptr, OPEN_EXISTING,
                         overlapped ? FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING : FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ref_ == INVALID_HANDLE_VALUE)
        return false; // (the busy counts stay raised, as in the original)
    payload_bytes_ = static_cast<int>(::GetFileSize(file_ref_, nullptr));
    payload_bytes_ = (payload_bytes_ + 0x7FF) & ~0x7FF; // whole 2 KB sectors (unbuffered reads)
    Virtual<void>(kSlotNotify, operation_, false, reinterpret_cast<int>(file_ref_), static_cast<int>(payload_bytes_));
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
    Virtual<void>(kSlotNotify, operation_, false, 0, 0);
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
            DWORD bytes;
            GetOverlappedResult(file_ref_, &async_ctl_, &bytes, FALSE);
            outcome_val_ = 0;
            Virtual<void>(kSlotNotify, operation_, false, static_cast<int>(bytes), 0);
        }
    }
    return done;
}

// 0x005B9700
bool PcAsyncFileHandle::os_read_pending()
{
    return outcome_val_ == kErrorIoPending;
}

// 0x005B9710
bool PcAsyncFileHandle::os_at_end()
{
    return outcome_val_ == kErrorHandleEof;
}

// 0x005B9720: read the whole file from the start
int PcAsyncFileHandle::os_load_whole(void* buffer)
{
    async_ctl_.Offset = 0;
    async_ctl_.OffsetHigh = 0;
    async_ctl_.hEvent = nullptr;
    return Virtual<int>(kSlotRead, buffer, 1, get_file_size());
}

// 0x005B9A40
int PcAsyncFileHandle::os_read_chunk(void* buffer, int size, int count)
{
    DWORD length = static_cast<DWORD>((size * count + 0x7FF) & ~0x7FF); // whole sectors
    pending_io_counter() = pending_io_counter() + 1;
    in_flight_ops_ = in_flight_ops_ + 1;
    DWORD bytes = 0;
    if (!ReadFile(file_ref_, buffer, length, &bytes, use_overlapped_io() ? &async_ctl_ : nullptr)) {
        outcome_val_ = static_cast<int>(GetLastError());
        if (outcome_val_ == kErrorIoPending && is_blocking_) {
            while (!Virtual<bool>(kSlotFinished)) {
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
                file->Virtual<bool>(AsyncFileHandle::kSlotFinished);
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
        auto* file = static_cast<PcAsyncFileHandle*>(memory::alloc(sizeof(PcAsyncFileHandle)));
        if (file) {
            construct_base(file);
            file->vtable_ = kPcAsyncFileHandleAdapterVtable;
            file->unknown7c_ = 0;
            file->destination_ = nullptr;
            file->staging_bytes_ = nullptr;
            file->file_ref_ = INVALID_HANDLE_VALUE;
        }
        slab_bank()[i] = file;
    }
    CreateThread(nullptr, 0x4000, async_io_thread, nullptr, 0, nullptr);

    OSVERSIONINFOA version = {};
    version.dwOSVersionInfoSize = sizeof(version);
#pragma warning(suppress : 4996) // GetVersionExA: deprecated, but what the original asks
    if (GetVersionExA(&version))
        use_overlapped_io() = version.dwPlatformId == VER_PLATFORM_WIN32_NT && version.dwMajorVersion > 5;
}

void construct_async_file_handle(AsyncFileHandle* file)
{
    construct_base(file);
}

PcAsyncMethods pc_async_file_handle_methods()
{
    return {code_address(&PcAsyncFileHandle::pc_io_callback), code_address(&PcAsyncFileHandle::os_open_setup),
            code_address(&PcAsyncFileHandle::os_open_file),     code_address(&PcAsyncFileHandle::os_close_file),
            code_address(&PcAsyncFileHandle::os_read_finished),   code_address(&PcAsyncFileHandle::os_read_pending),
            code_address(&PcAsyncFileHandle::os_at_end),    code_address(&PcAsyncFileHandle::os_load_whole),
            code_address(&PcAsyncFileHandle::os_read_chunk)};
}

bool& async_use_overlapped_io()
{
    return use_overlapped_io();
}

} // namespace File

namespace {

// 0x005B97B0 (scalar deleting destructor) and 0x005B9640 / 0x005B6370 (the destructor bodies: each sets its
// class's vtable on the way down)
struct PcAsyncFileHandleAdapter {
    AsyncFileHandle* teardown(unsigned int flags)
    {
        auto* file = reinterpret_cast<AsyncFileHandle*>(this);
        destroy_body();
        if (flags & 1)
            memory::dealloc(file);
        return file;
    }
    void destroy_body()
    {
        reinterpret_cast<AsyncFileHandle*>(this)->vtable_ = kPcAsyncFileHandleAdapterVtable;
        destroy_base_body();
    }
    void destroy_base_body() { reinterpret_cast<AsyncFileHandle*>(this)->vtable_ = kAsyncFileHandleAdapterVtable; }
};

} // namespace

// --- the async file loader module ------------------------------------------------------------------------
// An engine module (modules::PartUnit) whose per-frame task polls the platform handles and runs the completed I/O
// callbacks. One instance, reference counted (0x007CF190 / 0x007CF194).

namespace file {

struct AsyncLoaderModule {
    void** vtable_;          // 0x00658158
    uint8_t owning_module_[0x14 - 4]; // modules::PartUnit's fields
    void* task_;             // tasks::Task running async_loader_task
};
static_assert(sizeof(AsyncLoaderModule) == 0x18, "allocated as 0x18 bytes by the original");

namespace {

AsyncLoaderModule*& loader_module() { return g_loader_module; }
uint32_t& loader_module_refs() { return g_loader_module_refs; }

// Engine framework pieces.
void construct_module(void* module) { static_cast<modules::PartUnit*>(module)->make_new(); }
void destroy_module(void* module) { static_cast<modules::PartUnit*>(module)->destroy_body(); }
void* construct_task(void* task) { return static_cast<tasks::SchedulerTask*>(task)->make_new(0); }
void register_logic_task(void* task)
{
    // The main loop manager's logic task list.
    auto* manager = reinterpret_cast<uint8_t*>(assets::g_main_loop_manager);
    reinterpret_cast<tasks::PileUp*>(manager + 4)->queue_job(static_cast<tasks::SchedulerTask*>(task));
}

} // namespace

AsyncLoaderModule* construct_async_loader_module(void* memory);

// 0x005B9780: give each busy handle the chance to complete (also done by the I/O thread)
void async_poll_handles()
{
    for (int i = 0; i < kMaxHandles; ++i) {
        AsyncFileHandle* file = slab_bank()[i];
        if (file && file->is_occupied())
            file->Virtual<bool>(AsyncFileHandle::kSlotFinished);
    }
}

// 0x005B6990: the module's task, every frame
void __cdecl async_loader_task(void* /*task*/)
{
    async_poll_handles();
    async_execute_callbacks();
}

// 0x005B6CA0: returns the module (null if it doesn't exist) - the start-up code keeps it
AsyncLoaderModule* create_async_loader_module(bool create)
{
    if (!loader_module() && create) {
        void* memory = memory::alloc_zeroed(sizeof(AsyncLoaderModule), nullptr, 1);
        loader_module() = memory ? construct_async_loader_module(memory) : nullptr;
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
    if (AsyncLoaderModule* module = loader_module())
        reinterpret_cast<void(__thiscall*)(AsyncLoaderModule*, unsigned int)>(module->vtable_[0])(module, 1);
    loader_module() = nullptr;
    loader_module_refs() = 0;
}

} // namespace File

namespace {

using file::AsyncLoaderModule;

struct AsyncLoaderModuleMethods {
    AsyncLoaderModule* get_self() { return reinterpret_cast<AsyncLoaderModule*>(this); }

    // 0x005B6BC0
    AsyncLoaderModule* make_new()
    {
        AsyncLoaderModule* module = get_self();
        file::construct_module(module);
        module->vtable_ = kAsyncLoaderModuleMethodsVtable;
        auto* task = static_cast<uint32_t*>(memory::alloc_zeroed(0x20));
        if (task) {
            file::construct_task(task);
            task[7] = reinterpret_cast<uint32_t>(module);    // the task's data
            task[0] = reinterpret_cast<uint32_t>(game_code::kTaskVtable);                           // tasks::Task<CAsyncFileLoader> vtable
            task[6] = reinterpret_cast<uint32_t>(code_address(file::async_loader_task)); // its code: async_loader_task
        }
        module->task_ = task;
        return module;
    }
    // 0x005B6660 (destructor body)
    void destroy_body()
    {
        AsyncLoaderModule* module = get_self();
        module->vtable_ = kAsyncLoaderModuleMethodsVtable;
        if (auto* task = static_cast<void***>(module->task_))
            reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*task)[2])(task, 1);
        file::destroy_module(module);
    }
    // 0x005B6940 (scalar deleting destructor)
    AsyncLoaderModule* teardown(unsigned int flags)
    {
        destroy_body();
        if (flags & 1)
            memory::dealloc(get_self());
        return get_self();
    }
    // 0x005B6970 (virtual: the module starts - its task joins the logic tasks)
    void kickoff() { file::register_logic_task(get_self()->task_); }
};

} // namespace

file::AsyncLoaderModule* file::construct_async_loader_module(void* memory)
{
    return static_cast<AsyncLoaderModuleMethods*>(memory)->make_new();
}

namespace {
static VtableSlot g_async_file_handle_adapter_vtable_slots[] = {&AsyncFileHandleAdapter::teardown,
                &AsyncFileHandle::io_done_handler,
                &BaseStubs::prepare_all,
                &BaseStubs::unseal,
                &BaseStubs::shut,
                &BaseStubs::is_finished,
                &BaseStubs::is_occupied,
                &BaseStubs::is_eof,
                &BaseStubs::change_rank,
                &BaseStubs::set_stream,
                &BaseStubs::set_destination,
                &BaseStubs::set_buffer_size,
                &BaseStubs::set_blocking,
                &BaseStubs::read_in,
                &BaseStubs::get_take_in,
                &BaseStubs::emit_out,
                &BaseStubs::get_s,
                &BaseStubs::seek_to,
                &BaseStubs::get_cancel};
void** const kAsyncFileHandleAdapterVtable = reinterpret_cast<void**>(g_async_file_handle_adapter_vtable_slots);
static VtableSlot g_async_loader_module_methods_vtable_slots[] = {&AsyncLoaderModuleMethods::teardown,
                &AsyncLoaderModuleMethods::kickoff, &modules::Skate::stop_callback};
void** const kAsyncLoaderModuleMethodsVtable = reinterpret_cast<void**>(g_async_loader_module_methods_vtable_slots);
static VtableSlot g_pc_async_file_handle_adapter_vtable_slots[] = {&PcAsyncFileHandleAdapter::teardown,
                &file::PcAsyncFileHandle::pc_io_callback, &file::PcAsyncFileHandle::os_open_setup,
                &file::PcAsyncFileHandle::os_open_file, &file::PcAsyncFileHandle::os_close_file,
                &file::PcAsyncFileHandle::os_read_finished, &file::PcAsyncFileHandle::os_read_pending,
                &file::PcAsyncFileHandle::os_at_end, stubs::ignore_argument, stubs::ignore_argument,
                stubs::ignore_argument, stubs::ignore_argument, stubs::ignore_argument, &file::PcAsyncFileHandle::os_load_whole,
                &file::PcAsyncFileHandle::os_read_chunk, stubs::return_zero3, stubs::return_zero2, stubs::return_zero2,
                &BaseStubs::get_cancel};
void** const kPcAsyncFileHandleAdapterVtable = reinterpret_cast<void**>(g_pc_async_file_handle_adapter_vtable_slots);
} // namespace

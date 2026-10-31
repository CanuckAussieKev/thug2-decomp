#include "game/file/async.h"
#include "game/net/task_vtable.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/core/task_list.h"
#include "game/file/file_load.h"
#include "game/file/archive.h"
#include "game/front/data_overlay.h"
#include "game/sys/game_module.h"
#include "game/core/code_address.h"
#include "game/skate/skate_module.h"
#include "game/core/stubs.h"
#include "game/assets/resource_registry.h"

namespace {
extern void** const kAsyncFileHandleAdapterVtable;
extern void** const kAsyncLoaderModuleMethodsVtable;
extern void** const kPcAsyncFileHandleAdapterVtable;
} // namespace

namespace FileLoad {

static_assert(offsetof(AsyncFileHandle, m_callbackMask) == 0x14 && offsetof(AsyncFileHandle, m_isBlocking) == 0x21 &&
                  offsetof(AsyncFileHandle, m_payloadBytes) == 0x2C && offsetof(AsyncFileHandle, m_busyCount) == 0x38,
              "layout must match the original");

namespace {

constexpr int kMaxHandles = 16;

AsyncFileHandle* g_pool[kMaxHandles];
int g_handlesInUse;
int g_busyCount;
bool g_ioCompleted;
int g_currentList;
AsyncLoaderModule* g_loaderModule;
uint32_t g_loaderModuleRefs;
bool g_useOverlappedIo;

// Shared with the platform I/O thread: plain (volatile) accesses, as in the original.
AsyncFileHandle** SlabBank() { return g_pool; }
int& HandlesInUse() { return g_handlesInUse; }
volatile int& BusyCount() { return g_busyCount; }
volatile bool& IoCompleted() { return g_ioCompleted; }

// Completed operations waiting for their callbacks: two lists, one filled while the other is run.
struct PendingCallback {
    AsyncFileHandle* file;
    int event;
    int value;
};

PendingCallback g_pendingCallbacks[2][kMaxHandles];
int g_callbackCounts[2];

PendingCallback (&CallbackLists())[2][kMaxHandles]
{
    return g_pendingCallbacks;
}
int (&CallbackCounts())[2] { return g_callbackCounts; }
int& CurrentList() { return g_currentList; }

// The handle fields every allocation resets (as the original's Init and get_file_handle do).
void ResetFields(AsyncFileHandle* file)
{
    file->m_payloadBytes = -1;
    file->m_anchorPoint = -1;
    file->m_callbackMask = 0xFFFFFFFF;
    file->m_busyCount = 0;
    file->m_callback = nullptr;
    file->m_arg0 = nullptr;
    file->m_arg1 = nullptr;
    file->m_operation = kAsyncIdle;
    file->m_unknown1C = 0;
    file->m_feed = false;
    file->m_isBlocking = true;
    file->m_bufferSize = 0x10000;
    file->m_rank = 100;
    file->m_outcomeVal = 0;
    file->m_preFile = nullptr;
}

// Report an operation served from a .pre archive, as the platform does when its I/O completes.
void CompleteFromArchive(AsyncFileHandle* file)
{
    BusyCount() = BusyCount() + 1;
    file->m_busyCount = file->m_busyCount + 1;
    file->Virtual<void>(AsyncFileHandle::kIoCallback, file->m_operation, false, file->m_outcomeVal, 0);
}

} // namespace

// The platform (PC) side, below.
void PlatformInit();
DWORD WINAPI AsyncIoThread(void*);

// The CAsyncFileHandle constructor's work (0x005B69A0).
void ConstructBase(AsyncFileHandle* file)
{
    ResetFields(file);
    file->m_vtable = kAsyncFileHandleAdapterVtable;
    // The base class's platform init, through the base vtable (a debug stub).
    reinterpret_cast<bool(__thiscall*)(AsyncFileHandle*)>(file->m_vtable[AsyncFileHandle::kPlatInit])(file);
}

// --- CAsyncFileHandle ----------------------------------------------------------------------------------

// 0x005B66F0 (Init: back to defaults, then the platform's init)
bool AsyncFileHandle::PrepareAll()
{
    ResetFields(this);
    return Virtual<bool>(kPlatInit);
}

// 0x005B6380
void AsyncFileHandle::ChangeHook(IoCompletionHook callback, void* arg0, void* arg1)
{
    m_callback = callback;
    m_arg0 = arg0;
    m_arg1 = arg1;
}

// 0x005B63A0
int AsyncFileHandle::GetFileSize()
{
    if (m_preFile) {
        m_outcomeVal = Pre::GetFileSize(static_cast<Pre::ArchiveFile::FileInfo*>(m_preFile));
        return m_outcomeVal;
    }
    while (m_payloadBytes < 0) {
        // the platform's open sets it (on the I/O thread when overlapped)
    }
    return m_payloadBytes;
}

// 0x005B63D0
bool AsyncFileHandle::Cancel()
{
    m_operation = kAsyncCancel;
    if (m_preFile) {
        m_outcomeVal = -1;
        return true;
    }
    return Virtual<bool>(kPlatCancel);
}

// 0x005B6730
bool AsyncFileHandle::IsOccupied()
{
    return m_busyCount > 0;
}

// 0x005B6740
int AsyncFileHandle::TakeIn(void* buffer, int size, int count)
{
    m_operation = kAsyncRead;
    if (m_preFile) {
        m_outcomeVal = Pre::TakeIn(buffer, size, count, static_cast<Pre::ArchiveFile::FileInfo*>(m_preFile));
        CompleteFromArchive(this);
        return m_outcomeVal;
    }
    return Virtual<int>(kPlatRead, buffer, size, count);
}

// 0x005B67B0
bool AsyncFileHandle::Unseal(const char* name, bool blocking, int priority)
{
    m_rank = priority;
    m_isBlocking = blocking;
    m_operation = kAsyncOpen;
    // Not original - the data overlay (game/front/data_overlay.h): a file of the data overlay isn't taken from an
    // archive (the platform's open reads the overlay's).
    // Original behaviour: a loaded archive's copy first.
    char overlay[260];
    if (Pre::Hub() && !MenuSystem::DataOverlay::OverlayPath(name, overlay)) {
        m_preFile = Pre::Unseal(name, "rb", true);
        if (m_preFile) {
            m_outcomeVal = 1;
            m_isBlocking = true;
            m_payloadBytes = static_cast<Pre::ArchiveFile::FileInfo*>(m_preFile)->size;
            CompleteFromArchive(this);
            return m_outcomeVal != 0;
        }
    }
    return Virtual<bool>(kPlatOpen, name);
}

// 0x005B6840
bool AsyncFileHandle::Shut()
{
    m_operation = kAsyncClose;
    if (m_preFile) {
        m_outcomeVal = Pre::Shut(static_cast<Pre::ArchiveFile::FileInfo*>(m_preFile), true);
        CompleteFromArchive(this);
        return m_outcomeVal != 0;
    }
    return Virtual<bool>(kPlatClose);
}

// 0x005B69F0 (virtual onIoDone: an operation finished - queue its callback, or count it done)
void AsyncFileHandle::IoCallback(int event, bool error, int value, int /*extra*/)
{
    m_outcomeVal = value;
    IoCompleted() = true;
    if (m_callback && !error && (m_callbackMask & (1u << (event & 31)))) {
        AsyncAddCallback(this, event, value); // counted done when the callback has run
        return;
    }
    BusyCount() = BusyCount() - 1;
    if ((m_busyCount = m_busyCount - 1) == 0)
        m_operation = kAsyncIdle;
}

// --- CAsyncFileLoader (static) -------------------------------------------------------------------------

// 0x005B6560
void AsyncInit()
{
    HandlesInUse() = 0;
    PlatformInit();
}

// 0x005B6580
bool AsyncBusy()
{
    return BusyCount() > 0;
}

// 0x005B6590
bool AsyncFreeHandle(AsyncFileHandle* file)
{
    for (int i = 0; i < kMaxHandles; ++i) {
        if (SlabBank()[i] == file) {
            // Swap it with the last handle in use.
            int last = --HandlesInUse();
            SlabBank()[i] = SlabBank()[last];
            SlabBank()[last] = file;
            return true;
        }
    }
    return false;
}

// 0x005B65D0
void AsyncAddCallback(AsyncFileHandle* file, int event, int value)
{
    int list = CurrentList();
    int index = CallbackCounts()[list]++;
    CallbackLists()[list][index] = {file, event, value};
}

// 0x005B9770 (platform: start filling the other list)
void AsyncSwapCallbackLists()
{
    CurrentList() ^= 1;
    IoCompleted() = false;
}

// 0x005B68C0
void AsyncExecuteCallbacks()
{
    AsyncSwapCallbackLists();
    int list = CurrentList() ^ 1; // the one that was being filled
    int count = CallbackCounts()[list];
    for (int i = 0; i < count; ++i) {
        const PendingCallback& pending = CallbackLists()[list][i];
        AsyncFileHandle* file = pending.file;
        file->m_callback(file, pending.event, pending.value, file->m_arg0, file->m_arg1);
        BusyCount() = BusyCount() - 1;
        if ((file->m_busyCount = file->m_busyCount - 1) == 0)
            file->m_operation = kAsyncIdle;
    }
    CallbackCounts()[list] = 0;
}

// 0x005B6A50
void AsyncWaitForIO(bool all)
{
    for (;;) {
        DebugOut::Printf("CAsyncFileLoader waiting for io completion: busy count %d completion %d\n", BusyCount(),
                    IoCompleted());
        while (!IoCompleted()) {
        }
        DebugOut::Printf("CAsyncFileLoader got completion: busy count %d completion %d\n", BusyCount(), IoCompleted());
        AsyncExecuteCallbacks();
        if (!all)
            return;
        int busy = 0;
        while (busy < kMaxHandles && SlabBank()[busy]->m_busyCount <= 0)
            ++busy;
        if (busy == kMaxHandles)
            return;
        DebugOut::Printf("CAsyncFileLoader still needs to wait for file handle %d\n", busy);
    }
}

// 0x005B6AE0
AsyncFileHandle* AsyncGetHandle()
{
    int first_free = HandlesInUse();
    if (first_free >= kMaxHandles)
        return nullptr;
    // The next handle may still be finishing an operation: use a later idle one instead, or wait.
    if (SlabBank()[first_free]->m_busyCount > 0) {
        int idle = first_free + 1;
        while (idle < kMaxHandles && SlabBank()[idle]->m_busyCount > 0)
            ++idle;
        if (idle < kMaxHandles) {
            AsyncFileHandle* swap = SlabBank()[idle];
            SlabBank()[idle] = SlabBank()[first_free];
            SlabBank()[first_free] = swap;
        } else {
            AsyncFileHandle* busy = SlabBank()[first_free];
            do {
                AsyncExecuteCallbacks();
                first_free = HandlesInUse();
            } while (busy->m_busyCount > 0);
        }
    }
    AsyncFileHandle* file = SlabBank()[first_free];
    ResetFields(file);
    file->Virtual<bool>(AsyncFileHandle::kPlatInit);
    return SlabBank()[HandlesInUse()++];
}

// 0x005B6C60
AsyncFileHandle* AsyncOpen(const char* name, bool blocking, int priority)
{
    AsyncFileHandle* file = AsyncGetHandle();
    if (!file)
        return nullptr;
    if (file->Unseal(name, blocking, priority))
        return file;
    AsyncFreeHandle(file);
    return nullptr;
}

// 0x005B68A0
bool AsyncClose(AsyncFileHandle* file)
{
    bool result = file->Shut();
    AsyncFreeHandle(file);
    return result;
}

} // namespace File

namespace {

using FileLoad::AsyncFileHandle;

// 0x005B69A0 (constructor) and 0x005B66C0 (scalar deleting destructor) of CAsyncFileHandle
struct AsyncFileHandleAdapter {
    AsyncFileHandle* MakeNew()
    {
        auto* file = reinterpret_cast<AsyncFileHandle*>(this);
        FileLoad::ConstructBase(file);
        return file;
    }
    AsyncFileHandle* Teardown(unsigned int flags)
    {
        auto* file = reinterpret_cast<AsyncFileHandle*>(this);
        file->m_vtable = kAsyncFileHandleAdapterVtable;
        if (flags & 1)
            MemSys::Dealloc(file);
        return file;
    }
};

// The base class's platform methods: debug stubs, each printing its name (vtable slots 2-18).
struct BaseStubs {
    bool PrepareAll() { return Stub("STUB: CAsyncFileHandle::Init\n"); }
    bool Unseal(const char*) { return Stub("STUB: CAsyncFileHandle::Open\n"); }
    bool Shut() { return Stub("STUB: CAsyncFileHandle::Close\n"); }
    bool IsFinished() { return Stub("STUB: CAsyncFileHandle::IsDone\n"); }
    bool IsOccupied() { return Stub("STUB: CAsyncFileHandle::IsBusy\n"); }
    bool IsEOF() { return Stub("STUB: CAsyncFileHandle::IsEOF\n"); }
    void ChangeRank(int) { DebugOut::Printf("STUB: CAsyncFileHandle::SetPriority\n"); }
    void SetStream(bool) { DebugOut::Printf("STUB: CAsyncFileHandle::SetStream\n"); }
    void SetDestination(int) { DebugOut::Printf("STUB: CAsyncFileHandle::SetDestination\n"); }
    void SetBufferSize(int) { DebugOut::Printf("STUB: CAsyncFileHandle::SetBufferSize\n"); }
    void SetBlocking(bool) { DebugOut::Printf("STUB: CAsyncFileHandle::SetBlocking\n"); }
    int ReadIn(void*) { return StubInt("STUB: CAsyncFileHandle::Load\n"); }
    int TakeIn(void*, int, int) { return StubInt("STUB: CAsyncFileHandle::Read\n"); }
    int EmitOut(const void*, int, int) { return StubInt("STUB: CAsyncFileHandle::Write\n"); }
    int GetS(char*, int) { return StubInt("STUB: CAsyncFileHandle::GetS\n"); }
    int SeekTo(int, int) { return StubInt("STUB: CAsyncFileHandle::Seek\n"); }
    bool Cancel() { return Stub("STUB: CAsyncFileHandle::Cancel\n"); }

    static bool Stub(const char* message)
    {
        DebugOut::Printf(message);
        return false;
    }
    static int StubInt(const char* message)
    {
        DebugOut::Printf(message);
        return 0;
    }
};

} // namespace

// --- CPcAsyncFileHandle: the PC platform handle (vtable 0x006583D8) -------------------------------------

namespace FileLoad {

namespace {

constexpr int kErrorIoPending = 997;   // ERROR_IO_PENDING
constexpr int kErrorHandleEof = 38;    // ERROR_HANDLE_EOF
constexpr DWORD kStatusPending = 0x103; // OVERLAPPED.Internal while the I/O is in progress

// Overlapped (truly asynchronous, unbuffered) reads are used on Windows NT 6 (Vista) and later.
bool& UseOverlappedIo() { return g_useOverlappedIo; }

} // namespace

struct PcAsyncFileHandle : AsyncFileHandle {
    // Platform methods (vtable slots).
    void PcIoCallback(int event, bool error, int value, int extra);
    bool PlatInit();
    bool PlatOpen(const char* name);
    bool PlatClose();
    bool PlatIsDone();
    bool PlatIsBusy();
    bool PlatIsEOF();
    int PlatLoad(void* buffer);
    int PlatRead(void* buffer, int size, int count);

    uint8_t m_unknown3C[0x7C - 0x3C];
    int m_unknown7C;
    void* m_destination; // where a read through m_tempBuffer goes
    void* m_tempBuffer;
    HANDLE m_fileRef;
    OVERLAPPED m_asyncCtl;
};
static_assert(offsetof(PcAsyncFileHandle, m_fileRef) == 0x88 && sizeof(PcAsyncFileHandle) == 0xA0,
              "layout must match the original");

// 0x005B9950 (virtual onIoDone: keeps the size and position up to date, then the base class's)
void PcAsyncFileHandle::PcIoCallback(int event, bool error, int value, int extra)
{
    switch (event) {
    case kAsyncIdle:
        return;
    case kAsyncOpen:
        if (value >= 0 && !error)
            m_payloadBytes = extra;
        break;
    case 2:
    case kAsyncRead:
        if (!error && m_tempBuffer && value > 0)
            std::memcpy(m_destination, m_tempBuffer, static_cast<size_t>(value));
        [[fallthrough]];
    case 4:
        if (value >= 0) {
            if (!error)
                m_anchorPoint += value;
            if (m_tempBuffer) {
                MemSys::Dealloc(m_tempBuffer);
                m_destination = nullptr;
                m_tempBuffer = nullptr;
            }
        }
        break;
    case 5:
        if (value >= 0 && !error)
            m_anchorPoint = value;
        break;
    default:
        break;
    }
    IoCallback(event, error, value, extra);
}

// 0x005B9650 (returns false, as the original does - EAX is 0 when it returns)
bool PcAsyncFileHandle::PlatInit()
{
    m_unknown7C = 0;
    m_destination = nullptr;
    m_tempBuffer = nullptr;
    m_asyncCtl.Offset = 0;
    m_asyncCtl.OffsetHigh = 0;
    m_asyncCtl.hEvent = nullptr;
    return false;
}

// 0x005B97E0
bool PcAsyncFileHandle::PlatOpen(const char* name)
{
    m_anchorPoint = 0;
    char path[260];
    // Not original - the data overlay: a file of it is opened there.
    // Original behaviour: the data folder's file.
    if (!MenuSystem::DataOverlay::OverlayPath(name, path))
        MakeDiskPath(reinterpret_cast<char(&)[256]>(path), name);
    BusyCount() = BusyCount() + 1;
    m_busyCount = m_busyCount + 1;
    bool overlapped = UseOverlappedIo();
    m_fileRef = CreateFileA(path, GENERIC_READ, overlapped ? FILE_SHARE_READ : 0, nullptr, OPEN_EXISTING,
                         overlapped ? FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING : FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_fileRef == INVALID_HANDLE_VALUE)
        return false; // (the busy counts stay raised, as in the original)
    m_payloadBytes = static_cast<int>(::GetFileSize(m_fileRef, nullptr));
    m_payloadBytes = (m_payloadBytes + 0x7FF) & ~0x7FF; // whole 2 KB sectors (unbuffered reads)
    Virtual<void>(kIoCallback, m_operation, false, reinterpret_cast<int>(m_fileRef), static_cast<int>(m_payloadBytes));
    return true;
}

// 0x005B9AF0
bool PcAsyncFileHandle::PlatClose()
{
    if (m_fileRef == INVALID_HANDLE_VALUE)
        return false;
    BusyCount() = BusyCount() + 1;
    m_busyCount = m_busyCount + 1;
    CloseHandle(m_fileRef);
    m_fileRef = INVALID_HANDLE_VALUE;
    Virtual<void>(kIoCallback, m_operation, false, 0, 0);
    return true;
}

// 0x005B9680 (called by the I/O thread while the handle is busy)
bool PcAsyncFileHandle::PlatIsDone()
{
    if (!UseOverlappedIo()) {
        m_outcomeVal = 0;
        return true;
    }
    bool done = false;
    if (m_outcomeVal == kErrorIoPending) {
        done = m_asyncCtl.Internal != kStatusPending;
        if (done) {
            DWORD bytes;
            GetOverlappedResult(m_fileRef, &m_asyncCtl, &bytes, FALSE);
            m_outcomeVal = 0;
            Virtual<void>(kIoCallback, m_operation, false, static_cast<int>(bytes), 0);
        }
    }
    return done;
}

// 0x005B9700
bool PcAsyncFileHandle::PlatIsBusy()
{
    return m_outcomeVal == kErrorIoPending;
}

// 0x005B9710
bool PcAsyncFileHandle::PlatIsEOF()
{
    return m_outcomeVal == kErrorHandleEof;
}

// 0x005B9720: read the whole file from the start
int PcAsyncFileHandle::PlatLoad(void* buffer)
{
    m_asyncCtl.Offset = 0;
    m_asyncCtl.OffsetHigh = 0;
    m_asyncCtl.hEvent = nullptr;
    return Virtual<int>(kPlatRead, buffer, 1, GetFileSize());
}

// 0x005B9A40
int PcAsyncFileHandle::PlatRead(void* buffer, int size, int count)
{
    DWORD length = static_cast<DWORD>((size * count + 0x7FF) & ~0x7FF); // whole sectors
    BusyCount() = BusyCount() + 1;
    m_busyCount = m_busyCount + 1;
    DWORD bytes = 0;
    if (!ReadFile(m_fileRef, buffer, length, &bytes, UseOverlappedIo() ? &m_asyncCtl : nullptr)) {
        m_outcomeVal = static_cast<int>(GetLastError());
        if (m_outcomeVal == kErrorIoPending && m_isBlocking) {
            while (!Virtual<bool>(kPlatIsDone)) {
            }
        }
        return 0;
    }
    // Finished at once: counted done here, without a callback (as in the original).
    BusyCount() = BusyCount() - 1;
    m_busyCount = m_busyCount - 1;
    return 0;
}

// 0x005B9B40: the I/O thread - every 66 ms, lets each busy handle check whether its I/O has finished
DWORD WINAPI AsyncIoThread(void*)
{
    for (;;) {
        Sleep(66);
        for (int i = 0; i < kMaxHandles; ++i) {
            AsyncFileHandle* file = SlabBank()[i];
            if (file && file->IsOccupied())
                file->Virtual<bool>(AsyncFileHandle::kPlatIsDone);
        }
    }
}

// 0x005B9B80: create the 16 PC handles (or reset them), start the I/O thread, choose overlapped I/O
void PlatformInit()
{
    for (int i = 0; i < kMaxHandles; ++i) {
        if (SlabBank()[i]) {
            SlabBank()[i]->PrepareAll();
            continue;
        }
        auto* file = static_cast<PcAsyncFileHandle*>(MemSys::Alloc(sizeof(PcAsyncFileHandle)));
        if (file) {
            ConstructBase(file);
            file->m_vtable = kPcAsyncFileHandleAdapterVtable;
            file->m_unknown7C = 0;
            file->m_destination = nullptr;
            file->m_tempBuffer = nullptr;
            file->m_fileRef = INVALID_HANDLE_VALUE;
        }
        SlabBank()[i] = file;
    }
    CreateThread(nullptr, 0x4000, AsyncIoThread, nullptr, 0, nullptr);

    OSVERSIONINFOA version = {};
    version.dwOSVersionInfoSize = sizeof(version);
#pragma warning(suppress : 4996) // GetVersionExA: deprecated, but what the original asks
    if (GetVersionExA(&version))
        UseOverlappedIo() = version.dwPlatformId == VER_PLATFORM_WIN32_NT && version.dwMajorVersion > 5;
}

void ConstructAsyncFileHandle(AsyncFileHandle* file)
{
    ConstructBase(file);
}

PcAsyncMethods PcAsyncFileHandleMethods()
{
    return {CodeAddress(&PcAsyncFileHandle::PcIoCallback), CodeAddress(&PcAsyncFileHandle::PlatInit),
            CodeAddress(&PcAsyncFileHandle::PlatOpen),     CodeAddress(&PcAsyncFileHandle::PlatClose),
            CodeAddress(&PcAsyncFileHandle::PlatIsDone),   CodeAddress(&PcAsyncFileHandle::PlatIsBusy),
            CodeAddress(&PcAsyncFileHandle::PlatIsEOF),    CodeAddress(&PcAsyncFileHandle::PlatLoad),
            CodeAddress(&PcAsyncFileHandle::PlatRead)};
}

bool& AsyncUseOverlappedIo()
{
    return UseOverlappedIo();
}

} // namespace File

namespace {

// 0x005B97B0 (scalar deleting destructor) and 0x005B9640 / 0x005B6370 (the destructor bodies: each sets its
// class's vtable on the way down)
struct PcAsyncFileHandleAdapter {
    AsyncFileHandle* Teardown(unsigned int flags)
    {
        auto* file = reinterpret_cast<AsyncFileHandle*>(this);
        DestroyBody();
        if (flags & 1)
            MemSys::Dealloc(file);
        return file;
    }
    void DestroyBody()
    {
        reinterpret_cast<AsyncFileHandle*>(this)->m_vtable = kPcAsyncFileHandleAdapterVtable;
        DestroyBaseBody();
    }
    void DestroyBaseBody() { reinterpret_cast<AsyncFileHandle*>(this)->m_vtable = kAsyncFileHandleAdapterVtable; }
};

} // namespace

// --- the async file loader module ------------------------------------------------------------------------
// An engine module (GameModules::PartUnit) whose per-frame task polls the platform handles and runs the completed I/O
// callbacks. One instance, reference counted (0x007CF190 / 0x007CF194).

namespace FileLoad {

struct AsyncLoaderModule {
    void** m_vtable;          // 0x00658158
    uint8_t m_owningModule[0x14 - 4]; // GameModules::PartUnit's fields
    void* m_task;             // TaskList::Task running AsyncLoaderTask
};
static_assert(sizeof(AsyncLoaderModule) == 0x18, "allocated as 0x18 bytes by the original");

namespace {

AsyncLoaderModule*& LoaderModule() { return g_loaderModule; }
uint32_t& LoaderModuleRefs() { return g_loaderModuleRefs; }

// Engine framework pieces.
void ConstructModule(void* module) { static_cast<GameModules::PartUnit*>(module)->MakeNew(); }
void DestroyModule(void* module) { static_cast<GameModules::PartUnit*>(module)->DestroyBody(); }
void* ConstructTask(void* task) { return static_cast<TaskList::SchedulerTask*>(task)->MakeNew(0); }
void RegisterLogicTask(void* task)
{
    // The main loop manager's logic task list.
    auto* manager = reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager);
    reinterpret_cast<TaskList::PileUp*>(manager + 4)->QueueJob(static_cast<TaskList::SchedulerTask*>(task));
}

} // namespace

AsyncLoaderModule* ConstructAsyncLoaderModule(void* memory);

// 0x005B9780: give each busy handle the chance to complete (also done by the I/O thread)
void AsyncPollHandles()
{
    for (int i = 0; i < kMaxHandles; ++i) {
        AsyncFileHandle* file = SlabBank()[i];
        if (file && file->IsOccupied())
            file->Virtual<bool>(AsyncFileHandle::kPlatIsDone);
    }
}

// 0x005B6990: the module's task, every frame
void __cdecl AsyncLoaderTask(void* /*task*/)
{
    AsyncPollHandles();
    AsyncExecuteCallbacks();
}

// 0x005B6CA0: returns the module (null if it doesn't exist) - the start-up code keeps it
AsyncLoaderModule* CreateAsyncLoaderModule(bool create)
{
    if (!LoaderModule() && create) {
        void* memory = MemSys::AllocZeroed(sizeof(AsyncLoaderModule), nullptr, 1);
        LoaderModule() = memory ? ConstructAsyncLoaderModule(memory) : nullptr;
    }
    ++LoaderModuleRefs();
    return LoaderModule();
}

// 0x005B6620
void DestroyAsyncLoaderModule()
{
    if (LoaderModuleRefs() > 1) {
        --LoaderModuleRefs();
        return;
    }
    if (AsyncLoaderModule* module = LoaderModule())
        reinterpret_cast<void(__thiscall*)(AsyncLoaderModule*, unsigned int)>(module->m_vtable[0])(module, 1);
    LoaderModule() = nullptr;
    LoaderModuleRefs() = 0;
}

} // namespace File

namespace {

using FileLoad::AsyncLoaderModule;

struct AsyncLoaderModuleMethods {
    AsyncLoaderModule* Self() { return reinterpret_cast<AsyncLoaderModule*>(this); }

    // 0x005B6BC0
    AsyncLoaderModule* MakeNew()
    {
        AsyncLoaderModule* module = Self();
        FileLoad::ConstructModule(module);
        module->m_vtable = kAsyncLoaderModuleMethodsVtable;
        auto* task = static_cast<uint32_t*>(MemSys::AllocZeroed(0x20));
        if (task) {
            FileLoad::ConstructTask(task);
            task[7] = reinterpret_cast<uint32_t>(module);    // the task's data
            task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);                           // TaskList::Task<CAsyncFileLoader> vtable
            task[6] = reinterpret_cast<uint32_t>(CodeAddress(FileLoad::AsyncLoaderTask)); // its code: AsyncLoaderTask
        }
        module->m_task = task;
        return module;
    }
    // 0x005B6660 (destructor body)
    void DestroyBody()
    {
        AsyncLoaderModule* module = Self();
        module->m_vtable = kAsyncLoaderModuleMethodsVtable;
        if (auto* task = static_cast<void***>(module->m_task))
            reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*task)[2])(task, 1);
        FileLoad::DestroyModule(module);
    }
    // 0x005B6940 (scalar deleting destructor)
    AsyncLoaderModule* Teardown(unsigned int flags)
    {
        DestroyBody();
        if (flags & 1)
            MemSys::Dealloc(Self());
        return Self();
    }
    // 0x005B6970 (virtual: the module starts - its task joins the logic tasks)
    void Kickoff() { FileLoad::RegisterLogicTask(Self()->m_task); }
};

} // namespace

FileLoad::AsyncLoaderModule* FileLoad::ConstructAsyncLoaderModule(void* memory)
{
    return static_cast<AsyncLoaderModuleMethods*>(memory)->MakeNew();
}

namespace {
static VtableSlot kAsyncFileHandleAdapterVtable_slots[] = {&AsyncFileHandleAdapter::Teardown,
                &AsyncFileHandle::IoCallback,
                &BaseStubs::PrepareAll,
                &BaseStubs::Unseal,
                &BaseStubs::Shut,
                &BaseStubs::IsFinished,
                &BaseStubs::IsOccupied,
                &BaseStubs::IsEOF,
                &BaseStubs::ChangeRank,
                &BaseStubs::SetStream,
                &BaseStubs::SetDestination,
                &BaseStubs::SetBufferSize,
                &BaseStubs::SetBlocking,
                &BaseStubs::ReadIn,
                &BaseStubs::TakeIn,
                &BaseStubs::EmitOut,
                &BaseStubs::GetS,
                &BaseStubs::SeekTo,
                &BaseStubs::Cancel};
void** const kAsyncFileHandleAdapterVtable = reinterpret_cast<void**>(kAsyncFileHandleAdapterVtable_slots);
static VtableSlot kAsyncLoaderModuleMethodsVtable_slots[] = {&AsyncLoaderModuleMethods::Teardown,
                &AsyncLoaderModuleMethods::Kickoff, &GameModules::Skate::StopCallback};
void** const kAsyncLoaderModuleMethodsVtable = reinterpret_cast<void**>(kAsyncLoaderModuleMethodsVtable_slots);
static VtableSlot kPcAsyncFileHandleAdapterVtable_slots[] = {&PcAsyncFileHandleAdapter::Teardown,
                &FileLoad::PcAsyncFileHandle::PcIoCallback, &FileLoad::PcAsyncFileHandle::PlatInit,
                &FileLoad::PcAsyncFileHandle::PlatOpen, &FileLoad::PcAsyncFileHandle::PlatClose,
                &FileLoad::PcAsyncFileHandle::PlatIsDone, &FileLoad::PcAsyncFileHandle::PlatIsBusy,
                &FileLoad::PcAsyncFileHandle::PlatIsEOF, Stubs::IgnoreArgument, Stubs::IgnoreArgument,
                Stubs::IgnoreArgument, Stubs::IgnoreArgument, Stubs::IgnoreArgument, &FileLoad::PcAsyncFileHandle::PlatLoad,
                &FileLoad::PcAsyncFileHandle::PlatRead, Stubs::ReturnZero3, Stubs::ReturnZero2, Stubs::ReturnZero2,
                &BaseStubs::Cancel};
void** const kPcAsyncFileHandleAdapterVtable = reinterpret_cast<void**>(kPcAsyncFileHandleAdapterVtable_slots);
} // namespace

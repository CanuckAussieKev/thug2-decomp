#include "game/file/async.h"

#include <cstddef>
#include <new>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/file/pre.h"
#include "hook/hook.h"

namespace File {

static_assert(offsetof(AsyncFileHandle, m_callbackMask) == 0x14 && offsetof(AsyncFileHandle, m_blocking) == 0x21 &&
                  offsetof(AsyncFileHandle, m_fileSize) == 0x2C && offsetof(AsyncFileHandle, m_busyCount) == 0x38,
              "layout must match the original");

namespace {

constexpr int kMaxHandles = 16;

// Shared with the platform I/O thread: plain (volatile) accesses, as in the original.
AsyncFileHandle** Pool() { return GameVariable<AsyncFileHandle* [kMaxHandles]>(0x007CF138); }
int& HandlesInUse() { return GameVariable<int>(0x007CF178); }
volatile int& BusyCount() { return GameVariable<volatile int>(0x007CF17C); }
volatile bool& IoCompleted() { return GameVariable<volatile bool>(0x007CF180); }

// Completed operations waiting for their callbacks: two lists, one filled while the other is run.
struct PendingCallback {
    AsyncFileHandle* file;
    int event;
    int value;
};
PendingCallback (&CallbackLists())[2][kMaxHandles]
{
    return GameVariable<PendingCallback[2][kMaxHandles]>(0x007CEFB8);
}
int (&CallbackCounts())[2] { return GameVariable<int[2]>(0x007CF184); }
int& CurrentList() { return GameVariable<int>(0x007CF18C); }

// The platform (PC) start-up: creates the handles and the I/O thread (not rewritten yet).
void PlatformInit() { GameFunction<void(__cdecl*)()>(0x005B9B80)(); }

// The handle fields every allocation resets (as the original's Init and get_file_handle do).
void ResetFields(AsyncFileHandle* file)
{
    file->m_fileSize = -1;
    file->m_position = -1;
    file->m_callbackMask = 0xFFFFFFFF;
    file->m_busyCount = 0;
    file->m_callback = nullptr;
    file->m_arg0 = nullptr;
    file->m_arg1 = nullptr;
    file->m_operation = kAsyncIdle;
    file->m_unknown1C = 0;
    file->m_stream = false;
    file->m_blocking = true;
    file->m_bufferSize = 0x10000;
    file->m_priority = 100;
    file->m_result = 0;
    file->m_preFile = nullptr;
}

// Report an operation served from a .pre archive, as the platform does when its I/O completes.
void CompleteFromArchive(AsyncFileHandle* file)
{
    BusyCount() = BusyCount() + 1;
    file->m_busyCount = file->m_busyCount + 1;
    file->Virtual<void>(AsyncFileHandle::kIoCallback, file->m_operation, false, file->m_result, 0);
}

} // namespace

// --- CAsyncFileHandle ----------------------------------------------------------------------------------

// 0x005B66F0 (Init: back to defaults, then the platform's init)
bool AsyncFileHandle::Init()
{
    ResetFields(this);
    return Virtual<bool>(kPlatInit);
}

// 0x005B6380
void AsyncFileHandle::SetCallback(AsyncCallback callback, void* arg0, void* arg1)
{
    m_callback = callback;
    m_arg0 = arg0;
    m_arg1 = arg1;
}

// 0x005B63A0
int AsyncFileHandle::GetFileSize()
{
    if (m_preFile) {
        m_result = Pre::GetFileSize(static_cast<Pre::PreFile::FileInfo*>(m_preFile));
        return m_result;
    }
    while (m_fileSize < 0) {
        // the platform's open sets it (on the I/O thread when overlapped)
    }
    return m_fileSize;
}

// 0x005B63D0
bool AsyncFileHandle::Cancel()
{
    m_operation = kAsyncCancel;
    if (m_preFile) {
        m_result = -1;
        return true;
    }
    return Virtual<bool>(kPlatCancel);
}

// 0x005B6730
bool AsyncFileHandle::IsBusy()
{
    return m_busyCount > 0;
}

// 0x005B6740
int AsyncFileHandle::Read(void* buffer, int size, int count)
{
    m_operation = kAsyncRead;
    if (m_preFile) {
        m_result = Pre::Read(buffer, size, count, static_cast<Pre::PreFile::FileInfo*>(m_preFile));
        CompleteFromArchive(this);
        return m_result;
    }
    return Virtual<int>(kPlatRead, buffer, size, count);
}

// 0x005B67B0
bool AsyncFileHandle::Open(const char* name, bool blocking, int priority)
{
    m_priority = priority;
    m_blocking = blocking;
    m_operation = kAsyncOpen;
    if (Pre::Manager()) {
        m_preFile = Pre::Open(name, "rb", true);
        if (m_preFile) {
            m_result = 1;
            m_blocking = true;
            m_fileSize = static_cast<Pre::PreFile::FileInfo*>(m_preFile)->size;
            CompleteFromArchive(this);
            return m_result != 0;
        }
    }
    return Virtual<bool>(kPlatOpen, name);
}

// 0x005B6840
bool AsyncFileHandle::Close()
{
    m_operation = kAsyncClose;
    if (m_preFile) {
        m_result = Pre::Close(static_cast<Pre::PreFile::FileInfo*>(m_preFile), true);
        CompleteFromArchive(this);
        return m_result != 0;
    }
    return Virtual<bool>(kPlatClose);
}

// 0x005B69F0 (virtual io_callback: an operation finished - queue its callback, or count it done)
void AsyncFileHandle::IoCallback(int event, bool error, int value, int /*extra*/)
{
    m_result = value;
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
        if (Pool()[i] == file) {
            // Swap it with the last handle in use.
            int last = --HandlesInUse();
            Pool()[i] = Pool()[last];
            Pool()[last] = file;
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
        Dbg::Printf("CAsyncFileLoader waiting for io completion: busy count %d completion %d\n", BusyCount(),
                    IoCompleted());
        while (!IoCompleted()) {
        }
        Dbg::Printf("CAsyncFileLoader got completion: busy count %d completion %d\n", BusyCount(), IoCompleted());
        AsyncExecuteCallbacks();
        if (!all)
            return;
        int busy = 0;
        while (busy < kMaxHandles && Pool()[busy]->m_busyCount <= 0)
            ++busy;
        if (busy == kMaxHandles)
            return;
        Dbg::Printf("CAsyncFileLoader still needs to wait for file handle %d\n", busy);
    }
}

// 0x005B6AE0
AsyncFileHandle* AsyncGetHandle()
{
    int first_free = HandlesInUse();
    if (first_free >= kMaxHandles)
        return nullptr;
    // The next handle may still be finishing an operation: use a later idle one instead, or wait.
    if (Pool()[first_free]->m_busyCount > 0) {
        int idle = first_free + 1;
        while (idle < kMaxHandles && Pool()[idle]->m_busyCount > 0)
            ++idle;
        if (idle < kMaxHandles) {
            AsyncFileHandle* swap = Pool()[idle];
            Pool()[idle] = Pool()[first_free];
            Pool()[first_free] = swap;
        } else {
            AsyncFileHandle* busy = Pool()[first_free];
            do {
                AsyncExecuteCallbacks();
                first_free = HandlesInUse();
            } while (busy->m_busyCount > 0);
        }
    }
    AsyncFileHandle* file = Pool()[first_free];
    ResetFields(file);
    file->Virtual<bool>(AsyncFileHandle::kPlatInit);
    return Pool()[HandlesInUse()++];
}

// 0x005B6C60
AsyncFileHandle* AsyncOpen(const char* name, bool blocking, int priority)
{
    AsyncFileHandle* file = AsyncGetHandle();
    if (!file)
        return nullptr;
    if (file->Open(name, blocking, priority))
        return file;
    AsyncFreeHandle(file);
    return nullptr;
}

// 0x005B68A0
bool AsyncClose(AsyncFileHandle* file)
{
    bool result = file->Close();
    AsyncFreeHandle(file);
    return result;
}

} // namespace File

namespace {

using File::AsyncFileHandle;

// 0x005B69A0 (constructor) and 0x005B66C0 (scalar deleting destructor) of CAsyncFileHandle
struct AsyncFileHandleAdapter {
    AsyncFileHandle* Construct()
    {
        auto* file = reinterpret_cast<AsyncFileHandle*>(this);
        File::ResetFields(file);
        file->m_vtable = reinterpret_cast<void**>(0x00657EC8);
        // The base class's platform init, through the base vtable (a debug stub).
        reinterpret_cast<bool(__thiscall*)(AsyncFileHandle*)>(file->m_vtable[AsyncFileHandle::kPlatInit])(file);
        return file;
    }
    AsyncFileHandle* Destroy(unsigned int flags)
    {
        auto* file = reinterpret_cast<AsyncFileHandle*>(this);
        file->m_vtable = reinterpret_cast<void**>(0x00657EC8);
        if (flags & 1)
            Mem::Free(file);
        return file;
    }
};

// The base class's platform methods: debug stubs, each printing its name (vtable slots 2-18).
struct BaseStubs {
    bool Init() { return Stub("STUB: CAsyncFileHandle::Init\n"); }
    bool Open(const char*) { return Stub("STUB: CAsyncFileHandle::Open\n"); }
    bool Close() { return Stub("STUB: CAsyncFileHandle::Close\n"); }
    bool IsDone() { return Stub("STUB: CAsyncFileHandle::IsDone\n"); }
    bool IsBusy() { return Stub("STUB: CAsyncFileHandle::IsBusy\n"); }
    bool IsEOF() { return Stub("STUB: CAsyncFileHandle::IsEOF\n"); }
    void SetPriority(int) { Dbg::Printf("STUB: CAsyncFileHandle::SetPriority\n"); }
    void SetStream(bool) { Dbg::Printf("STUB: CAsyncFileHandle::SetStream\n"); }
    void SetDestination(int) { Dbg::Printf("STUB: CAsyncFileHandle::SetDestination\n"); }
    void SetBufferSize(int) { Dbg::Printf("STUB: CAsyncFileHandle::SetBufferSize\n"); }
    void SetBlocking(bool) { Dbg::Printf("STUB: CAsyncFileHandle::SetBlocking\n"); }
    int Load(void*) { return StubInt("STUB: CAsyncFileHandle::Load\n"); }
    int Read(void*, int, int) { return StubInt("STUB: CAsyncFileHandle::Read\n"); }
    int Write(const void*, int, int) { return StubInt("STUB: CAsyncFileHandle::Write\n"); }
    int GetS(char*, int) { return StubInt("STUB: CAsyncFileHandle::GetS\n"); }
    int Seek(int, int) { return StubInt("STUB: CAsyncFileHandle::Seek\n"); }
    bool Cancel() { return Stub("STUB: CAsyncFileHandle::Cancel\n"); }

    static bool Stub(const char* message)
    {
        Dbg::Printf(message);
        return false;
    }
    static int StubInt(const char* message)
    {
        Dbg::Printf(message);
        return 0;
    }
};

} // namespace

REPLACE(0x005B66F0, &AsyncFileHandle::Init);
REPLACE(0x005B6380, &AsyncFileHandle::SetCallback);
REPLACE(0x005B63A0, &AsyncFileHandle::GetFileSize);
REPLACE(0x005B63D0, &AsyncFileHandle::Cancel);
REPLACE(0x005B6730, &AsyncFileHandle::IsBusy);
REPLACE(0x005B6740, &AsyncFileHandle::Read);
REPLACE(0x005B67B0, &AsyncFileHandle::Open);
REPLACE(0x005B6840, &AsyncFileHandle::Close);
REPLACE(0x005B69F0, &AsyncFileHandle::IoCallback);
REPLACE(0x005B69A0, &AsyncFileHandleAdapter::Construct);
REPLACE(0x005B66C0, &AsyncFileHandleAdapter::Destroy);
REPLACE(0x005B6560, File::AsyncInit);
REPLACE(0x005B6580, File::AsyncBusy);
REPLACE(0x005B6590, File::AsyncFreeHandle);
REPLACE(0x005B65D0, File::AsyncAddCallback);
REPLACE(0x005B9770, File::AsyncSwapCallbackLists);
REPLACE(0x005B68C0, File::AsyncExecuteCallbacks);
REPLACE(0x005B6A50, static_cast<void (*)(bool)>(&File::AsyncWaitForIO));
REPLACE(0x005B6AE0, File::AsyncGetHandle);
REPLACE(0x005B6C60, static_cast<File::AsyncFileHandle* (*)(const char*, bool, int)>(&File::AsyncOpen));
REPLACE(0x005B68A0, File::AsyncClose);
REPLACE(0x005B63F0, &BaseStubs::Init);
REPLACE(0x005B6400, &BaseStubs::Open);
REPLACE(0x005B6420, &BaseStubs::Close);
REPLACE(0x005B6430, &BaseStubs::IsDone);
REPLACE(0x005B6440, &BaseStubs::IsBusy);
REPLACE(0x005B6450, &BaseStubs::IsEOF);
REPLACE(0x005B6460, &BaseStubs::SetPriority);
REPLACE(0x005B6470, &BaseStubs::SetStream);
REPLACE(0x005B6480, &BaseStubs::SetDestination);
REPLACE(0x005B6490, &BaseStubs::SetBufferSize);
REPLACE(0x005B64A0, &BaseStubs::SetBlocking);
REPLACE(0x005B64B0, &BaseStubs::Load);
REPLACE(0x005B64D0, &BaseStubs::Read);
REPLACE(0x005B64F0, &BaseStubs::Write);
REPLACE(0x005B6510, &BaseStubs::GetS);
REPLACE(0x005B6530, &BaseStubs::Seek);
REPLACE(0x005B6550, &BaseStubs::Cancel);

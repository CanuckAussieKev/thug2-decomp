// Self-tests for Net::App (src/game/net/net_app.cpp): the originals against ours on a stand-in memory manager (a
// default and a network heap), a stand-in app and connections laid out in an arena, the rest of the network
// library run as the game's own code where it only touches memory and stood in by recorders where it would reach
// further (the connection's constructor and destructor, the dispatcher, the app's virtual functions). The whole
// arena (relocated) and the recorded calls are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <new>
#include <type_traits>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/net/net_app.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using Mem::ListNode;
using Net::App;
using Net::Conn;
using Net::MsgDesc;
using TestStandIns::Regs;
using TestStandIns::StandIns;

constexpr uint32_t kArenaSize = 0x60000;
constexpr uint32_t kAppOffset = 0x1000;     // the app (0x1F08 bytes)
constexpr uint32_t kConnOffset = 0x3000;    // the stand-in connections
constexpr uint32_t kConnSize = 0x3D20;
constexpr int kConns = 5;
constexpr uint32_t kMiscOffset = 0x16400;   // settings, the app's manager, data, banned entries, peer buffers
constexpr uint32_t kHeapOffset = 0x1A000;   // the default heap
constexpr uint32_t kNetHeapOffset = 0x3A000; // the network heap

uint8_t* g_netB_arena;
std::vector<TestStandIns::Saved> g_netB_saved;
bool g_netB_swapped = false;

// Everything recorded by the stand-ins (and the tests' own results).
std::vector<uint32_t> g_netB_log;
void Log2(std::initializer_list<uint32_t> values) { g_netB_log.insert(g_netB_log.end(), values); }
template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// Scripted answers.
std::map<uint32_t, bool> g_messagesToSend;      // conn -> App::MessagesToSend
std::vector<int> g_dispatchResults;              // in call order
std::vector<bool> g_sequencedResults;            // in call order

void SaveGlobals()
{
    g_netB_saved = {
        {0x007CC979, 1, {}}, // the memory manager is ready
        {0x007D1800, 4, {}}, // the memory manager
        {0x006EFDD8, 4, {}}, // GameNet's network settings
        {0x007D1EC8, 8, {}}, // vblanks (Tmr::GetTime)
        {0x00684FD0, 4, {}}, // vblanks per second
    };
    TestStandIns::SaveAll(g_netB_saved);
    g_netB_swapped = true;
}
void RestoreGlobals()
{
    if (!g_netB_swapped)
        return;
    TestStandIns::RestoreAll(g_netB_saved);
    g_netB_swapped = false;
}
void CleanUp()
{
    TestStandIns::RemoveStandIns();
    RestoreGlobals();
}

// One arena for both runs (64 KB aligned: nothing else shares its segments - the half-pointer pass). At the same
// address the pass treats both runs alike: with an arena each, a 16-bit field that happened to equal one arena's
// segment number was "relocated" in that run only - in the game, a stream start's QueuedMsg length 0x0130 with
// our run's arena at 0x012A0000 (the end segment is in the range), six words "0130CD0E" vs "A006CD0E".
uint8_t* Arena(bool /*original*/)
{
    if (!g_netB_arena)
        g_netB_arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    return g_netB_arena;
}

// The stand-in manager over a fresh (dirty) arena: a default heap, and a network heap.
void SetUpArena(uint8_t* arena)
{
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    std::memset(arena, 0, kHeapOffset);
    std::memset(arena + kHeapOffset, 0xCD, kArenaSize - kHeapOffset);
    GameVariable<bool>(0x007CC979) = true;
    auto* manager = reinterpret_cast<Manager*>(arena);
    GameVariable<void*>(0x007D1800) = manager;
    auto* region = new (arena + 0x600) Region(arena + kHeapOffset, arena + kNetHeapOffset);
    auto* heap = new (arena + 0x620) Heap(region, 1, "default");
    auto* netRegion = new (arena + 0x6C0) Region(arena + kNetHeapOffset, arena + kArenaSize);
    auto* netHeap = new (arena + 0x6E0) Heap(netRegion, 1, "network");
    manager->m_heaps[0] = heap;
    manager->m_heaps[1] = netHeap;
    manager->m_heapCount = 2;
    manager->m_networkHeap = netHeap;
    manager->PushContext(heap);
    GameVariable<uint64_t>(0x007D1EC8) = 123457;
    GameVariable<float>(0x00684FD0) = 60.0f;
}

// The arena as words, pointers into it (and halves of them) made arena-relative, our task callbacks named by
// the game's addresses; then the log likewise.
std::vector<uint32_t> Image(uint8_t* arena)
{
    auto base = reinterpret_cast<uint32_t>(arena);
    auto relocate = [&](uint32_t word) {
        if (word >= base && word <= base + kArenaSize)
            return 0xA0000000u + (word - base);
        if (word == reinterpret_cast<uint32_t>(&App::NetworkMetrics))
            return 0x004593E0u;
        if (word == reinterpret_cast<uint32_t>(&App::ReceiveNetworkData))
            return 0x004593C0u;
        if (word == reinterpret_cast<uint32_t>(&App::ProcessNetworkData))
            return 0x00456CA0u;
        return word;
    };
    std::vector<uint32_t> image(kArenaSize / 4);
    std::memcpy(image.data(), arena, kArenaSize);
    for (uint32_t& word : image) {
        word = relocate(word);
        uint32_t high = word >> 16;
        if (high >= (base >> 16) && high <= ((base + kArenaSize) >> 16))
            word = (word & 0xFFFF) | ((0xA000u + high - (base >> 16)) << 16);
    }
    for (uint32_t word : g_netB_log)
        image.push_back(relocate(word));
    return image;
}

// Clears the stack below the caller: the originals send uninitialised stack bytes (the stream start's padding and
// the 3 bytes past a short first piece), which ours zeroes.
__declspec(noinline) void ZeroStack()
{
    volatile uint8_t area[0x4000];
    for (volatile uint8_t& b : area)
        b = 0;
}

// The guard that makes the original's calls to the functions under test run the originals too (in the game).
struct Originals {
    std::vector<OriginalCode*> guards;
    explicit Originals(bool original)
    {
        if (!original)
            return;
        for (uint32_t address : {0x00457EB0u, 0x00458740u, 0x00458AB0u, 0x00458BE0u, 0x00458C10u, 0x00458CA0u,
                                 0x00458D50u, 0x00458DB0u, 0x00458E60u, 0x00458F80u, 0x00458FB0u, 0x004590C0u,
                                 0x004593C0u, 0x004593E0u, 0x004594A0u, 0x004594C0u, 0x00459660u, 0x00459690u,
                                 0x004597C0u})
            guards.push_back(new OriginalCode(address));
    }
    ~Originals()
    {
        for (OriginalCode* guard : guards)
            delete guard;
    }
};

// --- Calls: the original or ours -----------------------------------------------------------------------------
void Enqueue(bool original, App* app, int handle, MsgDesc* desc)
{
    if (original)
        Original<void(__thiscall*)(App*, int, MsgDesc*)>(0x00457EB0)(app, handle, desc);
    else
        app->EnqueueMessage(handle, desc);
}
void StreamToConn(bool original, App* app, Conn* conn, uint8_t id, uint16_t size, void* data, const char* desc,
                  uint8_t group, bool allAtOnce, bool inPlace)
{
    ZeroStack();
    if (original)
        Original<void(__thiscall*)(App*, Conn*, uint8_t, uint16_t, void*, const char*, uint8_t, bool, bool)>(
            0x00458740)(app, conn, id, size, data, desc, group, allAtOnce, inPlace);
    else
        app->StreamMessageToConn(conn, id, size, data, desc, group, allAtOnce, inPlace);
}
void Stream(bool original, App* app, int handle, uint8_t id, uint16_t size, void* data, const char* desc,
            uint8_t group, bool allAtOnce, bool inPlace)
{
    ZeroStack();
    if (original)
        Original<void(__thiscall*)(App*, int, uint8_t, uint16_t, void*, const char*, uint8_t, bool, bool)>(
            0x00458AB0)(app, handle, id, size, data, desc, group, allAtOnce, inPlace);
    else
        app->StreamMessage(handle, id, size, data, desc, group, allAtOnce, inPlace);
}
void ProcessStreams(bool original, App* app)
{
    ZeroStack();
    if (original)
        Original<void(__thiscall*)(App*)>(0x004590C0)(app);
    else
        app->ProcessStreamMessages();
    // Each connection's streams and sequenced links after each frame (the pieces' timing).
    ListNode* head = &app->m_connections;
    for (ListNode* n = head->next; n != head; n = n->next) {
        auto* conn = static_cast<Conn*>(n->data);
        uint32_t streams = 0, links = 0;
        for (ListNode* l = At<ListNode>(conn, 0x1C90).next; l != &At<ListNode>(conn, 0x1C90); l = l->next)
            ++streams;
        for (ListNode* l = At<ListNode>(conn, 0x28).next; l != &At<ListNode>(conn, 0x28); l = l->next)
            ++links;
        Log2({0x5754u, streams, links});
    }
}
void RunTask(bool original, uint32_t address, void (*ours)(void*), void* task)
{
    ZeroStack();
    if (original)
        Original<void(__cdecl*)(void*)>(address)(task);
    else
        ours(task);
}
template <typename R, typename... A>
R Call(bool original, uint32_t address, R (App::*ours)(A...), App* app, std::type_identity_t<A>... args)
{
    if (original)
        return Original<R(__thiscall*)(App*, A...)>(address)(app, args...);
    return (app->*ours)(args...);
}

// --- The stand-in world -------------------------------------------------------------------------------------
void InitHead(ListNode* head, uint32_t vtable)
{
    head->vtable = reinterpret_cast<void**>(vtable);
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
}
void AddLast(ListNode* head, ListNode* node)
{
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

// The app's virtual functions: 2 ReceiveData, 3 SendEnqueuedMessages(conn).
void** StandInAppVtable(StandIns& s)
{
    static void* vtable[6];
    for (void*& slot : vtable)
        slot = nullptr;
    vtable[2] = s.Thunk(0, [](Regs& r) {
        Log2({0xA2u, r.ecx});
        return 0u;
    });
    vtable[3] = s.Thunk(4, [](Regs& r) {
        Log2({0xA3u, r.ecx, r.args[0], At<uint32_t>(reinterpret_cast<void*>(r.ecx), 8)});
        return 0u;
    });
    return reinterpret_cast<void**>(vtable);
}

// The rest of the library that's stood in, the same for the original and ours (game addresses and, for
// App's methods that ours calls directly, our functions too).
void StandInLibrary(StandIns& s)
{
    // Dbg::Printf (the game's C runtime; ours too: StreamDesc::DestroyBody calls it).
    s.At({0x00401C30, TestStandIns::AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Log2({0xDB6u, TestStandIns::Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    // Conn::Conn(flags): zeroed, linked to itself in its app node.
    s.At({0x00455C80, TestStandIns::AddressOf(&Conn::Construct)}, 4, [](Regs& r) {
        auto* conn = reinterpret_cast<uint8_t*>(r.ecx);
        std::memset(conn, 0x00, kConnSize);
        auto* node = reinterpret_cast<ListNode*>(conn + 0x3CF8);
        node->data = conn;
        node->next = node;
        node->prev = node;
        Log2({0xC0C7u, r.ecx, r.args[0]});
        return r.ecx;
    });
    // ~Conn body.
    s.At({0x00456790, TestStandIns::AddressOf(&Conn::Destroy)}, 0, [](Regs& r) {
        Log2({0xDC0Cu, r.ecx, At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0x60)});
        return 0u;
    });
    // App::MessagesToSend(conn).
    s.At({0x00457AC0, TestStandIns::AddressOf(&Net::App::MessagesDue)}, 4, [](Regs& r) {
        Log2({0x5E2Du, r.ecx, r.args[0]});
        return static_cast<uint32_t>(g_messagesToSend[r.args[0]]);
    });
    // Dispatcher::DispatchMessages(conn, flags).
    s.At({0x0045A830, TestStandIns::AddressOf(&Net::Dispatcher::DispatchMsgHandlers)}, 8, [](Regs& r) {
        Log2({0xD15u, r.ecx, r.args[0], r.args[1]});
        int result = 0;
        if (!g_dispatchResults.empty()) {
            result = g_dispatchResults.front();
            g_dispatchResults.erase(g_dispatchResults.begin());
        }
        return static_cast<uint32_t>(result);
    });
    // App::ProcessSequencedMessages(conn).
    s.At({0x00457560, TestStandIns::AddressOf(&Net::App::DispatchQueuedSequencedMessages)}, 4, [](Regs& r) {
        Log2({0x5E9u, r.ecx, r.args[0]});
        bool result = true;
        if (!g_sequencedResults.empty()) {
            result = g_sequencedResults.front();
            g_sequencedResults.erase(g_sequencedResults.begin());
        }
        return static_cast<uint32_t>(result);
    });
    // ~App body.
    s.At({0x00457710, TestStandIns::AddressOf(&Net::App::DestroyBody)}, 0, [](Regs& r) {
        Log2({0xDA99u, r.ecx});
        return 0u;
    });
}

struct World {
    uint8_t* arena;
    App* app;
    Conn* conns[kConns];
    uint8_t* misc;
    void* settings;
    void* manager;
    void* task;
    uint8_t* data;  // 0x400 bytes of message data
};

Conn* MakeConn(World& w, int index, int handle, uint32_t status, uint32_t flags)
{
    auto* conn = reinterpret_cast<Conn*>(w.arena + kConnOffset + kConnSize * index);
    InitHead(&At<ListNode>(conn, 0x00), 0x00647560);
    InitHead(&At<ListNode>(conn, 0x14), 0x00647564);
    InitHead(&At<ListNode>(conn, 0x28), 0x00647568);
    InitHead(&At<ListNode>(conn, 0x1C90), 0x0064756C);
    At<int>(conn, 0x3C) = handle;
    At<int>(conn, 0x40) = 0x0A000000 + handle * 0x10101;
    At<uint32_t>(conn, 0x60) = status;
    At<uint32_t>(conn, 0x64) = flags;
    for (int group = 0; group < 0x100; ++group)
        At<uint32_t>(conn, 0x78 + 4 * group) = static_cast<uint32_t>(group * 3 + handle * 100);
    At<int>(conn, 0x878) = 40 + handle;
    auto* node = &At<ListNode>(conn, 0x3CF8);
    node->vtable = reinterpret_cast<void**>(0x00647570);
    node->data = conn;
    AddLast(&w.app->m_connections, node);
    w.conns[index] = conn;
    return conn;
}

World MakeWorld(bool original, void** vtable)
{
    World w = {};
    w.arena = Arena(original);
    SetUpArena(w.arena);
    w.app = reinterpret_cast<App*>(w.arena + kAppOffset);
    w.app->m_vtable = vtable;
    w.app->m_time = 1000;
    InitHead(&w.app->m_connections, 0x006475AC);
    InitHead(&w.app->m_banned, 0x006475B0);
    w.misc = w.arena + kMiscOffset;
    w.settings = w.misc;            // +0x128 the connection type
    w.manager = w.misc + 0x200;     // +0x378 the bandwidth allowed
    w.task = w.misc + 0x600;        // +0x1C the app
    At<App*>(w.task, 0x1C) = w.app;
    w.app->m_manager = w.manager;
    GameVariable<void*>(0x006EFDD8) = w.settings;
    w.data = w.misc + 0x800;
    for (int i = 0; i < 0x400; ++i)
        w.data[i] = static_cast<uint8_t>(0x80 | (i * 7 + (i >> 5))); // (never like an arena address)
    return w;
}

MsgDesc Desc(int queue, uint8_t id, uint16_t length, void* data, int priority, int delay)
{
    MsgDesc desc = {};
    Original<void(__thiscall*)(MsgDesc*)>(0x004549F0)(&desc);
    desc.m_queue = queue;
    desc.m_id = id;
    desc.m_length = length;
    desc.m_data = data;
    desc.m_priority = priority;
    desc.m_delay = delay;
    return desc;
}

// --- Messages and streams -------------------------------------------------------------------------------------
// Every queue (default, important, sequenced, an unknown one) to every connection, all but one and one; ready,
// not ready (message 9 still goes), disconnecting and invalid connections; singular messages replacing queued
// ones; priorities ordering the queues; forced sequence ids and groups; an empty message; an RLE-compressed one.
// Then streamed messages: small, all at once, a few pieces a frame (copied, in place, one piece left, one byte
// left) on a normal and a slow connection, with the slow network setting, and the send task.
std::vector<uint32_t> RunMessages(bool original)
{
    StandIns s;
    StandInLibrary(s);
    void** vtable = StandInAppVtable(s);
    g_netB_log.clear();
    World w = MakeWorld(original, vtable);
    Originals guard(original);
    App* app = w.app;
    Conn* c0 = MakeConn(w, 0, 1, 1, 2);       // ready, remote
    Conn* c1 = MakeConn(w, 1, 2, 0, 2 | 4);   // not ready
    Conn* c2 = MakeConn(w, 2, 3, 8, 2);       // disconnecting
    Conn* c3 = MakeConn(w, 3, 5, 1, 1);       // ready, local
    MakeConn(w, 4, 6, 4 | 1, 2);              // invalid (and not "ready": status 5)
    At<int>(c3, 0x3D10) = 1;                  // a slow connection
    uint8_t* d = w.data;

    MsgDesc e;
    e = Desc(0, 0x20, 7, d, 0x80, 5);
    Enqueue(original, app, 0xFF, &e);
    e = Desc(0, 0x21, 9, d + 3, 0x90, 1);
    Enqueue(original, app, 0xFF, &e);
    e = Desc(0, 0x20, 5, d + 9, 0x40, 0);
    e.m_singular = true;
    Enqueue(original, app, 0x80 | 1, &e);
    e = Desc(0, 9, 4, d + 11, 0x80, 0);
    Enqueue(original, app, 2, &e);
    e = Desc(0, 0x22, 4, d + 12, 0x80, 0);
    Enqueue(original, app, 2, &e);
    e = Desc(0, 9, 4, d + 14, 0x80, 0);
    Enqueue(original, app, 0xFF, &e);
    e = Desc(0, 9, 4, d + 15, 0x80, 0);
    Enqueue(original, app, 0x80 | 5, &e);
    e = Desc(0, 0x21, 6, d + 13, 0x85, 2);
    e.m_singular = true;
    Enqueue(original, app, 5, &e);
    e = Desc(0, 0x23, 0, nullptr, 0x80, 0);
    Enqueue(original, app, 0xFF, &e);
    e = Desc(1, 0x30, 8, d + 20, 0x80, 2);
    Enqueue(original, app, 0xFF, &e);
    e = Desc(1, 0x31, 8, d + 22, 0x70, 3);
    Enqueue(original, app, 0xFF, &e);
    e = Desc(1, 0x30, 3, d + 24, 0x90, 4);
    e.m_singular = true;
    Enqueue(original, app, 0x80 | 5, &e);
    e = Desc(1, 0x31, 5, d + 26, 0x80, 0);
    Enqueue(original, app, 3, &e);
    e = Desc(1, 0x32, 5, d + 27, 0x80, 0);
    e.m_singular = true;
    Enqueue(original, app, 2, &e);
    e = Desc(2, 0x40, 12, d + 30, 0x55, 6);
    e.m_groupId = 3;
    e.m_stream = 1;
    Enqueue(original, app, 0xFF, &e);
    e = Desc(2, 0x41, 10, d + 31, 0x66, 7);
    e.m_groupId = 7;
    e.m_forcedSequenceId = 0x77;
    e.m_singular = true;
    Enqueue(original, app, 0x80 | 6, &e);
    e = Desc(2, 0x42, 11, d + 32, 0x67, 8);
    e.m_groupId = 3;
    Enqueue(original, app, 1, &e);
    e = Desc(2, 0x43, 11, d + 33, 0x68, 9);
    Enqueue(original, app, 3, &e);
    e = Desc(3, 0x44, 11, d + 34, 0x68, 9);
    Enqueue(original, app, 0xFF, &e);
    std::memset(d + 0x300, 0xC1, 0x40);
    e = Desc(0, 0x45, 0x40, d + 0x300, 0x80, 0);
    e.m_compression = 1;
    Enqueue(original, app, 1, &e);
    e = Desc(2, 0x46, 0x40, d + 0x300, 0x80, 0);
    e.m_compression = 1;
    e.m_groupId = 3;
    Enqueue(original, app, 1, &e);

    Stream(original, app, 0xFF, 0x50, 0x80, d, "small", 4, false, false);
    Stream(original, app, 1, 0x51, 0x350, d + 5, "all at once", 5, true, false);
    Stream(original, app, 3, 0x52, 0x40, d + 6, "to a disconnecting one", 5, true, false);
    StreamToConn(original, app, c1, 0x53, 0x2A0, d + 7, "later, copied", 6, false, false);
    StreamToConn(original, app, c0, 0x54, 0x390, d + 8, "later, in place", 6, false, true);
    StreamToConn(original, app, c0, 0x55, 0x350, d + 9, "three more pieces", 2, false, false);
    StreamToConn(original, app, c3, 0x56, 0x101, d + 10, "one more byte", 9, false, false);
    StreamToConn(original, app, c3, 0x57, 0x300, d + 11, "slow", 9, false, false);
    StreamToConn(original, app, c2, 0x58, 0x100, d + 12, "exactly one piece", 1, false, false);
    // Stream pieces already queued count against the limit: SetPieces leaves n of them marked.
    auto setPieces = [](Conn* conn, int n) {
        auto* queue = &At<ListNode>(conn, 0x28);
        for (ListNode* link = queue->next; link != queue; link = link->next)
            At<uint8_t>(link, 0x16) = n-- > 0 ? 1 : 0;
    };
    ProcessStreams(original, app);  // c0 full (8 queued), c1: both pieces, c3 (slow) full
    setPieces(c0, 3);
    ProcessStreams(original, app);  // c0: 2 pieces of "in place"
    setPieces(c0, 0);
    At<int>(w.settings, 0x128) = 1; // the slow network setting
    ProcessStreams(original, app);  // c0: 1 piece: "in place" done
    At<int>(w.settings, 0x128) = 0;
    setPieces(c0, 0);
    setPieces(c3, 1);
    ProcessStreams(original, app);  // c0: "one more piece" (3 pieces); c3: "one more byte"
    setPieces(c3, 0);
    RunTask(original, 0x004594A0, &App::SendNetworkData, w.task);
    for (int i = 0; i < 3; ++i) {
        setPieces(c3, 0);
        ProcessStreams(original, app);
    }
    Log2({app->m_time});
    std::vector<uint32_t> image = Image(w.arena);
    GameVariable<void*>(0x007D1800) = nullptr;
    return image;
}

void TestNetAppMessages()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestNetAppMessages skipped (the memory manager is already set up)");
        return;
    }
    SaveGlobals();
    std::vector<uint32_t> expected = RunMessages(true);
    std::vector<uint32_t> actual = RunMessages(false);
    RestoreGlobals();
    TestStandIns::Compare(expected, actual, "net app: queued and streamed messages");
}

// --- Connections, the tasks, construction -----------------------------------------------------------------------
// The constructor; new connections (to the maximum) and their handles (also with none free); the first
// connection; banned addresses; bandwidth (each way BandwidthExceeded answers); SendData both ways; the metrics
// and receive tasks; terminating connections (each condition); ProcessData (local data handed over, each
// dispatcher answer, the sequenced messages stopping it); the deleting destructor.
std::vector<uint32_t> RunConnections(bool original)
{
    StandIns s;
    StandInLibrary(s);
    void** vtable = StandInAppVtable(s);
    g_netB_log.clear();
    g_messagesToSend.clear();
    World w = MakeWorld(original, vtable);
    Originals guard(original);
    App* app = w.app;
    std::memset(app, 0xEE, sizeof(App));
    Log2({reinterpret_cast<uint32_t>(Call(original, 0x004594C0, &App::Construct, app, 0x1234))});
    // Keep the constructed app's image, then make it usable with the stand-in vtable.
    {
        std::vector<uint32_t> words(sizeof(App) / 4);
        std::memcpy(words.data(), app, sizeof(App));
        // (only the relocation-free parts are needed here: the whole arena is compared at the end too)
        uint32_t base = reinterpret_cast<uint32_t>(w.arena);
        for (uint32_t word : words)
            Log2({word >= base && word < base + kArenaSize ? 0xA0000000u + (word - base) : word});
    }
    app->m_vtable = vtable;
    app->m_manager = w.manager;
    app->m_maxConnections = 4;
    app->m_numConnections = 0;
    app->m_time = 5000;
    At<int>(w.manager, 0x378) = 3000;

    // New connections.
    Conn* conns[4] = {};
    for (int i = 0; i < 3; ++i) {
        GameVariable<uint64_t>(0x007D1EC8) = 1000 + 77 * i;
        conns[i] = Call(original, 0x004597C0, &App::NewConnection, app, 0x0A000001 + i,
                        static_cast<uint16_t>(0x1000 + i), 0x10 + i);
        Log2({reinterpret_cast<uint32_t>(conns[i])});
        app->m_numConnections = i + 1;
    }
    At<int>(conns[1], 0x3C) = 9; // handle 2 free again
    Log2({static_cast<uint32_t>(Call(original, 0x00458F80, &App::GetFreeHandle, app))});
    app->m_numConnections = 4;
    Log2({reinterpret_cast<uint32_t>(Call(original, 0x004597C0, &App::NewConnection, app, 7, uint16_t(8), 9))});
    app->m_numConnections = 3;
    conns[3] = Call(original, 0x004597C0, &App::NewConnection, app, 0x0A000009, uint16_t(0x2222), 0);
    Log2({reinterpret_cast<uint32_t>(conns[3])});

    // Every handle taken: 255 small connections in a list of their own.
    {
        static uint8_t many[255][0x60];
        static uint8_t head[sizeof(App)];
        std::memset(many, 0, sizeof(many));
        auto* other = reinterpret_cast<App*>(head);
        InitHead(&other->m_connections, 0x006475AC);
        for (int i = 0; i < 255; ++i) {
            At<int>(many[i], 0x3C) = 255 - i;
            auto* node = &At<ListNode>(many[i], 0x40);
            node->data = many[i];
            AddLast(&other->m_connections, node);
        }
        Log2({static_cast<uint32_t>(Call(original, 0x00458F80, &App::GetFreeHandle, other))});
        At<int>(many[3], 0x3C) = 300;
        Log2({static_cast<uint32_t>(Call(original, 0x00458F80, &App::GetFreeHandle, other))});
    }

    // The first connection (and of an empty list).
    {
        Net::ConnSearch search = {reinterpret_cast<void**>(0x0064759C), nullptr};
        Log2({reinterpret_cast<uint32_t>(Call(original, 0x00458BE0, &App::FirstConnection, app, &search)),
              reinterpret_cast<uint32_t>(search.m_node)});
        static uint8_t emptyApp[sizeof(App)];
        InitHead(&reinterpret_cast<App*>(emptyApp)->m_connections, 0x006475AC);
        search.m_node = reinterpret_cast<ListNode*>(1);
        Log2({reinterpret_cast<uint32_t>(Call(original, 0x00458BE0, &App::FirstConnection,
                                              reinterpret_cast<App*>(emptyApp), &search)),
              reinterpret_cast<uint32_t>(search.m_node)});
    }

    // Banned addresses.
    Log2({Call(original, 0x00458D50, &App::IsConnectionBanned, app, conns[0])});
    for (int i = 0; i < 3; ++i) {
        auto* banned = reinterpret_cast<ListNode*>(w.misc + 0x700 + 0x20 * i);
        banned->vtable = reinterpret_cast<void**>(0x006475B0);
        banned->data = banned;
        At<int>(banned, 0x14) = i == 1 ? 0x0A000003 : 0x0B000000 + i;
        AddLast(&app->m_banned, banned);
    }
    for (Conn* conn : conns)
        Log2({Call(original, 0x00458D50, &App::IsConnectionBanned, app, conn)});

    // Bandwidth: flags (+0x64: 2 remote, 4 foreign), out bytes (+0x2CE8 + 0x1008), limits (+0x3D1C).
    auto setConn = [&](int i, uint32_t flags, int bytes, int limit) {
        At<uint32_t>(conns[i], 0x64) = flags;
        At<int>(conns[i], 0x2CE8 + 0x1008) = bytes;
        At<int>(conns[i], 0x3D1C) = limit;
    };
    setConn(0, 2, 700, 2000);
    setConn(1, 2 | 4, 500, 2000);
    setConn(2, 1, 9999, 2000);
    setConn(3, 2, 300, 2000);
    Log2({static_cast<uint32_t>(Call(original, 0x00458DB0, &App::GetBandwidthUsed, app))});
    for (int total : {1500, 1501, 4000}) {
        At<int>(w.manager, 0x378) = total;
        for (Conn* conn : conns)
            Log2({Call(original, 0x00458E60, &App::BandwidthExceeded, app, conn)});
    }
    At<int>(w.manager, 0x378) = 20000;
    setConn(3, 2, 300, 300); // at its own limit
    Log2({Call(original, 0x00458E60, &App::BandwidthExceeded, app, conns[3])});
    setConn(3, 2, 300, 301);
    Log2({Call(original, 0x00458E60, &App::BandwidthExceeded, app, conns[3])});
    // Nobody shares (none remote and not foreign): the whole total.
    setConn(0, 2 | 4, 700, 30000);
    setConn(3, 1, 300, 30000);
    At<int>(w.manager, 0x378) = 1600;
    Log2({Call(original, 0x00458E60, &App::BandwidthExceeded, app, conns[0])});
    At<int>(w.manager, 0x378) = 1300; // (its whole share is over 700, half of it not)
    Log2({Call(original, 0x00458E60, &App::BandwidthExceeded, app, conns[0])});
    At<int>(w.manager, 0x378) = 1201;
    setConn(3, 2, 300, 30000);
    setConn(1, 2, 500, 30000);
    Log2({Call(original, 0x00458E60, &App::BandwidthExceeded, app, conns[3])});
    Log2({Call(original, 0x00458E60, &App::BandwidthExceeded, app, conns[1])});
    // Exactly its share (two sharing 1200).
    setConn(0, 2, 600, 30000);
    setConn(1, 2, 0, 30000);
    setConn(2, 1, 0, 30000);
    setConn(3, 1, 0, 30000);
    At<int>(w.manager, 0x378) = 1200;
    Log2({Call(original, 0x00458E60, &App::BandwidthExceeded, app, conns[0])});
    setConn(0, 2, 601, 30000);
    Log2({Call(original, 0x00458E60, &App::BandwidthExceeded, app, conns[0])});

    // Sending.
    Call(original, 0x00458CA0, &App::SendData, app, false);
    Call(original, 0x00458CA0, &App::SendData, app, true);
    Call(original, 0x00458C10, &App::SendEnqueuedMessages, app);
    for (Conn* conn : conns)
        Log2({At<uint32_t>(conn, 0x3CF4)});

    // The metrics task: each connection's in and out meters over the last second.
    for (int i = 0; i < 4; ++i) {
        for (uint32_t meter : {0x1CDCu, 0x2CE8u}) {
            uint8_t* m = reinterpret_cast<uint8_t*>(conns[i]) + meter;
            At<int>(m, 0x800) = 5 + i;
            for (int k = 0; k < 9; ++k) {
                At<int>(m, 8 * k) = 100 * (k + 1) + i + static_cast<int>(meter & 0xF);
                At<int>(m, 8 * k + 4) = 4100 + 150 * k - 30 * i;
            }
        }
    }
    RunTask(original, 0x004593E0, &App::NetworkMetrics, w.task);
    for (Conn* conn : conns)
        Log2({At<uint32_t>(conn, 0x1CDC + 0x1008), At<uint32_t>(conn, 0x2CE8 + 0x1008)});

    // ProcessData: conns 0 and 2 local with peer buffers; the dispatcher's answers.
    uint8_t* readBuffers = w.misc + 0x2000;
    for (int i = 0; i < 4; ++i) {
        At<uint8_t*>(conns[i], 0x54) = readBuffers + 0x100 * i;
        At<uint8_t*>(conns[i], 0x58) = readBuffers + 0x100 * i + 4 * i;
    }
    for (int i : {0, 2}) {
        At<uint32_t>(conns[i], 0x64) |= 1;
        uint8_t* peer = w.misc + 0x1800 + 0x100 * i;
        At<void*>(conns[i], 0x3D0C) = peer;
        At<uint8_t*>(peer, 0x4C) = w.data + 0x10 * i;
        At<uint8_t*>(peer, 0x50) = w.data + 0x10 * i + 13 + i;
    }
    At<uint32_t>(conns[1], 0x64) |= 1; // local without a buffer
    {
        uint8_t* peer = w.misc + 0x1B00; // not local, with a buffer: left alone
        At<void*>(conns[3], 0x3D0C) = peer;
        At<uint8_t*>(peer, 0x4C) = w.data + 0x40;
        At<uint8_t*>(peer, 0x50) = w.data + 0x48;
        At<uint32_t>(conns[3], 0x64) &= ~1u;
    }
    Log2({Call(original, 0x00459690, &App::ProcessData, app)});
    g_dispatchResults = {1, 2};
    Log2({Call(original, 0x00459690, &App::ProcessData, app)});
    g_dispatchResults = {0, 4, 3};
    Log2({Call(original, 0x00459690, &App::ProcessData, app)});
    g_dispatchResults = {};
    g_sequencedResults = {true, false};
    Log2({Call(original, 0x00459690, &App::ProcessData, app)});
    g_sequencedResults = {};
    for (Conn* conn : conns)
        Log2({reinterpret_cast<uint32_t>(At<uint8_t*>(conn, 0x58))});

    // Terminating: status (+0x60: 4 invalid, 8 disconnecting), read / write positions, messages to send.
    auto setTerm = [&](int i, uint32_t status, int unread, bool messages) {
        At<uint32_t>(conns[i], 0x60) = status;
        At<uint8_t*>(conns[i], 0x58) = At<uint8_t*>(conns[i], 0x54) + unread;
        g_messagesToSend[reinterpret_cast<uint32_t>(conns[i])] = messages;
    };
    setTerm(0, 8 | 1, 0, false); // disconnecting, done: invalidated, deleted
    setTerm(1, 8, 0, true);      // disconnecting, still sending
    setTerm(2, 4, 5, false);     // invalid, unread data
    setTerm(3, 4, 0, true);      // invalid, still sending
    GameVariable<uint64_t>(0x007D1EC8) = 999999;
    RunTask(original, 0x004593C0, &App::ReceiveNetworkData, w.task);
    Log2({app->m_time});
    setTerm(2, 4, -1, false);    // invalid, read past the end: deleted
    setTerm(3, 1, 0, false);     // valid
    Call(original, 0x00458FB0, &App::TerminateInvalidConnections, app);
    setTerm(1, 8, 0, false);
    Call(original, 0x00458FB0, &App::TerminateInvalidConnections, app);
    Log2({static_cast<uint32_t>(Call(original, 0x00458DB0, &App::GetBandwidthUsed, app))});

    // The deleting destructor (on a heap block: freed or not).
    for (unsigned int flags : {0u, 1u, 3u}) {
        auto* block = static_cast<App*>(Mem::Manager::Get()->Allocate(0x40, 1, nullptr));
        Log2({reinterpret_cast<uint32_t>(block),
              reinterpret_cast<uint32_t>(Call(original, 0x00459660, &App::Destroy, block, flags))});
    }
    std::vector<uint32_t> image = Image(w.arena);
    GameVariable<void*>(0x007D1800) = nullptr;
    return image;
}

void TestNetAppConnections()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestNetAppConnections skipped (the memory manager is already set up)");
        return;
    }
    SaveGlobals();
    std::vector<uint32_t> expected = RunConnections(true);
    std::vector<uint32_t> actual = RunConnections(false);
    RestoreGlobals();
    TestStandIns::Compare(expected, actual, "net app: connections, tasks, construction");
}

} // namespace

SELFTEST(Early, TestNetAppMessages, CleanUp);
SELFTEST(Early, TestNetAppConnections, CleanUp);

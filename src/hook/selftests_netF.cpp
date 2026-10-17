// Self-tests for Net::Conn (src/game/net/net_conn.cpp) and the list types of its range (net_conn.h, net_lists.cpp's
// additions, App::ProcessNetworkData): the originals against ours on a stand-in memory manager over a 64 KB-aligned
// arena. Connections are built (original constructor or ours) on dirty memory, filled with real links, queued
// messages and streams (the game's own link constructors), and taken apart by each queue function and the
// destructor; the whole arena (relocated) and the traced results are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/net_access_c.h"
#include "game/net/net_app.h"
#include "game/net/net_app_view.h"
#include "game/net/net_conn.h"
#include "game/net/net_lists.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using Mem::ListNode;
using Net::Conn;
using TestStandIns::Regs;
using TestStandIns::StandIns;

constexpr uint32_t kArenaSize = 0x40000;
constexpr uint32_t kConnOffset = 0x1000; // four connections
constexpr uint32_t kConnSize = 0x3D20;
constexpr int kConns = 4;
constexpr uint32_t kMiscOffset = 0x11000; // the fake app, list heads, nodes
constexpr uint32_t kHeapOffset = 0x14000;

uint8_t* g_netF_arenas[2];
std::vector<TestStandIns::Saved> g_netF_saved;
bool g_netF_swapped = false;
std::vector<uint32_t> g_netF_log;
void Log2(std::initializer_list<uint32_t> values) { g_netF_log.insert(g_netF_log.end(), values); }

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

void SaveGlobals()
{
    g_netF_saved = {
        {0x007CC979, 1, {}}, // the memory manager is ready
        {0x007D1800, 4, {}}, // the memory manager
        {0x0067D800, 4, {}}, // the connections' running number
    };
    TestStandIns::SaveAll(g_netF_saved);
    g_netF_swapped = true;
}
void CleanUp()
{
    TestStandIns::RemoveStandIns();
    if (g_netF_swapped) {
        TestStandIns::RestoreAll(g_netF_saved);
        g_netF_swapped = false;
    }
}

uint8_t* Arena(bool original)
{
    uint8_t*& arena = g_netF_arenas[original ? 0 : 1];
    if (!arena) // 64 KB aligned: nothing else shares the arena's segments (the half-pointer pass)
        arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    return arena;
}

void SetUpArena(uint8_t* arena)
{
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    std::memset(arena, 0, kConnOffset);
    std::memset(arena + kConnOffset, 0x5A, kHeapOffset - kConnOffset); // dirty connections and misc
    std::memset(arena + kHeapOffset, 0xCD, kArenaSize - kHeapOffset);
    GameVariable<bool>(0x007CC979) = true;
    auto* manager = reinterpret_cast<Manager*>(arena);
    GameVariable<void*>(0x007D1800) = manager;
    auto* region = new (arena + 0x600) Region(arena + kHeapOffset, arena + kArenaSize);
    auto* heap = new (arena + 0x620) Heap(region, 1, "default");
    manager->m_heaps[0] = heap;
    manager->m_heapCount = 1;
    manager->PushContext(heap);
    GameVariable<uint32_t>(0x0067D800) = 0x1234;
}

std::vector<uint32_t> Image(uint8_t* arena)
{
    auto base = reinterpret_cast<uint32_t>(arena);
    auto relocate = [&](uint32_t word) {
        if (word >= base && word <= base + kArenaSize)
            return 0xA0000000u + (word - base);
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
    for (uint32_t word : g_netF_log)
        image.push_back(relocate(word));
    image.push_back(GameVariable<uint32_t>(0x0067D800));
    return image;
}

// The original or ours.
template <typename T, typename R, typename... A>
R Call(bool original, uint32_t address, R (T::*ours)(A...), T* object, std::type_identity_t<A>... args)
{
    if (original)
        return Original<R(__thiscall*)(T*, A...)>(address)(object, args...);
    return (object->*ours)(args...);
}
template <typename T, typename R, typename... A>
uint32_t Value(bool original, uint32_t address, R (T::*ours)(A...), T* object, std::type_identity_t<A>... args)
{
    if constexpr (std::is_same_v<R, bool>)
        return Call(original, address, ours, object, args...) ? 1u : 0u;
    else if constexpr (std::is_pointer_v<R>)
        return reinterpret_cast<uint32_t>(Call(original, address, ours, object, args...));
    else
        return static_cast<uint32_t>(Call(original, address, ours, object, args...));
}

struct Originals {
    std::vector<OriginalCode*> guards;
    explicit Originals(bool original)
    {
        if (!original)
            return;
        for (uint32_t address :
             {0x00455970u, 0x00455980u, 0x00455990u, 0x004559A0u, 0x004559B0u, 0x004559C0u, 0x004559D0u, 0x004559E0u,
              0x004559F0u, 0x00455A00u, 0x00455A10u, 0x00455A20u, 0x00455A30u, 0x00455A40u, 0x00455A80u, 0x00455AB0u,
              0x00455AE0u, 0x00455B10u, 0x00455B40u, 0x00455B50u, 0x00455B70u, 0x00455B90u, 0x00455BB0u, 0x00455BD0u,
              0x00455C10u, 0x00455C20u, 0x00455C30u, 0x00455C60u, 0x00455C80u, 0x00455E50u, 0x00455F30u, 0x00456210u,
              0x00456370u, 0x00456540u, 0x004565E0u, 0x00456630u, 0x004566D0u, 0x00456770u, 0x00456790u, 0x00456CA0u,
              0x00457010u, 0x00456970u, 0x00457050u})
            guards.push_back(new OriginalCode(address));
    }
    ~Originals()
    {
        for (OriginalCode* guard : guards)
            delete guard;
    }
};

// --- Building a connection's contents (the same game code in both runs) ---------------------------------------
void AddLast(ListNode* head, ListNode* node)
{
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}
template <typename Msg>
Msg* NewMsg(uint8_t id, int refs, uint16_t length)
{
    auto* message = static_cast<Msg*>(Mem::New(sizeof(Msg)));
    message->m_id = id;
    message->m_length = length;
    message->m_data = length ? static_cast<char*>(Mem::New(length)) : nullptr;
    if (length)
        std::memset(message->m_data, id, length);
    message->m_refCount = refs;
    return message;
}
// The game's link constructors (wt/netE's range).
ListNode* NewLink(uint32_t constructor, uint32_t size, void* message)
{
    void* memory = Mem::New(size);
    return GameFunction<ListNode*(__thiscall*)(void*, void*)>(constructor)(memory, message);
}
void AddNormal(Conn* conn, Net::QueuedMsg* message)
{
    ListNode* link = NewLink(0x004555F0, 0x1C, message);
    At<uint32_t>(link, 0x18) = 0x700 + message->m_id;
    AddLast(&conn->m_normalMsgs, link);
}
void AddImportant(Conn* conn, Net::QueuedMsg* message, uint8_t resends, uint32_t packetstamp, uint32_t sent)
{
    auto* link = reinterpret_cast<Net::MsgImpLink*>(NewLink(0x00455620, 0x28, message));
    link->m_NumResends = resends;
    link->m_Packetstamp = packetstamp;
    link->m_Timestamp = sent;
    AddLast(&conn->m_importantMsgs, &link->m_node);
}
Net::MsgSeqLink* NewSeqLink(Net::SeqQueuedMsg* message, uint8_t resends, bool stream, uint32_t packetstamp, uint32_t sent)
{
    auto* link = reinterpret_cast<Net::MsgSeqLink*>(NewLink(0x00455650, 0x2C, message));
    link->m_NumResends = resends;
    link->m_StreamMessage = stream;
    link->m_Packetstamp = packetstamp;
    link->m_Timestamp = sent;
    return link;
}
void AddStream(ListNode* head, bool withDesc, bool withData, bool inPlace)
{
    Net::StreamDesc* desc = nullptr;
    if (withDesc) {
        desc = static_cast<Net::StreamDesc*>(Mem::New(sizeof(Net::StreamDesc)));
        std::memset(desc, 0x11, sizeof(Net::StreamDesc));
        desc->m_data = withData ? static_cast<uint8_t*>(Mem::New(0x30)) : nullptr;
        desc->m_sendInPlace = inPlace;
    }
    AddLast(head, NewLink(0x00455680, 0x18, desc));
}

// Each queue of `conn` filled; `variant` changes the resend counts, packetstamps and which lists have what.
void Fill(Conn* conn, int variant)
{
    using Net::QueuedMsg;
    using Net::SeqQueuedMsg;
    uint8_t id = static_cast<uint8_t>(0x10 * (variant + 1));
    auto* n1 = NewMsg<QueuedMsg>(id + 1, 1, 8);
    auto* n2 = NewMsg<QueuedMsg>(id + 2, 2, 12);
    auto* n3 = NewMsg<QueuedMsg>(id + 3, 5, 0);
    AddNormal(conn, n1);
    AddNormal(conn, n2);
    AddNormal(conn, n2);
    AddNormal(conn, n3);
    auto* i1 = NewMsg<QueuedMsg>(id + 4, 1, 6);
    auto* i2 = NewMsg<QueuedMsg>(id + 5, 3, 10);
    AddImportant(conn, i1, static_cast<uint8_t>(variant == 1 ? 12 : 3), 100, 11);
    AddImportant(conn, i2, 7, 200, 22);
    AddImportant(conn, i2, 1, variant == 2 ? 300 : 100, 33);
    auto* s1 = NewMsg<SeqQueuedMsg>(id + 6, 1, 4);
    auto* s2 = NewMsg<SeqQueuedMsg>(id + 7, 2, 0);
    AddLast(&conn->m_sequencedMsgs, &NewSeqLink(s1, 9, true, 200, 44)->m_node);
    AddLast(&conn->m_sequencedMsgs, &NewSeqLink(s2, 2, false, 100, 55)->m_node);
    AddLast(&conn->m_sequencedMsgs, &NewSeqLink(s2, 0, variant != 1, 300, 66)->m_node);
    // received ahead of their turn: their own messages (no reference count), or none
    AddLast(&conn->m_receivedQueues[0], &NewSeqLink(NewMsg<SeqQueuedMsg>(id + 8, 1, 5), 0, false, 0, 0)->m_node);
    AddLast(&conn->m_receivedQueues[0], &NewSeqLink(nullptr, 0, false, 0, 0)->m_node);
    AddLast(&conn->m_receivedQueues[7 + variant], &NewSeqLink(NewMsg<SeqQueuedMsg>(id + 9, 1, 0), 0, false, 0, 0)->m_node);
    AddLast(&conn->m_receivedQueues[255], &NewSeqLink(NewMsg<SeqQueuedMsg>(id + 10, 1, 3), 0, false, 0, 0)->m_node);
    AddStream(&conn->m_receivedStreams, true, true, false);
    AddStream(&conn->m_receivedStreams, false, false, false);
    AddStream(&conn->m_sendStreams, true, true, true);
    AddStream(&conn->m_sendStreams, true, false, false);
}

// The app's ProcessData (virtual slot 1), for the process task.
void** StandInAppVtable(StandIns& s)
{
    static void* vtable[6];
    for (void*& slot : vtable)
        slot = nullptr;
    vtable[1] = s.Thunk(0, [](Regs& r) {
        Log2({0xA1u, r.ecx, At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0x1410)});
        return 0x101u;
    });
    return reinterpret_cast<void**>(vtable);
}

std::vector<uint32_t> Run(bool original)
{
    g_netF_log.clear();
    uint8_t* arena = Arena(original);
    SetUpArena(arena);
    auto* s = new StandIns;
    // Dbg::Printf (~StreamDesc's message; the game's C runtime)
    s->At({0x00401C30, TestStandIns::AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Log2({0xDB6u, TestStandIns::Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    void** appVtable = StandInAppVtable(*s);
    Originals guards(original);
    uint8_t* misc = arena + kMiscOffset;

    // Construction (dirty memory, the first 64 groups' sequence ids zeroed only).
    Conn* conns[kConns];
    for (int i = 0; i < kConns; ++i) {
        auto* memory = reinterpret_cast<Conn*>(arena + kConnOffset + i * kConnSize);
        conns[i] = Call(original, 0x00455C80, &Conn::Construct, memory, i == 2 ? 6 : 1 << i);
        Log2({reinterpret_cast<uint32_t>(conns[i])});
    }
    for (int i = 0; i < kConns; ++i)
        Fill(conns[i], i);

    // Getters and setters.
    Conn* c = conns[0];
    c->m_sendInterval = 0x4321;
    Log2({Value(original, 0x00455970, &Conn::GetSendInterval, c)});
    for (bool force : {true, false, true}) {
        c->m_sendNow = !force;
        Call(original, 0x00455980, &Conn::SetForceSendThisFrame, c, force);
        Log2({Value(original, 0x00455990, &Conn::GetForceSendThisFrame, c)});
    }
    c->m_bandwidthType = 0x77;
    Log2({Value(original, 0x004559A0, &Conn::GetBandwidthType, c)});
    Call(original, 0x004559B0, &Conn::SetBandwidth, c, 98765);
    Log2({Value(original, 0x004559C0, &Conn::GetBandwidth, c)});
    for (int type : {0, 1, 2, 3, -1, 2}) {
        c->m_bandwidth = 5555;
        Call(original, 0x00455A40, &Conn::SetBandwidthType, c, type);
        Log2({static_cast<uint32_t>(c->m_bandwidthType), static_cast<uint32_t>(c->m_bandwidth)});
    }
    c->m_latencyTest.m_averageLatency = 321;
    for (uint32_t flags : {0u, 1u, 2u, 4u, 7u, 6u, 0xF8u}) {
        c->m_flags = flags;
        Log2({Value(original, 0x00455A10, &Conn::IsForeign, c), Value(original, 0x00455A20, &Conn::IsLocal, c),
              Value(original, 0x00455A30, &Conn::IsRemote, c), Value(original, 0x00455B40, &Conn::GetAveLatency, c)});
    }
    for (uint32_t status : {0u, 1u, 2u, 4u, 5u, 6u, 0xFBu}) {
        for (uint32_t flags : {0u, 1u, 6u}) {
            for (int latency : {100, 249, 250, 251, 1000, -5, 0x40000001}) {
                c->m_status = status;
                c->m_flags = flags;
                c->m_latencyTest.m_averageLatency = latency;
                Log2({Value(original, 0x00455C30, &Conn::GetResendThreshold, c)});
            }
        }
        c->m_status = status;
        Log2({Value(original, 0x00455C20, &Conn::IsValid, c)});
        Call(original, 0x00455C10, &Conn::Invalidate, c);
        Log2({c->m_status, Value(original, 0x00455C20, &Conn::IsValid, c)});
    }

    // The queues: counts, flags, resends.
    for (Conn* conn : conns) {
        Log2({Value(original, 0x00456540, &Conn::GetHighestResendCount, conn),
              Value(original, 0x004565E0, &Conn::GetNumQueuedStreamPieces, conn)});
    }
    Call(original, 0x004566D0, &Conn::FlagMessagesForResending, conns[0], 100u);
    Call(original, 0x004566D0, &Conn::FlagMessagesForResending, conns[2], 300u);
    Call(original, 0x00456630, &Conn::ClearResendCounts, conns[1]);
    Log2({Value(original, 0x00456540, &Conn::GetHighestResendCount, conns[1])});

    // Taking them apart.
    Call(original, 0x00456210, &Conn::DestroyImportantMessages, conns[1]);
    Call(original, 0x00455F30, &Conn::DestroyLinks, conns[1]); // (normal messages and received ones left)
    Call(original, 0x00456770, &Conn::DestroyMessageQueues, conns[2]);
    Call(original, 0x00455E50, &Conn::DestroyReceivedSequencedMessages, conns[3]);
    Call(original, 0x00456370, &Conn::ReleaseQueuedMessages, conns[3]);
    Call(original, 0x00455F30, &Conn::DestroyLinks, conns[3]);
    Log2({Value(original, 0x00456540, &Conn::GetHighestResendCount, conns[3]),
          Value(original, 0x004565E0, &Conn::GetNumQueuedStreamPieces, conns[1])});
    // The destructor: conn 0 in an app's list (taken out), conn 1 with its queues half emptied.
    auto* appList = reinterpret_cast<ListNode*>(misc);
    Net::InitConnListHead(appList, Net::kConnHeadVtable);
    AddLast(appList, &conns[0]->m_appNode);
    AddLast(appList, &conns[3]->m_appNode);
    Call(original, 0x00456790, &Conn::Destroy, conns[0]);
    Call(original, 0x00456790, &Conn::Destroy, conns[1]);

    // The list types.
    using SearchBodies = std::pair<uint32_t, uint32_t>;
    for (auto [body, deleting] : {SearchBodies{0x004559D0, 0x00455A80}, SearchBodies{0x004559E0, 0x00455AB0},
                                  SearchBodies{0x004559F0, 0x00455AE0}, SearchBodies{0x00455A00, 0x00455B10}}) {
        auto* search = reinterpret_cast<Net::MsgSeqLinkSearch*>(misc + 0x100);
        search->m_vtable = nullptr;
        // (ours by the type each address is; the same code shape for all four)
        void* oursBody = nullptr;
        void* oursDeleting = nullptr;
        switch (body) {
        case 0x004559D0: oursBody = reinterpret_cast<void*>(TestStandIns::AddressOf(&Net::MsgSeqLinkSearch::DestroyBody));
            oursDeleting = reinterpret_cast<void*>(TestStandIns::AddressOf(&Net::MsgSeqLinkSearch::Destroy)); break;
        case 0x004559E0: oursBody = reinterpret_cast<void*>(TestStandIns::AddressOf(&Net::MsgLinkSearch::DestroyBody));
            oursDeleting = reinterpret_cast<void*>(TestStandIns::AddressOf(&Net::MsgLinkSearch::Destroy)); break;
        case 0x004559F0: oursBody = reinterpret_cast<void*>(TestStandIns::AddressOf(&Net::MsgImpLinkSearch::DestroyBody));
            oursDeleting = reinterpret_cast<void*>(TestStandIns::AddressOf(&Net::MsgImpLinkSearch::Destroy)); break;
        default: oursBody = reinterpret_cast<void*>(TestStandIns::AddressOf(&Net::StreamLinkSearch::DestroyBody));
            oursDeleting = reinterpret_cast<void*>(TestStandIns::AddressOf(&Net::StreamLinkSearch::Destroy)); break;
        }
        auto bodyFn = reinterpret_cast<void(__thiscall*)(void*)>(original ? OriginalFunction(body) : oursBody);
        auto deletingFn = reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(
            original ? OriginalFunction(deleting) : oursDeleting);
        bodyFn(search);
        Log2({reinterpret_cast<uint32_t>(search->m_vtable)});
        for (unsigned int flags : {0u, 1u, 2u, 3u}) {
            auto* block = static_cast<uint32_t*>(Mem::New(8));
            block[0] = 0;
            block[1] = 0xBEEF;
            Log2({reinterpret_cast<uint32_t>(block), reinterpret_cast<uint32_t>(deletingFn(block, flags)), block[0]});
        }
    }
    // node destructor bodies: the middle of three
    using NodeCase = std::pair<uint32_t, void (*)(ListNode*)>;
    for (auto [address, ours] :
         {NodeCase{0x00455B50, [](ListNode* n) { static_cast<Net::MsgNormalNode*>(n)->DestroyBody(); }},
          NodeCase{0x00455B70, [](ListNode* n) { static_cast<Net::MsgImpNode*>(n)->DestroyBody(); }},
          NodeCase{0x00455B90, [](ListNode* n) { static_cast<Net::MsgSeqNode*>(n)->DestroyBody(); }},
          NodeCase{0x00455BB0, [](ListNode* n) { static_cast<Net::StreamLinkNode*>(n)->DestroyBody(); }}}) {
        auto* nodes = reinterpret_cast<ListNode*>(misc + 0x200 + (address & 0xF0) * 4);
        Net::InitConnListHead(&nodes[0], nullptr);
        for (int i = 1; i < 4; ++i) {
            nodes[i].data = &nodes[i];
            AddLast(&nodes[0], &nodes[i]);
        }
        if (original)
            Original<void(__thiscall*)(ListNode*)>(address)(&nodes[2]);
        else
            ours(&nodes[2]);
    }
    for (unsigned int flags : {0u, 1u}) {
        auto* node = static_cast<Net::ConnNode*>(Mem::New(0x14));
        Net::InitConnListHead(node, nullptr);
        auto* head = reinterpret_cast<ListNode*>(misc + 0x800 + flags * 0x20);
        Net::InitConnListHead(head, nullptr);
        AddLast(head, node);
        Log2({reinterpret_cast<uint32_t>(node), Value(original, 0x00455BD0, &Net::ConnNode::Destroy, node, flags)});
    }
    Log2({Value(original, 0x00455C60, &Net::MsgSeqLinkHead::Construct, reinterpret_cast<Net::MsgSeqLinkHead*>(misc + 0x900)),
          Value(original, 0x00457010, &Net::MsgHandlerNode::ConstructHead, reinterpret_cast<Net::MsgHandlerNode*>(misc + 0x920))});

    // The process task.
    uint8_t* app = misc + 0xA00;
    At<void**>(app, 0) = appVtable;
    At<uint32_t>(app, 0x1410) = 41;
    uint8_t* task = misc + 0x2A00;
    At<void*>(task, 0x1C) = app;
    for (int i = 0; i < 2; ++i) {
        if (original)
            Original<void(__cdecl*)(void*)>(0x00456CA0)(task);
        else
            Net::App::ProcessNetworkData(task);
    }

    std::vector<uint32_t> image = Image(arena);
    GameVariable<void*>(0x007D1800) = nullptr;
    TestStandIns::RemoveStandIns();
    return image;
}

void TestNetConn()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestNetConn skipped (the memory manager is already set up)");
        return;
    }
    SaveGlobals();
    std::vector<uint32_t> expected = Run(true);
    std::vector<uint32_t> actual = Run(false);
    CleanUp();
    TestStandIns::Compare(expected, actual, "net conn: construct, queues, destroy, list types, process task");
}

} // namespace

SELFTEST(Early, TestNetConn, CleanUp);

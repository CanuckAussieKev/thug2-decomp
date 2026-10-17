// Self-tests for the net library's 0x00456910-0x00457EB0 (game/net/net_app_view, net_latency, net_lists, and
// Lst::Head::AddNodeFromTail): the original against ours on identical stand-in apps, connections and lists, with
// everything they call outside (Winsock, the debugger output, the game's sprintf, the memory manager, the
// dispatcher, Conn's and the queued messages' destructors, the links' and tasks' virtual destructors) stood in
// for by recording code (standins.h). No real socket is touched.

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/lst/head.h"
#include "game/net/net_app_messages.h"
#include "game/net/net_app_view.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_latency.h"
#include "game/net/net_lists.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Net::App;
using Net::Conn;
using Mem::ListNode;

// Our function registered at a game address (from the replacements' own table).
void* OursAt(uint32_t address)
{
    static std::map<uint32_t, void*> ours;
    if (ours.empty()) {
        for (const ArgumentSizeCheck& c : CheckArgumentSizes()) {
            if (!c.replacement.fallback)
                ours[c.replacement.address] = c.replacement.function;
        }
    }
    auto it = ours.find(address);
    return it == ours.end() ? nullptr : it->second;
}
void* Code(bool original, uint32_t address) { return original ? OriginalFunction(address) : OursAt(address); }
template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

// Every function of this range: in the original's run, calls to them (from inside originals too) run originals.
const std::vector<uint32_t> kRange = {
    0x00456910, 0x00456970, 0x004569B0, 0x00456A50, 0x00456A60, 0x00456A70, 0x00456A80, 0x00456A90, 0x00456AF0,
    0x00456B00, 0x00456B30, 0x00456B40, 0x00456B60, 0x00456B70, 0x00456B80, 0x00456B90, 0x00456BA0, 0x00456CC0,
    0x00456D90, 0x00456E20, 0x00456F10, 0x00456F90, 0x00456FA0, 0x00456FB0, 0x00456FE0, 0x00457030, 0x00457050,
    0x00457070, 0x00457090, 0x004570D0, 0x00457110, 0x00457260, 0x00457280, 0x00457300, 0x00457350, 0x004573A0,
    0x004573D0, 0x00457400, 0x00457460, 0x00457490, 0x00457560, 0x00457710, 0x004578B0, 0x00457910, 0x00457970,
    0x00457AC0, 0x00457C80, 0x00401AA0, 0x005BAFC0, 0x005BABA0, 0x00401CC0};
struct Originals {
    std::vector<std::unique_ptr<OriginalCode>> guards;
    explicit Originals(bool original)
    {
        if (original) {
            for (uint32_t address : kRange)
                guards.push_back(std::make_unique<OriginalCode>(address));
        }
    }
};

// --- Stand-in objects ---------------------------------------------------------------------------------------------

alignas(16) uint8_t g_app[0x2000];
alignas(16) uint8_t g_conns[4][0x3D20];
alignas(16) uint8_t g_misc[0x4000]; // heads, links, messages, tasks
alignas(16) uint8_t g_heap[0x1000]; // what the stand-in allocator hands out
alignas(16) uint8_t g_fakeManager[0x600];

App* TheApp() { return reinterpret_cast<App*>(g_app); }
Conn* TheConn(int i) { return reinterpret_cast<Conn*>(g_conns[i]); }
template <typename T = uint8_t>
T* Misc(uint32_t offset)
{
    return reinterpret_cast<T*>(g_misc + offset);
}

// Addresses as offsets into the stand-in objects (identical in both runs); stack addresses (which differ) as one
// marker.
uint32_t Rel(uint32_t p)
{
    auto in = [p](const void* base, size_t size) {
        auto b = reinterpret_cast<uint32_t>(base);
        return p >= b && p < b + size ? p - b : 0xFFFFFFFF;
    };
    if (uint32_t o = in(g_app, sizeof(g_app)); o != 0xFFFFFFFF)
        return 0xA0000000 + o;
    if (uint32_t o = in(g_conns, sizeof(g_conns)); o != 0xFFFFFFFF)
        return 0xC0000000 + o;
    if (uint32_t o = in(g_misc, sizeof(g_misc)); o != 0xFFFFFFFF)
        return 0xD0000000 + o;
    if (uint32_t o = in(g_heap, sizeof(g_heap)); o != 0xFFFFFFFF)
        return 0xE0000000 + o;
    uint8_t probe = 0;
    auto sp = reinterpret_cast<uint32_t>(&probe);
    if (p > sp - 0x100000 && p < sp + 0x100000)
        return 0x57AC0000;
    return p;
}
uint32_t Rel(const void* p) { return Rel(reinterpret_cast<uint32_t>(p)); }

void InitHead(ListNode* head, uint32_t vtable)
{
    *head = {reinterpret_cast<void**>(vtable), reinterpret_cast<void*>(-1), 0, head, head};
}
void Append(ListNode* head, ListNode* node, uint32_t vtable, void* data, int priority = 0)
{
    node->vtable = reinterpret_cast<void**>(vtable);
    node->data = data;
    node->priority = priority;
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
void RemoveNode(ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}
// A list from its head, both ways round (at most 40 nodes).
void RecordList(ListNode* head)
{
    Record({0x1157, Rel(head)});
    int n = 0;
    for (ListNode* node = head->next; node != head && n < 40; node = node->next, ++n)
        Record({Rel(node), Rel(node->data), static_cast<uint32_t>(node->priority), Rel(node->vtable)});
    Record({0x1158});
    n = 0;
    for (ListNode* node = head->prev; node != head && n < 40; node = node->prev, ++n)
        Record({Rel(node)});
}
void RecordWords(const void* p, size_t bytes)
{
    auto* words = static_cast<const uint32_t*>(p);
    for (size_t i = 0; i < bytes / 4; ++i)
        Record({Rel(words[i])});
}

// Recording stand-ins every test may use.
uint32_t g_heapUsed;
bool g_allocFail;
void StandInMemory(StandIns& s)
{
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA110, r.ecx == reinterpret_cast<uint32_t>(g_fakeManager), r.args[0], r.args[1], r.args[2]});
        if (g_allocFail)
            return 0u;
        uint8_t* block = g_heap + g_heapUsed;
        g_heapUsed += (r.args[0] + 15) & ~15u;
        return reinterpret_cast<uint32_t>(block);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE, r.ecx == reinterpret_cast<uint32_t>(g_fakeManager), Rel(r.args[0])});
        return 0u;
    });
}

// The globals the memory code reads: the manager pointer (a fake one) and the "manager ready" flag.
std::vector<Saved> g_saved;
bool g_savedPending;
void SaveGlobals()
{
    g_saved = {{0x007D1800, 4, {}}, {0x007CC979, 1, {}}};
    SaveAll(g_saved);
    g_savedPending = true;
    GameVariable<uint32_t>(0x007D1800) = reinterpret_cast<uint32_t>(g_fakeManager);
    GameVariable<bool>(0x007CC979) = true;
}
void RestoreGlobals()
{
    if (g_savedPending)
        RestoreAll(g_saved);
    g_savedPending = false;
}
void CleanUp()
{
    RemoveStandIns();
    RestoreGlobals();
}

void ResetObjects()
{
    g_trace.clear();
    std::memset(g_app, 0xCD, sizeof(g_app));
    std::memset(g_conns, 0xCD, sizeof(g_conns));
    std::memset(g_misc, 0xCD, sizeof(g_misc));
    std::memset(g_heap, 0xAB, sizeof(g_heap));
    g_heapUsed = 0;
    g_allocFail = false;
    std::memcpy(TheApp()->m_name, "server", 7);
}

// --- LatencyTest --------------------------------------------------------------------------------------------------

std::vector<uint32_t> RunLatency(bool original)
{
    g_trace.clear();
    Originals guards(original);
    auto construct = Fn<void*(__thiscall*)(void*)>(original, 0x00456970);
    auto input = Fn<void(__thiscall*)(void*, int)>(original, 0x004569B0);
    alignas(4) uint8_t buffer[0x40];
    std::memset(buffer, 0xCD, sizeof(buffer));
    Record({construct(buffer) == buffer});
    RecordWords(buffer, sizeof(buffer));
    for (int value : {50, 0, -30, 100, 0x7FFFFFFF, 0x7FFFFFFF, 7, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1000, 3, 1, 2,
                      3, 4, 5, 6, 7, 8, 9, 10, 11}) {
        input(buffer, value);
        RecordWords(buffer, sizeof(buffer));
    }
    auto* test = reinterpret_cast<Net::LatencyTest*>(buffer);
    // A negative total (an unsigned division would differ), starting at the last slot.
    for (int i = 0; i < 10; ++i)
        test->m_times[i] = i % 3 ? -7 - i : 0;
    test->m_next = 9;
    test->m_averageLatency = 123;
    input(buffer, -5);
    RecordWords(buffer, sizeof(buffer));
    // Nothing measured: the average stays.
    for (int& time : test->m_times)
        time = 0;
    test->m_next = 3;
    test->m_averageLatency = 77;
    input(buffer, 0);
    RecordWords(buffer, sizeof(buffer));
    return g_trace;
}

void TestNetLatency()
{
    std::vector<uint32_t> expected = RunLatency(true);
    std::vector<uint32_t> actual = RunLatency(false);
    Compare(expected, actual, "Net::LatencyTest: constructor, InputLatencyValue (wrapping, negative, all-zero)");
}

// --- Lists, lookups, the checksum, the send checks -------------------------------------------------------------

using ThisCall0 = uint32_t(__thiscall*)(void*);
using ThisCall1 = uint32_t(__thiscall*)(void*, uint32_t);
using ThisCall2 = uint32_t(__thiscall*)(void*, uint32_t, uint32_t);
using ThisCall3 = uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t);

// A connection list of the four stand-in connections.
void SetUpConnections(int count)
{
    InitHead(&TheApp()->m_connections, 0x006475AC);
    const uint32_t ips[] = {0x0A000001, 0x0A000001, 0x0B000002, 0x0B000002};
    const uint16_t ports[] = {1, 2, 1, 2};
    const int handles[] = {10, 11, 12, 10};
    for (int i = 0; i < count; ++i) {
        Conn* conn = TheConn(i);
        conn->m_ip = ips[i];
        conn->m_port = ports[i];
        conn->m_handle = handles[i];
        Append(&TheApp()->m_connections, &conn->m_appNode, 0x00647574, conn);
    }
}

// Message links (0x2C bytes) in g_misc from 0x2000, vtable `vtable`.
constexpr uint32_t kLinkSize = 0x30;
Net::MsgLink* Link(int i) { return Misc<Net::MsgLink>(0x2000 + i * kLinkSize); }
uint32_t g_linkVtable[4];

void SetUpQueues(Conn* conn)
{
    InitHead(&conn->m_normalMsgs, 0x00647578);
    InitHead(&conn->m_importantMsgs, 0x0064757C);
    InitHead(&conn->m_sequencedMsgs, 0x00647580);
}
Net::MsgLink* AddLink(ListNode* queue, int i)
{
    Net::MsgLink* link = Link(i);
    std::memset(link, 0, kLinkSize);
    Append(queue, link, reinterpret_cast<uint32_t>(g_linkVtable), link);
    return link;
}

uint32_t CallSlot5(void* code, void* object, uint32_t a, uint32_t b)
{
    uint32_t result;
    __asm {
        push b
        push a
        mov ecx, object
        call code
        mov result, eax
    }
    return result;
}

std::vector<uint32_t> RunLists(bool original)
{
    ResetObjects();
    Originals guards(original);
    App* app = TheApp();

    // The getters and flags.
    app->m_processTask = reinterpret_cast<void*>(0x1444);
    app->m_sendTask = reinterpret_cast<void*>(0x1448);
    app->m_receiveTask = reinterpret_cast<void*>(0x144C);
    app->m_metricsTask = reinterpret_cast<void*>(0x1450);
    app->m_manager = reinterpret_cast<void*>(0x1440);
    app->m_socket = 0x1428;
    for (uint32_t address : {0x00456A50u, 0x00456A60u, 0x00456A70u, 0x00456A80u, 0x00456AF0u, 0x00456B60u})
        Record({address, Fn<ThisCall0>(original, address)(app)});
    for (uint32_t flags : {0u, 8u, 0x10u, 0x18u, 0xFFFFFFF7u, 0xFFFFFFEFu, 0xFFFFFFFFu}) {
        app->m_flags = flags;
        Record({flags, Fn<ThisCall0>(original, 0x00456F90)(app) & 0xFF, Fn<ThisCall0>(original, 0x00456FA0)(app) & 0xFF});
    }
    Fn<ThisCall1>(original, 0x00456B70)(app, 0x12345678);
    RecordWords(reinterpret_cast<uint8_t*>(app) + 0x1E80, 0x10);

    // The folded jump through vtable slot 5 (arguments left for the callee).
    {
        uint32_t vtable[8];
        auto* thunk = StandIns::s_current->Thunk(8, [](Regs& r) {
            Record({0x5105, Rel(r.ecx), r.args[0], r.args[1]});
            return 0x5A5A5A5Au;
        });
        for (uint32_t& slot : vtable)
            slot = 0;
        vtable[5] = reinterpret_cast<uint32_t>(thunk);
        uint32_t* object = Misc<uint32_t>(0x3F00);
        object[0] = reinterpret_cast<uint32_t>(vtable);
        void* code = original ? OriginalFunction(0x00456B30) : reinterpret_cast<void*>(&Net::VirtualSlot5Jump);
        Record({CallSlot5(code, object, 0x11, 0x22)});
    }

    // Aliases.
    Fn<ThisCall2>(original, 0x00456B40)(app, reinterpret_cast<uint32_t>(TheConn(0)), reinterpret_cast<uint32_t>(TheConn(1)));
    Record({Rel(TheConn(0)->m_aliasConn), Rel(TheConn(1)->m_aliasConn)});
    Fn<ThisCall2>(original, 0x00456B40)(app, reinterpret_cast<uint32_t>(TheConn(2)), reinterpret_cast<uint32_t>(TheConn(2)));
    Record({Rel(TheConn(2)->m_aliasConn)});

    // Lookups and the connection search.
    for (int count : {0, 4}) {
        SetUpConnections(count);
        for (uint32_t ip : {0x0A000001u, 0x0B000002u, 0x0C000003u}) {
            for (uint32_t port : {1u, 2u, 3u, 0xABCD0002u})
                Record({Rel(Fn<ThisCall2>(original, 0x004578B0)(app, ip, port))});
        }
        for (uint32_t handle : {10u, 11u, 12u, 13u, 0xFFFFFFFFu})
            Record({Rel(Fn<ThisCall1>(original, 0x00457910)(app, handle))});
        Net::ConnSearch search = {Net::kConnSearchVtable, &app->m_connections};
        for (int i = 0; i <= count; ++i) {
            Record({Rel(Fn<ThisCall1>(original, 0x004573D0)(app, reinterpret_cast<uint32_t>(&search))),
                    Rel(search.m_node)});
        }
    }

    // AddNodeFromTail.
    {
        ListNode* head = Misc<ListNode>(0);
        InitHead(head, 0x006475A8);
        const int priorities[] = {5, 3, 5, 9, 1, 3, 9, 0, -2, 5};
        for (int i = 0; i < 10; ++i) {
            ListNode* node = Misc<ListNode>(0x20 + i * 0x20);
            node->vtable = reinterpret_cast<void**>(0x006475A8);
            node->data = node;
            node->priority = priorities[i];
            Fn<ThisCall1>(original, 0x00457350)(head, reinterpret_cast<uint32_t>(node));
            RecordList(head);
        }
    }

    // The destructor bodies: out of a three-node list.
    for (uint32_t address : {0x00457030u, 0x00457050u, 0x00457070u}) {
        ListNode* head = Misc<ListNode>(0x400);
        InitHead(head, 0x1111);
        for (int i = 0; i < 3; ++i)
            Append(head, Misc<ListNode>(0x420 + i * 0x20), 0x2222, Misc(0x420 + i * 0x20));
        Fn<ThisCall0>(original, address)(Misc(0x440));
        RecordList(head);
        RecordWords(Misc(0x440), 0x14);
    }
    // The scalar deleting destructors.
    for (uint32_t address : {0x00457090u, 0x004570D0u, 0x004573A0u, 0x00457460u, 0x00456FB0u, 0x00456FE0u}) {
        for (uint32_t flags : {0u, 1u, 3u, 0xFFFFFFFEu}) {
            ListNode* head = Misc<ListNode>(0x400);
            InitHead(head, 0x1111);
            for (int i = 0; i < 3; ++i)
                Append(head, Misc<ListNode>(0x420 + i * 0x20), 0x2222, Misc(0x420 + i * 0x20));
            Record({address, flags, Rel(Fn<ThisCall1>(original, address)(Misc(0x440), flags))});
            RecordList(head);
            RecordWords(Misc(0x440), 0x18);
        }
    }
    for (uint32_t address : {0x00456B80u, 0x00456B90u}) {
        uint32_t search[2] = {0x3333, 0x4444};
        Fn<ThisCall0>(original, address)(search);
        Record({search[0], search[1]});
    }

    // The dispatcher's handler lists destroyed (two of them hold a node).
    {
        auto* lists = Misc<Net::HandlerLists>(0x800);
        for (Net::MsgHandlerNode& list : lists->m_lists)
            InitHead(&list, 0x006475A8);
        Append(&lists->m_lists[0], Misc<ListNode>(0x1C00), 0x2222, Misc(0x1C00));
        Append(&lists->m_lists[254], Misc<ListNode>(0x1C20), 0x2222, Misc(0x1C20));
        Fn<ThisCall0>(original, 0x00457260)(lists);
        RecordWords(lists, sizeof(Net::HandlerLists));
        RecordWords(Misc(0x1C00), 0x40);
    }

    // Encrypt / Decrypt.
    uint8_t data[400];
    for (int i = 0; i < 400; ++i)
        data[i] = static_cast<uint8_t>(i * 37 + 11);
    for (int length : {0, 1, 2, 7, 64, 300}) {
        uint8_t* out = Misc(0x3000);
        std::memset(out, 0xEE, 0x140);
        int outLength = 0x5A5A;
        Fn<void(__thiscall*)(void*, const uint8_t*, uint8_t*, int, int*)>(original, 0x00456910)(app, data, out, length,
                                                                                                 &outLength);
        Record({static_cast<uint32_t>(outLength), HashBytes(out, 0x140)});
    }
    auto decrypt = Fn<uint32_t(__thiscall*)(void*, const uint8_t*, uint8_t*, int)>(original, 0x00456CC0);
    struct Packet {
        int length;
        bool valid;
        int corrupt; // byte index to flip (-1 none)
    };
    for (uint32_t flags : {0u, 0x20u, 0xFFFFFFDFu, 0xFFFFFFFFu}) {
        app->m_flags = flags;
        for (Packet p : {Packet{2, true, -1}, Packet{3, true, -1}, Packet{10, true, -1}, Packet{66, true, -1},
                         Packet{66, true, 0}, Packet{66, true, 1}, Packet{66, true, 30}, Packet{0, true, -1},
                         Packet{-5, true, -1}, Packet{4, false, -1}}) {
            uint8_t packet[0x60];
            int body = p.length > 2 ? p.length - 2 : 0;
            uint32_t crc = Crc::GenerateCRCFromBuffer(data, body);
            auto checksum = static_cast<uint16_t>(crc + (crc >> 16));
            if (!p.valid)
                checksum ^= 0x0100;
            std::memcpy(packet, &checksum, 2);
            for (int i = 0; i < body; ++i)
                packet[2 + i] = data[i] ^ static_cast<uint8_t>(checksum);
            if (p.corrupt >= 0)
                packet[p.corrupt] ^= 0x40;
            uint8_t* out = Misc(0x3000);
            std::memset(out, 0xEE, 0x60);
            Record({flags, static_cast<uint32_t>(p.length), decrypt(app, packet, out, p.length) & 0xFF,
                    HashBytes(out, 0x60)});
        }
    }

    // IsSendDue.
    struct SendCase {
        uint32_t time, last, interval;
        bool now;
    };
    Conn* conn = TheConn(0);
    for (SendCase c : {SendCase{1000, 900, 100, false}, SendCase{1000, 901, 100, false}, SendCase{1000, 901, 100, true},
                       SendCase{5, 0xFFFFFFF0u, 0x16, false}, SendCase{5, 0xFFFFFFF0u, 0x15, false},
                       SendCase{5, 0xFFFFFFF0u, 0x16, true}, SendCase{0, 0, 0, false}}) {
        app->m_time = c.time;
        conn->m_lastSendTime = c.last;
        conn->m_sendInterval = c.interval;
        conn->m_sendNow = c.now;
        Record({Fn<ThisCall1>(original, 0x00456B00)(app, reinterpret_cast<uint32_t>(conn)) & 0xFF});
    }

    // ImportantMessagesDue / MessagesDue. The resend threshold: twice the average latency (800 here), or 1000 / 500
    // by the connection's flags.
    struct Due {
        int queue;     // 0 normal, 1 important, 2 sequenced (-1 none)
        uint32_t sent; // normal: its send time
        uint32_t t24;
    };
    struct DueCase {
        uint32_t time;
        uint32_t flags60, flags64;
        std::vector<Due> links;
    };
    const std::vector<DueCase> dueCases = {
        {1000, 0, 0, {}},
        {1000, 0, 0, {{0, 1001, 0}}},
        {1000, 0, 0, {{0, 1000, 0}}},
        {1000, 0, 0, {{0, 2000, 0}, {0, 999, 0}}},
        {1000, 0, 0, {{1, 0, 0}}},
        {1000, 0, 0, {{1, 200, 0}}},
        {1000, 0, 0, {{1, 199, 0}}},
        {1000, 0, 0, {{1, 200, 1001}}},
        {1000, 0, 0, {{1, 200, 1000}}},
        {1000, 0, 0, {{2, 0, 0}}},
        {1000, 0, 0, {{2, 200, 0}}},
        {1000, 0, 0, {{2, 199, 0}}},
        {1000, 0, 0, {{2, 200, 1001}}},
        {1000, 0, 0, {{2, 200, 1000}}},
        {1000, 0, 0, {{1, 500, 0}, {1, 100, 0}}},
        {1000, 0, 0, {{2, 500, 0}, {2, 100, 0}}},
        {1000, 0, 0, {{0, 5000, 0}, {1, 900, 0}, {2, 900, 0}, {2, 150, 0}}},
        {1000, 0, 0, {{1, 900, 0}, {2, 900, 2000}}},
        {10, 0, 0, {{1, 0xFFFFFF00u, 0}}},
        {10, 0, 0, {{2, 0xFFFFFFF0u, 0}}},
        {1000, 2, 0, {{1, 1, 0}}},
        {1000, 2, 0, {{1, 0, 0}, {2, 1, 0}}},
        {1000, 2, 0, {{2, 0xFFFFFFFFu, 0}}},
        {1000, 0, 1, {{1, 500, 0}}},
        {1000, 0, 1, {{1, 499, 0}}},
        {1000, 0, 1, {{2, 500, 0}, {2, 499, 0}}},
    };
    conn->m_latencyTest.m_averageLatency = 400;
    for (const DueCase& c : dueCases) {
        app->m_time = c.time;
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(conn) + 0x60) = c.flags60;
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(conn) + 0x64) = c.flags64;
        SetUpQueues(conn);
        int i = 0;
        for (const Due& due : c.links) {
            ListNode* queue = due.queue == 0 ? &conn->m_normalMsgs
                            : due.queue == 1 ? &conn->m_importantMsgs
                                             : &conn->m_sequencedMsgs;
            Net::MsgLink* link = AddLink(queue, i++);
            link->At<uint32_t>(due.queue == 0 ? 0x18 : due.queue == 1 ? 0x14 : 0x1C) = due.sent;
            link->At<uint32_t>(0x24) = due.t24;
        }
        Record({Fn<ThisCall1>(original, 0x00457970)(app, reinterpret_cast<uint32_t>(conn)) & 0xFF,
                Fn<ThisCall1>(original, 0x00457AC0)(app, reinterpret_cast<uint32_t>(conn)) & 0xFF});
    }
    return g_trace;
}

void TestNetLists()
{
    std::vector<uint32_t> traces[2];
    SaveGlobals();
    {
        auto* s = new StandIns();
        StandInMemory(*s);
        traces[0] = RunLists(true);
        traces[1] = RunLists(false);
        delete s;
    }
    RestoreGlobals();
    Compare(traces[0], traces[1],
            "Net::App lists: getters, flags, slot-5 jump, aliases, connection lookups and search, AddNodeFromTail, "
            "node/search/handler-list destructors, Encrypt/Decrypt, IsSendDue, ImportantMessagesDue, MessagesDue");
}

// --- Sockets ------------------------------------------------------------------------------------------------------

struct SocketScript {
    std::vector<uint32_t> errors; // WSAGetLastError, in turn (then 0)
    std::vector<int> binds;       // bind results, in turn (then 0)
    int socketResult = 0x77;
    int ioctlResult = 0;
    int setsockoptResult = 0;
    int getsocknameResult = 0;
    int sendtoResult = 10;
    int sendResult = 10;
    uint32_t managerValue = 0;
};
SocketScript g_script;

template <typename T>
T Next(std::vector<T>& values, T otherwise)
{
    if (values.empty())
        return otherwise;
    T value = values.front();
    values.erase(values.begin());
    return value;
}

void RecordAddress(uint32_t address)
{
    if (!address)
        return Record({0});
    RecordWords(reinterpret_cast<const void*>(address), 16);
}

void StandInSockets(StandIns& s)
{
    StandInApi(s, 0x00645274, reinterpret_cast<const void*>(&WSAGetLastError), 0, [](Regs&) {
        Record({0xE440});
        return Next(g_script.errors, 0u);
    });
    StandInApi(s, 0x00645280, reinterpret_cast<const void*>(&htons), 4, [](Regs& r) {
        Record({0x4705, r.args[0] & 0xFFFF});
        uint32_t v = r.args[0] & 0xFFFF;
        return ((v & 0xFF) << 8) | (v >> 8);
    });
    StandInApi(s, 0x0064527C, reinterpret_cast<const void*>(&bind), 12, [](Regs& r) {
        Record({0xB1D, r.args[0], Rel(r.args[1]), r.args[2]});
        RecordAddress(r.args[1]);
        return static_cast<uint32_t>(Next(g_script.binds, 0));
    });
    StandInApi(s, 0x00645278, reinterpret_cast<const void*>(&getsockname), 12, [](Regs& r) {
        Record({0x6E75, r.args[0], Rel(r.args[1]), *reinterpret_cast<uint32_t*>(r.args[2])});
        const uint8_t got[16] = {2, 0, 0x12, 0x34, 192, 168, 1, 9, 1, 2, 3, 4, 5, 6, 7, 8};
        std::memcpy(reinterpret_cast<void*>(r.args[1]), got, 16);
        return static_cast<uint32_t>(g_script.getsocknameResult);
    });
    StandInApi(s, 0x00645284, reinterpret_cast<const void*>(&sendto), 24, [](Regs& r) {
        Record({0x5E2D, r.args[0], Rel(r.args[1]), HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), r.args[2]),
                r.args[2], r.args[3], r.args[5]});
        RecordAddress(r.args[4]);
        return static_cast<uint32_t>(g_script.sendtoResult);
    });
    StandInApi(s, 0x00645288, reinterpret_cast<const void*>(&send), 16, [](Regs& r) {
        Record({0x5E2E, r.args[0], Rel(r.args[1]), HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), r.args[2]),
                r.args[2], r.args[3]});
        return static_cast<uint32_t>(g_script.sendResult);
    });
    StandInApi(s, 0x00645294, reinterpret_cast<const void*>(&socket), 12, [](Regs& r) {
        Record({0x50C, r.args[0], r.args[1], r.args[2]});
        return static_cast<uint32_t>(g_script.socketResult);
    });
    StandInApi(s, 0x00645290, reinterpret_cast<const void*>(&ioctlsocket), 12, [](Regs& r) {
        Record({0x10C7, r.args[0], r.args[1], *reinterpret_cast<uint32_t*>(r.args[2])});
        return static_cast<uint32_t>(g_script.ioctlResult);
    });
    StandInApi(s, 0x0064528C, reinterpret_cast<const void*>(&setsockopt), 20, [](Regs& r) {
        Record({0x5E7, r.args[0], r.args[1], r.args[2], *reinterpret_cast<uint32_t*>(r.args[3]), r.args[4]});
        return static_cast<uint32_t>(g_script.setsockoptResult);
    });
    StandInApi(s, 0x00645298, reinterpret_cast<const void*>(&closesocket), 4, [](Regs& r) {
        Record({0xC105, r.args[0]});
        return 0u;
    });
    StandInApi(s, 0x006450A4, reinterpret_cast<const void*>(&OutputDebugStringA), 4, [](Regs& r) {
        Record({0x0D85, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf); // the game's sprintf (not started offline)
    // -> the dispatcher (wt/netC)
    s.At({0x0045A640, AddressOf(&Net::Dispatcher::Init)}, 0, [](Regs& r) {
        Record({0xA640, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x0045A690, AddressOf(&Net::Dispatcher::Deinit)}, 0, [](Regs& r) {
        Record({0xA690, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x0045A5C0, AddressOf(&Net::Dispatcher::AddHandler)}, 0x14, [](Regs& r) {
        // (ours registers the handlers' C++ versions: named by the originals' addresses)
        uint32_t handler = r.args[1];
        std::pair<Net::MsgHandlerCode, uint32_t> handlers[] = {
            {Net::App::HandleAck, 0x0045B0B0}, {Net::App::HandleSequencedMessages, 0x0045AF10},
            {Net::App::HandleCompressedMessages, 0x0045ABF0}, {Net::App::HandleStreamMessages, 0x0045B2F0},
            {Net::App::HandleLatencyTest, 0x0045AD60}, {Net::App::HandleLatencyResponse, 0x0045AB60}};
        for (auto [code, original] : handlers) {
            if (handler == reinterpret_cast<uint32_t>(code))
                handler = original;
        }
        Record({0xA5C0, Rel(r.ecx), r.args[0] & 0xFF, handler, r.args[2], r.args[3], r.args[4]});
        return 0u;
    });
    // -> Conn's destructor (takes it out of the app's list, as the real one does)
    s.At({0x00456790, AddressOf(&Conn::Destroy)}, 0, [](Regs& r) {
        auto* node = &reinterpret_cast<Conn*>(r.ecx)->m_appNode;
        Record({0x6790, Rel(r.ecx), Rel(node->next), Rel(node->prev)}); // (still in the list?)
        RemoveNode(&reinterpret_cast<Conn*>(r.ecx)->m_appNode);
        return 0u;
    });
}

std::vector<uint32_t> RunSockets(bool original)
{
    ResetObjects();
    Originals guards(original);
    App* app = TheApp();
    uint32_t* manager = Misc<uint32_t>(0x3800);
    app->m_manager = manager;
    app->m_socket = 0x1234;

    // ReportError.
    g_script = {};
    g_script.errors = {10054};
    Fn<ThisCall0>(original, 0x00456A90)(app);

    // Bind.
    struct BindCase {
        std::vector<int> binds;
        std::vector<uint32_t> errors;
        int getsockname;
        uint32_t managerValue;
    };
    for (const BindCase& c : {BindCase{{0}, {}, 0, 0}, BindCase{{0}, {}, 0, 1}, BindCase{{-1, 0}, {0x2740, 7}, 0, 0},
                              BindCase{{-1, -1}, {0x2740, 7, 8}, 0, 0}, BindCase{{-1}, {0x2741, 9}, 0, 0},
                              BindCase{{5}, {}, -1, 0}, BindCase{{-1, 0}, {0x2740, 7, 11}, -1, 1}}) {
        g_script = {};
        g_script.binds = c.binds;
        g_script.errors = c.errors;
        g_script.getsocknameResult = c.getsockname;
        manager[0x128 / 4] = c.managerValue;
        std::memset(app->m_localAddress, 0xCD, sizeof(app->m_localAddress));
        Record({0xB10, Fn<ThisCall2>(original, 0x00456BA0)(app, 0xC0A80001, 0xBEEF0000u | 2001) & 0xFF});
        RecordWords(app->m_localAddress, 16);
    }

    // SendTo / Send / SendMessageTo.
    uint8_t data[64];
    for (int i = 0; i < 64; ++i)
        data[i] = static_cast<uint8_t>(i * 13 + 5);
    auto sendTo = Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, const uint8_t*, int, int)>(original, 0x00456E20);
    auto sendOn = Fn<uint32_t(__thiscall*)(void*, const uint8_t*, int, int)>(original, 0x00456F10);
    auto sendMessage = Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, const void*, uint32_t, uint32_t, int)>(
        original, 0x00457280);
    for (uint32_t flags : {0u, 0x20u}) {
        app->m_flags = flags;
        for (int result : {10, -1}) {
            for (uint32_t error : {0u, 10035u, 10036u, 10054u}) {
                if (result != -1 && error)
                    continue;
                g_script = {};
                g_script.sendtoResult = result;
                g_script.sendResult = result;
                g_script.errors = {error};
                std::memset(app->m_scrambled, 0xCD, 0x80);
                Record({0x5E10, sendTo(app, 0x0100007F, 0x99990000u | 4321, data, 37, 0x8) & 0xFF});
                Record({HashBytes(app->m_scrambled, 0x80)});
                g_script.errors = {error};
                Record({0x5E11, sendMessage(app, 0xFF00 | 0x2A, 0x77770000 | 20, data, 0x0200007F, 0x5555 | 0x10000, 2) &
                                    0xFF});
                Record({0x5E12, sendMessage(app, 3, 0, data, 0x0300007F, 80, 0) & 0xFF});
                Record({0x5E14, sendMessage(app, 4, 1, data + 9, 0x0400007F, 81, 0) & 0xFF});
                for (bool connected : {false, true}) {
                    app->m_connected = connected;
                    Record({0x5E13, sendOn(app, data, 25, 4) & 0xFF});
                }
            }
        }
    }

    // Init.
    struct InitCase {
        uint32_t flags;
        int socket, ioctl, setsockopt;
    };
    for (InitCase c : {InitCase{0, 0x77, 0, 0}, InitCase{1, 0x77, 0, 0}, InitCase{1, 0x77, 0, -1},
                       InitCase{0, -1, 0, 0}, InitCase{1, 0x77, -1, 0}, InitCase{8, -1, -1, -1},
                       InitCase{9, -1, -1, -1}, InitCase{0xFFFFFFF7u, 0x78, 0, 0}}) {
        g_script = {};
        g_script.socketResult = c.socket;
        g_script.ioctlResult = c.ioctl;
        g_script.setsockoptResult = c.setsockopt;
        g_script.errors = {10093};
        app->m_flags = c.flags;
        app->m_socket = 0x1234;
        Record({0x1417, Fn<ThisCall0>(original, 0x00457110)(app) & 0xFF});
        RecordWords(reinterpret_cast<uint8_t*>(app) + 0x13F8, 0x20);
        RecordWords(reinterpret_cast<uint8_t*>(app) + 0x1428, 4);
        RecordWords(reinterpret_cast<uint8_t*>(app) + 0x1454, 8);
    }

    // Deinit (connections deleted).
    for (uint32_t flags : {0u, 8u}) {
        for (int count : {0, 1, 4}) {
            g_script = {};
            app->m_flags = flags;
            SetUpConnections(count);
            Fn<ThisCall0>(original, 0x00457490)(app);
            RecordList(&app->m_connections);
        }
    }
    return g_trace;
}

void TestNetSockets()
{
    std::vector<uint32_t> traces[2];
    SaveGlobals();
    {
        auto* s = new StandIns();
        StandInMemory(*s);
        StandInSockets(*s);
        traces[0] = RunSockets(true);
        traces[1] = RunSockets(false);
        delete s;
    }
    RestoreGlobals();
    Compare(traces[0], traces[1],
            "Net::App sockets (stood in): ReportError, Bind, SendTo, SendMessageTo, Send, Init, Deinit");
}

// --- Queues, banning, the destructor ------------------------------------------------------------------------------

std::vector<int> g_dispatchResults;

void StandInQueues(StandIns& s)
{
    // -> SeqQueuedMsg's constructor and the queued messages' destructor
    s.At({0x00455040, AddressOf(&Net::SeqQueuedMsg::Construct)}, 0x14, [](Regs& r) {
        Record({0x5040, Rel(r.ecx), r.args[0] & 0xFF, r.args[1] & 0xFFFF, Rel(r.args[2]), r.args[3] & 0xFF,
                r.args[4] & 0xFF});
        return r.ecx;
    });
    s.At({0x004550F0, AddressOf(&Net::QueuedMsg::DestroyBody)}, 0, [](Regs& r) {
        Record({0x50F0, Rel(r.ecx)});
        return 0u;
    });
    // -> Dispatcher::DispatchMessage (wt/netC)
    s.At({0x0045A750, AddressOf(&Net::Dispatcher::DispatchMsg)}, 4, [](Regs& r) {
        auto* context = reinterpret_cast<const uint8_t*>(r.args[0]);
        auto word = [context](uint32_t offset) { return *reinterpret_cast<const uint32_t*>(context + offset); };
        Record({0xA750, Rel(r.ecx), Rel(word(0)), context[4], word(8), Rel(word(0xC)), Rel(word(0x10)), word(0x14),
                HashBytes(context + 0x18, 0xA1 - 0x18)}); // (0xA1-0xA3: padding)
        // (a handler may change the context: the next message must not see it)
        *reinterpret_cast<uint32_t*>(r.args[0] + 0x14) = 0xF1A65000;
        return static_cast<uint32_t>(Next(g_dispatchResults, 1));
    });
    // The links' scalar deleting destructors (they leave their list, as the real ones do), the tasks' (slot 2).
    void* linkDestroy = s.Thunk(4, [](Regs& r) {
        Record({0x11CC, Rel(r.ecx), r.args[0]});
        RemoveNode(reinterpret_cast<ListNode*>(r.ecx));
        return r.ecx;
    });
    void* taskDestroy = s.Thunk(4, [](Regs& r) {
        Record({0x7A5C, Rel(r.ecx), r.args[0]});
        return r.ecx;
    });
    g_linkVtable[0] = reinterpret_cast<uint32_t>(linkDestroy);
    g_linkVtable[1] = 0xBAD1;
    g_linkVtable[2] = reinterpret_cast<uint32_t>(taskDestroy);
    g_linkVtable[3] = 0xBAD3;
}

// Sequenced messages (SeqQueuedMsg, 0x10 bytes) at g_misc + 0x3400.
Net::SeqQueuedMsg* Msg(int i) { return Misc<Net::SeqQueuedMsg>(0x3400 + i * 0x10); }

std::vector<uint32_t> RunQueues(bool original)
{
    ResetObjects();
    Originals guards(original);
    App* app = TheApp();
    std::memcpy(app->m_name, "client", 7);

    // NewQueuedMsg.
    auto newMsg = Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, void*, uint32_t, uint32_t)>(original, 0x00456D90);
    Record({Rel(newMsg(app, 0x1234567A, 0xFFFF0040, Misc(0x10), 0x55AA, 0x101))});
    g_allocFail = true;
    Record({Rel(newMsg(app, 1, 2, Misc(0x10), 3, 4))});
    g_allocFail = false;

    // TerminateConnection, BanConnection.
    SetUpConnections(4);
    InitHead(&app->m_banned, 0x006475B0);
    Fn<ThisCall1>(original, 0x00457300)(app, reinterpret_cast<uint32_t>(TheConn(1)));
    RecordList(&app->m_connections);
    Fn<ThisCall1>(original, 0x00457400)(app, reinterpret_cast<uint32_t>(TheConn(2)));
    Fn<ThisCall1>(original, 0x00457400)(app, reinterpret_cast<uint32_t>(TheConn(0)));
    RecordList(&app->m_banned);
    RecordWords(g_heap, 0x80);

    // DispatchQueuedSequencedMessages. Each case: messages queued by group, and the handlers' answers.
    struct Queued {
        int group;    // the list it's in (and its +0x15 group, unless `otherGroup`)
        uint32_t seq; // its sequence number
        int otherGroup = -1;
        bool noData = false;
    };
    struct SeqCase {
        std::vector<Queued> queued;
        std::vector<int> results;
        uint32_t expected0, expected5; // the next numbers expected in groups 0 and 5
    };
    const std::vector<SeqCase> seqCases = {
        {{}, {}, 0, 0},
        {{{0, 0}}, {1}, 0, 0},
        {{{0, 1}}, {1}, 0, 0},
        {{{0, 3}, {0, 4}, {0, 6}}, {1, 1, 1}, 3, 0},
        {{{0, 3}, {0, 4}, {5, 7}, {5, 8}}, {1, 1, 1, 1}, 3, 7},
        {{{0, 3}, {0, 4}, {5, 7}}, {0}, 3, 7},
        {{{0, 3}, {0, 4}, {5, 7}}, {2}, 3, 7},
        {{{0, 3}, {0, 4}, {5, 7}}, {3}, 3, 7},
        {{{0, 3}, {0, 4}, {5, 7}}, {5}, 3, 7},
        {{{0, 3}, {0, 4}, {5, 7}}, {1, 4}, 3, 7},
        {{{0, 3}, {0, 4}, {5, 7}}, {1, 3}, 3, 7},
        {{{0, 3}, {0, 4}, {5, 7}}, {1, 1, 5}, 3, 7},
        {{{0, 3, 5}, {5, 7}, {5, 8, 0}}, {1, 1, 1}, 3, 7},
        {{{0, 3, -1, true}, {0, 4}}, {1, 1}, 3, 0},
        {{{5, 0xFFFFFFFFu}, {5, 0}}, {1, 1}, 0, 0xFFFFFFFFu},
        {{{0, 9}, {0, 3}, {0, 4}}, {1, 1, 1}, 3, 0},
    };
    Conn* conn = TheConn(3);
    for (const SeqCase& c : seqCases) {
        for (Mem::ListNode& queue : conn->m_receivedQueues)
            InitHead(&queue, 0x00647584);
        for (uint32_t& number : conn->m_sequenceNumbers)
            number = 0;
        conn->m_sequenceNumbers[0] = c.expected0;
        conn->m_sequenceNumbers[5] = c.expected5;
        int i = 0;
        for (const Queued& q : c.queued) {
            Net::MsgLink* link = AddLink(&conn->m_receivedQueues[q.group], i);
            link->At<uint8_t>(0x15) = static_cast<uint8_t>(q.otherGroup >= 0 ? q.otherGroup : q.group);
            link->At<uint32_t>(0x20) = q.seq;
            Net::SeqQueuedMsg* msg = Msg(i);
            *msg = {static_cast<uint8_t>(0x40 + i), static_cast<uint16_t>(0x100 + i),
                    q.noData ? nullptr : reinterpret_cast<char*>(Misc(0x3800 + i)), 0, 1};
            link->At<Net::SeqQueuedMsg*>(0x28) = msg;
            ++i;
        }
        g_dispatchResults = c.results;
        Record({0x5E0, Fn<ThisCall1>(original, 0x00457560)(app, reinterpret_cast<uint32_t>(conn)) & 0xFF,
                conn->m_sequenceNumbers[0], conn->m_sequenceNumbers[3], conn->m_sequenceNumbers[5],
                conn->m_sequenceNumbers[7], conn->m_sequenceNumbers[8]});
        RecordList(&conn->m_receivedQueues[0]);
        RecordList(&conn->m_receivedQueues[5]);
    }

    // FreeMessages.
    for (int queue : {0, 1, 2, 3, -1}) {
        SetUpQueues(conn);
        ListNode* lists[] = {&conn->m_normalMsgs, &conn->m_importantMsgs, &conn->m_sequencedMsgs};
        const uint32_t msgOffsets[] = {0x14, 0x18, 0x28};
        const uint8_t ids[] = {7, 9, 7, 7, 8, 7};
        const int counts[] = {1, 1, 2, 3, 1, 1};
        for (int list = 0; list < 3; ++list) {
            for (int i = 0; i < 6; ++i) {
                int index = list * 6 + i;
                Net::MsgLink* link = AddLink(lists[list], index);
                auto* msg = Misc<uint32_t>(0x3400 + index * 0x10);
                msg[0] = ids[i] | 0xCDCD0000u;
                msg[1] = 0;
                msg[2] = static_cast<uint32_t>(counts[i]);
                msg[3] = static_cast<uint32_t>(counts[(i + 1) % 6]);
                for (uint32_t offset : {0x14u, 0x18u, 0x28u})
                    link->At<uint32_t>(offset) = 0xDEAD0000u + offset;
                link->At<void*>(msgOffsets[list]) = msg;
            }
        }
        Fn<ThisCall3>(original, 0x00457C80)(app, reinterpret_cast<uint32_t>(conn), static_cast<uint32_t>(queue),
                                            0xABCDEF07u);
        for (ListNode* list : lists)
            RecordList(list);
        RecordWords(Misc(0x3400), 18 * 0x10);
    }

    // The destructor body.
    for (int variant = 0; variant < 2; ++variant) {
        app->m_vtable = reinterpret_cast<void**>(0x1234);
        InitHead(&app->m_banned, 0x006475B0);
        InitHead(&app->m_connections, 0x006475AC);
        InitHead(&app->m_list1414, 0x006474D8);
        if (variant) {
            for (int i = 0; i < 2; ++i)
                Append(&app->m_banned, Link(i), reinterpret_cast<uint32_t>(g_linkVtable), Link(i));
            Append(&app->m_connections, &TheConn(0)->m_appNode, 0x00647574, TheConn(0));
            Append(&app->m_list1414, Misc<ListNode>(0x1D00), 0x2222, Misc(0x1D00));
        }
        uint32_t* tasks = Misc<uint32_t>(0x1E00);
        for (int i = 0; i < 4; ++i)
            tasks[i * 4] = reinterpret_cast<uint32_t>(g_linkVtable);
        app->m_processTask = variant ? tasks : nullptr;
        app->m_sendTask = tasks + 4;
        app->m_receiveTask = variant ? tasks + 8 : nullptr;
        app->m_metricsTask = tasks + 12;
        for (Net::MsgHandlerNode& list : app->m_dispatcher.m_lists)
            InitHead(&list, 0x006475A8);
        if (variant)
            Append(&app->m_dispatcher.m_lists[8], Misc<ListNode>(0x1D40), 0x2222, Misc(0x1D40));
        Fn<ThisCall0>(original, 0x00457710)(app);
        RecordWords(app, 0x20);
        RecordWords(reinterpret_cast<uint8_t*>(app) + 0x1400, 0x60);
        RecordWords(reinterpret_cast<uint8_t*>(app) + 0x1E80, 0x38);
        RecordWords(Misc(0x1D00), 0x60);
        RecordWords(&TheConn(0)->m_appNode, 0x14);
        Record({HashBytes(reinterpret_cast<const uint8_t*>(&app->m_dispatcher), sizeof(app->m_dispatcher))});
        RecordWords(&app->m_dispatcher.m_lists[7], 0x3C);
    }
    return g_trace;
}

void TestNetQueues()
{
    std::vector<uint32_t> traces[2];
    SaveGlobals();
    {
        auto* s = new StandIns();
        StandInMemory(*s);
        StandInQueues(*s);
        StandInSockets(*s);
        traces[0] = RunQueues(true);
        traces[1] = RunQueues(false);
        delete s;
    }
    RestoreGlobals();
    Compare(traces[0], traces[1],
            "Net::App queues: NewQueuedMsg, TerminateConnection, BanConnection, DispatchQueuedSequencedMessages, "
            "FreeMessages, the destructor body");
}

} // namespace

SELFTEST(Early, TestNetLatency);
SELFTEST(Early, TestNetLists, CleanUp);
SELFTEST(Early, TestNetSockets, CleanUp);
SELFTEST(Early, TestNetQueues, CleanUp);

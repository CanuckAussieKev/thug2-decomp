// Self-tests for the net library's 0x00454FA0-0x00455970 (game/net/net_messages, net_manager_view,
// net_conn_times): the original against ours on identical stand-in objects, with everything they call outside
// (the memory manager, the message compressor, the links' destructor bodies, the Server's / Client's
// constructors, the apps' virtual functions, Bind, the task registration, Winsock's inet_addr / inet_ntoa, the
// debugger output, the timer) stood in for by recording code (standins.h). No real socket is touched.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, inet_ntoa stood in for
#include <winsock2.h>
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/net_app_view.h"
#include "game/net/net_client.h"
#include "game/net/net_manager_view.h"
#include "game/net/net_messages.h"
#include "game/net/net_server.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
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
template <typename F>
F Fn(bool original, uint32_t address)
{
    void* code = original ? OriginalFunction(address) : OursAt(address);
    if (!code)
        Log("self-test netE: nothing registered at %08X", address);
    return reinterpret_cast<F>(code);
}

// Every function of this range: in the original's run, calls to them (from inside originals too) run originals.
const std::vector<uint32_t> kRange = {
    0x00454FA0, 0x00455040, 0x004550F0, 0x00455110, 0x00455130, 0x00455170, 0x004551B0, 0x004551F0, 0x00455230,
    0x00455270, 0x004553F0, 0x00455550, 0x004555A0, 0x004555F0, 0x00455620, 0x00455650, 0x00455680, 0x004556B0,
    0x004556E0, 0x004557C0, 0x004557F0, 0x00455820, 0x00455850, 0x00455880, 0x004558D0, 0x00455920, 0x00455940,
    0x00455960, 0x00456F90};
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

alignas(16) uint8_t g_objs[0x4000];   // messages, links, nodes, the manager
alignas(16) uint8_t g_heap[0x40000];  // what the stand-in allocator hands out
alignas(16) uint8_t g_fakeManager[0x600];
uint32_t g_heapUsed;
bool g_allocFail;

template <typename T = uint8_t>
T* Obj(uint32_t offset)
{
    return reinterpret_cast<T*>(g_objs + offset);
}

uint32_t Rel(uint32_t p)
{
    auto in = [p](const void* base, size_t size) {
        auto b = reinterpret_cast<uint32_t>(base);
        return p >= b && p < b + size ? p - b : 0xFFFFFFFF;
    };
    if (uint32_t o = in(g_objs, sizeof(g_objs)); o != 0xFFFFFFFF)
        return 0xD0000000 + o;
    if (uint32_t o = in(g_heap, sizeof(g_heap)); o != 0xFFFFFFFF)
        return 0xE0000000 + o;
    if (uint32_t o = in(g_fakeManager, sizeof(g_fakeManager)); o != 0xFFFFFFFF)
        return 0xF0000000 + o;
    uint8_t probe = 0;
    auto sp = reinterpret_cast<uint32_t>(&probe);
    if (p > sp - 0x100000 && p < sp + 0x100000)
        return 0x57AC0000;
    return p;
}
uint32_t Rel(const void* p) { return Rel(reinterpret_cast<uint32_t>(p)); }

void RecordWords(const void* p, size_t bytes)
{
    auto* words = static_cast<const uint32_t*>(p);
    for (size_t i = 0; i < bytes / 4; ++i)
        Record({Rel(words[i])});
}
void InitHead(ListNode* head, uint32_t vtable)
{
    *head = {reinterpret_cast<void**>(vtable), reinterpret_cast<void*>(-1), 0, head, head};
}
void Append(ListNode* head, ListNode* node, uint32_t vtable, int priority)
{
    node->vtable = reinterpret_cast<void**>(vtable);
    node->data = node;
    node->priority = priority;
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
// A list from its head, both ways round (at most 40 nodes).
void RecordList(ListNode* head)
{
    Record({0x1157, Rel(head), Rel(head->vtable)});
    int n = 0;
    for (ListNode* node = head->next; node != head && n < 40; node = node->next, ++n)
        Record({Rel(node), Rel(node->data), static_cast<uint32_t>(node->priority), Rel(node->vtable)});
    Record({0x1158});
    n = 0;
    for (ListNode* node = head->prev; node != head && n < 40; node = node->prev, ++n)
        Record({Rel(node)});
}

uint8_t* HeapBlock(uint32_t size)
{
    uint8_t* block = g_heap + g_heapUsed;
    g_heapUsed += (size + 15) & ~15u;
    return block;
}

void StandInMemory(StandIns& s)
{
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA110, r.ecx == reinterpret_cast<uint32_t>(g_fakeManager), r.args[0], r.args[1], r.args[2]});
        if (g_allocFail)
            return 0u;
        return reinterpret_cast<uint32_t>(HeapBlock(r.args[0]));
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE, r.ecx == reinterpret_cast<uint32_t>(g_fakeManager), Rel(r.args[0])});
        return 0u;
    });
}

std::vector<Saved> g_saved;
bool g_savedPending;
void SaveGlobals()
{
    g_saved = {{0x007D1800, 4, {}}, {0x007CC979, 1, {}}, {0x006EFDD8, 8, {}}};
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
    std::memset(g_objs, 0xCD, sizeof(g_objs));
    std::memset(g_heap, 0xAB, sizeof(g_heap));
    std::memset(g_fakeManager, 0, sizeof(g_fakeManager));
    *reinterpret_cast<uint32_t*>(g_fakeManager + 0x12C) = 0x12C12C12; // the network heap
    g_heapUsed = 0;
    g_allocFail = false;
}

// --- Queued messages, links, list nodes -------------------------------------------------------------------------

using ThisCall0 = uint32_t(__thiscall*)(void*);
using ThisCall1 = uint32_t(__thiscall*)(void*, uint32_t);
using ThisCall4 = uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
using ThisCall5 = uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

void StandInMessages(StandIns& s)
{
    // -> the compressor (wt/netD): 1 succeeds (half the size), 3 succeeds (one byte less), else fails
    s.At({0x00454E30}, 0, [](Regs& r) {
        auto* id = reinterpret_cast<uint8_t*>(r.args[0]);
        auto* length = reinterpret_cast<uint16_t*>(r.args[1]);
        auto* data = reinterpret_cast<const uint8_t*>(r.args[2]);
        uint8_t method = r.args[3] & 0xFF;
        Record({0x4E30, Rel(r.args[0]), Rel(r.args[1]), *id, *length, Rel(r.args[2]), HashBytes(data, *length),
                method}); // (the original passes the argument's whole dword: only its low byte counts)
        if (method != 1 && method != 3)
            return 0u;
        uint16_t newLength = method == 1 ? static_cast<uint16_t>(*length / 2 + 1) : static_cast<uint16_t>(*length - 1);
        uint8_t* block = HeapBlock(newLength);
        for (uint16_t i = 0; i < newLength; ++i)
            block[i] = static_cast<uint8_t>(0x70 + i * 3 + method);
        *id = 0x12;
        *length = newLength;
        return reinterpret_cast<uint32_t>(block);
    });
    // -> the links' destructor bodies (wt/netF)
    std::pair<uint32_t, uint32_t> bodies[] = {{0x00455B50u, AddressOf(&Net::MsgNormalNode::DestroyBody)},
                                              {0x00455B70u, AddressOf(&Net::MsgImpNode::DestroyBody)},
                                              {0x00455B90u, AddressOf(&Net::MsgSeqNode::DestroyBody)},
                                              {0x00455BB0u, AddressOf(&Net::StreamLinkNode::DestroyBody)}};
    for (auto [address, ours] : bodies) {
        s.At({address, ours}, 0, [address](Regs& r) {
            Record({address, Rel(r.ecx)});
            return 0x5A5A5A5Au;
        });
    }
}

std::vector<uint32_t> RunMessages(bool original)
{
    ResetObjects();
    Originals guards(original);
    uint8_t* source = Obj(0x3000);
    for (int i = 0; i < 0x100; ++i)
        source[i] = static_cast<uint8_t>(i * 7 + 1);

    // The constructors: lengths 0 (nothing copied, even compressed), 1, 7, 9 and 0x10005 (16 bits: 5), each
    // plain and with compression that works (1, 3) or doesn't (2); dirty high bits in the byte arguments.
    struct Case {
        uint32_t id, length, compression;
    };
    const Case cases[] = {{0x11223305, 0, 0}, {0x00000006, 0, 1}, {0xFFFFFF07, 1, 0}, {0x08, 7, 0},
                          {0x09, 9, 0x101}, {0x0A, 9, 2}, {0x0B, 7, 3}, {0x0C, 0x10005, 0}, {0xFF, 0x10005, 1}};
    uint32_t offset = 0;
    for (const Case& c : cases) {
        for (bool seq : {false, true}) {
            uint8_t* message = Obj(offset);
            offset += 0x20;
            uint32_t result;
            if (seq)
                result = Fn<ThisCall5>(original, 0x00455040)(message, c.id, c.length, reinterpret_cast<uint32_t>(source),
                                                             0xABCDEF00 + c.id, c.compression);
            else
                result = Fn<ThisCall4>(original, 0x00454FA0)(message, c.id, c.length, reinterpret_cast<uint32_t>(source),
                                                             c.compression);
            Record({0xC0, seq, Rel(result)});
            RecordWords(message, 0x14);
            uint8_t* data = *reinterpret_cast<uint8_t**>(message + 4);
            uint16_t length = *reinterpret_cast<uint16_t*>(message + 2);
            if (data && Rel(data) >> 28 == 0xE)
                Record({HashBytes(data, length)});
            // The destructor (0x004550F0, both kinds): frees the data if there is any.
            Fn<ThisCall0>(original, 0x004550F0)(message);
            RecordWords(message, 0x14);
        }
    }

    // The link constructors on dirty memory.
    auto* msg = Obj<Net::QueuedMsg>(0x3800);
    for (uint32_t address : {0x004555F0u, 0x00455620u, 0x00455650u, 0x00455680u}) {
        uint8_t* link = Obj(offset);
        offset += 0x30;
        Record({address, Rel(Fn<ThisCall1>(original, address)(link, reinterpret_cast<uint32_t>(msg) + address % 7))});
        RecordWords(link, 0x30);
    }
    // The link deleting destructors: the body (stood in), then freed with flags bit 0.
    for (uint32_t address : {0x004557C0u, 0x004557F0u, 0x00455820u, 0x00455850u}) {
        for (uint32_t flags : {0u, 1u, 2u, 3u}) {
            uint8_t* link = Obj(offset);
            offset += 0x10;
            Record({address, flags, Rel(Fn<ThisCall1>(original, address)(link, flags))});
        }
    }

    // The node destructors: a middle node, the first, the last; each with other flags.
    const uint32_t nodeDestructors[] = {0x00455130, 0x00455170, 0x004551B0, 0x004551F0, 0x00455230, 0x004556B0,
                                        0x00455110};
    for (uint32_t address : nodeDestructors) {
        auto* head = Obj<ListNode>(offset);
        const uint32_t nodes = offset + 0x20;
        offset += 0x20 + 4 * 0x20;
        InitHead(head, 0x00640000 + address % 0x1000);
        for (uint32_t i = 0; i < 4; ++i)
            Append(head, Obj<ListNode>(nodes + i * 0x20), 0x00641000 + i, 10 - static_cast<int>(i));
        const uint32_t order[] = {1, 0, 3, 2};
        const uint32_t flags[] = {0, 1, 3, 2};
        for (int k = 0; k < 4; ++k) {
            ListNode* node = Obj<ListNode>(nodes + order[k] * 0x20);
            uint32_t result = address == 0x00455110 ? (Fn<ThisCall0>(original, address)(node), 0u)
                                                    : Fn<ThisCall1>(original, address)(node, flags[k]);
            Record({address, static_cast<uint32_t>(order[k]), address == 0x00455110 ? 0u : Rel(result)});
            RecordWords(node, 0x14);
            RecordList(head);
        }
    }
    Record({g_heapUsed});
    return g_trace;
}

void TestNetMessages()
{
    std::vector<uint32_t> traces[2];
    SaveGlobals();
    {
        auto* s = new StandIns();
        StandInMemory(*s);
        StandInMessages(*s);
        traces[0] = RunMessages(true);
        traces[1] = RunMessages(false);
        delete s;
    }
    RestoreGlobals();
    Compare(traces[0], traces[1],
            "Net queued messages: QueuedMsg / SeqQueuedMsg constructors (compressed or not) and destructor, the "
            "links' constructors and deleting destructors, the Lst::Node<T> destructors");
}

// --- The manager -------------------------------------------------------------------------------------------------

uint32_t g_appVtable[6];
char g_ntoa[32];

uint32_t __cdecl Unused() { return 0; }

void StandInManager(StandIns& s)
{
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, r.ecx == reinterpret_cast<uint32_t>(g_fakeManager), r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0xB110, r.ecx == reinterpret_cast<uint32_t>(g_fakeManager)});
        return 0u;
    });
    s.At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0,
         [](Regs& r) {
             Record({0x1D40, r.args[0], r.args[1], r.args[2]});
             if (g_allocFail)
                 return 0u;
             uint8_t* block = HeapBlock(r.args[0]);
             std::memset(block, 0, r.args[0]);
             return reinterpret_cast<uint32_t>(block);
         });
    auto construct = [](uint32_t tag) {
        return [tag](Regs& r) {
            Record({tag, Rel(r.ecx), r.args[0]});
            auto* app = reinterpret_cast<uint8_t*>(r.ecx);
            *reinterpret_cast<uint32_t*>(app) = reinterpret_cast<uint32_t>(g_appVtable);
            *reinterpret_cast<uint32_t*>(app + 0x1E84) = r.args[0];
            auto* node = reinterpret_cast<ListNode*>(app + 0x1414);
            *node = {reinterpret_cast<void**>(0x006474D8), app, 0, node, node};
            return r.ecx;
        };
    };
    s.At({0x0045B830, AddressOf(&Net::Server::Construct)}, 4, construct(0xB830));
    s.At({0x0045A250, AddressOf(&Net::Client::Construct)}, 4, construct(0xA250));
    // The apps' virtual functions: 0 the deleting destructor, 4 Init, 5 Deinit.
    for (uint32_t& slot : g_appVtable)
        slot = reinterpret_cast<uint32_t>(&Unused);
    g_appVtable[0] = reinterpret_cast<uint32_t>(s.Thunk(4, [](Regs& r) {
        Record({0xDE1, Rel(r.ecx), r.args[0]});
        return r.ecx;
    }));
    g_appVtable[4] = reinterpret_cast<uint32_t>(s.Thunk(0, [](Regs& r) {
        auto* app = reinterpret_cast<uint8_t*>(r.ecx);
        Record({0x1A17, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(app + 0x1440)), HashBytes(app + 0x1EB8, 0x40)});
        return 1u;
    }));
    g_appVtable[5] = reinterpret_cast<uint32_t>(s.Thunk(0, [](Regs& r) {
        Record({0xDE17, Rel(r.ecx)});
        return 0x77u;
    }));
    s.At({0x00456BA0, AddressOf(&Net::App::Bind)}, 8, [](Regs& r) {
        Record({0xB1D, Rel(r.ecx), r.args[0], r.args[1] & 0xFFFF});
        return 1u;
    });
    // -> 0x00454CB0 (wt/netD): the app's tasks
    s.At({0x00454CB0}, 4, [](Regs& r) {
        Record({0x4CB0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        char text[512];
        std::vsnprintf(text, sizeof(text), reinterpret_cast<const char*>(r.args[0]),
                       reinterpret_cast<va_list>(&r.args[1]));
        Record({0x1C30, Hash(text)});
        return 0xFFFFFFFFu;
    });
    StandInApi(s, 0x006452A0, reinterpret_cast<const void*>(&inet_addr), 4, [](Regs& r) {
        Record({0x1AD0, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0x0D0C0B0Au + static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(r.args[0])));
    });
    StandInApi(s, 0x006452A4, reinterpret_cast<const void*>(&inet_ntoa), 4, [](Regs& r) {
        Record({0x1A70, r.args[0]});
        std::snprintf(g_ntoa, sizeof(g_ntoa), "ip<%08X>", r.args[0]);
        return reinterpret_cast<uint32_t>(g_ntoa);
    });
}

void RecordManager(uint8_t* manager)
{
    RecordList(reinterpret_cast<ListNode*>(manager));
    RecordList(reinterpret_cast<ListNode*>(manager + 0x14));
    Record({*reinterpret_cast<uint32_t*>(manager + 0x2DC)});
}
void RecordApp(uint8_t* app)
{
    Record({Rel(app)});
    if (!app)
        return;
    RecordWords(app, 8);
    RecordWords(app + 0x1414, 0x14);
    RecordWords(app + 0x143C, 8);
    RecordWords(app + 0x1458, 4);
    RecordWords(app + 0x1E84, 4);
    RecordWords(app + 0x1EB4, 0x48);
}

std::vector<uint32_t> RunManager(bool original)
{
    ResetObjects();
    Originals guards(original);
    GameVariable<uint32_t>(0x006EFDD8) = 0;
    GameVariable<uint32_t>(0x006EFDDC) = 0;

    // The constructor on dirty memory (the last message flag isn't cleared), then the destructor body.
    uint8_t* manager = Obj(0x100);
    Record({Rel(Fn<ThisCall0>(original, 0x004556E0)(manager))});
    RecordWords(manager, 0x380);
    for (int i = 0; i < 3; ++i)
        Append(reinterpret_cast<ListNode*>(manager + (i == 1 ? 0x14 : 0)), Obj<ListNode>(0x600 + i * 0x20),
               0x006474D8, i);
    Fn<ThisCall0>(original, 0x004555A0)(manager);
    RecordWords(manager, 0x28);
    RecordWords(Obj(0x600), 0x60);

    // The singleton.
    auto create = Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x00455880);
    auto release = Fn<void(__cdecl*)()>(original, 0x004558D0);
    auto globals = [] {
        Record({Rel(GameVariable<uint32_t>(0x006EFDD8)), GameVariable<uint32_t>(0x006EFDDC)});
    };
    Record({Rel(create(0xFFFFFF00))}); // (false in the low byte)
    globals();
    Record({Rel(create(1))});
    globals();
    Record({Rel(create(0x101))});
    globals();
    uint8_t* instance = GameVariable<uint8_t*>(0x006EFDD8);
    RecordWords(instance, 0x37C);
    // An app node in each list, so the destructor body's unlinking shows.
    Append(reinterpret_cast<ListNode*>(instance), Obj<ListNode>(0x700), 0x006474D8, 1);
    Append(reinterpret_cast<ListNode*>(instance + 0x14), Obj<ListNode>(0x720), 0x006474D8, 2);
    for (int i = 0; i < 4; ++i) {
        release();
        globals();
    }
    RecordWords(instance, 0x28);
    RecordWords(Obj(0x700), 0x40);
    g_allocFail = true;
    Record({Rel(create(1))});
    globals();
    g_allocFail = false;
    Record({Rel(create(0))});
    globals();
    release();
    globals();
    release();
    globals();

    // Apps: servers and clients with the configured IP address used or not, local or not, connected or not;
    // names short, empty and longer than the 64 bytes copied.
    Fn<ThisCall0>(original, 0x004556E0)(manager);
    const char* longName = "a name of more than sixty-four characters, so that strncpy stops short of its end";
    struct ServerCase {
        int id;
        const char* name;
        int maxClients;
        uint32_t port;
        int address;
        int flags;
        int connectionType;
        bool useLocalIp;
    };
    const ServerCase servers[] = {
        {1, "server", 8, 0x5555ABCD, 0x01020304, 0, 0, false}, {2, "srv2", 3, 5150, 0, 1, 2, true},
        {3, longName, 0x7FFFFFFF, 1, 5, 8, 1, true},            {4, "x", 0, 2, 6, 9, 3, true},
        {5, "", 4, 0xFFFF, -1, 2, 0, true},                     {6, "s6", 2, 3, 7, 0x10, 4, true}};
    using CreateServer = uint32_t(__thiscall*)(void*, int, const char*, int, uint32_t, int, int);
    using CreateClient = uint32_t(__thiscall*)(void*, int, const char*, uint32_t, int, int);
    std::vector<uint8_t*> apps;
    int k = 0;
    for (const ServerCase& c : servers) {
        *reinterpret_cast<int*>(manager + 0x128) = c.connectionType;
        manager[0x2E5] = c.useLocalIp;
        std::snprintf(reinterpret_cast<char*>(manager + 0x134), 0x14, "10.0.%d.%d", k, k * 3);
        ++k;
        auto* app = reinterpret_cast<uint8_t*>(Fn<CreateServer>(original, 0x00455270)(
            manager, c.id, c.name, c.maxClients, c.port, c.address, c.flags));
        RecordApp(app);
        RecordManager(manager);
        apps.push_back(app);
    }
    const ServerCase clients[] = {{11, "client", 0, 0x1234, 0x0A0B0C0D, 0, 0, false},
                                  {12, longName, 0, 0x8000, 7, 8, 2, true},
                                  {13, "c3", 0, 99, 7, 1, 3, true},
                                  {14, "c4", 0, 0x1FFFF, 8, 0x11, 0x101, true},
                                  {15, "c5", 0, 5, 9, 0, 1, false}};
    for (const ServerCase& c : clients) {
        *reinterpret_cast<int*>(manager + 0x128) = c.connectionType;
        manager[0x2E5] = c.useLocalIp;
        std::snprintf(reinterpret_cast<char*>(manager + 0x134), 0x14, "10.1.%d.%d", k, k + 1);
        ++k;
        auto* app = reinterpret_cast<uint8_t*>(
            Fn<CreateClient>(original, 0x004553F0)(manager, c.id, c.name, c.port, c.address, c.flags));
        RecordApp(app);
        RecordManager(manager);
        apps.push_back(app);
    }
    // Destroyed in a mixed order.
    for (size_t i : {1u, 7u, 0u, 10u, 5u, 6u}) {
        Fn<ThisCall1>(original, 0x00455550)(manager, reinterpret_cast<uint32_t>(apps[i]));
        RecordWords(apps[i] + 0x1414, 0x14);
        RecordManager(manager);
    }
    Record({g_heapUsed});
    return g_trace;
}

void TestNetManager()
{
    std::vector<uint32_t> traces[2];
    SaveGlobals();
    {
        auto* s = new StandIns();
        StandInMemory(*s);
        StandInManager(*s);
        traces[0] = RunManager(true);
        traces[1] = RunManager(false);
        delete s;
    }
    RestoreGlobals();
    Compare(traces[0], traces[1],
            "Net::Manager: constructor, destructor body, CreateManager / ReleaseManager, CreateNewAppServer, "
            "CreateNewAppClient, DestroyApp");
}

// --- Conn's communication times ------------------------------------------------------------------------------

uint32_t g_now;

std::vector<uint32_t> RunConnTimes(bool original)
{
    ResetObjects();
    Originals guards(original);
    uint8_t* conn = Obj(0);
    uint32_t& last = *reinterpret_cast<uint32_t*>(conn + 0x6C);
    struct Case {
        uint32_t last, now, extra;
    };
    const Case cases[] = {{100, 50, 10},          {100, 90, 10}, {100, 90, 11},          {0xFFFFFFF0, 0x10, 0xFFFFFFF0},
                          {5, 0xFFFFFFFF, 2},     {0, 1, 0},     {0x80000000, 0x7FFFFFFF, 2}, {7, 7, 0},
                          {0xFFFFFFFF, 5, 0}};
    for (const Case& c : cases) {
        last = c.last;
        g_now = c.now;
        Fn<ThisCall1>(original, 0x00455920)(conn, c.extra);
        Record({last});
        Record({Fn<ThisCall0>(original, 0x00455940)(conn)});
        last = c.extra;
        Record({Fn<ThisCall0>(original, 0x00455940)(conn)});
    }
    for (uint32_t interval : {0u, 33u, 0xFFFFFFFFu}) {
        Fn<ThisCall1>(original, 0x00455960)(conn, interval);
        RecordWords(conn + 0x68, 0x10);
    }
    return g_trace;
}

void TestNetConnTimes()
{
    std::vector<uint32_t> traces[2];
    SaveGlobals();
    {
        auto* s = new StandIns();
        s->At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
            Record({0x7123});
            return g_now;
        });
        traces[0] = RunConnTimes(true);
        traces[1] = RunConnTimes(false);
        delete s;
    }
    RestoreGlobals();
    Compare(traces[0], traces[1],
            "Net::Conn: UpdateCommTime, GetTimeElapsedSinceCommunication (wrapping, equal), SetSendInterval");
}

} // namespace

SELFTEST(Early, TestNetMessages, CleanUp);
SELFTEST(Early, TestNetManager, CleanUp);
SELFTEST(Early, TestNetConnTimes, CleanUp);

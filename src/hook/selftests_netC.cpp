// Self-tests for the network library's 0x4598C0-0x45BDA0 (Net::Client, Net::Server, the message dispatcher, the
// App's message bundling and handlers). Each original runs against ours on identical stand-in objects (an App,
// connections, an arena the stand-in memory manager allocates from); every function they call outside this range
// (the rest of the library, the memory manager, the timer, WinSock) is stood in for by recording code, and the
// call traces and the objects' memory are compared. Nothing touches the real network.

#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/head.h"
#include "game/net/net_access_c.h"
#include "game/net/net_app_messages.h"
#include "game/net/net_client.h"
#include "game/net/net_compress.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_latency.h"
#include "game/net/net_server.h"
#include "game/sys/timer.h"
#include "game/util/lzss.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace Net;

// --- The stand-in world ---

constexpr uint32_t kArenaSize = 0x40000;
constexpr uint32_t kAppSize = 0x2000;
constexpr uint32_t kConnSize = 0x4000;
constexpr int kNumConns = 3;
uint8_t* g_arena;
uint32_t g_arenaUsed;
int g_allocations;
int g_failAllocation = -1; // this allocation (counted from 0) returns null
alignas(16) uint8_t g_app[kAppSize];
alignas(16) uint8_t g_conns[kNumConns][kConnSize];
alignas(16) uint8_t g_memManager[0x600];
alignas(16) uint8_t g_flagTable[0x130];
std::map<uint32_t, std::deque<uint32_t>> g_script;
std::vector<std::pair<uint32_t, uint32_t>> g_codeMap; // our code addresses -> the originals'
uint32_t g_savedManager;
uint8_t g_savedReady;
bool g_saved;

App* TheApp() { return reinterpret_cast<App*>(g_app); }
Conn* TheConn(int i) { return reinterpret_cast<Conn*>(g_conns[i]); }
Dispatcher* TheDispatcher() { return reinterpret_cast<Dispatcher*>(g_app + offsetof(Net::App, m_dispatcher)); }

uint32_t Next(uint32_t tag, uint32_t otherwise)
{
    auto& queue = g_script[tag];
    if (queue.empty())
        return otherwise;
    uint32_t value = queue.front();
    queue.pop_front();
    return value;
}
void Answers(uint32_t tag, std::initializer_list<uint32_t> values) { g_script[tag].assign(values); }

// Pointers into the stand-in objects, made comparable (both runs use the same objects anyway).
uint32_t Rel(uint32_t p)
{
    auto arena = reinterpret_cast<uint32_t>(g_arena);
    auto app = reinterpret_cast<uint32_t>(g_app);
    auto conns = reinterpret_cast<uint32_t>(g_conns);
    auto table = reinterpret_cast<uint32_t>(g_flagTable);
    if (p >= arena && p < arena + kArenaSize)
        return 0xA0000000 + (p - arena);
    if (p >= app && p < app + kAppSize)
        return 0xB0000000 + (p - app);
    if (p >= conns && p < conns + sizeof(g_conns))
        return 0xC0000000 + (p - conns);
    if (p >= table && p < table + sizeof(g_flagTable))
        return 0xD0000000 + (p - table);
    return p;
}
uint32_t Rel(const void* p) { return Rel(reinterpret_cast<uint32_t>(p)); }
uint32_t Hash(const void* data, uint32_t size) { return HashBytes(static_cast<const uint8_t*>(data), size); }

void* Bump(uint32_t size)
{
    uint32_t at = (g_arenaUsed + 15) & ~15u;
    g_arenaUsed = at + size;
    if (g_arenaUsed > kArenaSize) {
        Log("self-test: netC arena full");
        g_arenaUsed = at;
        return nullptr;
    }
    return g_arena + at;
}
// Memory for the test's own objects (not recorded).
template <typename T>
T* Make(uint32_t size = sizeof(T))
{
    auto* p = static_cast<T*>(Bump(size));
    std::memset(p, 0, size);
    return p;
}
char* MakeBytes(uint32_t size, uint8_t seed)
{
    auto* p = static_cast<char*>(Bump(size));
    for (uint32_t i = 0; i < size; ++i)
        p[i] = static_cast<char>(seed + i * 7);
    return p;
}

void InitHead(Mem::ListNode* head)
{
    head->vtable = nullptr;
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head->prev = head;
}
void AddTail(Mem::ListNode* head, Mem::ListNode* node, void* data)
{
    node->data = data;
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

// A vtable for the test's list nodes: slot 0 (the deleting destructor) records and unlinks.
uint32_t g_nodeVtable[1];

void SetUpStandIns(StandIns& s)
{
    auto memManager = reinterpret_cast<uint32_t>(g_memManager);
    // the memory manager
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [=](Regs& r) {
        Record({0xA11C, r.ecx == memManager, r.args[0], r.args[1], r.args[2]});
        if (g_allocations++ == g_failAllocation)
            return 0u;
        return reinterpret_cast<uint32_t>(Bump(r.args[0]));
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xCA11, r.args[0]});
        if (g_allocations++ == g_failAllocation)
            return 0u;
        void* p = Bump(r.args[0]);
        std::memset(p, 0, r.args[0]);
        return reinterpret_cast<uint32_t>(p);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [=](Regs& r) {
        Record({0xF4EE, r.ecx == memManager, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [=](Regs& r) {
        Record({0x9054, r.ecx == memManager, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [=](Regs& r) {
        Record({0x90B, r.ecx == memManager});
        return 0u;
    });
    s.At({0x004E1950, AddressOf(&Mem::EnsureManager)}, 0, [](Regs&) {
        Record({0xE45});
        GameVariable<bool>(0x007CC979) = true;
        return 1u;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        Record({0x713E});
        return Next(0x713E, 777);
    });

    // the rest of the library
    s.At({0x00457EB0, AddressOf(&App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* desc = reinterpret_cast<MsgDesc*>(r.args[1]);
        Record({0xE4C, Rel(r.ecx), r.args[0], desc->m_id, desc->m_length, static_cast<uint32_t>(desc->m_priority),
                static_cast<uint32_t>(desc->m_queue), desc->m_data ? Hash(desc->m_data, desc->m_length) : 0,
                desc->m_groupId, desc->m_singular, static_cast<uint32_t>(desc->m_delay), desc->m_forcedSequenceId,
                desc->m_compression, desc->m_stream});
        // (the rest of the descriptor isn't initialised)
        return 0u;
    });
    // (wt/netA's in C++ too: ours calls them directly)
    std::pair<uint32_t, uint32_t> conditions[] = {{0x00456B00u, AddressOf(&App::IsSendDue)},
                                                  {0x00457AC0u, AddressOf(&App::MessagesDue)},
                                                  {0x00458E60u, AddressOf(&App::BandwidthExceeded)},
                                                  {0x00457970u, AddressOf(&App::ImportantMessagesDue)}};
    for (auto [address, ours] : conditions) {
        s.At({address, ours}, 4, [address](Regs& r) {
            Record({address, Rel(r.ecx), Rel(r.args[0])});
            return Next(address, address == 0x00458E60u ? 0 : 1);
        });
    }
    s.At({0x00456E20, AddressOf(&App::SendTo)}, 0x14, [](Regs& r) {
        Record({0x6E20, Rel(r.ecx), r.args[0], r.args[1], Rel(r.args[2]), r.args[3], r.args[4],
                Hash(reinterpret_cast<void*>(r.args[2]), r.args[3])});
        return Next(0x6E20, 1);
    });
    s.At({0x00456F10, AddressOf(&App::Send)}, 0xC, [](Regs& r) {
        Record({0x6F10, Rel(r.ecx), Rel(r.args[0]), r.args[1], r.args[2], Hash(reinterpret_cast<void*>(r.args[0]), r.args[1])});
        return Next(0x6F10, 1);
    });
    s.At({0x004566D0, AddressOf(&Conn::FlagMessagesForResending)}, 4, [](Regs& r) {
        Record({0x66D0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00456540, AddressOf(&Conn::GetHighestResendCount)}, 0, [](Regs& r) {
        Record({0x6540, Rel(r.ecx)});
        return Next(0x6540, 0);
    });
    s.At({0x004569B0, AddressOf(&LatencyTest::InputLatencyValue)}, 4, [](Regs& r) {
        Record({0x69B0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00454550, AddressOf(&Net::Metrics::AddMessage)}, 8, [](Regs& r) {
        Record({0x4550, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00454D90, AddressOf(&Net::Metrics::AddPacket)}, 8, [](Regs& r) {
        Record({0x4D90, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004578B0, AddressOf(&App::GetConnectionByAddress)}, 8, [](Regs& r) {
        Record({0x78B0, Rel(r.ecx), r.args[0], r.args[1]});
        return Next(0x78B0, 0);
    });
    s.At({0x004597C0, AddressOf(&App::NewConnection)}, 0xC, [](Regs& r) {
        Record({0x97C0, Rel(r.ecx), r.args[0], r.args[1], r.args[2]});
        return Next(0x97C0, 0);
    });
    s.At({0x00456CC0, AddressOf(&App::Decrypt)}, 0xC, [](Regs& r) {
        Record({0x6CC0, Rel(r.ecx), Rel(r.args[0]), Rel(r.args[1]), r.args[2],
                Hash(reinterpret_cast<void*>(r.args[0]), std::min<uint32_t>(r.args[2], 0x514))});
        return Next(0x6CC0, 1);
    });
    s.At({0x00456790, AddressOf(&Conn::Destroy)}, 0, [](Regs& r) {
        Record({0x6790, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00457110, AddressOf(&App::Init)}, 0, [](Regs& r) {
        Record({0x7110, Rel(r.ecx)});
        return Next(0x7110, 1);
    });
    s.At({0x004594C0, AddressOf(&App::Construct)}, 4, [](Regs& r) {
        Record({0x94C0, Rel(r.ecx), r.args[0]});
        return r.ecx;
    });
    s.At({0x00406890}, 4, [](Regs& r) {
        Record({0x6890, Rel(r.ecx), r.args[0]});
        return r.ecx;
    });
    s.At({0x00457710, AddressOf(&App::DestroyBody)}, 0, [](Regs& r) {
        Record({0x7710, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00457490, AddressOf(&App::Deinit)}, 0, [](Regs& r) {
        Record({0x7490, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00456A90, AddressOf(&App::ReportError)}, 0, [](Regs& r) {
        Record({0x6A90, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00457300, AddressOf(&App::TerminateConnection)}, 4, [](Regs& r) {
        Record({0x7300, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00457280, AddressOf(&App::SendMessageTo)}, 0x18, [](Regs& r) {
        Record({0x7280, Rel(r.ecx), r.args[0] & 0xFF, r.args[1], r.args[2], r.args[3], r.args[4] & 0xFFFF, r.args[5]});
        return 0u;
    });
    s.At({0x00458740, AddressOf(&App::StreamMessageToConn)}, 0x20, [](Regs& r) {
        Record({0x8740, Rel(r.ecx), Rel(r.args[0])});
        for (int i = 1; i < 8; ++i)
            Record({r.args[i]});
        return 0u;
    });
    // method 1's decompressor: writes the script's size and pattern
    s.At({0x00454C20, AddressOf(&Net::RleDecompress)}, 0, [](Regs& r) {
        Record({0x4C20, Rel(r.args[0]), r.args[1], Rel(r.args[2]), Hash(reinterpret_cast<void*>(r.args[0]), r.args[1])});
        uint32_t size = Next(0x4C21, 0);
        for (uint32_t i = 0; i < size; ++i)
            reinterpret_cast<uint8_t*>(r.args[2])[i] = static_cast<uint8_t>(Next(0x4C22, i * 3));
        *reinterpret_cast<int*>(r.args[3]) = static_cast<int>(size);
        return Next(0x4C20, 0);
    });
    s.At({0x004014F0, AddressOf(&Lzss::Decode)}, 0, [](Regs& r) {
        Record({0x14F0, Rel(r.args[0]), Rel(r.args[1]), r.args[2], Hash(reinterpret_cast<void*>(r.args[0]), r.args[2])});
        uint32_t size = Next(0x14F1, 0);
        for (uint32_t i = 0; i < size; ++i)
            reinterpret_cast<uint8_t*>(r.args[1])[i] = static_cast<uint8_t>(Next(0x4C22, i * 5));
        return r.args[1] + size;
    });

    // WinSock
    StandInApi(s, 0x00645270, reinterpret_cast<const void*>(&recvfrom), 24, [](Regs& r) {
        int fromLength = *reinterpret_cast<int*>(r.args[5]);
        Record({0xF40F, r.args[0], Rel(r.args[1]), r.args[2], r.args[3], static_cast<uint32_t>(fromLength)});
        int size = static_cast<int>(Next(0xF40F, static_cast<uint32_t>(-1)));
        for (int i = 0; i < size && i < 0x514; ++i)
            reinterpret_cast<uint8_t*>(r.args[1])[i] = static_cast<uint8_t>(size + i * 11);
        auto* from = reinterpret_cast<sockaddr_in*>(r.args[4]);
        from->sin_family = AF_INET;
        from->sin_port = static_cast<u_short>(Next(0xF40A, 0x3412));
        from->sin_addr.s_addr = Next(0xF40B, 0x0100007F);
        return static_cast<uint32_t>(size);
    });
    StandInApi(s, 0x006452AC, reinterpret_cast<const void*>(&recv), 16, [](Regs& r) {
        Record({0x4EC, r.args[0], Rel(r.args[1]), r.args[2], r.args[3]});
        int size = static_cast<int>(Next(0xF40F, static_cast<uint32_t>(-1)));
        for (int i = 0; i < size && i < 0x514; ++i)
            reinterpret_cast<uint8_t*>(r.args[1])[i] = static_cast<uint8_t>(size - i * 3);
        return static_cast<uint32_t>(size);
    });
    StandInApi(s, 0x0064529C, reinterpret_cast<const void*>(&connect), 12, [](Regs& r) {
        auto* to = reinterpret_cast<sockaddr_in*>(r.args[1]);
        Record({0xC044, r.args[0], Rel(r.args[1]), r.args[2], to->sin_family, to->sin_port, to->sin_addr.s_addr,
                Hash(to->sin_zero, 8)});
        return Next(0xC044, 0);
    });
    StandInApi(s, 0x00645274, reinterpret_cast<const void*>(&WSAGetLastError), 0, [](Regs&) {
        Record({0x3A6E});
        return Next(0x3A6E, WSAEWOULDBLOCK);
    });
    // (htons and ntohs may be one function in WS2_32: both stand-ins do the same)
    auto swap = [](Regs& r) {
        Record({0x5A9, r.args[0] & 0xFFFF});
        return static_cast<uint32_t>(static_cast<uint16_t>((r.args[0] << 8) | ((r.args[0] >> 8) & 0xFF)));
    };
    StandInApi(s, 0x00645280, reinterpret_cast<const void*>(&htons), 4, swap);
    StandInApi(s, 0x006452B0, reinterpret_cast<const void*>(&ntohs), 4, swap);

    // the test's list nodes
    g_nodeVtable[0] = reinterpret_cast<uint32_t>(s.Thunk(4, [](Regs& r) {
        Record({0xDE1, Rel(r.ecx), r.args[0]});
        auto* node = reinterpret_cast<Mem::ListNode*>(r.ecx);
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->next = node->prev = node;
        return r.ecx;
    }));
}

void SaveGlobals()
{
    g_savedManager = GameVariable<uint32_t>(0x007D1800);
    g_savedReady = GameVariable<uint8_t>(0x007CC979);
    g_saved = true;
}
void RestoreGlobals()
{
    if (!g_saved)
        return;
    GameVariable<uint32_t>(0x007D1800) = g_savedManager;
    GameVariable<uint8_t>(0x007CC979) = g_savedReady;
    g_saved = false;
}
// (run even if a test crashes)
void CleanUpNetTest()
{
    RemoveStandIns();
    RestoreGlobals();
    if (g_arena) {
        VirtualFree(g_arena, 0, MEM_RELEASE);
        g_arena = nullptr;
    }
}

// A fresh world for one run: empty lists everywhere, a message flags table, the App's first connection.
void Reset()
{
    g_script.clear();
    g_arenaUsed = 0;
    g_allocations = 0;
    g_failAllocation = -1;
    std::memset(g_arena, 0xCD, kArenaSize);
    std::memset(g_app, 0, sizeof(g_app));
    std::memset(g_conns, 0, sizeof(g_conns));
    std::memset(g_memManager, 0, sizeof(g_memManager));
    *reinterpret_cast<uint32_t*>(g_memManager + 0x12C) = 0xBEEF012C; // the network heap
    GameVariable<uint32_t>(0x007D1800) = reinterpret_cast<uint32_t>(g_memManager);
    GameVariable<uint8_t>(0x007CC979) = 1;
    std::memset(g_flagTable, 0, sizeof(g_flagTable));
    for (int id : {0x21, 0x31, 0x40})
        g_flagTable[0x28 + id] = kMsgNoLength;
    g_flagTable[0x28 + 0x24] = 0xFE; // other bits don't matter

    for (int id = 0; id < Dispatcher::kNumMessageIds; ++id)
        InitHead(&TheDispatcher()->List(id).m_node);
    Field<uint8_t*>(TheApp(), offsetof(Net::App, m_manager)) = g_flagTable;
    InitHead(&Field<Mem::ListNode>(TheApp(), offsetof(Net::App, m_connections)));
    Field<uint32_t>(TheApp(), offsetof(Net::App, m_time)) = 40000;
    Field<int>(TheApp(), offsetof(Net::App, m_socket)) = 0x5C;
    for (int i = 0; i < kNumConns; ++i) {
        Conn* conn = TheConn(i);
        for (uint32_t offset : {offsetof(Net::Conn, m_normalMsgs), offsetof(Net::Conn, m_importantMsgs), offsetof(Net::Conn, m_sequencedMsgs),
                                offsetof(Net::Conn, m_receivedStreams)})
            InitHead(&Field<Mem::ListNode>(conn, offset));
        for (uint32_t group = 0; group < 256; ++group)
            InitHead(&Field<Mem::ListNode>(conn, offsetof(Net::Conn, m_receivedQueues) + group * 0x14));
        Field<App*>(conn, offsetof(Net::Conn, m_app)) = TheApp();
        Field<int>(conn, offsetof(Net::Conn, m_handle)) = 0x100 + i;
        Field<uint32_t>(conn, offsetof(Net::Conn, m_ip)) = 0x0A000001 + i;
        Field<uint16_t>(conn, offsetof(Net::Conn, m_port)) = static_cast<uint16_t>(5150 + i);
        char* read = MakeBytes(kConnReceiveBufferSize, static_cast<uint8_t>(0x10 + i));
        Field<char*>(conn, offsetof(Net::Conn, m_readBuffer)) = read;
        Field<char*>(conn, offsetof(Net::Conn, m_readPtr)) = read;
        char* write = MakeBytes(0x600, static_cast<uint8_t>(0x40 + i));
        Field<char*>(conn, offsetof(Net::Conn, m_writeBuffer)) = write;
        Field<char*>(conn, offsetof(Net::Conn, m_writePtr)) = write;
    }
}

// The objects' memory, our code addresses mapped to the originals'.
void RecordMemory()
{
    auto add = [](const uint8_t* bytes, uint32_t size) {
        for (uint32_t i = 0; i + 4 <= size; i += 4) {
            uint32_t word;
            std::memcpy(&word, bytes + i, 4);
            for (auto& [ours, original] : g_codeMap) {
                if (word == ours)
                    word = original;
            }
            g_trace.push_back(word);
        }
    };
    add(g_arena, (g_arenaUsed + 3) & ~3u);
    add(g_app, sizeof(g_app));
    add(reinterpret_cast<uint8_t*>(g_conns), sizeof(g_conns));
}

// Each case in a fresh world, the original's run then ours; the traces (a case marker, the calls, the memory
// after the case) compared.
template <typename Run>
void CompareRuns(const char* what, int cases, Run run)
{
    std::vector<uint32_t> traces[2];
    for (int original = 1; original >= 0; --original) {
        g_trace.clear();
        for (int i = 0; i < cases; ++i) {
            Reset();
            Record({0xCA5E0000u + i});
            run(original != 0, i);
            RecordMemory();
        }
        traces[original ? 0 : 1] = g_trace;
    }
    Compare(traces[0], traces[1], what);
    for (size_t i = 0; i < traces[0].size() && i < traces[1].size(); ++i) {
        if (traces[0][i] != traces[1][i]) { // the context of the first difference
            for (size_t j = i > 12 ? i - 12 : 0; j <= i; ++j)
                Log("  %s [%u] original %08X ours %08X", what, static_cast<unsigned>(j), traces[0][j], traces[1][j]);
            break;
        }
    }
}

struct World {
    World()
    {
        SaveGlobals();
        g_arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        auto* s = new StandIns;
        SetUpStandIns(*s);
    }
    ~World() { CleanUpNetTest(); }
};

template <typename T>
uint32_t Code(T function)
{
    uint32_t address;
    std::memcpy(&address, &function, 4);
    return address;
}

// --- Test handlers: record what they get, return what the script says ---

int __cdecl RecordingHandler(MsgHandlerContext* context)
{
    auto tag = reinterpret_cast<uint32_t>(context->m_data);
    // (a message without a length has a whole buffer's worth copied: look past its length then)
    uint32_t size = (g_flagTable[0x28 + context->m_MsgId] & kMsgNoLength) ? 0x40 : std::min<uint32_t>(context->m_MsgLength, 0x40);
    Record({0x4A, tag, context->m_MsgId, context->m_MsgLength, context->m_PacketFlags, Rel(context->m_App),
            Rel(context->m_Conn), Hash(context->m_Msg, size)});
    return static_cast<int>(Next(0x4A00 + tag, 1));
}
// Changes the length (as the timestamp and ack handlers do) and asks for acks.
int __cdecl AckingHandler(MsgHandlerContext* context)
{
    Record({0x4B, context->m_MsgId, context->m_MsgLength});
    context->m_MsgLength = Next(0x4B01, context->m_MsgLength);
    context->m_Acknowledge = Next(0x4B02, 1) != 0;
    context->m_SequencesToAck[context->m_NumSequencesToAck++] = Next(0x4B03, 0x12345);
    return kHandlerContinue;
}

using AddHandlerFn = MsgHandler*(__thiscall*)(Dispatcher*, uint32_t, MsgHandlerCode, uint32_t, void*, int);
MsgHandler* AddHandler(bool original, uint8_t id, MsgHandlerCode code, uint32_t flags, uint32_t tag, int priority)
{
    if (original)
        return Original<AddHandlerFn>(0x0045A5C0)(TheDispatcher(), id, code, flags, reinterpret_cast<void*>(tag),
                                                  priority);
    return TheDispatcher()->AddHandler(id, code, flags, reinterpret_cast<void*>(tag), priority);
}
int Dispatch(bool original, MsgHandlerContext* context)
{
    if (original)
        return Original<int(__thiscall*)(Dispatcher*, MsgHandlerContext*)>(0x0045A750)(TheDispatcher(), context);
    return TheDispatcher()->DispatchMsg(context);
}

void RecordList(Mem::ListNode* head)
{
    for (Mem::ListNode* node = head->next; node != head; node = node->next)
        Record({0x1157, Rel(node), Rel(node->data), static_cast<uint32_t>(node->priority), Rel(node->prev)});
}

// --- Tests ---

// AddHandler, DispatchMessage, Init, Deinit, the handler's and its search's destructors, AddUniqueSequence.
void TestNetDispatcher()
{
    World world;
    CompareRuns("net dispatcher: AddHandler, DispatchMessage, Init, Deinit", 1, [](bool original, int) {
        // handlers of id 5 in priority order (equal ones newest first), a couple elsewhere
        Record({Rel(AddHandler(original, 5, RecordingHandler, 3, 1, 0x80))});
        Record({Rel(AddHandler(original, 5, RecordingHandler, 1, 2, 0xFF))});
        Record({Rel(AddHandler(original, 5, RecordingHandler, 2, 3, 0x80))});
        Record({Rel(AddHandler(original, 5, RecordingHandler, 0, 4, 0x10))});
        Record({Rel(AddHandler(original, 5, RecordingHandler, 0xFFFFFFFF, 5, 0x05))});
        Record({Rel(AddHandler(original, 254, RecordingHandler, 7, 6, 0x80))});
        Record({Rel(AddHandler(original, 0, RecordingHandler, 0, 7, 0x80))});
        RecordList(&TheDispatcher()->List(5).m_node);

        auto* context = Make<MsgHandlerContext>();
        context->m_Msg = MakeBytes(16, 3);
        context->m_MsgLength = 16;
        context->m_App = TheApp();
        context->m_Conn = TheConn(0);
        struct Case {
            uint8_t id;
            uint32_t flags;
            std::vector<std::pair<uint32_t, uint32_t>> results; // handler tag -> its result
        };
        std::vector<Case> cases = {
            {5, 0, {}}, {5, 1, {}}, {5, 2, {}}, {5, 3, {}}, {5, 4, {}}, {9, 0, {}}, {254, 7, {}}, {254, 6, {}},
            {0, 0, {}},
        };
        for (uint32_t result : {0u, 2u, 3u, 4u, 5u, 6u, 0xFFFFFFFFu})
            cases.push_back({5, 0, {{1, result}}});
        cases.push_back({5, 0, {{2, 4}}});
        cases.push_back({5, 0, {{5, 2}}});
        cases.push_back({5, 1, {{1, 3}}});
        for (const Case& c : cases) {
            g_script.clear();
            for (auto& [tag, result] : c.results)
                Answers(0x4A00 + tag, {result});
            context->m_MsgId = c.id;
            context->m_PacketFlags = c.flags;
            context->m_data = reinterpret_cast<void*>(0x5A5A);
            Record({0xD15, c.id, c.flags, static_cast<uint32_t>(Dispatch(original, context)),
                    reinterpret_cast<uint32_t>(context->m_data)});
        }

        // Init deletes them all (node by node), Deinit too (item by item)
        if (original)
            Original<void(__thiscall*)(Dispatcher*)>(0x0045A640)(TheDispatcher());
        else
            TheDispatcher()->Init();
        RecordList(&TheDispatcher()->List(5).m_node);
        AddHandler(original, 5, RecordingHandler, 3, 8, 0x80);
        AddHandler(original, 5, RecordingHandler, 3, 9, 0x81);
        AddHandler(original, 77, RecordingHandler, 3, 10, 0x80);
        AddHandler(original, 254, RecordingHandler, 3, 11, 0x80);
        if (original)
            Original<void(__thiscall*)(Dispatcher*)>(0x0045A690)(TheDispatcher());
        else
            TheDispatcher()->Deinit();
        RecordList(&TheDispatcher()->List(5).m_node);
        RecordList(&TheDispatcher()->List(254).m_node);
    });

    CompareRuns("net dispatcher: the destructors", 1, [](bool original, int) {
        // (a null handler would crash in AddNode: not tested)
        MsgHandler* handler = AddHandler(original, 3, RecordingHandler, 1, 1, 0x80);
        MsgHandler* other = AddHandler(original, 3, RecordingHandler, 1, 2, 0x80);
        for (auto [h, flags] : {std::pair<MsgHandler*, uint32_t>{handler, 0}, {other, 1}}) {
            MsgHandler* result = original ? Original<MsgHandler*(__thiscall*)(MsgHandler*, uint32_t)>(0x0045A590)(h, flags)
                                          : h->Destroy(flags);
            Record({Rel(result)});
        }
        auto* search = Make<MsgHandlerSearch>();
        for (uint32_t flags : {0u, 1u, 2u, 3u}) {
            search->m_vtable = reinterpret_cast<void**>(0x1234);
            MsgHandlerSearch* result =
                original ? Original<MsgHandlerSearch*(__thiscall*)(MsgHandlerSearch*, uint32_t)>(0x0045A510)(search, flags)
                         : search->Destroy(flags);
            Record({Rel(result), reinterpret_cast<uint32_t>(search->m_vtable)});
            search->m_vtable = reinterpret_cast<void**>(0x1234);
            if (original)
                Original<void(__thiscall*)(MsgHandlerSearch*)>(0x0045A500)(search);
            else
                search->DestroyBody();
            Record({reinterpret_cast<uint32_t>(search->m_vtable)});
        }
    });

    CompareRuns("net Lst::Head::AddUniqueSequence", 1, [](bool original, int) {
        auto* head = Make<Lst::Head>();
        InitHead(&head->m_node);
        auto add = [&](int priority) {
            auto* node = Make<Mem::ListNode>();
            node->priority = priority;
            node->data = node;
            bool added = original ? Original<bool(__thiscall*)(Lst::Head*, Mem::ListNode*)>(0x0045AEB0)(head, node)
                                  : head->AddUniqueSequence(node);
            Record({0xADD, static_cast<uint32_t>(priority), added});
        };
        for (int priority : {7, 3, 9, 5, 1, 10, 7, 3, 10, -4, 8, 1})
            add(priority);
        RecordList(&head->m_node);
    });
}

// Bytes written one after the other.
struct Writer {
    char* p;
    Writer& Byte(uint32_t value)
    {
        *p++ = static_cast<char>(value);
        return *this;
    }
    Writer& Word(uint32_t value)
    {
        *reinterpret_cast<uint16_t*>(p) = static_cast<uint16_t>(value);
        p += 2;
        return *this;
    }
    Writer& Dword(uint32_t value)
    {
        *reinterpret_cast<uint32_t*>(p) = value;
        p += 4;
        return *this;
    }
    Writer& Bytes(uint32_t count, uint8_t seed)
    {
        for (uint32_t i = 0; i < count; ++i)
            *p++ = static_cast<char>(seed + i * 13);
        return *this;
    }
};

int DispatchMsgHandlers(bool original, Conn* conn, uint32_t flags)
{
    if (original)
        return Original<int(__thiscall*)(Dispatcher*, Conn*, uint32_t)>(0x0045A830)(TheDispatcher(), conn, flags);
    return TheDispatcher()->DispatchMsgHandlers(conn, flags);
}

// Every message of a received packet to its handlers, then the acks.
void TestNetDispatchMsgHandlers()
{
    World world;
    CompareRuns("net Dispatcher::DispatchMsgHandlers", 14, [](bool original, int i) {
        Dispatcher* d = TheDispatcher();
        d->AddHandler(0x20, RecordingHandler, 0xFFFFFFFF, reinterpret_cast<void*>(1), 0x80);
        d->AddHandler(0x21, RecordingHandler, 0xFFFFFFFF, reinterpret_cast<void*>(2), 0x80);
        d->AddHandler(kMsgSequenced, RecordingHandler, 0xFFFFFFFF, reinterpret_cast<void*>(3), 0x80);
        d->AddHandler(0x22, AckingHandler, 0xFFFFFFFF, nullptr, 0x80);
        d->AddHandler(0x24, RecordingHandler, 0, reinterpret_cast<void*>(4), 0x80);
        d->AddHandler(0x24, RecordingHandler, 0xFFFFFFFF, reinterpret_cast<void*>(5), 0x10);
        Conn* conn = TheConn(1);
        char* start = Field<char*>(conn, offsetof(Net::Conn, m_readBuffer));
        Writer w{start};
        w.Byte(0x20).Word(5).Bytes(5, 1);
        w.Byte(0x21).Bytes(5, 2);                       // no length: as long as the one before
        w.Byte(kMsgSequenced).Word(10).Byte(3).Dword(0x11223344).Byte(0x55).Bytes(4, 3);
        w.Byte(0x22).Word(6).Byte(0x30).Byte(0x30);     // the handler makes it 2 bytes long: then
        w.Byte(0x20).Word(1).Byte(0x7A);                // this is the next message
        w.Byte(0x24).Word(3).Bytes(3, 4);
        w.Byte(0x20).Word(0);
        if (i == 13)
            w.Byte(0x22).Word(2).Bytes(2, 5);           // a second ack
        Field<char*>(conn, offsetof(Net::Conn, m_readPtr)) = w.p;
        Answers(0x4B01, {2, 2});
        uint32_t flags = 0;
        switch (i) {
        case 1:
            Field<uint32_t>(conn, 0x64) = 4; // foreign
            break;
        case 2:
            flags = 2;
            break;
        case 3:
            Answers(0x4A01, {0});
            break;
        case 4:
            Answers(0x4A02, {2});
            break;
        case 5:
            Answers(0x4A03, {3});
            break;
        case 6:
            Answers(0x4A01, {4, 5});
            break;
        case 7:
            Answers(0x4B02, {0}); // no acks after all
            break;
        case 8:
            Field<char*>(conn, offsetof(Net::Conn, m_readPtr)) = start; // nothing received
            break;
        case 9:
            Answers(0x4A05, {0x77});
            break;
        case 10:
            Answers(0x4A01, {1, 2}); // the last message stops it
            break;
        case 11:
            Answers(0x4A05, {3});
            break;
        case 12:
            Field<uint32_t>(conn, 0x64) = 5; // local and foreign: flag 1 added to the ones passed
            flags = 6;
            break;
        default:
            break;
        }
        Answers(0x4B03, {0xABCD1234, 0x5678});
        Record({static_cast<uint32_t>(DispatchMsgHandlers(original, conn, flags))});
    });
}

QueuedMsg* MakeQueuedMsg(uint8_t id, uint32_t length, int refs, uint8_t seed)
{
    auto* msg = Make<QueuedMsg>();
    msg->m_id = id;
    msg->m_length = static_cast<uint16_t>(length);
    msg->m_data = MakeBytes(std::max<uint32_t>(length, 1), seed);
    msg->m_refCount = refs;
    return msg;
}
template <typename Link>
Link* MakeLink(Conn* conn, uint32_t queue, int priority)
{
    auto* link = Make<Link>();
    link->m_node.vtable = reinterpret_cast<void**>(g_nodeVtable);
    link->m_node.priority = priority;
    AddTail(&Field<Mem::ListNode>(conn, queue), &link->m_node, link);
    return link;
}
MsgNormalLink* Normal(Conn* conn, uint8_t id, uint32_t length, int refs, uint32_t sendTime)
{
    auto* link = MakeLink<MsgNormalLink>(conn, offsetof(Net::Conn, m_normalMsgs), 0x80);
    link->m_QMsg = MakeQueuedMsg(id, length, refs, static_cast<uint8_t>(length + id));
    link->m_SendTime = sendTime;
    return link;
}
MsgImpLink* Important(Conn* conn, uint8_t id, uint32_t length, uint32_t timestamp, uint32_t sendTime)
{
    auto* link = MakeLink<MsgImpLink>(conn, offsetof(Net::Conn, m_importantMsgs), 0x80);
    link->m_QMsg = MakeQueuedMsg(id, length, 1, static_cast<uint8_t>(length + id));
    link->m_Timestamp = timestamp;
    link->m_SendTime = sendTime;
    link->m_Packetstamp = 0x5151;
    link->m_NumResends = 3;
    return link;
}
MsgSeqLink* Sequenced(Conn* conn, uint8_t id, uint32_t length, uint32_t timestamp, uint32_t sendTime)
{
    auto* link = MakeLink<MsgSeqLink>(conn, offsetof(Net::Conn, m_sequencedMsgs), 0x80);
    auto* msg = Make<SeqQueuedMsg>();
    msg->m_id = id;
    msg->m_length = static_cast<uint16_t>(length);
    msg->m_data = MakeBytes(std::max<uint32_t>(length, 1), static_cast<uint8_t>(id * 3));
    msg->m_refCount = 1;
    link->m_QMsg = msg;
    link->m_Timestamp = timestamp;
    link->m_SendTime = sendTime;
    link->m_GroupId = static_cast<uint8_t>(0x60 + id);
    link->m_SequenceId = 0x01020304u * id;
    link->m_Packetstamp = 0x5252;
    link->m_NumResends = 1;
    return link;
}

bool Bundle(bool original, Conn* conn, int queue, bool resendsOnly)
{
    if (original)
        return Original<bool(__thiscall*)(App*, Conn*, int, bool)>(0x004598C0)(TheApp(), conn, queue, resendsOnly);
    return TheApp()->BundleMessages(conn, queue, resendsOnly);
}

// App::BundleMessages: each queue, due and not due messages, resends, full packets, the bandwidth limit.
void TestNetBundleMessages()
{
    World world;
    CompareRuns("net App::BundleMessages", 26, [](bool original, int i) {
        Conn* conn = TheConn(2);
        char*& writePtr = Field<char*>(conn, offsetof(Net::Conn, m_writePtr));
        char* start = Field<char*>(conn, offsetof(Net::Conn, m_writeBuffer));
        int queue = i < 8 ? kQueueNormal : i < 17 ? kQueueImportant : i < 25 ? kQueueSequenced : 3;
        bool resendsOnly = false;
        Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 2; // a resend threshold of 1000
        Field<uint32_t>(conn, offsetof(Net::Conn, m_timestampOut)) = 0x00ABCDEF;
        switch (i) {
        // normal queue
        case 0:
            Normal(conn, 0x20, 10, 1, 0);
            Normal(conn, 0x21, 7, 2, 39999); // no length field
            Normal(conn, 0x22, 0, 1, 40000);
            break;
        case 1:
            Normal(conn, 0x20, 10, 1, 40001); // not yet
            Normal(conn, 0x22, 3, 3, 0);
            break;
        case 2:
            Normal(conn, 0x20, 10, 1, 0);
            Normal(conn, 0x22, 3, 1, 0);
            Normal(conn, 0x23, 3, 1, 0);
            Answers(0x00458E60, {0, 1}); // the bandwidth runs out
            break;
        case 3:
            writePtr = start + 0x10;
            Normal(conn, 0x20, 0x505, 1, 0); // doesn't fit
            Normal(conn, 0x22, 3, 1, 0);
            break;
        case 4:
            Normal(conn, 0x20, 0x50E, 1, 0); // exactly fits
            Normal(conn, 0x21, 0, 1, 0);
            break;
        case 5:
            writePtr = start + 1;
            Normal(conn, 0x20, 0x50E, 1, 0); // one byte too many
            Normal(conn, 0x21, 0x50F, 1, 0); // (no length field: one byte of header, fits)
            break;
        case 6:
            Normal(conn, 0x20, 0xFFFE, 1, 0); // the size wraps around to 1: "fits"
            Field<char*>(conn, offsetof(Net::Conn, m_writeBuffer)) = writePtr = MakeBytes(0x10100, 9); // room for it
            break;
        case 7:
            break;
        // important queue
        case 8:
            Important(conn, 0x20, 5, 0, 0);
            Important(conn, 0x21, 6, 38999, 0); // overdue: resent
            Important(conn, 0x22, 7, 39000, 0); // not yet overdue
            Important(conn, 0x23, 8, 39001, 0);
            break;
        case 9:
            resendsOnly = true;
            Important(conn, 0x20, 5, 0, 0);
            Important(conn, 0x21, 6, 38999, 0);
            break;
        case 10:
            Important(conn, 0x20, 5, 0, 40001);
            Important(conn, 0x21, 6, 0, 40000);
            break;
        case 11:
            Important(conn, 0x20, 5, 0, 0);
            Important(conn, 0x21, 6, 0, 0);
            Answers(0x00458E60, {0, 1});
            break;
        case 12:
            writePtr = start + 0x100;
            Important(conn, 0x20, 0x410, 0, 0); // doesn't fit
            Important(conn, 0x21, 0x10, 0, 0);
            break;
        case 13:
            Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 0;
            Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_averageLatency)) = 300; // threshold 600 (unless local)
            Important(conn, 0x20, 5, 39400, 0);
            Important(conn, 0x21, 6, 39399, 0);
            break;
        case 14:
            Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 0;
            Field<uint32_t>(conn, 0x64) = 1; // local: threshold 0
            Important(conn, 0x20, 5, 40000, 0);
            Important(conn, 0x21, 6, 39999, 0);
            break;
        case 15:
            Important(conn, 0x21, 0x30, 0, 0); // no length field
            Important(conn, 0x22, 0, 30000, 0);
            break;
        case 16:
            Important(conn, 0x22, 0xFFFE, 1, 0); // wraps around
            Field<char*>(conn, offsetof(Net::Conn, m_writeBuffer)) = writePtr = MakeBytes(0x10100, 9); // room for it
            break;
        // sequenced queue
        case 17:
            Sequenced(conn, 0x20, 5, 0, 0);
            Sequenced(conn, 0x21, 6, 38999, 0);
            Sequenced(conn, 0x22, 7, 39000, 0);
            Sequenced(conn, 0x23, 0, 0, 0);
            break;
        case 18:
            resendsOnly = true;
            Sequenced(conn, 0x20, 5, 0, 0);
            Sequenced(conn, 0x21, 6, 38999, 0);
            break;
        case 19:
            Sequenced(conn, 0x20, 5, 0, 40001);
            Sequenced(conn, 0x21, 6, 0, 40000);
            break;
        case 20:
            Sequenced(conn, 0x20, 5, 0, 0);
            Sequenced(conn, 0x21, 6, 0, 0);
            Answers(0x00458E60, {0, 1});
            break;
        case 21:
            writePtr = start + 1;
            Sequenced(conn, 0x20, 0x508, 0, 0); // one too many
            Sequenced(conn, 0x21, 0x507, 0, 0); // fits exactly
            break;
        case 22:
            Sequenced(conn, 0x20, 0xFFF8, 0, 0); // the size wraps around to 1
            Field<char*>(conn, offsetof(Net::Conn, m_writeBuffer)) = writePtr = MakeBytes(0x10100, 9); // room for it
            break;
        case 23:
            Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 0;
            Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_averageLatency)) = 250;
            Sequenced(conn, 0x20, 5, 39500, 0);
            Sequenced(conn, 0x21, 6, 39499, 0);
            break;
        case 24:
            Sequenced(conn, 0x24, 0x20, 0, 0); // (the flags table plays no part here)
            break;
        default:
            Normal(conn, 0x20, 5, 1, 0); // an unknown queue: nothing
            break;
        }
        Record({Bundle(original, conn, queue, resendsOnly), Rel(writePtr)});
        if (i == 7 || i == 25) {
            Record({Bundle(original, conn, kQueueImportant, true), Bundle(original, conn, kQueueSequenced, false)});
            Record({Bundle(original, conn, -1, false)});
        }
    });
}

int CallHandler(bool original, uint32_t address, MsgHandlerCode ours, MsgHandlerContext* context)
{
    return original ? Original<MsgHandlerCode>(address)(context) : ours(context);
}
MsgHandlerContext* MakeContext(Conn* conn, uint8_t id, char* msg, uint32_t length, uint32_t flags)
{
    auto* context = Make<MsgHandlerContext>();
    context->m_Msg = msg;
    context->m_MsgId = id;
    context->m_MsgLength = length;
    context->m_App = TheApp();
    context->m_Conn = conn;
    context->m_PacketFlags = flags;
    context->m_NumSequencesToAck = 2;
    context->m_SequencesToAck[0] = 0x11;
    context->m_SequencesToAck[1] = 0x22;
    return context;
}
char* Dwords(std::initializer_list<uint32_t> values)
{
    auto* p = static_cast<uint32_t*>(Bump(4 * static_cast<uint32_t>(values.size())));
    std::copy(values.begin(), values.end(), p);
    return reinterpret_cast<char*>(p);
}
void Handle(bool original, uint32_t address, MsgHandlerCode ours, MsgHandlerContext* context)
{
    Record({0x4A4A, static_cast<uint32_t>(CallHandler(original, address, ours, context))});
}

// The latency handlers, the client's and server's small handlers.
void TestNetSmallHandlers()
{
    World world;
    CompareRuns("net App::HandleLatencyTest", 2, [](bool original, int i) {
        Conn* conn = TheConn(i);
        Handle(original, 0x0045AD60, App::HandleLatencyTest,
               MakeContext(conn, kMsgLatencyTest, Dwords({0x11223344u + i}), 4, 0));
    });
    CompareRuns("net App::HandleLatencyResponse", 7, [](bool original, int i) {
        Conn* conn = TheConn(0);
        Field<uint32_t>(conn, 0x64) = 2;
        Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_sendTime)) = 39000;
        Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_receiveTime)) = 1234;
        uint32_t sent = 39000;
        if (i == 1)
            sent = 38999; // not ours
        if (i == 2)
            Field<uint32_t>(conn, 0x64) = 3; // local
        if (i == 3)
            sent = 30000; // longer than the longest latency
        if (i == 4)
            sent = 40500; // from the future: wraps around
        if (i == 5)
            sent = 38000; // exactly the longest
        if (i == 6)
            sent = 38001;
        if (i >= 3)
            Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_sendTime)) = sent;
        Handle(original, 0x0045AB60, App::HandleLatencyResponse,
               MakeContext(conn, kMsgLatencyResponse, Dwords({sent}), 4, 0));
    });
    CompareRuns("net Client::HandleConnectionAccepted / Refused", 2, [](bool original, int i) {
        MsgHandlerContext* context = MakeContext(TheConn(1), kMsgConnectionAccepted, Dwords({1}), 0, 0);
        if (i == 0)
            Handle(original, 0x0045ABC0, Client::HandleConnectionAccepted, context);
        else
            Handle(original, 0x0045ABD0, Client::HandleConnectionRefused, context);
    });
    struct Stamp {
        uint32_t newest, stamp, flags;
    };
    static const Stamp stamps[] = {
        {0, 100, 0},    {100, 100, 0},  {100, 101, 0},  {100, 104, 0},  {100, 98, 0},   {6000, 500, 0},
        {6000, 999, 0}, {6000, 1000, 0}, {100, 5101, 0}, {100, 5100, 0}, {100, 5102, 0}, {100, 101, 1},
        {100, 0, 2},    {0xFFFF, 0, 0}, {0, 0, 0},      {0x12345, 0x2345, 0},
    };
    CompareRuns("net Client::HandleTimestamp", static_cast<int>(std::size(stamps)), [](bool original, int i) {
        Conn* conn = TheConn(2);
        Field<uint32_t>(conn, offsetof(Net::Conn, m_lastTimestampIn)) = stamps[i].newest;
        Field<int>(TheApp(), offsetof(Net::App, m_lostPackets)) = 10;
        Field<int>(TheApp(), offsetof(Net::App, m_latePackets)) = 20;
        Field<int>(TheApp(), offsetof(Net::App, m_dupePackets)) = 30;
        Handle(original, 0x0045AA70, Client::HandleTimestamp,
               MakeContext(conn, kMsgTimestamp, Dwords({0xAB0000 | stamps[i].stamp}), 7, stamps[i].flags));
    });
    CompareRuns("net Server::HandleTimestamp", 3, [](bool original, int i) {
        Handle(original, 0x0045AA30, Server::HandleTimestamp,
               MakeContext(TheConn(i), kMsgTimestamp, Dwords({0xCD001234u + i}), 9, i == 1 ? 1 : i));
    });
    CompareRuns("net Server::HandleConnectionRequest", 6, [](bool original, int i) {
        Conn* conn = TheConn(1);
        uint32_t flags = 0;
        switch (i) {
        case 0:
            flags = 1; // foreign, not taken
            break;
        case 1:
            flags = 3;
            Field<uint32_t>(TheApp(), offsetof(Net::App, m_flags)) = kAppAcceptForeign;
            break;
        case 2:
            Field<uint32_t>(conn, 0x64) = 1; // local
            break;
        case 3:
            Field<uint32_t>(conn, 0x64) = 6; // remote, not foreign
            break;
        case 4:
            flags = 2;
            Field<uint32_t>(conn, 0x64) = 7;
            break;
        default:
            flags = 1;
            Field<uint32_t>(TheApp(), offsetof(Net::App, m_flags)) = 0xFF;
            Field<uint32_t>(conn, 0x64) = 1;
            break;
        }
        Handle(original, 0x0045ADB0, Server::HandleConnectionRequest,
               MakeContext(conn, kMsgConnectionRequest, Dwords({5}), 0, flags));
    });
    CompareRuns("net Server::HandleDisconnRequest", 3, [](bool original, int i) {
        Conn* conn = TheConn(0);
        Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = i == 1 ? 0x9 : i == 2 ? 0x7 : 0;
        Handle(original, 0x0045AE50, Server::HandleDisconnRequest,
               MakeContext(conn, kMsgDisconnRequest, Dwords({5}), 0, 0));
    });
}

// Sequenced messages buffered, acks.
void TestNetSequenceHandlers()
{
    World world;
    CompareRuns("net App::HandleSequencedMessages", 8, [](bool original, int i) {
        Conn* conn = TheConn(1);
        uint8_t group = i == 4 ? 0xFF : 3;
        Field<uint32_t>(conn, offsetof(Net::Conn, m_sequenceNumbers) + group * 4) = 10;
        uint32_t sequence = i == 1 ? 9 : i == 2 ? 10 : 12;
        auto* buffer = &Field<Mem::ListNode>(conn, offsetof(Net::Conn, m_receivedQueues) + group * 0x14);
        for (int priority : {5, 11, 14}) {
            auto* node = Make<Mem::ListNode>();
            node->vtable = reinterpret_cast<void**>(g_nodeVtable);
            node->priority = i == 3 && priority == 11 ? 12 : priority; // case 3: 12 is there already
            AddTail(buffer, node, node);
        }
        uint32_t dataLength = i == 4 ? 0 : 9;
        auto* msg = Make<uint8_t>(6 + dataLength + 4);
        Writer w{reinterpret_cast<char*>(msg)};
        w.Byte(group).Dword(sequence).Byte(0x44).Bytes(dataLength, 7);
        if (i == 5)
            GameVariable<uint8_t>(0x007CC979) = 0;
        if (i == 6)
            g_failAllocation = 0; // the message
        // (case 7: only the length's low 16 bits count)
        MsgHandlerContext* context = MakeContext(conn, kMsgSequenced, reinterpret_cast<char*>(msg),
                                                 (i == 7 ? 0x30000 : 0) + 6 + dataLength, 0);
        Handle(original, 0x0045AF10, App::HandleSequencedMessages, context);
        RecordList(buffer);
    });
    CompareRuns("net App::HandleAck", 9, [](bool original, int i) {
        Conn* conn = TheConn(2);
        Field<uint32_t>(conn, 0x64) = 2;
        Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 1;
        Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_averageLatency)) = 150;
        uint32_t lastTime = 39800;
        switch (i) {
        case 2:
            Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_averageLatency)) = 300;
            break;
        case 3:
            Field<uint32_t>(conn, 0x64) = 3;
            Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_averageLatency)) = 300;
            break;
        case 4:
            Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 0x1E;
            break;
        case 5:
            Field<uint32_t>(conn, 0x64) = 5;
            break;
        case 6:
            lastTime = 30000;
            break;
        case 8:
            Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_averageLatency)) = 200;
            break;
        default:
            break;
        }
        uint32_t ack = i == 7 ? 0x6000 : 0x5151;
        auto important = [&](uint32_t stamp, uint32_t time, int refs) {
            auto* link = MakeLink<MsgImpLink>(conn, offsetof(Net::Conn, m_importantMsgs), 0x80);
            link->m_QMsg = MakeQueuedMsg(0x30, 4, refs, 1);
            link->m_Packetstamp = stamp;
            link->m_Timestamp = time;
        };
        important(0x5151, 39000, 1);
        important(0x25151, 39500, 2);
        important(0x5151, 0, 1);
        important(0x5152, 39600, 1);
        auto sequenced = [&](uint32_t stamp, uint32_t time, int refs) {
            MsgSeqLink* link = Sequenced(conn, 0x31, 3, time, 0);
            link->m_Packetstamp = stamp;
            link->m_QMsg->m_refCount = refs;
        };
        sequenced(0x5150, 39700, 1);
        sequenced(0x5151, 39900, 3);
        sequenced(0x5151, 0, 1);
        sequenced(0xF5151, lastTime, 1); // (the last one acked gives the round trip)
        Handle(original, 0x0045B0B0, App::HandleAck,
               MakeContext(conn, kMsgAck, Dwords({0xEE000000 | ack}), 0x77, i == 0 ? 3 : i == 8 ? 2 : 0));
        RecordList(&Field<Mem::ListNode>(conn, offsetof(Net::Conn, m_importantMsgs)));
        RecordList(&Field<Mem::ListNode>(conn, offsetof(Net::Conn, m_sequencedMsgs)));
    });
}

// Compressed messages and streams, dispatched to a handler when whole.
void TestNetStreamHandlers()
{
    World world;
    CompareRuns("net App::HandleCompressedMessages", 9, [](bool original, int i) {
        TheDispatcher()->AddHandler(0x33, RecordingHandler, 0xFFFFFFFF, reinterpret_cast<void*>(6), 0x80);
        uint8_t method = i < 3 || i >= 6 ? 1 : i < 5 ? 2 : 3;
        auto* msg = Make<char>(0x20);
        Writer{msg}.Byte(method).Word(7).Bytes(12, 0x21);
        Answers(0x4C22, {0x33, 4, 0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5});
        Answers(0x4C21, {i == 2 ? 6u : 7u});
        Answers(0x14F1, {i == 4 ? 8u : 7u});
        if (i == 1)
            Answers(0x4C20, {1}); // didn't work
        if (i == 6)
            Answers(0x4A06, {3});
        if (i == 7)
            GameVariable<uint8_t>(0x007CC979) = 0;
        if (i == 8)
            Answers(0x4C22, {0x34, 4, 0}); // no handler for it
        Handle(original, 0x0045ABF0, App::HandleCompressedMessages,
               MakeContext(TheConn(0), kMsgCompressed, msg, 15, 2));
    });
    CompareRuns("net App::HandleStreamMessages", 14, [](bool original, int i) {
        TheDispatcher()->AddHandler(0x34, RecordingHandler, 0xFFFFFFFF, reinterpret_cast<void*>(7), 0x80);
        Conn* conn = TheConn(1);
        auto* streams = &Field<Mem::ListNode>(conn, offsetof(Net::Conn, m_receivedStreams));
        // streams 0x51 and 0x52 being received: 0x30 bytes, 0x10 so far
        for (uint32_t id : {0x51u, 0x52u}) {
            auto* desc = Make<StreamDesc>();
            desc->m_size = 0x30;
            desc->m_streamId = id;
            desc->m_msgId = 0x34;
            desc->m_data = reinterpret_cast<uint8_t*>(MakeBytes(0x60, static_cast<uint8_t>(id))); // (room for case 7's overrun)
            desc->m_next = desc->m_data + 0x10;
            auto* link = Make<StreamLink>();
            link->m_node.vtable = reinterpret_cast<void**>(g_nodeVtable);
            link->m_Desc = desc;
            AddTail(streams, &link->m_node, link);
        }
        auto* msg = Make<char>(0x200);
        uint8_t id = kMsgStreamStart;
        uint32_t length = 0;
        auto start = [&](uint32_t size, uint32_t streamId) {
            Writer w{msg};
            for (char c : std::string_view("stream name"))
                w.Byte(static_cast<uint8_t>(c));
            w.Byte(0);
            w.p = msg + 0x20;
            w.Dword(size).Dword(streamId).Dword(0xC4EC4EC4).Byte(0x34).Bytes(std::min<uint32_t>(size, 0x100), 0x61);
            length = 0x2D + std::min<uint32_t>(size, 0x100);
        };
        auto piece = [&](uint32_t streamId, uint32_t size) {
            Writer{msg}.Dword(streamId).Bytes(size, 0x71);
            id = kMsgStreamData;
            length = 4 + size;
        };
        switch (i) {
        case 0:
            start(0x80, 0x60); // all in one
            break;
        case 1:
            start(0x300, 0x60);
            break;
        case 2:
            start(0x30, 0x52); // started already
            break;
        case 3:
            piece(0x51, 0x10);
            break;
        case 4:
        case 5:
        case 8:
        case 9: {
            piece(0x52, 0x20); // the last piece
            // the checksum of the whole (case 5: wrong)
            auto* link = reinterpret_cast<StreamLink*>(streams->prev->data);
            char whole[0x30];
            std::memcpy(whole, link->m_Desc->m_data, 0x10);
            std::memcpy(whole + 0x10, msg + 4, 0x20);
            link->m_Desc->m_checksum = GameFunction<uint32_t(__cdecl*)(const void*, int)>(0x00401AA0)(whole, 0x30) +
                                       (i == 5 ? 1 : 0);
            if (i == 8)
                Answers(0x4A07, {5}); // kept
            if (i == 9)
                Answers(0x4A07, {3});
            break;
        }
        case 6:
            piece(0x53, 0x20); // unknown
            break;
        case 7:
            piece(0x51, 0x30); // more than is left
            break;
        case 10:
            id = 0x10;
            break;
        case 11:
            start(0x100, 0x61);
            break;
        case 12:
            start(0x101, 0x61);
            break;
        default:
            start(0x40, 0x62);
            Answers(0x4A07, {5});
            GameVariable<uint8_t>(0x007CC979) = 0;
            break;
        }
        Handle(original, 0x0045B2F0, App::HandleStreamMessages, MakeContext(conn, id, msg, length, 0));
        RecordList(streams);
    });
}

void SetCodeMap()
{
    g_codeMap = {
        {Code(&Client::HandleConnectionAccepted), 0x0045ABC0},
        {Code(&Client::HandleConnectionRefused), 0x0045ABD0},
        {Code(&Client::HandleTimestamp), 0x0045AA70},
        {Code(&Server::HandleConnectionRequest), 0x0045ADB0},
        {Code(&Server::HandleTimestamp), 0x0045AA30},
        {Code(&Server::HandleDisconnRequest), 0x0045AE50},
        {Code(&App::SendNetworkData), 0x004594A0},
    };
}

void __cdecl ForeignPacketHandler(char* data, int size, sockaddr* from)
{
    auto* address = reinterpret_cast<sockaddr_in*>(from);
    Record({0xF0E, Rel(data), static_cast<uint32_t>(size), address->sin_family, address->sin_port,
            address->sin_addr.s_addr});
}

// Construction, destruction, Init, ConnectToServer, the messages to the server.
void TestNetClientServerSetUp()
{
    World world;
    SetCodeMap();
    CompareRuns("net Client/Server::Construct, Destroy, Deinit", 6, [](bool original, int i) {
        int params = 0x9A9A0000 + i;
        if (i == 1 || i == 3)
            g_failAllocation = 0;
        if (i < 2) {
            void* result = original ? Original<void*(__thiscall*)(void*, int)>(0x0045A250)(g_app, params)
                                    : reinterpret_cast<Client*>(g_app)->Construct(params);
            Record({Rel(result)});
        } else if (i < 4) {
            void* result = original ? Original<void*(__thiscall*)(void*, int)>(0x0045B830)(g_app, params)
                                    : reinterpret_cast<Server*>(g_app)->Construct(params);
            Record({Rel(result)});
        } else {
            for (uint32_t flags : {0u, 1u, 3u}) {
                void* result = original ? Original<void*(__thiscall*)(void*, uint32_t)>(0x0045B8E0)(g_app, flags)
                                        : reinterpret_cast<Client*>(g_app)->Destroy(flags);
                Record({Rel(result)});
            }
            if (original) {
                Original<void(__thiscall*)(void*)>(0x0045B910)(g_app);
                Original<void(__thiscall*)(void*)>(0x0045B820)(g_app);
            } else {
                reinterpret_cast<Client*>(g_app)->DestroyBody();
                reinterpret_cast<Client*>(g_app)->Deinit();
            }
        }
    });
    CompareRuns("net Client/Server::Init", 4, [](bool original, int i) {
        Field<uint8_t>(TheApp(), offsetof(Net::App, m_connected)) = 1;
        Field<uint32_t>(TheApp(), offsetof(Net::App, m_maxConnections)) = 0x1458;
        if (i % 2 == 0)
            Answers(0x7110, {0}); // the App's Init fails
        bool result;
        if (i < 2)
            result = original ? Original<bool(__thiscall*)(void*)>(0x00459EC0)(g_app) : reinterpret_cast<Client*>(g_app)->Init();
        else
            result = original ? Original<bool(__thiscall*)(void*)>(0x0045B7A0)(g_app) : reinterpret_cast<Server*>(g_app)->Init();
        Record({result});
        for (int id : {3, 4, 5, 6, 9, 0xB})
            RecordList(&TheDispatcher()->List(id).m_node);
    });
    CompareRuns("net Client::ConnectToServer", 5, [](bool original, int i) {
        uint32_t ip = i == 1 ? 0 : 0x0100007F + i;
        Field<uint8_t>(TheApp(), offsetof(Net::App, m_connected)) = i == 3 ? 1 : 0x55;
        std::memset(&Field<uint8_t>(TheApp(), offsetof(Net::Client, m_serverAddress)), 0xEE, 16);
        switch (i) {
        case 2:
            Answers(0xC044, {0xFFFFFFFF, 0xFFFFFFFF, 0});
            Answers(0x3A6E, {WSAEWOULDBLOCK, WSAEWOULDBLOCK});
            break;
        case 3:
            Answers(0xC044, {0xFFFFFFFF});
            Answers(0x3A6E, {WSAECONNREFUSED});
            break;
        case 4:
            Answers(0xC044, {0xFFFFFFFF, 0xFFFFFFFF});
            Answers(0x3A6E, {WSAEWOULDBLOCK, WSAEINVAL});
            break;
        default:
            break;
        }
        uint32_t port = 0xABCD0000 | (5150 + i); // (only the low 16 bits are the port)
        bool result = original ? Original<bool(__thiscall*)(void*, uint32_t, uint32_t)>(0x00459F40)(g_app, ip, port)
                               : reinterpret_cast<Client*>(g_app)->ConnectToServer(ip, static_cast<uint16_t>(port));
        Record({result});
    });
    CompareRuns("net Client::EnqueueMessageToServer, StreamMessageToServer", 3, [](bool original, int i) {
        auto* connections = &Field<Mem::ListNode>(TheApp(), offsetof(Net::App, m_connections));
        if (i > 0) {
            auto* node = Make<Mem::ListNode>();
            AddTail(connections, node, i == 1 ? TheConn(1) : nullptr);
            AddTail(connections, Make<Mem::ListNode>(), TheConn(2));
        }
        auto* desc = Make<MsgDesc>();
        desc->m_id = 0x40;
        desc->m_length = 4;
        desc->m_data = Dwords({0x600DF00D});
        if (original) {
            Original<void(__thiscall*)(void*, MsgDesc*)>(0x00459E50)(g_app, desc);
            Original<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
                0x00459DC0)(g_app, 0x41, 0x1234, 3, 4, 5, 1, 0);
        } else {
            reinterpret_cast<Client*>(g_app)->EnqueueMessageToServer(desc);
            reinterpret_cast<Client*>(g_app)->StreamMessageToServer(0x41, 0x1234, reinterpret_cast<void*>(3),
                                                                    reinterpret_cast<const char*>(4), 5, true, false);
        }
    });
}

// ReceiveData: packets into connections (stood-in sockets).
void TestNetReceiveData()
{
    World world;
    for (int server = 0; server < 2; ++server) {
        CompareRuns(server ? "net Server::ReceiveData" : "net Client::ReceiveData", 10, [server](bool original, int i) {
            Conn* conn = TheConn(0);
            auto conn0 = reinterpret_cast<uint32_t>(TheConn(0));
            auto conn1 = reinterpret_cast<uint32_t>(TheConn(1));
            auto conn2 = reinterpret_cast<uint32_t>(TheConn(2));
            auto& server_address = Field<sockaddr_in>(TheApp(), offsetof(Net::Client, m_serverAddress));
            server_address.sin_port = 0x1F2E;
            server_address.sin_addr.s_addr = 0x0302A8C0;
            Field<uint32_t>(conn, offsetof(Net::Conn, m_lastCommTime)) = 500;
            Field<void*>(TheApp(), offsetof(Net::App, m_foreignPacketHandler)) = reinterpret_cast<void*>(ForeignPacketHandler);
            switch (i) {
            case 0:
                Field<uint32_t>(TheApp(), offsetof(Net::App, m_flags)) = 8; // no socket
                Answers(0xF40F, {20});
                break;
            case 1:
                Answers(0xF40F, {20, 0});
                Answers(0x78B0, {conn0, conn0});
                Answers(0x713E, {777, 400});
                break;
            case 2:
                Field<uint32_t>(TheApp(), offsetof(Net::App, m_flags)) = kAppSecure;
                Answers(0xF40F, {20, 30, 1});
                Answers(0x78B0, {conn0, conn0, conn0});
                Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = i == 2 && server ? 4 : 0; // (the server: not new)
                break;
            case 3:
                Answers(0xF40F, {20, 30});
                Answers(0x78B0, {conn0, conn0});
                Answers(0x6CC0, {0, 0});
                Answers(0x713E, {600, 0});
                break;
            case 4:
                Field<char*>(conn, offsetof(Net::Conn, m_readPtr)) += 0xA20; // nearly full
                Answers(0xF40F, {8, 7});
                Answers(0x78B0, {conn0, conn0});
                break;
            case 5:
                Answers(0xF40F, {20, 30, 40});
                Answers(0x97C0, {0, conn1, conn2});
                Answers(0x6CC0, {1, 0});
                Answers(0xF40A, {0x1111, 0x2222, 0x3333});
                Answers(0xF40B, {0x0A0B0C0D, 0x01020304, 0x05060708});
                break;
            case 6:
                Field<uint8_t>(TheApp(), offsetof(Net::App, m_connected)) = 1; // recv; unknown senders aren't taken
                Answers(0xF40F, {20, 30});
                Answers(0x78B0, {0, conn0});
                break;
            case 7:
                Answers(0xF40F, {0xFFFFFFFF});
                Answers(0x3A6E, {WSAECONNRESET});
                break;
            case 8:
                Field<uint32_t>(TheApp(), offsetof(Net::App, m_flags)) = kAppSecure;
                Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 4; // not new
                Answers(0xF40F, {20, 0});
                Answers(0x78B0, {conn0, conn0});
                Field<void*>(TheApp(), offsetof(Net::App, m_foreignPacketHandler)) = nullptr;
                Answers(0x6CC0, {0});
                break;
            default:
                Answers(0xF40F, {20, 30});
                Answers(0x97C0, {conn1, conn2});
                Answers(0x6CC0, {0, 0});
                Field<void*>(TheApp(), offsetof(Net::App, m_foreignPacketHandler)) = nullptr;
                break;
            }
            if (server) {
                if (original)
                    Original<void(__thiscall*)(void*)>(0x0045BBD0)(g_app);
                else
                    reinterpret_cast<Server*>(g_app)->ReceiveData();
            } else {
                if (original)
                    Original<void(__thiscall*)(void*)>(0x0045A300)(g_app);
                else
                    reinterpret_cast<Client*>(g_app)->ReceiveData();
            }
        });
    }
}

// SendData: timestamps, latency tests, the queues into packets, sending.
void TestNetSendData()
{
    World world;
    for (int server = 0; server < 2; ++server) {
        CompareRuns(server ? "net Server::SendData" : "net Client::SendData", 19, [server](bool original, int i) {
            Conn* conn = TheConn(1);
            Field<uint32_t>(conn, 0x64) = 2; // remote
            Field<uint32_t>(TheApp(), offsetof(Net::App, m_flags)) = kAppMeasureLatency;
            Field<uint32_t>(conn, offsetof(Net::Conn, m_timestampOut)) = 0x00017FFF;
            Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_sendTime)) = 39000;
            Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_receiveTime)) = 39000;
            Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_averageLatency)) = 120;
            Field<int>(conn, 0x3D1C) = 0x1000; // bandwidth
            Field<int>(TheApp(), offsetof(Net::App, m_totalBytesOut)) = 1000;
            Normal(conn, 0x20, 10, 1, 0);
            Important(conn, 0x22, 6, 0, 0);
            Answers(0x00457AC0, {1, 0});
            switch (i) {
            case 0:
                Answers(0x00456B00, {0}); // nothing this frame
                break;
            case 1:
                Answers(0x00457AC0, {0});
                break;
            case 2:
                Answers(0x00458E60, {1});
                break;
            case 3:
                Answers(0x00457970, {1}); // a timestamp due
                Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_sendTime)) = 0; // and the first latency test
                break;
            case 4:
                Field<uint8_t>(TheApp(), offsetof(Net::App, m_connected)) = 1;
                break;
            case 5:
                Answers(0x6E20, {0}); // sending fails
                Answers(0x6F10, {0});
                break;
            case 6:
                Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_sendTime)) = 37999; // overdue, unanswered
                Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_receiveTime)) = 37000;
                Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_averageLatency)) = 1950;
                break;
            case 7:
                Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_sendTime)) = 38000; // exactly not overdue
                Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_receiveTime)) = 1;
                break;
            case 8:
                Field<uint32_t>(conn, 0x64) = 3; // local
                Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_sendTime)) = 30000;
                Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_receiveTime)) = 1;
                break;
            case 9:
                Field<uint32_t>(conn, 0x64) = 6; // foreign
                Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_sendTime)) = 30000;
                break;
            case 10:
                Field<uint32_t>(TheApp(), offsetof(Net::App, m_flags)) = 0;
                Field<uint32_t>(conn, offsetof(Net::Conn, m_latencyTest.m_sendTime)) = 30000;
                break;
            case 11: {
                Field<char*>(conn, offsetof(Net::Conn, m_writePtr)) += 0x500; // a full packet: the loop ends
                Answers(0x00457AC0, {1, 1, 1});
                break;
            }
            case 12:
                Field<uint32_t>(conn, 0x64) = 0; // not remote: nothing goes out
                Answers(0x00457AC0, {1, 1, 0});
                break;
            case 13:
                Answers(0x6540, {3, 5}); // more waiting for acks
                Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 1;
                break;
            case 14:
                Answers(0x6540, {3, 5});
                Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 1;
                Field<int>(conn, 0x3D1C) = 0x700;
                break;
            case 15:
                Answers(0x6540, {3, 5});
                Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 0;
                break;
            case 16:
                Answers(0x00457AC0, {0});
                Answers(0x00458E60, {1}); // limited at the end, nothing waiting for acks
                Answers(0x6540, {0});
                break;
            case 17:
                Answers(0x00457AC0, {0});
                Answers(0x00458E60, {1});
                Answers(0x6540, {1});
                break;
            default:
                // overdue resends in both queues: the order they go into the packet
                Important(conn, 0x23, 9, 30000, 0);
                Sequenced(conn, 0x24, 5, 30000, 0);
                Field<uint32_t>(conn, offsetof(Net::Conn, m_status)) = 2;
                break;
            }
            if (server) {
                if (original)
                    Original<void(__thiscall*)(void*, Conn*)>(0x0045B920)(g_app, conn);
                else
                    reinterpret_cast<Server*>(g_app)->SendData(conn);
            } else {
                if (original)
                    Original<void(__thiscall*)(void*, Conn*)>(0x00459FD0)(g_app, conn);
                else
                    reinterpret_cast<Client*>(g_app)->SendData(conn);
            }
        });
    }
}

} // namespace

SELFTEST(Early, TestNetDispatcher, CleanUpNetTest);
SELFTEST(Early, TestNetDispatchMsgHandlers, CleanUpNetTest);
SELFTEST(Early, TestNetBundleMessages, CleanUpNetTest);
SELFTEST(Early, TestNetSmallHandlers, CleanUpNetTest);
SELFTEST(Early, TestNetSequenceHandlers, CleanUpNetTest);
SELFTEST(Early, TestNetStreamHandlers, CleanUpNetTest);
SELFTEST(Early, TestNetClientServerSetUp, CleanUpNetTest);
SELFTEST(Early, TestNetReceiveData, CleanUpNetTest);
SELFTEST(Early, TestNetSendData, CleanUpNetTest);

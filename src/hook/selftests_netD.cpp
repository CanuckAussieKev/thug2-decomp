// Self-tests for the network library's first part (0x004544E0 - 0x00454FA0): Net::Metrics, Net::Manager's settings,
// MsgDesc / StreamDesc / MsgHandlerContext, and the message compression - the originals against ours. Winsock,
// the debug output, the memory manager's free and the main loop's task list are stood in for (no real network);
// CompressMessage runs on a stand-in memory manager in one arena (both runs at the same address).

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, gethostbyname, inet_ntoa: what the game uses

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <new>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/net/net_app.h"
#include "game/net/net_compress.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_manager.h"
#include "game/net/net_metrics.h"
#include "game/util/lzss.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using Net::Manager;
using Net::Metrics;
using TestStandIns::AddressOf;
using TestStandIns::Compare;
using TestStandIns::g_trace;
using TestStandIns::HashBytes;
using TestStandIns::Record;
using TestStandIns::Regs;
using TestStandIns::StandIns;

std::vector<TestStandIns::Saved> g_netD_saved;
bool g_netD_swapped = false;

void SaveGlobals(std::vector<TestStandIns::Saved> globals)
{
    g_netD_saved = std::move(globals);
    TestStandIns::SaveAll(g_netD_saved);
    g_netD_swapped = true;
}
void CleanUp()
{
    TestStandIns::RemoveStandIns();
    if (g_netD_swapped) {
        TestStandIns::RestoreAll(g_netD_saved);
        g_netD_swapped = false;
    }
}

uint32_t HashText(const char* text) { return TestStandIns::Hash(text); }

// Scripted answers, in order (then the default).
std::deque<uint32_t> g_answers;
uint32_t NextAnswer(uint32_t otherwise)
{
    if (g_answers.empty())
        return otherwise;
    uint32_t answer = g_answers.front();
    g_answers.pop_front();
    return answer;
}

// Dbg::Printf (the game's and ours): the format and as many arguments as it has conversions.
void StandInPrintf(StandIns& s)
{
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        const char* format = reinterpret_cast<const char*>(r.args[0]);
        Record({0xDB6, HashText(format)});
        int arg = 1;
        for (const char* p = format; *p; ++p) {
            if (*p == '%')
                Record({r.args[arg++]});
        }
        return 0xFFFFFFFFu;
    });
}

// Each case run by the original and by ours (`run(original, case)`), the traces compared.
template <typename Run>
void CompareRuns(const char* what, int cases, Run run)
{
    std::vector<uint32_t> traces[2];
    for (int original = 1; original >= 0; --original) {
        g_trace.clear();
        g_answers.clear();
        for (int i = 0; i < cases; ++i) {
            Record({0xCA5E0000u + i});
            run(original != 0, i);
        }
        traces[original ? 0 : 1] = g_trace;
    }
    Compare(traces[0], traces[1], what);
}

void RecordBytes(const void* bytes, size_t size)
{
    const auto* p = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i < size; i += 4) {
        uint32_t word = 0;
        std::memcpy(&word, p + i, size - i < 4 ? size - i : 4);
        Record({word});
    }
}

template <typename Fn>
Fn At(uint32_t address)
{
    return Original<Fn>(address);
}

// --- Metrics ---------------------------------------------------------------------------------------------------

alignas(16) uint8_t g_metrics[0x1100];

// Every Metrics function on one object with stale contents: counts, a full ring and its wrap, the usage over
// partly and wholly filled histories, packets from before / within / after the last second and across the
// time's wrap; packet counts below zero (the signed remainder writes before the history) and wrapping.
void TestNetMetrics()
{
    CompareRuns("net metrics", 1, [](bool original, int) {
        for (size_t i = 0; i < sizeof(g_metrics); ++i)
            g_metrics[i] = static_cast<uint8_t>(i * 13 + 7);
        auto* m = reinterpret_cast<Metrics*>(g_metrics + 0x40); // (room before it for the negative index)
        auto construct = [&]() {
            return original ? At<Metrics*(__thiscall*)(Metrics*)>(0x004544E0)(m) : m->Construct();
        };
        auto addMessage = [&](int id, int size) {
            original ? At<void(__thiscall*)(Metrics*, int, int)>(0x00454550)(m, id, size) : m->AddMessage(id, size);
        };
        auto addPacket = [&](int size, int time) {
            original ? At<void(__thiscall*)(Metrics*, int, int)>(0x00454D90)(m, size, time) : m->AddPacket(size, time);
        };
        auto usage = [&](int time) {
            original ? At<void(__thiscall*)(Metrics*, int)>(0x00454D00)(m, time) : m->CalculateUsage(time);
            Record({static_cast<uint32_t>(original ? At<int(__thiscall*)(Metrics*)>(0x00454520)(m) : m->GetBytesPerSec())});
        };
        auto counts = [&](int id) {
            Record({static_cast<uint32_t>(original ? At<int(__thiscall*)(Metrics*, int)>(0x00454530)(m, id)
                                                   : m->GetTotalMessageData(id)),
                    static_cast<uint32_t>(original ? At<int(__thiscall*)(Metrics*, int)>(0x00454540)(m, id)
                                                   : m->GetTotalNumMessagesOfId(id))});
        };
        Record({static_cast<uint32_t>(reinterpret_cast<uint8_t*>(construct()) - g_metrics)});
        usage(5000); // nothing yet (the stale history isn't read)
        addMessage(0, 10);
        addMessage(5, 300);
        addMessage(5, -4);
        addMessage(255, 0x7FFFFFFF);
        addMessage(255, 2);
        addMessage(0x80, 1);
        for (int id : {0, 1, 5, 0x7F, 0x80, 255})
            counts(id);
        // Five packets: only they count (the rest of the history is stale).
        for (int i = 0; i < 5; ++i)
            addPacket(100 + i * 11, 1000 + i * 300);
        for (int t : {1000, 1999, 2000, 2200, 2201, 3199, 3200, 900, -5})
            usage(t);
        // A full ring and some (the oldest overwritten).
        for (int i = 5; i < 300; ++i)
            addPacket(i * 3 + 1, 1000 + i * 17);
        for (int t : {1000 + 299 * 17, 1000 + 299 * 17 + 999, 1000 + 299 * 17 + 1000, 1000 + 250 * 17 + 1000,
                      6000, 0x7FFFFFFF})
            usage(t);
        // Times across the wrap of the clock.
        for (int i = 0; i < 6; ++i)
            addPacket(7 + i, static_cast<int>(0x7FFFFE00u + 0x80u * i));
        for (uint32_t t : {0x7FFFFF00u, 0x80000010u, 0x80000200u})
            usage(static_cast<int>(t));
        // Packet counts below zero and at the top.
        m->m_numPackets = -3;
        addPacket(0x55, 0x66);
        addPacket(0x57, 0x68);
        usage(0x70);
        m->m_numPackets = -256;
        addPacket(0x59, 0x6A);
        m->m_numPackets = 0x7FFFFFFF;
        addPacket(0x5B, 0x6C);
        addPacket(0x5D, 0x6E);
        usage(0x70);
        m->m_numPackets = 3;
        usage(0x70);
        RecordBytes(g_metrics, sizeof(g_metrics)); // (the totals, before the constructor clears them)
        Record({static_cast<uint32_t>(reinterpret_cast<uint8_t*>(construct()) - g_metrics)});
        RecordBytes(g_metrics, sizeof(g_metrics));
    });
}

// --- Manager -----------------------------------------------------------------------------------------------------

alignas(16) uint8_t g_manager[0x500];
alignas(16) uint8_t g_mlp[0x100];      // the main loop's manager (its logic task list at +0x04)
alignas(16) uint8_t g_app[0x1F08];     // an App: only its tasks are read

// Two host entries, and inet_ntoa's buffer.
char g_hostName1[] = "netd-host";
char g_hostName2[] = "netd-other";
uint32_t g_hostAddress1 = 0x0A01020Au;
uint32_t g_hostAddress2 = 0xC0A80117u;
char* g_hostList1[] = {reinterpret_cast<char*>(&g_hostAddress1), nullptr};
char* g_hostList2[] = {reinterpret_cast<char*>(&g_hostAddress2), nullptr};
hostent g_host1 = {g_hostName1, nullptr, AF_INET, 4, g_hostList1};
hostent g_host2 = {g_hostName2, nullptr, AF_INET, 4, g_hostList2};
char g_ntoa[32];

void StandInWinsock(StandIns& s)
{
    TestStandIns::StandInApi(s, 0x006452A0, reinterpret_cast<const void*>(&inet_addr), 4, [](Regs& r) {
        Record({0x1AD0, HashText(reinterpret_cast<const char*>(r.args[0]))});
        return NextAnswer(0x01020304u);
    });
    TestStandIns::StandInApi(s, 0x006452A8, reinterpret_cast<const void*>(&gethostbyname), 4, [](Regs& r) {
        Record({0x6E70, HashText(reinterpret_cast<const char*>(r.args[0]))});
        switch (NextAnswer(0)) {
        case 1:
            return reinterpret_cast<uint32_t>(&g_host1);
        case 2:
            return reinterpret_cast<uint32_t>(&g_host2);
        default:
            return 0u;
        }
    });
    TestStandIns::StandInApi(s, 0x006452A4, reinterpret_cast<const void*>(&inet_ntoa), 4, [](Regs& r) {
        Record({0x170A, r.args[0]});
        std::snprintf(g_ntoa, sizeof(g_ntoa), "ip %08X", r.args[0]);
        return reinterpret_cast<uint32_t>(g_ntoa);
    });
}

// A returned string: where it is (in the manager: its offset) and what it says.
void RecordString(const char* text)
{
    auto at = reinterpret_cast<const uint8_t*>(text);
    Record({at >= g_manager && at < g_manager + sizeof(g_manager) ? static_cast<uint32_t>(at - g_manager) : 0xFFFFu,
            HashText(text)});
}

// Every getter and setter on a manager with stale contents: each string setter (equal and different strings,
// each index), every connection type (and the bandwidth left alone by the same type and unknown ones), the
// flags, the dial-up number (type 3 or not, each index), the local address (set, or inet_addr of the local IP),
// the local IP's three paths (localhost unknown; the host found; not found), the app's tasks added in order.
void TestNetManager()
{
    SaveGlobals({{0x00696D68, 4, {}}});
    auto* s = new StandIns;
    StandInWinsock(*s);
    s->At({0x00406C90}, 4, [](Regs& r) {
        Record({0x6C90, r.ecx - reinterpret_cast<uint32_t>(g_mlp), r.args[0]});
        return 0u;
    });
    GameVariable<uint8_t*>(0x00696D68) = g_mlp;

    CompareRuns("net manager", 1, [](bool original, int) {
        for (size_t i = 0; i < sizeof(g_manager); ++i)
            g_manager[i] = static_cast<uint8_t>(0x41 + i % 23); // stale (printable, unterminated) bytes
        g_manager[sizeof(g_manager) - 1] = 0;
        auto* m = reinterpret_cast<Manager*>(g_manager + 0x40);
        using SetText = void(__thiscall*)(Manager*, const char*);
        using SetIndexed = void(__thiscall*)(Manager*, int, const char*);
        using SetInt = void(__thiscall*)(Manager*, int);
        using GetInt = int(__thiscall*)(Manager*);
        auto setText = [&](uint32_t address, void (Manager::*ours)(const char*), const char* text) {
            original ? At<SetText>(address)(m, text) : (m->*ours)(text);
        };
        auto setIndexed = [&](uint32_t address, void (Manager::*ours)(int, const char*), int index, const char* text) {
            original ? At<SetIndexed>(address)(m, index, text) : (m->*ours)(index, text);
        };
        auto getInt = [&](uint32_t address, int (Manager::*ours)()) {
            Record({static_cast<uint32_t>(original ? At<GetInt>(address)(m) : (m->*ours)())});
        };
        auto getBool = [&](uint32_t address, bool (Manager::*ours)()) {
            Record({(original ? At<bool(__thiscall*)(Manager*)>(address)(m) : (m->*ours)()) ? 1u : 0u});
        };
        auto setType = [&](int type) {
            original ? At<SetInt>(0x00454770)(m, type) : m->SetConnectionType(type);
            getInt(0x00454960, &Manager::GetConnectionType);
            getInt(0x004545B0, &Manager::GetBandwidth);
        };
        auto dialupNumber = [&]() {
            RecordString(original ? At<const char*(__thiscall*)(Manager*)>(0x00454DE0)(m) : m->GetDialupNumber());
        };

        // Strings.
        setText(0x00454710, &Manager::SetLocalIP, "10.0.0.7");
        setText(0x00454710, &Manager::SetLocalIP, "10.0.0.7");
        setText(0x00454710, &Manager::SetLocalIP, "10.0.0.17");
        setText(0x00454640, &Manager::SetGateway, "10.0.0.1");
        setText(0x00454640, &Manager::SetGateway, "");
        setText(0x004546A0, &Manager::SetSubnetMask, "255.255.255.0");
        setText(0x004546A0, &Manager::SetSubnetMask, "255.255.255.0");
        setIndexed(0x00454820, &Manager::SetDNSServer, 0, "8.8.8.8");
        setIndexed(0x00454820, &Manager::SetDNSServer, 2, "1.1.1.1");
        setIndexed(0x00454820, &Manager::SetDNSServer, 1, "9.9.9.9");
        setIndexed(0x00454820, &Manager::SetDNSServer, 2, "1.1.1.1");
        setIndexed(0x004545D0, &Manager::SetDialupNumber, 1, "555-0101");
        setIndexed(0x004545D0, &Manager::SetDialupNumber, 2, "555-0102 ext 7");
        setIndexed(0x004545D0, &Manager::SetDialupNumber, 0, "");
        setText(0x00454600, &Manager::SetDialupUserName, "skater");
        setText(0x00454620, &Manager::SetDialupPassword, "hunter2");
        setText(0x00454890, &Manager::SetHostName, "thug2-box");
        setText(0x004548C0, &Manager::SetDomainName, "example.lan");
        setText(0x004548C0, &Manager::SetDomainName, "x");
        // Plain values.
        original ? At<void(__thiscall*)(Manager*, uint32_t)>(0x00454700)(m, 0xC0A80005u) : m->SetLocalAddress(0xC0A80005u);
        for (int type : {3, 3, 0, 7})
            original ? At<SetInt>(0x004547D0)(m, type) : m->SetDeviceType(type);
        for (bool on : {true, true, false, true}) {
            original ? At<void(__thiscall*)(Manager*, bool)>(0x004547F0)(m, on) : m->SetDHCP(on);
            Record({g_manager[0x40 + 0x2E5]});
        }
        for (bool on : {true, false, true})
            original ? At<void(__thiscall*)(Manager*, bool)>(0x00454810)(m, on) : m->SetFlag2E6(on);
        for (int state : {5, 8, -1})
            original ? At<SetInt>(0x004549B0)(m, state) : m->SetModemState(state);
        getInt(0x004549A0, &Manager::GetModemState);
        for (int id : {0, 9, 0x80, 0xFF}) {
            char flags = static_cast<char>(0x80 | id);
            original ? At<void(__thiscall*)(Manager*, uint8_t, char)>(0x00454990)(m, static_cast<uint8_t>(id), flags)
                     : m->SetMessageFlags(static_cast<uint8_t>(id), flags);
        }
        for (int id : {0, 1, 9, 0x80, 0xFF}) {
            char flags = original ? At<char(__thiscall*)(Manager*, uint8_t)>(0x00454980)(m, static_cast<uint8_t>(id))
                                  : m->GetMessageFlags(static_cast<uint8_t>(id));
            Record({static_cast<uint8_t>(flags)});
        }
        // Connection types: the bandwidth with each.
        m->m_connectionType = 9;
        m->m_bandwidth = 12345;
        setType(9);
        setType(1);
        m->m_bandwidth = 777;
        setType(1);
        setType(2);
        setType(3);
        setType(4);
        setType(0);
        setType(5);
        setType(-1);
        setType(4);
        // The dial-up number in use.
        for (int index : {0, 1, 2}) {
            m->m_dialupNumberIndex = index;
            m->m_connectionType = 1;
            dialupNumber();
            m->m_connectionType = 3;
            dialupNumber();
            m->m_connectionType = 4;
            dialupNumber();
        }
        // Getters.
        m->m_modemBaudRate = 28800;
        m->m_lastError = 6;
        getInt(0x004545A0, &Manager::GetModemBaudRate);
        getInt(0x00454970, &Manager::GetLastError);
        getBool(0x004545C0, &Manager::IsOnline);
        getBool(0x00454580, &Manager::ConnectToInternet);
        getBool(0x004545C0, &Manager::IsOnline);
        getBool(0x00454590, &Manager::DisconnectFromInternet);
        getBool(0x004545C0, &Manager::IsOnline);
        getBool(0x00454580, &Manager::ConnectToInternet);
        getBool(0x004545C0, &Manager::IsOnline);
        bool loaded = original ? At<bool(__thiscall*)(Manager*, const char*, const char*, const char*)>(0x004549C0)(
                                     m, "1.2.3.4", "255.0.0.0", "1.2.3.1")
                               : m->LoadNetworkDrivers("1.2.3.4", "255.0.0.0", "1.2.3.1");
        Record({loaded ? 1u : 0u});
        // The local address: set, then from the local IP (inet_addr).
        auto localAddress = [&]() {
            Record({original ? At<uint32_t(__thiscall*)(Manager*)>(0x004548F0)(m) : m->GetLocalAddress()});
        };
        localAddress();
        m->m_localAddress = 0;
        g_answers = {0x0700000Au};
        localAddress();
        g_answers = {0xFFFFFFFFu};
        localAddress();
        // The local IP: localhost unknown; found and its host found; found and its host not.
        auto localIP = [&]() {
            RecordString(original ? At<const char*(__thiscall*)(Manager*)>(0x00454910)(m) : m->GetLocalIP());
        };
        g_answers = {0};
        localIP();
        g_answers = {1, 2};
        localIP();
        g_answers = {2, 0};
        localIP();
        g_answers = {1, 1};
        localIP();
        // The app's tasks.
        std::memset(g_app, 0, sizeof(g_app));
        for (uint32_t i = 0; i < 4; ++i)
            *reinterpret_cast<uint32_t*>(g_app + 0x1444 + 4 * i) = 0x7A5C0000u + i * 0x11;
        auto* app = reinterpret_cast<Net::App*>(g_app);
        original ? At<void(__thiscall*)(Manager*, Net::App*)>(0x00454CB0)(m, app) : m->AddAppTasks(app);
        RecordBytes(g_manager, sizeof(g_manager));
    });
    CleanUp();
}

// --- MsgDesc, StreamDesc, MsgHandlerContext ------------------------------------------------------------------------

alignas(16) uint8_t g_desc[0x100];

// The three constructors on stale memory (and their return values); StreamDesc's destructor with no data, data
// sent in place, and its own data (debug output, freed).
void TestNetDescs()
{
    SaveGlobals({{0x007D1800, 4, {}}});
    auto* s = new StandIns;
    StandInPrintf(*s);
    s->At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE, r.ecx, r.args[0]});
        return 0u;
    });
    GameVariable<uint32_t>(0x007D1800) = 0x3E3E0000u; // the memory manager (stood in: only passed to Free)

    CompareRuns("net message descriptors", 1, [](bool original, int) {
        auto fill = [&]() {
            for (size_t i = 0; i < sizeof(g_desc); ++i)
                g_desc[i] = static_cast<uint8_t>(0xE1 + i * 5);
        };
        uint8_t* at = g_desc + 0x10;
        auto offset = [&](void* p) { return static_cast<uint32_t>(static_cast<uint8_t*>(p) - g_desc); };
        fill();
        auto* msg = reinterpret_cast<Net::MsgDesc*>(at);
        Record({offset(original ? At<Net::MsgDesc*(__thiscall*)(Net::MsgDesc*)>(0x004549F0)(msg) : msg->Construct())});
        RecordBytes(g_desc, sizeof(g_desc));
        fill();
        auto* stream = reinterpret_cast<Net::StreamDesc*>(at);
        Record({offset(original ? At<Net::StreamDesc*(__thiscall*)(Net::StreamDesc*)>(0x004549D0)(stream)
                                : stream->Construct())});
        RecordBytes(g_desc, sizeof(g_desc));
        fill();
        auto* context = reinterpret_cast<Net::MsgHandlerContext*>(g_desc + 4);
        Record({offset(original ? At<Net::MsgHandlerContext*(__thiscall*)(Net::MsgHandlerContext*)>(0x00454A20)(context)
                                : context->Construct())});
        RecordBytes(g_desc, sizeof(g_desc));
        auto destroy = [&](uint8_t* data, uint8_t inPlace) {
            fill();
            stream->m_data = data;
            *reinterpret_cast<uint8_t*>(&stream->m_sendInPlace) = inPlace; // (any nonzero byte is "in place")
            Record({0xDE57});
            original ? At<void(__thiscall*)(Net::StreamDesc*)>(0x00454E00)(stream) : stream->DestroyBody();
            RecordBytes(g_desc, sizeof(g_desc));
        };
        destroy(nullptr, 0);
        destroy(nullptr, 1);
        destroy(reinterpret_cast<uint8_t*>(0x12345678), 1);
        destroy(reinterpret_cast<uint8_t*>(0x12345678), 2);
        destroy(reinterpret_cast<uint8_t*>(0x12345679), 0);
    });
    CleanUp();
}

// --- Compression -----------------------------------------------------------------------------------------------------

// Test data: each case's bytes.
std::vector<std::vector<uint8_t>> CompressionInputs()
{
    std::vector<std::vector<uint8_t>> inputs;
    auto add = [&](std::vector<uint8_t> v) { inputs.push_back(std::move(v)); };
    add({});
    add({0x42});
    add({0x42, 0x42});
    add({0x42, 0x43});
    add({1, 1, 1});
    add({1, 2, 2});
    add({1, 1, 2});
    add({1, 2, 3});
    add({1, 2, 2, 3});
    add({1, 2, 3, 3});
    add({5, 5, 6, 6, 7, 7, 8});
    // Runs of every length around the limits (and one more byte after).
    for (int n : {2, 3, 4, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x100, 0x101, 0x102, 0x103, 0x104, 0x300}) {
        add(std::vector<uint8_t>(n, 0xAA));
        std::vector<uint8_t> v(n, 0xAA);
        v.push_back(0xAB);
        add(v);
        v.push_back(0xAC);
        add(v);
        v.push_back(0xAC);
        add(v);
    }
    // Literals around the limits, ending alone, in a pair or a run.
    for (int n : {3, 4, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0xFF, 0x100, 0x101, 0x180}) {
        std::vector<uint8_t> v;
        for (int i = 0; i < n; ++i)
            v.push_back(static_cast<uint8_t>(i * 7 + 1));
        add(v);
        std::vector<uint8_t> w = v;
        w.push_back(w.back());
        add(w);
        w.push_back(w.back());
        add(w);
        w.push_back(0x99);
        add(w);
        std::vector<uint8_t> x = v;
        x.push_back(0x55);
        x.push_back(0x55);
        x.push_back(0x56);
        add(x);
    }
    // Pseudo-random data over small and large alphabets, short and long.
    uint32_t seed = 0x1234567;
    auto next = [&]() {
        seed = seed * 1103515245u + 12345u;
        return seed >> 16;
    };
    for (int alphabet : {2, 3, 5, 16, 256}) {
        for (int n : {5, 37, 300, 0x1000}) {
            std::vector<uint8_t> v(n);
            for (uint8_t& b : v)
                b = static_cast<uint8_t>(next() % alphabet);
            add(v);
        }
    }
    // Long runs mixed with literals.
    std::vector<uint8_t> mixed;
    for (int i = 0; i < 40; ++i) {
        int run = static_cast<int>(next() % 300);
        uint8_t value = static_cast<uint8_t>(next());
        mixed.insert(mixed.end(), run, value);
        int literal = static_cast<int>(next() % 200);
        for (int j = 0; j < literal; ++j)
            mixed.push_back(static_cast<uint8_t>(next()));
    }
    add(mixed);
    return inputs;
}

alignas(16) uint8_t g_rleIn[0x8000];
alignas(16) uint8_t g_rleOut[0x10000];

// RleCompress, the original against ours on every input: the size and the whole output buffer; each of ours
// expands back (RleDecompress) to its input - but a single byte, which codes to nothing.
void TestNetRleCompress()
{
    std::vector<std::vector<uint8_t>> inputs = CompressionInputs();
    CompareRuns("net RLE compress", static_cast<int>(inputs.size()), [&](bool original, int i) {
        const std::vector<uint8_t>& input = inputs[i];
        std::memset(g_rleIn, 0x3C, sizeof(g_rleIn));
        std::memcpy(g_rleIn, input.data(), input.size());
        std::memset(g_rleOut, 0xCC, sizeof(g_rleOut));
        int size = static_cast<int>(input.size());
        int coded = original ? Original<int(__cdecl*)(const uint8_t*, int, uint8_t*, int)>(0x00454A60)(
                                   g_rleIn, size, g_rleOut, size * 2 + 16)
                             : Net::RleCompress(g_rleIn, size, g_rleOut, size * 2 + 16);
        Record({static_cast<uint32_t>(coded), HashBytes(g_rleOut, size * 2 + 64)});
    });
    bool roundTrips = true;
    for (const std::vector<uint8_t>& input : inputs) {
        int size = static_cast<int>(input.size());
        std::memcpy(g_rleIn, input.data(), input.size());
        int coded = Net::RleCompress(g_rleIn, size, g_rleOut, 0);
        static uint8_t expanded[0x8000];
        int expandedSize = sizeof(expanded);
        int result = Net::RleDecompress(g_rleOut, coded, expanded, &expandedSize);
        bool ok = result == 0 && (size == 1 ? expandedSize == 0 && coded == 0
                                            : expandedSize == size && std::memcmp(expanded, input.data(), size) == 0);
        if (!ok) {
            Log("  net RLE round trip: size %d coded %d result %d expanded %d", size, coded, result, expandedSize);
            roundTrips = false;
        }
    }
    Check(roundTrips, "net RLE compress: round trips");
}

// RleDecompress, the original against ours: every coded input with room exactly, one byte short, plenty and
// none; empty code; hand-made code (runs and literals of each length, a literal running past the end of the
// code - it reads on until the output is full).
void TestNetRleDecompress()
{
    std::vector<std::vector<uint8_t>> codes;
    for (const std::vector<uint8_t>& input : CompressionInputs()) {
        std::memcpy(g_rleIn, input.data(), input.size());
        int coded = Net::RleCompress(g_rleIn, static_cast<int>(input.size()), g_rleOut, 0);
        codes.emplace_back(g_rleOut, g_rleOut + coded);
    }
    codes.push_back({0xFF, 0x11, 0x80, 0x22, 0x00, 0x33, 0x7F});
    codes.push_back({0x02, 1, 2, 3, 0x81, 9, 0x7F});
    codes.push_back({0x05, 1, 2});
    std::vector<uint8_t> literal(1, 0x7F);
    for (int i = 0; i < 0x80; ++i)
        literal.push_back(static_cast<uint8_t>(i ^ 0x5A));
    codes.push_back(literal);
    int cases = static_cast<int>(codes.size());
    CompareRuns("net RLE decompress", cases * 4, [&](bool original, int i) {
        const std::vector<uint8_t>& code = codes[i / 4];
        int expanded = 0;
        for (size_t j = 0; j < code.size();) { // the size the code expands to (if well formed)
            uint8_t c = code[j];
            expanded += c < 0x80 ? c + 1 : (c & 0x7F) + 2;
            j += c < 0x80 ? c + 2 : 2;
        }
        int room[4] = {expanded, expanded - 1, expanded + 100, 0};
        std::memset(g_rleIn, 0x07, sizeof(g_rleIn)); // past the code: literals of 8 (malformed code reads on)
        std::memcpy(g_rleIn, code.data(), code.size());
        std::memset(g_rleOut, 0xCC, sizeof(g_rleOut));
        int size = room[i % 4];
        if (size < 0)
            size = 0;
        int outSize = size;
        int result = original ? Original<int(__cdecl*)(const uint8_t*, int, uint8_t*, int*)>(0x00454C20)(
                                    g_rleIn, static_cast<int>(code.size()), g_rleOut, &outSize)
                              : Net::RleDecompress(g_rleIn, static_cast<int>(code.size()), g_rleOut, &outSize);
        Record({static_cast<uint32_t>(result), static_cast<uint32_t>(outSize), HashBytes(g_rleOut, expanded + 200)});
    });
}

// CompressMessage on a stand-in memory manager (one heap over the arena; both runs in the same arena):
// method 1 and 2 on compressible and incompressible data, a length the coded size equals (not smaller: not
// sent compressed) and one more, other methods (nothing coded: the 3-byte header "wins"), an empty message.
// The returned buffer, the id and length, the debug output and the whole arena are compared.
constexpr uint32_t kArenaSize = 0x40000;
constexpr uint32_t kHeapOffset = 0x1000;
uint8_t* g_arena;

void SetUpArena()
{
    using Mem::Heap;
    using Mem::Region;
    std::memset(g_arena, 0, kHeapOffset);
    std::memset(g_arena + kHeapOffset, 0xCD, kArenaSize - kHeapOffset);
    GameVariable<bool>(0x007CC979) = true;
    auto* manager = reinterpret_cast<Mem::Manager*>(g_arena);
    GameVariable<void*>(0x007D1800) = manager;
    auto* region = new (g_arena + 0x600) Region(g_arena + kHeapOffset, g_arena + kArenaSize);
    auto* heap = new (g_arena + 0x620) Heap(region, 1, "default");
    manager->m_heaps[0] = heap;
    manager->m_heapCount = 1;
    manager->m_topDownHeap = heap; // (LZSS allocates its tables there)
    manager->PushContext(heap);
}

void TestNetCompressMessage()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestNetCompressMessage skipped (the memory manager is already set up)");
        return;
    }
    SaveGlobals({{0x007CC979, 1, {}}, {0x007D1800, 4, {}}});
    g_arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* s = new StandIns;
    StandInPrintf(*s);

    struct Case {
        uint8_t id;
        std::vector<uint8_t> data;
        char method;
    };
    std::vector<Case> cases;
    std::vector<uint8_t> runs;
    for (int i = 0; i < 6; ++i)
        runs.insert(runs.end(), 40 + i * 9, static_cast<uint8_t>(0x30 + i));
    std::vector<uint8_t> noise;
    uint32_t seed = 99;
    for (int i = 0; i < 300; ++i) {
        seed = seed * 1103515245u + 12345u;
        noise.push_back(static_cast<uint8_t>(seed >> 16));
    }
    std::vector<uint8_t> text;
    for (const char* p = "the quick brown fox jumps over the lazy dog; the quick brown fox jumps again. "; *p; ++p)
        text.push_back(static_cast<uint8_t>(*p));
    text.insert(text.end(), text.begin(), text.end());
    text.insert(text.end(), text.begin(), text.end());
    cases.push_back({0x21, runs, 1});
    cases.push_back({0x22, noise, 1});
    cases.push_back({0x23, runs, 2});
    cases.push_back({0x24, noise, 2});
    cases.push_back({0x25, text, 2});
    cases.push_back({0x26, text, 1});
    cases.push_back({0x27, runs, 0});
    cases.push_back({0x28, runs, 4}); // (3 is THUGPRO's LZHAM now: selftests_thugpro_join.cpp)
    cases.push_back({0x29, {}, 0});
    cases.push_back({0x2A, {}, 1});
    cases.push_back({0x2B, {7}, 1});
    // Lengths where the RLE-coded message (id, length and data) is exactly as long as the data, and one shorter:
    // a run of r bytes after k distinct ones.
    for (int wanted : {0, -1}) {
        bool found = false;
        for (int k = 1; k < 120 && !found; ++k) {
            for (int r = 2; r < 120 && !found; ++r) {
                std::vector<uint8_t> data;
                for (int j = 0; j < k; ++j)
                    data.push_back(static_cast<uint8_t>(0x80 + j));
                data.insert(data.end(), r, 0x11);
                std::vector<uint8_t> message = {0x2C, static_cast<uint8_t>(data.size()),
                                                static_cast<uint8_t>(data.size() >> 8)};
                message.insert(message.end(), data.begin(), data.end());
                int coded = Net::RleCompress(message.data(), static_cast<int>(message.size()), g_rleOut, 0);
                if (coded - static_cast<int>(data.size()) == wanted) {
                    cases.push_back({0x2C, data, 1});
                    found = true;
                }
            }
        }
        Check(found, "net compress message: a boundary case found");
    }

    CompareRuns("net compress message", static_cast<int>(cases.size()), [&](bool original, int i) {
        SetUpArena();
        std::vector<OriginalCode*> guards;
        if (original) {
            for (uint32_t address : {0x00454A60u, 0x00401700u})
                guards.push_back(new OriginalCode(address));
        }
        const Case& c = cases[i];
        uint8_t* data = g_rleIn;
        std::memcpy(data, c.data.data(), c.data.size());
        uint8_t id = c.id;
        uint16_t length = static_cast<uint16_t>(c.data.size());
        uint8_t* result = original ? Original<uint8_t*(__cdecl*)(uint8_t*, uint16_t*, const void*, char)>(0x00454E30)(
                                         &id, &length, data, c.method)
                                   : Net::CompressMessage(&id, &length, data, c.method);
        for (OriginalCode* guard : guards)
            delete guard;
        Record({result ? static_cast<uint32_t>(result - g_arena) : 0xFFFFFFFFu, id, length});
        if (result) {
            // The coded message expands back to the original one.
            int size = result[1] | (result[2] << 8);
            static uint8_t expanded[0x10000];
            int expandedSize = sizeof(expanded);
            if (result[0] == 1) {
                int ok = Net::RleDecompress(result + 3, length - 3, expanded, &expandedSize);
                Record({static_cast<uint32_t>(ok), static_cast<uint32_t>(expandedSize), static_cast<uint32_t>(size)});
            } else if (result[0] == 2) {
                expandedSize = static_cast<int>(Lzss::Decode(result + 3, expanded, length - 3) - expanded);
                Record({static_cast<uint32_t>(expandedSize), static_cast<uint32_t>(size)});
            }
            if (result[0] == 1 || result[0] == 2) {
                bool same = expandedSize == size && expanded[0] == c.id &&
                            (expanded[1] | (expanded[2] << 8)) == static_cast<int>(c.data.size()) &&
                            std::memcmp(expanded + 3, c.data.data(), c.data.size()) == 0;
                Check(same, "net compress message: expands back");
            }
        }
        std::vector<uint32_t> image(kArenaSize / 4);
        std::memcpy(image.data(), g_arena, kArenaSize);
        g_trace.insert(g_trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
    });
    CleanUp();
    VirtualFree(g_arena, 0, MEM_RELEASE);
    g_arena = nullptr;
}

} // namespace

SELFTEST(Early, TestNetMetrics, CleanUp);
SELFTEST(Early, TestNetManager, CleanUp);
SELFTEST(Early, TestNetDescs, CleanUp);
SELFTEST(Early, TestNetRleCompress, CleanUp);
SELFTEST(Early, TestNetRleDecompress, CleanUp);
SELFTEST(Early, TestNetCompressMessage, CleanUp);

// Self-tests for the network manager's tasks and GameSpy callbacks (src/game/net/gamenet_tasks.cpp): the original and
// ours run on the same stand-in arena (gamenet_testkit.h), everything they call outside stood in for by recording
// code; the calls and the arena are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/task.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/net_manager.h"
#include "game/net/player_info.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;

constexpr uint32_t kTask = 0x16000;     // the task: +0x1C the manager
constexpr uint32_t kNet = 0x16100;      // Net::Manager (0x37C)
constexpr uint32_t kConns = 0x18000;    // connections, 0x4000 apart (6)
constexpr uint32_t kApps = 0x17000;     // the client and the server (0x800 apart)

uint32_t g_now;
uint32_t g_king;
int g_playerCount;
// The apps' connections: which of the six each app has.
std::vector<int> g_appConns[2];

uint32_t ConnAt(int i) { return kConns + 0x4000 * static_cast<uint32_t>(i); }

void Install(StandIns& s)
{
    InstallCommon(s);
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return g_now; });
    Stub(s, {0x00565A00}, 4, 0x5A00);
    Stub(s, {0x00537480, AddressOf(&GameNet::Manager::GetKingOfTheHill)}, 0, 0x7480, [](Regs&) { return g_king ? At(g_king) : 0u; });
    Stub(s, {0x00544340, AddressOf(&GameNet::PlayerInfo::MarkAsKing)}, 4, 0x4340);
    s.At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Rec(0x72C0, {r.ecx, r.args[1] & 0xFF});
        reinterpret_cast<uint32_t*>(r.args[0])[1] = 0;
        return g_playerCount > 0 ? At(kPlayers) : 0u;
    });
    s.At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        uint32_t& index = reinterpret_cast<uint32_t*>(r.args[0])[1];
        Rec(0x7010, {r.args[1] & 0xFF, index});
        ++index;
        return static_cast<int>(index) < g_playerCount ? At(kPlayers + kPlayerSize * index) : 0u;
    });
    Stub(s, {0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, 0x68F0);
    // Strings by name: a text naming the checksum.
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
        Rec(0x8FD0, {r.args[0]});
        char* text = &Word<char>(kMisc + 0xE40);
        std::snprintf(text, 32, "<%08X>", r.args[0]);
        return reinterpret_cast<uint32_t>(text);
    });
    Stub(s, {0x00454DE0, AddressOf(&Net::Manager::GetDialupNumber)}, 0, 0x4DE0, [](Regs&) { return At(kMisc + 0xE80); });
    Stub(s, {0x00454580, AddressOf(&Net::Manager::ConnectToInternet)}, 0, 0x4580, [](Regs&) { return 1u; });
    // Connections.
    auto app = [](uint32_t ecx) { return ecx == At(kApps) ? 0 : 1; };
    s.At({0x00458BE0, AddressOf(&Net::App::FirstConnection)}, 4, [app](Regs& r) {
        Rec(0x8BE0, {r.ecx});
        auto& list = g_appConns[app(r.ecx)];
        reinterpret_cast<uint32_t*>(r.args[0])[1] = 0;
        return list.empty() ? 0u : At(ConnAt(list[0]));
    });
    s.At({0x004573D0, AddressOf(&Net::App::NextConnection)}, 4, [app](Regs& r) {
        Rec(0x73D0, {r.ecx});
        auto& list = g_appConns[app(r.ecx)];
        uint32_t& index = reinterpret_cast<uint32_t*>(r.args[0])[1];
        ++index;
        return index < list.size() ? At(ConnAt(list[index])) : 0u;
    });
    Stub(s, {0x00456540, AddressOf(&Net::Conn::GetHighestResendCount)}, 0, 0x6540, [](Regs& r) {
        return (r.ecx - At(kConns)) / 0x4000 + 3;
    });
    // GameSpy and the debug output.
    StubCdecl(s, {0x005FA010}, 0xA010, 1, [](Regs& r) {
        Rec(0xA011, {Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    StubCdecl(s, {0x005F9FE0}, 0x9FE0, 2);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Rec(0x1C30, {Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
        return 0u;
    });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_now = 1000;
        g_king = 0;
        g_playerCount = 3;
        g_appConns[0].clear();
        g_appConns[1].clear();
        Word<uint32_t>(kTask + 0x1C) = At(kManager);
        GameVariable<uint32_t>(0x006EFDD8) = At(kNet);
        GameVariable<uint32_t>(0x007CCDE8) = 0;
        GameVariable<uint32_t>(0x007CCDF0) = 0;
        StandIns* s = new StandIns;
        Install(*s);
        scenario(original);
        Rec(0xC10B, {GameVariable<uint32_t>(0x007CCDE8), GameVariable<uint32_t>(0x007CCDF0)});
        delete s;
        return Finish();
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

using TaskCode = void(__cdecl*)(void*);
void RunTask(bool original, uint32_t address, TaskCode ours)
{
    void* task = &Word<uint8_t>(kTask);
    original ? Original<TaskCode>(address)(task) : ours(task);
    Rec(0x7A5C, {address});
}

void LevelChange(bool original)
{
    Word<uint32_t>(kManager + 0xEC) = At(kMisc + 0x480);
    GameVariable<uint32_t>(0x007CCDE8) = 2000;
    RunTask(original, 0x0052F4A0, GameNet::LevelChangeTask); // not yet
    g_now = 2000;
    RunTask(original, 0x0052F4A0, GameNet::LevelChangeTask);
    g_king = kPlayers + kPlayerSize;
    g_playerCount = 0;
    g_now = 2001;
    RunTask(original, 0x0052F4A0, GameNet::LevelChangeTask);
}

void ModemStates(bool original)
{
    std::memcpy(&Word<char>(kMisc + 0xE80), "555-0123", 9);
    Word<int>(kNet + 0x374) = 56000;
    // Each state (and then the same again), the connection types for 0 and 2, and the redial wait.
    for (int state : {0, 0, 1, 2, 3, 4, 5, 6, 9, 0, 2}) {
        Word<int>(kNet + 0x2E8) = state;
        Word<int>(kNet + 0x128) = state == 0 ? 3 : state == 2 ? 1 : 2;
        RunTask(original, 0x00533810, GameNet::ModemStateTask);
    }
    Word<int>(kNet + 0x128) = 1;
    Word<int>(kNet + 0x2E8) = 0;
    RunTask(original, 0x00533810, GameNet::ModemStateTask);
    Word<int>(kNet + 0x2E8) = 2;
    Word<int>(kNet + 0x128) = 3;
    RunTask(original, 0x00533810, GameNet::ModemStateTask);
    Word<int>(kNet + 0x2E8) = 7;
    RunTask(original, 0x00533810, GameNet::ModemStateTask);
    RunTask(original, 0x00533810, GameNet::ModemStateTask); // state 8 now: waiting
    g_now = 1000 + 5000;
    RunTask(original, 0x00533810, GameNet::ModemStateTask);
    g_now = 1000 + 5001;
    RunTask(original, 0x00533810, GameNet::ModemStateTask);
}

void Metrics(bool original)
{
    for (int i = 0; i < 6; ++i) {
        uint32_t conn = ConnAt(i);
        Word<uint32_t>(conn + 0x64) = i == 2 ? 1 : 0; // a local connection
        Word<int>(conn + 0x1CA4) = 30 + i;
        Word<int>(conn + 0x1CDC + 0x1008) = 1000 * i + 1;
        Word<int>(conn + 0x2CE8 + 0x1008) = 2000 * i + 2;
    }
    Word<uint32_t>(kManager + 0x08) = At(kApps);
    Word<uint32_t>(kManager + 0x04) = At(kApps + 0x800);
    g_appConns[0] = {0, 1, 2};
    g_appConns[1] = {3, 2, 4, 5};
    RunTask(original, 0x00533B50, GameNet::NetMetricsTask);
    Word<Net::App>(kApps).m_flags = Net::App::kLocal; // a local client: skipped
    RunTask(original, 0x00533B50, GameNet::NetMetricsTask);
    Word<uint32_t>(kManager + 0x04) = 0;
    Word<uint32_t>(kManager + 0x08) = 0;
    RunTask(original, 0x00533B50, GameNet::NetMetricsTask);
}

void JoinTimeout(bool original)
{
    Word<uint32_t>(kManager + 0xE4) = At(kMisc + 0x4C0);
    struct Case {
        int state;
        uint32_t start, now;
    };
    const Case cases[] = {{8, 0, 100000}, {2, 50000, 110000}, {2, 50000, 110001}, {6, 0xFFFFF000, 0x1000}, {2, 0, 0x90000000}};
    for (const Case& c : cases) {
        Word<int>(kManager + 0x198) = c.state;
        Word<uint32_t>(kManager + 0x1AC) = c.start;
        g_now = c.now;
        RunTask(original, 0x00533ED0, GameNet::JoinTimeoutTask);
    }
}

void Callbacks(bool original)
{
    void* buffer = &Word<uint8_t>(kMisc + 0x100);
    for (int key : {0, 3, 7}) {
        if (original)
            Original<void(__cdecl*)(int, int, void*, void*)>(0x00533400)(key, key + 1, buffer, nullptr);
        else
            GameNet::PlayerTeamKeyCallback(key, key + 1, buffer, nullptr);
    }
    for (int type : {0, 1, 2, -1}) {
        if (original)
            Original<void(__cdecl*)(int, void*, void*)>(0x00533420)(type, buffer, nullptr);
        else
            GameNet::KeyListCallback(type, buffer, nullptr);
    }
    const char* message = "the error";
    if (original)
        Original<void(__cdecl*)(int, const char*, void*)>(0x005334B0)(-3, message, nullptr);
    else
        GameNet::AddErrorCallback(-3, message, nullptr);
}

void TestGameNetTasks()
{
    SaveGlobals({{0x006EFDD8, 4, {}}, {0x007CCDE8, 4, {}}, {0x007CCDF0, 4, {}}});
    RunCase("GameNet::LevelChangeTask", LevelChange);
    RunCase("GameNet::ModemStateTask", ModemStates);
    RunCase("GameNet::NetMetricsTask", Metrics);
    RunCase("GameNet::JoinTimeoutTask", JoinTimeout);
    RunCase("GameNet: the GameSpy query callbacks", Callbacks);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestGameNetTasks, GameNetKit::Cleanup);

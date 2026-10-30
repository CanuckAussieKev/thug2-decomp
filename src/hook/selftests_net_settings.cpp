// Self-tests for the network manager's network settings and the members after them (src/game/net/gamenet_settings.cpp,
// the start points / toggles / score rule of gamenet_members.cpp): the original and ours on the same stand-in arena
// (gamenet_testkit.h), everything they call outside stood in for by recording code; the calls, results and arena are
// compared, case by case.

#include <windows.h>

#pragma warning(disable : 4996) // (strcpy into the test's own arena)

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/task.h"
#include "game/mth/random.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_manager.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;
using Script::CArray;
using Script::CStruct;

constexpr uint32_t kNetMgr = 0x14000;   // Net::Manager (0x37C)
constexpr uint32_t kArray = 0x14400;    // default_master_servers (a CArray)
constexpr uint32_t kElements = 0x14440; // its elements
constexpr uint32_t kEntries = 0x14500;  // stand-in structures, 8 apart
constexpr uint32_t kTexts = 0x14600;    // strings, 0x20 apart
constexpr uint32_t kSkatePrefs = 0x14C00; // the skate module's preferences (a stand-in pointer)

bool g_netGame, g_teamGame, g_online, g_connects, g_accumulate, g_combo, g_elimiskate, g_horse;
uint32_t g_gameType;
int g_maxPlayers, g_modeMaxPlayers;
uint32_t g_random;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_answer; // (structure, name) -> value
std::map<uint32_t, uint32_t> g_globals;                     // Script::GetString's answers

uint32_t Text(int i) { return At(kTexts + 0x20 * static_cast<uint32_t>(i)); }
void SetText(int i, const char* text) { std::strcpy(&Word<char>(kTexts + 0x20 * static_cast<uint32_t>(i)), text); }

uint32_t Answer(Regs& r, uint32_t tag, uint32_t name)
{
    // (not the value the out slot held before: the original's are uninitialised locals)
    Rec(tag, {r.ecx, name, r.args[2] & 0xFF});
    auto it = g_answer.find({r.ecx, name});
    if (it == g_answer.end())
        return 0u;
    *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
    return 1u;
}

void NetSetter(StandIns& s, uint32_t address, uint32_t ours, int args, bool strings)
{
    s.At({address, ours}, static_cast<uint16_t>(4 * args), [address, args, strings](Regs& r) {
        g_trace.push_back(address);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < args; ++i) {
            bool last = i == args - 1;
            g_trace.push_back(strings && last ? Str(r.args[i]) : r.args[i]);
        }
        return 0u;
    });
}

void Install(StandIns& s)
{
    InstallCommon(s);
    Stub(s, {0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, 0x32A0, [](Regs&) { return g_gameType; });
    Stub(s, {0x005535F0, AddressOf(&Game::CGameMode::IsTeamGame)}, 0, 0x35F0, [](Regs&) { return g_teamGame ? 1u : 0u; });
    Stub(s, {0x0044E4F0, AddressOf(&Manager::InNetGame)}, 0, 0xE4F0, [](Regs&) { return g_netGame ? 1u : 0u; });
    Stub(s, {0x00552FB0}, 0, 0x2FB0, [](Regs&) { return g_accumulate ? 1u : 0u; });
    Stub(s, {0x00552FD0}, 0, 0x2FD0, [](Regs&) { return g_combo ? 1u : 0u; });
    Stub(s, {0x00553910, AddressOf(&Game::CGameMode::EndConditionsMet)}, 0, 0x3910, [](Regs&) { return g_elimiskate ? 1u : 0u; });
    Stub(s, {0x005531D0}, 0, 0x31D0, [](Regs&) { return static_cast<uint32_t>(g_modeMaxPlayers); });
    Stub(s, {0x00553220, AddressOf(&Game::CGameMode::IsTrue)}, 4, 0x3220, [](Regs&) { return g_horse ? 1u : 0u; });
    StubCdecl(s, {0x00403DE0, AddressOf(&Mth::Rnd)}, 0x3DE0, 1, [](Regs& r) {
        g_random = g_random * 1103515245u + 12345u;
        int n = static_cast<int>(r.args[0]);
        return n > 0 ? ((g_random >> 16) & 0x7FFF) % static_cast<uint32_t>(n) : 0u;
    });
    Stub(s, {0x00564D80, AddressOf(&Mdl::Skate::GetSplitScreenPreferences)}, 0, 0x4D80, [](Regs&) { return At(kSkatePrefs); });
    // Preferences: each preference stood in by its own name (a fake pointer), answered by g_answer.
    Stub(s, {0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, 0xC4A0, [](Regs& r) { return r.args[0]; });
    s.At({0x0046C5D0, AddressOf(&Prefs::Preferences::SetPreference)}, 8, [](Regs& r) {
        Rec(0xC5D0, {r.ecx, r.args[0], r.args[1]});
        return 1u;
    });
    s.At({0x00476EC0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) { return Answer(r, 0x6EC0, r.args[0]); });
    s.At({0x00476EE0, AddressOf(static_cast<bool (CStruct::*)(const char*, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) { return Answer(r, 0x6EE0, Str(r.args[0])); });
    s.At({0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) { return Answer(r, 0x6C60, r.args[0]); });
    StubCdecl(s, {0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0x9070, 1,
              [](Regs&) { return At(kArray); });
    StubCdecl(s, {0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0x8FD0, 1,
              [](Regs& r) {
                  auto it = g_globals.find(r.args[0]);
                  return it == g_globals.end() ? Text(0) /* "" */ : it->second;
              });
    // The net library.
    // (the flag's byte only: the original pushes a whole stack slot)
    s.At({0x004547F0, AddressOf(&Net::Manager::SetDHCP)}, 4, [](Regs& r) {
        Rec(0x47F0, {r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    NetSetter(s, 0x00454890, AddressOf(&Net::Manager::SetHostName), 1, true);
    NetSetter(s, 0x004548C0, AddressOf(&Net::Manager::SetDomainName), 1, true);
    NetSetter(s, 0x00454710, AddressOf(&Net::Manager::SetLocalIP), 1, true);
    NetSetter(s, 0x00454640, AddressOf(&Net::Manager::SetGateway), 1, true);
    NetSetter(s, 0x004546A0, AddressOf(&Net::Manager::SetSubnetMask), 1, true);
    NetSetter(s, 0x00454820, AddressOf(&Net::Manager::SetDNSServer), 2, true);
    NetSetter(s, 0x004545D0, AddressOf(&Net::Manager::SetDialupNumber), 2, true);
    NetSetter(s, 0x00454600, AddressOf(&Net::Manager::SetDialupUserName), 1, true);
    NetSetter(s, 0x00454620, AddressOf(&Net::Manager::SetDialupPassword), 1, true);
    NetSetter(s, 0x004547D0, AddressOf(&Net::Manager::SetDeviceType), 1, false);
    NetSetter(s, 0x00454770, AddressOf(&Net::Manager::SetConnectionType), 1, false);
    Stub(s, {0x00454580, AddressOf(&Net::Manager::ConnectToInternet)}, 0, 0x4580, [](Regs&) { return g_connects ? 1u : 0u; });
    Stub(s, {0x00454590, AddressOf(&Net::Manager::DisconnectFromInternet)}, 0, 0x4590, [](Regs&) { return 1u; });
    Stub(s, {0x005453E0, AddressOf(&GameNet::Manager::SetServiceState)}, 4, 0x53E0);
    Stub(s, {0x00532C50, AddressOf(&Manager::GetMaxPlayers)}, 0, 0x2C50, [](Regs&) { return static_cast<uint32_t>(g_maxPlayers); });
    Stub(s, {0x00534090, AddressOf(&Manager::SetMaxPlayersPreference)}, 4, 0x4090);
    Stub(s, {0x005341B0, AddressOf(&Manager::SetMaxObserversPreference)}, 4, 0x41B0);
    // Tasks.
    Stub(s, {0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, 0x6C90);
    Stub(s, {0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, 0x68F0);
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    Log("self-test: %s", what);
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_answer.clear();
        g_globals.clear();
        g_netGame = g_teamGame = g_online = g_connects = g_accumulate = g_combo = g_elimiskate = g_horse = false;
        g_gameType = 0;
        g_maxPlayers = 8;
        g_modeMaxPlayers = 4;
        g_random = 0;
        SetText(0, "");
        GameVariable<uint32_t>(0x006EFDD8) = At(kNetMgr);
        StandIns* s = new StandIns;
        Install(*s);
        scenario(original);
        delete s;
        return Finish();
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

Manager* Self() { return &Word<Manager>(kManager); }
template <typename R, typename... A>
R Orig(uint32_t address, A... args)
{
    return Original<R(__thiscall*)(Manager*, A...)>(address)(Self(), args...);
}
void Result(uint32_t tag, uint32_t value) { Rec(0xF000 | tag, {value}); }

// --- UseNetworkPreferences ---

// The preferences' names.
constexpr uint32_t kChecksum = 0x21902065, kUiString = 0x96875C0F, kName = 0xA1DC81F9, kTrue = 0xF81BC89B;
constexpr uint32_t kUseDefault = 0xE7A3CB08, kServer1 = 0xF480C7DC, kServer2 = 0x6D899666, kBroadband = 0xBB2EB924,
                   kIpAddress = 0xDD002A73, kGateway = 0xEB012280, kSubnet = 0xF9794735, kDns = 0x4C3512CD,
                   kDns2 = 0x2D4EDA9F, kHost = 0x7F91069B, kDomain = 0x0C00AC9E, kAutoDns = 0x5DDFCAD3,
                   kDial1 = 0x3510C289, kDial2 = 0x5C867AC6, kDial3 = 0x2B814A50, kUser = 0x7573A233,
                   kPassword = 0xB8EFE291, kDevice = 0xFA187DEC, kAuth = 0xE5ADE642;
constexpr uint32_t kOverrideIp = 0x5F6FDED5, kOverrideGateway = 0x3E683519, kOverrideSubnet = 0x676BC0CB,
                   kOverrideDns = 0x93CF2232, kForceBroadbandPc = 0x504ACA1F, kForcePcmcia = 0xF3780073;

struct Settings {
    bool useDefault = false;
    const char* server1 = "1.2.3.4";
    const char* server2 = "0.0.0.0";
    bool dhcp = false;
    bool autoDns = false;
    uint32_t device = 0;
    bool auth = true;
    int defaults = 2;
    const char* ip = nullptr;      // the override globals (null: missing - "" from the stand-in)
    const char* gateway = nullptr;
    const char* subnet = nullptr;
    const char* dns = nullptr;
    bool ipNull = false;           // the override's GetString gives null
    bool forceBroadbandPc = false, forcePcmcia = false;
    bool dropAutoDns = false;      // auto_dns has no checksum (the slot keeps use_default's)
};

void UsePreferences(bool original, const Settings& c)
{
    g_answer.clear();
    g_globals.clear();
    int texts = 1;
    auto text = [&](const char* value) {
        SetText(texts, value);
        return Text(texts++);
    };
    auto string = [&](uint32_t preference, const char* value) { g_answer[{preference, kUiString}] = text(value); };
    auto checksum = [&](uint32_t preference, uint32_t value) { g_answer[{preference, kChecksum}] = value; };
    checksum(kUseDefault, c.useDefault ? kTrue : 0x1111);
    string(kServer1, c.server1);
    string(kServer2, c.server2);
    checksum(kBroadband, c.dhcp ? 0x2189CFF8u : 0x2222u);
    string(kIpAddress, "10.0.0.2");
    string(kGateway, "10.0.0.1");
    string(kSubnet, "255.0.0.0");
    string(kDns, "10.0.0.53");
    string(kDns2, "10.0.0.54");
    string(kHost, "host");
    string(kDomain, "domain");
    if (!c.dropAutoDns)
        checksum(kAutoDns, c.autoDns ? kTrue : 0x3333);
    string(kDial1, "555-1");
    string(kDial2, "555-2");
    string(kDial3, "555-3");
    string(kUser, "user");
    string(kPassword, "pass");
    checksum(kDevice, c.device);
    checksum(kAuth, c.auth ? kTrue : 0x4444);
    // The default list.
    auto* array = &Word<CArray>(kArray);
    std::memset(array, 0, sizeof(CArray));
    array->m_type = Script::ESYMBOLTYPE_STRUCTURE;
    array->m_size = static_cast<uint32_t>(c.defaults);
    for (int i = 0; i < c.defaults; ++i) {
        uint32_t entry = At(kEntries + 8 * static_cast<uint32_t>(i));
        Word<uint32_t>(kElements + 4 * static_cast<uint32_t>(i)) = entry;
        char name[16];
        std::snprintf(name, sizeof(name), "master%d.gs", i);
        g_answer[{entry, kName}] = text(name);
    }
    if (c.defaults == 1)
        array->m_single = Word<uint32_t>(kElements);
    else
        array->m_elements = &Word<uint32_t>(kElements);
    if (c.ip)
        g_globals[kOverrideIp] = text(c.ip);
    if (c.ipNull)
        g_globals[kOverrideIp] = 0;
    if (c.gateway)
        g_globals[kOverrideGateway] = text(c.gateway);
    if (c.subnet)
        g_globals[kOverrideSubnet] = text(c.subnet);
    if (c.dns)
        g_globals[kOverrideDns] = text(c.dns);
    if (c.forceBroadbandPc)
        g_globals[kForceBroadbandPc] = text("1");
    if (c.forcePcmcia)
        g_globals[kForcePcmcia] = text("1");
    std::memset(&Word<uint8_t>(kManager + 0x9954), 0x77, 0xA0);
    Word<uint8_t>(kNetMgr + 0x2E6) = 0x55;
    if (original)
        Orig<void>(0x00535040);
    else
        Self()->UseNetworkPreferences();
    Result(1, Word<uint8_t>(kNetMgr + 0x2E6));
    Result(15, HashBytes(&Word<uint8_t>(kManager + 0x9954), 0xA0)); // the master servers
}

void Preferences(bool original)
{
    Settings base;
    UsePreferences(original, base);
    Settings c = base;
    c.server1 = "0.0.0.0";
    c.server2 = "5.6.7.8";
    c.defaults = 3;
    UsePreferences(original, c);
    c = base;
    c.server2 = "9.9.9.9";
    c.defaults = 1;
    c.dhcp = true;
    UsePreferences(original, c);
    c = base;
    c.useDefault = true;
    c.defaults = 4;
    c.autoDns = true;
    c.auth = false;
    UsePreferences(original, c);
    c = base;
    c.useDefault = true;
    c.dropAutoDns = true; // auto_dns takes use_default's boolean_true
    UsePreferences(original, c);
    c = base;
    c.server1 = "0.0.0.0.";
    c.server2 = "0.0.0";
    c.defaults = 0;
    UsePreferences(original, c);
    // The overrides: an address (with / without gateway, subnet, DNS), an empty one, a null one, DNS alone.
    c = base;
    c.ip = "192.168.0.9";
    c.gateway = "192.168.0.1";
    c.subnet = "255.255.255.0";
    c.dns = "192.168.0.53";
    c.dhcp = true;
    UsePreferences(original, c);
    c = base;
    c.ip = "192.168.0.9";
    c.autoDns = true;
    UsePreferences(original, c);
    c = base;
    c.ip = "192.168.0.9";
    c.gateway = "";
    c.dns = "";
    UsePreferences(original, c);
    c = base;
    c.ip = "";
    c.gateway = "1.1.1.1";
    c.autoDns = true;
    c.dhcp = true;
    UsePreferences(original, c);
    c = base;
    c.ipNull = true;
    c.dns = "8.8.8.8";
    UsePreferences(original, c);
    // Each device (with and without an address of our own), and the two forcing globals.
    const uint32_t devices[] = {0x7A8EAB6D, 0xDCCC4380, 0x5D92DB35, 0x3DBCBD95, 0x221B8CA0, 0x0304EE13, 0x12345678};
    for (uint32_t device : devices) {
        for (int own = 0; own < 2; ++own) {
            c = base;
            c.device = device;
            c.ip = own ? "172.16.0.2" : nullptr;
            UsePreferences(original, c);
        }
    }
    g_maxPlayers = 3;
    c = base;
    c.device = 0x3DBCBD95;
    UsePreferences(original, c);
    c = base;
    c.forceBroadbandPc = true;
    UsePreferences(original, c);
    c = base;
    c.forcePcmcia = true;
    c.device = 0x7A8EAB6D;
    UsePreferences(original, c);
    c.ip = "172.16.0.3";
    UsePreferences(original, c);
}

// --- The game set-up's preferences, the connection ---

void GamePreferences(bool original)
{
    for (int net = 0; net < 2; ++net)
        for (int team = 0; team < 2; ++team)
            for (int answer = 0; answer < 2; ++answer) {
                g_netGame = net != 0;
                g_teamGame = team != 0;
                g_answer.clear();
                if (answer) {
                    g_answer[{0xA8774BF7, kChecksum}] = 0xAAAA0001; // team_graffiti_mode
                    g_answer[{0x38DBE1D0, Str(reinterpret_cast<uint32_t>("checksum"))}] = 0xAAAA0002; // goals
                    g_answer[{0x9834C4FA, kChecksum}] = 0xAAAA0003; // game_type
                }
                Result(2, original ? Orig<uint32_t>(0x00535AD0) : Self()->GetTeamGraffitiMode());
                Result(3, original ? Orig<uint32_t>(0x00535B20) : Self()->GetGoalsPreference());
                Result(4, original ? Orig<uint32_t>(0x00535B70) : Self()->GetGameTypePreference());
            }
}

void Connection(bool original)
{
    // Online already; going online works; fails on each connection type; with and without an online script.
    int& type = Word<int>(kNetMgr + 0x128);
    bool& online = Word<bool>(kNetMgr + offsetof(Net::Manager, m_online));
    auto connect = [&](uint32_t script, uint32_t fail) {
        Result(5, original ? Orig<uint32_t>(0x00535C10, script, fail) & 0xFF
                           : static_cast<uint32_t>(Self()->ConnectToInternet(script, fail)));
        Result(6, Word<uint32_t>(kManager + 0x1A8));
        Result(7, Word<uint32_t>(kManager + 0x1C4));
        Result(8, Word<uint32_t>(kManager + 0x1C8));
    };
    Word<uint32_t>(kManager + 0xD8) = At(kMisc + 0x500);
    online = true;
    connect(0x1234, 0x5678);
    connect(0, 0x5678);
    online = false;
    g_connects = true;
    connect(0x1234, 0x9999);
    g_connects = false;
    for (int t = 0; t <= 4; ++t) {
        type = t;
        connect(0x2222, 0x3333);
    }
    for (uint32_t script : {0u, 0x4444u}) {
        Word<uint32_t>(kManager + 0x1C8) = 0x7777;
        if (original)
            Orig<void>(0x00535CB0, script);
        else
            Self()->DisconnectFromInternet(script);
        Result(9, Word<uint32_t>(kManager + 0x1A8));
        Result(10, Word<uint32_t>(kManager + 0x1C8));
    }
}

// --- The members after them ---

void StartPoints(bool original)
{
    auto choose = [&]() {
        for (int i = 0; i < 10; ++i)
            Word<uint32_t>(kManager + 0x48 + 4 * static_cast<uint32_t>(i)) = 0xEEEE0000u + static_cast<uint32_t>(i);
        if (original)
            Orig<void>(0x005359F0);
        else
            Self()->ChooseStartPoints();
    };
    uint32_t& editorMode = Word<uint32_t>(kMisc + 0x300 + 0x40);
    g_netGame = true;
    choose();
    g_netGame = false;
    for (int players : {4, 3, 1, 0, 9}) {
        g_modeMaxPlayers = players;
        editorMode = 0xD443A2BC; // off
        choose();
        editorMode = 0x1234;
        g_horse = true;
        choose();
        g_horse = false;
        choose();
    }
}

void Toggles(bool original)
{
    for (int i = 0; i < 3; ++i) {
        if (original)
            Orig<void>(0x00535BC0);
        else
            Self()->ToggleNetMetrics();
        if (original)
            Orig<void>(0x00535BF0);
        else
            Self()->ToggleScores();
        Result(11, Word<uint32_t>(kManager + 0x44));
    }
    Word<uint32_t>(kManager + 0x44) = 0xFFFFFFFF;
    for (int i = 0; i < 2; ++i) {
        if (original)
            Orig<void>(0x00535BC0);
        else
            Self()->ToggleNetMetrics();
        if (original)
            Orig<void>(0x00535BF0);
        else
            Self()->ToggleScores();
        Result(12, Word<uint32_t>(kManager + 0x44));
    }
    for (int flags : {-1, 5, -128, 127, 0}) {
        if (original)
            Orig<void>(0x00535D90, static_cast<char>(flags));
        else
            Self()->SetProSetFlags(static_cast<char>(flags));
        Result(13, Word<uint32_t>(kManager + 0x194));
    }
}

void Scores(bool original)
{
    const uint32_t types[] = {0xBFF33600, 0xEC200EAA, 0x6C5FF266, 0xFC13B531, 0x10F84276, 0x12345678};
    for (uint32_t type : types)
        for (int flags = 0; flags < 8; ++flags) {
            g_gameType = type;
            g_accumulate = (flags & 1) != 0;
            g_combo = (flags & 2) != 0;
            g_elimiskate = (flags & 4) != 0;
            Result(14, original ? Orig<uint32_t>(0x00535D00) & 0xFF : static_cast<uint32_t>(Self()->KeepsScore()));
        }
}

void TestNetSettings()
{
    SaveGlobals({{0x006EFDD8, 4, {}}});
    RunCase("GameNet::Manager::UseNetworkPreferences", Preferences);
    RunCase("GameNet::Manager::GetTeamGraffitiMode / GetGoals / GameTypePreference", GamePreferences);
    RunCase("GameNet::Manager::ConnectToInternet / DisconnectFromInternet", Connection);
    RunCase("GameNet::Manager::ChooseStartPoints", StartPoints);
    RunCase("GameNet::Manager::ToggleNetMetrics / ToggleScores / SetProSetFlags", Toggles);
    RunCase("GameNet::Manager::KeepsScore", Scores);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetSettings, GameNetKit::Cleanup);

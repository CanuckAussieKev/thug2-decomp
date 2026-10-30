// Self-tests for the network manager's members after its tasks (src/game/net/gamenet_members.cpp): the original and
// ours on the same stand-in arena (gamenet_testkit.h), everything they call outside stood in for by recording code;
// the calls, results and arena are compared, case by case.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/task.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_manager.h"
#include "game/obj/object.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skate/cas_options.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/script/game_cfuncs.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;
using Script::CArray;
using Script::CStruct;

// Stand-in objects (in the connection's part of the arena, unused here) and the two apps (at the heap's far end).
constexpr uint32_t kNetMgr = 0x14000;    // Net::Manager (0x37C)
constexpr uint32_t kTasks = 0x14400;     // two tasks, 0x40 apart
constexpr uint32_t kSkater = 0x14500;
constexpr uint32_t kTagging = 0x14600;   // +0x18: has a custom tag texture
constexpr uint32_t kServerConn = 0x14700;
constexpr uint32_t kProfile = 0x14800;
constexpr uint32_t kTexture = 0x14900;   // its vtable pointer
constexpr uint32_t kPixels = 0x14A00;
constexpr uint32_t kPalette = 0x14B00;
constexpr uint32_t kLobby = 0x14C00;
constexpr uint32_t kMatch = 0x14D00;
constexpr uint32_t kArray = 0x14E00;     // a CArray
constexpr uint32_t kElements = 0x14E40;  // its elements
constexpr uint32_t kServerApp = 0x38000; // Net::App (0x1F08)
constexpr uint32_t kClientApp = 0x3A000;

uint32_t g_gameType;
bool g_teamGame, g_split;
uint32_t g_prefChecksum;
bool g_haveSkater, g_haveTagging, g_haveTexture;
int g_bandwidthType;
uint32_t g_parts;          // ProfileHasPart's answers, by bit: hat, front, back, deck
uint32_t g_arrayAnswer;    // GetArray's array (arena offset; 0 = none)
void* g_textureVtable[32];

// The getters' answers: (structure address, name) -> value.
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_answer;
uint32_t Answer(Regs& r, uint32_t tag)
{
    Rec(tag, {r.ecx, r.args[0], *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2] & 0xFF});
    auto it = g_answer.find({r.ecx, r.args[0]});
    if (it == g_answer.end())
        return 0u;
    *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
    return 1u;
}

const char* TextFor(uint32_t name)
{
    switch (name) {
    case 0x50589935: return "Players";       // netmenu_str_players
    case 0x8AD4F7EA: return "No Observers";  // netmenu_str_no_observers
    case 0x201D7F4E: return "Observer";      // netmenu_str_observer
    case 0x5848516E: return "Observers";     // netmenu_str_observers
    default: return "?";
    }
}

void Install(StandIns& s)
{
    InstallCommon(s);
    Stub(s, {0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, 0x32A0, [](Regs&) { return g_gameType; });
    Stub(s, {0x005535F0}, 0, 0x35F0, [](Regs&) { return g_teamGame ? 1u : 0u; });
    StubCdecl(s, {0x005A1180, AddressOf(&CFuncs::CFunc_InSplitScreenGame)}, 0x1180, 2, [](Regs&) { return g_split ? 1u : 0u; });
    StubCdecl(s, {0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0x8FD0, 1,
              [](Regs& r) { return reinterpret_cast<uint32_t>(TextFor(r.args[0])); });
    Stub(s, {0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, 0xC4A0,
         [](Regs& r) { return 0x01000000u + r.args[0] % 7; /* a structure stood in by its getters */ });
    Stub(s, {0x0046C590, AddressOf(&Prefs::Preferences::GetPreferenceChecksum)}, 8, 0xC590,
         [](Regs&) { return g_prefChecksum; });
    s.At({0x0046C5D0, AddressOf(&Prefs::Preferences::SetPreference)}, 8, [](Regs& r) {
        Rec(0xC5D0, {r.ecx, r.args[0], r.args[1]});
        return 1u;
    });
    s.At({0x00476EC0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) { return Answer(r, 0x6EC0); });
    s.At({0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) { return Answer(r, 0x6CB0); });
    s.At({0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) { return Answer(r, 0x6C60); });
    StubCdecl(s, {0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0x9070, 1,
              [](Regs&) { return g_arrayAnswer ? At(g_arrayAnswer) : 0u; });
    // The server's end.
    Stub(s, {0x0053F7E0, AddressOf(&GameNet::LobbyMan::StopHosting)}, 0, 0xF7E0);
    StubCdecl(s, {0x005FBCD0}, 0xBCD0, 0);
    Stub(s, {0x00455550, AddressOf(&Net::Manager::DestroyApp)}, 4, 0x5550);
    Stub(s, {0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, 0x68F0);
    // The custom tag texture.
    Stub(s, {0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, 0x6520, [](Regs&) { return g_haveSkater ? At(kSkater) : 0u; });
    Stub(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, 0xDB90,
         [](Regs&) { return g_haveTagging ? At(kTagging) : 0u; });
    s.At({0x00458BE0, AddressOf(&Net::App::FirstConnection)}, 4, [](Regs& r) {
        auto* search = reinterpret_cast<uint32_t*>(r.args[0]);
        Rec(0x8BE0, {r.ecx, search[0], search[1]});
        return At(kServerConn);
    });
    Stub(s, {0x004559A0, AddressOf(&Net::Conn::GetBandwidthType)}, 0, 0x59A0,
         [](Regs&) { return static_cast<uint32_t>(g_bandwidthType); });
    Stub(s, {0x00564FC0, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(int)>(&Mdl::Skate::GetProfile))}, 4, 0x4FC0, [](Regs&) { return At(kProfile); });
    StubCdecl(s, {0x00485CB0, AddressOf(&Obj::PartIsCreatedGraphic)}, 0x5CB0, 2, [](Regs& r) {
        switch (r.args[1]) {
        case 0x5B288E0B: return g_parts & 1;
        case 0x23A1DF80: return (g_parts >> 1) & 1;
        case 0xD0C656A2: return (g_parts >> 2) & 1;
        case 0x07CB4FA9: return (g_parts >> 3) & 1;
        default: return 0u;
        }
    });
    Stub(s, {0x00553E90}, 0, 0x3E90, [](Regs&) { return g_haveTexture ? At(kTexture) : 0u; });
    g_textureVtable[25] = s.Thunk(0, [](Regs& r) { Rec(0x7E25, {r.ecx}); return At(kPixels); });
    g_textureVtable[26] = s.Thunk(0, [](Regs& r) { Rec(0x7E26, {r.ecx}); return 0x12345u; });
    g_textureVtable[27] = s.Thunk(0, [](Regs& r) { Rec(0x7E27, {r.ecx}); return At(kPalette); });
    g_textureVtable[28] = s.Thunk(0, [](Regs& r) { Rec(0x7E28, {r.ecx}); return 0x10400u; });
    s.At({0x00458740, AddressOf(&Net::App::StreamMessageToConn)}, 32, [](Regs& r) {
        Rec(0x8740, {r.ecx, r.args[0], r.args[1] & 0xFF, r.args[2] & 0xFFFF, r.args[3], Str(r.args[4]), r.args[5] & 0xFF,
                     r.args[6] & 0xFF, r.args[7] & 0xFF});
        return 0u;
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        RecordDesc(0x9E50, r.ecx, reinterpret_cast<const Net::MsgDesc*>(r.args[0]));
        return 0u;
    });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_answer.clear();
        g_gameType = 0;
        g_teamGame = g_split = false;
        g_prefChecksum = 0;
        g_haveSkater = g_haveTagging = g_haveTexture = true;
        g_bandwidthType = 0;
        g_parts = 0;
        g_arrayAnswer = 0;
        Word<uint32_t>(kManager + 0x00) = 0x5A000000; // (no flags)
        Word<uint32_t>(kManager + 0x04) = At(kServerApp);
        Word<uint32_t>(kManager + 0x08) = At(kClientApp);
        Word<uint32_t>(kManager + 0x10) = At(kLobby);
        Word<uint32_t>(kManager + 0x2C) = At(kMatch);
        Word<uint32_t>(kManager + 0xCC) = At(kTasks);
        Word<uint32_t>(kManager + 0xE8) = At(kTasks + 0x40);
        Word<void**>(kTexture) = g_textureVtable;
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

void Flags(bool original)
{
    // Every mode on several starting flags; the server flag on and off; the four flag getters on 0-15.
    const uint32_t starts[] = {0x5A000000u, 0x5A0000FFu, 0x123456F3u, 0x0000000Cu};
    for (uint32_t start : starts) {
        for (int mode = -1; mode <= 3; ++mode) {
            Word<uint32_t>(kManager) = start;
            if (original)
                Orig<void>(0x00533F20, mode);
            else
                Self()->SetNetworkMode(mode);
            Result(1, Word<uint32_t>(kManager));
        }
        for (bool on : {true, false}) {
            Word<uint32_t>(kManager) = start ^ (on ? 0 : 1);
            if (original)
                Orig<void>(0x00533F50, on);
            else
                Self()->SetServerMode(on);
            Result(2, Word<uint32_t>(kManager));
        }
    }
    for (uint32_t flags = 0; flags < 16; ++flags) {
        Word<uint32_t>(kManager) = 0x5A000000u | flags;
        Result(3, original ? Orig<uint32_t>(0x00533F70) : static_cast<uint32_t>(Self()->InLanMode()));
        Result(4, original ? Orig<uint32_t>(0x00533F80) : static_cast<uint32_t>(Self()->InInternetMode()));
        Result(5, original ? Orig<uint32_t>(0x00533F90) & 0xFF : static_cast<uint32_t>(Self()->OnServer()));
        Result(6, original ? Orig<uint32_t>(0x00533FA0) : static_cast<uint32_t>(Self()->OnClient()));
    }
    for (uint32_t level : {0u, 0x12345678u, 0xFFFFFFFFu}) {
        Word<uint32_t>(kManager + 0x34) = level;
        Result(7, original ? Orig<uint32_t>(0x00534F40) : Self()->GetLevel());
    }
}

void TeamFlags(bool original)
{
    for (int teams : {0, 2, 4, -1}) {
        if (original)
            Orig<void>(0x00533FB0, teams);
        else
            Self()->CreateTeamFlags(teams);
    }
}

void CountPreferences(bool original)
{
    for (int count : {2, 8, 0, 1, -3}) {
        if (original)
            Orig<void>(0x00534090, count);
        else
            Self()->SetMaxPlayersPreference(count);
    }
    for (int count : {0, 1, 2, 7, -1}) {
        if (original)
            Orig<void>(0x005341B0, count);
        else
            Self()->SetMaxObserversPreference(count);
    }
}

void Password(bool original)
{
    static const char kSecret[] = "secret";
    // The password preference (0x01000000 + password % 7) has its ui_string, then not (the manager comes back).
    uint32_t preference = 0x01000000u + 0xCA3DB92Au % 7;
    g_answer[{preference, 0x96875C0F}] = reinterpret_cast<uint32_t>(kSecret);
    Result(8, reinterpret_cast<uint32_t>(original ? Orig<const char*>(0x00534310) : Self()->GetPassword()));
    g_answer.clear();
    Result(9, original ? Orig<uint32_t>(0x00534310) : reinterpret_cast<uint32_t>(Self()->GetPassword()));
}

void Collision(bool original)
{
    const uint32_t types[] = {0x34861A16, 0x9D65D0E7, 0x1C471C60, 0xF9D5D933, 0xCA1F360F, 0x12345678};
    for (uint32_t type : types)
        for (int over = 0; over < 2; ++over)
            for (int split = 0; split < 2; ++split)
                for (uint32_t pref : {0xF81BC89Bu, 0x11111111u}) {
                    g_gameType = type;
                    g_split = split != 0;
                    g_prefChecksum = pref;
                    Word<uint8_t>(kManager + 0x3A) = static_cast<uint8_t>(over);
                    Result(10, original ? Orig<uint32_t>(0x00534340) & 0xFF
                                        : static_cast<uint32_t>(Self()->PlayerCollisionEnabled()));
                }
    for (uint32_t type : {0x1C471C60u, 0x12345678u})
        for (int team = 0; team < 2; ++team)
            for (uint32_t pref : {0xD071112Fu, 0x22222222u}) {
                g_gameType = type;
                g_teamGame = team != 0;
                g_prefChecksum = pref;
                Result(11, original ? Orig<uint32_t>(0x005343E0) & 0xFF
                                    : static_cast<uint32_t>(Self()->ShowTeamScores()));
            }
}

void ServerEnd(bool original)
{
    // No server; a local one (internet); a remote one on the LAN; a remote one on the internet. Then the match client.
    auto destroy = [&]() {
        if (original)
            Orig<void>(0x00534CF0);
        else
            Self()->DestroyServer();
        Result(12, Word<uint32_t>(kManager));
        Result(13, Word<uint32_t>(kManager + 4));
    };
    Word<uint32_t>(kManager + 4) = 0;
    Word<uint32_t>(kManager) = 0x5A00000D;
    destroy();
    const struct {
        uint32_t flags, appFlags;
    } cases[] = {{0x5A000005, 0x08}, {0x5A000009, 0x10}, {0x5A000005, 0x17}, {0x5A000004, 0x00}};
    for (const auto& c : cases) {
        Word<uint32_t>(kManager + 4) = At(kServerApp);
        Word<uint32_t>(kManager) = c.flags;
        Word<uint32_t>(kServerApp + offsetof(Net::App, m_flags)) = c.appFlags;
        destroy();
    }
    for (int i = 0; i < 2; ++i) {
        if (original)
            Orig<void>(0x00534D50);
        else
            Self()->DestroyMatchClient();
        Result(14, Word<uint32_t>(kManager + 0x2C));
    }
}

void TagTexture(bool original)
{
    auto send = [&]() {
        Result(15, original ? Orig<uint32_t>(0x00534D70) & 0xFF : static_cast<uint32_t>(Self()->SendCustomTagTexture()));
    };
    int& connectionType = Word<int>(kNetMgr + 0x128); // Net::Manager::m_connectionType
    Word<uint8_t>(kTagging + 0x18) = 1;
    // A modem; on the server; no skater; no tagging component; no custom tag; a narrowband connection; no texture.
    connectionType = Net::Manager::kConnectionModem;
    send();
    connectionType = Net::Manager::kConnection2;
    Word<uint32_t>(kManager) = 0x5A000001;
    send();
    Word<uint32_t>(kManager) = 0x5A00000E;
    g_haveSkater = false;
    send();
    g_haveSkater = true;
    g_haveTagging = false;
    send();
    g_haveTagging = true;
    Word<uint8_t>(kTagging + 0x18) = 0;
    send();
    Word<uint8_t>(kTagging + 0x18) = 1;
    g_bandwidthType = 1;
    send();
    g_bandwidthType = 2;
    send();
    g_bandwidthType = 0;
    g_haveTexture = false;
    g_parts = 0xF;
    send();
    g_haveTexture = true;
    // Sent, with each set of logos.
    for (uint32_t parts : {0x0u, 0x1u, 0x2u, 0x4u, 0x8u, 0xAu, 0xFu}) {
        g_parts = parts;
        send();
    }
}

void Choices(bool original)
{
    // Entries at kElements (8 bytes apart, stand-in structures answered by g_answer).
    auto entry = [](int i) { return At(kElements + 0x40 + 8 * static_cast<uint32_t>(i)); };
    auto list = [&](std::initializer_list<int> entries) {
        auto* array = &Word<CArray>(kArray);
        std::memset(array, 0, sizeof(CArray));
        array->m_type = Script::ESYMBOLTYPE_STRUCTURE;
        array->m_size = static_cast<uint32_t>(entries.size());
        int n = 0;
        for (int i : entries)
            Word<uint32_t>(kElements + 4 * static_cast<uint32_t>(n++)) = entry(i);
        if (n == 1)
            array->m_single = Word<uint32_t>(kElements);
        else
            array->m_elements = &Word<uint32_t>(kElements);
        g_arrayAnswer = kArray;
    };
    auto name = [&](uint32_t listName, uint32_t choice) {
        Result(16, original ? Orig<uint32_t>(0x00534F50, listName, choice)
                            : reinterpret_cast<uint32_t>(Self()->GetChoiceName(listName, choice)));
    };
    auto integer = [&](uint32_t listName, uint32_t choice, uint32_t field) {
        Result(17, original ? Orig<uint32_t>(0x00534FC0, listName, choice, field)
                            : static_cast<uint32_t>(Self()->GetChoiceInteger(listName, choice, field)));
    };
    const uint32_t kName = 0xA1DC81F9, kChecksum = 0x21902065, kField = 0x7777;
    for (int i = 0; i < 6; ++i) {
        g_answer[{entry(i), kChecksum}] = 0xC0 + static_cast<uint32_t>(i % 4); // entries 4 and 5 repeat 0 and 1
        g_answer[{entry(i), kName}] = At(kPixels + 0x10 * static_cast<uint32_t>(i));
        g_answer[{entry(i), kField}] = 100 + static_cast<uint32_t>(i);
    }
    g_answer.erase({entry(2), kName});
    g_answer.erase({entry(3), kField});
    list({0, 1, 2, 3});
    for (uint32_t choice : {0xC0u, 0xC1u, 0xC2u, 0xC3u, 0xC9u}) {
        name(0x5150, choice);
        integer(0x5150, choice, kField);
    }
    list({0, 1, 2, 3, 4, 5}); // two matches each for 0xC0 / 0xC1: the last one's
    name(0x5150, 0xC0);
    integer(0x5150, 0xC1, kField);
    list({1}); // one element
    name(0x5150, 0xC1);
    integer(0x5150, 0xC1, kField);
    list({});
    name(0x5150, 0xC1);
    integer(0x5150, 0xC1, kField);
    // An entry without a checksum: it compares what the slot held - the list's name / the choice itself.
    g_answer.erase({entry(1), kChecksum});
    list({1, 0});
    name(0x5150, 0x5150);
    name(0x5150, 0xC0);
    integer(0x5150, 0xC2, kField);
    list({0, 1});
    name(0x5150, 0xC0); // (entry 1 then compares entry 0's checksum: 0xC0 again)
    integer(0x5150, 0xC0, kField);
}

void TestNetMembers()
{
    static_assert(sizeof(Net::Manager) <= 0x400, "the stand-in Net::Manager's room");
    SaveGlobals({{0x006EFDD8, 4, {}}});
    RunCase("GameNet::Manager flags and level", Flags);
    RunCase("GameNet::Manager::CreateTeamFlags", TeamFlags);
    RunCase("GameNet::Manager::SetMaxPlayers / ObserversPreference", CountPreferences);
    RunCase("GameNet::Manager::GetPassword", Password);
    RunCase("GameNet::Manager::PlayerCollisionEnabled / ShowTeamScores", Collision);
    RunCase("GameNet::Manager::DestroyServer / DestroyMatchClient", ServerEnd);
    RunCase("GameNet::Manager::SendCustomTagTexture", TagTexture);
    RunCase("GameNet::Manager::GetChoiceName / GetChoiceInteger", Choices);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetMembers, GameNetKit::Cleanup);

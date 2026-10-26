// Self-tests for the buddy list's GP requests, messages and status, its list types and the folded getters
// (src/game/net/buddy_status.cpp): the original and ours run on the same stand-in arena (gamenet_testkit.h), GP and
// everything else stood in for by recording code; the calls, the results and the arena are compared. Nothing goes
// online.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/net/buddy_man.h"
#include "game/net/buddy_status.h"
#include "game/net/gamenet_manager.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::BuddyMan;

constexpr uint32_t kBuddies = 0x14000; // the BuddyMan (0x3C)
constexpr uint32_t kLobby = 0x14100;   // the LobbyMan: +0x34 its name
constexpr uint32_t kPrefs = 0x14300;   // stand-in structures; texts at +0x40
constexpr uint32_t kScript = 0x14400;  // a script and its parameters
constexpr uint32_t kNodes = 0x14600;   // list nodes, 0x20 apart

uint32_t g_hasText;
uint32_t g_textNext;
int g_profile;

BuddyMan* Buddies() { return &Word<BuddyMan>(kBuddies); }
uint32_t Text(uint32_t text) { return text ? Hash(reinterpret_cast<const char*>(text)) : 0xDEAD0000u; }

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x004758F0, AddressOf(static_cast<void (*)(const char*, Script::CStruct*, Obj::CObject*, bool)>(&Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x58F0, {Text(r.args[0]), r.args[1], r.args[2], r.args[3] & 0xFF});
             return 0u;
         });
    s.At({0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, [](Regs& r) {
        Rec(0xC4A0, {r.ecx, r.args[0]});
        return At(kPrefs);
    });
    s.At({0x0046C510, AddressOf(&Prefs::Preferences::GetPreferenceString)}, 8, [](Regs& r) {
        Rec(0xC510, {r.ecx, r.args[0], r.args[1]});
        char* text = &Word<char>(kPrefs + 0x40 + (r.args[0] & 0x30));
        std::snprintf(text, 16, "pref%08X", r.args[0]);
        return reinterpret_cast<uint32_t>(text);
    });
    auto getString = [](Regs& r) {
        Rec(0x6590, {r.ecx, r.args[0], r.args[2] & 0xFF});
        if (!g_hasText)
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = g_hasText == 1 ? At(kPrefs + 0x80) : At(kPrefs + 0x90);
        return 1u;
    };
    s.At({0x00476590, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, const char**, int)>(&Script::CStruct::GetString))},
         12, getString);
    s.At({0x00476C60}, 12, getString);
    s.At({0x00476680, AddressOf(static_cast<bool (Script::CStruct::*)(const char*, int*, int)>(&Script::CStruct::GetInteger))},
         12, [](Regs& r) {
             Rec(0x6680, {r.ecx, Text(r.args[0]), r.args[2]});
             *reinterpret_cast<int*>(r.args[1]) = g_profile;
             return 1u;
         });
    // Texts by name, each in its own buffer.
    s.At({0x00478FF0, AddressOf(static_cast<const char* (*)(const char*)>(&Script::GetString))}, 0, [](Regs& r) {
        uint32_t next = g_textNext++;
        uint32_t h = Hash(reinterpret_cast<const char*>(r.args[0]));
        Rec(0x8FF0, {h});
        char* text = &Word<char>(kMisc + 0xE00 + 0x20 * (next & 3));
        std::snprintf(text, 32, "text-%08X", h);
        return reinterpret_cast<uint32_t>(text);
    });
    // GP.
    StubCdecl(s, {0x006001A0}, 0x01A0, 2);
    s.At({0x005FFDE0}, 0, [](Regs& r) {
        uint32_t callback = r.args[6] == reinterpret_cast<uint32_t>(&BuddyMan::OnConnectNewUser) ||
                                    r.args[6] == AddressOf(&BuddyMan::OnConnectNewUser)
                                ? 0x0054A2C0u
                                : r.args[6];
        Rec(0xFDE0, {r.args[0], Text(r.args[1]), Text(r.args[2]), Text(r.args[3]), r.args[4], r.args[5], callback, r.args[7]});
        return 0u;
    });
    s.At({0x006005D0}, 0, [](Regs& r) {
        Rec(0x05D0, {r.args[0], r.args[1], Text(r.args[2])});
        return 0u;
    });
    s.At({0x00600330}, 0, [](Regs& r) {
        Rec(0x0330, {r.args[0], r.args[1], Text(r.args[2]), Text(r.args[3])});
        return 0u;
    });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_hasText = 1;
        g_textNext = 0;
        g_profile = 4321;
        for (uint32_t i = 0x800; i < 0xDF0; i += 4)
            Word<uint32_t>(kMisc + i) = 0x6D000000u + i; // the memory manager's fields told apart
        Word<uint32_t>(kManager + 0x10) = At(kLobby);
        Word<uint32_t>(kManager + 0x18) = At(kBuddies);
        std::memset(Buddies(), 0x42, sizeof(BuddyMan));
        std::memcpy(&Word<char>(kLobby + 0x34), "Lobbyname", 10);
        std::memcpy(&Word<char>(kPrefs + 0x80), "Hello there", 12);
        std::memcpy(&Word<char>(kPrefs + 0x90), "", 1);
        std::memcpy(reinterpret_cast<void*>(0x007CCA90), "cmd@mail", 9);
        std::memcpy(reinterpret_cast<void*>(0x007CCB90), "cmdpass", 8);
        GameVariable<uint8_t>(0x007CC260) = 0;
        GameVariable<uint8_t>(0x007CCA80) = 0;
        Word<uint32_t>(kScript + 0x14) = At(kScript + 0x40);
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

template <typename R, typename C, typename... P, typename... A>
void CallRec(bool original, uint32_t address, R (C::*ours)(P...), void* object, A... args)
{
    R result = original ? Original<R(__thiscall*)(void*, P...)>(address)(object, static_cast<P>(args)...)
                        : (static_cast<C*>(object)->*ours)(static_cast<P>(args)...);
    uint32_t word = 0;
    std::memcpy(&word, &result, sizeof(result) < 4 ? sizeof(result) : 4);
    if (sizeof(result) == 1)
        word &= 0xFF;
    Rec(0xCA11, {address, word});
}
template <typename C, typename... P, typename... A>
void CallVoid(bool original, uint32_t address, void (C::*ours)(P...), void* object, A... args)
{
    if (original)
        Original<void(__thiscall*)(void*, P...)>(address)(object, static_cast<P>(args)...);
    else
        (static_cast<C*>(object)->*ours)(static_cast<P>(args)...);
    Rec(0xCA11, {address});
}
using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
void RunCFunc(bool original, uint32_t address, CFunc ours)
{
    auto* params = &Word<Script::CStruct>(kScript + 0x80);
    auto* script = &Word<Script::CScript>(kScript);
    bool result = original ? Original<CFunc>(address)(params, script) : ours(params, script);
    Rec(0xCF00, {address, result ? 1u : 0u});
}

void Presence(bool original)
{
    int request[2] = {777, 0};
    using Request = void(__cdecl*)(void*, const void*, BuddyMan*);
    original ? Original<Request>(0x00548100)(Buddies(), request, Buddies())
             : BuddyMan::OnBuddyRequest(Buddies(), request, Buddies());
    using Thread = unsigned long(__stdcall*)(void*);
    for (int mode = 0; mode < 3; ++mode) {
        GameVariable<uint8_t>(0x007CC260) = mode == 1;
        GameVariable<uint8_t>(0x007CCA80) = mode == 2;
        unsigned long r = original ? Original<Thread>(0x00548140)(Buddies()) : BuddyMan::LoginThread(Buddies());
        Rec(0x8140, {r});
    }
    CallVoid(original, 0x00548200, &BuddyMan::SendBuddyMessage, Buddies(), 55, "how are you");
    CallVoid(original, 0x005482A0, &BuddyMan::SetLoggedIn, Buddies(), 1234);
    Rec(0x10, {Word<uint8_t>(kBuddies + 0x10), static_cast<uint32_t>(Buddies()->m_profile), static_cast<uint32_t>(Buddies()->m_state)});
    CallVoid(original, 0x00548320, &BuddyMan::SetStatus, Buddies(), 3, "status text", "somewhere");
    for (int loggedIn : {0, 1, 2}) {
        Buddies()->m_loggedIn = loggedIn != 0;
        reinterpret_cast<uint8_t&>(Buddies()->m_loggedIn) = static_cast<uint8_t>(loggedIn);
        RunCFunc(original, 0x00548360, GameNet::ProfileLoggedIn);
    }
    for (uint32_t has : {0u, 2u, 1u}) {
        g_hasText = has;
        RunCFunc(original, 0x00548370, GameNet::SendBuddyMessage);
    }
    RunCFunc(original, 0x005483E0, GameNet::SetLobbyStatus);
}

void ListTypes(bool original)
{
    // Searches: the vtable reset (freed with flag 1).
    for (uint32_t i = 0; i < 4; ++i)
        Word<uint32_t>(kNodes + 0x10 * i) = 0x11111111u * (i + 1);
    CallVoid(original, 0x005480D0, &GameNet::BuddySearch::DestroyBody, &Word<uint8_t>(kNodes));
    CallRec(original, 0x00548440, &GameNet::BuddySearch::Destroy, &Word<uint8_t>(kNodes + 0x10), 1u);
    CallVoid(original, 0x005480E0, &GameNet::BuddyMessageSearch::DestroyBody, &Word<uint8_t>(kNodes + 0x20));
    CallRec(original, 0x00548470, &GameNet::BuddyMessageSearch::Destroy, &Word<uint8_t>(kNodes + 0x30), 0u);
    // Nodes in a ring of six; each destructor takes one out.
    const uint32_t ring = kNodes + 0x100;
    for (uint32_t i = 0; i < 6; ++i) {
        uint32_t node = ring + 0x20 * i, following = ring + 0x20 * ((i + 1) % 6);
        Word<uint32_t>(node) = 0x22222222;
        Word<uint32_t>(node + 0x0C) = At(following);
        Word<uint32_t>(following + 0x10) = At(node);
    }
    CallVoid(original, 0x005484A0, &GameNet::BuddyNode::DestroyBody, &Word<uint8_t>(ring + 0x20));
    CallRec(original, 0x005484E0, &GameNet::BuddyNode::Destroy, &Word<uint8_t>(ring + 0x40), 1u);
    CallVoid(original, 0x005484C0, &GameNet::BuddyMessageNode::DestroyBody, &Word<uint8_t>(ring + 0x60));
    CallRec(original, 0x00548520, &GameNet::BuddyMessageNode::Destroy, &Word<uint8_t>(ring + 0x80), 0u);
    CallRec(original, 0x00548520, &GameNet::BuddyMessageNode::Destroy, &Word<uint8_t>(ring + 0xA0), 1u);
    // The folded getters.
    for (uint32_t i = 0; i < 0x20; ++i)
        Word<uint8_t>(kNodes + 0x200 + i) = static_cast<uint8_t>(0x80 + 7 * i);
    CallRec(original, 0x005480B0, &GameNet::FoldedGetters::Byte10, &Word<uint8_t>(kNodes + 0x200));
    CallRec(original, 0x005480C0, &GameNet::FoldedGetters::Word04, &Word<uint8_t>(kNodes + 0x200));
    CallRec(original, 0x005480F0, &GameNet::FoldedGetters::Word18, &Word<uint8_t>(kNodes + 0x200));
}

void TestNetBuddyStatus()
{
    SaveGlobals({{0x007CCA90, 0x100, {}}, {0x007CCB90, 0x100, {}}, {0x007CC260, 1, {}}, {0x007CCA80, 1, {}}});
    RunCase("GameNet::BuddyMan: requests, login, messages, status", Presence);
    RunCase("GameNet: the buddy lists' types and the folded getters", ListTypes);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetBuddyStatus, GameNetKit::Cleanup);

// Self-tests for the GameSpy lobby (src/game/net/lobby.cpp): the original and ours run on the same stand-in arena
// (gamenet_testkit.h), the GameSpy SDK and everything else they call stood in for by recording code (callbacks handed
// to the SDK recorded by their game address); the calls, the results and the arena are compared. Nothing goes online.

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/task.h"
#include "game/net/gamenet_manager.h"
#include "game/net/lobby.h"
#include "game/net/online_identity.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::LobbyInfo;
using GameNet::LobbyMan;
using GameNet::LobbyPlayer;

constexpr uint32_t kLobby = 0x14000;    // the LobbyMan (0x154)
constexpr uint32_t kBuddies = 0x14400;  // the buddy list: +0x04 our profile, +0x10 logged in
constexpr uint32_t kStats = 0x14500;    // the stats manager (its 0x0054CA80 answer at +0x10)
constexpr uint32_t kLobbies = 0x14600;  // LobbyInfo, 0x80 apart (4)
constexpr uint32_t kScript = 0x14900;   // a script: +0x14 its parameters (kScript + 0x40)
constexpr uint32_t kText = 0x14A00;     // strings handed in (nicks, keys), 0x20 apart
constexpr uint32_t kTasks = 0x14E00;    // (task stand-ins)

std::map<uint32_t, uint32_t> g_ours; // our callbacks -> their game addresses
uint32_t g_profile;
int g_failAllocations;
int g_index;
uint32_t g_isBuddy;

const char* TextAt(int i) { return &Word<char>(kText + 0x20 * static_cast<uint32_t>(i)); }
char* SetText(int i, const char* text)
{
    char* at = &Word<char>(kText + 0x20 * static_cast<uint32_t>(i));
    std::memcpy(at, text, std::strlen(text) + 1);
    return at;
}
uint32_t Code(uint32_t address)
{
    auto it = g_ours.find(address);
    return it == g_ours.end() ? Rel(address) : it->second;
}
uint32_t Text(uint32_t text) { return text ? Hash(reinterpret_cast<const char*>(text)) : 0xDEAD0000u; }
LobbyMan* Lobby() { return &Word<LobbyMan>(kLobby); }

void Install(StandIns& s)
{
    InstallCommon(s);
    g_ours = {{AddressOf(&LobbyMan::PlayerJoinedCallback), 0x00541D50},
              {AddressOf(&LobbyMan::PlayerLeftCallback), 0x005420C0},
              {AddressOf(&LobbyMan::PlayerInfoCallback), 0x00541FD0},
              {AddressOf(&LobbyMan::NewPlayerListCallback), 0x00542B70},
              {AddressOf(&LobbyMan::RoomKeyChangedCallback), 0x00542650},
              {AddressOf(&LobbyMan::RoomKeysCallback), 0x00542330},
              {AddressOf(&LobbyMan::EnumPlayersCallback), 0x005424F0},
              {AddressOf(&LobbyMan::JoinRoomCallback), 0x00542B80}};
    // (and the function pointers as the code takes them: maybe an incremental-linking jump)
    const std::pair<uint32_t, uint32_t> raw[] = {
        {reinterpret_cast<uint32_t>(&LobbyMan::PlayerJoinedCallback), 0x00541D50},
        {reinterpret_cast<uint32_t>(&LobbyMan::PlayerLeftCallback), 0x005420C0},
        {reinterpret_cast<uint32_t>(&LobbyMan::PlayerInfoCallback), 0x00541FD0},
        {reinterpret_cast<uint32_t>(&LobbyMan::NewPlayerListCallback), 0x00542B70},
        {reinterpret_cast<uint32_t>(&LobbyMan::RoomKeyChangedCallback), 0x00542650},
        {reinterpret_cast<uint32_t>(&LobbyMan::RoomKeysCallback), 0x00542330},
        {reinterpret_cast<uint32_t>(&LobbyMan::EnumPlayersCallback), 0x005424F0},
        {reinterpret_cast<uint32_t>(&LobbyMan::JoinRoomCallback), 0x00542B80}};
    for (const auto& entry : raw)
        g_ours[entry.first] = entry.second;
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.args[0], r.args[1] & 0xFF, r.args[2]});
        if (g_failAllocations > 0) {
            --g_failAllocations;
            return 0u;
        }
        uint32_t block = At(g_heapNext);
        g_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xCD, r.args[0]);
        return block;
    });
    // The C runtime the original calls (not started offline).
    s.At({0x00626EFB}, 0, [](Regs& r) {
        return static_cast<uint32_t>(_stricmp(reinterpret_cast<const char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x00624E52}, 0, [](Regs& r) { return static_cast<uint32_t>(std::atol(reinterpret_cast<const char*>(r.args[0]))); });
    // Script structures by name.
    s.At({0x00477A90, AddressOf(static_cast<void (Script::CStruct::*)(const char*, const char*)>(&Script::CStruct::AddString))},
         8, [](Regs& r) {
             Rec(0x7A90, {r.ecx, Text(r.args[0]), Text(r.args[1])});
             return 0u;
         });
    s.At({0x00477DF0, AddressOf(static_cast<void (Script::CStruct::*)(const char*, uint32_t)>(&Script::CStruct::AddChecksum))},
         8, [](Regs& r) {
             Rec(0x7DF0, {r.ecx, Text(r.args[0]), r.args[1]});
             return 0u;
         });
    s.At({0x00476630, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(&Script::CStruct::GetInteger))},
         12, [](Regs& r) {
             Rec(0x6630, {r.ecx, r.args[0], r.args[2]});
             *reinterpret_cast<int*>(r.args[1]) = g_index;
             return 1u;
         });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
        Rec(0x8FD0, {r.args[0]});
        char* text = &Word<char>(kMisc + 0xE40);
        std::snprintf(text, 32, "<%08X>", r.args[0]);
        return reinterpret_cast<uint32_t>(text);
    });
    // The lobby's other code (not in this file).
    Stub(s, {0x005411E0}, 4, 0x11E0);
    Stub(s, {0x00540900}, 4, 0x0900);
    Stub(s, {0x00540FB0}, 8, 0x0FB0);
    Stub(s, {0x00540860}, 0, 0x0860);
    Stub(s, {0x00540750}, 0, 0x0750);
    Stub(s, {0x00549CB0}, 4, 0x9CB0, [](Regs& r) { return (g_isBuddy >> (r.args[0] & 31)) & 1; });
    Stub(s, {0x005453E0}, 4, 0x53E0);
    Stub(s, {0x00548320}, 12, 0x8320, [](Regs& r) {
        Rec(0x8321, {Text(r.args[1]), Text(r.args[2])});
        return 0u;
    });
    Stub(s, {0x0054CA80}, 0, 0xCA80, [](Regs&) { return At(kStats + 0x10); });
    Stub(s, {0x00406890, AddressOf(&Tsk::BaseTask::Construct)}, 4, 0x6890, [](Regs& r) { return r.ecx; });
    Stub(s, {0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, 0x68F0);
    Stub(s, {0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, 0x6C90);
    StubCdecl(s, {0x0053F520}, 0xF520, 1, [](Regs&) { return At(kManager); });
    StubCdecl(s, {0x005395C0}, 0x95C0, 0);
    // The GameSpy SDK.
    StubCdecl(s, {0x005FF660}, 0xF660, 1, [](Regs& r) {
        Rec(0xF661, {Text(r.args[1])});
        return 0u;
    });
    s.At({0x005FCBA0}, 0, [](Regs& r) {
        Rec(0xCBA0, {r.args[0], Text(r.args[1]), r.args[2], r.args[3]});
        *reinterpret_cast<uint32_t*>(r.args[3]) = ++g_profile;
        return 1u;
    });
    s.At({0x005FCE60}, 0, [](Regs& r) {
        const uint32_t* callbacks = reinterpret_cast<const uint32_t*>(r.args[0]);
        g_trace.push_back(0xCE60);
        for (int i = 0; i < 0x1D; ++i)
            g_trace.push_back(Code(callbacks[i]));
        return At(kMisc + 0xA00); // the peer
    });
    StubCdecl(s, {0x005FD3E0}, 0xD3E0, 1, [](Regs& r) {
        Rec(0xD3E1, {Text(r.args[1]), Code(r.args[2]), r.args[3], r.args[4]});
        return 0u;
    });
    StubCdecl(s, {0x005FCA40}, 0xCA40, 1);
    StubCdecl(s, {0x005FCB10}, 0xCB10, 2, [](Regs& r) {
        Rec(0xCB11, {Code(r.args[2]), r.args[3]});
        return 0u;
    });
    StubCdecl(s, {0x005FCCA0}, 0xCCA0, 1);
    StubCdecl(s, {0x005FC7B0}, 0xC7B0, 1);
    StubCdecl(s, {0x005FD670}, 0xD670, 1);
    StubCdecl(s, {0x005FD220}, 0xD220, 2, [](Regs& r) {
        Rec(0xD221, {Code(r.args[2]), r.args[3], r.args[4]});
        return 0u;
    });
    StubCdecl(s, {0x005FC920}, 0xC920, 1, [](Regs&) { return reinterpret_cast<uint32_t>(SetText(7, "OurNick")); });
    StubCdecl(s, {0x005FCDF0}, 0xCDF0, 2, [](Regs& r) {
        char** keys = reinterpret_cast<char**>(r.args[4]);
        char** values = reinterpret_cast<char**>(r.args[5]);
        Rec(0xCDF1, {Text(r.args[2]), r.args[3], Text(reinterpret_cast<uint32_t>(keys[0])), Text(reinterpret_cast<uint32_t>(values[0]))});
        return 0u;
    });
    StubCdecl(s, {0x005FD4A0}, 0xD4A0, 2, [](Regs& r) {
        char** keys = reinterpret_cast<char**>(r.args[4]);
        Rec(0xD4A1, {Text(r.args[2]), r.args[3], Text(reinterpret_cast<uint32_t>(keys[0])), Code(r.args[5]), r.args[6], r.args[7]});
        return 0u;
    });
    StubCdecl(s, {0x005FCF30}, 0xCF30, 1, [](Regs& r) {
        // (the game name: the original's own text or the configured one)
        auto name = [](uint32_t text) {
            const char* p = reinterpret_cast<const char*>(text);
            return static_cast<uint32_t>(std::strcmp(p, "thps6pc") == 0 || std::strcmp(p, Net::GameName()) == 0);
        };
        const uint32_t* pings = reinterpret_cast<const uint32_t*>(r.args[8]);
        const uint32_t* crossPings = reinterpret_cast<const uint32_t*>(r.args[9]);
        Rec(0xCF31, {name(r.args[1]), Text(r.args[2]), name(r.args[3]), Text(r.args[4]), r.args[5], r.args[6], r.args[7],
                     pings[0], pings[1], pings[2], crossPings[0], crossPings[1], crossPings[2]});
        return 1u;
    });
    StandInApi(s, 0x006450C8, reinterpret_cast<const void*>(&CreateThread), 24, [](Regs& r) {
        Rec(0x7EAD, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        *reinterpret_cast<DWORD*>(r.args[5]) = 0x1234;
        return 0x5678u;
    });
    StandInApi(s, 0x006450E0, reinterpret_cast<const void*>(&ResumeThread), 4, [](Regs& r) {
        Rec(0x8E5E, {r.args[0]});
        return 0u;
    });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_profile = 100;
        g_failAllocations = 0;
        g_index = 0;
        g_isBuddy = 0;
        Word<uint32_t>(kManager + 0x10) = At(kLobby);
        Word<uint32_t>(kManager + 0x18) = At(kBuddies);
        Word<uint32_t>(kManager + 0x1C) = At(kStats);
        Word<int>(kBuddies + 0x04) = 103;
        Word<uint8_t>(kBuddies + 0x10) = 1;
        Word<int>(kStats + 0x10) = 1750;
        Word<uint32_t>(kScript + 0x14) = At(kScript + 0x40);
        for (uint32_t i = 0x800; i < 0xDF0; i += 4)
            Word<uint32_t>(kMisc + i) = 0x6D000000u + i; // the memory manager's fields told apart
        std::memset(Lobby(), 0xEE, sizeof(LobbyMan));
        for (uint32_t i = 0; i < 0x154; i += 4)
            Word<uint32_t>(kLobby + i) = 0x4C000000u + i;
        for (uint32_t i = 0; i < 4; ++i)
            Word<uint32_t>(kTasks + 0x40 * i) = 0x7A5C0000u + i;
        GameVariable<uint32_t>(0x00696D68) = At(kMisc + 0x400);
        GameVariable<uint32_t>(0x007CE430) = 0x11111111;
        GameVariable<uint16_t>(0x007CCE38) = 0x2222;
        StandIns* s = new StandIns;
        Install(*s);
        scenario(original);
        Rec(0xC10B, {GameVariable<uint32_t>(0x007CE430), GameVariable<uint16_t>(0x007CCE38)});
        delete s;
        return Finish();
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
    for (size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        if (expected[i] == actual[i])
            continue;
        for (size_t j = i > 24 ? i - 24 : 0; j < i + 4 && j < expected.size() && j < actual.size(); ++j)
            Log("    [%u] %08X %08X", static_cast<unsigned>(j), expected[j], actual[j]);
        break;
    }
}

template <typename R, typename C, typename... P, typename... A>
void CallRec(bool original, uint32_t address, R (C::*ours)(P...), void* object, A... args)
{
    R result = original ? Original<R(__thiscall*)(void*, P...)>(address)(object, static_cast<P>(args)...)
                        : (static_cast<C*>(object)->*ours)(static_cast<P>(args)...);
    uint32_t word = 0;
    std::memcpy(&word, &result, sizeof(result) < 4 ? sizeof(result) : 4);
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
template <typename F, typename... A>
void CallStatic(bool original, uint32_t address, F ours, A... args)
{
    original ? Original<F>(address)(args...) : ours(args...);
    Rec(0xCA12, {address});
}
using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
void RunCFunc(bool original, uint32_t address, CFunc ours)
{
    auto* params = &Word<Script::CStruct>(kScript + 0x80);
    auto* script = &Word<Script::CScript>(kScript);
    bool result = original ? Original<CFunc>(address)(params, script) : ours(params, script);
    Rec(0xCF00, {address, result ? 1u : 0u});
}

// The lobby made empty (the original's constructor), three lobbies to choose from.
void Prepare(bool original)
{
    CallRec(original, 0x00541870, &LobbyMan::Construct, Lobby());
    Lobby()->m_peer = &Word<uint8_t>(kMisc + 0xA00);
    for (uint32_t i = 0; i < 3; ++i) {
        auto* lobby = &Word<LobbyInfo>(kLobbies + 0x80 * i);
        std::memset(lobby, 0x5D, 0x80);
        CallRec(original, 0x00545860, &LobbyInfo::Construct, lobby);
        std::snprintf(lobby->m_name, 64, "Lobby %u", i);
        lobby->m_groupId = 200 + static_cast<int>(i);
        lobby->m_unknown58 = 10 + static_cast<int>(i);
        lobby->m_unknown5C = 20 + static_cast<int>(i);
        lobby->m_unknown6A = i == 1;
        lobby->prev = Lobby()->m_lobbies.prev;
        lobby->next = &Lobby()->m_lobbies;
        Lobby()->m_lobbies.prev->next = lobby;
        Lobby()->m_lobbies.prev = lobby;
    }
}

void Construction(bool original)
{
    Prepare(original);
    std::memset(&Word<uint8_t>(kLobbies + 0x180), 0x5D, 0x80);
    CallRec(original, 0x00545860, &LobbyInfo::Construct, &Word<LobbyInfo>(kLobbies + 0x180));
    g_failAllocations = 1; // the first task not made
    CallRec(original, 0x00541870, &LobbyMan::Construct, &Word<uint8_t>(kMisc + 0xB00));
    CallVoid(original, 0x00542DF0, &LobbyMan::InitPeer, Lobby());
    CallVoid(original, 0x00541A10, &LobbyMan::StartListingGames, Lobby());
    for (int i : {0, 1, 2, 3, -1})
        CallRec(original, 0x00542970, &LobbyMan::GetLobby, Lobby(), i);
    CallVoid(original, 0x005428B0, &LobbyMan::FillLobbyList, Lobby());
    CallVoid(original, 0x005427E0, &LobbyMan::ClearLobbies, Lobby());
    CallRec(original, 0x00542970, &LobbyMan::GetLobby, Lobby(), 0);
    Lobby()->m_peer = nullptr;
    CallVoid(original, 0x005427E0, &LobbyMan::ClearLobbies, Lobby());
    CallRec(original, 0x00541AC0, &LobbyMan::Destroy, &Word<uint8_t>(kMisc + 0xB00), 0u);
    CallRec(original, 0x00541AC0, &LobbyMan::Destroy, &Word<uint8_t>(kMisc + 0xB00), 1u);
}

void Players(bool original)
{
    Prepare(original);
    void* peer = Lobby()->m_peer;
    using Joined = void(__cdecl*)(void*, int, const char*, LobbyMan*);
    using Left = void(__cdecl*)(void*, int, const char*, const char*, LobbyMan*);
    using Info = void(__cdecl*)(void*, int, const char*, uint32_t, int, LobbyMan*);
    using Enum = void(__cdecl*)(void*, int, int, int, const char*, int, LobbyMan*);
    using KeyChanged = void(__cdecl*)(void*, int, const char*, const char*, const char*, LobbyMan*);
    using Keys = void(__cdecl*)(void*, int, int, const char*, int, char**, char**, LobbyMan*);
    using NewList = void(__cdecl*)(void*, int, LobbyMan*);
    // Enumerated: two new, one twice, failures ignored.
    CallStatic<Enum>(original, 0x005424F0, &LobbyMan::EnumPlayersCallback, peer, 1, 1, 0, SetText(0, "Alpha"), 0, Lobby());
    CallStatic<Enum>(original, 0x005424F0, &LobbyMan::EnumPlayersCallback, peer, 1, 1, 1, SetText(1, "beta"), 0, Lobby());
    CallStatic<Enum>(original, 0x005424F0, &LobbyMan::EnumPlayersCallback, peer, 1, 1, 2, SetText(2, "ALPHA"), 0, Lobby());
    CallStatic<Enum>(original, 0x005424F0, &LobbyMan::EnumPlayersCallback, peer, 0, 1, 3, SetText(3, "Gamma"), 0, Lobby());
    CallStatic<Enum>(original, 0x005424F0, &LobbyMan::EnumPlayersCallback, peer, 1, 1, -1, TextAt(3), 0, Lobby());
    RunCFunc(original, 0x00542AA0, GameNet::CFunc_FillPlayerList); // (the enumerated ones aren't listed yet)
    // Joined: one known (listed now), one new.
    CallStatic<Joined>(original, 0x00541D50, &LobbyMan::PlayerJoinedCallback, peer, 1, SetText(4, "BETA"), Lobby());
    CallStatic<Joined>(original, 0x00541D50, &LobbyMan::PlayerJoinedCallback, peer, 1, SetText(5, "Delta"), Lobby());
    // Info: unknown, known with a profile, a null nick.
    CallStatic<Info>(original, 0x00541FD0, &LobbyMan::PlayerInfoCallback, peer, 1, SetText(6, "Nobody"), 0x0A000001u, 55, Lobby());
    CallStatic<Info>(original, 0x00541FD0, &LobbyMan::PlayerInfoCallback, peer, 1, TextAt(0), 0x0A000002u, 66, Lobby());
    CallStatic<Info>(original, 0x00541FD0, &LobbyMan::PlayerInfoCallback, peer, 1, static_cast<const char*>(nullptr), 0u, 77, Lobby());
    Word<LobbyPlayer>(g_heapNext - 0x60).m_profileId = 0; // (the last one added: no profile)
    CallStatic<Info>(original, 0x00541FD0, &LobbyMan::PlayerInfoCallback, peer, 1, TextAt(5), 0x0A000003u, 88, Lobby());
    // Ratings.
    CallStatic<KeyChanged>(original, 0x00542650, &LobbyMan::RoomKeyChangedCallback, peer, 1, TextAt(1), SetText(8, "x"), SetText(9, "1999"), Lobby());
    CallStatic<KeyChanged>(original, 0x00542650, &LobbyMan::RoomKeyChangedCallback, peer, 1, TextAt(5), TextAt(8), SetText(10, "999999"), Lobby());
    CallStatic<KeyChanged>(original, 0x00542650, &LobbyMan::RoomKeyChangedCallback, peer, 1, TextAt(6), TextAt(8), TextAt(9), Lobby());
    char* keys[1] = {SetText(11, "b_rating")};
    char* values[1] = {SetText(12, "3500")};
    CallStatic<Keys>(original, 0x00542330, &LobbyMan::RoomKeysCallback, peer, 1, 1, TextAt(0), 1, keys, values, Lobby());
    CallStatic<Keys>(original, 0x00542330, &LobbyMan::RoomKeysCallback, peer, 1, 1, TextAt(0), 0, keys, values, Lobby());
    char* other[1] = {SetText(13, "c_other")};
    CallStatic<Keys>(original, 0x00542330, &LobbyMan::RoomKeysCallback, peer, 1, 1, TextAt(0), 1, other, values, Lobby());
    char* unknown[1] = {SetText(15, "zzz")};
    CallStatic<Keys>(original, 0x00542330, &LobbyMan::RoomKeysCallback, peer, 1, 1, TextAt(0), 1, unknown, values, Lobby());
    char* none[1] = {nullptr};
    CallStatic<Keys>(original, 0x00542330, &LobbyMan::RoomKeysCallback, peer, 1, 1, TextAt(0), 1, none, values, Lobby());
    // The lists.
    RunCFunc(original, 0x00541A70, GameNet::CFunc_NumPlayersInLobby);
    RunCFunc(original, 0x00542AA0, GameNet::CFunc_FillPlayerList);
    g_isBuddy = 1u << (101 & 31);
    CallStatic<Enum>(original, 0x005424F0, &LobbyMan::EnumPlayersCallback, peer, 1, 1, 4, SetText(16, "Zero"), 0, Lobby());
    Word<LobbyPlayer>(g_heapNext - 0x60).m_profileId = 0; // (no profile)
    RunCFunc(original, 0x00542B00, GameNet::CFunc_FillLobbyProspectiveBuddyList);
    // Left: known, unknown.
    CallStatic<Left>(original, 0x005420C0, &LobbyMan::PlayerLeftCallback, peer, 1, TextAt(2), SetText(14, "quit"), Lobby());
    CallStatic<Left>(original, 0x005420C0, &LobbyMan::PlayerLeftCallback, peer, 1, TextAt(6), TextAt(14), Lobby());
    RunCFunc(original, 0x00541A70, GameNet::CFunc_NumPlayersInLobby);
    // A new list: everything forgotten, asked for again.
    CallStatic<NewList>(original, 0x00542B70, &LobbyMan::NewPlayerListCallback, peer, 1, Lobby());
    RunCFunc(original, 0x00541A70, GameNet::CFunc_NumPlayersInLobby);
    g_failAllocations = 0;
}

void Joining(bool original)
{
    Prepare(original);
    void* peer = Lobby()->m_peer;
    using Joined = void(__cdecl*)(void*, int, int, int, LobbyMan*);
    g_index = 1;
    RunCFunc(original, 0x00542D30, GameNet::CFunc_ChooseLobby);
    RunCFunc(original, 0x00542CE0, GameNet::CFunc_RejoinLobby);
    CallStatic<Joined>(original, 0x00542B80, &LobbyMan::JoinRoomCallback, peer, 0, 3, 1, Lobby());
    CallStatic<Joined>(original, 0x00542B80, &LobbyMan::JoinRoomCallback, peer, 1, 0, 1, Lobby());
    Rec(0x1B, {Lobby()->m_inLobby ? 1u : 0u});
    Word<uint8_t>(kBuddies + 0x10) = 0; // not logged in to presence
    CallStatic<Joined>(original, 0x00542B80, &LobbyMan::JoinRoomCallback, peer, 1, 0, 2, Lobby());
    Lobby()->m_groupId = 0;
    RunCFunc(original, 0x00542CE0, GameNet::CFunc_RejoinLobby);
    // Disconnecting: the pending count to 0, the lists, two tasks, the peer.
    Lobby()->m_pending = 3;
    Lobby()->m_unknown136 = false;
    Lobby()->m_tasks[0] = &Word<uint8_t>(kTasks);
    Lobby()->m_tasks[1] = &Word<uint8_t>(kTasks + 0x40);
    RunCFunc(original, 0x005429C0, GameNet::CFunc_LobbyDisconnect);
    Lobby()->m_peer = nullptr;
    RunCFunc(original, 0x005429C0, GameNet::CFunc_LobbyDisconnect);
}

void Connecting(bool original)
{
    Prepare(original);
    Lobby()->m_tasks[0] = &Word<uint8_t>(kTasks);
    RunCFunc(original, 0x00542EE0, GameNet::CFunc_LobbyConnect);
}

void TestNetLobby()
{
    SaveGlobals({{0x00696D68, 4, {}}, {0x007CE430, 4, {}}, {0x007CCE38, 2, {}}});
    RunCase("GameNet::LobbyMan: made, the peer, the lobbies", Construction);
    RunCase("GameNet::LobbyMan: the players and their callbacks", Players);
    RunCase("GameNet::LobbyMan: choosing, joining, leaving a lobby", Joining);
    RunCase("GameNet::LobbyMan: LobbyConnect", Connecting);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetLobby, GameNetKit::Cleanup);

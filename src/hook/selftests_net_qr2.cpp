// Self-tests for the server's GameSpy reporting (src/game/net/gamenet_qr2.cpp) and the lobby's small members and
// hosting callbacks (gamenet_lobby.cpp): the original and ours on the same stand-in arena (net2_testkit.h), the SDK's
// buffer / key buffer / init stood in for by recording code; the calls and the arenas compared.

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/gamenet_qr2.h"
#include "game/net/net_conn.h"
#include "hook/log.h"
#include "hook/net2_testkit.h"
#include "hook/online_identity_testkit.h"

#pragma warning(disable : 4996) // (strcpy into the stand-in texts)

namespace {

using namespace Net2Kit;
using GameNet::LobbyMan;
using GameNet::Manager;

bool g_levelNamed;   // GetLevelName answers a name (else null)
const char* g_password;
int g_qrError;
uint32_t g_gameType;

constexpr uint32_t kLobby = kFree + 0x1000;
constexpr uint32_t kBuddy = kFree + 0x1200;
constexpr uint32_t kConns = kFree + 0x400;
constexpr uint32_t kTexts = kFree + 0x1800;

uint32_t ConnAt(int i) { return kConns + 0x80 * static_cast<uint32_t>(i); }

void Install(StandIns& s)
{
    StubCdecl(s, {0x005FA010}, 0xA010, 1, [](Regs& r) {
        Rec(0xA011, {GameNetKit::Str(r.args[1])});
        return 0u;
    });
    StubCdecl(s, {0x005FB350}, 0xB350, 2);
    StubCdecl(s, {0x005F9FE0}, 0x9FE0, 2);
    StubCdecl(s, {0x005FCCA0}, 0xCCA0, 1);
    // qr2_init: its arguments (strings by their text, our callbacks as the original's).
    StubCdecl(s, {0x005FB230}, 0xB230, 0, [](Regs& r) {
        auto callback = [](uint32_t code) {
            const uint32_t ours[][2] = {
                {AddressOf(&GameNet::ServerKeyCallback), 0x0053B9C0},
                {reinterpret_cast<uint32_t>(&GameNet::ServerKeyCallback), 0x0053B9C0},
                {AddressOf(&GameNet::PlayerKeyCallback), 0x0053BBC0},
                {reinterpret_cast<uint32_t>(&GameNet::PlayerKeyCallback), 0x0053BBC0},
                {AddressOf(&GameNet::CountCallback), 0x0053BD90},
                {reinterpret_cast<uint32_t>(&GameNet::CountCallback), 0x0053BD90},
            };
            for (const auto& o : ours)
                if (o[0] == code)
                    return o[1];
            return code;
        };
        Rec(0xB231, {r.args[0], r.args[1], r.args[2], GameNetKit::Str(r.args[3]), GameNetKit::Str(r.args[4]), r.args[5], r.args[6],
                     callback(r.args[7]), callback(r.args[8]), r.args[9], r.args[10], callback(r.args[11]),
                     r.args[12], r.args[13]});
        return static_cast<uint32_t>(g_qrError);
    });
    s.At({0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, [](Regs& r) {
        Rec(0xC4A0, {r.ecx, r.args[0]});
        return At(kStructs + (r.args[0] == 0x2A5CF019 ? 0 : 0x80));
    });
    Stub(s, {0x005369A0, AddressOf(&Manager::GetLevelName)}, 4, 0x69A0,
         [](Regs&) { return g_levelNamed ? At(kTexts) : 0u; });
    Stub(s, {0x00536A40, AddressOf(&GameNet::Manager::GetGameTypeName)}, 0, 0x6A40, [](Regs&) { return At(kTexts + 0x40); });
    Stub(s, {0x00539B00, AddressOf(&GameNet::Manager::CountPlayers)}, 0, 0x9B00, [](Regs&) { return 3u; });
    Stub(s, {0x00539C70, AddressOf(&GameNet::Manager::CountObservers)}, 0, 0x9C70, [](Regs&) { return 2u; });
    Stub(s, {0x00532CF0, AddressOf(&Manager::GetNumTeams)}, 0, 0x2CF0, [](Regs&) { return 4u; });
    Stub(s, {0x00532C50, AddressOf(&Manager::GetMaxPlayers)}, 0, 0x2C50, [](Regs&) { return 8u; });
    Stub(s, {0x00532D60, AddressOf(&Manager::GetMaxObservers)}, 0, 0x2D60, [](Regs&) { return 6u; });
    Stub(s, {0x00532BE0, AddressOf(&Manager::GetSkillLevel)}, 0, 0x2BE0, [](Regs&) { return 1u; });
    Stub(s, {0x00534310, AddressOf(&GameNet::Manager::GetPassword)}, 0, 0x4310, [](Regs&) { return reinterpret_cast<uint32_t>(g_password); });
    Stub(s, {0x005532A0}, 0, 0x32A0, [](Regs&) { return g_gameType; });
}

void Prepare()
{
    g_levelNamed = true;
    g_password = "";
    g_qrError = 0;
    g_gameType = 0x1C471C60;
    std::strcpy(&Word<char>(kTexts), "Level Name");
    std::strcpy(&Word<char>(kTexts + 0x40), "Trick Attack");
    ArenaStruct(kStructs).String(Crc::GenerateCRCFromString("ui_string"), "My Server");
    ArenaStruct(kStructs + 0x80).String(Crc::GenerateCRCFromString("ui_string"), "Some Level");
    Word<uint32_t>(kManager + 0x10) = At(kLobby);
    Word<uint32_t>(kManager + 0x18) = At(kBuddy);
    Word<uint8_t>(kBuddy + 0x10) = 1;
    for (int i = 0; i < 8; ++i) {
        Word<uint32_t>(PlayerAt(i) + 0x18) = At(ConnAt(i));
        Word<uint32_t>(ConnAt(i) + 0x64) = i == 1 ? 1 : 0; // (player 1's connection is local)
        PlayerFlags(i) = 0;
    }
    SetPlayers({0, 1, 2});
    GameVariable<uint8_t>(0x007CCE1E) = 0;
    GameVariable<uint8_t>(0x007CCE1D) = 0;
    GameVariable<uint32_t>(0x007CC264) = 0x2640;
    GameVariable<uint8_t>(0x007CCE1C) = 0;
    GameVariable<uint8_t>(0x007CCE48) = 0;
    std::strcpy(&GameVariable<char>(0x0067F0A0), "Boston");
}

void ServerKeys(bool original)
{
    Prepare();
    using Fn = void(__cdecl*)(int, void*, void*);
    void* buffer = &Word<uint8_t>(kFree + 0x1F00);
    auto key = [&](int k) {
        original ? Original<Fn>(0x0053B9C0)(k, buffer, nullptr) : GameNet::ServerKeyCallback(k, buffer, nullptr);
    };
    for (int k = -1; k <= 22; ++k)
        key(k);
    g_levelNamed = false;
    g_password = "secret";
    GameVariable<uint8_t>(0x007CCE1D) = 1;
    for (int k : {5, 11, 19})
        key(k);
    GameVariable<uint8_t>(0x007CCE1E) = 1;
    key(11);
    // The player keys: each key for indexes -1 ... 3 (only player 0 is reported - the original's bug).
    using PlayerFn = void(__cdecl*)(int, int, void*, void*);
    for (int index = -1; index <= 3; ++index)
        for (int k : {20, 21, 22, 24, 25})
            original ? Original<PlayerFn>(0x0053BBC0)(k, index, buffer, nullptr)
                     : GameNet::PlayerKeyCallback(k, index, buffer, nullptr);
    Word<uint32_t>(ConnAt(0) + 0x1CA4) = 77; // its latency
    original ? Original<PlayerFn>(0x0053BBC0)(24, 0, buffer, nullptr) : GameNet::PlayerKeyCallback(24, 0, buffer, nullptr);
    Word<uint32_t>(ConnAt(0) + 0x64) = 1; // local
    original ? Original<PlayerFn>(0x0053BBC0)(24, 0, buffer, nullptr) : GameNet::PlayerKeyCallback(24, 0, buffer, nullptr);
    PlayerFlags(0) = 4; // an observer first: the next player is player 0's
    original ? Original<PlayerFn>(0x0053BBC0)(21, 0, buffer, nullptr) : GameNet::PlayerKeyCallback(21, 0, buffer, nullptr);
    SetPlayers({});
    original ? Original<PlayerFn>(0x0053BBC0)(21, 0, buffer, nullptr) : GameNet::PlayerKeyCallback(21, 0, buffer, nullptr);
    using CountFn = int(__cdecl*)(int, void*);
    for (int type = -1; type <= 3; ++type) {
        int n = original ? Original<CountFn>(0x0053BD90)(type, nullptr) : GameNet::CountCallback(type, nullptr);
        Rec(0xBD90, {static_cast<uint32_t>(n)});
    }
}

void Init(bool original)
{
    Prepare();
    using Fn = void(__cdecl*)(bool);
    auto start = [&](bool keep) {
        original ? Original<Fn>(0x0053BDC0)(keep) : GameNet::StartQueryReporting(keep);
        Rec(0xBDC0, {GameVariable<uint8_t>(0x007CCE1C), Hash(&GameVariable<char>(0x0067F0A0))});
    };
    g_qrError = 1;
    start(true);  // failed: not reporting
    g_qrError = 0;
    start(false); // the level's name noted
    start(false); // reporting already: only the name
    GameVariable<uint8_t>(0x007CCE1C) = 0;
    std::strcpy(&GameVariable<char>(0x0067F0A0), "Boston");
    start(true);
}

void LobbyKeys(bool original)
{
    Prepare();
    using Fn = void(__cdecl*)(void*, int, void*, void*);
    void* buffer = &Word<uint8_t>(kFree + 0x1F00);
    void* peer = &Word<uint8_t>(kFree + 0x1F80);
    auto key = [&](int k) {
        GameVariable<uint8_t>(0x007CCE48) = 0;
        original ? Original<Fn>(0x0053F8D0)(peer, k, buffer, nullptr) : LobbyMan::ServerKeyCallback(peer, k, buffer, nullptr);
        Rec(0xF8D0, {GameVariable<uint8_t>(0x007CCE48)});
    };
    for (int k = -1; k <= 58; ++k)
        key(k);
    g_levelNamed = false;
    g_password = "pw";
    g_gameType = 0x6C5FF266;
    Word<uint8_t>(kBuddy + 0x10) = 0;
    Word<uint32_t>(kManager + 0x40) = 2; // the host mode
    for (int k : {5, 19, 54, 55, 56})
        key(k);
    using CountFn = int(__cdecl*)(void*, int, void*);
    for (int type = -1; type <= 3; ++type) {
        int n = original ? Original<CountFn>(0x0053FEC0)(peer, type, nullptr) : LobbyMan::CountCallback(peer, type, nullptr);
        Rec(0xFEC0, {static_cast<uint32_t>(n)});
    }
    using ListFn = void(__cdecl*)(void*, int, void*, void*);
    for (int type = -1; type <= 3; ++type)
        original ? Original<ListFn>(0x0053F620)(peer, type, buffer, nullptr)
                 : LobbyMan::KeyListCallback(peer, type, buffer, nullptr);
}

void LobbyMembers(bool original)
{
    Prepare();
    auto* lobby = &Word<LobbyMan>(kLobby);
    using Method = void(__thiscall*)(LobbyMan*);
    Word<uint32_t>(kLobby + 0x150) = At(kMisc + 0x480); // the hosting task
    for (uint32_t peer : {0u, At(kFree + 0x1F80)}) {
        Word<uint32_t>(kLobby + 0x140) = peer;
        Word<uint8_t>(kLobby + 0x139) = 1;
        original ? Original<Method>(0x0053F7E0)(lobby) : lobby->StopHosting();
        Rec(0xF7E0, {Word<uint8_t>(kLobby + 0x139)});
    }
    for (int value : {0, 1, 0x80}) {
        Word<uint8_t>(kLobby + 0x139) = static_cast<uint8_t>(value);
        Word<uint8_t>(kLobby + 0x134) = static_cast<uint8_t>(value ^ 1);
        bool hosting = original ? Original<bool(__thiscall*)(LobbyMan*)>(0x0053F810)(lobby) : lobby->IsHosting();
        bool group = original ? Original<bool(__thiscall*)(LobbyMan*)>(0x0053F890)(lobby) : lobby->InGroupRoom();
        Rec(0xF810, {hosting ? 1u : 0u, group ? 1u : 0u});
    }
    Word<uint32_t>(kLobby + 0x140) = At(kFree + 0x1F84);
    void* peer = original ? Original<void*(__thiscall*)(LobbyMan*)>(0x0053F880)(lobby) : lobby->GetPeer();
    char* room = original ? Original<char*(__thiscall*)(LobbyMan*)>(0x0053F820)(lobby) : lobby->GetRoomName();
    Rec(0xF880, {reinterpret_cast<uint32_t>(peer), reinterpret_cast<uint32_t>(room)});
    std::memset(&Word<char>(kLobby + 0xB4), 0x5A, 0x40);
    original ? Original<void(__thiscall*)(LobbyMan*, const char*)>(0x0053F830)(lobby, "A Server")
             : lobby->SetServerName("A Server");
    char* server = original ? Original<char*(__thiscall*)(LobbyMan*)>(0x0053F850)(lobby) : lobby->GetServerName();
    Rec(0xF850, {reinterpret_cast<uint32_t>(server)});
    Word<uint8_t>(kLobby + 0x137) = 0x55;
    original ? Original<Method>(0x0053F870)(lobby) : lobby->SetLostConnection();
    Rec(0xF870, {Word<uint8_t>(kLobby + 0x137)});
    original ? Original<Method>(0x0053F860)(lobby) : lobby->ClearLostConnection();
    Rec(0xF860, {Word<uint8_t>(kLobby + 0x137)});
    // The two searches' destructor bodies.
    Word<uint32_t>(kFree + 0x1FC0) = 0x11111111;
    Word<uint32_t>(kFree + 0x1FD0) = 0x22222222;
    auto* a = &Word<GameNet::LobbySearchA>(kFree + 0x1FC0);
    auto* b = &Word<GameNet::LobbySearchB>(kFree + 0x1FD0);
    original ? Original<void(__thiscall*)(void*)>(0x0053F8B0)(a) : a->DestroyBody();
    original ? Original<void(__thiscall*)(void*)>(0x0053F8C0)(b) : b->DestroyBody();
    // The connection timer.
    GameVariable<uint32_t>(0x007CCE3C) = 0;
    g_now = 0x3C3C;
    bool result = original ? Original<bool(__cdecl*)(Script::CStruct*, Script::CScript*)>(0x0053F8A0)(nullptr, nullptr)
                           : GameNet::CFunc_ResetConnectionTimer(nullptr, nullptr);
    Rec(0xF8A0, {result ? 1u : 0u, GameVariable<uint32_t>(0x007CCE3C)});
}

void RestoreQr2() { Cleanup(); }

void TestNetQr2()
{
    OnlineIdentityKit::ConfiguredGameName name; // the original reports the configured name, as ours does
    SaveGlobals({{0x006F0FB8, 4, {}}, {0x00696D40, 4, {}}, {0x007CCDEC, 4, {}}, {0x007CCE1E, 1, {}},
                 {0x007CCE1D, 1, {}}, {0x007CC264, 4, {}}, {0x007CCE1C, 1, {}}, {0x007CCE48, 1, {}},
                 {0x0067F0A0, 0x40, {}}, {0x007CCE3C, 4, {}}});
    RunNet2Case("GameNet qr2: the server / player keys and the counts", ServerKeys, Install);
    RunNet2Case("GameNet qr2: started (the level's name, the secret key, once)", Init, Install);
    RunNet2Case("GameNet lobby: the hosted game's keys, counts and key lists", LobbyKeys, Install);
    RunNet2Case("GameNet lobby: its small members, the searches, the connection timer", LobbyMembers, Install);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetQr2, RestoreQr2);

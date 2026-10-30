// Self-tests for the network manager's player queries, the observer's camera, the host mode
// (src/game/net/gamenet_players.cpp) and the network game's script commands (gamenet_game_cfuncs.cpp): the original
// and ours on the same stand-in arena (net2_testkit.h) with the manager's real player lists; the calls and the arenas
// compared.

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/net/gamenet_game_cfuncs.h"
#include "game/net/gamenet_manager.h"
#include "game/net/gamenet_qr2.h"
#include "game/net/player_info.h"
#include "game/skater/walk_camera.h"
#include "game/skate/score.h"
#include "game/skater/skater_camera_component.h"
#include "hook/log.h"
#include "hook/net2_testkit.h"

namespace {

using namespace Net2Kit;
using GameNet::Manager;
using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);

Manager* TheManager() { return &Word<Manager>(kManager); }

// The objects: 0 skater camera, 1 walk camera, 2 / 6 physics control, 3 / 7 score (the camera's / a skater's);
// 8 the surveyor task, 9 the input handler.
void InstallObjects(StandIns& s)
{
    RecordingObject(s, 0, {{7, 4}});
    RecordingObject(s, 1, {{7, 4}});
    RecordingObject(s, 8, {{2, 4}});
    RecordingObject(s, 9, {{2, 4}});
    Word<uint32_t>(kObjects + 0x40 * 6 + 0x2C) = At(kFree); // the physics control's state
    Word<uint32_t>(kObjects + 0x40 * 7 + 0x18) = At(kFree + 0x200); // the score's counters
    Stub(s, {0x004FA3C0, AddressOf(&Obj::CSkaterCameraComponent::SetSkater)}, 4, 0xA3C0);
    Stub(s, {0x00524C50, AddressOf(&Obj::CWalkCameraComponent::SetSkater)}, 4, 0x4C50);
    Stub(s, {0x00524C30, AddressOf(&Obj::CWalkCameraComponent::ResetNow)}, 0, 0x4C30);
    Stub(s, {0x00543F00, AddressOf(&GameNet::PlayerInfo::TookFlag)}, 4, 0x3F00);
    Stub(s, {0x00543A90, AddressOf(&GameNet::PlayerInfo::CapturedFlag)}, 4, 0x3A90);
    Stub(s, {0x00561B40, AddressOf(&Mdl::Score::GetTotalScore)}, 0, 0x1B40, [](Regs&) { return 5u; });
    Stub(s, {0x00561A80, AddressOf(&Mdl::Score::SetTotalScore)}, 4, 0x1A80);
    Stub(s, {0x00539880, AddressOf(&GameNet::Manager::CountTeamPlayers)}, 4, 0x9880, [](Regs& r) { return r.args[0] * 3 + 1; });
    Stub(s, {0x0053CB70, AddressOf(&Manager::DropPlayer)}, 8, 0xCB70);
    Stub(s, {0x00565790}, 4, 0x5790);
    Stub(s, {0x0053C6E0, AddressOf(&Manager::CancelJoin)}, 0, 0xC6E0);
    StubCdecl(s, {0x0053BDC0, AddressOf(&GameNet::StartQueryReporting)}, 0xBDC0, 1);
    Stub(s, {0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, 0xC4A0,
         [](Regs&) { return At(kStructs + 0x200); });
}

// Players: connections (handles 0x31 + i), skater ids, the manager's apps.
void PreparePlayers(const std::vector<int>& list)
{
    for (int i = 0; i < 8; ++i) {
        uint32_t conn = kFree + 0x400 + 0x40 * static_cast<uint32_t>(i);
        Word<uint32_t>(PlayerAt(i) + 0x18) = At(conn);
        Word<int>(conn + 0x3C) = 0x31 + i;
        PlayerFlags(i) = 0;
    }
    SetPlayers(list);
    SetNewPlayers({});
    Word<uint32_t>(kManager + 0x04) = At(kApp);         // the server
    Word<uint32_t>(kManager + 0x08) = At(kApp + 0x400); // client 0
    Word<uint8_t>(kManager + 0x00) = 0;
}

void Players(bool original)
{
    Manager* m = TheManager();
    PreparePlayers({0, 1, 2, 3, 4, 5});
    PlayerFlags(0) = 1;     // local
    PlayerFlags(1) = 0x24;  // an observer (with 0x20: not chosen)
    PlayerFlags(3) = 0x20;
    PlayerFlags(4) = 0x20;
    Word<uint32_t>(kSkaters + 0x100 * 2 + 0x14) = 0x10012;
    Word<uint32_t>(PlayerAt(5) + 0x14) = 0; // no skater
    using ById = uint8_t*(__thiscall*)(Manager*, uint32_t);
    using ByConn = uint8_t*(__thiscall*)(Manager*, Net::Conn*);
    for (uint32_t id : {0x10u, 0x11u, 0x13u, 0x12u, 0x10012u, 0x20012u, 0x99u}) {
        uint8_t* p = original ? Original<ById>(0x0053AEA0)(m, id) : m->PlayerByObjectId(static_cast<uint16_t>(id));
        Rec(0xAEA0, {reinterpret_cast<uint32_t>(p)});
    }
    for (int i : {0, 1, 5, 7}) {
        auto* conn = &Word<Net::Conn>(kFree + 0x400 + 0x40 * static_cast<uint32_t>(i));
        uint8_t* p = original ? Original<ByConn>(0x0053AE10)(m, conn) : m->PlayerByConnection(conn);
        Rec(0xAE10, {reinterpret_cast<uint32_t>(p)});
    }
    // First come, first served: the first remote non-observer with 0x20 (player 3).
    ArenaStruct(kStructs + 0x200).Checksum(Crc::GenerateCRCFromString("Checksum"), 0xFC5F0001u);
    original ? Original<void(__thiscall*)(Manager*)>(0x0053AAC0)(m) : m->AssignFirstComeFirstServed();
    PlayerFlags(3) = 0x200;
    PlayerFlags(4) = 0x400;
    original ? Original<void(__thiscall*)(Manager*)>(0x0053AAC0)(m) : m->AssignFirstComeFirstServed(); // nobody
    // The observe request: sent (the local player plays), then not (observing).
    original ? Original<void(__thiscall*)(Manager*)>(0x0053AA70)(m) : m->SendObserveRequest();
    PlayerFlags(0) = 5;
    original ? Original<void(__thiscall*)(Manager*)>(0x0053AA70)(m) : m->SendObserveRequest();
}

void ObjectIds(bool original)
{
    Manager* m = TheManager();
    PreparePlayers({0, 1, 2, 3});
    PlayerFlags(1) = 4; // an observer (its id 4 doesn't count)
    const uint32_t ids[4] = {0, 4, 1, 3};
    for (int i = 0; i < 4; ++i)
        Word<uint32_t>(kSkaters + 0x100 * static_cast<uint32_t>(i) + 0x14) = ids[i];
    using Fn = int(__thiscall*)(Manager*);
    auto next = [&] {
        int id = original ? Original<Fn>(0x0053AC40)(m) : m->NextPlayerObjectId();
        Rec(0xAC40, {static_cast<uint32_t>(id)});
    };
    next(); // 2
    SetNewPlayers({{2, 0}, {4, 4}, {6, 0xC}}); // 2 taken; 4 an observer joining (free); 6 a pending observer
    next(); // 4
    SetNewPlayers({{2, 0}, {4, 8}, {5, 0x10}});
    next(); // 6
    SetNewPlayers({{2, 0}, {4, 0xC}});
    next(); // 5 (a pending observer has its id)
    SetNewPlayers({{2, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0}, {11, 0}, {12, 0}, {13, 0}, {14, 0},
                   {15, 0}});
    next(); // 16
}

void Camera(bool original)
{
    Manager* m = TheManager();
    PreparePlayers({0, 1, 2});
    PlayerFlags(1) = 1; // the local player
    Word<uint32_t>(PlayerAt(2) + 0x14) = 0;
    using Fn = void(__thiscall*)(Manager*, uint8_t*);
    auto observe = [&](int player) {
        uint8_t* p = player < 0 ? nullptr : &Word<uint8_t>(PlayerAt(player));
        original ? Original<Fn>(0x0053A910)(m, p) : m->ObservePlayer(p);
        Rec(0xA910, {Word<uint32_t>(kManager + 0x1CC), Word<uint32_t>(kObjects + 0x40 + 0x18)});
        Word<uint32_t>(kObjects + 0x40 + 0x18) = 0x77; // (the walk camera's frames)
    };
    observe(-1);
    observe(2); // no skater
    observe(0); // someone else's
    observe(1); // the local skater, not walking
    Word<uint8_t>(kFree + 0x118) = 1;
    observe(1); // walking
    Word<uint8_t>(kFree + 0x118) = 0;
    Word<int>(kFree + 0x1C) = 2;
    observe(1);
}

void HostMode(bool original)
{
    Manager* m = TheManager();
    PreparePlayers({0, 1, 3});
    PlayerFlags(0) = 1;
    PlayerFlags(3) = 0x20;
    ArenaStruct(kStructs + 0x200).Checksum(Crc::GenerateCRCFromString("Checksum"), 0xFC5F0002u);
    Word<uint32_t>(kManager + 0xF4) = At(kMisc + 0x480); // the auto-serve task
    m->m_hostMode = 0;
    using Fn = void(__thiscall*)(Manager*, int);
    for (int mode : {0, 1, 1, 2, 0, 2, 3}) {
        original ? Original<Fn>(0x0053C320)(m, mode) : m->SetHostMode(mode);
        Rec(0xC320, {static_cast<uint32_t>(m->m_hostMode), GameVariable<uint32_t>(0x007CCDEC)});
        g_now += 0x100;
    }
}

// The script commands.
void RunCFunc(bool original, uint32_t address, CFunc ours, Script::CStruct* params)
{
    Script::CScript* script = TestScript();
    bool result = original ? Original<CFunc>(address)(params, script) : ours(params, script);
    Rec(0xCF00, {address, result ? 1u : 0u});
}

void FlagCommands(bool original)
{
    PreparePlayers({0, 1, 2, 3});
    PlayerFlags(0) = 4;      // an observer (still told)
    PlayerFlags(1) = 0x1001; // local, carrying team 1's flag
    PlayerFlags(2) = 0x2000; // team 2's
    PlayerFlags(3) = 0x8000;
    ArenaStruct took(kStructs);
    took.Integer(0x67E6859A, 0x10011).Integer(0xD61CFF21, 0x102); // player 1 (16 bits), team 2 (a byte sent)
    RunCFunc(original, 0x0053B450, GameNet::CFunc_PlayerTookFlag, took.Struct());
    ArenaStruct nobody(kStructs + 0x80);
    nobody.Integer(0x67E6859A, 0x17).Integer(0xD61CFF21, 1);
    RunCFunc(original, 0x0053B450, GameNet::CFunc_PlayerTookFlag, nobody.Struct());
    ArenaStruct captured(kStructs + 0x100);
    captured.Integer(0x67E6859A, 0x11);
    RunCFunc(original, 0x0053B5A0, GameNet::CFunc_PlayerCapturedFlag, captured.Struct());
    captured = ArenaStruct(kStructs + 0x100);
    captured.Integer(0x67E6859A, 0x12);
    RunCFunc(original, 0x0053B5A0, GameNet::CFunc_PlayerCapturedFlag, captured.Struct());
    captured = ArenaStruct(kStructs + 0x100);
    captured.Integer(0x67E6859A, 0x13); // blocked (0x8000)
    RunCFunc(original, 0x0053B5A0, GameNet::CFunc_PlayerCapturedFlag, captured.Struct());
    RunCFunc(original, 0x0053B5A0, GameNet::CFunc_PlayerCapturedFlag, nobody.Struct());
    ArenaStruct retrieved(kStructs + 0x180);
    retrieved.Integer(Crc::GenerateCRCFromString("player"), 0x12).Integer(Crc::GenerateCRCFromString("flag_team"), 3);
    RunCFunc(original, 0x0053B710, GameNet::CFunc_PlayerRetrievedFlag, retrieved.Struct());
    retrieved = ArenaStruct(kStructs + 0x180);
    retrieved.Integer(Crc::GenerateCRCFromString("player"), 0x18);
    RunCFunc(original, 0x0053B710, GameNet::CFunc_PlayerRetrievedFlag, retrieved.Struct());
    for (int player : {0x11, 0x13, 0x18}) {
        ArenaStruct has(kStructs + 0x280);
        has.Integer(Crc::GenerateCRCFromString("player"), player);
        RunCFunc(original, 0x0053B860, GameNet::CFunc_PlayerHasFlag, has.Struct());
    }
    PlayerFlags(3) = 0x4000;
    ArenaStruct has(kStructs + 0x280);
    has.Integer(Crc::GenerateCRCFromString("player"), 0x13);
    RunCFunc(original, 0x0053B860, GameNet::CFunc_PlayerHasFlag, has.Struct());
}

void TeamCommands(bool original)
{
    PreparePlayers({0, 1, 2});
    ArenaStruct team(kStructs);
    team.Integer(Crc::GenerateCRCFromString("team"), 2);
    RunCFunc(original, 0x0053B8A0, GameNet::CFunc_GetNumPlayersOnTeam, team.Struct());
    ArenaStruct noTeam(kStructs + 0x180);
    noTeam.Integer(Crc::GenerateCRCFromString("teams"), 2);
    RunCFunc(original, 0x0053B8A0, GameNet::CFunc_GetNumPlayersOnTeam, noTeam.Struct()); // (asserts: team 0)
    for (int id : {0x12, 0x15}) {
        ArenaStruct who(kStructs + 0x80);
        who.Integer(0x0830ECAF, id);
        RunCFunc(original, 0x0053B8F0, GameNet::CFunc_GetPlayersTeam, who.Struct());
    }
    for (int mode : {2, 1}) {
        ArenaStruct host(kStructs + 0x100);
        host.Integer(0, mode);
        Word<uint32_t>(kManager + 0xF4) = At(kMisc + 0x480);
        PlayerFlags(0) = 1;
        ArenaStruct(kStructs + 0x200).Checksum(Crc::GenerateCRCFromString("Checksum"), 0xFC5F0003u);
        RunCFunc(original, 0x0053D400, GameNet::CFunc_SetHostMode, host.Struct());
        Rec(0xD400, {static_cast<uint32_t>(TheManager()->m_hostMode)});
    }
    Word<uint32_t>(kManager + 0x198) = 3;
    RunCFunc(original, 0x0053D760, GameNet::CFunc_CancelJoinServer, team.Struct());
    for (bool internet : {false, true}) {
        GameVariable<bool>(0x007CCA82) = internet;
        GameVariable<bool>(0x007CCDF8) = false;
        Word<uint8_t>(kManager + 0x46) = 0;
        RunCFunc(original, 0x0053D440, GameNet::CFunc_EnteredNetworkGame, team.Struct());
        Rec(0xD440, {GameVariable<uint8_t>(0x007CCDF8)});
    }
}

void KickBan(bool original)
{
    for (bool server : {true, false}) {
        for (int marked : {-1, 2, 3}) {
            PreparePlayers({0, 1, 2, 3});
            PlayerFlags(0) = 1;
            PlayerFlags(3) = 4; // an observer: no skater removed
            if (marked >= 0)
                PlayerFlags(marked) |= 0x400;
            Word<uint8_t>(kManager) = server ? 1 : 0;
            RunCFunc(original, 0x0053D490, GameNet::CFunc_KickPlayer, nullptr);
            RunCFunc(original, 0x0053D5E0, GameNet::CFunc_BanPlayer, nullptr);
        }
    }
}

void Surveyor(bool original)
{
    Manager* m = TheManager();
    PreparePlayers({1, 2});
    RunCFunc(original, 0x0053B940, GameNet::CFunc_ExitSurveyorMode, nullptr); // no local player
    PlayerFlags(2) = 1;
    RunCFunc(original, 0x0053B940, GameNet::CFunc_ExitSurveyorMode, nullptr); // not surveying
    PlayerFlags(2) = 5;
    RunCFunc(original, 0x0053D780, GameNet::CFunc_EnterSurveyorMode, nullptr); // observing
    PlayerFlags(2) = 0x10001;
    RunCFunc(original, 0x0053D780, GameNet::CFunc_EnterSurveyorMode, nullptr); // surveying already
    PlayerFlags(2) = 0x20001;
    Word<uint32_t>(kInput + 4) = 0xDE01CE00u; // controller 0's device
    RunCFunc(original, 0x0053D780, GameNet::CFunc_EnterSurveyorMode, nullptr);
    Rec(0xD780, {Word<uint32_t>(PlayerAt(2) + 0x1EC), reinterpret_cast<uint32_t>(m->m_observerInputHandler)});
    // Out again, with stand-in task and handler.
    Word<uint32_t>(PlayerAt(2) + 0x1EC) = At(kObjects + 0x40 * 8);
    m->m_observerInputHandler = &Word<uint8_t>(kObjects + 0x40 * 9);
    RunCFunc(original, 0x0053B940, GameNet::CFunc_ExitSurveyorMode, nullptr);
    PlayerFlags(2) = 0x10001;
    RunCFunc(original, 0x0053B940, GameNet::CFunc_ExitSurveyorMode, nullptr); // neither
}

std::vector<Saved> g_playersSaved;

void RestorePlayers() { Cleanup(); }

void TestNetPlayers()
{
    SaveGlobals({{0x006F0FB8, 4, {}}, {0x00696D40, 4, {}}, {0x007CCDEC, 4, {}}, {0x007CCA82, 1, {}},
                 {0x007CCDF8, 1, {}}});
    RunNet2Case("GameNet players: by object id / connection, first come first served, the observe request", Players,
                InstallObjects);
    RunNet2Case("GameNet players: the next free object id", ObjectIds, InstallObjects);
    RunNet2Case("GameNet players: the observer's camera", Camera, InstallObjects);
    RunNet2Case("GameNet players: the host mode", HostMode, InstallObjects);
    RunNet2Case("GameNet script commands: flags took / captured / retrieved / has", FlagCommands, InstallObjects);
    RunNet2Case("GameNet script commands: teams, host mode, cancel join, entered network game", TeamCommands,
                InstallObjects);
    RunNet2Case("GameNet script commands: kick / ban", KickBan, InstallObjects);
    RunNet2Case("GameNet script commands: surveyor mode in and out", Surveyor, InstallObjects);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetPlayers, RestorePlayers);

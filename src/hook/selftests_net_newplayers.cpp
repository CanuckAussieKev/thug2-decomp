// Self-tests for the network manager's joining players made real, the observe mode, the connection task and the
// manager's construction (src/game/net/gamenet_newplayers.cpp, gamenet_players.cpp's EnterObserveMode, gamenet_flow.cpp's
// ConnectionTask, gamenet_manager_create.cpp): the original and ours on the same stand-in arena (net2_testkit.h) with
// real player and joining-player lists; the calls, messages and arenas compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/stubs.h"
#include "game/ed/park_editor.h"
#include "game/front/screen_element_manager.h"
#include "game/net/buddy_man.h"
#include "game/net/chat.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_conn.h"
#include "game/net/net_lists.h"
#include "game/net/player_info.h"
#include "game/net/service.h"
#include "game/net/stats_man.h"
#include "game/net/vault_man.h"
#include "game/obj/game_objects.h"
#include "game/obj/smtptr.h"
#include "game/obj/trick_objects.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/skater/walk_camera.h"
#include "game/ed/rail_editor.h"
#include "game/skate/score.h"
#include "game/skater/skater_camera_component.h"
#include "game/script/game_cfuncs.h"
#include "hook/log.h"
#include "hook/net2_testkit.h"

#pragma warning(disable : 4996) // (strcpy into the stand-in records)

namespace {

using namespace Net2Kit;
using GameNet::Manager;

constexpr uint32_t kServerApp = 0x30000; // Net::App (+0x1410 its frame, +0x1428 its socket)
constexpr uint32_t kClientApp = 0x32000;
constexpr uint32_t kConns = kFree + 0x400;  // connections, 0x100 apart (their +0x60 status, +0x64 flags)
constexpr uint32_t kInfos = kFree + 0x1000; // NewPlayerInfo records, 0x50 apart
constexpr uint32_t kProfiles = kFree + 0x1400;
constexpr uint32_t kNet = 0x34000;          // Net::Manager
constexpr uint32_t kLobby = kFree + 0x1800;

bool g_inNetGame;
bool g_splitScreen;
bool g_unplugged;
uint32_t g_dialog;
uint32_t g_clock;
uint32_t g_silence[8]; // each connection's time since it was heard
int g_resends[8];
int g_bandwidth[8];
int g_rand;

uint32_t ConnAt(int i) { return kConns + 0x100 * static_cast<uint32_t>(i); }
int ConnIndex(uint32_t conn) { return static_cast<int>((conn - At(kConns)) / 0x100); }

void Install(StandIns& s)
{
    AddAlias(&Manager::ConnectionTask, 0x0053EB70);
    AddAlias(&GameNet::NetMetricsTask, 0x00533B50);
    AddAlias(&Manager::AutoServeTask, 0x0053BF00);
    AddAlias(&Manager::ClientAddNewPlayersTask, 0x0053EA30);
    AddAlias(&Manager::ServerAddNewPlayersTask, 0x0053D9D0);
    AddAlias(&GameNet::KeyboardTask, 0x005334D0);
    AddAlias(&GameNet::ModemStateTask, 0x00533810);
    AddAlias(&GameNet::JoinTimeoutTask, 0x00533ED0);
    AddAlias(&Net::ServiceTask, 0x00545880);
    AddAlias(&Manager::JoinStateTask, 0x0053BF90);
    AddAlias(&Manager::StartGameTask, 0x0053C1F0);
    AddAlias(&GameNet::LevelChangeTask, 0x0052F4A0);
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return g_clock += 3; });
    Stub(s, {0x0044E4F0, AddressOf(&Manager::InNetGame)}, 0, 0xE4F0, [](Regs&) { return g_inNetGame ? 1u : 0u; });
    Stub(s, {0x0044E4E0, AddressOf(&Manager::InNetMode)}, 0, 0xE4E0, [](Regs&) { return 1u; });
    StubCdecl(s, {0x005A1180, AddressOf(CFuncs::CFunc_InSplitScreenGame)}, 0x1180, 2, [](Regs&) { return g_splitScreen ? 1u : 0u; });
    // The skate module.
    Stub(s, {0x00564FC0, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(int)>(&Mdl::Skate::GetProfile))}, 4, 0x4FC0, [](Regs& r) { return At(kProfiles + 0x40 * (r.args[0] & 7)); });
    Stub(s, {0x00567180}, 16, 0x7180, [](Regs& r) { return At(kSkaters + 0x100 * (r.args[2] & 7)); });
    Stub(s, {0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, 0x6520, [](Regs&) { return At(kSkaters + 0x700); });
    Stub(s, {0x0041AD90, AddressOf(&Mdl::Skate::GetNumSkaters)}, 0, 0xAD90, [](Regs&) { return 6u; });
    Stub(s, {0x00566370, AddressOf(&Mdl::Skate::MoveToRestart)}, 12, 0x6370);
    Stub(s, {0x00565790}, 4, 0x5790);
    Stub(s, {0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, 0x65C0, [](Regs& r) { return r.args[0] % 3 == 1 ? 0u : At(kSkaters + 0x100 * r.args[0]); });
    // Players and skaters.
    Stub(s, {0x00581510, AddressOf(&Obj::CSkater::HideSkater)}, 0, 0x1510);
    Stub(s, {0x005814D0, AddressOf(&Obj::CSkater::Reset27C)}, 0, 0x14D0);
    Stub(s, {0x00561A80, AddressOf(&Mdl::Score::SetTotalScore)}, 4, 0x1A80);
    Stub(s, {0x00544340, AddressOf(&GameNet::PlayerInfo::MarkAsKing)}, 4, 0x4340);
    Stub(s, {0x00543270, AddressOf(&GameNet::PlayerInfo::SetProfile)}, 4, 0x3270);
    Stub(s, {0x005438D0, AddressOf(&GameNet::PlayerInfo::SetSkater)}, 4, 0x38D0, [](Regs& r) {
        reinterpret_cast<uint32_t*>(r.ecx)[0x14 / 4] = r.args[0];
        return 0u;
    });
    Stub(s, {0x00544670, AddressOf(&GameNet::PlayerInfo::AwaitReadyResponse)}, 4, 0x4670);
    Stub(s, {0x00543560, AddressOf(&GameNet::NewPlayerInfo::DestroyBody)}, 0, 0x3560);
    Stub(s, {0x00543740, AddressOf(&GameNet::PlayerInfo::Construct)}, 4, 0x3740, [](Regs& r) {
        Word<uint32_t>(r.ecx - g_base + 0x1D0) = r.args[0];
        Word<uint32_t>(r.ecx - g_base + 0x1EC) = 0;
        Word<uint32_t>(r.ecx - g_base + 0) = At(kVtables + 0x2C0);
        return r.ecx;
    });
    Stub(s, {0x005436E0, AddressOf(&GameNet::Manager::GetPlayerScore)}, 4, 0x36E0, [](Regs& r) { return r.args[0] * 100; });
    s.At({0x00587050, AddressOf(&Obj::CSkaterProfile::WriteToBuffer)}, 12, [](Regs& r) { // the appearance written: a few bytes naming the profile
        Rec(0x7050, {r.ecx, r.args[1], r.args[2] & 0xFF});
        auto* out = reinterpret_cast<uint8_t*>(r.args[0]);
        uint32_t profile = Rel(r.ecx);
        std::memcpy(out, &profile, 4);
        out[4] = static_cast<uint8_t>(r.args[2]);
        return 5u + (profile & 3);
    });
    // The manager's own calls not rewritten here.
    Stub(s, {0x0053A580, AddressOf(&GameNet::Manager::NextCamPlayer)}, 0, 0xA580, [](Regs&) { return At(PlayerAt(1)); });
    Stub(s, {0x0053A040, AddressOf(&GameNet::Manager::PrepareLocalFace)}, 0, 0xA040);
    Stub(s, {0x00534D70, AddressOf(&GameNet::Manager::SendCustomTagTexture)}, 0, 0x4D70);
    Stub(s, {0x005889F0, AddressOf(&Obj::CTrickObjectManager::RemoveAllClusters)}, 0, 0x89F0);
    Stub(s, {0x00536820, AddressOf(&GameNet::Manager::RemoveAllNewPlayers)}, 0, 0x6820);
    Stub(s, {0x005399F0, AddressOf(&GameNet::Manager::DisconnectClients)}, 0, 0x99F0);
    Stub(s, {0x005782D0, AddressOf(&Obj::CCrownObject::TakeOff)}, 0, 0x82D0);
    StubCdecl(s, {0x005FC730}, 0xC730, 1, [](Regs&) { return 1u; });
    StubCdecl(s, {0x005FCCF0}, 0xCCF0, 1);
    StubCdecl(s, {0x005FBAD0}, 0xBAD0, 1);
    // The created park and the rails.
    Stub(s, {0x0058D680, AddressOf(&Ed::CParkEditor::WriteCompressedMapIfBuilt)}, 0, 0xD680);
    Stub(s, {0x0058D690, AddressOf(&Ed::CParkEditor::GetCompressedMapBuffer)}, 4, 0xD690, [](Regs&) { return At(kFree + 0x1F00); });
    StubCdecl(s, {0x004F4970, AddressOf(Ed::CRailEditorComponent::Instance)}, 0x4970, 0, [](Regs&) { return At(kFree + 0x1F40); });
    Stub(s, {0x004F45B0, AddressOf(&Ed::CRailEditorComponent::GetPacked)}, 0, 0x45B0, [](Regs&) { return At(kFree + 0x1F80); });
    s.At({0x00458AB0, AddressOf(&Net::App::StreamMessage)}, 32, [](Regs& r) {
        Rec(0x8AB0, {r.ecx, r.args[0], r.args[1] & 0xFF, r.args[2] & 0xFFFF, r.args[3], GameNetKit::Str(r.args[4]), r.args[5] & 0xFF,
                     r.args[6] & 0xFF, r.args[7] & 0xFF});
        return 0u;
    });
    // Connections.
    s.At({0x00455940, AddressOf(&Net::Conn::GetTimeElapsedSinceCommunication)}, 0, [](Regs& r) {
        return g_silence[ConnIndex(r.ecx) & 7];
    });
    s.At({0x00456540, AddressOf(&Net::Conn::GetHighestResendCount)}, 0, [](Regs& r) {
        return static_cast<uint32_t>(g_resends[ConnIndex(r.ecx) & 7]);
    });
    Stub(s, {0x00455920, AddressOf(&Net::Conn::UpdateCommTime)}, 4, 0x5920);
    s.At({0x004559A0, AddressOf(&Net::Conn::GetBandwidthType)}, 0, [](Regs& r) {
        return static_cast<uint32_t>(g_bandwidth[ConnIndex(r.ecx) & 7]);
    });
    Stub(s, {0x0053CB70, AddressOf(&Manager::DropPlayer)}, 8, 0xCB70);
    s.At({0x00458BE0, AddressOf(&Net::App::FirstConnection)}, 4, [](Regs& r) {
        Rec(0x8BE0, {r.ecx});
        return r.ecx == At(kClientApp) ? At(ConnAt(7)) : 0u;
    });
    // The link and its dialogs.
    // (the network object's "cable unplugged" method, the shared return-false stub on the PC: ours calls the stub
    // directly, without the object in ECX - not recorded)
    StubCdecl(s, {0x005B9E50, AddressOf(&Stubs::ReturnFalse)}, 0x9E50, 0, [](Regs&) { return g_unplugged ? 1u : 0u; });
    s.At({0x004AAE20, AddressOf(&Front::CScreenElementManager::GetElement)}, 12, [](Regs& r) {
        Rec(0xAE20, {r.args[1], r.args[2]});
        reinterpret_cast<uint32_t*>(r.args[0])[0] = g_dialog ? At(g_dialog) : 0u;
        return r.args[0];
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Rec(0x37E0, {r.ecx});
        return 0u;
    });
    s.At({0x00416DD0}, 0, [](Regs& r) { // (CSmtPtr<CRefCounted>'s destructor: as RemoveSmartPointer when it points)
        uint32_t object = *reinterpret_cast<uint32_t*>(r.ecx);
        if (object)
            Rec(0x37E0, {object});
        return 0u;
    });
    // The players' deleting destructor and the nodes'.
    void* destroy = s.Thunk(4, [](Regs& r) {
        Rec(0xDE57, {r.ecx, r.args[0]});
        return 0u;
    });
    Word<uint32_t>(kVtables + 0x2C0) = reinterpret_cast<uint32_t>(destroy);
    RecordingObject(s, 0, {{7, 4}});
    RecordingObject(s, 1, {{7, 4}});
    Stub(s, {0x004FA3C0, AddressOf(&Obj::CSkaterCameraComponent::SetSkater)}, 4, 0xA3C0);
    Stub(s, {0x00524C50, AddressOf(&Obj::CWalkCameraComponent::SetSkater)}, 4, 0x4C50);
    Stub(s, {0x00524C30, AddressOf(&Obj::CWalkCameraComponent::ResetNow)}, 0, 0x4C30);
}

void Prepare()
{
    g_inNetGame = true;
    g_splitScreen = false;
    g_unplugged = false;
    g_dialog = 0;
    g_clock = 0x5000;
    std::memset(g_silence, 0, sizeof(g_silence));
    std::memset(g_resends, 0, sizeof(g_resends));
    std::memset(g_bandwidth, 0, sizeof(g_bandwidth));
    for (int i = 0; i < 8; ++i) {
        uint32_t player = PlayerAt(i);
        Word<uint32_t>(player + 0) = At(kVtables + 0x2C0);
        Word<uint32_t>(player + 0x18) = At(ConnAt(i));
        Word<int>(ConnAt(i) + 0x3C) = 0x31 + i;
        Word<uint32_t>(ConnAt(i) + 0x64) = 0;
        Word<uint32_t>(ConnAt(i) + 0x60) = 1;
        Word<uint32_t>(player + 0x28) = 0x2800 + static_cast<uint32_t>(i);
        Word<uint32_t>(player + 0x2C) = 0x2C00 + static_cast<uint32_t>(i);
        Word<uint32_t>(player + 0x48) = 0x4800 + static_cast<uint32_t>(i);
        Word<uint32_t>(player + 0x4C) = At(kProfiles + 0x40 * static_cast<uint32_t>(i));
        Word<uint32_t>(player + 0x1D8) = i % 2;
        Word<uint32_t>(kSkaters + 0x100 * static_cast<uint32_t>(i) + 0x14) = static_cast<uint32_t>(i);
        PlayerFlags(i) = 0;
    }
    SetPlayers({0, 1, 2});
    SetNewPlayers({});
    Word<uint8_t>(kManager) = 1;
    Word<uint32_t>(kManager + 0x04) = At(kServerApp);
    Word<uint32_t>(kManager + 0x08) = At(kClientApp);
    Word<uint32_t>(kManager + 0x0C) = 0;
    Word<uint32_t>(kManager + 0x10) = At(kLobby);
    Word<uint32_t>(kManager + 0x34) = 0x1E7E1000;
    Word<uint32_t>(kManager + 0x6C) = 0;
    Word<uint32_t>(kManager + 0xE4) = At(kMisc + 0x4A0);
    Word<uint32_t>(kServerApp + 0x1410) = 100; // their frames
    Word<uint32_t>(kClientApp + 0x1410) = 100;
    Word<uint32_t>(kLobby + 0x140) = At(kLobby + 0x180);
    Word<uint8_t>(kLobby + 0x139) = 1;
    GameVariable<uint32_t>(0x006EFDD8) = At(kNet);
    Word<int>(kNet + 0x128) = 2; // (its connection type)
    Word<uint32_t>(kMisc + 0x800 + 0x5D4) = 0xBEE005D4u;
    Word<uint32_t>(kMisc + 0x800 + 0x180) = 0xBEE00180u;
    Word<uint32_t>(kMisc + 0x800 + 0x12C) = 0xBEE0012Cu;
    // The skate module: its skaters' list (three).
    BuildList(kMisc + 0x60, {At(kSkaters), At(kSkaters + 0x100), At(kSkaters + 0x200)});
    Word<uint32_t>(kMisc + 0x34) = Word<uint32_t>(kMisc + 0x60 + 0xC); // (+0x34: its first node)
    GameVariable<uint8_t>(0x007CCA82) = 1;
    GameVariable<uint8_t>(0x007CCE1C) = 1;
    GameVariable<uint32_t>(0x007CCE30) = 0;
}

// A joining player's record (the lists' items, 0x50 apart).
uint32_t Info(int i, const char* name, int8_t objId, uint32_t flags, uint32_t time, int conn)
{
    uint32_t info = kInfos + 0x50 * static_cast<uint32_t>(i);
    std::memset(&Word<uint8_t>(info), 0, 0x44);
    std::strcpy(&Word<char>(info), name);
    Word<int8_t>(info + 0x10) = objId;
    Word<uint32_t>(info + 0x14) = At(ConnAt(conn));
    Word<uint32_t>(info + 0x18) = time;
    Word<uint32_t>(info + 0x1C) = At(kProfiles + 0x40 * static_cast<uint32_t>(i + 3));
    Word<uint32_t>(info + 0x20) = flags;
    Word<uint32_t>(info + 0x24) = 1;
    Word<uint32_t>(info + 0x28) = 2;
    Word<uint32_t>(info + 0x2C) = 0x2C2C0000u + static_cast<uint32_t>(i);
    Word<uint32_t>(info + 0x30) = 0x30300000u + static_cast<uint32_t>(i);
    Word<uint32_t>(info + 0x34) = 0x34340000u + static_cast<uint32_t>(i);
    Word<uint32_t>(info + 0x38) = 0x38380000u + static_cast<uint32_t>(i);
    Word<uint32_t>(info + 0x3C) = 0x3C3C0000u + static_cast<uint32_t>(i);
    Word<uint32_t>(info + 0x40) = i == 1 ? 3 : 0;
    return At(info);
}
// The joining list's nodes get a recording vtable (deleted after).
void JoiningList(const std::vector<uint32_t>& infos)
{
    uint32_t start = g_nextNode;
    BuildList(kManager + 0x130, infos);
    for (uint32_t n = start; n < g_nextNode; n += 0x20)
        Word<uint32_t>(kNodes + n) = At(kVtables + 0x2C0);
}

void AddPlayers(bool original)
{
    Manager* m = &Word<Manager>(kManager);
    using Fn = void(__thiscall*)(Manager*, uint8_t*);
    struct Case {
        uint32_t flags;
        bool netGame, splitScreen;
        int8_t objId;
        int existing; // a player with that object id already (its index), -1 none
    };
    const Case cases[] = {
        {0x04, true, false, 5, -1},   // an observer: no skater
        {0x01, true, true, 3, -1},    // the local player (split screen's profile)
        {0x00, true, false, 4, -1},   // a remote player (with a vehicle type: record 1)
        {0x01, false, false, 2, -1},  // not a network game: counted skaters
        {0x80, true, false, 6, -1},   // the crown
        {0x00, true, false, 1, 1},    // that player exists already: kept
        {0x10, true, false, 2, 2},    // resync: the old one destroyed
        {0x11, true, false, 7, -1},   // resync of nobody: the local player destroyed
    };
    int i = 0;
    for (const Case& c : cases) {
        Prepare();
        PlayerFlags(0) = 1; // the local player
        g_inNetGame = c.netGame;
        g_splitScreen = c.splitScreen;
        uint32_t info = Info(i % 4, "Joining", c.objId, c.flags, 0, 4);
        if (i == 2)
            Word<uint32_t>(info - g_base + 0x40) = 3;
        original ? Original<Fn>(0x0053E440)(m, &Word<uint8_t>(info - g_base)) : m->AddNewPlayer(&Word<uint8_t>(info - g_base));
        ++i;
    }
}

void ClientTask(bool original)
{
    using TaskFn = void(__cdecl*)(void*);
    void* task = &Word<uint8_t>(kMisc + 0x480);
    for (int variant = 0; variant < 4; ++variant) {
        Prepare();
        PlayerFlags(0) = 1;
        Word<uint32_t>(kMisc + 0x480 + 0x1C) = At(kManager);
        Word<uint32_t>(kManager + 0x0C) = variant == 3 ? At(kServerApp) : 0; // client 1 (no connection)
        if (variant == 0)
            JoiningList({});
        else if (variant == 1)
            JoiningList({Info(0, "Later", 4, 0, 200, 4)}); // not yet
        else
            JoiningList({Info(0, "First", 4, 0, 50, 4), Info(1, "Second", 5, 0, 100, 5),
                         Info(2, "Third", 6, 0, variant == 2 ? 101 : 99, 6)});
        original ? Original<TaskFn>(0x0053EA30)(task) : Manager::ClientAddNewPlayersTask(task);
    }
}

void ServerTask(bool original)
{
    using TaskFn = void(__cdecl*)(void*);
    void* task = &Word<uint8_t>(kMisc + 0x480);
    struct Case {
        uint32_t flags;
        uint32_t level;
        uint32_t bandwidth; // the joining connection's bandwidth type
        int connectionType; // the network's (1: broadband too)
        bool qr2;
    };
    const Case cases[] = {
        {0x00, 0x1E7E1000, 1, 2, true},  // a remote player
        {0x00, 0xB664035D, 0, 1, true},  // ... into the created park, broadband by the network
        {0x10, 0x1E7E1000, 1, 2, false}, // resync: its group's players
        {0x10, 0x1E7E1000, 0, 2, true},  // ... narrowband
        {0x04, 0x1E7E1000, 0, 2, true},  // an observer
        {0x01, 0xB664035D, 0, 2, true},  // the server's own player
    };
    for (const Case& c : cases) {
        Prepare();
        SetPlayers({0, 1, 2, 3});
        PlayerFlags(0) = 1;     // the host's own
        PlayerFlags(2) = 4;     // an observer
        PlayerFlags(3) = 0x2400;
        Word<uint32_t>(kMisc + 0x480 + 0x1C) = At(kManager);
        Word<uint32_t>(kManager + 0x34) = c.level;
        g_bandwidth[4] = static_cast<int>(c.bandwidth);
        Word<int>(kNet + 0x128) = c.connectionType;
        GameVariable<uint8_t>(0x007CCA82) = c.qr2;
        Word<uint32_t>(ConnAt(5) + 0x64) = c.flags & 1; // (the host's own player's connection is local)
        JoiningList({Info(0, "NewOne", 4, c.flags, 90, c.flags & 1 ? 5 : 4), Info(1, "Waits", 5, 0, 101, 6)});
        original ? Original<TaskFn>(0x0053D9D0)(task) : Manager::ServerAddNewPlayersTask(task);
    }
}

void Observe(bool original)
{
    Manager* m = &Word<Manager>(kManager);
    using Fn = void(__thiscall*)(Manager*);
    for (int variant = 0; variant < 4; ++variant) {
        Prepare();
        PlayerFlags(0) = variant == 0 ? 5 : 1; // observing already / playing
        if (variant == 2)
            PlayerFlags(0) |= 0x80 | 0x1000; // the crown and team 1's flag
        Word<uint8_t>(kManager) = variant == 3 ? 0 : 1;
        Word<uint32_t>(kManager + 0x6C) = variant == 2 ? At(kMisc + 0x600) : 0;
        Word<int>(kManager + 0x100) = 7;
        original ? Original<Fn>(0x0053E650)(m) : m->EnterObserveMode();
    }
}

void Connections(bool original)
{
    using TaskFn = void(__cdecl*)(void*);
    void* task = &Word<uint8_t>(kMisc + 0x480);
    struct Link {
        bool netGame, unplugged;
        uint32_t dialog, lastCheck;
    };
    const Link links[] = {{true, false, 0, 0x5000}, {true, true, 0, 0x5003 - 1000}, {true, true, 0, 0x5003 - 1001},
                          {true, true, 0, 0}, {true, true, kMisc + 0x700, 0},
                          {false, true, 0, 0}, {false, true, kMisc + 0x700, 0}, {false, false, 0, 0}};
    for (const Link& l : links) {
        Prepare();
        Word<uint32_t>(kMisc + 0x480 + 0x1C) = At(kManager);
        Word<uint8_t>(kManager) = 0; // a client, joined, its server heard
        Word<int>(kManager + 0x198) = 7;
        g_inNetGame = l.netGame;
        g_unplugged = l.unplugged;
        g_dialog = l.dialog;
        GameVariable<uint32_t>(0x007CCE30) = l.lastCheck;
        original ? Original<TaskFn>(0x0053EB70)(task) : Manager::ConnectionTask(task);
        Rec(0xEB70, {GameVariable<uint32_t>(0x007CCE30)});
    }
    // The server's players: local, playing (silent / resending), joining (silent), an observer timed out.
    Prepare();
    Word<uint32_t>(kMisc + 0x480 + 0x1C) = At(kManager);
    SetPlayers({0, 1, 2, 3, 4, 5, 6});
    Word<uint32_t>(ConnAt(0) + 0x64) = 1; // local
    g_silence[0] = 99999;
    g_silence[1] = 20001;
    g_silence[2] = 20000;
    g_resends[2] = 21;
    g_resends[3] = 20;
    Word<uint32_t>(ConnAt(4) + 0x60) = 0; // joining
    g_silence[4] = 60001;
    Word<uint32_t>(ConnAt(5) + 0x60) = 0;
    g_silence[5] = 60000;
    PlayerFlags(6) = 4; // an observer
    g_silence[6] = 30000;
    original ? Original<TaskFn>(0x0053EB70)(task) : Manager::ConnectionTask(task);
    // A client's server silent: each join state, going to the main menu or not.
    for (int variant = 0; variant < 6; ++variant) {
        Prepare();
        Word<uint32_t>(kMisc + 0x480 + 0x1C) = At(kManager);
        Word<uint8_t>(kManager) = 0;
        const int states[6] = {6, 7, 8, 5, 8, 7};
        Word<int>(kManager + 0x198) = states[variant];
        g_silence[7] = variant == 5 ? 20000 : 20001;
        Word<uint32_t>(kMisc + 0x378) = variant == 4 ? 0xE92ECAFE : 0;
        if (variant == 3)
            Word<uint32_t>(kManager + 0x08) = 0;
        original ? Original<TaskFn>(0x0053EB70)(task) : Manager::ConnectionTask(task);
        Rec(0xEB71, {Word<uint32_t>(kManager + 0x198)});
    }
}

void Construction(bool original)
{
    Prepare();
    using CreateFn = Manager*(__cdecl*)(bool);
    GameVariable<uint32_t>(0x007CCE18) = 4;
    // There already: a reference more.
    Manager* m = original ? Original<CreateFn>(0x0053F520)(true) : Manager::Create(true);
    Rec(0xF520, {reinterpret_cast<uint32_t>(m), GameVariable<uint32_t>(0x007CCE18)});
    // None, not made.
    GameVariable<uint32_t>(0x007CCE14) = 0;
    m = original ? Original<CreateFn>(0x0053F520)(false) : Manager::Create(false);
    Rec(0xF521, {reinterpret_cast<uint32_t>(m), GameVariable<uint32_t>(0x007CCE18)});
    // Made: the constructor on the arena's heap.
    g_rand = 7;
    m = original ? Original<CreateFn>(0x0053F520)(true) : Manager::Create(true);
    Rec(0xF522, {reinterpret_cast<uint32_t>(m), GameVariable<uint32_t>(0x007CCE18), GameVariable<uint32_t>(0x007CCE14)});
    if (m) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(m);
        g_trace.push_back(HashBytes(bytes + 0x20, 9)); // the challenge
    }
    GameVariable<uint32_t>(0x007CCE14) = At(kManager);
}

void InstallConstruction(StandIns& s)
{
    Install(s);
    Stub(s, {0x0046C470, AddressOf(&Prefs::Preferences::Init)}, 0, 0xC470, [](Regs& r) { return r.ecx; });
    Stub(s, {0x0046C650, AddressOf(&Prefs::Preferences::Load)}, 4, 0xC650, [](Regs&) { return 1u; });
    Stub(s, {0x00541870, AddressOf(&GameNet::LobbyMan::Construct)}, 0, 0x1870, [](Regs& r) { return r.ecx; });
    Stub(s, {0x0054C1D0, AddressOf(&GameNet::VaultMan::Construct)}, 0, 0xC1D0, [](Regs& r) { return r.ecx; });
    Stub(s, {0x0054A480, AddressOf(&GameNet::BuddyMan::Construct)}, 0, 0xA480, [](Regs& r) { return r.ecx; });
    Stub(s, {0x0054DBD0, AddressOf(&GameNet::StatsMan::Construct)}, 0, 0xDBD0, [](Regs& r) { return r.ecx; });
    StubCdecl(s, {0x005F9540}, 0x9540, 1);
    StubCdecl(s, {0x00625138}, 0x5138, 1, [](Regs&) { return 0x1234567u; });
    StubCdecl(s, {0x00623B96}, 0x3B96, 1);
    StubCdecl(s, {0x00623BA3}, 0x3BA3, 0, [](Regs&) { return static_cast<uint32_t>(g_rand += 11); });
    s.At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1D40, {r.args[0], r.args[1], r.args[2]});
        uint32_t block = At(g_heapNext);
        g_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0, r.args[0]);
        return block;
    });
}

void RestoreNewPlayers() { Cleanup(); }

void TestNetNewPlayers()
{
    SaveGlobals({{0x006F0FB8, 4, {}}, {0x00696D40, 4, {}}, {0x007CCDEC, 4, {}}, {0x006EFDD8, 4, {}},
                 {0x007CCA82, 1, {}}, {0x007CCE1C, 1, {}}, {0x007CCE30, 4, {}}, {0x007CCE18, 4, {}}});
    RunNet2Case("GameNet new players: a client's joining player made real", AddPlayers, Install);
    RunNet2Case("GameNet new players: the client's add task", ClientTask, Install);
    RunNet2Case("GameNet new players: the server's add task", ServerTask, Install);
    RunNet2Case("GameNet players: into observe mode", Observe, Install);
    RunNet2Case("GameNet flow: the connection task", Connections, Install);
    RunNet2Case("GameNet manager: made (Create, the constructor)", Construction, InstallConstruction);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetNewPlayers, RestoreNewPlayers);

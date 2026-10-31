// The server's reporting to GameSpy's master server through the query & reporting SDK (qr2; library code, called by
// address): its set-up and the callbacks that answer the master server's queries (0x0053B9C0-0x0053BEC5). See
// gamenet_qr2.h.
//
// NOT ORIGINAL: the game name reported is the online identity's (NetLink::GameName(), [Miscellaneous] OnlineGameName -
// online_identity.h); the original pushes "thps6pc" here and online_identity.cpp wrote the configured name over those
// pushes (0x0053BE86, 0x0053BA1F), which this source replaces. The secret key is still read from where the identity
// writes it (0x0067F120, stored bitwise-NOT).

#include "engine/net/gamenet_qr2.h"
#include "engine/net/gamespy.h"

#include <cstdint>
#include <cstring>

#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_conn.h"
#include "engine/net/online_identity.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"

#pragma warning(disable : 4996) // (strcpy: the original's copy of the level's name)

namespace OnlinePlay {

using namespace Detail;

namespace {

// qr2's keys (the SDK's standard numbering).
enum Qr2Key : int {
    kKeyHostname = 1,
    kKeyGamename = 2,
    kKeyGamever = 3,
    kKeyHostport = 4,
    kKeyMapname = 5,
    kKeyGametype = 6,
    kKeyNumplayers = 8,
    kKeyNumteams = 9,
    kKeyMaxplayers = 10,
    kKeyGamemode = 11,
    kKeyPassword = 19,
    kKeyGroupid = 20,
    kKeyForPlayer = 21, // player_
    kKeyPing = 24,   // ping_
    kKeyTeam = 25,   // team_
};
constexpr int kKeyTypePlayer = 1;
constexpr int kKeyTypeTeam = 2;

constexpr uint32_t kServerName = 0x2A5CF019; // server_name
constexpr uint32_t kLevel = 0x651533EC;      // level
constexpr uint16_t kHostPort = 0x1421;       // 5153 (reported)
constexpr int kQueryPort = 0x1423;           // 5155 (qr2's own)

// The SDK.
void BufferAdd(void* buffer, const char* text) { GameSpy::Qr2BufferAdd(buffer, text); }
void BufferAddInt(void* buffer, int value) { GameSpy::Qr2BufferAddInt(buffer, value); }

Hub* NetSessionSlot() { return g_onlineHub; }
OptionStore::Preferences* NetGameOptions(Hub* m) { return reinterpret_cast<OptionStore::Preferences*>(m->m_netOptions); }

char g_fallbackMapName[128]{"Boston"};

// The level's name for qr2's mapname when the level select has none (0x0067F0A0, "Boston" until set up).
char* FallbackMapName() { return g_fallbackMapName; }

} // namespace

int g_lobbyGroupId;

// 0x0053B9C0
void __cdecl ServerKeyCallback(int key, void* buffer, void*)
{
    Hub* manager = NetSessionSlot();
    switch (key) {
    case kKeyHostname: {
        const char* text = nullptr;
        NetGameOptions(manager)->FetchSetting(kServerName)->FetchText("ui_string", &text, 0);
        BufferAdd(buffer, text);
        return;
    }
    case kKeyGamename:
        BufferAdd(buffer, NetLink::GameName()); // (the original: "thps6pc")
        return;
    case kKeyGamever:
        BufferAdd(buffer, "1.00");
        return;
    case kKeyHostport:
        BufferAddInt(buffer, kHostPort);
        return;
    case kKeyMapname: {
        const char* name = manager->GetMapName(true);
        BufferAdd(buffer, name ? name : FallbackMapName());
        return;
    }
    case kKeyGametype:
        BufferAdd(buffer, manager->GetGameTypeName()); // the game type's name
        return;
    case kKeyNumplayers:
        BufferAddInt(buffer, manager->CountPlayers());
        return;
    case kKeyNumteams:
        BufferAddInt(buffer, manager->GetNumTeams());
        return;
    case kKeyMaxplayers:
        BufferAddInt(buffer, manager->GetPlayerLimit());
        return;
    case kKeyGamemode:
        if (g_gameReported)
            BufferAdd(buffer, "openplaying");
        else if (g_openWaiting)
            BufferAdd(buffer, "openwaiting");
        else
            BufferAdd(buffer, "closedwaiting");
        return;
    case kKeyPassword:
        BufferAddInt(buffer, *manager->FetchSecret() != '\0' ? 1 : 0);
        return;
    case kKeyGroupid:
        BufferAddInt(buffer, g_lobbyGroupId); // (the lobby's group room)
        return;
    default:
        BufferAdd(buffer, "");
        return;
    }
}

// 0x0053BBC0
void __cdecl PlayerKeyCallback(int key, int index, void* buffer, void*)
{
    Hub* manager = NetSessionSlot();
    if (index >= manager->CountPlayers()) {
        BufferAdd(buffer, "");
        return;
    }
    // QUIRK kept: the original never counts `index` down - only player 0 is found; for the others the walk runs off
    // the end and they're reported empty.
    PlayerSearch search;
    uint8_t* player = StartRosterSweep(manager, &search, false);
    if (index != 0)
        while (player)
            player = StepRosterSweep(manager, &search, false);
    if (!player) {
        BufferAdd(buffer, "");
        return;
    }
    switch (key) {
    case kKeyForPlayer:
        BufferAdd(buffer, PlayerName(player));
        return;
    case kKeyPing: {
        NetLink::Conn* conn = Prop<NetLink::Conn*>(player, 0x18);
        BufferAddInt(buffer, conn->IsHostLocal() ? 0 : conn->m_pingProbe.m_averageLatency);
        return;
    }
    case kKeyTeam:
        BufferAddInt(buffer, PlayerTeam(player));
        return;
    default:
        BufferAdd(buffer, "");
        return;
    }
}

// 0x0053BD90
int __cdecl CountCallback(int type, void*)
{
    if (type == kKeyTypePlayer)
        return NetSessionSlot()->CountPlayers();
    if (type == kKeyTypeTeam)
        return NetSessionSlot()->GetNumTeams();
    return 0;
}

// 0x0053BDC0
void __cdecl StartQueryReporting(bool keepMapName)
{
    if (!keepMapName) { // the level's name, for mapname when the level select has none
        const char* text = nullptr;
        NetGameOptions(NetSessionSlot())->FetchSetting(kLevel)->FetchText("ui_string", &text, 0);
        std::strcpy(FallbackMapName(), text);
    }
    bool& reporting = g_queryReporting;
    if (reporting)
        return;
    const char* key = NetLink::GameKey(); // (given to the SDK: nothing written into data)
    int error = GameSpy::Qr2Init(nullptr, nullptr, kQueryPort, NetLink::GameName(), key, 1, 0, &ServerKeyCallback, &PlayerKeyCallback,
                        CodeAddress(OnlinePlay::PlayerTeamKeyCallback), CodeAddress(OnlinePlay::KeyListCallback), &CountCallback,
                        CodeAddress(OnlinePlay::AddErrorCallback), nullptr);
    if (error == 0)
        reporting = true;
}

} // namespace OnlinePlay

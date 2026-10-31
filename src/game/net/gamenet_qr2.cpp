// The server's reporting to GameSpy's master server through the query & reporting SDK (qr2; library code, called by
// address): its set-up and the callbacks that answer the master server's queries (0x0053B9C0-0x0053BEC5). See
// gamenet_qr2.h.
//
// NOT ORIGINAL: the game name reported is the online identity's (Net::GameName(), [Miscellaneous] OnlineGameName -
// online_identity.h); the original pushes "thps6pc" here and online_identity.cpp wrote the configured name over those
// pushes (0x0053BE86, 0x0053BA1F), which this source replaces. The secret key is still read from where the identity
// writes it (0x0067F120, stored bitwise-NOT).

#include "game/net/gamenet_qr2.h"
#include "game/net/gamespy.h"

#include <cstdint>
#include <cstring>

#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_conn.h"
#include "game/net/online_identity.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

#pragma warning(disable : 4996) // (strcpy: the original's copy of the level's name)

namespace GameNet {

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
    kKeyPlayer = 21, // player_
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

Manager* NetManager() { return g_gameNetManager; }
Prefs::Preferences* NetworkPreferences(Manager* m) { return reinterpret_cast<Prefs::Preferences*>(m->m_networkPreferences); }

char g_fallbackMapName[128]{"Boston"};

// The level's name for qr2's mapname when the level select has none (0x0067F0A0, "Boston" until set up).
char* FallbackMapName() { return g_fallbackMapName; }

} // namespace

int g_lobbyGroupId;

// 0x0053B9C0
void __cdecl ServerKeyCallback(int key, void* buffer, void*)
{
    Manager* manager = NetManager();
    switch (key) {
    case kKeyHostname: {
        const char* text = nullptr;
        NetworkPreferences(manager)->GetPreference(kServerName)->GetString("ui_string", &text, 0);
        BufferAdd(buffer, text);
        return;
    }
    case kKeyGamename:
        BufferAdd(buffer, Net::GameName()); // (the original: "thps6pc")
        return;
    case kKeyGamever:
        BufferAdd(buffer, "1.00");
        return;
    case kKeyHostport:
        BufferAddInt(buffer, kHostPort);
        return;
    case kKeyMapname: {
        const char* name = manager->GetLevelName(true);
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
        BufferAddInt(buffer, manager->GetMaxPlayers());
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
        BufferAddInt(buffer, *manager->GetPassword() != '\0' ? 1 : 0);
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
    Manager* manager = NetManager();
    if (index >= manager->CountPlayers()) {
        BufferAdd(buffer, "");
        return;
    }
    // QUIRK kept: the original never counts `index` down - only player 0 is found; for the others the walk runs off
    // the end and they're reported empty.
    PlayerSearch search;
    uint8_t* player = FirstPlayer(manager, &search, false);
    if (index != 0)
        while (player)
            player = NextPlayer(manager, &search, false);
    if (!player) {
        BufferAdd(buffer, "");
        return;
    }
    switch (key) {
    case kKeyPlayer:
        BufferAdd(buffer, PlayerName(player));
        return;
    case kKeyPing: {
        Net::Conn* conn = Field<Net::Conn*>(player, 0x18);
        BufferAddInt(buffer, conn->IsLocal() ? 0 : conn->m_latencyTest.m_averageLatency);
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
        return NetManager()->CountPlayers();
    if (type == kKeyTypeTeam)
        return NetManager()->GetNumTeams();
    return 0;
}

// 0x0053BDC0
void __cdecl StartQueryReporting(bool keepMapName)
{
    if (!keepMapName) { // the level's name, for mapname when the level select has none
        const char* text = nullptr;
        NetworkPreferences(NetManager())->GetPreference(kLevel)->GetString("ui_string", &text, 0);
        std::strcpy(FallbackMapName(), text);
    }
    bool& reporting = g_queryReporting;
    if (reporting)
        return;
    const char* key = Net::GameKey(); // (given to the SDK: nothing written into data)
    int error = GameSpy::Qr2Init(nullptr, nullptr, kQueryPort, Net::GameName(), key, 1, 0, &ServerKeyCallback, &PlayerKeyCallback,
                        CodeAddress(GameNet::PlayerTeamKeyCallback), CodeAddress(GameNet::KeyListCallback), &CountCallback,
                        CodeAddress(GameNet::AddErrorCallback), nullptr);
    if (error == 0)
        reporting = true;
}

} // namespace GameNet

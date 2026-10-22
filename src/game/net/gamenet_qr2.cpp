// The server's reporting to GameSpy's master server through the query & reporting SDK (qr2; library code, called by
// address): its set-up and the callbacks that answer the master server's queries (0x0053B9C0-0x0053BEC5). See
// gamenet_qr2.h.
//
// NOT ORIGINAL: the game name reported is the online identity's (Net::GameName(), [Miscellaneous] OnlineGameName -
// online_identity.h); the original pushes "thps6pc" here and online_identity.cpp wrote the configured name over those
// pushes (0x0053BE86, 0x0053BA1F), which this source replaces. The secret key is still read from where the identity
// writes it (0x0067F120, stored bitwise-NOT).

#include "game/net/gamenet_qr2.h"

#include <cstdint>
#include <cstring>

#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_conn.h"
#include "game/net/online_identity.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

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
void BufferAdd(void* buffer, const char* text) { GameFunction<void(__cdecl*)(void*, const char*)>(0x005FA010)(buffer, text); }
void BufferAddInt(void* buffer, int value) { GameFunction<void(__cdecl*)(void*, int)>(0x005FB350)(buffer, value); }
using KeyCallback = void(__cdecl*)(int, void*, void*);
using PlayerKeyCallbackFn = void(__cdecl*)(int, int, void*, void*);
using CountCallbackFn = int(__cdecl*)(int, void*);
int Qr2Init(void* qrec, const char* ip, int basePort, const char* gameName, const char* secretKey, int isPublic,
            int natNegotiate, KeyCallback serverKey, PlayerKeyCallbackFn playerKey, void* teamKey, void* keyList,
            CountCallbackFn count, void* addError, void* userData)
{
    return GameFunction<int(__cdecl*)(void*, const char*, int, const char*, const char*, int, int, KeyCallback,
                                      PlayerKeyCallbackFn, void*, void*, CountCallbackFn, void*, void*)>(0x005FB230)(
        qrec, ip, basePort, gameName, secretKey, isPublic, natNegotiate, serverKey, playerKey, teamKey, keyList, count,
        addError, userData);
}

Manager* NetManager() { return GameVariable<Manager*>(0x007CCE14); }
Prefs::Preferences* NetworkPreferences(Manager* m) { return reinterpret_cast<Prefs::Preferences*>(m->m_networkPreferences); }
// The level's name for qr2's mapname when the level select has none (0x0067F0A0, "Boston" until set up).
char* FallbackMapName() { return &GameVariable<char>(0x0067F0A0); }

} // namespace

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
        BufferAdd(buffer, Call<const char*>(0x00536A40, manager)); // the game type's name
        return;
    case kKeyNumplayers:
        BufferAddInt(buffer, Call<int>(0x00539B00, manager));
        return;
    case kKeyNumteams:
        BufferAddInt(buffer, manager->GetNumTeams());
        return;
    case kKeyMaxplayers:
        BufferAddInt(buffer, manager->GetMaxPlayers());
        return;
    case kKeyGamemode:
        if (GameVariable<bool>(0x007CCE1E))
            BufferAdd(buffer, "openplaying");
        else if (GameVariable<bool>(0x007CCE1D))
            BufferAdd(buffer, "openwaiting");
        else
            BufferAdd(buffer, "closedwaiting");
        return;
    case kKeyPassword:
        BufferAddInt(buffer, *Call<const char*>(0x00534310, manager) != '\0' ? 1 : 0);
        return;
    case kKeyGroupid:
        BufferAddInt(buffer, GameVariable<int>(0x007CC264)); // (the lobby's group room)
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
    if (index >= Call<int>(0x00539B00, manager)) {
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
        return Call<int>(0x00539B00, NetManager());
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
    bool& reporting = GameVariable<bool>(0x007CCE1C);
    if (reporting)
        return;
    char key[7];
    const auto* stored = &GameVariable<uint8_t>(0x0067F120);
    for (int i = 0; i < 6; ++i)
        key[i] = static_cast<char>(~stored[i]);
    key[6] = '\0';
    int error = Qr2Init(nullptr, nullptr, kQueryPort, Net::GameName(), key, 1, 0, &ServerKeyCallback, &PlayerKeyCallback,
                        reinterpret_cast<void*>(0x00533400), reinterpret_cast<void*>(0x00533420), &CountCallback,
                        reinterpret_cast<void*>(0x005334B0), nullptr);
    if (error == 0)
        reporting = true;
}

} // namespace GameNet

REPLACE(0x0053B9C0, GameNet::ServerKeyCallback);
REPLACE(0x0053BBC0, GameNet::PlayerKeyCallback);
REPLACE(0x0053BD90, GameNet::CountCallback);
REPLACE(0x0053BDC0, GameNet::StartQueryReporting);

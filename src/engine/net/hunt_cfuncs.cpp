// Script commands (CFunctions) of the network game: the hunt, vehicles, projectiles, the lobby keyboard (see
// hunt_cfuncs.h).

#include "engine/net/hunt_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/player_info.h"
#include "engine/objects/entity.h"
#include "engine/objects/tracker.h"
#include "game/rider/rider.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/device.h"

namespace ScriptCommands {

namespace {

using OnlinePlay::Hub;
using Scripts::CStruct;

constexpr uint32_t kIndex = 0x7F8C98FE;          // "index"
constexpr uint32_t kPos = 0x7F261953;            // "pos"
constexpr uint32_t kColObjId = 0xF79C90AE;       // "colObjId"
constexpr uint32_t kId = 0x40C698AF;             // "id"
constexpr uint32_t kSkaterId = 0x5B24FAAA;       // "SkaterId"
constexpr uint32_t kControlType = 0x81CFF663;    // "control_type"
constexpr uint32_t kKeyboardAnchor = 0x31631B98; // "keyboard_anchor"
constexpr uint32_t kLobbyEnterKbChat = 0x6C41FCB3; // "lobby_enter_kb_chat"
constexpr uint32_t kObjId = 0x0830ECAF;          // "ObjId"
constexpr uint32_t kType = 0x7321A8D6;           // "type"
constexpr uint32_t kAmmoType = 0x1D1A8B59;       // "ammo_type"
constexpr uint32_t kVel = 0x0C4C809E;            // "vel"
constexpr uint32_t kScale = 0x13B9DA7B;          // "scale"
constexpr uint32_t kRadius = 0xC48391A5;         // "radius"

// The messages (game group 8, sequenced).
constexpr uint8_t kMsgObjectPickedUp = 0x90;   // ReportObjectPickup: the object's id
constexpr uint8_t kMsgNeedToPlaceObjects = 0x91;
constexpr uint8_t kMsgHuntStarted = 0x93;
constexpr uint8_t kMsgBoardVehicle = 0x89;     // BroadcastEnterVehicle: the skater and the control type
constexpr uint8_t kMsgObjectPlacements = 0x8F; // a player's placed objects (from the server) / one placed (to it)
constexpr uint8_t kMsgShotFired = 0x83;       // BroadcastProjectile
constexpr int kMaxPlaced = 5;                  // (the placements message's room)
constexpr uint8_t kGroupGame = 8;

Hub* NetSessionSlot() { return OnlinePlay::g_onlineHub; }

// A hunt player's placed objects: how many (+0x34) and where (+0x180, 16 bytes each).
int& PlacedCount(uint8_t* player) { return OnlinePlay::Detail::AsPlayer(player)->m_unknown34; }
Scripts::VectorValue* PlacedPositions(uint8_t* player) { return reinterpret_cast<Scripts::VectorValue*>(player + 0x180); }

// The player whose skater the script runs on (null without an object or a player).
uint8_t* ScriptPlayer(Scripts::CScript* script)
{
    GameObjects::CEntity* object = script->m_entity.m_ownerObject;
    if (!object)
        return nullptr;
    return NetSessionSlot()->FindPlayerSlotByObj(static_cast<uint16_t>(object->m_ident));
}

// A message without data to every player (or every remote one) through the server.
void AnnounceToPlayers(uint8_t id, bool remoteOnly)
{
    Hub* manager = NetSessionSlot();
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    NetLink::AppHost* server = manager->m_host;
    OnlinePlay::Detail::PlayerSearch search;
    for (uint8_t* player = OnlinePlay::Detail::StartRosterSweep(manager, &search, true); player;
         player = OnlinePlay::Detail::StepRosterSweep(manager, &search, true)) {
        if (remoteOnly && OnlinePlay::Detail::IsOwnPlayer(player))
            continue;
        desc.m_ident = id;
        desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
        desc.m_groupId = kGroupGame;
        server->PutOnOutbox(OnlinePlay::Detail::PlayerHandle(player), &desc);
    }
}

} // namespace

// 0x005B1EE0: PlaceObject pos=(x,y,z): an object placed by the script's skater's player: index, the number placed
// before, and the position kept. False without an object or a player.
bool __cdecl CFunc_PlaceObject(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint8_t* player = ScriptPlayer(script);
    if (!player)
        return false;
    script->m_args->PutInt(kIndex, PlacedCount(player));
    Scripts::VectorValue pos = {}; // (uninitialised in the original without pos)
    params->FetchVec(kPos, &pos, 0);
    PlacedPositions(player)[PlacedCount(player)++] = pos;
    return true;
}

// 0x005B1F70: AnnounceHuntStarted: the hunt-started message to every remote player. Always true.
bool __cdecl CFunc_AnnounceHuntStarted(Scripts::CStruct*, Scripts::CScript*)
{
    AnnounceToPlayers(kMsgHuntStarted, true);
    return true;
}

// 0x005B2030: AnnounceNeedToPlaceObjects: the need-to-place-objects message to every player. Always true.
bool __cdecl CFunc_AnnounceNeedToPlaceObjects(Scripts::CStruct*, Scripts::CScript*)
{
    AnnounceToPlayers(kMsgNeedToPlaceObjects, false);
    return true;
}

// 0x005B20E0: AtLeastOneObjectPlaced: whether a player (observers not counted) has placed an object.
bool __cdecl CFunc_AtLeastOneObjectPlaced(Scripts::CStruct*, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    OnlinePlay::Detail::PlayerSearch search;
    for (uint8_t* player = OnlinePlay::Detail::StartRosterSweep(manager, &search, false); player;
         player = OnlinePlay::Detail::StepRosterSweep(manager, &search, false)) {
        if (PlacedCount(player) > 0)
            return true;
    }
    return false;
}

// 0x005B2170: ReportObjectPickup colObjId=<n> id=<object>: the picked-up message (the id) to the server through
// client 0 online, otherwise through the client numbered colObjId (as the original does). Always true.
bool __cdecl CFunc_ReportObjectPickup(Scripts::CStruct* params, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    uint32_t client = 0; // (both uninitialised in the original when missing)
    params->ReadHashValue(kColObjId, &client, 0);
    uint32_t id = 0;
    params->ReadHashValue(kId, &id, 0);
    NetLink::AppHost* app = manager->FetchPatron(manager->InNetGame() ? 0 : static_cast<int>(client));
    uint32_t msg = id;
    desc.m_ident = kMsgObjectPickedUp;
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
    desc.m_groupId = kGroupGame;
    desc.m_content = &msg;
    desc.m_byteCount = sizeof(msg);
    OnlinePlay::Detail::SendToServer(app, &desc);
    return true;
}

// 0x005B2210: PlacedLessThan <n>: whether the script's skater's player has placed fewer than n objects. False without
// an object or a player.
bool __cdecl CFunc_PlacedLessThan(Scripts::CStruct* params, Scripts::CScript* script)
{
    int count = 0; // (uninitialised in the original without n)
    params->FetchIntValue(0u, &count, 0);
    uint8_t* player = ScriptPlayer(script);
    if (!player)
        return false;
    return PlacedCount(player) < count;
}

// 0x005B2260: BroadcastEnterVehicle SkaterId=<n> control_type=<type>: the enter-vehicle message (the skater's id as
// a byte, the control type) to the server through client 0, if there is one. Always true.
bool __cdecl CFunc_BroadcastEnterVehicle(Scripts::CStruct* params, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    uint32_t skater;
    params->ReadHashValue(kSkaterId, &skater, 1);
    uint32_t steerKind;
    params->ReadHashValue(kControlType, &steerKind, 1);
    NetLink::AppHost* client = manager->FetchPatron(0);
    if (client) {
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        struct {
            uint8_t skater;
            uint8_t pad[3];
            uint32_t steerKind;
        } msg;
        msg.skater = static_cast<uint8_t>(skater);
        msg.steerKind = steerKind;
        desc.m_content = &msg;
        desc.m_ident = kMsgBoardVehicle;
        desc.m_byteCount = sizeof(msg);
        desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
        desc.m_groupId = kGroupGame;
        OnlinePlay::Detail::SendToServer(client, &desc);
    }
    return true;
}

// 0x005B2300: LobbyCheckKeyboard: a space typed (the first character waiting) opens the lobby's keyboard chat
// (lobby_enter_kb_chat, the typed characters cleared) unless the keyboard_anchor object is already there. Always
// true.
bool __cdecl CFunc_LobbyCheckKeyboard(Scripts::CStruct*, Scripts::CScript*)
{
    char typed[256];
    if (Sio::GetKeyboardChars(typed) > 0 && typed[0] == ' ') {
        CStruct* check = OnlinePlay::Detail::NewParams();
        check->PutNameKey(kId, kKeyboardAnchor);
        if (!GameObjects::CFunc_ObjectExists(check, nullptr)) {
            Scripts::LaunchScript(kLobbyEnterKbChat, nullptr, nullptr, false, nullptr);
            Sio::ClearKeyboardChars();
        }
        OnlinePlay::Detail::DeleteParams(check);
    }
    return true;
}

// 0x005B4F80: BroadcastObjectPlacements: every player's placed objects sent to every other player (not observers):
// for each player (observers too) a message per other player - its object id byte, the count and the positions
// (the rest of the 5 left as they were from the last one). Always true.
bool __cdecl CFunc_BroadcastObjectPlacements(Scripts::CStruct*, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    OnlinePlay::Detail::PlayerSearch others;
    OnlinePlay::Detail::PlayerSearch receivers;
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    NetLink::AppHost* server = manager->m_host;
    struct {
        uint8_t id;
        uint8_t pad1[3];
        int count;
        Scripts::VectorValue positions[kMaxPlaced];
    } msg;
    desc.m_ident = kMsgObjectPlacements;
    desc.m_content = &msg;
    desc.m_byteCount = sizeof(msg);
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
    desc.m_groupId = kGroupGame;
    for (uint8_t* receiver = OnlinePlay::Detail::StartRosterSweep(manager, &receivers, true); receiver;
         receiver = OnlinePlay::Detail::StepRosterSweep(manager, &receivers, true)) {
        for (uint8_t* player = OnlinePlay::Detail::StartRosterSweep(manager, &others, false); player;
             player = OnlinePlay::Detail::StepRosterSweep(manager, &others, false)) {
            if (player == receiver)
                continue;
            msg.id = static_cast<uint8_t>(OnlinePlay::Detail::AsPlayer(player)->m_skater->m_ident);
            msg.count = PlacedCount(player);
            for (int i = 0; i < PlacedCount(player); ++i)
                msg.positions[i] = PlacedPositions(player)[i];
            server->PutOnOutbox(OnlinePlay::Detail::PlayerHandle(receiver), &desc);
        }
    }
    return true;
}

// 0x005B50F0: ReportObjectPlacement ObjId=<n> pos=(x,y,z): the placement message (the position) to the server through
// client 0 online, otherwise through the client numbered by ObjId (as the original does). Always true.
bool __cdecl CFunc_ReportObjectPlacement(Scripts::CStruct* params, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    uint32_t client = 0; // (both uninitialised in the original when missing)
    params->ReadHashValue(kObjId, &client, 0);
    Scripts::VectorValue pos = {};
    params->FetchVec(kPos, &pos, 0);
    NetLink::AppHost* app = manager->FetchPatron(manager->InNetGame() ? 0 : static_cast<int>(client));
    if (app) {
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        Scripts::VectorValue msg = pos;
        desc.m_content = &msg;
        desc.m_ident = kMsgObjectPlacements;
        desc.m_byteCount = sizeof(msg);
        desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
        desc.m_groupId = kGroupGame;
        OnlinePlay::Detail::SendToServer(app, &desc);
    }
    return true;
}

// 0x005B51B0: BroadcastProjectile ObjId=<n> type= ammo_type= pos= vel= scale= radius=: the projectile message to the
// server through client 0, if there is one. Always true.
bool __cdecl CFunc_BroadcastProjectile(Scripts::CStruct* params, Scripts::CScript*)
{
    NetLink::AppHost* client = NetSessionSlot()->FetchPatron(0);
    if (!client)
        return true;
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    uint32_t id = 0; // (all uninitialised in the original when missing)
    params->ReadHashValue(kObjId, &id, 0);
    uint32_t type = 0;
    params->ReadHashValue(kType, &type, 0);
    uint32_t ammoType = 0;
    params->ReadHashValue(kAmmoType, &ammoType, 0);
    Scripts::VectorValue pos = {};
    params->FetchVec(kPos, &pos, 0);
    Scripts::VectorValue vel = {};
    params->FetchVec(kVel, &vel, 0);
    float scale = 0.0f;
    params->FetchFloat(kScale, &scale, 0);
    int radius = 0;
    params->FetchIntValue(kRadius, &radius, 0);
    struct {
        uint32_t zero;
        Scripts::VectorValue pos;
        Scripts::VectorValue vel;
        uint32_t type;
        uint32_t ammoType;
        int radius;
        float scale;
        uint8_t id;
        uint8_t pad35[3];
    } msg;
    static_assert(sizeof(msg) == 0x38, "the message's size");
    msg.id = static_cast<uint8_t>(id);
    msg.pos = pos;
    msg.vel = vel;
    msg.radius = radius;
    msg.scale = scale;
    msg.type = type;
    msg.ammoType = ammoType;
    msg.zero = 0;
    desc.m_ident = kMsgShotFired;
    desc.m_content = &msg;
    desc.m_byteCount = sizeof(msg);
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
    desc.m_groupId = kGroupGame;
    OnlinePlay::Detail::SendToServer(client, &desc);
    return true;
}

} // namespace ScriptCommands

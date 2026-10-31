// Script commands (CFunctions) of the network game: the hunt, vehicles, projectiles, the lobby keyboard (see
// hunt_cfuncs.h).

#include "game/net/hunt_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/skater/skater.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/device.h"

namespace CFuncs {

namespace {

using GameNet::Manager;
using Script::CStruct;

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
constexpr uint8_t kMsgEnterVehicle = 0x89;     // BroadcastEnterVehicle: the skater and the control type
constexpr uint8_t kMsgObjectPlacements = 0x8F; // a player's placed objects (from the server) / one placed (to it)
constexpr uint8_t kMsgProjectile = 0x83;       // BroadcastProjectile
constexpr int kMaxPlaced = 5;                  // (the placements message's room)
constexpr uint8_t kGroupGame = 8;

Manager* NetManager() { return GameNet::g_gameNetManager; }

// A hunt player's placed objects: how many (+0x34) and where (+0x180, 16 bytes each).
int& PlacedCount(uint8_t* player) { return GameNet::Detail::AsPlayer(player)->m_unknown34; }
Script::VectorValue* PlacedPositions(uint8_t* player) { return reinterpret_cast<Script::VectorValue*>(player + 0x180); }

// The player whose skater the script runs on (null without an object or a player).
uint8_t* ScriptPlayer(Script::CScript* script)
{
    Obj::CObject* object = script->m_object.mp_object;
    if (!object)
        return nullptr;
    return NetManager()->PlayerByObjectId(static_cast<uint16_t>(object->m_id));
}

// A message without data to every player (or every remote one) through the server.
void AnnounceToPlayers(uint8_t id, bool remoteOnly)
{
    Manager* manager = NetManager();
    Net::MsgDesc desc;
    desc.Construct();
    Net::App* server = manager->m_server;
    GameNet::Detail::PlayerSearch search;
    for (uint8_t* player = GameNet::Detail::FirstPlayer(manager, &search, true); player;
         player = GameNet::Detail::NextPlayer(manager, &search, true)) {
        if (remoteOnly && GameNet::Detail::IsLocalPlayer(player))
            continue;
        desc.m_id = id;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        server->EnqueueMessage(GameNet::Detail::PlayerHandle(player), &desc);
    }
}

} // namespace

// 0x005B1EE0: PlaceObject pos=(x,y,z): an object placed by the script's skater's player: index, the number placed
// before, and the position kept. False without an object or a player.
bool __cdecl CFunc_PlaceObject(Script::CStruct* params, Script::CScript* script)
{
    uint8_t* player = ScriptPlayer(script);
    if (!player)
        return false;
    script->m_params->AddInteger(kIndex, PlacedCount(player));
    Script::VectorValue pos = {}; // (uninitialised in the original without pos)
    params->GetVector(kPos, &pos, 0);
    PlacedPositions(player)[PlacedCount(player)++] = pos;
    return true;
}

// 0x005B1F70: AnnounceHuntStarted: the hunt-started message to every remote player. Always true.
bool __cdecl CFunc_AnnounceHuntStarted(Script::CStruct*, Script::CScript*)
{
    AnnounceToPlayers(kMsgHuntStarted, true);
    return true;
}

// 0x005B2030: AnnounceNeedToPlaceObjects: the need-to-place-objects message to every player. Always true.
bool __cdecl CFunc_AnnounceNeedToPlaceObjects(Script::CStruct*, Script::CScript*)
{
    AnnounceToPlayers(kMsgNeedToPlaceObjects, false);
    return true;
}

// 0x005B20E0: AtLeastOneObjectPlaced: whether a player (observers not counted) has placed an object.
bool __cdecl CFunc_AtLeastOneObjectPlaced(Script::CStruct*, Script::CScript*)
{
    Manager* manager = NetManager();
    GameNet::Detail::PlayerSearch search;
    for (uint8_t* player = GameNet::Detail::FirstPlayer(manager, &search, false); player;
         player = GameNet::Detail::NextPlayer(manager, &search, false)) {
        if (PlacedCount(player) > 0)
            return true;
    }
    return false;
}

// 0x005B2170: ReportObjectPickup colObjId=<n> id=<object>: the picked-up message (the id) to the server through
// client 0 online, otherwise through the client numbered colObjId (as the original does). Always true.
bool __cdecl CFunc_ReportObjectPickup(Script::CStruct* params, Script::CScript*)
{
    Manager* manager = NetManager();
    Net::MsgDesc desc;
    desc.Construct();
    uint32_t client = 0; // (both uninitialised in the original when missing)
    params->GetChecksum(kColObjId, &client, 0);
    uint32_t id = 0;
    params->GetChecksum(kId, &id, 0);
    Net::App* app = manager->GetClient(manager->InNetGame() ? 0 : static_cast<int>(client));
    uint32_t msg = id;
    desc.m_id = kMsgObjectPickedUp;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    desc.m_data = &msg;
    desc.m_length = sizeof(msg);
    GameNet::Detail::SendToServer(app, &desc);
    return true;
}

// 0x005B2210: PlacedLessThan <n>: whether the script's skater's player has placed fewer than n objects. False without
// an object or a player.
bool __cdecl CFunc_PlacedLessThan(Script::CStruct* params, Script::CScript* script)
{
    int count = 0; // (uninitialised in the original without n)
    params->GetInteger(0u, &count, 0);
    uint8_t* player = ScriptPlayer(script);
    if (!player)
        return false;
    return PlacedCount(player) < count;
}

// 0x005B2260: BroadcastEnterVehicle SkaterId=<n> control_type=<type>: the enter-vehicle message (the skater's id as
// a byte, the control type) to the server through client 0, if there is one. Always true.
bool __cdecl CFunc_BroadcastEnterVehicle(Script::CStruct* params, Script::CScript*)
{
    Manager* manager = NetManager();
    uint32_t skater;
    params->GetChecksum(kSkaterId, &skater, 1);
    uint32_t controlType;
    params->GetChecksum(kControlType, &controlType, 1);
    Net::App* client = manager->GetClient(0);
    if (client) {
        Net::MsgDesc desc;
        desc.Construct();
        struct {
            uint8_t skater;
            uint8_t pad[3];
            uint32_t controlType;
        } msg;
        msg.skater = static_cast<uint8_t>(skater);
        msg.controlType = controlType;
        desc.m_data = &msg;
        desc.m_id = kMsgEnterVehicle;
        desc.m_length = sizeof(msg);
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        GameNet::Detail::SendToServer(client, &desc);
    }
    return true;
}

// 0x005B2300: LobbyCheckKeyboard: a space typed (the first character waiting) opens the lobby's keyboard chat
// (lobby_enter_kb_chat, the typed characters cleared) unless the keyboard_anchor object is already there. Always
// true.
bool __cdecl CFunc_LobbyCheckKeyboard(Script::CStruct*, Script::CScript*)
{
    char typed[256];
    if (Sio::GetKeyboardChars(typed) > 0 && typed[0] == ' ') {
        CStruct* check = GameNet::Detail::NewParams();
        check->AddChecksum(kId, kKeyboardAnchor);
        if (!Obj::CFunc_ObjectExists(check, nullptr)) {
            Script::RunScript(kLobbyEnterKbChat, nullptr, nullptr, false, nullptr);
            Sio::ClearKeyboardChars();
        }
        GameNet::Detail::DeleteParams(check);
    }
    return true;
}

// 0x005B4F80: BroadcastObjectPlacements: every player's placed objects sent to every other player (not observers):
// for each player (observers too) a message per other player - its object id byte, the count and the positions
// (the rest of the 5 left as they were from the last one). Always true.
bool __cdecl CFunc_BroadcastObjectPlacements(Script::CStruct*, Script::CScript*)
{
    Manager* manager = NetManager();
    GameNet::Detail::PlayerSearch others;
    GameNet::Detail::PlayerSearch receivers;
    Net::MsgDesc desc;
    desc.Construct();
    Net::App* server = manager->m_server;
    struct {
        uint8_t id;
        uint8_t pad1[3];
        int count;
        Script::VectorValue positions[kMaxPlaced];
    } msg;
    desc.m_id = kMsgObjectPlacements;
    desc.m_data = &msg;
    desc.m_length = sizeof(msg);
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    for (uint8_t* receiver = GameNet::Detail::FirstPlayer(manager, &receivers, true); receiver;
         receiver = GameNet::Detail::NextPlayer(manager, &receivers, true)) {
        for (uint8_t* player = GameNet::Detail::FirstPlayer(manager, &others, false); player;
             player = GameNet::Detail::NextPlayer(manager, &others, false)) {
            if (player == receiver)
                continue;
            msg.id = static_cast<uint8_t>(GameNet::Detail::AsPlayer(player)->m_skater->m_id);
            msg.count = PlacedCount(player);
            for (int i = 0; i < PlacedCount(player); ++i)
                msg.positions[i] = PlacedPositions(player)[i];
            server->EnqueueMessage(GameNet::Detail::PlayerHandle(receiver), &desc);
        }
    }
    return true;
}

// 0x005B50F0: ReportObjectPlacement ObjId=<n> pos=(x,y,z): the placement message (the position) to the server through
// client 0 online, otherwise through the client numbered by ObjId (as the original does). Always true.
bool __cdecl CFunc_ReportObjectPlacement(Script::CStruct* params, Script::CScript*)
{
    Manager* manager = NetManager();
    uint32_t client = 0; // (both uninitialised in the original when missing)
    params->GetChecksum(kObjId, &client, 0);
    Script::VectorValue pos = {};
    params->GetVector(kPos, &pos, 0);
    Net::App* app = manager->GetClient(manager->InNetGame() ? 0 : static_cast<int>(client));
    if (app) {
        Net::MsgDesc desc;
        desc.Construct();
        Script::VectorValue msg = pos;
        desc.m_data = &msg;
        desc.m_id = kMsgObjectPlacements;
        desc.m_length = sizeof(msg);
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        GameNet::Detail::SendToServer(app, &desc);
    }
    return true;
}

// 0x005B51B0: BroadcastProjectile ObjId=<n> type= ammo_type= pos= vel= scale= radius=: the projectile message to the
// server through client 0, if there is one. Always true.
bool __cdecl CFunc_BroadcastProjectile(Script::CStruct* params, Script::CScript*)
{
    Net::App* client = NetManager()->GetClient(0);
    if (!client)
        return true;
    Net::MsgDesc desc;
    desc.Construct();
    uint32_t id = 0; // (all uninitialised in the original when missing)
    params->GetChecksum(kObjId, &id, 0);
    uint32_t type = 0;
    params->GetChecksum(kType, &type, 0);
    uint32_t ammoType = 0;
    params->GetChecksum(kAmmoType, &ammoType, 0);
    Script::VectorValue pos = {};
    params->GetVector(kPos, &pos, 0);
    Script::VectorValue vel = {};
    params->GetVector(kVel, &vel, 0);
    float scale = 0.0f;
    params->GetFloat(kScale, &scale, 0);
    int radius = 0;
    params->GetInteger(kRadius, &radius, 0);
    struct {
        uint32_t zero;
        Script::VectorValue pos;
        Script::VectorValue vel;
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
    desc.m_id = kMsgProjectile;
    desc.m_data = &msg;
    desc.m_length = sizeof(msg);
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    GameNet::Detail::SendToServer(client, &desc);
    return true;
}

} // namespace CFuncs

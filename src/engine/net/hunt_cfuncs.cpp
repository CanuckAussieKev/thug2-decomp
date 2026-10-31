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

namespace commands {

namespace {

using online::Hub;
using script::Struct;

constexpr uint32_t kIndex = 0x7F8C98FE;            // "index"
constexpr uint32_t kPos = 0x7F261953;              // "pos"
constexpr uint32_t kColObjId = 0xF79C90AE;         // "colObjId"
constexpr uint32_t kId = 0x40C698AF;               // "id"
constexpr uint32_t kSkaterId = 0x5B24FAAA;         // "SkaterId"
constexpr uint32_t kControlType = 0x81CFF663;      // "control_type"
constexpr uint32_t kKeyboardAnchor = 0x31631B98;   // "keyboard_anchor"
constexpr uint32_t kLobbyEnterKbChat = 0x6C41FCB3; // "lobby_enter_kb_chat"
constexpr uint32_t kObjId = 0x0830ECAF;            // "ObjId"
constexpr uint32_t kType = 0x7321A8D6;             // "type"
constexpr uint32_t kAmmoType = 0x1D1A8B59;         // "ammo_type"
constexpr uint32_t kVel = 0x0C4C809E;              // "vel"
constexpr uint32_t kScale = 0x13B9DA7B;            // "scale"
constexpr uint32_t kRadius = 0xC48391A5;           // "radius"

// The messages (game group 8, sequenced).
constexpr uint8_t kMsgObjectPickedUp = 0x90; // ReportObjectPickup: the object's id
constexpr uint8_t kMsgNeedToPlaceObjects = 0x91;
constexpr uint8_t kMsgHuntStarted = 0x93;
constexpr uint8_t kMsgBoardVehicle = 0x89;     // BroadcastEnterVehicle: the skater and the control type
constexpr uint8_t kMsgObjectPlacements = 0x8F; // a player's placed objects (from the server) / one placed (to it)
constexpr uint8_t kMsgShotFired = 0x83;        // BroadcastProjectile
constexpr int kMaxPlaced = 5;                  // (the placements message's room)
constexpr uint8_t kGroupGame = 8;

Hub* net_session_slot() { return online::g_online_hub; }

// A hunt player's placed objects: how many (+0x34) and where (+0x180, 16 bytes each).
int& placed_count(uint8_t* player) { return online::detail::as_player(player)->unknown34_; }
script::VectorValue* placed_positions(uint8_t* player)
{
    return reinterpret_cast<script::VectorValue*>(player + 0x180);
}

// The player whose skater the script runs on (null without an object or a player).
uint8_t* script_player(script::Script* script)
{
    objects::Entity* object = script->entity_.owner_object_;
    if (!object)
        return nullptr;
    return net_session_slot()->find_player_slot_by_obj(static_cast<uint16_t>(object->ident_));
}

// A message without data to every player (or every remote one) through the server.
void announce_to_players(uint8_t id, bool remote_only)
{
    Hub* manager = net_session_slot();
    net::NetMessageInfo desc;
    desc.make_new();
    net::AppHost* server = manager->host_;
    online::detail::PlayerSearch search;
    for (uint8_t* player = online::detail::start_roster_sweep(manager, &search, true); player;
         player = online::detail::step_roster_sweep(manager, &search, true)) {
        if (remote_only && online::detail::is_own_player(player))
            continue;
        desc.ident_ = id;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        server->put_on_outbox(online::detail::player_handle(player), &desc);
    }
}

} // namespace

// 0x005B1EE0: PlaceObject pos=(x,y,z): an object placed by the script's skater's player: index, the number placed
// before, and the position kept. False without an object or a player.
bool __cdecl cfunc_place_object(script::Struct* params, script::Script* script)
{
    uint8_t* player = script_player(script);
    if (!player)
        return false;
    script->args_->put_int(kIndex, placed_count(player));
    script::VectorValue pos = {}; // (uninitialised in the original without pos)
    params->fetch_vec(kPos, &pos, 0);
    placed_positions(player)[placed_count(player)++] = pos;
    return true;
}

// 0x005B1F70: AnnounceHuntStarted: the hunt-started message to every remote player. Always true.
bool __cdecl cfunc_announce_hunt_started(script::Struct*, script::Script*)
{
    announce_to_players(kMsgHuntStarted, true);
    return true;
}

// 0x005B2030: AnnounceNeedToPlaceObjects: the need-to-place-objects message to every player. Always true.
bool __cdecl cfunc_announce_need_to_place_objects(script::Struct*, script::Script*)
{
    announce_to_players(kMsgNeedToPlaceObjects, false);
    return true;
}

// 0x005B20E0: AtLeastOneObjectPlaced: whether a player (observers not counted) has placed an object.
bool __cdecl cfunc_at_least_one_object_placed(script::Struct*, script::Script*)
{
    Hub* manager = net_session_slot();
    online::detail::PlayerSearch search;
    for (uint8_t* player = online::detail::start_roster_sweep(manager, &search, false); player;
         player = online::detail::step_roster_sweep(manager, &search, false)) {
        if (placed_count(player) > 0)
            return true;
    }
    return false;
}

// 0x005B2170: ReportObjectPickup colObjId=<n> id=<object>: the picked-up message (the id) to the server through
// client 0 online, otherwise through the client numbered colObjId (as the original does). Always true.
bool __cdecl cfunc_report_object_pickup(script::Struct* params, script::Script*)
{
    Hub* manager = net_session_slot();
    net::NetMessageInfo desc;
    desc.make_new();
    uint32_t client = 0; // (both uninitialised in the original when missing)
    params->read_hash_value(kColObjId, &client, 0);
    uint32_t id = 0;
    params->read_hash_value(kId, &id, 0);
    net::AppHost* app = manager->fetch_patron(manager->in_net_game() ? 0 : static_cast<int>(client));
    uint32_t msg = id;
    desc.ident_ = kMsgObjectPickedUp;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    desc.content_ = &msg;
    desc.byte_count_ = sizeof(msg);
    online::detail::send_to_server(app, &desc);
    return true;
}

// 0x005B2210: PlacedLessThan <n>: whether the script's skater's player has placed fewer than n objects. False without
// an object or a player.
bool __cdecl cfunc_placed_less_than(script::Struct* params, script::Script* script)
{
    int count = 0; // (uninitialised in the original without n)
    params->fetch_int_value(0u, &count, 0);
    uint8_t* player = script_player(script);
    if (!player)
        return false;
    return placed_count(player) < count;
}

// 0x005B2260: BroadcastEnterVehicle SkaterId=<n> control_type=<type>: the enter-vehicle message (the skater's id as
// a byte, the control type) to the server through client 0, if there is one. Always true.
bool __cdecl cfunc_broadcast_enter_vehicle(script::Struct* params, script::Script*)
{
    Hub* manager = net_session_slot();
    uint32_t skater;
    params->read_hash_value(kSkaterId, &skater, 1);
    uint32_t steer_kind;
    params->read_hash_value(kControlType, &steer_kind, 1);
    net::AppHost* client = manager->fetch_patron(0);
    if (client) {
        net::NetMessageInfo desc;
        desc.make_new();
        struct {
            uint8_t skater;
            uint8_t pad[3];
            uint32_t steer_kind_;
        } msg;
        msg.skater = static_cast<uint8_t>(skater);
        msg.steer_kind_ = steer_kind;
        desc.content_ = &msg;
        desc.ident_ = kMsgBoardVehicle;
        desc.byte_count_ = sizeof(msg);
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        online::detail::send_to_server(client, &desc);
    }
    return true;
}

// 0x005B2300: LobbyCheckKeyboard: a space typed (the first character waiting) opens the lobby's keyboard chat
// (lobby_enter_kb_chat, the typed characters cleared) unless the keyboard_anchor object is already there. Always
// true.
bool __cdecl cfunc_lobby_check_keyboard(script::Struct*, script::Script*)
{
    char typed[256];
    if (sio::get_keyboard_chars(typed) > 0 && typed[0] == ' ') {
        Struct* check = online::detail::new_params();
        check->put_name_key(kId, kKeyboardAnchor);
        if (!objects::cfunc_object_exists(check, nullptr)) {
            script::launch_script(kLobbyEnterKbChat, nullptr, nullptr, false, nullptr);
            sio::clear_keyboard_chars();
        }
        online::detail::delete_params(check);
    }
    return true;
}

// 0x005B4F80: BroadcastObjectPlacements: every player's placed objects sent to every other player (not observers):
// for each player (observers too) a message per other player - its object id byte, the count and the positions
// (the rest of the 5 left as they were from the last one). Always true.
bool __cdecl cfunc_broadcast_object_placements(script::Struct*, script::Script*)
{
    Hub* manager = net_session_slot();
    online::detail::PlayerSearch others;
    online::detail::PlayerSearch receivers;
    net::NetMessageInfo desc;
    desc.make_new();
    net::AppHost* server = manager->host_;
    struct {
        uint8_t id;
        uint8_t pad1[3];
        int count;
        script::VectorValue positions[kMaxPlaced];
    } msg;
    desc.ident_ = kMsgObjectPlacements;
    desc.content_ = &msg;
    desc.byte_count_ = sizeof(msg);
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    for (uint8_t* receiver = online::detail::start_roster_sweep(manager, &receivers, true); receiver;
         receiver = online::detail::step_roster_sweep(manager, &receivers, true)) {
        for (uint8_t* player = online::detail::start_roster_sweep(manager, &others, false); player;
             player = online::detail::step_roster_sweep(manager, &others, false)) {
            if (player == receiver)
                continue;
            msg.id = static_cast<uint8_t>(online::detail::as_player(player)->skater_->ident_);
            msg.count = placed_count(player);
            for (int i = 0; i < placed_count(player); ++i)
                msg.positions[i] = placed_positions(player)[i];
            server->put_on_outbox(online::detail::player_handle(receiver), &desc);
        }
    }
    return true;
}

// 0x005B50F0: ReportObjectPlacement ObjId=<n> pos=(x,y,z): the placement message (the position) to the server through
// client 0 online, otherwise through the client numbered by ObjId (as the original does). Always true.
bool __cdecl cfunc_report_object_placement(script::Struct* params, script::Script*)
{
    Hub* manager = net_session_slot();
    uint32_t client = 0; // (both uninitialised in the original when missing)
    params->read_hash_value(kObjId, &client, 0);
    script::VectorValue pos = {};
    params->fetch_vec(kPos, &pos, 0);
    net::AppHost* app = manager->fetch_patron(manager->in_net_game() ? 0 : static_cast<int>(client));
    if (app) {
        net::NetMessageInfo desc;
        desc.make_new();
        script::VectorValue msg = pos;
        desc.content_ = &msg;
        desc.ident_ = kMsgObjectPlacements;
        desc.byte_count_ = sizeof(msg);
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        online::detail::send_to_server(app, &desc);
    }
    return true;
}

// 0x005B51B0: BroadcastProjectile ObjId=<n> type= ammo_type= pos= vel= scale= radius=: the projectile message to the
// server through client 0, if there is one. Always true.
bool __cdecl cfunc_broadcast_projectile(script::Struct* params, script::Script*)
{
    net::AppHost* client = net_session_slot()->fetch_patron(0);
    if (!client)
        return true;
    net::NetMessageInfo desc;
    desc.make_new();
    uint32_t id = 0; // (all uninitialised in the original when missing)
    params->read_hash_value(kObjId, &id, 0);
    uint32_t type = 0;
    params->read_hash_value(kType, &type, 0);
    uint32_t ammo_type = 0;
    params->read_hash_value(kAmmoType, &ammo_type, 0);
    script::VectorValue pos = {};
    params->fetch_vec(kPos, &pos, 0);
    script::VectorValue vel = {};
    params->fetch_vec(kVel, &vel, 0);
    float scale = 0.0f;
    params->fetch_float(kScale, &scale, 0);
    int radius = 0;
    params->fetch_int_value(kRadius, &radius, 0);
    struct {
        uint32_t zero;
        script::VectorValue pos;
        script::VectorValue vel;
        uint32_t type;
        uint32_t ammo_type_;
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
    msg.ammo_type_ = ammo_type;
    msg.zero = 0;
    desc.ident_ = kMsgShotFired;
    desc.content_ = &msg;
    desc.byte_count_ = sizeof(msg);
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    online::detail::send_to_server(client, &desc);
    return true;
}

} // namespace commands

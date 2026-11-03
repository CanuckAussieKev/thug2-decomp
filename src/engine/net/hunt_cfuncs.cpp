// Script commands (CFunctions) of the network game: the hunt, vehicles, projectiles, the lobby keyboard (see
// hunt_cfuncs.h).

#include "engine/net/hunt_cfuncs.h"

#include <cstddef>
#include <cstdint>
#include <array>

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
#include "formats/byte_stream.h"
#include "wire/play_messages.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include "wire/message_ids.h"

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
// (their formats: wire/play_messages.h)
using online::MessageId;


// A hunt player's placed objects: how many (+0x34) and where (+0x180, 16 bytes each).
int& placed_count(online::PlayerRecord* player) { return player->num_placed(); }
auto& placed_positions(online::PlayerRecord* player) { return player->placed(); }

// The player whose skater the script runs on (null without an object or a player).
online::PlayerRecord* script_player(script::Script* script)
{
    const objects::Entity* object = script->entity().get_pull();
    if (!object)
        return nullptr;
    return online::online_hub()->find_player_slot_by_obj(static_cast<uint16_t>(object->ident()));
}

// A message without data to every player (or every remote one) through the server.
void announce_to_players(MessageId id, bool remote_only)
{
    Hub* manager = online::online_hub();
    net::NetMessageInfo desc;
    net::AppHost* server = manager->host();
    online::detail::PlayerSearch search;
    for (online::PlayerRecord* player = online::detail::start_roster_sweep(manager, &search, true); player;
         player = online::detail::step_roster_sweep(manager, &search, true)) {
        if (remote_only && online::detail::is_own_player(player))
            continue;
        desc.set_ident(id);
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(online::MessageGroup::kGame);
        server->put_on_outbox(online::detail::player_handle(player), &desc);
    }
}

} // namespace

// 0x005B1EE0: PlaceObject pos=(x,y,z): an object placed by the script's skater's player: index, the number placed
// before, and the position kept. False without an object or a player.
bool cfunc_place_object(script::Struct* params, script::Script* script)
{
    online::PlayerRecord* player = script_player(script);
    if (!player)
        return false;
    script->args()->put_int(checksums::Name{kIndex}, placed_count(player));
    script::VectorValue pos = {}; // (uninitialised in the original without pos)
    params->fetch_vec(kPos, &pos, 0);
    core::at(placed_positions(player), placed_count(player)++) = pos;
    return true;
}

// 0x005B1F70: AnnounceHuntStarted: the hunt-started message to every remote player. Always true.
bool cfunc_announce_hunt_started(script::Struct* /*params*/, script::Script* /*script*/)
{
    announce_to_players(online::MessageId::kHuntStarted, true);
    return true;
}

// 0x005B2030: AnnounceNeedToPlaceObjects: the need-to-place-objects message to every player. Always true.
bool cfunc_announce_need_to_place_objects(script::Struct* /*params*/, script::Script* /*script*/)
{
    announce_to_players(online::MessageId::kHuntNeedToPlace, false);
    return true;
}

// 0x005B20E0: AtLeastOneObjectPlaced: whether a player (observers not counted) has placed an object.
bool cfunc_at_least_one_object_placed(script::Struct* /*params*/, script::Script* /*script*/)
{
    Hub* manager = online::online_hub();
    online::detail::PlayerSearch search;
    for (online::PlayerRecord* player = online::detail::start_roster_sweep(manager, &search, false); player;
         player = online::detail::step_roster_sweep(manager, &search, false)) {
        if (placed_count(player) > 0)
            return true;
    }
    return false;
}

// 0x005B2170: ReportObjectPickup colObjId=<n> id=<object>: the picked-up message (the id) to the server through
// client 0 online, otherwise through the client numbered colObjId (as the original does). Always true.
bool cfunc_report_object_pickup(script::Struct* params, script::Script* /*script*/)
{
    Hub* manager = online::online_hub();
    net::NetMessageInfo desc;
    uint32_t client = 0; // (both uninitialised in the original when missing)
    params->read_hash_value(kColObjId, &client, 0);
    uint32_t id = 0;
    params->read_hash_value(kId, &id, 0);
    net::AppHost* app = manager->fetch_patron(manager->in_net_game() ? 0 : static_cast<int>(client));
    uint32_t msg = id;
    desc.set_ident(online::MessageId::kHuntItemPickedUp);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(online::MessageGroup::kGame);
    desc.set_content(&msg);
    desc.set_byte_count(sizeof(msg));
    online::detail::send_to_server(app, &desc);
    return true;
}

// 0x005B2210: PlacedLessThan <n>: whether the script's skater's player has placed fewer than n objects. False without
// an object or a player.
bool cfunc_placed_less_than(script::Struct* params, script::Script* script)
{
    int count = 0; // (uninitialised in the original without n)
    params->fetch_int_value(0U, &count, 0);
    online::PlayerRecord* player = script_player(script);
    if (!player)
        return false;
    return placed_count(player) < count;
}

// 0x005B2260: BroadcastEnterVehicle SkaterId=<n> control_type=<type>: the enter-vehicle message (the skater's id as
// a byte, the control type) to the server through client 0, if there is one. Always true.
bool cfunc_broadcast_enter_vehicle(script::Struct* params, script::Script* /*script*/)
{
    Hub* manager = online::online_hub();
    uint32_t skater = 0;
    params->read_hash_value(kSkaterId, &skater, 1);
    uint32_t steer_kind = 0;
    params->read_hash_value(kControlType, &steer_kind, 1);
    net::AppHost* client = manager->fetch_patron(0);
    if (client) {
        net::NetMessageInfo desc;
        online::BoardVehicle msg;
        msg.skater_id = static_cast<int8_t>(skater);
        msg.control_type = steer_kind;
        std::array<uint8_t, online::kBoardVehicleSize> data{};
        util::ByteWriter writer(data.data(), sizeof(data));
        online::write_board_vehicle(writer, msg);
        desc.set_content(data.data());
        desc.set_ident(MessageId::kBoardVehicle);
        desc.set_byte_count(sizeof(data));
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(online::MessageGroup::kGame);
        online::detail::send_to_server(client, &desc);
    }
    return true;
}

// 0x005B2300: LobbyCheckKeyboard: a space typed (the first character waiting) opens the lobby's keyboard chat
// (lobby_enter_kb_chat, the typed characters cleared) unless the keyboard_anchor object is already there. Always
// true.
bool cfunc_lobby_check_keyboard(script::Struct* /*params*/, script::Script* /*script*/)
{
    constexpr size_t kTypedSize = 256;
    std::array<char, kTypedSize> typed{};
    if (sio::get_keyboard_chars(typed) > 0 && core::at(typed, 0) == ' ') {
        const script::StructPtr check = script::make_struct();
        check->put_name_key(checksums::Name{kId}, kKeyboardAnchor);
        if (!objects::cfunc_object_exists(check.get(), nullptr)) {
            script::launch_script(kLobbyEnterKbChat, nullptr, nullptr, false, nullptr);
            sio::clear_keyboard_chars();
        }
    }
    return true;
}

// 0x005B4F80: BroadcastObjectPlacements: every player's placed objects sent to every other player (not observers):
// for each player (observers too) a message per other player - its object id byte, the count and the positions
// (the rest of the 5 left as they were from the last one). Always true.
bool cfunc_broadcast_object_placements(script::Struct* /*params*/, script::Script* /*script*/)
{
    Hub* manager = online::online_hub();
    online::detail::PlayerSearch others;
    online::detail::PlayerSearch receivers;
    net::NetMessageInfo desc;
    net::AppHost* server = manager->host();
    online::HuntPlacements msg;
    std::array<uint8_t, online::kHuntPlacementsSize> data{};
    desc.set_ident(MessageId::kHuntPlacements);
    desc.set_content(data.data());
    desc.set_byte_count(sizeof(data));
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(online::MessageGroup::kGame);
    for (online::PlayerRecord* receiver = online::detail::start_roster_sweep(manager, &receivers, true); receiver;
         receiver = online::detail::step_roster_sweep(manager, &receivers, true)) {
        for (online::PlayerRecord* player = online::detail::start_roster_sweep(manager, &others, false); player;
             player = online::detail::step_roster_sweep(manager, &others, false)) {
            if (player == receiver)
                continue;
            msg.player_id = static_cast<int8_t>(player->skater()->ident());
            msg.count = placed_count(player);
            // (NOT ORIGINAL: no more than the message holds - the original wrote a player's sixth on past it)
            for (int i = 0; i < placed_count(player) && i < online::kHuntPlacements; ++i) {
                const script::VectorValue& at = core::at(placed_positions(player), i);
                core::at(core::at(msg.positions, i), 0) = at.x;
                core::at(core::at(msg.positions, i), 1) = at.y;
                core::at(core::at(msg.positions, i), 2) = at.z;
                core::at(core::at(msg.positions, i), 3) = at.w;
            }
            util::ByteWriter writer(data.data(), sizeof(data));
            online::write_hunt_placements(writer, msg);
            server->put_on_outbox(online::detail::player_handle(receiver), &desc);
        }
    }
    return true;
}

// 0x005B50F0: ReportObjectPlacement ObjId=<n> pos=(x,y,z): the placement message (the position) to the server through
// client 0 online, otherwise through the client numbered by ObjId (as the original does). Always true.
bool cfunc_report_object_placement(script::Struct* params, script::Script* /*script*/)
{
    Hub* manager = online::online_hub();
    uint32_t client = 0; // (both uninitialised in the original when missing)
    params->read_hash_value(kObjId, &client, 0);
    script::VectorValue pos = {};
    params->fetch_vec(kPos, &pos, 0);
    net::AppHost* app = manager->fetch_patron(manager->in_net_game() ? 0 : static_cast<int>(client));
    if (app) {
        net::NetMessageInfo desc;
        const std::array<float, 4> at = {{pos.x, pos.y, pos.z, pos.w}};
        std::array<uint8_t, online::kHuntPlacementSize> data{};
        util::ByteWriter writer(data.data(), sizeof(data));
        online::write_hunt_position(writer, at);
        desc.set_content(data.data());
        desc.set_ident(MessageId::kHuntPlacements);
        desc.set_byte_count(sizeof(data));
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(online::MessageGroup::kGame);
        online::detail::send_to_server(app, &desc);
    }
    return true;
}

// 0x005B51B0: BroadcastProjectile ObjId=<n> type= ammo_type= pos= vel= scale= radius=: the projectile message to the
// server through client 0, if there is one. Always true.
bool cfunc_broadcast_projectile(script::Struct* params, script::Script* /*script*/)
{
    net::AppHost* client = online::online_hub()->fetch_patron(0);
    if (!client)
        return true;
    net::NetMessageInfo desc;
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
    float scale = 0.0F;
    params->fetch_float(kScale, &scale, 0);
    int radius = 0;
    params->fetch_int_value(kRadius, &radius, 0);
    online::Projectile msg;
    msg.owner_id = static_cast<int8_t>(id);
    core::at(msg.position, 0) = pos.x;
    core::at(msg.position, 1) = pos.y;
    core::at(msg.position, 2) = pos.z;
    core::at(msg.position, 3) = pos.w;
    core::at(msg.velocity, 0) = vel.x;
    core::at(msg.velocity, 1) = vel.y;
    core::at(msg.velocity, 2) = vel.z;
    core::at(msg.velocity, 3) = vel.w;
    msg.radius = radius;
    msg.scale = scale;
    msg.type = type;
    msg.ammo_type = ammo_type;
    msg.time = 0;
    std::array<uint8_t, online::kProjectileSize> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_projectile(writer, msg);
    desc.set_ident(MessageId::kProjectile);
    desc.set_content(data.data());
    desc.set_byte_count(sizeof(data));
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(online::MessageGroup::kGame);
    online::detail::send_to_server(client, &desc);
    return true;
}

} // namespace commands

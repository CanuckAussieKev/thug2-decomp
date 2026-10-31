// The network game's joining players made real (online::Hub): on a client, a JoinerRecord from the server
// becomes a player with its skater (add_new_player) once its time has come (guest_player_intake_tick); on the server the
// same (host_player_intake_tick) with the messages telling everyone about it (0x0053D9D0-0x0053EB70). See
// gamenet_manager.h.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/net/gamespy.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "game/park_editing/park_editor.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_manager.h"
#include "engine/objects/trick_objects.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "platform/stopwatch.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "game/park_editing/rail_editor.h"
#include "engine/core/code_address.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"

#pragma warning(disable : 4996) // (strcpy: the original's copies of a player's name)

namespace online {

using namespace detail;

namespace {

// JoinerRecord (0x44 bytes; constructor 0x005434D0, destructor 0x00543560) - the fields used here.
constexpr uint32_t kInfoObjId = 0x10; // int8_t
constexpr uint32_t kInfoConn = 0x14;
constexpr uint32_t kInfoTime = 0x18;    // when it may be added (the app's +0x1410: its frame)
constexpr uint32_t kInfoProfile = 0x1C; // RiderIdentity*
constexpr uint32_t kInfoFlags = 0x20;
constexpr uint32_t kInfoTeam = 0x28;
constexpr uint32_t kInfoProfileId = 0x2C;
constexpr uint32_t kInfoRating = 0x30;
constexpr uint32_t kInfoScore = 0x34;
constexpr uint32_t kInfo38 = 0x38;
constexpr uint32_t kInfo3c = 0x3C;
constexpr uint32_t kInfoVehicle = 0x40;
// Its flags.
constexpr uint32_t kNewLocal = 0x01;
constexpr uint32_t kNewObserver = 0x04;
constexpr uint32_t kNewResync = 0x10; // the player again (after a level change)
constexpr uint32_t kNewHasCrown = 0x80;
constexpr uint32_t kNewPlayerFlags = 0x27805; // the flags a player keeps

constexpr uint32_t kSkaterScore = 0xD262802F;      // the score component
constexpr uint32_t kVehicleComponent = 0x18223FD6; // (its vehicle control type at +0x1104)
constexpr uint32_t kResyncedPlayers = 0x1D02CE3B;  // (run after a resync)

Hub* task_manager(void* task) { return prop<Hub*>(task, 0x1C); }
uint8_t* skate_module() { return get_skate(); }

// The skate module's list of skaters (+0x34): how many.
int skater_count(uint8_t* skate)
{
    int count = 0;
    for (auto* node = prop<memory::ListNode*>(skate, 0x34); node->data != reinterpret_cast<void*>(-1) && node;
         node = node->next)
        ++count;
    return count;
}

} // namespace

// 0x0053E440
void Hub::add_new_player(uint8_t* info)
{
    uint8_t* skate = skate_module();
    objects::Entity* skater = nullptr;
    int8_t obj_id = prop<int8_t>(info, kInfoObjId);
    uint32_t flags = prop<uint32_t>(info, kInfoFlags);
    if (!(flags & kNewObserver)) {
        bool split_screen = commands::cfunc_in_split_screen_game(nullptr, nullptr) != 0;
        void* profile = get_call<void*>(
            code_address(static_cast<objects::RiderIdentity* (modules::Skate::*)(int)>(&modules::Skate::fetch_account)),
            skate, split_screen ? static_cast<int>(obj_id) : 0); // the skater's profile
        if (!in_net_game()) {
            skater = get_call<objects::Entity*>(code_address(&modules::Skate::spawn_skater_entity), skate, profile,
                                                true, static_cast<int>(obj_id), skater_count(skate));
        } else if (prop<uint32_t>(info, kInfoFlags) & kNewLocal) {
            skater = get_call<objects::Entity*>(code_address(&modules::Skate::spawn_skater_entity), skate, profile,
                                                true, static_cast<int>(obj_id), 0);
        } else {
            auto* skaters = reinterpret_cast<modules::Skate*>(skate);
            int count = reinterpret_cast<modules::Skate*>(skate)->get_home_rider() ? skaters->count_riders()
                                                                                   : (skaters->count_riders() + 1) & 7;
            skater =
                get_call<objects::Entity*>(code_address(&modules::Skate::spawn_skater_entity), skate,
                                           prop<void*>(info, kInfoProfile), false, static_cast<int>(obj_id), count);
            if (prop<int>(info, kInfoVehicle)) // its vehicle control type
                prop<int>(static_cast<objects::GameEntity*>(skater)->fetch_module(kVehicleComponent), 0x1104) =
                    prop<int>(info, kInfoVehicle);
        }
    }
    uint8_t* player = find_player_slot_by_obj(static_cast<uint16_t>(static_cast<int16_t>(obj_id)));
    if (prop<uint32_t>(info, kInfoFlags) & kNewResync) { // the old player (or the local one) goes
        if (!player)
            player = find_own_player_record(this);
        if (player) {
            remove_player_record(player);
            player = nullptr;
        }
        get_call(code_address(&modules::Skate::teleport_to_spawn), skate, skater, 0, 0);
    }
    if (!player) {
        player =
            new_player(skater, prop<net::Conn*>(info, kInfoConn), prop<uint32_t>(info, kInfoFlags) & kNewPlayerFlags);
        std::strcpy(reinterpret_cast<char*>(player + 0x38), reinterpret_cast<const char*>(info));
        prop<int>(player, 0x30) = prop<int>(info, kInfoTeam);
        prop<int>(player, 0x28) = prop<int>(info, kInfoProfileId);
        prop<int>(player, 0x2C) = prop<int>(info, kInfoRating);
        prop<int>(player, 0x48) = prop<int>(info, kInfoVehicle);
        prop<uint32_t>(player, 0x20) = prop<uint32_t>(info, kInfo38);
        prop<uint32_t>(player, 0x1C) = prop<uint32_t>(info, kInfo3c);
        auto* head = &prop<memory::ListNode>(this, 0x11C);
        auto* node = reinterpret_cast<memory::ListNode*>(player);
        node->prev = head->prev;
        node->next = head;
        head->prev->next = node;
        head->prev = node;
        if (prop<uint8_t>(info, kInfoFlags) & kNewHasCrown)
            as_player(player)->give_crown(true);
    }
    if (prop<uint32_t>(info, kInfoFlags) & kNewResync) {
        prepare_local_face();
        send_custom_tag_texture();
        objects::g_trick_spot_hub->remove_all_clusters(); // the trick objects
        get_run(kResyncedPlayers);
    }
    if (!is_own_player(player)) {
        auto* object = static_cast<objects::GameEntity*>(skater);
        uint8_t* score = prop<uint8_t*>(object->fetch_module(kSkaterScore), 0x18);
        static_cast<objects::Rider*>(skater)->conceal_skater(); // hidden
        get_call(code_address(&modules::PointTally::set_grand_total), score, prop<int>(info, kInfoScore));
    }
}

// 0x0053EA30
void __cdecl Hub::guest_player_intake_tick(void* task)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap_);
    Hub* manager = task_manager(task);
    uint8_t* skate = skate_module();
    net::ConnSearch search = {net::kConnSearchVtable, nullptr};
    auto* node = prop<memory::ListNode*>(manager, 0x13C);
    bool added = false;
    if (node->data != reinterpret_cast<void*>(-1) && node) {
        while (node) {
            memory::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
            auto* info = static_cast<uint8_t*>(node->data);
            if (prop<uint32_t>(info, kInfoTime) > prop<uint32_t>(manager->client_app_, 0x1410))
                break; // (its time hasn't come: the rest wait too)
            manager->add_new_player(info);
            reinterpret_cast<JoinerRecord*>(info)->destroy_body(); // (its destructor)
            memory::Hub::get_pull()->dealloc(info);
            reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(node))[0])(node, 1);
            added = true;
            node = next;
        }
        if (added) {
            for (int i = 0; i < 2; ++i) {
                if (net::AppHost* client = manager->client_slot(i))
                    if (net::Conn* conn = client->begin_conn_walk(&search))
                        conn->stamp_last_heard(0); // (the time spent adding them isn't silence)
            }
            for (int i = 0; i < 8; ++i)
                if (void* skater = get_call<void*>(code_address(&modules::Skate::get_rider_at), skate, i))
                    static_cast<objects::Rider*>(skater)->reset_27c();
            manager->send_ready_response();
        }
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

namespace {

// A player's details for the others (message 0x22, the client's on_player_added_msg 0x0052DB80): a 0x30-byte header and
// its appearance (RiderIdentity::emit_into_buffer, at most 0x1000 bytes).
struct NewPlayerMessage {
    int32_t profile_id_;                 // +0x00
    int32_t rating;                      // +0x04
    uint32_t flags;                      // +0x08
    int32_t score;                       // +0x0C
    uint32_t value10;                    // +0x10 (the player's +0x1C)
    uint32_t value14;                    // +0x14 (the player's +0x20)
    int8_t obj_id_;                      // +0x18
    int8_t team;                         // +0x19
    uint8_t pad1a_[2];                   // (never written)
    int32_t drive_scheme_;               // +0x1C
    char name[kNameSize];                // +0x20
    uint8_t appearance[kAppearanceSize]; // +0x30
};
static_assert(offsetof(NewPlayerMessage, obj_id_) == 0x18 && offsetof(NewPlayerMessage, name) == 0x20 &&
              offsetof(NewPlayerMessage, appearance) == 0x30);
constexpr uint16_t kNewPlayerHeader = 0x30;

constexpr uint8_t kMsgPlayerArrived = 0x22;
constexpr uint8_t kMsgReadyQuery = 0x25;
constexpr uint8_t kMsgJoinAccepted = 0x53;
constexpr uint8_t kMsgParkMap = 0x6C;
constexpr uint8_t kMsgFlushImportant = 0x79;
constexpr uint8_t kMsgRails = 0x88;
constexpr uint16_t kParkMapSize = 0x3A98;
constexpr uint16_t kRailsSize = 0x1132;
constexpr uint8_t kCompressionLzss = 2;
constexpr uint32_t kLoadParkLevel = 0xB664035D; // load_sk5ed_gameplay (the created park)
constexpr uint32_t kPlayerFlagsSent = 0x27880;  // the flags a player's details carry
constexpr int kMidPriority = 0x80;
constexpr int kTopRank = 0xFF;

// The player's details (its object id given), its appearance written into the message; the message's length.
uint16_t fill_new_player_message(NewPlayerMessage& msg, uint8_t* player, int8_t obj_id, uint32_t flags, bool broadband)
{
    msg.obj_id_ = obj_id;
    msg.flags = flags;
    msg.team = static_cast<int8_t>(prop<int>(player, 0x30));
    msg.profile_id_ = prop<int32_t>(player, 0x28);
    msg.rating = prop<int32_t>(player, 0x2C);
    msg.drive_scheme_ = prop<int32_t>(player, 0x48);
    msg.score = 0;
    msg.value14 = 0;
    msg.value10 = 0;
    std::strcpy(msg.name, reinterpret_cast<const char*>(player + 0x38));
    int size = prop<objects::RiderIdentity*>(player, 0x4C)
                   ->emit_into_buffer(msg.appearance, kAppearanceSize, broadband ? 1 : 0);
    return static_cast<uint16_t>(size + kNewPlayerHeader);
}

void send_new_player(net::AppHost* server, net::NetMessageInfo& desc, uint8_t* to, NewPlayerMessage& msg,
                     uint16_t length)
{
    desc.content_ = &msg;
    desc.byte_count_ = length;
    desc.ident_ = kMsgPlayerArrived;
    desc.rank_ = kMidPriority;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    desc.compression_ = kCompressionLzss;
    server->put_on_outbox(player_handle(to), &desc);
    desc.compression_ = 0;
}

void send_word(net::AppHost* server, net::NetMessageInfo& desc, uint8_t* to, uint8_t id, uint32_t* word)
{
    desc.content_ = word;
    desc.byte_count_ = sizeof(*word);
    desc.ident_ = id;
    desc.rank_ = kMidPriority;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    server->put_on_outbox(player_handle(to), &desc);
}

} // namespace

// 0x0053D9D0
void __cdecl Hub::host_player_intake_tick(void* task)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap_);
    Hub* manager = task_manager(task);
    uint8_t* skate = skate_module();
    net::NetMessageInfo desc;
    desc.make_new();
    net::Hub* net = net::Hub::sole();
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    auto* node = prop<memory::ListNode*>(manager, 0x13C);
    if (node->data != reinterpret_cast<void*>(-1) && node) {
        while (node) {
            PlayerSearch search;
            memory::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
            auto* info = static_cast<uint8_t*>(node->data);
            net::AppHost* server = manager->host_;
            if (prop<uint32_t>(info, kInfoTime) > prop<uint32_t>(server, 0x1410))
                break; // (its time hasn't come: the rest wait too)
            auto* conn = prop<net::Conn*>(info, kInfoConn);
            bool broadband = conn->get_link_speed_class() == 1 || net->get_net_link_kind() == 1;
            memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->skater_info_heap_);
            uint8_t* player = manager->new_player(nullptr, conn, prop<uint32_t>(info, kInfoFlags));
            memory::Hub::get_pull()->leave_heap_scope();
            std::strcpy(reinterpret_cast<char*>(player + 0x38), reinterpret_cast<const char*>(info));
            as_player(player)->set_profile(
                prop<objects::RiderIdentity*>(info, kInfoProfile)); // its appearance (a copy of the profile)
            prop<uint32_t>(player, 0x1D8) = prop<uint32_t>(info, 0x24);
            prop<int>(player, 0x28) = prop<int>(info, kInfoProfileId);
            prop<int>(player, 0x2C) = prop<int>(info, kInfoRating);
            prop<int>(player, 0x48) = prop<int>(info, kInfoVehicle);
            prop<uint32_t>(player, 0x20) = 0;
            prop<uint32_t>(player, 0x1C) = 0;
            auto* head = &prop<memory::ListNode>(manager, 0x11C);
            auto* player_node = reinterpret_cast<memory::ListNode*>(player);
            player_node->prev = head->prev;
            player_node->next = head;
            head->prev->next = player_node;
            head->prev = player_node;
            uint32_t now = frame_clock::get_time();
            NewPlayerMessage msg;
            int8_t obj_id = prop<int8_t>(info, kInfoObjId);
            if (!is_own_player(player)) { // its own details (no appearance) after its important messages
                msg.obj_id_ = obj_id;
                msg.team = static_cast<int8_t>(prop<int>(player, 0x30));
                msg.profile_id_ = prop<int32_t>(player, 0x28);
                msg.rating = prop<int32_t>(player, 0x2C);
                msg.drive_scheme_ = prop<int32_t>(player, 0x48);
                msg.score = 0;
                msg.value14 = 0;
                msg.value10 = 0;
                std::strcpy(msg.name, reinterpret_cast<const char*>(info));
                msg.flags = 1;
                if (prop<uint32_t>(player, 0x1D0) & kNewResync)
                    msg.flags = 0x11;
                if (is_observer(player))
                    msg.flags |= 4;
                desc.content_ = nullptr;
                desc.byte_count_ = 0;
                desc.ident_ = kMsgFlushImportant;
                desc.rank_ = kTopRank;
                server->put_on_outbox(player_handle(player), &desc);
                desc.content_ = &msg;
                desc.byte_count_ = kNewPlayerHeader;
                desc.ident_ = kMsgPlayerArrived;
                desc.compression_ = kCompressionLzss;
                server->put_on_outbox(player_handle(player), &desc);
                desc.compression_ = 0;
            }
            if (!is_observer(player)) { // the new player to every other remote player
                for (uint8_t* other = start_roster_sweep(manager, &search, true); other;
                     other = step_roster_sweep(manager, &search, true)) {
                    if (other == player || is_own_player(other))
                        continue;
                    uint16_t length = fill_new_player_message(msg, player, obj_id, 0, false);
                    send_new_player(server, desc, other, msg, length);
                }
            }
            if (prop<uint32_t>(player, 0x1D0) & kNewResync) { // the players of its own group to it
                for (uint8_t* other = start_roster_sweep(manager, &search, false); other;
                     other = step_roster_sweep(manager, &search, false)) {
                    if (other == player || prop<uint32_t>(other, 0x1D8) != prop<uint32_t>(player, 0x1D8))
                        continue;
                    uint16_t length =
                        fill_new_player_message(msg, other, static_cast<int8_t>(get_skater_id(player_skater(other))),
                                                prop<uint32_t>(other, 0x1D0) & kPlayerFlagsSent, broadband);
                    send_new_player(server, desc, player, msg, length);
                }
            } else { // every playing player to it, the created park and its rails, then joined
                for (uint8_t* other = start_roster_sweep(manager, &search, false); other;
                     other = step_roster_sweep(manager, &search, false)) {
                    if (other == player || is_own_player(player))
                        continue;
                    int8_t other_id = static_cast<int8_t>(get_skater_id(player_skater(other)));
                    msg.obj_id_ = other_id;
                    msg.flags = prop<uint32_t>(other, 0x1D0) & kPlayerFlagsSent;
                    msg.team = static_cast<int8_t>(prop<int>(other, 0x30));
                    msg.profile_id_ = prop<int32_t>(other, 0x28);
                    msg.rating = prop<int32_t>(other, 0x2C);
                    msg.drive_scheme_ = prop<int32_t>(other, 0x48);
                    msg.score = manager->get_entrant_points(get_skater_id(player_skater(other))); // (its score)
                    msg.value14 = prop<uint32_t>(other, 0x20);
                    msg.value10 = prop<uint32_t>(other, 0x1C);
                    std::strcpy(msg.name, reinterpret_cast<const char*>(other + 0x38));
                    int size = prop<objects::RiderIdentity*>(other, 0x4C)
                                   ->emit_into_buffer(msg.appearance, kAppearanceSize, broadband ? 1 : 0);
                    send_new_player(server, desc, player, msg, static_cast<uint16_t>(size + kNewPlayerHeader));
                }
                uint32_t level = manager->level_;
                if (!is_own_player(player) && level == kLoadParkLevel) {
                    auto* park = park_editing::ParkBuilderMode::sole();
                    park->write_compressed_map_if_built();
                    server->send_bulk_message(player_handle(player), kMsgParkMap, kParkMapSize,
                                              park->get_packed_park_data(false), "level data", kGroupGame, false, true);
                    park_editing::RailAuthoring::sole(); // (called twice, the first result unused)
                    void* rails = get_call<void*>(code_address(&park_editing::RailAuthoring::get_packed),
                                                  park_editing::RailAuthoring::sole());
                    server->send_bulk_message(player_handle(player), kMsgRails, kRailsSize, rails, "rail data",
                                              kGroupGame, false, true);
                }
                send_word(server, desc, player, kMsgJoinAccepted, &level);
            }
            if (!is_observer(player)) { // its skater; every other player asked whether it's ready
                int count = is_own_player(player) && manager->in_net_game() ? 0 : skater_count(skate);
                bool local_conn = prop<net::Conn*>(player, 0x18)->is_host_local();
                auto* skater = get_call<objects::Entity*>(code_address(&modules::Skate::spawn_skater_entity), skate,
                                                          prop<void*>(info, kInfoProfile), local_conn,
                                                          static_cast<int>(obj_id), count);
                as_player(player)->bind_rider(static_cast<objects::Rider*>(skater));
                if (!is_own_player(player))
                    static_cast<objects::Rider*>(skater)->conceal_skater(); // hidden
                for (uint8_t* other = start_roster_sweep(manager, &search, true); other;
                     other = step_roster_sweep(manager, &search, true)) {
                    if (other == player)
                        continue;
                    send_word(server, desc, other, kMsgReadyQuery, &now);
                    if (!is_own_player(other) && !is_observer(other)) {
                        static_cast<objects::Rider*>(player_skater(other))->reset_27c(); // reset
                        as_player(other)->await_ready_response(now);
                    }
                }
            }
            send_word(server, desc, player, kMsgReadyQuery, &now);
            as_player(player)->await_ready_response(now);
            reinterpret_cast<JoinerRecord*>(info)->destroy_body(); // (its destructor)
            memory::Hub::get_pull()->dealloc(info);
            reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(node))[0])(node, 1);
            node = next;
        }
    }
    memory::Hub::get_pull()->leave_heap_scope();
    Hub* global = g_online_hub;
    global->update_lobby_server();
    if (g_online_active && global->in_net_game() && g_query_reporting)
        game_spy::qr2_send_state_changed(nullptr); // qr2_send_statechanged
}

} // namespace online

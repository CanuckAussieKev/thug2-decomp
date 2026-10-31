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
#include "game/rider/skater_score.h"
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

// A joining player's flags (JoinerRecord::flag_bits_).
constexpr uint32_t kNewLocal = 0x01;
constexpr uint32_t kNewObserver = 0x04;
constexpr uint32_t kNewResync = 0x10; // the player again (after a level change)
constexpr uint32_t kNewHasCrown = 0x80;
constexpr uint32_t kNewPlayerFlags = 0x27805; // the flags a player keeps

constexpr uint32_t kSkaterScore = 0xD262802F;      // the score component
constexpr uint32_t kVehicleComponent = 0x18223FD6; // (its vehicle control type at +0x1104)
constexpr uint32_t kResyncedPlayers = 0x1D02CE3B;  // (run after a resync)

Hub* task_manager(tasks::Task<Hub>* task) { return task->content_; }
modules::Skate* skate_module() { return modules::g_skate_module; }

// The skate module's list of skaters (its head at +0x28): how many.
int skater_count(modules::Skate* skate)
{
    int count = 0;
    for (auto* node = skate->riders_.list_link_.next; node->data != reinterpret_cast<void*>(-1) && node;
         node = node->next)
        ++count;
    return count;
}

} // namespace

// 0x0053E440
void Hub::add_new_player(JoinerRecord* info)
{
    modules::Skate* skate = skate_module();
    objects::Entity* skater = nullptr;
    int8_t obj_id = static_cast<int8_t>(info->entity_key_);
    uint32_t flags = info->flag_bits_;
    if (!(flags & kNewObserver)) {
        bool split_screen = commands::cfunc_in_split_screen_game(nullptr, nullptr) != 0;
        void* profile = get_call<void*>(
            code_address(static_cast<objects::RiderIdentity* (modules::Skate::*)(int)>(&modules::Skate::fetch_account)),
            skate, split_screen ? static_cast<int>(obj_id) : 0); // the skater's profile
        if (!in_net_game()) {
            skater = get_call<objects::Entity*>(code_address(&modules::Skate::spawn_skater_entity), skate, profile,
                                                true, static_cast<int>(obj_id), skater_count(skate));
        } else if (info->flag_bits_ & kNewLocal) {
            skater = get_call<objects::Entity*>(code_address(&modules::Skate::spawn_skater_entity), skate, profile,
                                                true, static_cast<int>(obj_id), 0);
        } else {
            auto* skaters = reinterpret_cast<modules::Skate*>(skate);
            int count = reinterpret_cast<modules::Skate*>(skate)->get_home_rider() ? skaters->count_riders()
                                                                                   : (skaters->count_riders() + 1) & 7;
            skater = get_call<objects::Entity*>(code_address(&modules::Skate::spawn_skater_entity), skate,
                                                info->account_, false, static_cast<int>(obj_id), count);
            if (info->drive_control_mode_) // its vehicle control type
                prop<int>(static_cast<objects::GameEntity*>(skater)->fetch_module(kVehicleComponent), 0x1104) =
                    info->drive_control_mode_;
        }
    }
    PlayerRecord* player = find_player_slot_by_obj(static_cast<uint16_t>(static_cast<int16_t>(obj_id)));
    if (info->flag_bits_ & kNewResync) { // the old player (or the local one) goes
        if (!player)
            player = find_own_player_record(this);
        if (player) {
            remove_player_record(player);
            player = nullptr;
        }
        get_call(code_address(&modules::Skate::teleport_to_spawn), skate, skater, 0, 0);
    }
    if (!player) {
        player = new_player(skater, info->conn_, info->flag_bits_ & kNewPlayerFlags);
        std::strcpy(player->label_, info->label_);
        player->team_ = info->team_;
        player->profile_id_ = info->profile_id_;
        player->skill_points_ = info->skill_points_;
        player->drive_control_mode_ = info->drive_control_mode_;
        player->accumulated_score_ = info->accumulated_score_;
        player->eliminated_time_ = info->eliminated_time_;
        auto* head = &players_;
        auto* node = reinterpret_cast<memory::ListNode*>(player);
        node->prev = head->prev;
        node->next = head;
        head->prev->next = node;
        head->prev = node;
        if (info->flag_bits_ & kNewHasCrown)
            player->give_crown(true);
    }
    if (info->flag_bits_ & kNewResync) {
        prepare_local_face();
        send_custom_tag_texture();
        objects::g_trick_spot_hub->remove_all_clusters(); // the trick objects
        get_run(kResyncedPlayers);
    }
    if (!is_own_player(player)) {
        auto* object = static_cast<objects::GameEntity*>(skater);
        modules::PointTally* score =
            static_cast<objects::RiderPointKeeper*>(object->fetch_module(kSkaterScore))->points_held_;
        static_cast<objects::Rider*>(skater)->conceal_skater(); // hidden
        score->set_grand_total(info->score_);
    }
}

// 0x0053EA30
void __cdecl Hub::guest_player_intake_tick(tasks::Task<Hub>* task)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap_);
    Hub* manager = task_manager(task);
    modules::Skate* skate = skate_module();
    net::ConnSearch search;
    auto* node = manager->joiners_.next; // (the joining players' first node)
    bool added = false;
    if (node->data != reinterpret_cast<void*>(-1) && node) {
        while (node) {
            memory::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
            auto* info = static_cast<JoinerRecord*>(node->data);
            if (info->add_at_frame_ > manager->client_app_->frame_count_)
                break; // (its time hasn't come: the rest wait too)
            manager->add_new_player(info);
            info->destroy_body(); // (its destructor)
            memory::Hub::get_pull()->dealloc(info);
            memory::ListNode::destroy(node); // (its node: 0x005367E0)
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
uint16_t fill_new_player_message(NewPlayerMessage& msg, PlayerRecord* player, int8_t obj_id, uint32_t flags,
                                 bool broadband)
{
    msg.obj_id_ = obj_id;
    msg.flags = flags;
    msg.team = static_cast<int8_t>(player->team_);
    msg.profile_id_ = player->profile_id_;
    msg.rating = player->skill_points_;
    msg.drive_scheme_ = player->drive_control_mode_;
    msg.score = 0;
    msg.value14 = 0;
    msg.value10 = 0;
    std::strcpy(msg.name, player->label_);
    int size = player->account_->emit_into_buffer(msg.appearance, kAppearanceSize, broadband ? 1 : 0);
    return static_cast<uint16_t>(size + kNewPlayerHeader);
}

void send_new_player(net::AppHost* server, net::NetMessageInfo& desc, PlayerRecord* to, NewPlayerMessage& msg,
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

void send_word(net::AppHost* server, net::NetMessageInfo& desc, PlayerRecord* to, uint8_t id, uint32_t* word)
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
void __cdecl Hub::host_player_intake_tick(tasks::Task<Hub>* task)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap_);
    Hub* manager = task_manager(task);
    modules::Skate* skate = skate_module();
    net::NetMessageInfo desc;
    desc.make_new();
    net::Hub* net = net::Hub::sole();
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    auto* node = manager->joiners_.next; // (the joining players' first node)
    if (node->data != reinterpret_cast<void*>(-1) && node) {
        while (node) {
            PlayerSearch search;
            memory::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
            auto* info = static_cast<JoinerRecord*>(node->data);
            net::AppHost* server = manager->host_;
            if (info->add_at_frame_ > server->frame_count_)
                break; // (its time hasn't come: the rest wait too)
            auto* conn = info->conn_;
            bool broadband = conn->get_link_speed_class() == 1 || net->get_net_link_kind() == 1;
            memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->skater_info_heap_);
            PlayerRecord* player = manager->new_player(nullptr, conn, info->flag_bits_);
            memory::Hub::get_pull()->leave_heap_scope();
            std::strcpy(player->label_, info->label_);
            player->set_profile(info->account_); // its appearance (a copy of the profile)
            player->rejoin_group_ = info->rejoin_group_;
            player->profile_id_ = info->profile_id_;
            player->skill_points_ = info->skill_points_;
            player->drive_control_mode_ = info->drive_control_mode_;
            player->accumulated_score_ = 0;
            player->eliminated_time_ = 0;
            auto* head = &manager->players_;
            auto* player_node = reinterpret_cast<memory::ListNode*>(player);
            player_node->prev = head->prev;
            player_node->next = head;
            head->prev->next = player_node;
            head->prev = player_node;
            uint32_t now = frame_clock::get_time();
            NewPlayerMessage msg;
            int8_t obj_id = static_cast<int8_t>(info->entity_key_);
            if (!is_own_player(player)) { // its own details (no appearance) after its important messages
                msg.obj_id_ = obj_id;
                msg.team = static_cast<int8_t>(player->team_);
                msg.profile_id_ = player->profile_id_;
                msg.rating = player->skill_points_;
                msg.drive_scheme_ = player->drive_control_mode_;
                msg.score = 0;
                msg.value14 = 0;
                msg.value10 = 0;
                std::strcpy(msg.name, reinterpret_cast<const char*>(info));
                msg.flags = 1;
                if (player->flag_bits_ & kNewResync)
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
                for (PlayerRecord* other = start_roster_sweep(manager, &search, true); other;
                     other = step_roster_sweep(manager, &search, true)) {
                    if (other == player || is_own_player(other))
                        continue;
                    uint16_t length = fill_new_player_message(msg, player, obj_id, 0, false);
                    send_new_player(server, desc, other, msg, length);
                }
            }
            if (player->flag_bits_ & kNewResync) { // the players of its own group to it
                for (PlayerRecord* other = start_roster_sweep(manager, &search, false); other;
                     other = step_roster_sweep(manager, &search, false)) {
                    if (other == player || other->rejoin_group_ != player->rejoin_group_)
                        continue;
                    uint16_t length =
                        fill_new_player_message(msg, other, static_cast<int8_t>(get_skater_id(player_skater(other))),
                                                other->flag_bits_ & kPlayerFlagsSent, broadband);
                    send_new_player(server, desc, player, msg, length);
                }
            } else { // every playing player to it, the created park and its rails, then joined
                for (PlayerRecord* other = start_roster_sweep(manager, &search, false); other;
                     other = step_roster_sweep(manager, &search, false)) {
                    if (other == player || is_own_player(player))
                        continue;
                    int8_t other_id = static_cast<int8_t>(get_skater_id(player_skater(other)));
                    msg.obj_id_ = other_id;
                    msg.flags = other->flag_bits_ & kPlayerFlagsSent;
                    msg.team = static_cast<int8_t>(other->team_);
                    msg.profile_id_ = other->profile_id_;
                    msg.rating = other->skill_points_;
                    msg.drive_scheme_ = other->drive_control_mode_;
                    msg.score = manager->get_entrant_points(get_skater_id(player_skater(other))); // (its score)
                    msg.value14 = other->accumulated_score_;
                    msg.value10 = other->eliminated_time_;
                    std::strcpy(msg.name, other->label_);
                    int size = other->account_->emit_into_buffer(msg.appearance, kAppearanceSize, broadband ? 1 : 0);
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
                bool local_conn = player->conn_->is_host_local();
                auto* skater = get_call<objects::Entity*>(code_address(&modules::Skate::spawn_skater_entity), skate,
                                                          info->account_, local_conn, static_cast<int>(obj_id), count);
                player->bind_rider(static_cast<objects::Rider*>(skater));
                if (!is_own_player(player))
                    static_cast<objects::Rider*>(skater)->conceal_skater(); // hidden
                for (PlayerRecord* other = start_roster_sweep(manager, &search, true); other;
                     other = step_roster_sweep(manager, &search, true)) {
                    if (other == player)
                        continue;
                    send_word(server, desc, other, kMsgReadyQuery, &now);
                    if (!is_own_player(other) && !is_observer(other)) {
                        static_cast<objects::Rider*>(player_skater(other))->reset_27c(); // reset
                        other->await_ready_response(now);
                    }
                }
            }
            send_word(server, desc, player, kMsgReadyQuery, &now);
            player->await_ready_response(now);
            info->destroy_body(); // (its destructor)
            memory::Hub::get_pull()->dealloc(info);
            memory::ListNode::destroy(node); // (its node: 0x005367E0)
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

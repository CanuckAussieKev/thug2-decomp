// The network game's joining players made real (online::Hub): on a client, a JoinerRecord from the server
// becomes a player with its skater (add_new_player) once its time has come (guest_player_intake_tick); on the server the
// same (host_player_intake_tick) with the messages telling everyone about it (0x0053D9D0-0x0053EB70). See
// gamenet_manager.h.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/manager.h"
#include "platform/gamespy.h"
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
#include "platform/code_address.h"
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
#include "game/rider/state_history.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "platform/compiler.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include <gsl/pointers>

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's copies of a player's name)

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
constexpr uint32_t kVehicleComponent = 0x18223FD6; // SkaterStateHistory (its vehicle control type)
constexpr uint32_t kResyncedPlayers = 0x1D02CE3B;  // (run after a resync)

Hub* task_manager(tasks::Task<Hub>* task) { return task->content_; }
modules::Skate* skate_module() { return modules::g_skate_module; }

// The skate module's list of skaters (its head at +0x28): how many.
int skater_count(modules::Skate* skate)
{
    int count = 0;
    for (auto* node = skate->riders_.list_link_.next; node->data != memory::ListNode::head_marker() && node;
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
    auto obj_id = static_cast<int8_t>(info->entity_key_);
    const uint32_t flags = info->flag_bits_;
    if (!(flags & kNewObserver)) {
        const bool split_screen = commands::cfunc_in_split_screen_game(nullptr, nullptr) != 0;
        auto* profile = static_cast<modules::Skate*>(skate)->fetch_account(split_screen ? static_cast<int>(obj_id)
                                                                                        : 0); // the skater's profile
        if (!in_net_game()) {
            skater = static_cast<modules::Skate*>(skate)->spawn_skater_entity(profile, true, static_cast<int>(obj_id),
                                                                              skater_count(skate));
        } else if (info->flag_bits_ & kNewLocal) {
            skater =
                static_cast<modules::Skate*>(skate)->spawn_skater_entity(profile, true, static_cast<int>(obj_id), 0);
        } else {
            auto* skaters = skate;
            const int count = skate->get_home_rider()
                                  ? skaters->count_riders()
                                  : (skaters->count_riders() + 1) & 7;
            skater = static_cast<modules::Skate*>(skate)->spawn_skater_entity(info->account_, false,
                                                                              static_cast<int>(obj_id), count);
            if (info->drive_control_mode_) // its vehicle control type
                core::down_cast<objects::RiderFrameLog>(
                    core::down_cast<objects::GameEntity>(skater)->fetch_module(kVehicleComponent))
                    ->vehicle_control_type_ = static_cast<uint32_t>(info->drive_control_mode_);
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
        static_cast<modules::Skate*>(skate)->teleport_to_spawn(core::down_cast<objects::Rider>(skater), 0, 0);
    }
    if (!player) {
        player = new_player(skater, info->conn_, info->flag_bits_ & kNewPlayerFlags);
        std::strcpy(player->label_.data(), info->label_.data());
        player->team_ = info->team_;
        player->profile_id_ = info->profile_id_;
        player->skill_points_ = info->skill_points_;
        player->drive_control_mode_ = info->drive_control_mode_;
        player->accumulated_score_ = static_cast<int>(info->accumulated_score_);
        player->eliminated_time_ = info->eliminated_time_;
        auto* head = &players_;
        memory::ListNode* node = player;
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
        auto* object = core::down_cast<objects::GameEntity>(skater);
        modules::PointTally* score =
            core::down_cast<objects::RiderPointKeeper>(object->fetch_module(kSkaterScore))->points_held_;
        core::down_cast<objects::Rider>(skater)->conceal_skater(); // hidden
        score->set_grand_total(info->score_);
    }
}

// 0x0053EA30
void Hub::guest_player_intake_tick(tasks::Task<Hub>* task)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap_);
    Hub* manager = task_manager(task);
    modules::Skate* skate = skate_module();
    net::ConnSearch search;
    auto* node = manager->joiners_.next; // (the joining players' first node)
    bool added = false;
    if (node->data != memory::ListNode::head_marker() && node) {
        while (node) {
            memory::ListNode* next = node->next->data == memory::ListNode::head_marker() ? nullptr : node->next;
            auto* info = static_cast<JoinerRecord*>(node->data);
            if (info->add_at_frame_ > manager->client_app_->frame_count_)
                break; // (its time hasn't come: the rest wait too)
            manager->add_new_player(info);
            info->~JoinerRecord(); // (its destructor)
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
                if (void* skater = static_cast<modules::Skate*>(skate)->get_rider_at(i))
                    static_cast<objects::Rider*>(skater)->reset_27c();
            manager->send_ready_response();
        }
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

namespace {

// A player's details for the others (message 0x22, the client's on_player_added_msg 0x0052DB80; the format:
// gamenet_messages.h): the header's fields and the appearance (RiderIdentity::emit_into_buffer, at most 0x1000 bytes).
struct NewPlayerMessage {
    PlayerArrived header;
    std::array<uint8_t, kAppearanceSize> appearance{};
};
constexpr uint16_t kNewPlayerHeader = static_cast<uint16_t>(kPlayerArrivedHeaderSize);
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
    msg.header.object_id = obj_id;
    msg.header.flags = flags;
    msg.header.team = static_cast<int8_t>(player->team_);
    msg.header.profile_id = player->profile_id_;
    msg.header.rating = player->skill_points_;
    msg.header.drive_scheme = player->drive_control_mode_;
    msg.header.score = 0;
    msg.header.accumulated_score = 0;
    msg.header.eliminated_time = 0;
    std::strncpy(msg.header.name.data(), player->label_.data(), kNameSize - 1);
    const int size = player->account_->emit_into_buffer(msg.appearance.data(), kAppearanceSize, broadband ? 1 : 0);
    return static_cast<uint16_t>(size + kNewPlayerHeader);
}

// The message's bytes: the header and the appearance's `length - kNewPlayerHeader` bytes (the message is copied when
// queued).
uint16_t write_new_player(const NewPlayerMessage& msg, uint16_t length, uint8_t* bytes)
{
    util::ByteWriter writer(bytes, kNewPlayerHeader + kAppearanceSize);
    write_player_arrived(writer, msg.header);
    if (length > kNewPlayerHeader)
        writer.bytes(msg.appearance.data(), length - kNewPlayerHeader);
    return static_cast<uint16_t>(writer.position());
}

void send_new_player(net::AppHost* server, net::NetMessageInfo& desc, PlayerRecord* to, NewPlayerMessage& msg,
                     uint16_t length)
{
    static std::array<uint8_t, kNewPlayerHeader + kAppearanceSize> bytes;
    desc.content_ = bytes.data();
    desc.byte_count_ = write_new_player(msg, length, bytes.data());
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
void Hub::host_player_intake_tick(tasks::Task<Hub>* task)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap_);
    Hub* manager = task_manager(task);
    modules::Skate* skate = skate_module();
    net::NetMessageInfo desc;
    net::Hub* net = net::Hub::sole();
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    auto* node = manager->joiners_.next; // (the joining players' first node)
    if (node->data != memory::ListNode::head_marker() && node) {
        while (node) {
            PlayerSearch search;
            memory::ListNode* next = node->next->data == memory::ListNode::head_marker() ? nullptr : node->next;
            auto* info = static_cast<JoinerRecord*>(node->data);
            net::AppHost* server = manager->host_;
            if (info->add_at_frame_ > server->frame_count_)
                break; // (its time hasn't come: the rest wait too)
            auto* conn = info->conn_;
            const bool broadband = conn->get_link_speed_class() == 1 || net->get_net_link_kind() == 1;
            memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->skater_info_heap_);
            PlayerRecord* player = manager->new_player(nullptr, conn, info->flag_bits_);
            memory::Hub::get_pull()->leave_heap_scope();
            std::strcpy(player->label_.data(), info->label_.data());
            player->set_profile(info->account_); // its appearance (a copy of the profile)
            player->rejoin_group_ = info->rejoin_group_;
            player->profile_id_ = info->profile_id_;
            player->skill_points_ = info->skill_points_;
            player->drive_control_mode_ = info->drive_control_mode_;
            player->accumulated_score_ = 0;
            player->eliminated_time_ = 0;
            auto* head = &manager->players_;
            memory::ListNode* player_node = player;
            player_node->prev = head->prev;
            player_node->next = head;
            head->prev->next = player_node;
            head->prev = player_node;
            uint32_t now = frame_clock::get_time();
            static NewPlayerMessage msg; // (the original's is on its stack: 4 KB)
            auto obj_id = static_cast<int8_t>(info->entity_key_);
            if (!is_own_player(player)) { // its own details (no appearance) after its important messages
                msg.header.object_id = obj_id;
                msg.header.team = static_cast<int8_t>(player->team_);
                msg.header.profile_id = player->profile_id_;
                msg.header.rating = player->skill_points_;
                msg.header.drive_scheme = player->drive_control_mode_;
                msg.header.score = 0;
                msg.header.accumulated_score = 0;
                msg.header.eliminated_time = 0;
                std::memset(msg.header.name.data(), 0, sizeof(msg.header.name));
                std::strncpy(msg.header.name.data(), info->label_.data(), kNameSize - 1);
                msg.header.flags = 1;
                if (player->flag_bits_ & kNewResync)
                    msg.header.flags = 0x11;
                if (is_observer(player))
                    msg.header.flags |= 4;
                desc.content_ = nullptr;
                desc.byte_count_ = 0;
                desc.ident_ = kMsgFlushImportant;
                desc.rank_ = kTopRank;
                server->put_on_outbox(player_handle(player), &desc);
                std::array<uint8_t, kNewPlayerHeader> own{};
                util::ByteWriter own_writer(own.data(), sizeof(own));
                write_player_arrived(own_writer, msg.header);
                desc.content_ = own.data();
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
                    const uint16_t length = fill_new_player_message(msg, player, obj_id, 0, false);
                    send_new_player(server, desc, other, msg, length);
                }
            }
            if (player->flag_bits_ & kNewResync) { // the players of its own group to it
                for (PlayerRecord* other = start_roster_sweep(manager, &search, false); other;
                     other = step_roster_sweep(manager, &search, false)) {
                    if (other == player || other->rejoin_group_ != player->rejoin_group_)
                        continue;
                    const uint16_t length =
                        fill_new_player_message(msg, other, static_cast<int8_t>(get_skater_id(player_skater(other))),
                                                other->flag_bits_ & kPlayerFlagsSent, broadband);
                    send_new_player(server, desc, player, msg, length);
                }
            } else { // every playing player to it, the created park and its rails, then joined
                for (PlayerRecord* other = start_roster_sweep(manager, &search, false); other;
                     other = step_roster_sweep(manager, &search, false)) {
                    if (other == player || is_own_player(player))
                        continue;
                    auto other_id = static_cast<int8_t>(get_skater_id(player_skater(other)));
                    msg.header.object_id = other_id;
                    msg.header.flags = other->flag_bits_ & kPlayerFlagsSent;
                    msg.header.team = static_cast<int8_t>(other->team_);
                    msg.header.profile_id = other->profile_id_;
                    msg.header.rating = other->skill_points_;
                    msg.header.drive_scheme = other->drive_control_mode_;
                    msg.header.score = manager->get_entrant_points(get_skater_id(player_skater(other))); // (its score)
                    msg.header.accumulated_score = other->accumulated_score_;
                    msg.header.eliminated_time = other->eliminated_time_;
                    std::memset(msg.header.name.data(), 0, sizeof(msg.header.name));
                    std::strncpy(msg.header.name.data(), other->label_.data(), kNameSize - 1);
                    const int size =
                        other->account_->emit_into_buffer(msg.appearance.data(), kAppearanceSize, broadband ? 1 : 0);
                    send_new_player(server, desc, player, msg, static_cast<uint16_t>(size + kNewPlayerHeader));
                }
                uint32_t level = manager->level_;
                if (!is_own_player(player) && level == kLoadParkLevel) {
                    auto* park = park_editing::ParkBuilderMode::sole();
                    park->write_compressed_map_if_built();
                    server->send_bulk_message(player_handle(player), kMsgParkMap, kParkMapSize,
                                              park->get_packed_park_data(false), "level data", kGroupGame, false, true);
                    park_editing::RailAuthoring::sole(); // (called twice, the first result unused)
                    void* rails =
                        static_cast<park_editing::RailAuthoring*>(park_editing::RailAuthoring::sole())->get_packed();
                    server->send_bulk_message(player_handle(player), kMsgRails, kRailsSize, rails, "rail data",
                                              kGroupGame, false, true);
                }
                send_word(server, desc, player, kMsgJoinAccepted, &level);
            }
            if (!is_observer(player)) { // its skater; every other player asked whether it's ready
                const int count = is_own_player(player) && manager->in_net_game() ? 0 : skater_count(skate);
                const bool local_conn = player->conn_->is_host_local();
                gsl::owner<objects::Rider*> skater = static_cast<modules::Skate*>(skate)->spawn_skater_entity(
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
                        core::down_cast<objects::Rider>(player_skater(other))->reset_27c(); // reset
                        other->await_ready_response(now);
                    }
                }
            }
            send_word(server, desc, player, kMsgReadyQuery, &now);
            player->await_ready_response(now);
            info->~JoinerRecord(); // (its destructor)
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

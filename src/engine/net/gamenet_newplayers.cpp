// The network game's joining players made real (online::Hub): on a client, a JoinerRecord from the server
// becomes a player with its skater (add_new_player) once its time has come (guest_player_intake_tick); on the server the
// same (host_player_intake_tick) with the messages telling everyone about it (0x0053D9D0-0x0053EB70). See
// gamenet_manager.h.

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/enum_ops.h"
#include "platform/gamespy.h"
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
#include "game/skate/point_tally.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include "game/rider/state_history.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "platform/compiler.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/str.h"
#include "engine/net/net_game_state.h"
#include "engine/net/player_info.h"
#include "wire/message_ids.h"
#include <gsl/pointers>

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's copies of a player's name)

namespace online {

using namespace detail;

namespace {
constexpr uint8_t kArrivedResyncFlags = 0x11;

// A joining player's flags (JoinerRecord::flag_bits_).
constexpr PlayerFlag kNewPlayerFlags = PlayerFlag::kLocal | PlayerFlag::kObserver | PlayerFlag::kCarryingFlags | PlayerFlag::kEliminated; // the flags a player keeps (0x27805)

constexpr uint32_t kSkaterScore = 0xD262802F;      // the score component
constexpr uint32_t kVehicleComponent = 0x18223FD6; // SkaterStateHistory (its vehicle control type)
constexpr uint32_t kResyncedPlayers = 0x1D02CE3B;  // (run after a resync)

Hub* task_manager(tasks::Task<Hub>* task) { return task->content(); }

// The skate module's list of skaters (its head at +0x28): how many.
int skater_count(modules::Skate* skate)
{
    int count = 0;
    for (auto* node = skate->riders().list_link().next(); node->data() != memory::ListNode::head_marker() && node;
         node = node->next()) {
        ++count;
    }
    return count;
}

} // namespace

// 0x0053E440
void Hub::add_new_player(JoinerRecord* info)
{
    modules::Skate* skate = modules::Skate::sole();
    gsl::owner<objects::Entity*> skater = nullptr;
    auto obj_id = static_cast<int8_t>(info->entity_key());
    const PlayerFlag flags = info->flag_bits();
    if (!core::any(flags & PlayerFlag::kObserver)) {
        const bool split_screen = commands::cfunc_in_split_screen_game(nullptr, nullptr);
        auto* profile =
            modules::Skate::fetch_account(split_screen ? static_cast<int>(obj_id) : 0); // the skater's profile
        if (!in_net_game()) {
            skater = skate->spawn_skater_entity(profile, true, static_cast<int>(obj_id), skater_count(skate));
        } else if (core::any(info->flag_bits() & PlayerFlag::kLocal)) {
            skater = skate->spawn_skater_entity(profile, true, static_cast<int>(obj_id), 0);
        } else {
            auto* skaters = skate;
            const int count = skate->get_home_rider()
                                  ? skaters->count_riders()
                                  : (skaters->count_riders() + 1) & 7;
            skater = skate->spawn_skater_entity(info->account(), false, static_cast<int>(obj_id), count);
            if (info->drive_control_mode() != 0) { // its vehicle control type
                core::down_cast<objects::RiderFrameLog>(
                    core::down_cast<objects::GameEntity>(skater)->fetch_module(kVehicleComponent))
                    ->set_vehicle_control_type(static_cast<uint32_t>(info->drive_control_mode()));
            }
        }
    }
    PlayerRecord* player = find_player_slot_by_obj(static_cast<uint16_t>(static_cast<int16_t>(obj_id)));
    if (core::any(info->flag_bits() & PlayerFlag::kRejoining)) { // the old player (or the local one) goes
        if (!player) {
            player = find_own_player_record(this);
        }
        if (player) {
            remove_player_record(player);
            player = nullptr;
        }
        skate->teleport_to_spawn(core::down_cast<objects::Rider>(skater), 0, 0);
    }
    if (!player) {
        player = new_player(skater, info->conn(), info->flag_bits() & kNewPlayerFlags);
        text::copy_into(player->label(), info->label().data());
        player->set_team(info->team());
        player->set_profile_id(info->profile_id());
        player->set_skill_points(info->skill_points());
        player->set_drive_control_mode(info->drive_control_mode());
        player->set_accumulated_score(static_cast<int>(info->accumulated_score()));
        player->set_eliminated_time(info->eliminated_time());
        auto* head = &players_;
        memory::ListNode* node = player;
        node->set_prev(head->prev());
        node->set_next(head);
        head->prev()->set_next(node);
        head->set_prev(node);
        if (core::any(info->flag_bits() & PlayerFlag::kKing)) {
            player->give_crown(true);
        }
    }
    if (core::any(info->flag_bits() & PlayerFlag::kRejoining)) {
        prepare_local_face();
        send_custom_tag_texture();
        objects::trick_spot_hub()->remove_all_clusters(); // the trick objects
        get_run(kResyncedPlayers);
    }
    if (!is_own_player(player)) {
        auto* object = core::down_cast<objects::GameEntity>(skater);
        modules::PointTally* score =
            core::down_cast<objects::RiderPointKeeper>(object->fetch_module(kSkaterScore))->points_held();
        core::down_cast<objects::Rider>(skater)->conceal_skater(); // hidden
        score->set_grand_total(info->score());
    }
}

// 0x0053EA30
void Hub::guest_player_intake_tick(tasks::Task<Hub>* task)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap());
    Hub* manager = task_manager(task);
    modules::Skate* skate = modules::Skate::sole();
    net::ConnSearch search;
    auto* node = manager->joiners_.next(); // (the joining players' first node)
    bool added = false;
    if (node->data() != memory::ListNode::head_marker() && node) {
        while (node) {
            memory::ListNode* next = node->next()->data() == memory::ListNode::head_marker() ? nullptr : node->next();
            auto* info = static_cast<JoinerRecord*>(node->data());
            if (info->add_at_frame() > manager->client_app_->frame_count()) {
                break; // (its time hasn't come: the rest wait too)
            }
            manager->add_new_player(info);
            info->~JoinerRecord(); // (its destructor)
            memory::Hub::dealloc(info);
            memory::ListNode::destroy(node); // (its node: 0x005367E0)
            added = true;
            node = next;
        }
        if (added) {
            for (int i = 0; i < 2; ++i) {
                if (net::AppHost* client = manager->client_slot(i)) {
                    if (net::Conn* conn = client->begin_conn_walk(&search)) {
                        conn->stamp_last_heard(0); // (the time spent adding them isn't silence)
                    }
                }
            }
            for (int i = 0; i < online::kPlayerSlotCount; ++i) {
                if (void* skater = skate->get_rider_at(i)) {
                    static_cast<objects::Rider*>(skater)->reset_27c();
                }
            }
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
constexpr uint16_t kParkMapSize = 0x3A98;
constexpr uint16_t kRailsSize = 0x1132;
constexpr uint8_t kCompressionLzss = 2;
constexpr uint32_t kLoadParkLevel = 0xB664035D; // load_sk5ed_gameplay (the created park)
constexpr PlayerFlag kPlayerFlagsSent = PlayerFlag::kKing | PlayerFlag::kCarryingFlags | PlayerFlag::kEliminated; // the flags a player's details carry (0x27880)
constexpr int kMidPriority = 0x80;
constexpr int kTopRank = 0xFF;

// A new player's object id and its flags.
struct PlayerFlags {
    int8_t obj_id_{};
    PlayerFlag flags{};
};

// The player's details (its object id given), its appearance written into the message; the message's length.
uint16_t fill_new_player_message(NewPlayerMessage& msg, PlayerRecord* player, PlayerFlags player_flags,
                                 bool broadband)
{
    const auto obj_id = player_flags.obj_id_;
    const auto flags = player_flags.flags;
    msg.header.object_id = obj_id;
    msg.header.flags = core::to_underlying(flags);
    msg.header.team = static_cast<int8_t>(player->team());
    msg.header.profile_id = player->profile_id();
    msg.header.rating = player->skill_points();
    msg.header.drive_scheme = player->drive_control_mode();
    msg.header.score = 0;
    msg.header.accumulated_score = 0;
    msg.header.eliminated_time = 0;
    std::strncpy(msg.header.name.data(), player->label().data(), kNameSize - 1);
    const int size = player->account()->emit_into_buffer(msg.appearance.data(), kAppearanceSize, broadband ? 1 : 0);
    return static_cast<uint16_t>(size + kNewPlayerHeader);
}

// The message's bytes: the header and the appearance's `length - kNewPlayerHeader` bytes (the message is copied when
// queued).
uint16_t write_new_player(const NewPlayerMessage& msg, uint16_t length, uint8_t* bytes)
{
    util::ByteWriter writer(bytes, kNewPlayerHeader + kAppearanceSize);
    write_player_arrived(writer, msg.header);
    if (length > kNewPlayerHeader) {
        writer.bytes(msg.appearance.data(), length - kNewPlayerHeader);
    }
    return static_cast<uint16_t>(writer.position());
}

void send_new_player(net::AppHost* server, net::NetMessageInfo& desc, PlayerRecord* to, NewPlayerMessage& msg,
                     uint16_t length)
{
    static std::array<uint8_t, kNewPlayerHeader + kAppearanceSize> bytes;
    desc.set_content(bytes.data());
    desc.set_byte_count(write_new_player(msg, length, bytes.data()));
    desc.set_ident(MessageId::kPlayerArrived);
    desc.set_rank(kMidPriority);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(MessageGroup::kGame);
    desc.set_compression(kCompressionLzss);
    server->put_on_outbox(player_handle(to), &desc);
    desc.set_compression(0);
}

void send_word(net::AppHost* server, net::NetMessageInfo& desc, PlayerRecord* to, MessageId id, uint32_t* word)
{
    desc.set_content(word);
    desc.set_byte_count(sizeof(*word));
    desc.set_ident(id);
    desc.set_rank(kMidPriority);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(MessageGroup::kGame);
    server->put_on_outbox(player_handle(to), &desc);
}

// The joining player told its own details (no appearance), after its important messages.
void send_own_details(net::AppHost* server, net::NetMessageInfo& desc, PlayerRecord* player, JoinerRecord* info,
                      NewPlayerMessage& msg, int8_t obj_id)
{
    msg.header.object_id = obj_id;
    msg.header.team = static_cast<int8_t>(player->team());
    msg.header.profile_id = player->profile_id();
    msg.header.rating = player->skill_points();
    msg.header.drive_scheme = player->drive_control_mode();
    msg.header.score = 0;
    msg.header.accumulated_score = 0;
    msg.header.eliminated_time = 0;
    std::memset(msg.header.name.data(), 0, sizeof(msg.header.name));
    std::strncpy(msg.header.name.data(), info->label().data(), kNameSize - 1);
    msg.header.flags = 1;
    if (core::any(player->flag_bits() & PlayerFlag::kRejoining)) {
        msg.header.flags = kArrivedResyncFlags;
    }
    if (is_observer(player)) {
        msg.header.flags |= 4;
    }
    desc.set_content(nullptr);
    desc.set_byte_count(0);
    desc.set_ident(MessageId::kFlushImportant);
    desc.set_rank(kTopRank);
    server->put_on_outbox(player_handle(player), &desc);
    std::array<uint8_t, kNewPlayerHeader> own{};
    util::ByteWriter own_writer(own.data(), sizeof(own));
    write_player_arrived(own_writer, msg.header);
    desc.set_content(own.data());
    desc.set_byte_count(kNewPlayerHeader);
    desc.set_ident(MessageId::kPlayerArrived);
    desc.set_compression(kCompressionLzss);
    server->put_on_outbox(player_handle(player), &desc);
    desc.set_compression(0);
}

// Every playing player sent to the joining one, the created park and its rails, then joined.
void send_roster_to_joiner(Hub* manager, net::AppHost* server, net::NetMessageInfo& desc, PlayerSearch& search,
                           PlayerRecord* player, NewPlayerMessage& msg, bool broadband)
{
    for (PlayerRecord* other = start_roster_sweep(manager, &search, false); other;
         other = step_roster_sweep(manager, &search, false)) {
        if (other == player || is_own_player(player)) {
            continue;
        }
        auto other_id = static_cast<int8_t>(get_skater_id(player_skater(other)));
        msg.header.object_id = other_id;
        msg.header.flags = core::to_underlying(other->flag_bits() & kPlayerFlagsSent);
        msg.header.team = static_cast<int8_t>(other->team());
        msg.header.profile_id = other->profile_id();
        msg.header.rating = other->skill_points();
        msg.header.drive_scheme = other->drive_control_mode();
        msg.header.score = manager->get_entrant_points(get_skater_id(player_skater(other))); // (its score)
        msg.header.accumulated_score = other->accumulated_score();
        msg.header.eliminated_time = other->eliminated_time();
        std::memset(msg.header.name.data(), 0, sizeof(msg.header.name));
        std::strncpy(msg.header.name.data(), other->label().data(), kNameSize - 1);
        const int size = other->account()->emit_into_buffer(msg.appearance.data(), kAppearanceSize, broadband ? 1 : 0);
        send_new_player(server, desc, player, msg, static_cast<uint16_t>(size + kNewPlayerHeader));
    }
    uint32_t level = manager->level();
    if (!is_own_player(player) && level == kLoadParkLevel) {
        park_editing::ParkBuilderMode::write_compressed_map_if_built();
        server->send_bulk_message(player_handle(player), {.msg_id_ = MessageId::kParkMap, .size = kParkMapSize},
                                  park_editing::ParkBuilderMode::get_packed_park_data(false)->bytes().data(),
                                  "level data", MessageGroup::kGame, false, true);
        park_editing::RailAuthoring::sole(); // (called twice, the first result unused)
        void* rails = static_cast<park_editing::RailAuthoring*>(park_editing::RailAuthoring::sole())->get_packed();
        server->send_bulk_message(player_handle(player), {.msg_id_ = MessageId::kRailsData, .size = kRailsSize}, rails,
                                  "rail data", MessageGroup::kGame, false, true);
    }
    send_word(server, desc, player, MessageId::kJoinAccepted, &level);
}

// The joining player's skater; every other player asked whether it's ready.
void spawn_joiner_skater(Hub* manager, modules::Skate* skate, net::AppHost* server, net::NetMessageInfo& desc,
                         PlayerSearch& search, PlayerRecord* player, JoinerRecord* info, int8_t obj_id, uint32_t& now)
{
    const int count = is_own_player(player) && manager->in_net_game() ? 0 : skater_count(skate);
    const bool local_conn = player->conn()->is_host_local();
    gsl::owner<objects::Rider*> skater =
        skate->spawn_skater_entity(info->account(), local_conn, static_cast<int>(obj_id), count);
    player->bind_rider(skater);
    if (!is_own_player(player)) {
        skater->conceal_skater(); // hidden
    }
    for (PlayerRecord* other = start_roster_sweep(manager, &search, true); other;
         other = step_roster_sweep(manager, &search, true)) {
        if (other == player) {
            continue;
        }
        send_word(server, desc, other, MessageId::kReadyQuery, &now);
        if (!is_own_player(other) && !is_observer(other)) {
            core::down_cast<objects::Rider>(player_skater(other))->reset_27c(); // reset
            other->await_ready_response(now);
        }
    }
}

} // namespace

// 0x0053D9D0
void Hub::host_player_intake_tick(tasks::Task<Hub>* task)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap());
    Hub* manager = task_manager(task);
    modules::Skate* skate = modules::Skate::sole();
    net::NetMessageInfo desc;
    const net::Hub* net = net::Hub::sole();
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(MessageGroup::kGame);
    auto* node = manager->joiners_.next(); // (the joining players' first node)
    if (node->data() != memory::ListNode::head_marker() && node) {
        while (node) {
            PlayerSearch search;
            memory::ListNode* next = node->next()->data() == memory::ListNode::head_marker() ? nullptr : node->next();
            auto* info = static_cast<JoinerRecord*>(node->data());
            net::AppHost* server = manager->host_;
            if (info->add_at_frame() > server->frame_count()) {
                break; // (its time hasn't come: the rest wait too)
            }
            auto* conn = info->conn();
            const bool broadband = conn->get_link_speed_class() == 1 || net->get_net_link_kind() == 1;
            memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->skater_info_heap());
            PlayerRecord* player = manager->new_player(nullptr, conn, info->flag_bits());
            memory::Hub::get_pull()->leave_heap_scope();
            text::copy_into(player->label(), info->label().data());
            player->set_profile(info->account()); // its appearance (a copy of the profile)
            player->set_rejoin_group(info->rejoin_group());
            player->set_profile_id(info->profile_id());
            player->set_skill_points(info->skill_points());
            player->set_drive_control_mode(info->drive_control_mode());
            player->set_accumulated_score(0);
            player->set_eliminated_time(0);
            auto* head = &manager->players_;
            memory::ListNode* player_node = player;
            player_node->set_prev(head->prev());
            player_node->set_next(head);
            head->prev()->set_next(player_node);
            head->set_prev(player_node);
            uint32_t now = frame_clock::get_time();
            static NewPlayerMessage msg; // (the original's is on its stack: 4 KB)
            auto obj_id = static_cast<int8_t>(info->entity_key());
            if (!is_own_player(player)) { // its own details (no appearance) after its important messages
                send_own_details(server, desc, player, info, msg, obj_id);
            }
            if (!is_observer(player)) { // the new player to every other remote player
                for (PlayerRecord* other = start_roster_sweep(manager, &search, true); other;
                     other = step_roster_sweep(manager, &search, true)) {
                    if (other == player || is_own_player(other)) {
                        continue;
                    }
                    const uint16_t length = fill_new_player_message(msg, player, {.obj_id_ = obj_id, .flags = PlayerFlag::kNone}, false);
                    send_new_player(server, desc, other, msg, length);
                }
            }
            if (core::any(player->flag_bits() & PlayerFlag::kRejoining)) { // the players of its own group to it
                for (PlayerRecord* other = start_roster_sweep(manager, &search, false); other;
                     other = step_roster_sweep(manager, &search, false)) {
                    if (other == player || other->rejoin_group() != player->rejoin_group()) {
                        continue;
                    }
                    const uint16_t length =
                        fill_new_player_message(msg, other, {.obj_id_ = static_cast<int8_t>(get_skater_id(player_skater(other))), .flags = other->flag_bits() & kPlayerFlagsSent}, broadband);
                    send_new_player(server, desc, player, msg, length);
                }
            } else { // every playing player to it, the created park and its rails, then joined
                send_roster_to_joiner(manager, server, desc, search, player, msg, broadband);
            }
            if (!is_observer(player)) { // its skater; every other player asked whether it's ready
                spawn_joiner_skater(manager, skate, server, desc, search, player, info, obj_id, now);
            }
            send_word(server, desc, player, MessageId::kReadyQuery, &now);
            player->await_ready_response(now);
            info->~JoinerRecord(); // (its destructor)
            memory::Hub::dealloc(info);
            memory::ListNode::destroy(node); // (its node: 0x005367E0)
            node = next;
        }
    }
    memory::Hub::get_pull()->leave_heap_scope();
    Hub* global = net_game_state().online_hub();
    global->update_lobby_server();
    if (net_game_state().online_active() && global->in_net_game() && net_game_state().query_reporting()) {
        game_spy::qr2_send_state_changed(nullptr); // qr2_send_statechanged
    }
}

} // namespace online

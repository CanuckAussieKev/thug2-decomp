// The skate module's network side, the server's part and the rest of the client's (0x00569560-0x0056C91F): the two
// main-loop tasks (the other skaters' updates to each client, the scores every two seconds), the handlers that relay
// a client's messages to the others (as they are, to the skaters near it, sequenced), the scavenger hunt's counts,
// the graffiti / face textures passed on, the client's state-history messages, projectiles and cheats; and the skate
// module's LeaveServer / send_cheats / broadcast_points. From the disassembly; see skate_net.h.

#include "game/skate/skate_net.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_texture.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_lists.h"
#include "engine/net/player_info.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "game/rider/skater_state.h"
#include "game/rider/state_history.h"
#include "game/rider/tagging.h"
#include "engine/util/bitstream.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/model.h"
#include "engine/gfx/anim_blend.h"
#include "engine/objects/compositemgr.h"
#include "engine/core/code_address.h"

namespace gfx {
extern float g_one_over255; // 0x00645D70 (defined in tod_manager.cpp)
} // namespace gfx

namespace modules {

uint8_t g_net_relay_object_id;
float g_relay_time_scale{0.000244140625f}; // (2^-12: the relayed times and positions)

namespace {

using online::PlayerRecord;
using script::Struct;

// Names.
constexpr uint32_t kNetLobby = 0x1C471C60;               // NetLobby (a game mode)
constexpr uint32_t kFirefight = 0x3D6D444F;              // firefight
constexpr uint32_t kNetFirefight = 0xBFF33600;           // netfirefight
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;          // netgoalattack
constexpr uint32_t kShouldModulateColor = 0x11941568;    // should_modulate_color
constexpr uint32_t kSkaterScore = 0xD262802F;            // SkaterScore (the component)
constexpr uint32_t kTagging = 0x5B512EDC;                // Tagging (the component)
constexpr uint32_t kModel = 0x286A8D26;                  // model (the component)
constexpr uint32_t kAnimBlender = 0x7149B753;            // animblender (the component)
constexpr uint32_t kScavengerCollectPeriod = 0xD4F04E7F; // scavenger_collect_period (a goal)
constexpr uint32_t kCreateServerQuitDialog = 0x1D3A6D04; // CreateServerQuitDialog
constexpr uint32_t kPatchCleanup = 0xFF5C39B6;           // PatchCleanup
constexpr uint32_t kHatLogo = 0x5B288E0B;                // hat_logo
constexpr uint32_t kFrontLogo = 0x23A1DF80;              // front_logo
constexpr uint32_t kBackLogo = 0xD0C656A2;               // back_logo
constexpr uint32_t kDeckGraphic = 0x07CB4FA9;            // deck_graphic
constexpr uint32_t kAll = 0xC4E78E22;                    // ALL
constexpr uint32_t kOn = 0xF649D637;                     // on
constexpr uint32_t kCheatFlag = 0xAE94C183;              // cheat_flag
constexpr uint32_t kClientToggleCheat = 0x3CF9E86D;      // client_toggle_cheat
constexpr uint32_t kOwnerId = 0x81C39E06;                // owner_id
constexpr uint32_t kAmmoType = 0x1D1A8B59;               // ammo_type
constexpr uint32_t kX = 0x7323E97C;                      // x
constexpr uint32_t kY = 0x0424D9EA;                      // y
constexpr uint32_t kZ = 0x9D2D8850;                      // z
constexpr uint32_t kScaledX = 0xBDED7A76;                // scaled_x
constexpr uint32_t kScaledY = 0xCAEA4AE0;                // scaled_y
constexpr uint32_t kScaledZ = 0x53E31B5A;                // scaled_z
constexpr uint32_t kRadius = 0xC48391A5;                 // radius
constexpr uint32_t kScale = 0x13B9DA7B;                  // scale
constexpr uint32_t kPos = 0x7F261953;                    // Pos
constexpr uint32_t kVel = 0x0C4C809E;                    // vel
constexpr uint32_t kFireball = 0xD039432C;               // fireball
constexpr uint32_t kClientLaunchFireball = 0xAA2B5552;   // ClientLaunchFireball
constexpr uint32_t kClientLaunchProjectile = 0x866C26A9; // ClientLaunchProjectile

// The cheats send_cheats tells a player about, in order (CHEAT_ALWAYS_SPECIAL twice, as the original).
constexpr uint32_t kSentCheats[] = {
    0xEEFECF1B, // CHEAT_ALWAYS_SPECIAL
    0xB38341C9, // CHEAT_PERFECT_MANUAL
    0xCD09E062, // CHEAT_PERFECT_RAIL
    0x69A1CE96, // CHEAT_PERFECT_SKITCH
    0xEEFECF1B, // CHEAT_ALWAYS_SPECIAL
    0x6B0F24BC, // CHEAT_STATS_13
    0x520A66EF, // FLAG_G_EXPERT_MODE_NO_REVERTS
    0xEBA89179, // FLAG_G_EXPERT_MODE_NO_WALKING
    0xC4FFC672, // FLAG_G_EXPERT_MODE_NO_MANUALS
    0xD5982173, // NO_G_DISPLAY_BALANCE
};

online::Hub* net_link() { return online::g_online_hub; }
goals::MatchRules* get_game_mode() { return goals::g_game_mode; }
objects::ObjectHubBase* entity_hub_handle()
{
    return reinterpret_cast<objects::ObjectHubBase*>(objects::g_composite_registry);
}

template <typename T>
T& prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
// thiscall calls into code not rewritten yet, by return type.
template <typename R = void, typename... A>
R get_call(uint32_t address, const void* object, A... args)
{
    return reinterpret_cast<R(__thiscall*)(const void*, A...)>(address)(object, args...);
}

PlayerRecord* details_of(uint8_t* player) { return reinterpret_cast<PlayerRecord*>(player); }
PlayerRecord* by_connection(net::Conn* conn) { return details_of(net_link()->find_player_by_sender(conn)); }
PlayerRecord* by_object_id(int id)
{
    return details_of(net_link()->find_player_slot_by_obj(static_cast<uint16_t>(id)));
}
// An object id from a message byte (signed, as the original reads it).
int id_byte(const uint8_t* data, int offset) { return static_cast<int8_t>(data[offset]); }

// A walk over the network manager's players (begin_player_walk / step_player_walk).
struct PlayerSearch : online::PlayerSearch {
    PlayerRecord* get_first(bool observers)
    {
        return static_cast<PlayerRecord*>(net_link()->begin_player_walk(this, observers));
    }
    PlayerRecord* get_next(bool observers)
    {
        return static_cast<PlayerRecord*>(net_link()->step_player_walk(this, observers));
    }
};

// A message on the server app to `player`, sequenced in `group`.
void get_sequence(net::NetMessageInfo& desc, uint8_t group)
{
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = group;
}
void enqueue(net::AppHost* app, PlayerRecord* player, net::NetMessageInfo* desc)
{
    int handle = player->get_link_handle();
    app->put_on_outbox(handle, desc);
}
net::AppHost* server_app() { return reinterpret_cast<net::AppHost*>(net_link()->host_); }

objects::Rider* skater_of(PlayerRecord* player) { return player->skater_; }
uint8_t get_skater_id(objects::Rider* skater) { return prop<uint8_t>(skater, 0x14); }
PointTally* get_score_of(objects::Rider* skater) { return static_cast<PointTally*>(skater->get_points_total()); }

Struct* spawn_param_table()
{
    Struct* memory = script::spawn_param_bag();
    return memory ? memory->prepare_all() : nullptr;
}
void delete_structure(Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

// A skater's state history (+0x27C) and its newest state / message.
objects::RiderFrameLog* get_history(const void* object) { return prop<objects::RiderFrameLog*>(object, 0x27C); }
int& message_count(objects::RiderFrameLog* history) { return prop<int>(history, 0xA6C); }
// The newest message slot made a message of `id`, stamped with the newest state's time.
objects::StateHistoryMessage* new_message(objects::RiderFrameLog* history, uint8_t id)
{
    objects::StateHistoryMessage* message = history->get_latest_message();
    message->ident_ = id;
    auto* state = reinterpret_cast<objects::SkaterStateHistoryFrame*>(history->fetch_newest_snapshot());
    message->change_moment(state->get_time());
    return message;
}
template <typename T>
T& get_at(objects::StateHistoryMessage* message, uint32_t offset)
{
    return prop<T>(message, offset);
}
float get_scaled(uint32_t value, float constant) { return static_cast<float>(static_cast<double>(value) * constant); }

// The relay message: the received one as it is.
void relay(net::NetMessageInfo& desc, const net::InboundMsgCtx* context)
{
    desc.byte_count_ = static_cast<uint16_t>(context->body_size_);
    desc.content_ = context->body_;
    desc.ident_ = context->type_code_;
}

} // namespace

// --- the server's tasks ---------------------------------------------------------------------------------------------

// 0x00569BB0
void SkateNet::server_task(tasks::Task<Skate>* /*task*/)
{
    PlayerSearch search;
    net::AppHost* server = server_app();
    uint8_t buffer[0x1000];
    for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
        RelayStream out;
        if (!player->conn_->is_off_machine())
            continue;
        out.feed_.bind_output_buffer(buffer, sizeof(buffer));
        out.unknown18_ = static_cast<uint32_t>(player->conn_->hdl_);
        out.sent_ = player;
        uint32_t mask = 0;
        int count = 0;
        // (round the skaters from the one after the last sent, as many as the connection takes in one message)
        int start = (player->get_last_sent_skater() + 1) % 8;
        for (int i = 0; i < 8; ++i) {
            int id = (i + start) % 8;
            PlayerRecord* other = by_object_id(id);
            if (!other)
                continue;
            other = by_object_id(static_cast<int>(prop<uint32_t>(skater_of(other), 0x14)));
            if (!other || other == player)
                continue;
            objects::Rider* skater = skater_of(other);
            auto* history = get_history(skater);
            int states = prop<int>(history, 0x18);
            if (!states)
                continue;
            uint16_t newest = prop<uint16_t>(history, 0x1C + ((states - 1) % 20) * 0x84 + 0x78);
            if (newest == prop<uint32_t>(player, 0x70 + prop<uint32_t>(skater, 0x14) * 4))
                continue;
            mask |= 1u << id;
            if (++count >= player->get_skaters_per_update())
                break;
        }
        if (!count)
            continue;
        out.feed_.write_bits(mask, 8);
        for (int i = 0; i < 8; ++i) {
            if (!(mask & (1u << i)))
                continue;
            write_object_update(reinterpret_cast<objects::Entity*>(skater_of(by_object_id(i))), &out);
            player->set_last_sent_skater(i);
        }
        out.feed_.drain();
        int size = out.feed_.get_byte_count();
        if (size) {
            net::NetMessageInfo desc;
            desc.make_new();
            desc.content_ = buffer;
            desc.byte_count_ = static_cast<uint16_t>(size);
            desc.ident_ = 0x23;
            enqueue(server, player, &desc);
        }
    }
}

// 0x0056BE10
void SkateNet::score_task(tasks::Task<Skate>* task)
{
    Skate* skate = task->content_;
    online::Hub* net = net_link();
    if (!net->keeps_score())
        return;
    net::AppHost* server = server_app();
    if (server->clock_ - net->fetch_score_sync_stamp() >= 2000)
        skate->broadcast_points(false);
}

// --- the server's handlers ------------------------------------------------------------------------------------------

// 0x00569E20
int SkateNet::handle_trick_objects(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    PlayerRecord* player = by_connection(context->sender_conn_);
    if (!player)
        return 4;
    PointTally* score = get_score_of(skater_of(player));
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    if (static_cast<int8_t>(data[0]) == 1 && data[0xA] == net->get_game_id())
        score->note_tagged_prop(prop<int>(data, 0xC), prop<int>(data, 0x10), prop<uint32_t>(data, 0x14),
                                reinterpret_cast<uint32_t*>(data + 0x18), true);
    return 1;
}

// 0x00569E90
int SkateNet::handle_game_stats(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    PlayerRecord* player = by_connection(context->sender_conn_);
    if (!player)
        return 4;
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    if (get_game_mode()->get_name_key() == kNetLobby || net->get_game_id() != data[0])
        return 4;
    player->unknown24_ = prop<uint32_t>(data, 4);
    return 1;
}

// 0x00569EF0
int SkateNet::handle_combo_score(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    PlayerRecord* player = by_connection(context->sender_conn_);
    if (!player || !skater_of(player))
        return 4;
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    if (net->get_game_id() != data[0])
        return 4;
    int combo = prop<int>(data, 4);
    PointTally* score = get_score_of(skater_of(player));
    if (combo > score->top_chain_)
        score->top_chain_ = combo;
    goals::MatchRules* mode = get_game_mode();
    if (mode->is_true(kShouldModulateColor) || !mode->keeps_move_points())
        return 1;
    if (mode->keeps_running_total()) {
        score->set_grand_total(score->get_total_score() + combo);
        return 1;
    }
    if (mode->keeps_best_chain() && score->get_total_score() < combo)
        score->set_grand_total(combo);
    return 1;
}

// 0x00569FD0
int SkateNet::handle_player_value48(net::InboundMsgCtx* context)
{
    PlayerRecord* player = by_connection(context->sender_conn_);
    if (!player)
        return 4;
    player->unknown48_ = prop<uint32_t>(context->body_, 4);
    return 1;
}

// 0x0056A000
int SkateNet::handle_relay(net::InboundMsgCtx* context)
{
    PlayerSearch search;
    PlayerRecord* sender = by_connection(context->sender_conn_);
    if (!sender)
        return 4;
    for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
        net::NetMessageInfo desc;
        desc.make_new();
        if (player == sender)
            continue;
        relay(desc, context);
        switch (context->type_code_) {
        case 0x3A:
        case 0x4C:
        case 0x89:
            get_sequence(desc, 8);
            break;
        case 0x99:
            get_sequence(desc, 10);
            break;
        case 0x83: {
            // a projectile in firefight: its owner and the sender's latency put in (once in range)
            uint32_t mode = get_game_mode()->get_name_key();
            if (mode != kFirefight && get_game_mode()->get_name_key() != kNetFirefight)
                break;
            uint8_t* msg = reinterpret_cast<uint8_t*>(context->body_);
            if (!(prop<float>(msg, 0x30) <= 10.0f) || prop<int>(msg, 0x2C) > 0xF0)
                break;
            msg[0x34] = get_skater_id(skater_of(sender));
            prop<int>(msg, 0) = context->sender_conn_->get_mean_ping();
            break;
        }
        default:
            break;
        }
        enqueue(context->receiver_app_, player, &desc);
    }
    return 1;
}

// 0x0056A170
int SkateNet::handle_relay_nearby(net::InboundMsgCtx* context)
{
    PlayerSearch search;
    PlayerRecord* sender = by_connection(context->sender_conn_);
    if (!sender)
        return 4;
    objects::Rider* from = skater_of(sender);
    if (!from)
        return 4;
    for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
        if (player == sender)
            continue;
        if (!player->is_observer()) {
            objects::Rider* to = skater_of(player);
            if (!to)
                continue;
            // (x87: the differences and the sum unrounded)
            double dx = static_cast<double>(prop<float>(to, 0x4C)) - prop<float>(from, 0x4C);
            double dy = static_cast<double>(prop<float>(to, 0x50)) - prop<float>(from, 0x50);
            double dz = static_cast<double>(prop<float>(to, 0x54)) - prop<float>(from, 0x54);
            double distance = std::sqrt(dz * dz + dy * dy + dx * dx);
            if (!(distance < 1800.0))
                continue;
        }
        net::NetMessageInfo desc;
        desc.make_new();
        relay(desc, context);
        enqueue(context->receiver_app_, player, &desc);
    }
    return 1;
}

// 0x0056A2D0
int SkateNet::handle_relay_sequenced(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    PlayerSearch search;
    PlayerRecord* sender = by_connection(context->sender_conn_);
    if (!sender)
        return 4;
    for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
        net::NetMessageInfo desc;
        desc.make_new();
        if (player == sender)
            continue;
        relay(desc, context);
        get_sequence(desc, 8);
        enqueue(context->receiver_app_, player, &desc);
    }
    // a script run on every machine (0x49): the server queues its own
    auto* msg = reinterpret_cast<uint8_t*>(context->body_);
    if (context->type_code_ == 0x49 && msg[0xC])
        net->queue_script(prop<int>(msg, 8), prop<uint32_t>(msg, 4), prop<uint32_t>(msg, 0));
    return 1;
}

// 0x0056A3E0
int SkateNet::handle_joined(net::InboundMsgCtx* context)
{
    PlayerRecord* player = by_connection(context->sender_conn_);
    if (player)
        player->clear_just_joined();
    return 1;
}

// 0x0056AD10
int SkateNet::handle_scavenger_collected(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    PlayerSearch search;
    auto* msg = context->body_;
    PlayerRecord* collector = by_connection(context->sender_conn_);
    if (skater_of(collector)) {
        auto* component = skater_of(collector)->fetch_module(kSkaterScore);
        PointTally* score = prop<PointTally*>(component, 0x18);
        if (score)
            score->set_grand_total(score->get_total_score() + 1);
        int total = 0;
        for (PlayerRecord* player = search.get_first(false); player; player = search.get_next(false))
            total += player->unknown34_;
        goals::MatchRules* mode = get_game_mode();
        int collected = mode->is_squad_match() ? net->get_squad_points(collector->team_) : score->get_total_score();
        net::NetMessageInfo desc;
        uint8_t left[2];
        if (collected == total) {
            // all collected: the collect period over everywhere
            desc.make_new();
            net::AppHost* server = server_app();
            desc.ident_ = 0x94;
            get_sequence(desc, 8);
            for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
                if (!player->is_own_player())
                    enqueue(server, player, &desc);
            }
            goals::Objective* goal = goals::get_objective_board()->get_goal(kScavengerCollectPeriod, 1);
            if (goals::Virtual<bool>(goal, goals::Objective::kVtIsActive))
                goals::Virtual<void>(goal, goals::Objective::kVtExpire);
        } else {
            // five or one left: told to everyone (0x95: the player or the team, how many)
            uint8_t count = 0;
            if (collected == total - 5 && !commands::cfunc_in_split_screen_game(nullptr, nullptr))
                count = 5;
            else if (score->get_total_score() == total - 1 && !commands::cfunc_in_split_screen_game(nullptr, nullptr))
                count = 1;
            if (count) {
                desc.make_new();
                net::AppHost* server = server_app();
                left[0] = get_game_mode()->is_squad_match() ? static_cast<uint8_t>(collector->team_)
                                                            : get_skater_id(skater_of(collector));
                left[1] = count;
                desc.content_ = left;
                desc.ident_ = 0x95;
                get_sequence(desc, 8);
                desc.byte_count_ = 2;
                for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true))
                    enqueue(server, player, &desc);
            }
        }
    }
    // the object picked up: told to the collector's team mates
    if (get_game_mode()->is_squad_match()) {
        net::NetMessageInfo desc;
        desc.make_new();
        net::AppHost* server = server_app();
        for (PlayerRecord* player = search.get_first(false); player; player = search.get_next(false)) {
            if (player == collector || player->team_ != collector->team_)
                continue;
            desc.content_ = msg;
            desc.ident_ = 0x90;
            get_sequence(desc, 8);
            desc.byte_count_ = 4;
            enqueue(server, player, &desc);
        }
    }
    return 1;
}

// 0x0056B0B0
int SkateNet::handle_scavenger_dropped(net::InboundMsgCtx* context)
{
    PlayerSearch search;
    PlayerRecord* dropper = by_connection(context->sender_conn_);
    if (!dropper)
        return 4;
    if (dropper->unknown34_ > 5 || (dropper->unknown34_ == 5 && !dropper->is_own_player()))
        return 4;
    auto* position = reinterpret_cast<const uint32_t*>(context->body_);
    if (!dropper->is_own_player()) {
        auto* to = &prop<uint32_t>(dropper, static_cast<uint32_t>((dropper->unknown34_ + 0x18) * 0x10));
        for (int i = 0; i < 4; ++i)
            to[i] = position[i];
    }
    if (!dropper->is_own_player())
        ++dropper->unknown34_;
    int dropped = 0;
    int players = 0;
    for (PlayerRecord* player = search.get_first(false); player; player = search.get_next(false)) {
        dropped += player->unknown34_;
        ++players;
    }
    if (dropped == players * 5) {
        // every player's five dropped: the hunt goes on (0x92 to everyone)
        net::NetMessageInfo desc;
        desc.make_new();
        net::AppHost* server = server_app();
        for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
            desc.ident_ = 0x92;
            get_sequence(desc, 8);
            enqueue(server, player, &desc);
        }
    }
    return 1;
}

// 0x0056B2B0
int SkateNet::handle_graffiti_texture(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    auto* msg = reinterpret_cast<uint8_t*>(context->body_);
    // (a client's server: the sender; a client: the player the last 0x96 named)
    auto from = [&]() -> PlayerRecord* {
        if (net->get_on_server())
            return by_connection(context->sender_conn_);
        return by_object_id(g_net_relay_object_id);
    };
    // the texture's virtual slot 5 (0x00456B30): its texture and palette
    auto load = [](void* texture, const void* texture_data, uint32_t texture_size, const void* palette,
                   uint32_t palette_size) {
        reinterpret_cast<void(__thiscall*)(void*, const void*, uint32_t, const void*, uint32_t)>(
            prop<void**>(texture, 0)[5])(texture, texture_data, texture_size, palette, palette_size);
    };
    switch (context->type_code_) {
    case 0x96: // the player the next ones are for
        g_net_relay_object_id = msg[0];
        break;
    case 0x98: { // the palette
        PlayerRecord* player = from();
        if (player)
            load(player->get_graffiti_texture(), nullptr, 0, msg, context->body_size_);
        break;
    }
    case 0x97: { // the texture
        PlayerRecord* player = from();
        if (!player)
            break;
        load(player->get_graffiti_texture(), msg, context->body_size_, nullptr, 0);
        player->set_custom_tag(true);
        break;
    }
    case 0x9A: { // which board parts show it - the last: passed on to the others by a server
        bool on_server = false;
        PlayerRecord* player;
        if (net->get_on_server()) {
            player = by_connection(context->sender_conn_);
            on_server = true;
        } else {
            player = by_object_id(g_net_relay_object_id);
        }
        uint32_t parts = 0;
        if (player) {
            parts = prop<uint32_t>(msg, 0);
            if (parts & 1)
                skater_of(player)->show_board_element(kHatLogo);
            if (parts & 2)
                skater_of(player)->show_board_element(kFrontLogo);
            if (parts & 4)
                skater_of(player)->show_board_element(kBackLogo);
            if (parts & 8)
                skater_of(player)->show_board_element(kDeckGraphic);
        }
        if (!on_server || !player)
            break;
        PlayerSearch search;
        auto* texture = static_cast<render::BitmapTexture*>(player->get_graffiti_texture());
        net::AppHost* app = context->receiver_app_;
        for (PlayerRecord* other = search.get_first(true); other; other = search.get_next(true)) {
            net::NetMessageInfo desc;
            desc.make_new();
            if (other->is_own_player() || other == player || other->conn_->get_link_speed_class() == 1)
                continue;
            uint8_t id = get_skater_id(skater_of(player));
            desc.content_ = &id;
            desc.ident_ = 0x96;
            desc.byte_count_ = 1;
            get_sequence(desc, 10);
            enqueue(app, other, &desc);
            // (the texture's palette and pixels: through the geoms' jumps 0x0048DF20 / 0x0048DF30 and 0x0048DF00 /
            // 0x0049F3A0 - the same stub in the engine's and the PC's tables: none)
            auto* palette = const_cast<void*>(texture->get_palette());
            uint32_t palette_size = texture->get_palette_size();
            app->send_bulk_to_conn(other->conn_, 0x98, static_cast<uint16_t>(palette_size), palette, "CAG Palette", 10,
                                   false, true); // "CAG Palette"
            auto* pixels = const_cast<void*>(texture->get_pixels());
            uint32_t pixels_size = texture->get_pixels_size();
            app->send_bulk_to_conn(other->conn_, 0x97, static_cast<uint16_t>(pixels_size), pixels, "CAG Texture", 10,
                                   false, true); // "CAG Texture"
            uint32_t sent_parts = parts;
            desc.content_ = &sent_parts;
            desc.ident_ = 0x9A;
            desc.byte_count_ = 4;
            get_sequence(desc, 10);
            enqueue(app, other, &desc);
        }
        break;
    }
    default:
        break;
    }
    return 1;
}

// 0x0056B5D0
int SkateNet::handle_face_texture(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    void* memory = memory::alloc_zeroed(0x449C);
    void* texture = memory ? reinterpret_cast<void*>(static_cast<gfx::FaceSkin*>(memory)->make_new()) : nullptr;
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    uint32_t size = context->body_size_;
    PlayerRecord* player;
    if (net->get_on_server()) {
        player = by_connection(context->sender_conn_);
    } else {
        // (a client: the player's object id first)
        uint8_t id = *data++;
        --size;
        player = by_object_id(id);
    }
    // (quirk kept: the texture is leaked here)
    if (!player)
        return 4;
    objects::Rider* skater = skater_of(player);
    if (!skater)
        return 4;
    static_cast<gfx::FaceSkin*>(texture)->read_from(data, static_cast<int>(size));
    static_cast<gfx::FaceSkin*>(texture)->mark_usable(true);
    auto* model = skater->fetch_module(kModel);
    static_cast<render::RenderModel*>(prop<void*>(model, 0x208))
        ->replace_face_texture(texture, "CS_NSN_facemap.png", kAll);
    if (net->get_on_server()) {
        // the server keeps the face and streams it to the others
        PlayerSearch search;
        net::AppHost* app = context->receiver_app_;
        PlayerRecord* sender = by_connection(context->sender_conn_);
        sender->set_face_texture_data(reinterpret_cast<const uint8_t*>(context->body_), context->body_size_);
        for (PlayerRecord* other = search.get_first(true); other; other = search.get_next(true)) {
            if (other->is_own_player() || other == sender)
                continue;
            app->send_bulk_to_conn(other->conn_, 0x7E, 0x4499, sender->get_face_texture_data(), "Face Data", 9, false,
                                   true); // "Face Data"
        }
    }
    // (freed without its destructor, as the original)
    memory::Hub::sole()->dealloc(texture);
    return 1;
}

// --- the client's handlers ------------------------------------------------------------------------------------------

// 0x00569560
int SkateNet::on_level_switch_msg(net::InboundMsgCtx* context)
{
    static_cast<Skate*>(context->content_)->change_level(prop<uint32_t>(context->body_, 0));
    return 1;
}

// 0x0056A410
int SkateNet::handle_history_value(net::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    objects::Rider* skater =
        static_cast<Skate*>(context->content_)->get_rider_by_number(static_cast<uint32_t>(id_byte(data, 0)));
    if (!skater)
        return 4;
    auto* history = skater->fetch_module(objects::RiderFrameLog::kType);
    prop<uint32_t>(history, 0x1104) = prop<uint32_t>(data, 4);
    return 1;
}

// 0x0056A450
int SkateNet::handle_skater_messages(net::InboundMsgCtx* context)
{
    auto* skate = static_cast<Skate*>(context->content_);
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    objects::ObjectHubBase* objects = entity_hub_handle();
    // the object (by the message's id byte at `offset`) and, for some, the skater too
    auto object = [&](int offset) { return objects->look_up_entity(static_cast<uint32_t>(id_byte(data, offset))); };
    auto skater = [&](int offset) -> objects::Entity* {
        if (!object(offset))
            return nullptr;
        return reinterpret_cast<objects::Entity*>(
            skate->get_rider_by_number(static_cast<uint32_t>(id_byte(data, offset))));
    };
    objects::Entity* target = nullptr;
    objects::StateHistoryMessage* message;
    int result = 1;
    switch (context->type_code_) {
    case 0x29: {
        uint8_t first = data[0];
        uint8_t id = data[1];
        uint32_t value = prop<uint32_t>(data, 2);
        uint16_t a = prop<uint16_t>(data, 6), b = prop<uint16_t>(data, 8), time = prop<uint16_t>(data, 10);
        uint16_t c = prop<uint16_t>(data, 12);
        target = objects->look_up_entity(static_cast<uint32_t>(static_cast<int8_t>(id)));
        if (!target)
            return 1;
        float scaled_time = get_scaled(time, g_relay_time_scale);
        online::Hub* net = net_link();
        if (net->in_net_game()) {
            // (only the camera's own player's)
            uint8_t* player = net->find_player_slot_by_obj(static_cast<uint16_t>(target->ident_));
            void* camera = net->fetch_viewed_player();
            if (!camera || player != camera)
                scaled_time = 0.0f;
        }
        message = new_message(get_history(target), 0x29);
        get_at<uint32_t>(message, 0x20) = value;
        get_at<float>(message, 0x10) = get_scaled(a, g_relay_time_scale);
        get_at<uint8_t>(message, 1) = id;
        get_at<float>(message, 0x18) = scaled_time;
        get_at<uint8_t>(message, 2) = first;
        get_at<float>(message, 0x14) = get_scaled(b, g_relay_time_scale);
        get_at<float>(message, 0x1C) = get_scaled(c, g_relay_time_scale);
        break;
    }
    case 0x2B:
        target = object(0);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x2B);
        get_at<uint8_t>(message, 1) = data[0];
        get_at<float>(message, 0xC) = get_scaled(data[1], gfx::g_one_over255);
        break;
    case 0x2A:
        target = object(0);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x2A);
        get_at<uint8_t>(message, 1) = data[0];
        get_at<bool>(message, 9) = data[1] != 0;
        break;
    case 0x3C: {
        target = object(1);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x3C);
        get_at<uint8_t>(message, 1) = data[1];
        uint32_t bits = data[0];
        // five settings picked by the bits of [0]: two bits, then one each
        static const float kFirst[4] = {std::bit_cast<float>(0x3D4CCCCDu), std::bit_cast<float>(0x3DCCCCCDu),
                                        std::bit_cast<float>(0x4121999Au),
                                        0.0f}; // 0.05, 0.1, 10.1, 0 (0x00645D6C, 0x00645D74, 0x006486B8, 0x0066D520)
        get_at<float>(message, 0x28) = kFirst[bits & 3];
        get_at<float>(message, 0x2C) = (bits >> 2 & 1)
                                           ? std::bit_cast<float>(0x4121999Au)
                                           : std::bit_cast<float>(0x3D23D70Au); // 10.1 / 0.04 (0x006486B8 / 0x006486B4)
        get_at<float>(message, 0x30) = (bits >> 3 & 1)
                                           ? std::bit_cast<float>(0x41A00000u)
                                           : std::bit_cast<float>(0x3B102DE0u); // 20 / 0.0022 (0x006486AC / 0x006486B0)
        get_at<float>(message, 0x34) = (bits >> 4 & 1)
                                           ? std::bit_cast<float>(0x41200000u)
                                           : std::bit_cast<float>(0x3ADED289u); // 10 / 0.0017 (0x00646710 / 0x006486A8)
        get_at<float>(message, 0x38) = (bits >> 5 & 1)
                                           ? std::bit_cast<float>(0x3F800000u)
                                           : std::bit_cast<float>(0x3FC00000u); // 1 / 1.5 (0x006453A8 / 0x00646744)
        break;
    }
    case 0x3D:
        target = object(0);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x3D);
        get_at<uint8_t>(message, 1) = data[0];
        get_at<uint8_t>(message, 2) = data[1];
        break;
    case 0x3E:
        target = object(4);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x3E);
        get_at<uint8_t>(message, 1) = data[4];
        get_at<uint32_t>(message, 0x20) = prop<uint32_t>(data, 0);
        get_at<bool>(message, 0xA) = data[5] != 0;
        break;
    case 0x41:
        target = object(4);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x41);
        get_at<uint8_t>(message, 1) = data[4];
        get_at<uint32_t>(message, 0x1C) = prop<uint32_t>(data, 0);
        break;
    case 0x2C:
        // (also handled by 0x0056C8C0: 4 lets that run)
        target = skater(0);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x2C);
        get_at<bool>(message, 8) = data[1] != 0;
        get_at<uint8_t>(message, 1) = data[0];
        result = 4;
        break;
    case 0x76:
        target = skater(0xC);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x76);
        get_at<uint8_t>(message, 1) = data[0xC];
        get_at<uint32_t>(message, 0x3C) = prop<uint32_t>(data, 0);
        get_at<uint32_t>(message, 0x40) = prop<uint32_t>(data, 4);
        get_at<float>(message, 0x48) = static_cast<float>(prop<int16_t>(data, 8));
        get_at<float>(message, 0x4C) = static_cast<float>(prop<int16_t>(data, 0xA));
        get_at<uint8_t>(message, 0x50) = data[0xD];
        get_at<uint8_t>(message, 3) = data[0xE];
        break;
    case 0x8C:
        target = skater(0);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x8C);
        get_at<uint8_t>(message, 1) = data[0];
        break;
    case 0x77:
        target = skater(8);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x77);
        get_at<uint8_t>(message, 1) = data[8];
        get_at<uint32_t>(message, 0x24) = prop<uint32_t>(data, 4);
        get_at<int>(message, 0x44) = static_cast<int8_t>(data[9]);
        get_at<uint32_t>(message, 0x20) = prop<uint32_t>(data, 0);
        break;
    case 0x78:
        target = skater(8);
        if (!target)
            return 1;
        message = new_message(get_history(target), 0x78);
        get_at<uint8_t>(message, 1) = data[8];
        get_at<int>(message, 0x44) = static_cast<int8_t>(data[9]);
        break;
    default:
        return 1;
    }
    ++message_count(get_history(target));
    return result;
}

// 0x0056ABD0
int SkateNet::handle_player_left(net::InboundMsgCtx* context)
{
    auto* skate = static_cast<Skate*>(context->content_);
    online::Hub* net = net_link();
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    uint8_t* player = net->find_player_slot_by_obj(static_cast<uint16_t>(id_byte(data, 0)));
    if (player) {
        objects::Rider* skater = details_of(player)->skater_;
        net->evict_player(player, id_byte(data, 1));
        skate->drop_skater_from_roster(skater);
    } else {
        // (one still joining)
        void* joining = net->fetch_incoming_player_by_obj(static_cast<uint16_t>(id_byte(data, 0)));
        if (joining)
            net->remove_new_player(joining);
    }
    net->send_ready_response();
    return 1;
}

// 0x0056AC50
int SkateNet::handle_server_quit(net::InboundMsgCtx* /*context*/)
{
    online::Hub* net = net_link();
    script::launch_script(kCreateServerQuitDialog, nullptr, nullptr, false, nullptr);
    net->remove_players();
    net->disconnect_clients();
    return 3;
}

// 0x0056AC90
int SkateNet::handle_graffiti_objects(net::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    online::Hub* net = net_link();
    if (data[0] != net->get_game_id())
        return 4;
    int owner;
    if (get_game_mode()->is_squad_match())
        owner = details_of(net->find_player_slot_by_obj(prop<uint16_t>(data, 4)))->team_;
    else
        owner = prop<int>(data, 4);
    auto* names = reinterpret_cast<const uint32_t*>(data + 0xC);
    for (uint32_t i = 0; i < prop<uint32_t>(data, 8); ++i)
        objects::g_trick_spot_hub->set_object_graffiti_color(names[i], owner);
    return 1;
}

// 0x0056B220
int SkateNet::handle_place_tag(net::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    PlayerRecord* player = by_object_id(id_byte(data, 0));
    if (!player)
        return 4;
    auto* tagging = static_cast<objects::TaggingComponent*>(skater_of(player)->fetch_module(kTagging));
    if (!tagging)
        return 4;
    maths::Vec4 start = prop<maths::Vec4>(data, 4);
    maths::Vec4 end = prop<maths::Vec4>(data, 0x14);
    tagging->place_tag(data[1] != 0, start, end, prop<int>(data, 0x24), false);
    return 1;
}

// 0x0056B7A0
int SkateNet::on_sparks_msg(net::InboundMsgCtx* context)
{
    auto* object = reinterpret_cast<objects::Rider*>(entity_hub_handle()->look_up_entity(
        static_cast<uint32_t>(id_byte(reinterpret_cast<uint8_t*>(context->body_), 0))));
    if (!object)
        return 1;
    if (context->type_code_ == 0x37)
        object->sparks_on();
    else if (context->type_code_ == 0x38)
        object->sparks_off();
    return 1;
}

// 0x0056B7E0
int SkateNet::handle_look_up_object(net::InboundMsgCtx* context)
{
    // (the object looked up, nothing done with it)
    entity_hub_handle()->look_up_entity(static_cast<uint32_t>(id_byte(reinterpret_cast<uint8_t*>(context->body_), 0)));
    return 1;
}

// 0x0056B800
int SkateNet::handle_terrain_sound(net::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    maths::Vec4 position;
    position.x = static_cast<float>(prop<int16_t>(data, 0));
    position.y = static_cast<float>(prop<int16_t>(data, 2));
    position.z = static_cast<float>(prop<int16_t>(data, 4));
    position.w = 0.0f; // (the original leaves w as the stack had it)
    float volume = static_cast<float>(static_cast<int8_t>(data[8]));
    float pitch = static_cast<float>(static_cast<int8_t>(data[9]));
    float value = static_cast<float>(static_cast<int8_t>(data[0xA]));
    surface_kind::play_terrain_sound(data[6], data[7], &position, volume, pitch, value, false);
    return 1;
}

// 0x0056C330
int SkateNet::handle_run_script_now(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    auto* data = reinterpret_cast<uint32_t*>(context->body_);
    if (!prop<bool>(net, 0x18C)) {
        net->queue_script(static_cast<int>(data[2]), data[1], data[0]);
        return 1;
    }
    objects::Entity* object = entity_hub_handle()->look_up_entity(data[1]);
    script::Script* script =
        script::spawn_script(data[0], nullptr, 0, nullptr, static_cast<int>(data[2]), 0, false, false, false, false);
    script->entity_.assign_value(object);
    script->tick_frame();
    return 1;
}

// 0x0056C3D0
int SkateNet::on_projectile_msg(net::InboundMsgCtx* context)
{
    auto* skate = static_cast<Skate*>(context->content_);
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    objects::Rider* skater = skate->get_rider_by_number(static_cast<uint32_t>(id_byte(data, 0x34)));
    if (!skater)
        return 4;
    maths::Vec4 velocity = prop<maths::Vec4>(data, 0x14);
    maths::Vec4 position = prop<maths::Vec4>(data, 4);
    velocity.make_unit();
    uint32_t time = prop<uint32_t>(data, 0);
    if (!net_link()->get_on_server()) {
        // (a client: the server's latency added)
        net::ConnSearch search;
        net::Conn* conn = context->receiver_app_->begin_conn_walk(&search);
        time += conn->is_host_local() ? 0 : static_cast<uint32_t>(conn->ping_probe_.average_latency_);
    }
    // where it is now: moved along its direction for the time it took (x87: unrounded until stored)
    double seconds = static_cast<double>(time) * static_cast<double>(goals::g_goal_life_seconds_per_millisecond);
    position.x = static_cast<float>(velocity.x * seconds + position.x);
    position.y = static_cast<float>(position.y + velocity.y * seconds);
    position.z = static_cast<float>(position.z + velocity.z * seconds);
    Struct* params = spawn_param_table();
    params->put_name_key(kOwnerId, static_cast<uint32_t>(id_byte(data, 0x34)));
    params->put_name_key(kAmmoType, prop<uint32_t>(data, 0x28));
    params->put_int(kX, static_cast<int>(position.x));
    params->put_int(kY, static_cast<int>(position.y));
    params->put_int(kZ, static_cast<int>(position.z));
    params->put_int(kScaledX, static_cast<int>(prop<float>(data, 0x14)));
    params->put_int(kScaledY, static_cast<int>(prop<float>(data, 0x18)));
    params->put_int(kScaledZ, static_cast<int>(prop<float>(data, 0x1C)));
    params->put_int(kRadius, prop<int>(data, 0x2C));
    params->push_float(kScale, prop<float>(data, 0x30));
    params->put_vec3(kPos, prop<script::VectorValue>(data, 4));
    params->put_vec3(kVel, prop<script::VectorValue>(data, 0x14));
    uint32_t launch = prop<uint32_t>(data, 0x24) == kFireball ? kClientLaunchFireball : kClientLaunchProjectile;
    script::launch_script(launch, params, reinterpret_cast<objects::Entity*>(skater), false, nullptr);
    delete_structure(params);
    return 1;
}

// 0x0056C6E0
int SkateNet::on_cheat_state_msg(net::InboundMsgCtx* context)
{
    commands::cfunc_clear_omnigons(nullptr, nullptr); // (the cheats off first: the function folded there)
    auto* data = reinterpret_cast<uint32_t*>(context->body_);
    for (int i = 0; i < static_cast<int>(data[0]); ++i) {
        Struct* params = spawn_param_table();
        params->put_name_key(kOn, 0);
        params->put_name_key(kCheatFlag, data[1 + i]);
        script::launch_script(kClientToggleCheat, params, nullptr, false, nullptr);
        delete_structure(params);
    }
    return 1;
}

// 0x0056C7E0
int SkateNet::handle_toggle_cheat(net::InboundMsgCtx* context)
{
    Struct* params = spawn_param_table();
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    if (data[4])
        params->put_name_key(kOn, 0);
    params->put_name_key(kCheatFlag, prop<uint32_t>(data, 0));
    script::launch_script(kClientToggleCheat, params, nullptr, false, nullptr);
    delete_structure(params);
    return 1;
}

// 0x0056C8C0
int SkateNet::handle_skater_channel(net::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->body_);
    objects::Entity* object = entity_hub_handle()->look_up_entity(static_cast<uint32_t>(id_byte(data, 0)));
    if (!object)
        return 1;
    auto* blender = static_cast<objects::AnimBlenderComponent*>(
        static_cast<objects::GameEntity*>(object)->fetch_module(kAnimBlender));
    if (!blender)
        return 1;
    gfx::BlendChannel* channel = blender->find_channel(0);
    if (channel)
        channel->set_flipped(static_cast<uint32_t>(id_byte(data, 0)), data[1] != 0, 0, false);
    return 1;
}

// --- the skate module's side ----------------------------------------------------------------------------------------

// 0x0056B890
void Skate::leave_server()
{
    online::Hub* net = net_link();
    script::launch_script(kPatchCleanup, nullptr, nullptr, false, nullptr);
    script::process_pending_symbols();
    if (net->get_on_server()) {
        // the server: everyone told it's over (0x32), the server and its tasks gone
        net::NetMessageInfo desc;
        desc.make_new();
        net::AppHost* server = server_app();
        if (net->get_hosting_role() == 1)
            net->remove_host_task();
        desc.ident_ = 0x32;
        desc.rank_ = 0xFF;
        server->put_on_outbox(0xFF, &desc);
        server->transmit(false);
        net->destroy_server();
        server_tasks_[0]->drop_out();
        server_tasks_[1]->drop_out();
        net->remove_players_and_apps();
        return;
    }
    // a client: the server told it's leaving (0xB)
    net::AppHost* client = net->fetch_patron(0);
    if (client) {
        net::NetMessageInfo desc;
        desc.make_new();
        desc.ident_ = 0xB;
        get_sequence(desc, 8);
        static_cast<net::Guest*>(client)->send_to_host(&desc);
        client->transmit(false);
    }
    net->remove_players_and_apps();
}

// 0x0056B990
void Skate::send_cheats(online::PlayerRecord* player)
{
    online::Hub* net = net_link();
    net::NetMessageInfo desc;
    desc.make_new();
    struct {
        int count;
        uint32_t cheats[32];
    } message;
    message.count = 0;
    for (uint32_t cheat : kSentCheats) {
        if (career_->is_code_on(cheat))
            message.cheats[message.count++] = cheat;
    }
    desc.content_ = &message;
    desc.ident_ = 0x82;
    desc.byte_count_ = static_cast<uint16_t>(message.count * 4 + 4);
    get_sequence(desc, 8);
    int handle = player->get_link_handle();
    reinterpret_cast<net::AppHost*>(net->host_)->put_on_outbox(handle, &desc);
}

// 0x0056BB70
void Skate::broadcast_points(bool final)
{
    online::Hub* net = net_link();
    PlayerSearch players;
    PlayerSearch others;
    // the message (0x40): the time left in seconds, the cheats, final, the count, then 5 bytes a skater (its id and
    // score)
#pragma pack(push, 1)
    struct ScoreMessage {
        uint32_t time_left_;
        uint32_t cheats;
        uint8_t final;
        uint8_t count;
        struct {
            uint8_t id;
            int score;
        } entries[52];
    };
#pragma pack(pop)
    static_assert(sizeof(ScoreMessage) == 0x10E, "the message's layout");
    for (PlayerRecord* player = players.get_first(true); player; player = players.get_next(true)) {
        ScoreMessage message;
        message.cheats = 0;
        message.final = final;
        message.time_left_ = final ? 0u : static_cast<uint32_t>(get_game_mode()->get_goal_time_left()) / 1000u;
        uint8_t count = 0;
        for (PlayerRecord* other = others.get_first(false); other; other = others.get_next(false)) {
            // (its own score too, when it isn't the trick score the player keeps itself)
            goals::MatchRules* mode = get_game_mode();
            bool all = final || mode->is_true(kShouldModulateColor) || !mode->keeps_move_points();
            if (other == player && !all)
                continue;
            uint8_t id = get_skater_id(skater_of(other));
            int score;
            if (get_game_mode()->get_name_key() == kNetGoalAttack)
                score = goals::get_objective_board()->count_cleared_by_player(prop<int>(skater_of(other), 0x14));
            else
                score = net->get_entrant_points(prop<uint32_t>(skater_of(other), 0x14));
            message.entries[count].id = id;
            message.entries[count].score = score;
            ++count;
        }
        if (static_cast<int8_t>(count) <= 0)
            continue;
        net::NetMessageInfo desc;
        desc.make_new();
        uint32_t cheats = get_cheats();
        desc.content_ = &message;
        message.count = count;
        message.cheats = cheats;
        desc.ident_ = 0x40;
        desc.byte_count_ = static_cast<uint16_t>((static_cast<int8_t>(count) + 2) * 5);
        if (final || cheats)
            get_sequence(desc, 8);
        enqueue(server_app(), player, &desc);
        if (player->is_own_player() || !player->has_finished_joining())
            continue;
        // the cheats' check (0x80): 8 bytes, the second word whatever the stack held (zero here)
        uint32_t check[2] = {get_cheats() ^ 0xDEADFACEu, 0};
        desc.content_ = check;
        desc.ident_ = 0x80;
        get_sequence(desc, 8);
        desc.byte_count_ = 8;
        enqueue(server_app(), player, &desc);
    }
    net->change_score_sync_stamp(server_app()->clock_);
}

} // namespace modules

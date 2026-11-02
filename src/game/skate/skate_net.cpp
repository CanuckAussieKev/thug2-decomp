// The skate module's network handlers and their registration (0x0056C920-0x0056F090). See skate_net.h.

#include "game/skate/skate_net.h"
#include "game/goals/objective.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/player_info.h"
#include "engine/net/thugpro_compat.h"
#include "engine/net/thugpro_peer.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "game/rider/local_net_logic.h"
#include "game/rider/rider.h"
#include "game/rider/skater_state.h"
#include "game/rider/state_history.h"
#include "engine/util/bitstream.h"
#include "game/goals/goal_manager.h"
#include "engine/script/game_cfuncs.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "wire/skate_net_messages.h"
#include "wire/gamenet_messages.h"
#include "wire/play_messages.h"
#include "formats/byte_stream.h"
#include "engine/assets/resource_registry.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/movie_manager.h"
#include "engine/sys/game_config.h"
#include "engine/sys/mlp.h"
#include "platform/compiler.h"
#include "engine/core/ints.h"
#include "platform/code_address.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"


namespace modules {

namespace {

using script::Array;
using script::Struct;
namespace thugpro = online::thugpro;

// Names.
constexpr uint32_t kWidth = 0x73E5BAD0;                  // width
constexpr uint32_t kTitle = 0xD4C98794;                  // title
constexpr uint32_t kNetNoticeMsg = 0x170356DF;           // net_notice_msg
constexpr uint32_t kServerRemovedYouDialog = 0xB836BD2B; // CreateServerRemovedYouDialog
constexpr uint32_t kServerMovedOnDialog = 0x2A874456;    // CreateServerMovedOnDialog
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;           // load_mainmenu
constexpr uint32_t kScavengerAddTime = 0x15B30635;       // goal_scavenger_add_time
constexpr uint32_t kScavenger = 0x1F3430CE;              // scavenger (a goal)
constexpr uint32_t kScavengerInterval = 0x8A6B04AD;      // scavenger_interval
constexpr uint32_t kScavengerCollectPeriod = 0xD4F04E7F; // scavenger_collect_period
constexpr uint32_t kName = 0xA1DC81F9;                   // name
constexpr uint32_t kOneLeftYou = 0x8C7162C9;             // ScavengerHuntOneObjectLeftYou
constexpr uint32_t kOneLeftOther = 0x6343ED6F;           // ScavengerHuntOneObjectLeftOther
constexpr uint32_t kOneLeftOtherTeam = 0x2834F181;       // ScavengerHuntOneObjectLeftOtherTeam
constexpr uint32_t kFiveLeftYou = 0x14D1928C;            // ScavengerHuntFiveObjectsLeftYou
constexpr uint32_t kFiveLeftOther = 0x53F03C93;          // ScavengerHuntFiveObjectsLeftOther
constexpr uint32_t kFiveLeftOtherTeam = 0x81FECA4E;      // ScavengerHuntFiveObjectsLeftOtherTeam
constexpr uint32_t kId = 0x40C698AF;                     // id
constexpr uint32_t kRemoveHuntItem = 0xAB0B62AA;         // ScavengerHunt_KillObject
constexpr uint32_t kIndex = 0x7F8C98FE;                  // index
constexpr uint32_t kObjId = 0x0830ECAF;                  // ObjId
constexpr uint32_t kPos = 0x7F261953;                    // pos
constexpr uint32_t kShowNow = 0x4F285F1E;                // show_now
constexpr uint32_t kDropObject = 0x5A968743;             // DropSHObject
constexpr uint32_t kShouldModulateColor = 0x11941568;    // should_modulate_color
constexpr uint32_t kNetFirefight = 0xBFF33600;           // netfirefight
constexpr uint32_t kAnnounceElimination = 0x9B043179;    // announce_elimination
constexpr uint32_t kNotifyCheating = 0x1CEDB62C;         // notify_cheating
constexpr uint32_t kNetElimiskate = 0x10F84276;          // netelimiskate
constexpr uint32_t kShowLeaderMessages = 0xD63DFD0F;     // show_leader_messages
constexpr uint32_t kString0 = 0xA4B08520;                // String0
constexpr uint32_t kWonGame = 0x96935417;                // WonGame
constexpr uint32_t kLostGame = 0xE3EC7E9E;               // LostGame
constexpr uint32_t kLeaderYou = 0xA1D334D0;              // NewScoreLeaderYou
constexpr uint32_t kLeaderOther = 0x7A4B9D14;            // NewScoreLeaderOther
constexpr uint32_t kLeaderYourTeam = 0xA8D9DCC2;         // NewScoreLeaderYourTeam
constexpr uint32_t kLeaderOtherTeam = 0x6C499C84;        // NewScoreLeaderOtherTeam
constexpr uint32_t kLevel = 0x651533EC;                  // level
constexpr uint32_t kRequestLevel = 0x38028FC9;           // request_level
constexpr uint32_t kLeaveFrontEnd = 0xB89ABE96;          // leave_front_end
constexpr uint32_t kCleanupBeforeLoading = 0x65ED88DA;   // cleanup_before_loading_level
constexpr uint32_t kProSetGeoFlag = 0xC12C8094;          // FLAG_PROSET1_GEO_ON
constexpr uint32_t kFlag = 0x2E0B1465;                   // flag
constexpr uint32_t kSetFlag = 0x1BC80657;                // SetFlag
constexpr uint32_t kNetLobby = 0x1C471C60;               // netlobby
constexpr uint32_t kNetCtf = 0x6C5FF266;                 // netctf
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;          // netgoalattack
constexpr uint32_t kFirefight = 0x3D6D444F;              // firefight
constexpr uint32_t kTargetScoreArray = 0x50E63314;       // targetScoreArray
constexpr uint32_t kHighestScoreArray = 0x1A2C22F9;      // highestScoreArray
constexpr uint32_t kScore = 0xCD66C8AE;                  // score
constexpr uint32_t kVictoryConditions = 0x94A24C5E;      // victory_conditions
constexpr uint32_t kDefaultTimeLimit = 0x8D38A280;       // default_time_limit
constexpr uint32_t kNetGameTypeInfo = 0x2F94C542;        // net_game_type_info
constexpr uint32_t kMpGameTypeInfo = 0x4DA72272;         // mp_game_type_info
constexpr uint32_t kChecksum = 0x21902065;               // checksum
constexpr uint32_t kTime = 0x906B67BA;                   // time
constexpr uint32_t kUnlimitedTime = 0xF0E712D2;          // unlimited_time
constexpr uint32_t kNetScavengerHunt = 0xFC13B531;       // netscavengerhunt
constexpr uint32_t kElimiskateInterval = 0xF194FB10;     // StartGoal_ElimiSkate_Interval
constexpr uint32_t kElimiskateBreak = 0xA420A073;        // StartGoal_ElimiSkate_Break_Period
constexpr uint32_t kScavengerIntervalGoal = 0x5DF0B0E0;  // StartGoal_Scavenger_Interval
constexpr uint32_t kScavengerCollectGoal = 0x991E4ABE;   // StartGoal_Scavenger_Collect

constexpr uint8_t kSymbolChecksum = 0xD; // kQbvalName
constexpr uint8_t kSymbolString = 3;     // kQbvalString

// The singletons (not rewritten): the skate module, the network manager (online::Hub), the game mode
// (goals::MatchRules), the object manager, the goal manager, the profile manager (graffiti colours).
Skate* skate_module() { return g_skate_module.get(); }
online::Hub* net_session_slot() { return online::g_online_hub; }
goals::MatchRules* get_game_mode() { return goals::g_game_mode.get(); }
objects::ObjectHubBase* entity_hub_handle()
{
    return static_cast<objects::ObjectHubBase*>(objects::g_composite_registry.get());
}
goals::GoalManager* objective_board() { return goals::get_objective_board(); }

void get_run(uint32_t name, Struct* params = nullptr, objects::Entity* object = nullptr)
{
    script::launch_script(name, params, object, false, nullptr);
}

using online::PlayerRecord;
PlayerRecord* find_own_player_record() { return net_session_slot()->get_own_player(); }
PlayerRecord* find_player_slot_by_obj(uint32_t id)
{
    return net_session_slot()->find_player_slot_by_obj(static_cast<uint16_t>(id));
}
bool is_own_player(PlayerRecord* player) { return player->is_own_player() != 0; }
// 0x005430D0 on the network manager: its last leading team set - the getter 0x005331A0's field. (The address is
// folded with PlayerRecord::set_ready_query_owed, a setter of the same offset.)
void set_last_leading_team(online::Hub* net, int team) { net->last_leading_team_ = team; }
objects::Entity* player_skater(const PlayerRecord* player) { return player->skater_; }

// A goal by name (the goal manager's 0x00557F90) and two of its virtuals: +0xC is it active, +0x14 its update.
goals::Objective* get_goal(goals::GoalManager* manager, uint32_t name) { return manager->get_goal(name, 1); }
// A goal active (the original's vtable +0x0C) / expired (+0x14).
bool goal_active(goals::Objective* goal) { return goal->is_operative(); }
void expire_goal(goals::Objective* goal) { goal->expire(); }

// lists::Hunt<PlayerRecord> (vtable 0x0064C1A8) for the network manager's begin_player_walk / step_player_walk.
using PlayerSearch = online::PlayerSearch;

// A skater's state history (+0x27C): the newest of 20 states at count % 20.
using StateEntry = objects::SkaterStateHistoryFrame;
objects::RiderFrameLog* history_of(objects::Entity* object)
{
    return core::down_cast<objects::Rider>(object)->state_history_;
}

void append_listener(net::AppHost* app, int id, net::HandlerSlotValue code, uint32_t flags, void* data,
                     int priority = 0x80)
{
    app->dispatcher_.append_listener(static_cast<uint8_t>(id), code, flags, data, priority);
}

} // namespace

// 0x0056E2B0
void SkateNet::create_network_tasks(Skate* skate)
{
    core::at(skate->server_tasks_, 0) = std::make_unique<tasks::Task<Skate>>(&SkateNet::server_task, skate, -1000);
    core::at(skate->server_tasks_, 1) = std::make_unique<tasks::Task<Skate>>(&SkateNet::score_task, skate, -1000);
}

// 0x00569530
void SkateNet::delete_network_tasks(Skate* skate)
{
    for (std::unique_ptr<tasks::Task<Skate>>& task : skate->server_tasks_)
        memory::destroy_then_clear(task); // (the original left the pointers as they were)
}

// 0x0056DEE0
void SkateNet::launch_host(Skate* skate)
{
    frame_loop::Hub* mlp = assets::g_main_loop_manager.get();
    online::Hub* net = net_session_slot();
    bool auto_serving = false;
    if (net->is_network_game() != 0) {
        auto_serving = true;
        net->disconnect_clients();
    }
    auto* server = net->start_host_session(!auto_serving, true);
    if (!server)
        return;
    mlp->logic_tasks_.queue_job(core::at(skate->server_tasks_, 0).get());
    mlp->logic_tasks_.queue_job(core::at(skate->server_tasks_, 1).get());
    append_listener(server, 0x3B, handle_trick_objects, 0, nullptr);
    append_listener(server, 0x52, handle_combo_score, 0, skate);
    append_listener(server, 0x86, handle_game_stats, 0, skate);
    append_listener(server, 0x4C, handle_relay, 2, nullptr);
    append_listener(server, 0x45, handle_joined, 2, nullptr);
    append_listener(server, 0x23, &SkateNet::handle_client_object_update, 3, nullptr);
    append_listener(server, 0x8F, handle_scavenger_dropped, 0, skate);
    append_listener(server, 0x90, handle_scavenger_collected, 0, skate);
    if (server->is_host_local())
        return;
    // The messages a server relays for its clients.
    append_listener(server, 0x7E, handle_face_texture, 0, nullptr);
    append_listener(server, 0x97, handle_graffiti_texture, 0, skate);
    append_listener(server, 0x98, handle_graffiti_texture, 0, skate);
    append_listener(server, 0x9A, handle_graffiti_texture, 0, skate);
    append_listener(server, 0x83, handle_relay, 0, skate);
    for (const int id : {0x99, 0x89})
        append_listener(server, id, handle_relay, 0, nullptr);
    append_listener(server, 0x89, handle_player_value48, 0, nullptr);
    append_listener(server, 0x29, handle_relay, 0, nullptr);
    append_listener(server, 0x2B, handle_relay_nearby, 0, nullptr);
    for (const int id : {0x2A, 0x76, 0x8C, 0x77, 0x78, 0x2C})
        append_listener(server, id, handle_relay, 0, nullptr);
    append_listener(server, 0x3C, handle_relay_nearby, 0, nullptr);
    for (const int id : {0x3D, 0x3E, 0x41})
        append_listener(server, id, handle_relay, 0, nullptr);
    append_listener(server, 0x35, handle_relay_nearby, 0, nullptr);
    for (const int id : {0x36, 0x39, 0x3A, 0x37})
        append_listener(server, id, handle_relay, 0, nullptr);
    append_listener(server, 0x38, handle_relay, 2, nullptr);
    append_listener(server, 0x48, handle_relay_sequenced, 2, nullptr);
    append_listener(server, 0x49, handle_relay_sequenced, 2, nullptr);
}

// 0x0056EB50
void SkateNet::add_client_handlers(Skate* skate, net::AppHost* client, int index)
{
    append_listener(client, 0x33, on_run_start_msg, 0, skate);
    append_listener(client, 0x23, handle_object_updates, 3, skate);
    append_listener(client, 0x29, handle_skater_messages, 0, skate);
    append_listener(client, 0x7F, handle_toggle_cheat, 0, skate);
    for (const int id : {0x2B, 0x76, 0x8C, 0x77, 0x78, 0x2A})
        append_listener(client, id, handle_skater_messages, 0, skate);
    append_listener(client, 0x2C, handle_skater_messages, 0, skate, 0xFF);
    append_listener(client, 0x2C, handle_skater_channel, 0, skate);
    for (const int id : {0x3C, 0x3D, 0x3E, 0x41})
        append_listener(client, id, handle_skater_messages, 0, skate);
    append_listener(client, 0x2D, handle_player_left, 2, skate);
    append_listener(client, 0x32, handle_server_quit, 2, skate);
    append_listener(client, 0x35, handle_terrain_sound, 0, skate);
    append_listener(client, 0x36, handle_terrain_sound, 0, skate);
    append_listener(client, 0x37, on_sparks_msg, 0, skate);
    append_listener(client, 0x38, on_sparks_msg, 0, skate);
    append_listener(client, 0x39, handle_look_up_object, 0, skate);
    append_listener(client, 0x3A, handle_look_up_object, 0, skate);
    append_listener(client, 0x40, handle_scores, 2, skate);
    append_listener(client, 0x46, on_level_switch_msg, 0, skate);
    append_listener(client, 0x48, on_remote_script_msg, 2, skate);
    append_listener(client, 0x49, handle_run_script_now, 2, skate);
    append_listener(client, 0x51, handle_server_removed_you, 2, skate);
    append_listener(client, 0x56, handle_server_removed_you, 2, skate);
    append_listener(client, 0x5B, handle_graffiti_objects, 0, skate);
    append_listener(client, 0x5C, handle_graffiti_colors, 0, skate);
    append_listener(client, 0x7E, handle_face_texture, 0, skate);
    for (const int id : {0x96, 0x97, 0x98, 0x9A})
        append_listener(client, id, handle_graffiti_texture, 0, skate);
    append_listener(client, 0x82, on_cheat_state_msg, 0, skate);
    append_listener(client, 0x83, on_projectile_msg, 0, skate);
    append_listener(client, 0x8F, handle_scavenger_drop_objects, 0, skate);
    append_listener(client, 0x89, handle_history_value, 0, skate);
    append_listener(client, 0x95, handle_scavenger_hunt, 0, skate);
    append_listener(client, 0x93, handle_scavenger_hunt, 0, skate);
    append_listener(client, 0x90, handle_scavenger_kill_object, 0, skate);
    append_listener(client, 0x99, handle_place_tag, 0, skate);
    // The handlers of skate client's own skaters.
    for (uint32_t i = 0; i < 8; ++i) {
        auto* skater =
            core::down_cast<objects::Rider>(skate->get_rider_at(static_cast<int>(i)));
        if (!skater || !skater->is_local_skater_ || skater->get_skater_number() != index)
            continue;
        append_listener(client, 0x3B, &modules::PointTally::on_score_msg, 0, skater);
        append_listener(client, 0x44, &objects::RiderFrameLog::handle_collision_messages, 0, skater);
        append_listener(client, 0x43, &objects::RiderFrameLog::handle_collision_messages, 0, skater);
        append_listener(client, 0x85, &objects::RiderFrameLog::handle_hit_messages, 0, skater);
        append_listener(client, 0x84, &objects::RiderFrameLog::handle_hit_messages, 0, skater);
        append_listener(client, 0x4B, &objects::RiderHostSync::handle_graffiti_steal, 0, skater);
    }
    if (index == 0) {
        for (const int id : {0x91, 0x92, 0x94})
            append_listener(client, id, handle_scavenger_hunt, 0, skate);
    }
    // NOT ORIGINAL - the THUGPRO peer mode's messages, 155-159 (engine/net/thugpro_peer.h)
    if (online::thugpro::peer_mode())
        online::thugpro::add_client_handlers(client, skate);
}

// 0x0056C920
int SkateNet::handle_server_removed_you(net::InboundMsgCtx* context)
{
    const Skate* skate = skate_module();
    online::Hub* net = net_session_slot();
    script::kill_all_child_scripts();
    script::StructPtr params = script::make_struct();
    params->add_component_of_type(kWidth, script::kQbvalInteger, 300);
    params->add_component_of_type(kTitle, kSymbolChecksum, kNetNoticeMsg);
    // (the dialogs get no parameters: the structure isn't passed)
    if (context->type_code_ == 'Q')
        get_run(kServerRemovedYouDialog);
    else if (context->type_code_ == 'V')
        get_run(kServerMovedOnDialog);
    params.reset();
    if (skate->pending_level_ == kLoadMainMenu) {
        net->cancel_join();
        net->set_entry_phase(10);
        return 2;
    }
    net->remove_players();
    net->disconnect_clients();
    return 3;
}

// 0x0056CA70
int SkateNet::handle_scavenger_hunt(net::InboundMsgCtx* context)
{
    switch (context->type_code_) {
    case 0x91:
        get_run(kScavengerAddTime);
        return 1;
    case 0x92: {
        goals::Objective* goal = get_goal(objective_board(), kScavenger);
        if (goal_active(goal))
            expire_goal(goal);
        return 1;
    }
    case 0x93: {
        goals::GoalManager* manager = objective_board();
        goals::Objective* interval = get_goal(manager, kScavengerInterval);
        if (goal_active(interval)) {
            expire_goal(interval);
        } else if (goal_active(get_goal(manager, kScavenger))) {
            script::launch_script("ForceStartScavengerCollect", nullptr, nullptr, false);
        }
        return 1;
    }
    case 0x94: {
        goals::Objective* goal = get_goal(objective_board(), kScavengerCollectPeriod);
        if (goal_active(goal))
            expire_goal(goal);
        return 1;
    }
    case 0x95:
        break;
    default:
        return 1;
    }
    // Objects left (wire/play_messages.h): [0] the player (or the team), [1] how many.
    util::ByteReader reader(context->body_, context->body_size_);
    std::array<int8_t, 2> data{};
    core::at(data, 0) = reader.i8();
    core::at(data, 1) = reader.i8();
    if (!reader.ok()) // NOT ORIGINAL: too short - dropped
        return 4;
    online::Hub* net = net_session_slot();
    gsl::owner<Struct*> params = nullptr;
    std::array<char, 128> text{};
    if ((get_game_mode()->is_squad_match() != 0)) { // teams
        int local_team = -1;
        PlayerRecord* local = net->get_own_player();
        if (local && !local->is_observer() && local->has_finished_joining())
            local_team = local->team_;
        params = script::new_struct();
        params->append_word(kName, local->label_.data()); // (no local player: read through null, as the original)
        if (core::at(data, 1) == 1 || core::at(data, 1) == 5) {
            const bool one = core::at(data, 1) == 1;
            if (core::at(data, 0) == local_team) {
                get_run(one ? kOneLeftYou : kFiveLeftYou);
            } else {
                text::format_to(text, "team_%d_name", core::at(data, 0) + 1);
                params->append_word(kName, script::fetch_text(text.data()));
                get_run(one ? kOneLeftOtherTeam : kFiveLeftOtherTeam, params);
            }
        }
    } else {
        PlayerRecord* player = net->find_player_slot_by_obj(static_cast<uint16_t>(static_cast<int16_t>(core::at(data, 0))));
        if (!player)
            return 1;
        params = script::new_struct();
        params->append_word(kName, player->label_.data());
        if (core::at(data, 1) == 1 || core::at(data, 1) == 5) {
            const bool one = core::at(data, 1) == 1;
            if (is_own_player(player))
                get_run(one ? kOneLeftYou : kFiveLeftYou);
            else
                get_run(one ? kOneLeftOther : kFiveLeftOther, params);
        }
    }
    script::free_struct(params);
    return 1;
}

// 0x0056CDF0
int SkateNet::handle_scavenger_kill_object(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    const uint32_t id = reader.u32();
    if (!reader.ok()) // NOT ORIGINAL: too short - dropped
        return 4;
    script::StructPtr params = script::make_struct();
    params->put_name_key(kId, id);
    get_run(kRemoveHuntItem, params.get());
    params.reset();
    return 1;
}

// 0x0056CEC0
int SkateNet::handle_scavenger_drop_objects(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    online::HuntPlacements message;
    if (!online::read_hunt_placements(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    PlayerRecord* player = find_player_slot_by_obj(static_cast<uint32_t>(static_cast<int16_t>(message.player_id)));
    if (!player)
        return 4;
    player->num_placed_ = message.count;
    for (int i = 0; i < player->num_placed_; ++i) {
        std::array<float, 4> at{};
        // NOT ORIGINAL: no positions past the message's end (the original read on)
        if (!online::read_hunt_position(reader, at))
            break;
        const script::VectorValue position = {.x = core::at(at, 0), .y = core::at(at, 1), .z = core::at(at, 2), .w = core::at(at, 3)};
        const script::StructPtr params = script::make_struct();
        params->put_int(kIndex, i);
        params->put_int(kObjId, static_cast<int>(player_skater(player)->ident_));
        params->put_vec3(kPos, position);
        params->put_name_key(kShowNow, kShowNow);
        get_run(kDropObject, params.get(), player_skater(player));
    }
    return 1;
}

// 0x0056D030
int SkateNet::handle_graffiti_colors(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    skate_net::GraffitiColorsMessage message;
    if (!get_game_mode()->is_true(kShouldModulateColor)) {
        debug::printf("No need to continue cause we're not in graffiti mode");
        return 4;
    }
    if (!skate_net::read_graffiti_colors(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    const auto& counts = message.counts;
    objects::TrickSpotHub* trick_objects = objects::g_trick_spot_hub.get();
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    gsl::owner<Struct*> params = script::new_struct();
    for (int i = 0; i < skate_net::kGraffitiSkaters; ++i) {
        if (!core::at(counts, i))
            continue;
        gsl::owner<Array*> array = script::new_array();
        array->configure_size_kind(core::at(counts, i), static_cast<script::QbValueKind>(kSymbolChecksum));
        // (NOT ORIGINAL: past the message's end the indexes read as 0 - the original read on)
        for (int j = 0; std::cmp_less(j, core::at(counts, i)); ++j)
            array->set_element(static_cast<uint32_t>(j), trick_objects->get_cluster_name(reader.u8()));
        std::array<char, 32> name{};
        text::format_to(name, "Skater%d", i);
        params->append_array_value(checksums::hash_from_text(name.data()),
                                   array); // (the array isn't freed: the original's too)
    }
    trick_objects->change_args(params);
    trick_objects->apply_player_graffiti();
    script::free_struct(params);
    memory::Hub::get_pull()->leave_heap_scope();
    return 1;
}

// 0x0056D250
int SkateNet::handle_scores(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    auto message = std::make_unique<skate_net::ScoresMessage>();
    // NOT ORIGINAL: a message too short for its header is dropped; the entries of one cut short are taken as far as
    // they go (the original read on past its end)
    skate_net::read_scores(reader, message.get());
    if (reader.position() < skate_net::kScoresHeaderSize)
        return 4;
    auto* skate = static_cast<Skate*>(context->content_);
    online::Hub* net = net_session_slot();
    if (!net->get_on_server())
        get_game_mode()->set_goal_time_left(static_cast<int>(message->time_left));
    // Each skater's score: the object's id, its score.
    for (int i = 0; i < message->count; ++i) {
        const int8_t id = core::at(message->entries, i).id;
        const int value = core::at(message->entries, i).score;
        objects::Entity* object = entity_hub_handle()->look_up_entity(static_cast<uint32_t>(static_cast<int>(id)));
        if (!object)
            continue;
        auto* skater = core::down_cast<objects::Rider>(object);
        modules::PointTally* score = nullptr;
        if (!get_game_mode()->is_true(kShouldModulateColor) && get_game_mode()->keeps_move_points()) {
            if (skater->is_local_skater_) // (a local skater keeps its own)
                continue;
            score = skater->get_points_total();
        } else {
            score = skater->get_points_total();
            if (get_game_mode()->get_name_key() == kNetFirefight && !skater->is_local_skater_ &&
                score->get_total_score() > 0 && value == 0) {
                // Eliminated.
                if (PlayerRecord* player = find_player_slot_by_obj(object->ident_))
                    player->mark_eliminated();
                script::StructPtr params = script::make_struct();
                params->append_word(kName, skater->fetch_shown_name());
                get_run(kAnnounceElimination, params.get());
                params.reset();
                skate->conceal_skater(skater, 1 != 0);
            }
        }
        score->set_grand_total(value);
    }
    if (message->final)
        net->set_final_scores_received(true);
    if (message->cheats != skate->get_cheats() && !net->cheating_noted()) {
        get_run(kNotifyCheating);
        net->note_cheating();
    }
    const bool final = message->final;
    std::array<char, 128> text{};
    if ((get_game_mode()->is_squad_match() != 0) && get_game_mode()->get_name_key() != kNetElimiskate) {
        // The leading team (none on a tie).
        const int teams = get_game_mode()->current_num_teams();
        int best = -1;
        int best_score = 0;
        for (int team = 0; team < teams; ++team) {
            const int score = net->get_squad_points(team);
            if (score > best_score) {
                best_score = score;
                best = team;
            } else if (score == best_score) {
                best = -1;
            }
        }
        if (best == -1)
            return 1;
        PlayerRecord* local = net->get_own_player();
        if (final) {
            objects::Entity* skater = player_skater(local);
            if (skater)
                skater->post_own_event(local->team_ == best ? kWonGame : kLostGame, nullptr);
            return 1;
        }
        if (net->get_last_leading_team() == best || !get_game_mode()->is_true(kShowLeaderMessages) ||
            net->has_match_ended())
            return 1;
        if (player_skater(local) && local->team_ == best) {
            get_run(kLeaderYourTeam, nullptr, player_skater(local));
        } else {
            text::format_to(text, "team_%d_name", best + 1);
            const script::StructPtr params = script::make_struct();
            params->get_clear();
            params->append_word(kString0, kSymbolString, script::fetch_text(text.data()));
            get_run(kLeaderOtherTeam, params.get(), player_skater(local));
        }
        set_last_leading_team(net, best);
        return 1;
    }
    // The leading player (none on a tie).
    PlayerSearch search;
    int count = 0;
    PlayerRecord* best = nullptr;
    int best_score = 0;
    for (PlayerRecord* player = net->begin_player_walk(&search, false); player;
         player = net->step_player_walk(&search, false)) {
        const int score = net->get_entrant_points(player_skater(player)->ident_);
        if (score > best_score) {
            best_score = score;
            best = player;
        } else if (score == best_score) {
            best = nullptr;
        }
        ++count;
    }
    if (!best)
        return 1;
    PlayerRecord* local = net->get_own_player();
    if (final) {
        // (the loser's event goes to the leader's skater too: the original's)
        if (is_own_player(best))
            player_skater(best)->post_own_event(kWonGame, nullptr);
        else if (player_skater(local))
            player_skater(best)->post_own_event(kLostGame, nullptr);
        return 1;
    }
    if (net->get_last_leader() == best || count <= 1 || !get_game_mode()->is_true(kShowLeaderMessages) ||
        objects::g_simulation_halted)
        return 1;
    if (is_own_player(best)) {
        get_run(kLeaderYou, nullptr, player_skater(best));
    } else {
        const script::StructPtr params = script::make_struct();
        params->get_clear();
        params->append_word(kString0, kSymbolString, best->label_.data());
        get_run(kLeaderOther, params.get(), player_skater(local));
    }
    net->set_last_leader(best);
    return 1;
}

// 0x0056D810
int SkateNet::on_run_start_msg(net::InboundMsgCtx* context)
{
    online::Hub* net = net_session_slot();
    net->set_entry_phase(8);
    net->empty_server_listing();
    // (the message: wire/gamenet_messages.h; NOT ORIGINAL: one too short is read as zeros past its end - the
    // original read on past it)
    util::ByteReader reader(context->body_, context->body_size_);
    online::SessionBrief brief;
    online::read_session_brief(reader, &brief);
    auto* skate = static_cast<Skate*>(context->content_);
    net::Conn* conn = context->sender_conn_; // (the server's connection)
    conn->set_link_speed_class(brief.not_lan ? 0 : 1);
    conn->set_transmit_period(static_cast<uint32_t>((1000 / game_config::g_frame_rate) << 1));
    net->set_game_id(brief.game_id);
    net->change_arena(brief.level);
    net->set_crown_start_spot(static_cast<char>(brief.crown_start_slot));
    net->set_start_info_words(brief.start_points.data());
    script::StructPtr params = script::make_struct();
    params->get_clear();
    params->add_component_of_type(kLevel, kSymbolChecksum, brief.level);
    get_run(kRequestLevel, params.get());
    params.reset();
    get_run(kLeaveFrontEnd);
    get_run(kCleanupBeforeLoading);
    conn->stamp_last_heard(0);
    get_game_mode()->set_rider_ceiling(static_cast<int>(static_cast<int8_t>(brief.max_players)));
    get_game_mode()->apply_match_style(brief.game_type);
    skate->launch_game();
    get_game_mode()->apply_match_style(brief.game_type);
    get_game_mode()->set_num_teams(static_cast<int>(brief.teams));
    get_game_mode()->set_goals_mode(brief.goals);
    get_game_mode()->set_team_graffiti_mode(brief.team_graffiti_mode);
    get_game_mode()->set_round(static_cast<int>(brief.round));
    net->set_preset_option_bits(static_cast<char>(brief.pro_option_bits));
    // The pro set geometry flags.
    for (int i = 0; i < 7; ++i) {
        if (!(static_cast<int>(static_cast<int8_t>(brief.pro_option_bits)) & (1 << i)))
            continue;
        const script::StructPtr flag = script::make_struct();
        flag->put_int(kFlag, script::fetch_int_value(kProSetGeoFlag) + i);
        get_run(kSetFlag, flag.get());
    }
    // The victory conditions.
    script::StructPtr victory = script::make_struct();
    const uint32_t mode = brief.game_type;
    if (mode != kNetLobby) {
        if (brief.time_limit == 0 && mode != kNetCtf && mode != kNetGoalAttack) {
            if (mode != kFirefight && mode != kNetFirefight) {
                gsl::owner<Array*> conditions = script::new_array();
                script::duplicate_array(conditions, script::fetch_list(kTargetScoreArray));
                auto* condition = conditions->struct_at(0);
                condition->add_component_of_type(kScore, script::kQbvalInteger, brief.target_score);
                victory->append_array_value(kVictoryConditions, conditions);
            }
            victory->add_component_of_type(kDefaultTimeLimit, script::kQbvalInteger, 0);
            skate->set_unknown370(static_cast<int>(0));
        } else {
            victory->add_component_of_type(kDefaultTimeLimit, script::kQbvalInteger, brief.time_limit);
            if (brief.game_type == kNetCtf) {
                gsl::owner<Array*> conditions = script::new_array();
                script::duplicate_array(conditions, script::fetch_list(kHighestScoreArray));
                victory->append_array_value(kVictoryConditions, conditions);
            }
            skate->set_unknown370(static_cast<int>(brief.time_limit));
            get_game_mode()->set_goal_time_left(static_cast<int>(brief.time_left));
        }
        get_game_mode()->force_match_settings(victory.get());
    }
    victory.reset();
    // The mode's goal_script.
    Array* types = script::fetch_list(net->in_net_game() ? kNetGameTypeInfo : kMpGameTypeInfo);
    for (int i = 0; i < ints::as_signed(types->extent_); ++i) {
        auto* type = types->struct_at(static_cast<uint32_t>(i));
        uint32_t checksum = 0;
        type->read_hash_value(kChecksum, &checksum, 1);
        if (checksum != brief.game_type)
            continue;
        script::StructPtr goal_params = script::make_struct();
        goal_params->put_int(kTime, static_cast<int>(brief.time_limit));
        goal_params->put_int(kScore, static_cast<int>(brief.target_score));
        goal_params->put_int(kUnlimitedTime, brief.time_limit == 0);
        // (the original keeps the script in its argument's slot: with no goal_script, the skate module's address)
        uint32_t script = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
        const uint8_t variant = brief.variant;
        if (!variant)
            type->read_hash_value("goal_script", &script, 1);
        else if (brief.game_type == kNetElimiskate)
            script = variant == 1 ? kElimiskateInterval : kElimiskateBreak;
        else if (brief.game_type == kNetScavengerHunt)
            script = variant == 1 ? kScavengerIntervalGoal : kScavengerCollectGoal;
        get_run(script, goal_params.get());
        goal_params.reset();
        break;
    }
    if (net->in_net_game() && net->get_entry_mode() == 0 &&
        !commands::cfunc_is_observing(nullptr, nullptr)) { // IsObserving
        net->prepare_local_face();
        net->send_custom_tag_texture();
    }
    return 1;
}

// 0x0056E380
int SkateNet::on_remote_script_msg(net::InboundMsgCtx* context)
{
    // (the message: wire/play_messages.h; NOT ORIGINAL: one too short for its header is dropped, and its
    // parameters are read no further than its end)
    util::ByteReader reader(context->body_, context->body_size_);
    online::RunScript message;
    if (!online::read_run_script(reader, &message))
        return 4;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    objects::Entity* object = entity_hub_handle()->look_up_entity(message.object_id);
    script::StructPtr params = script::make_struct();
    script::decode_from_bytes(params.get(), reader.cursor(), reader.remaining()); // the script reader
    get_run(message.name, params.get(), object);
    params.reset();
    memory::Hub::get_pull()->leave_heap_scope();
    return 1;
}

// 0x0056E470
int SkateNet::handle_object_updates(net::InboundMsgCtx* context)
{
    util::PackedBitBuffer stream;
    stream.change_intake_contents(context->body_, 0x400);
    const uint32_t objects = stream.read_bits(8);
    for (uint32_t i = 0; i < 8; ++i) {
        if (!(objects & (1u << i)))
            continue;
        auto time = static_cast<uint16_t>(stream.read_bits(16));
        const uint32_t fields = stream.read_bits(9);
        std::array<int16_t, 3> pos = {};
        std::array<float, 3> angles = {};
        uint8_t state = 0, flag74 = 0, value_7c = 0, flag75 = 0, flag76 = 0;
        uint32_t value68 = 0, value_6c = 0, value80 = 0;
        for (int k = 0; k < 3; ++k) {
            if (fields & (1u << k))
                core::at(pos, k) = static_cast<int16_t>(stream.read_signed_bits(16));
        }
        for (int k = 0; k < 3; ++k) {
            if (fields & (8u << k))
                core::at(angles, k) = static_cast<float>(static_cast<int16_t>(stream.read_signed_bits(16)) * (1.0 / 4096.0));
        }
        if (fields & 0x40) {
            const uint32_t bits = stream.read_bits(4);
            state = static_cast<uint8_t>(bits & 7);
            flag74 = static_cast<uint8_t>((bits >> 7) & 1); // (always 0: the original's)
            value_7c = static_cast<uint8_t>(stream.read_bits(6));
            flag75 = static_cast<uint8_t>(stream.read_bits(1));
            flag76 = static_cast<uint8_t>(stream.read_bits(1));
        }
        if (fields & 0x80) {
            value68 = stream.read_bits(5);
            value_6c = stream.read_bits(3);
        }
        if (fields & 0x100)
            value80 = stream.read_bits(32);

        objects::Entity* object = entity_hub_handle()->look_up_entity(i);
        if (!object || (context->wire_options_ & 3))
            continue;
        auto* skater = core::down_cast<objects::GameEntity>(object);
        objects::RiderFrameLog* history = history_of(object);
        int previous = 0;
        if (history->state_count_ > 0) {
            previous = (history->state_count_ - 1) % 20;
        } else {
            // The first: what came before is where the skater is now.
            maths::Mat4 matrix{};
            matrix = skater->basis_frame_;
            maths::Vec4 eulers{};
            matrix.get_euler_angles(eulers);
            StateEntry& first = core::at(history->states_, 0);
            for (int k = 0; k < 3; ++k) {
                // (TP-R6: in a big level as sent there; THUGPRO's own left this one in halves / quarters)
                if (k == 1)
                    core::at(first.pos, 1) = thugpro::pack_height(skater->at_point_.y);
                else
                    core::at(first.pos, k) = thugpro::pack_horizontal(skater->at_point_[k]);
                core::at(first.angles, k) = eulers[k];
            }
            previous = 0;
        }
        const StateEntry& prev = core::at(history->states_, previous);
        StateEntry& cur = core::at(history->states_, history->state_count_ % 20);
        for (int k = 0; k < 3; ++k) {
            if (!(fields & (1u << k)))
                core::at(pos, k) = core::at(prev.pos, k);
        }
        for (int k = 0; k < 3; ++k)
            core::at(cur.pos, k) = core::at(pos, k);
        // (TP-R6: whole units in a big level)
        const std::array<float, 3> position = {{thugpro::unpack_horizontal(core::at(pos, 0)), thugpro::unpack_height(core::at(pos, 1)),
                                   thugpro::unpack_horizontal(core::at(pos, 2))}};
        for (int k = 0; k < 3; ++k) {
            if (!(fields & (8u << k)))
                core::at(angles, k) = core::at(prev.angles, k);
        }
        for (int k = 0; k < 3; ++k)
            core::at(cur.angles, k) = core::at(angles, k);
        maths::Mat4 matrix{};
        matrix.set_from_euler_angles(core::at(angles, 0), core::at(angles, 1), core::at(angles, 2));
        cur.matrix = matrix;
        cur.position = {.x = core::at(position, 0), .y = core::at(position, 1), .z = core::at(position, 2), .w = 0.0f};
        if (!(fields & 0x40)) {
            state = static_cast<uint8_t>(prev.state);
            flag74 = prev.flag74;
            value_7c = prev.value_7c_;
            flag75 = prev.flag75;
            flag76 = prev.flag76;
        }
        if (!(fields & 0x80)) {
            value68 = prev.value68;
            value_6c = prev.value_6c_;
        }
        if (!(fields & 0x100))
            value80 = prev.value80;
        cur.value68 = value68;
        cur.value_6c_ = value_6c;
        cur.state = int{static_cast<int8_t>(state)};
        cur.value_7c_ = value_7c;
        cur.flag74 = flag74;
        cur.flag76 = flag76;
        cur.flag75 = flag75;
        cur.value80 = value80;
        uint16_t wraps = prev.wraps;
        if (prev.time > time) // (the time stamp went round)
            ++wraps;
        cur.wraps = wraps;
        cur.time = time;
        ++history->state_count_;
    }
    return 1;
}

namespace {

// A state entry's time stamp as 32 bits (0x005197D0 / 0x005197E0: +0x78 the low half, +0x7A the high).
uint32_t entry_time(const StateEntry& entry) { return static_cast<uint32_t>(entry.wraps) << 16 | entry.time; }

// The server's frame count (its app: the network manager's +4, read through 0x005480C0 - a folded getter).
uint32_t server_frame()
{
    const net::AppHost* server = net_session_slot()->host_;
    return static_cast<uint32_t>(server->frame_count_);
}

int16_t pack_angle(float angle) { return static_cast<int16_t>(static_cast<int>(static_cast<double>(angle) * 4096.0)); }

} // namespace

// 0x005696C0
uint32_t SkateNet::get_relay_flags(void* sent_record, void* state_entry, uint8_t index)
{
    online::SentSkaterUpdates* sent = &static_cast<PlayerRecord*>(sent_record)->sent_;
    auto* entry = static_cast<StateEntry*>(state_entry);
    uint32_t flags = 0;
    // Everything again once 60 of the server's frames have gone by.
    const uint32_t frame = server_frame();
    const uint32_t since = frame > core::at(sent->forced_frame_, index) ? server_frame() - core::at(sent->forced_frame_, index)
                                                              : core::at(sent->forced_frame_, index) - server_frame();
    const bool force = since >= 60;
    if (force)
        core::at(sent->forced_frame_, index) = server_frame();
    const std::array<float, 3> angles = {{core::at(entry->angles, 0), core::at(entry->angles, 1), core::at(entry->angles, 2)}};
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        const int16_t value =
            k == 1 ? thugpro::pack_height(entry->position[1]) : thugpro::pack_horizontal(entry->position[k]);
        if (value != core::at(core::at(sent->pos_, index), k) || force)
            flags |= 1u << k;
    }
    for (int k = 0; k < 3; ++k) {
        if (pack_angle(core::at(angles, k)) != core::at(core::at(sent->angles_, index), k) || force)
            flags |= 8u << k;
    }
    if (static_cast<uint8_t>(entry->state) != core::at(sent->state_, index) || entry->flag74 != core::at(sent->flag74_, index) ||
        entry->value_7c_ != core::at(sent->value_7c_, index) || entry->flag75 != core::at(sent->flag75_, index) ||
        entry->flag76 != core::at(sent->flag76_, index) || force)
        flags |= 0x40;
    if (entry->value68 != core::at(sent->value68_, index) || entry->value_6c_ != core::at(sent->value_6c_, index) || force)
        flags |= 0x80;
    if (entry->value80 != core::at(sent->value80_, index) || force)
        flags |= 0x100;
    return flags;
}

// 0x005698F0
void SkateNet::write_object_update(objects::Entity* object, RelayStream* out)
{
    PlayerRecord* player = find_player_slot_by_obj(object->ident_);
    objects::Entity* skater = player_skater(player);
    auto index = static_cast<uint8_t>(skater->ident_);
    objects::RiderFrameLog* history = history_of(skater);
    StateEntry& entry = core::at(history->states_, (history->state_count_ - 1) % 20);
    online::SentSkaterUpdates* sent = &static_cast<PlayerRecord*>(out->sent_)->sent_;
    util::PackedBitBuffer& stream = out->feed_;
    core::at(sent->time_, index) = entry_time(entry);
    const std::array<float, 3> angles = {{core::at(entry.angles, 0), core::at(entry.angles, 1), core::at(entry.angles, 2)}};
    stream.write_bits(entry.time, 16);
    const uint32_t flags = get_relay_flags(sent, &entry, index);
    stream.write_bits(flags, 9);
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        const int16_t value =
            k == 1 ? thugpro::pack_height(entry.position.y) : thugpro::pack_horizontal(entry.position[k]);
        if (flags & (1u << k))
            stream.write_bits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        core::at(core::at(sent->pos_, index), k) = value;
    }
    for (int k = 0; k < 3; ++k) {
        const int16_t value = pack_angle(core::at(angles, k));
        if (flags & (8u << k))
            stream.write_bits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        core::at(core::at(sent->angles_, index), k) = value;
    }
    // (the bytes go out sign-extended: write_bits doesn't mask them, as the original)
    auto signed_byte = [](uint8_t value) {
        return static_cast<uint32_t>(static_cast<int>(static_cast<int8_t>(value)));
    };
    const uint8_t value_7c = entry.value_7c_;
    const auto state = static_cast<uint8_t>(entry.state);
    const uint8_t flag74 = entry.flag74;
    const uint8_t flag75 = entry.flag75;
    const uint8_t flag76 = entry.flag76;
    if (flags & 0x40) {
        stream.write_bits(signed_byte(flag74 ? static_cast<uint8_t>(state | 0x80) : state), 4);
        stream.write_bits(signed_byte(value_7c), 6);
        stream.write_bits(signed_byte(flag75), 1);
        stream.write_bits(signed_byte(flag76), 1);
        core::at(sent->state_, index) = state;
        core::at(sent->flag74_, index) = flag74;
        core::at(sent->value_7c_, index) = value_7c;
        core::at(sent->flag75_, index) = flag75;
        core::at(sent->flag76_, index) = flag76;
    }
    if (flags & 0x80) {
        core::at(sent->value68_, index) = entry.value68;
        core::at(sent->value_6c_, index) = entry.value_6c_;
        stream.write_bits(entry.value68, 5);
        stream.write_bits(entry.value_6c_, 3);
    }
    if (flags & 0x100) {
        core::at(sent->value80_, index) = entry.value80;
        stream.write_bits(entry.value80, 32);
    }
}

// 0x0056BE60
int SkateNet::handle_client_object_update(net::InboundMsgCtx* context)
{
    util::PackedBitBuffer stream;
    PlayerRecord* player = net_session_slot()->find_player_by_sender(context->sender_conn_);
    objects::Entity* object = player ? player_skater(player) : nullptr;
    if (!object || !(player->conn_->cur_standing_ & 1) || (context->wire_options_ & 3)) {
        // Not taken: the message's length worked out from its fields (a 32-bit time stamp, 9 field bits, then 16
        // bits for each position and angle, 12 for the state, 8 for the 0x80 pair, 32 for the last).
        stream.change_intake_contents(context->body_, 0x400);
        stream.read_signed_bits(32);
        const uint32_t fields = stream.read_bits(9);
        int bits = fields & 1 ? 0x39 : 0x29;
        for (const uint32_t bit : {2u, 4u, 8u, 0x10u, 0x20u}) {
            if (fields & bit)
                bits += 0x10;
        }
        if (fields & 0x40)
            bits += 0xC;
        if (fields & 0x80)
            bits += 8;
        if (fields & 0x100)
            bits += 0x20;
        context->body_size_ = static_cast<uint32_t>((bits + 7) >> 3);
        return 1;
    }
    objects::RiderFrameLog* history = history_of(object);
    StateEntry& cur = core::at(history->states_, history->state_count_ % 20);
    // What isn't sent stays as it was (nothing yet: zero).
    const StateEntry* prev = nullptr;
    std::array<int16_t, 3> pos = {};
    std::array<float, 3> angles = {};
    uint8_t state = 0, flag74 = 0, value_7c = 0, flag75 = 0, flag76 = 0;
    uint32_t value68 = 0, value_6c = 0, value80 = 0;
    if (history->state_count_ > 0) {
        prev = &core::at(history->states_, (history->state_count_ + 19) % 20);
        value68 = prev->value68;
        flag74 = prev->flag74;
        value_6c = prev->value_6c_;
        state = static_cast<uint8_t>(prev->state);
        flag75 = prev->flag75;
        value_7c = prev->value_7c_;
        value80 = prev->value80;
        flag76 = prev->flag76;
        for (int k = 0; k < 3; ++k) {
            core::at(pos, k) = core::at(prev->pos, k);
            core::at(angles, k) = core::at(prev->angles, k);
        }
    }
    stream.change_intake_contents(context->body_, 0x400);
    const uint32_t time = stream.read_bits(32);
    const uint32_t fields = stream.read_bits(9);
    for (int k = 0; k < 3; ++k) {
        if (fields & (1u << k))
            core::at(pos, k) = static_cast<int16_t>(stream.read_signed_bits(16));
    }
    for (int k = 0; k < 3; ++k) {
        if (fields & (8u << k))
            core::at(angles, k) = static_cast<float>(static_cast<int16_t>(stream.read_signed_bits(16)) * (1.0 / 4096.0));
    }
    if (fields & 0x40) {
        const uint32_t bits = stream.read_bits(4);
        state = static_cast<uint8_t>(bits & 7);
        flag74 = static_cast<uint8_t>((bits >> 7) & 1); // (always 0: the original's)
        value_7c = static_cast<uint8_t>(stream.read_bits(6));
        flag75 = static_cast<uint8_t>(stream.read_bits(1));
        flag76 = static_cast<uint8_t>(stream.read_bits(1));
    }
    if (fields & 0x80) {
        value68 = stream.read_bits(5);
        value_6c = stream.read_bits(3);
    }
    if (fields & 0x100)
        value80 = stream.read_bits(32);
    context->body_size_ = static_cast<uint32_t>(stream.get_byte_count());
    if (history->state_count_ > 0 && time < entry_time(*prev))
        return 4; // (an older update: dropped)
    maths::Mat4 matrix{};
    matrix.set_from_euler_angles(core::at(angles, 0), core::at(angles, 1), core::at(angles, 2));
    cur.matrix = matrix;
    // (TP-R6: whole units in a big level)
    cur.position = {.x = thugpro::unpack_horizontal(core::at(pos, 0)), .y = thugpro::unpack_height(core::at(pos, 1)),
                    .z = thugpro::unpack_horizontal(core::at(pos, 2)), .w = 0.0f};
    cur.time = static_cast<uint16_t>(time);
    cur.wraps = static_cast<uint16_t>(time >> 16);
    for (int k = 0; k < 3; ++k) {
        core::at(cur.pos, k) = core::at(pos, k);
        core::at(cur.angles, k) = core::at(angles, k);
    }
    cur.flag74 = flag74;
    cur.value68 = value68;
    cur.value_6c_ = value_6c;
    cur.value_7c_ = value_7c;
    cur.state = int{static_cast<int8_t>(state)};
    cur.value80 = value80;
    cur.flag76 = flag76;
    cur.flag75 = flag75;
    ++history->state_count_;
    return 1;
}

} // namespace modules

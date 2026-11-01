// The skate module's network handlers and their registration (0x0056C920-0x0056F090). See skate_net.h.

#include "game/skate/skate_net.h"
#include "game/goals/objective.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

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
#include "game/rider/state_history.h"
#include "engine/util/bitstream.h"
#include "game/goals/goal_manager.h"
#include "engine/script/game_cfuncs.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "engine/core/code_address.h"
#include "game/skate/skate_module.h"
#include "engine/assets/resource_registry.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/movie_manager.h"
#include "engine/sys/game_config.h"
#include "engine/sys/mlp.h"

#pragma warning(disable : 4996) // (the original's sprintf into its fixed buffers, kept)

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
Skate* skate_module() { return g_skate_module; }
online::Hub* net_session_slot() { return online::g_online_hub; }
goals::MatchRules* get_game_mode() { return goals::g_game_mode; }
objects::ObjectHubBase* entity_hub_handle()
{
    return reinterpret_cast<objects::ObjectHubBase*>(objects::g_composite_registry);
}
uint8_t* objective_board() { return reinterpret_cast<uint8_t*>(goals::get_objective_board()); }

template <typename T>
T& prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

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
Array* new_array_init()
{
    Array* memory = script::alloc_script_array();
    return memory ? memory->prepare_all() : nullptr;
}
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
uint8_t* get_goal(uint8_t* manager, uint32_t name)
{
    return reinterpret_cast<uint8_t*>(reinterpret_cast<goals::GoalManager*>(manager)->get_goal(name, 1));
}
// A goal active (the original's vtable +0x0C) / expired (+0x14).
bool goal_active(uint8_t* goal) { return reinterpret_cast<goals::Objective*>(goal)->is_operative(); }
void expire_goal(uint8_t* goal) { reinterpret_cast<goals::Objective*>(goal)->expire(); }

// lists::Hunt<PlayerRecord> (vtable 0x0064C1A8) for the network manager's begin_player_walk / step_player_walk.
using PlayerSearch = online::PlayerSearch;

// A skater's state history (+0x27C): the newest of 20 states at count % 20.
using StateEntry = objects::SkaterStateHistoryFrame;
objects::RiderFrameLog* history_of(objects::Entity* object)
{
    return static_cast<objects::Rider*>(object)->state_history_;
}

void append_listener(net::AppHost* app, int id, net::HandlerSlotValue code, uint32_t flags, void* data,
                     int priority = 0x80)
{
    app->dispatcher_.append_listener(static_cast<uint8_t>(id), code, flags, data, priority);
}
// (a handler not rewritten yet, by address)
void append_listener(net::AppHost* app, int id, uint32_t code, uint32_t flags, void* data, int priority = 0x80)
{
    append_listener(app, id, reinterpret_cast<net::HandlerSlotValue>(code), flags, data, priority);
}

} // namespace

// 0x0056E2B0
void SkateNet::create_network_tasks()
{
    auto* skate = reinterpret_cast<Skate*>(this);
    skate->server_tasks_[0] = new tasks::Task<Skate>(&SkateNet::server_task, skate, -1000);
    skate->server_tasks_[1] = new tasks::Task<Skate>(&SkateNet::score_task, skate, -1000);
}

// 0x00569530
void SkateNet::delete_network_tasks()
{
    for (tasks::Task<Skate>* task : reinterpret_cast<Skate*>(this)->server_tasks_)
        delete task;
}

// 0x0056DEE0
void SkateNet::launch_host()
{
    frame_loop::Hub* mlp = assets::g_main_loop_manager;
    online::Hub* net = net_session_slot();
    bool auto_serving = false;
    if (net->is_network_game() != 0) {
        auto_serving = true;
        net->disconnect_clients();
    }
    auto* server = net->start_host_session(!auto_serving, true);
    if (!server)
        return;
    auto* skate = reinterpret_cast<uint8_t*>(this);
    mlp->logic_tasks_.queue_job(reinterpret_cast<Skate*>(skate)->server_tasks_[0]);
    mlp->logic_tasks_.queue_job(reinterpret_cast<Skate*>(skate)->server_tasks_[1]);
    append_listener(server, 0x3B, handle_trick_objects, 0, nullptr);
    append_listener(server, 0x52, handle_combo_score, 0, this);
    append_listener(server, 0x86, handle_game_stats, 0, this);
    append_listener(server, 0x4C, handle_relay, 2, nullptr);
    append_listener(server, 0x45, handle_joined, 2, nullptr);
    append_listener(server, 0x23, &SkateNet::handle_client_object_update, 3, nullptr);
    append_listener(server, 0x8F, handle_scavenger_dropped, 0, this);
    append_listener(server, 0x90, handle_scavenger_collected, 0, this);
    if (server->is_host_local())
        return;
    // The messages a server relays for its clients.
    append_listener(server, 0x7E, handle_face_texture, 0, nullptr);
    append_listener(server, 0x97, handle_graffiti_texture, 0, this);
    append_listener(server, 0x98, handle_graffiti_texture, 0, this);
    append_listener(server, 0x9A, handle_graffiti_texture, 0, this);
    append_listener(server, 0x83, handle_relay, 0, this);
    for (int id : {0x99, 0x89})
        append_listener(server, id, handle_relay, 0, nullptr);
    append_listener(server, 0x89, handle_player_value48, 0, nullptr);
    append_listener(server, 0x29, handle_relay, 0, nullptr);
    append_listener(server, 0x2B, handle_relay_nearby, 0, nullptr);
    for (int id : {0x2A, 0x76, 0x8C, 0x77, 0x78, 0x2C})
        append_listener(server, id, handle_relay, 0, nullptr);
    append_listener(server, 0x3C, handle_relay_nearby, 0, nullptr);
    for (int id : {0x3D, 0x3E, 0x41})
        append_listener(server, id, handle_relay, 0, nullptr);
    append_listener(server, 0x35, handle_relay_nearby, 0, nullptr);
    for (int id : {0x36, 0x39, 0x3A, 0x37})
        append_listener(server, id, handle_relay, 0, nullptr);
    append_listener(server, 0x38, handle_relay, 2, nullptr);
    append_listener(server, 0x48, handle_relay_sequenced, 2, nullptr);
    append_listener(server, 0x49, handle_relay_sequenced, 2, nullptr);
}

// 0x0056EB50
void SkateNet::add_client_handlers(net::AppHost* client, int index)
{
    append_listener(client, 0x33, on_run_start_msg, 0, this);
    append_listener(client, 0x23, handle_object_updates, 3, this);
    append_listener(client, 0x29, handle_skater_messages, 0, this);
    append_listener(client, 0x7F, handle_toggle_cheat, 0, this);
    for (int id : {0x2B, 0x76, 0x8C, 0x77, 0x78, 0x2A})
        append_listener(client, id, handle_skater_messages, 0, this);
    append_listener(client, 0x2C, handle_skater_messages, 0, this, 0xFF);
    append_listener(client, 0x2C, handle_skater_channel, 0, this);
    for (int id : {0x3C, 0x3D, 0x3E, 0x41})
        append_listener(client, id, handle_skater_messages, 0, this);
    append_listener(client, 0x2D, handle_player_left, 2, this);
    append_listener(client, 0x32, handle_server_quit, 2, this);
    append_listener(client, 0x35, handle_terrain_sound, 0, this);
    append_listener(client, 0x36, handle_terrain_sound, 0, this);
    append_listener(client, 0x37, on_sparks_msg, 0, this);
    append_listener(client, 0x38, on_sparks_msg, 0, this);
    append_listener(client, 0x39, handle_look_up_object, 0, this);
    append_listener(client, 0x3A, handle_look_up_object, 0, this);
    append_listener(client, 0x40, handle_scores, 2, this);
    append_listener(client, 0x46, on_level_switch_msg, 0, this);
    append_listener(client, 0x48, on_remote_script_msg, 2, this);
    append_listener(client, 0x49, handle_run_script_now, 2, this);
    append_listener(client, 0x51, handle_server_removed_you, 2, this);
    append_listener(client, 0x56, handle_server_removed_you, 2, this);
    append_listener(client, 0x5B, handle_graffiti_objects, 0, this);
    append_listener(client, 0x5C, handle_graffiti_colors, 0, this);
    append_listener(client, 0x7E, handle_face_texture, 0, this);
    for (int id : {0x96, 0x97, 0x98, 0x9A})
        append_listener(client, id, handle_graffiti_texture, 0, this);
    append_listener(client, 0x82, on_cheat_state_msg, 0, this);
    append_listener(client, 0x83, on_projectile_msg, 0, this);
    append_listener(client, 0x8F, handle_scavenger_drop_objects, 0, this);
    append_listener(client, 0x89, handle_history_value, 0, this);
    append_listener(client, 0x95, handle_scavenger_hunt, 0, this);
    append_listener(client, 0x93, handle_scavenger_hunt, 0, this);
    append_listener(client, 0x90, handle_scavenger_kill_object, 0, this);
    append_listener(client, 0x99, handle_place_tag, 0, this);
    // The handlers of this client's own skaters.
    for (uint32_t i = 0; i < 8; ++i) {
        auto* skater =
            static_cast<objects::Rider*>(reinterpret_cast<modules::Skate*>(this)->get_rider_at(static_cast<int>(i)));
        if (!skater || !skater->is_local_skater_ || skater->get_skater_number() != index)
            continue;
        append_listener(client, 0x3B,
                        reinterpret_cast<net::HandlerSlotValue>(code_address(modules::PointTally::on_score_msg)), 0,
                        skater);
        append_listener(client, 0x44, &objects::RiderFrameLog::handle_collision_messages, 0, skater);
        append_listener(client, 0x43, &objects::RiderFrameLog::handle_collision_messages, 0, skater);
        append_listener(client, 0x85, &objects::RiderFrameLog::handle_hit_messages, 0, skater);
        append_listener(client, 0x84, &objects::RiderFrameLog::handle_hit_messages, 0, skater);
        append_listener(client, 0x4B, &objects::RiderHostSync::handle_graffiti_steal, 0, skater);
    }
    if (index == 0) {
        for (int id : {0x91, 0x92, 0x94})
            append_listener(client, id, handle_scavenger_hunt, 0, this);
    }
    // NOT ORIGINAL - the THUGPRO peer mode's messages, 155-159 (engine/net/thugpro_peer.h)
    if (online::thugpro::peer_mode())
        online::thugpro::add_client_handlers(client, this);
}

// 0x0056C920
int SkateNet::handle_server_removed_you(net::InboundMsgCtx* context)
{
    Skate* skate = skate_module();
    online::Hub* net = net_session_slot();
    script::kill_all_child_scripts();
    Struct* params = spawn_param_table();
    params->add_component_of_type(kWidth, script::kQbvalInteger, 300);
    params->add_component_of_type(kTitle, kSymbolChecksum, kNetNoticeMsg);
    // (the dialogs get no parameters: the structure isn't passed)
    if (context->type_code_ == 'Q')
        get_run(kServerRemovedYouDialog);
    else if (context->type_code_ == 'V')
        get_run(kServerMovedOnDialog);
    delete_structure(params);
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
        uint8_t* goal = get_goal(objective_board(), kScavenger);
        if (goal_active(goal))
            expire_goal(goal);
        return 1;
    }
    case 0x93: {
        uint8_t* manager = objective_board();
        uint8_t* interval = get_goal(manager, kScavengerInterval);
        if (goal_active(interval)) {
            expire_goal(interval);
        } else if (goal_active(get_goal(manager, kScavenger))) {
            script::launch_script("ForceStartScavengerCollect", nullptr, nullptr, false);
        }
        return 1;
    }
    case 0x94: {
        uint8_t* goal = get_goal(objective_board(), kScavengerCollectPeriod);
        if (goal_active(goal))
            expire_goal(goal);
        return 1;
    }
    case 0x95:
        break;
    default:
        return 1;
    }
    // Objects left: [0] the player (or the team), [1] how many.
    auto* data = reinterpret_cast<const int8_t*>(context->body_);
    online::Hub* net = net_session_slot();
    Struct* params = nullptr;
    char text[128];
    if ((get_game_mode()->is_squad_match() != 0)) { // teams
        int local_team = -1;
        PlayerRecord* local = net->get_own_player();
        if (local && !local->is_observer() && local->has_finished_joining())
            local_team = local->team_;
        params = spawn_param_table();
        params->append_word(kName, local->label_); // (no local player: read through null, as the original)
        if (data[1] == 1 || data[1] == 5) {
            bool one = data[1] == 1;
            if (data[0] == local_team) {
                get_run(one ? kOneLeftYou : kFiveLeftYou);
            } else {
                std::sprintf(text, "team_%d_name", data[0] + 1);
                params->append_word(kName, script::fetch_text(text));
                get_run(one ? kOneLeftOtherTeam : kFiveLeftOtherTeam, params);
            }
        }
    } else {
        PlayerRecord* player = net->find_player_slot_by_obj(static_cast<uint16_t>(static_cast<int16_t>(data[0])));
        if (!player)
            return 1;
        params = spawn_param_table();
        params->append_word(kName, player->label_);
        if (data[1] == 1 || data[1] == 5) {
            bool one = data[1] == 1;
            if (is_own_player(player))
                get_run(one ? kOneLeftYou : kFiveLeftYou);
            else
                get_run(one ? kOneLeftOther : kFiveLeftOther, params);
        }
    }
    delete_structure(params);
    return 1;
}

// 0x0056CDF0
int SkateNet::handle_scavenger_kill_object(net::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<const uint32_t*>(context->body_);
    Struct* params = spawn_param_table();
    params->put_name_key(kId, data[0]);
    get_run(kRemoveHuntItem, params);
    delete_structure(params);
    return 1;
}

// 0x0056CEC0
int SkateNet::handle_scavenger_drop_objects(net::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<const uint8_t*>(context->body_);
    PlayerRecord* player =
        find_player_slot_by_obj(static_cast<uint32_t>(static_cast<int16_t>(static_cast<int8_t>(data[0]))));
    if (!player)
        return 4;
    player->num_placed_ = *reinterpret_cast<const int*>(data + 4);
    auto* positions = reinterpret_cast<const script::VectorValue*>(data + 8);
    for (int i = 0; i < player->num_placed_; ++i) {
        Struct* params = spawn_param_table();
        params->put_int(kIndex, i);
        params->put_int(kObjId, static_cast<int>(player_skater(player)->ident_));
        params->put_vec3(kPos, positions[i]);
        params->put_name_key(kShowNow, kShowNow);
        get_run(kDropObject, params, player_skater(player));
        delete_structure(params);
    }
    return 1;
}

// 0x0056D030
int SkateNet::handle_graffiti_colors(net::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<const uint8_t*>(context->body_);
    if (!get_game_mode()->is_true(kShouldModulateColor)) {
        debug::printf("No need to continue cause we're not in graffiti mode");
        return 4;
    }
    const uint8_t* colors = data + 8;
    objects::TrickSpotHub* trick_objects = objects::g_trick_spot_hub;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    Struct* params = spawn_param_table();
    for (int i = 0; i < 8; ++i) {
        if (!data[i])
            continue;
        Array* array = new_array_init();
        array->configure_size_kind(data[i], static_cast<script::QbValueKind>(kSymbolChecksum));
        for (int j = 0; j < static_cast<int>(data[i]); ++j)
            array->set_element(static_cast<uint32_t>(j), trick_objects->get_cluster_name(*colors++));
        char name[32];
        std::sprintf(name, "Skater%d", i);
        params->append_array_value(checksums::hash_from_text(name),
                                   array); // (the array isn't freed: the original's too)
    }
    trick_objects->change_args(params);
    trick_objects->apply_player_graffiti();
    delete_structure(params);
    memory::Hub::get_pull()->leave_heap_scope();
    return 1;
}

// 0x0056D250
int SkateNet::handle_scores(net::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<const uint8_t*>(context->body_);
    auto* skate = static_cast<Skate*>(context->content_);
    online::Hub* net = net_session_slot();
    if (!net->get_on_server())
        get_game_mode()->set_goal_time_left(static_cast<int>(*reinterpret_cast<const uint32_t*>(data)));
    // Each skater's score: [9] entries of 5 bytes from [10] - the object's id, its score.
    const uint8_t* entry = data + 10;
    for (int i = 0; i < static_cast<int8_t>(data[9]); ++i) {
        int8_t id = static_cast<int8_t>(entry[0]);
        int value = *reinterpret_cast<const int*>(entry + 1);
        entry += 5;
        objects::Entity* object = entity_hub_handle()->look_up_entity(static_cast<uint32_t>(static_cast<int>(id)));
        if (!object)
            continue;
        auto* skater = static_cast<objects::Rider*>(object);
        modules::PointTally* score;
        if (!get_game_mode()->is_true(kShouldModulateColor) && get_game_mode()->keeps_move_points()) {
            if (skater->is_local_skater_) // (a local skater keeps its own)
                continue;
            score = static_cast<modules::PointTally*>(skater->get_points_total());
        } else {
            score = static_cast<modules::PointTally*>(skater->get_points_total());
            if (get_game_mode()->get_name_key() == kNetFirefight && !skater->is_local_skater_ &&
                score->get_total_score() > 0 && value == 0) {
                // Eliminated.
                if (PlayerRecord* player = find_player_slot_by_obj(object->ident_))
                    player->mark_eliminated();
                Struct* params = spawn_param_table();
                params->append_word(kName, skater->fetch_shown_name());
                get_run(kAnnounceElimination, params);
                delete_structure(params);
                skate->conceal_skater(skater, 1 != 0);
            }
        }
        score->set_grand_total(value);
    }
    if (data[8])
        net->set_final_scores_received(true);
    if (*reinterpret_cast<const int*>(data + 4) != static_cast<int>(skate->get_cheats()) && !net->cheating_noted()) {
        get_run(kNotifyCheating);
        net->note_cheating();
    }
    bool final = data[8] != 0;
    char text[128];
    if ((get_game_mode()->is_squad_match() != 0) && get_game_mode()->get_name_key() != kNetElimiskate) {
        // The leading team (none on a tie).
        int teams = get_game_mode()->current_num_teams();
        int best = -1;
        int best_score = 0;
        for (int team = 0; team < teams; ++team) {
            int score = net->get_squad_points(team);
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
            std::sprintf(text, "team_%d_name", best + 1);
            Struct* params = spawn_param_table();
            params->get_clear();
            params->append_word(kString0, kSymbolString, script::fetch_text(text));
            get_run(kLeaderOtherTeam, params, player_skater(local));
            delete_structure(params);
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
        int score = net->get_entrant_points(player_skater(player)->ident_);
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
        Struct* params = spawn_param_table();
        params->get_clear();
        params->append_word(kString0, kSymbolString, best->label_);
        get_run(kLeaderOther, params, player_skater(local));
        delete_structure(params);
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
    auto* data = reinterpret_cast<const uint8_t*>(context->body_);
    auto* skate = static_cast<Skate*>(context->content_);
    auto u32 = [data](uint32_t offset) { return *reinterpret_cast<const uint32_t*>(data + offset); };
    net::Conn* conn = context->sender_conn_; // (the server's connection)
    conn->set_link_speed_class(data[1] == 0 ? 1 : 0);
    conn->set_transmit_period(static_cast<uint32_t>((1000 / game_config::g_frame_rate) << 1));
    net->set_game_id(data[2]);
    net->change_arena(u32(0x10));
    net->set_crown_start_spot(static_cast<char>(data[0]));
    net->set_start_info_words(reinterpret_cast<const uint32_t*>(data + 0x28));
    Struct* params = spawn_param_table();
    params->get_clear();
    params->add_component_of_type(kLevel, kSymbolChecksum, u32(0x10));
    get_run(kRequestLevel, params);
    delete_structure(params);
    get_run(kLeaveFrontEnd);
    get_run(kCleanupBeforeLoading);
    conn->stamp_last_heard(0);
    get_game_mode()->set_rider_ceiling(static_cast<int>(static_cast<int8_t>(data[4])));
    get_game_mode()->apply_match_style(u32(0xC));
    skate->launch_game();
    get_game_mode()->apply_match_style(u32(0xC));
    get_game_mode()->set_num_teams(static_cast<int>(u32(0x14)));
    get_game_mode()->set_goals_mode(u32(0x18));
    get_game_mode()->set_team_graffiti_mode(u32(0x1C));
    get_game_mode()->set_round(static_cast<int>(u32(0x48)));
    net->set_preset_option_bits(static_cast<char>(data[3]));
    // The pro set geometry flags.
    for (int i = 0; i < 7; ++i) {
        if (!(static_cast<int>(static_cast<int8_t>(data[3])) & (1 << i)))
            continue;
        Struct* flag = spawn_param_table();
        flag->put_int(kFlag, script::fetch_int_value(kProSetGeoFlag) + i);
        get_run(kSetFlag, flag);
        delete_structure(flag);
    }
    // The victory conditions.
    Struct* victory = spawn_param_table();
    uint32_t mode = u32(0xC);
    if (mode != kNetLobby) {
        if (u32(0x20) == 0 && mode != kNetCtf && mode != kNetGoalAttack) {
            if (mode != kFirefight && mode != kNetFirefight) {
                Array* conditions = new_array_init();
                script::duplicate_array(conditions, script::fetch_list(kTargetScoreArray));
                auto* condition = reinterpret_cast<Struct*>(conditions->fetch_part(0));
                condition->add_component_of_type(kScore, script::kQbvalInteger, u32(0x24));
                victory->append_array_value(kVictoryConditions, conditions);
            }
            victory->add_component_of_type(kDefaultTimeLimit, script::kQbvalInteger, 0);
            skate->set_unknown370(static_cast<int>(0));
        } else {
            victory->add_component_of_type(kDefaultTimeLimit, script::kQbvalInteger, u32(0x20));
            if (u32(0xC) == kNetCtf) {
                Array* conditions = new_array_init();
                script::duplicate_array(conditions, script::fetch_list(kHighestScoreArray));
                victory->append_array_value(kVictoryConditions, conditions);
            }
            skate->set_unknown370(static_cast<int>(u32(0x20)));
            get_game_mode()->set_goal_time_left(static_cast<int>(u32(8)));
        }
        get_game_mode()->force_match_settings(victory);
    }
    delete_structure(victory);
    // The mode's goal_script.
    Array* types = script::fetch_list(net->in_net_game() ? kNetGameTypeInfo : kMpGameTypeInfo);
    for (int i = 0; i < static_cast<int>(types->extent_); ++i) {
        auto* type = reinterpret_cast<Struct*>(types->fetch_part(static_cast<uint32_t>(i)));
        uint32_t checksum;
        type->read_hash_value(kChecksum, &checksum, 1);
        if (checksum != u32(0xC))
            continue;
        Struct* goal_params = spawn_param_table();
        goal_params->put_int(kTime, static_cast<int>(u32(0x20)));
        goal_params->put_int(kScore, static_cast<int>(u32(0x24)));
        goal_params->put_int(kUnlimitedTime, u32(0x20) == 0);
        // (the original keeps the script in its argument's slot: with no goal_script, the skate module's address)
        uint32_t script = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(skate));
        uint8_t variant = data[0x4C];
        if (!variant)
            type->read_hash_value("goal_script", &script, 1);
        else if (u32(0xC) == kNetElimiskate)
            script = variant == 1 ? kElimiskateInterval : kElimiskateBreak;
        else if (u32(0xC) == kNetScavengerHunt)
            script = variant == 1 ? kScavengerIntervalGoal : kScavengerCollectGoal;
        get_run(script, goal_params);
        delete_structure(goal_params);
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
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    auto* data = reinterpret_cast<const uint32_t*>(context->body_);
    objects::Entity* object = entity_hub_handle()->look_up_entity(data[1]);
    Struct* params = spawn_param_table();
    script::decode_from_bytes(params, reinterpret_cast<const uint8_t*>(data + 2)); // the script reader
    get_run(data[0], params, object);
    delete_structure(params);
    memory::Hub::get_pull()->leave_heap_scope();
    return 1;
}

// 0x0056E470
int SkateNet::handle_object_updates(net::InboundMsgCtx* context)
{
    util::PackedBitBuffer stream;
    stream.change_intake_contents(context->body_, 0x400);
    uint32_t objects = stream.read_bits(8);
    for (uint32_t i = 0; i < 8; ++i) {
        if (!(objects & (1u << i)))
            continue;
        uint16_t time = static_cast<uint16_t>(stream.read_bits(16));
        uint32_t fields = stream.read_bits(9);
        int16_t pos[3] = {};
        float angles[3] = {};
        uint8_t state = 0, flag74 = 0, value_7c = 0, flag75 = 0, flag76 = 0;
        uint32_t value68 = 0, value_6c = 0, value80 = 0;
        for (int k = 0; k < 3; ++k) {
            if (fields & (1u << k))
                pos[k] = static_cast<int16_t>(stream.read_signed_bits(16));
        }
        for (int k = 0; k < 3; ++k) {
            if (fields & (8u << k))
                angles[k] = static_cast<float>(static_cast<int16_t>(stream.read_signed_bits(16)) * (1.0 / 4096.0));
        }
        if (fields & 0x40) {
            uint32_t bits = stream.read_bits(4);
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
        auto* skater = static_cast<objects::GameEntity*>(object);
        objects::RiderFrameLog* history = history_of(object);
        int previous;
        if (history->state_count_ > 0) {
            previous = (history->state_count_ - 1) % 20;
        } else {
            // The first: what came before is where the skater is now.
            maths::Mat4 matrix;
            matrix = skater->basis_frame_;
            maths::Vec4 eulers;
            matrix.get_euler_angles(eulers);
            StateEntry& first = history->states_[0];
            for (int k = 0; k < 3; ++k) {
                // (TP-R6: in a big level as sent there; THUGPRO's own left this one in halves / quarters)
                if (k == 1)
                    first.pos[1] = thugpro::pack_height(skater->at_point_.y);
                else
                    first.pos[k] = thugpro::pack_horizontal((&skater->at_point_.x)[k]);
                first.angles[k] = (&eulers.x)[k];
            }
            previous = 0;
        }
        StateEntry& prev = history->states_[previous];
        StateEntry& cur = history->states_[history->state_count_ % 20];
        for (int k = 0; k < 3; ++k) {
            if (!(fields & (1u << k)))
                pos[k] = prev.pos[k];
        }
        for (int k = 0; k < 3; ++k)
            cur.pos[k] = pos[k];
        // (TP-R6: whole units in a big level)
        float position[3] = {thugpro::unpack_horizontal(pos[0]), thugpro::unpack_height(pos[1]),
                             thugpro::unpack_horizontal(pos[2])};
        for (int k = 0; k < 3; ++k) {
            if (!(fields & (8u << k)))
                angles[k] = prev.angles[k];
        }
        for (int k = 0; k < 3; ++k)
            cur.angles[k] = angles[k];
        maths::Mat4 matrix;
        matrix.set_from_euler_angles(angles[0], angles[1], angles[2]);
        cur.matrix = matrix;
        cur.position = {position[0], position[1], position[2], 0.0f};
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
        cur.state = static_cast<int8_t>(state);
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
    net::AppHost* server = net_session_slot()->host_;
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
    uint32_t frame = server_frame();
    uint32_t since = frame > sent->forced_frame_[index] ? server_frame() - sent->forced_frame_[index]
                                                        : sent->forced_frame_[index] - server_frame();
    bool force = since >= 60;
    if (force)
        sent->forced_frame_[index] = server_frame();
    float angles[3] = {entry->angles[0], entry->angles[1], entry->angles[2]};
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        int16_t value =
            k == 1 ? thugpro::pack_height((&entry->position.x)[1]) : thugpro::pack_horizontal((&entry->position.x)[k]);
        if (value != sent->pos_[index][k] || force)
            flags |= 1u << k;
    }
    for (int k = 0; k < 3; ++k) {
        if (pack_angle(angles[k]) != sent->angles_[index][k] || force)
            flags |= 8u << k;
    }
    if (static_cast<uint8_t>(entry->state) != sent->state_[index] || entry->flag74 != sent->flag74_[index] ||
        entry->value_7c_ != sent->value_7c_[index] || entry->flag75 != sent->flag75_[index] ||
        entry->flag76 != sent->flag76_[index] || force)
        flags |= 0x40;
    if (entry->value68 != sent->value68_[index] || entry->value_6c_ != sent->value_6c_[index] || force)
        flags |= 0x80;
    if (entry->value80 != sent->value80_[index] || force)
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
    StateEntry& entry = history->states_[(history->state_count_ - 1) % 20];
    online::SentSkaterUpdates* sent = &static_cast<PlayerRecord*>(out->sent_)->sent_;
    util::PackedBitBuffer& stream = out->feed_;
    sent->time_[index] = entry_time(entry);
    float angles[3] = {entry.angles[0], entry.angles[1], entry.angles[2]};
    stream.write_bits(entry.time, 16);
    uint32_t flags = get_relay_flags(sent, &entry, index);
    stream.write_bits(flags, 9);
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        int16_t value =
            k == 1 ? thugpro::pack_height(entry.position.y) : thugpro::pack_horizontal((&entry.position.x)[k]);
        if (flags & (1u << k))
            stream.write_bits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        sent->pos_[index][k] = value;
    }
    for (int k = 0; k < 3; ++k) {
        int16_t value = pack_angle(angles[k]);
        if (flags & (8u << k))
            stream.write_bits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        sent->angles_[index][k] = value;
    }
    // (the bytes go out sign-extended: write_bits doesn't mask them, as the original)
    auto signed_byte = [](uint8_t value) {
        return static_cast<uint32_t>(static_cast<int>(static_cast<int8_t>(value)));
    };
    uint8_t value_7c = entry.value_7c_, state = static_cast<uint8_t>(entry.state), flag74 = entry.flag74;
    uint8_t flag75 = entry.flag75, flag76 = entry.flag76;
    if (flags & 0x40) {
        stream.write_bits(signed_byte(flag74 ? static_cast<uint8_t>(state | 0x80) : state), 4);
        stream.write_bits(signed_byte(value_7c), 6);
        stream.write_bits(signed_byte(flag75), 1);
        stream.write_bits(signed_byte(flag76), 1);
        sent->state_[index] = state;
        sent->flag74_[index] = flag74;
        sent->value_7c_[index] = value_7c;
        sent->flag75_[index] = flag75;
        sent->flag76_[index] = flag76;
    }
    if (flags & 0x80) {
        sent->value68_[index] = entry.value68;
        sent->value_6c_[index] = entry.value_6c_;
        stream.write_bits(entry.value68, 5);
        stream.write_bits(entry.value_6c_, 3);
    }
    if (flags & 0x100) {
        sent->value80_[index] = entry.value80;
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
        uint32_t fields = stream.read_bits(9);
        int bits = fields & 1 ? 0x39 : 0x29;
        for (uint32_t bit : {2u, 4u, 8u, 0x10u, 0x20u}) {
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
    StateEntry& cur = history->states_[history->state_count_ % 20];
    // What isn't sent stays as it was (nothing yet: zero).
    StateEntry* prev = nullptr;
    int16_t pos[3] = {};
    float angles[3] = {};
    uint8_t state = 0, flag74 = 0, value_7c = 0, flag75 = 0, flag76 = 0;
    uint32_t value68 = 0, value_6c = 0, value80 = 0;
    if (history->state_count_ > 0) {
        prev = &history->states_[(history->state_count_ + 19) % 20];
        value68 = prev->value68;
        flag74 = prev->flag74;
        value_6c = prev->value_6c_;
        state = static_cast<uint8_t>(prev->state);
        flag75 = prev->flag75;
        value_7c = prev->value_7c_;
        value80 = prev->value80;
        flag76 = prev->flag76;
        for (int k = 0; k < 3; ++k) {
            pos[k] = prev->pos[k];
            angles[k] = prev->angles[k];
        }
    }
    stream.change_intake_contents(context->body_, 0x400);
    uint32_t time = stream.read_bits(32);
    uint32_t fields = stream.read_bits(9);
    for (int k = 0; k < 3; ++k) {
        if (fields & (1u << k))
            pos[k] = static_cast<int16_t>(stream.read_signed_bits(16));
    }
    for (int k = 0; k < 3; ++k) {
        if (fields & (8u << k))
            angles[k] = static_cast<float>(static_cast<int16_t>(stream.read_signed_bits(16)) * (1.0 / 4096.0));
    }
    if (fields & 0x40) {
        uint32_t bits = stream.read_bits(4);
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
    maths::Mat4 matrix;
    matrix.set_from_euler_angles(angles[0], angles[1], angles[2]);
    cur.matrix = matrix;
    // (TP-R6: whole units in a big level)
    cur.position = {thugpro::unpack_horizontal(pos[0]), thugpro::unpack_height(pos[1]),
                    thugpro::unpack_horizontal(pos[2]), 0.0f};
    cur.time = static_cast<uint16_t>(time);
    cur.wraps = static_cast<uint16_t>(time >> 16);
    for (int k = 0; k < 3; ++k) {
        cur.pos[k] = pos[k];
        cur.angles[k] = angles[k];
    }
    cur.flag74 = flag74;
    cur.value68 = value68;
    cur.value_6c_ = value_6c;
    cur.value_7c_ = value_7c;
    cur.state = static_cast<int8_t>(state);
    cur.value80 = value80;
    cur.flag76 = flag76;
    cur.flag75 = flag75;
    ++history->state_count_;
    return 1;
}

} // namespace modules

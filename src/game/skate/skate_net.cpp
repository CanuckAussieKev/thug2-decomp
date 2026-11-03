// The skate module's network handlers and their registration (0x0056C920-0x0056F090). See skate_net.h.

#include "game/skate/skate_net.h"
#include "game/goals/objective.h"

#include <cstddef>
#include <cstdint>
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
#include "wire/skater_messages.h"
#include "formats/byte_stream.h"
#include "engine/assets/resource_registry.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/movie_manager.h"
#include "engine/sys/game_config.h"
#include "engine/sys/mlp.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/net/net_game_state.h"
#include "wire/message_ids.h"
#include "wire/object_updates.h"

namespace modules {

namespace {
constexpr int kDefaultListenerPriority = 0x80;
constexpr int kServerTaskPriority = 1000;        // the server tasks run at the negative of this
constexpr int kSkaterMessagePriority = 0xFF;
constexpr int kDialogWidth = 300;
constexpr int kFiveLeft = 5;
constexpr std::size_t kTextBytes = 128;
constexpr std::size_t kNameBytes = 32;
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kProSetGeoFlagCount = 7;
// The object updates' state ring and their lengths in bits by what they hold (the layout: wire/object_updates.h).
constexpr int kEntriesBack = 19;                 // one state back in the ring (plus the ring's size, modulo)
constexpr int kWrapShift = 16;
constexpr int kBitsWithTime = 0x39;              // the size of an update in bits, by what it holds
constexpr int kBitsWithoutTime = 0x29;
constexpr int kStateFieldBits = 0xC;
constexpr int kRoundUpToByte = 7;

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


// The singletons (not rewritten): the skate module, the network manager (online::Hub), the game mode
// (goals::MatchRules), the object manager, the goal manager, the profile manager (graffiti colours).


objects::ObjectHubBase* entity_hub_handle()
{
    return static_cast<objects::ObjectHubBase*>(objects::object_hub_slot().get());
}
goals::GoalManager* objective_board() { return goals::get_objective_board(); }

void get_run(uint32_t name, Struct* params = nullptr, objects::Entity* object = nullptr)
{
    script::launch_script(name, params, object, false, nullptr);
}

using online::PlayerRecord;
PlayerRecord* find_own_player_record() { return online::online_hub()->get_own_player(); }
PlayerRecord* find_player_slot_by_obj(uint32_t id)
{
    return online::online_hub()->find_player_slot_by_obj(static_cast<uint16_t>(id));
}
bool is_own_player(PlayerRecord* player) { return player->is_own_player() != 0; }
objects::Entity* player_skater(const PlayerRecord* player) { return player->skater(); }

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
    return core::down_cast<objects::Rider>(object)->state_history();
}

void append_listener(net::AppHost* app, online::MessageId id, net::HandlerSlotValue code, uint32_t flags, void* data,
                     int priority = kDefaultListenerPriority)
{
    app->dispatcher().append_listener(id, code, flags, data, priority);
}

} // namespace

// 0x0056E2B0
void SkateNet::create_network_tasks(Skate* skate)
{
    core::at(skate->server_tasks(), 0) = std::make_unique<tasks::Task<Skate>>(&SkateNet::server_task, skate, -kServerTaskPriority);
    core::at(skate->server_tasks(), 1) = std::make_unique<tasks::Task<Skate>>(&SkateNet::score_task, skate, -kServerTaskPriority);
}

// 0x00569530
void SkateNet::delete_network_tasks(Skate* skate)
{
    for (std::unique_ptr<tasks::Task<Skate>>& task : skate->server_tasks()) {
        memory::destroy_then_clear(task); // (the original left the pointers as they were)
    }
}

// 0x0056DEE0
void SkateNet::launch_host(Skate* skate)
{
    frame_loop::Hub* mlp = assets::get_main_loop_manager();
    online::Hub* net = online::online_hub();
    bool auto_serving = false;
    if (net->is_network_game() != 0) {
        auto_serving = true;
        net->disconnect_clients();
    }
    auto* server = net->start_host_session(!auto_serving, true);
    if (!server) {
        return;
    }
    mlp->logic_tasks().queue_job(core::at(skate->server_tasks(), 0).get());
    mlp->logic_tasks().queue_job(core::at(skate->server_tasks(), 1).get());
    append_listener(server, online::MessageId::kTrickObjects, handle_trick_objects, 0, nullptr);
    append_listener(server, online::MessageId::kLandedScore, handle_combo_score, 0, skate);
    append_listener(server, online::MessageId::kBestCombo, handle_game_stats, 0, skate);
    append_listener(server, online::MessageId::kChat, handle_relay, 2, nullptr);
    append_listener(server, online::MessageId::kJoined, handle_joined, 2, nullptr);
    append_listener(server, online::MessageId::kObjectUpdates, &SkateNet::handle_client_object_update, 3, nullptr);
    append_listener(server, online::MessageId::kHuntPlacements, handle_scavenger_dropped, 0, skate);
    append_listener(server, online::MessageId::kHuntItemPickedUp, handle_scavenger_collected, 0, skate);
    if (server->is_host_local()) {
        return;
    }
    // The messages a server relays for its clients.
    append_listener(server, online::MessageId::kFaceTexture, handle_face_texture, 0, nullptr);
    append_listener(server, online::MessageId::kGraffitiTexture, handle_graffiti_texture, 0, skate);
    append_listener(server, online::MessageId::kGraffitiPalette, handle_graffiti_texture, 0, skate);
    append_listener(server, online::MessageId::kGraffitiParts, handle_graffiti_texture, 0, skate);
    append_listener(server, online::MessageId::kProjectile, handle_relay, 0, skate);
    for (const online::MessageId id : {online::MessageId::kPlaceTag, online::MessageId::kBoardVehicle}) {
        append_listener(server, id, handle_relay, 0, nullptr);
    }
    append_listener(server, online::MessageId::kBoardVehicle, handle_player_value48, 0, nullptr);
    append_listener(server, online::MessageId::kPlayAnim, handle_relay, 0, nullptr);
    append_listener(server, online::MessageId::kWobbleTarget, handle_relay_nearby, 0, nullptr);
    for (const online::MessageId id :
         {online::MessageId::kBoardRotated, online::MessageId::kSpinModel, online::MessageId::kCancelSpins,
          online::MessageId::kCreateSpecialItem, online::MessageId::kDestroySpecialItem, online::MessageId::kFlip}) {
        append_listener(server, id, handle_relay, 0, nullptr);
    }
    append_listener(server, online::MessageId::kWobbleDetails, handle_relay_nearby, 0, nullptr);
    for (const online::MessageId id :
         {online::MessageId::kLoopType, online::MessageId::kHideGeom, online::MessageId::kAnimSpeed}) {
        append_listener(server, id, handle_relay, 0, nullptr);
    }
    append_listener(server, online::MessageId::kTerrainSound, handle_relay_nearby, 0, nullptr);
    for (const online::MessageId id : {online::MessageId::kTerrainSoundB, online::MessageId::kLookUpObjectA,
                                       online::MessageId::kLookUpObjectB, online::MessageId::kSparksOn}) {
        append_listener(server, id, handle_relay, 0, nullptr);
    }
    append_listener(server, online::MessageId::kSparksOff, handle_relay, 2, nullptr);
    append_listener(server, online::MessageId::kRunScript, handle_relay_sequenced, 2, nullptr);
    append_listener(server, online::MessageId::kSpawnScript, handle_relay_sequenced, 2, nullptr);
}

// 0x0056EB50
void SkateNet::add_client_handlers(Skate* skate, net::AppHost* client, int index)
{
    append_listener(client, online::MessageId::kSessionBrief, on_run_start_msg, 0, skate);
    append_listener(client, online::MessageId::kObjectUpdates, handle_object_updates, 3, skate);
    append_listener(client, online::MessageId::kPlayAnim, handle_skater_messages, 0, skate);
    append_listener(client, online::MessageId::kToggleCheat, handle_toggle_cheat, 0, skate);
    for (const online::MessageId id : {online::MessageId::kWobbleTarget, online::MessageId::kSpinModel,
                                       online::MessageId::kCancelSpins, online::MessageId::kCreateSpecialItem,
                                       online::MessageId::kDestroySpecialItem, online::MessageId::kBoardRotated}) {
        append_listener(client, id, handle_skater_messages, 0, skate);
    }
    append_listener(client, online::MessageId::kFlip, handle_skater_messages, 0, skate, kSkaterMessagePriority);
    append_listener(client, online::MessageId::kFlip, handle_skater_channel, 0, skate);
    for (const online::MessageId id : {online::MessageId::kWobbleDetails, online::MessageId::kLoopType,
                                       online::MessageId::kHideGeom, online::MessageId::kAnimSpeed}) {
        append_listener(client, id, handle_skater_messages, 0, skate);
    }
    append_listener(client, online::MessageId::kPlayerGone, handle_player_left, 2, skate);
    append_listener(client, online::MessageId::kServerQuit, handle_server_quit, 2, skate);
    append_listener(client, online::MessageId::kTerrainSound, handle_terrain_sound, 0, skate);
    append_listener(client, online::MessageId::kTerrainSoundB, handle_terrain_sound, 0, skate);
    append_listener(client, online::MessageId::kSparksOn, on_sparks_msg, 0, skate);
    append_listener(client, online::MessageId::kSparksOff, on_sparks_msg, 0, skate);
    append_listener(client, online::MessageId::kLookUpObjectA, handle_look_up_object, 0, skate);
    append_listener(client, online::MessageId::kLookUpObjectB, handle_look_up_object, 0, skate);
    append_listener(client, skate_net::MessageId::kScores, handle_scores, 2, skate);
    append_listener(client, online::MessageId::kLevelSwitch, on_level_switch_msg, 0, skate);
    append_listener(client, online::MessageId::kRunScript, on_remote_script_msg, 2, skate);
    append_listener(client, online::MessageId::kSpawnScript, handle_run_script_now, 2, skate);
    append_listener(client, online::MessageId::kRemovedYou, handle_server_removed_you, 2, skate);
    append_listener(client, online::MessageId::kMovedOn, handle_server_removed_you, 2, skate);
    append_listener(client, online::MessageId::kObservedTrickObjects, handle_graffiti_objects, 0, skate);
    append_listener(client, online::MessageId::kGraffitiColors, handle_graffiti_colors, 0, skate);
    append_listener(client, online::MessageId::kFaceTexture, handle_face_texture, 0, skate);
    for (const online::MessageId id : {online::MessageId::kGraffitiPlayer, online::MessageId::kGraffitiTexture,
                                       online::MessageId::kGraffitiPalette, online::MessageId::kGraffitiParts}) {
        append_listener(client, id, handle_graffiti_texture, 0, skate);
    }
    append_listener(client, online::MessageId::kCheats, on_cheat_state_msg, 0, skate);
    append_listener(client, online::MessageId::kProjectile, on_projectile_msg, 0, skate);
    append_listener(client, online::MessageId::kHuntPlacements, handle_scavenger_drop_objects, 0, skate);
    append_listener(client, online::MessageId::kBoardVehicle, handle_history_value, 0, skate);
    append_listener(client, online::MessageId::kHuntItemsLeft, handle_scavenger_hunt, 0, skate);
    append_listener(client, online::MessageId::kHuntStarted, handle_scavenger_hunt, 0, skate);
    append_listener(client, online::MessageId::kHuntItemPickedUp, handle_scavenger_kill_object, 0, skate);
    append_listener(client, online::MessageId::kPlaceTag, handle_place_tag, 0, skate);
    // The handlers of skate client's own skaters.
    for (int i = 0; i < online::kPlayerSlotCount; ++i) {
        auto* skater =
            core::down_cast<objects::Rider>(skate->get_rider_at(i));
        if (!skater || !skater->is_local_skater() || skater->get_skater_number() != index) {
            continue;
        }
        append_listener(client, online::MessageId::kTrickObjects, &modules::PointTally::on_score_msg, 0, skater);
        append_listener(client, online::MessageId::kRanIntoAck, &objects::RiderFrameLog::handle_collision_messages, 0, skater);
        append_listener(client, online::MessageId::kRanInto, &objects::RiderFrameLog::handle_collision_messages, 0, skater);
        append_listener(client, online::MessageId::kHitOwner, &objects::RiderFrameLog::handle_hit_messages, 0, skater);
        append_listener(client, online::MessageId::kHit, &objects::RiderFrameLog::handle_hit_messages, 0, skater);
        append_listener(client, online::MessageId::kTagStolen, &objects::RiderHostSync::handle_graffiti_steal, 0, skater);
    }
    if (index == 0) {
        for (const online::MessageId id :
             {online::MessageId::kHuntNeedToPlace, online::MessageId::kHuntEnd, online::MessageId::kHuntCollectEnd}) {
            append_listener(client, id, handle_scavenger_hunt, 0, skate);
        }
    }
    // NOT ORIGINAL - the THUGPRO peer mode's messages, 155-159 (engine/net/thugpro_peer.h)
    if (online::thugpro::peer_mode()) {
        online::thugpro::add_client_handlers(client, skate);
    }
}

// 0x0056C920
int SkateNet::handle_server_removed_you(net::InboundMsgCtx* context)
{
    const Skate* skate = Skate::sole();
    online::Hub* net = online::online_hub();
    script::kill_all_child_scripts();
    script::StructPtr params = script::make_struct();
    params->add_component_of_type(kWidth, script::QbValueKind::kInteger, kDialogWidth);
    params->add_component_of_type(kTitle, script::QbValueKind::kName, kNetNoticeMsg);
    // (the dialogs get no parameters: the structure isn't passed)
    if (context->type_code() == online::MessageId::kRemovedYou) {
        get_run(kServerRemovedYouDialog);
    } else if (context->type_code() == online::MessageId::kMovedOn) {
        get_run(kServerMovedOnDialog);
    }
    params.reset();
    if (skate->pending_level() == kLoadMainMenu) {
        net->cancel_join();
        net->set_entry_phase(online::EntryPhase::kCancelled); // back to the main menu
        return 2;
    }
    net->remove_players();
    net->disconnect_clients();
    return 3;
}

// 0x0056CA70
int SkateNet::handle_scavenger_hunt(net::InboundMsgCtx* context)
{
    switch (context->type_code()) {
    case online::MessageId::kHuntNeedToPlace:
        get_run(kScavengerAddTime);
        return 1;
    case online::MessageId::kHuntEnd: {
        goals::Objective* goal = get_goal(objective_board(), kScavenger);
        if (goal_active(goal)) {
            expire_goal(goal);
        }
        return 1;
    }
    case online::MessageId::kHuntStarted: {
        goals::GoalManager* manager = objective_board();
        goals::Objective* interval = get_goal(manager, kScavengerInterval);
        if (goal_active(interval)) {
            expire_goal(interval);
        } else if (goal_active(get_goal(manager, kScavenger))) {
            script::launch_script("ForceStartScavengerCollect", nullptr, nullptr, false);
        }
        return 1;
    }
    case online::MessageId::kHuntCollectEnd: {
        goals::Objective* goal = get_goal(objective_board(), kScavengerCollectPeriod);
        if (goal_active(goal)) {
            expire_goal(goal);
        }
        return 1;
    }
    case online::MessageId::kHuntItemsLeft:
        break;
    default:
        return 1;
    }
    // Objects left (wire/play_messages.h): [0] the player (or the team), [1] how many.
    util::ByteReader reader(context->body(), context->body_size());
    std::array<int8_t, 2> data{};
    core::at(data, 0) = reader.i8();
    core::at(data, 1) = reader.i8();
    if (!reader.ok()) { // NOT ORIGINAL: too short - dropped
        return 4;
    }
    online::Hub* net = online::online_hub();
    gsl::owner<Struct*> params = nullptr;
    std::array<char, kTextBytes> text{};
    if ((goals::game_mode()->is_squad_match() != 0)) { // teams
        int local_team = -1;
        PlayerRecord* local = net->get_own_player();
        if (local && !local->is_observer() && local->has_finished_joining()) {
            local_team = local->team();
        }
        params = script::new_struct();
        params->append_word(kName, local->label().data()); // (no local player: read through null, as the original)
        if (core::at(data, 1) == 1 || core::at(data, 1) == kFiveLeft) {
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
        if (!player) {
            return 1;
        }
        params = script::new_struct();
        params->append_word(kName, player->label().data());
        if (core::at(data, 1) == 1 || core::at(data, 1) == kFiveLeft) {
            const bool one = core::at(data, 1) == 1;
            if (is_own_player(player)) {
                get_run(one ? kOneLeftYou : kFiveLeftYou);
            } else {
                get_run(one ? kOneLeftOther : kFiveLeftOther, params);
            }
        }
    }
    script::free_struct(params);
    return 1;
}

// 0x0056CDF0
int SkateNet::handle_scavenger_kill_object(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body(), context->body_size());
    const uint32_t id = reader.u32();
    if (!reader.ok()) { // NOT ORIGINAL: too short - dropped
        return 4;
    }
    script::StructPtr params = script::make_struct();
    params->put_name_key(checksums::Name{kId}, id);
    get_run(kRemoveHuntItem, params.get());
    params.reset();
    return 1;
}

// 0x0056CEC0
int SkateNet::handle_scavenger_drop_objects(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body(), context->body_size());
    online::HuntPlacements message;
    if (!online::read_hunt_placements(reader, &message)) { // NOT ORIGINAL: too short - dropped
        return 4;
    }
    PlayerRecord* player = find_player_slot_by_obj(static_cast<uint32_t>(static_cast<int16_t>(message.player_id)));
    if (!player) {
        return 4;
    }
    player->num_placed() = message.count;
    for (int i = 0; i < player->num_placed(); ++i) {
        std::array<float, 4> at{};
        // NOT ORIGINAL: no positions past the message's end (the original read on)
        if (!online::read_hunt_position(reader, at)) {
            break;
        }
        const script::VectorValue position = {.x = core::at(at, 0), .y = core::at(at, 1), .z = core::at(at, 2), .w = core::at(at, 3)};
        const script::StructPtr params = script::make_struct();
        params->put_int(checksums::Name{kIndex}, i);
        params->put_int(checksums::Name{kObjId}, static_cast<int>(player_skater(player)->ident()));
        params->put_vec3(checksums::Name{kPos}, position);
        params->put_name_key(checksums::Name{kShowNow}, kShowNow);
        get_run(kDropObject, params.get(), player_skater(player));
    }
    return 1;
}

// 0x0056D030
int SkateNet::handle_graffiti_colors(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body(), context->body_size());
    skate_net::GraffitiColorsMessage message;
    if (!goals::game_mode()->is_true(kShouldModulateColor)) {
        debug::printf("No need to continue cause we're not in graffiti mode");
        return 4;
    }
    if (!skate_net::read_graffiti_colors(reader, &message)) { // NOT ORIGINAL: too short - dropped
        return 4;
    }
    const auto& counts = message.counts;
    objects::TrickSpotHub* trick_objects = objects::trick_spot_hub();
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap());
    gsl::owner<Struct*> params = script::new_struct();
    for (int i = 0; i < skate_net::kGraffitiSkaters; ++i) {
        if (!core::at(counts, i)) {
            continue;
        }
        gsl::owner<Array*> array = script::new_array();
        array->configure_size_kind(core::at(counts, i), script::QbValueKind::kName);
        // (NOT ORIGINAL: past the message's end the indexes read as 0 - the original read on)
        for (int j = 0; std::cmp_less(j, core::at(counts, i)); ++j) {
            array->set_element(static_cast<uint32_t>(j), trick_objects->get_cluster_name(reader.u8()));
        }
        std::array<char, kNameBytes> name{};
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
    util::ByteReader reader(context->body(), context->body_size());
    auto message = std::make_unique<skate_net::ScoresMessage>();
    // NOT ORIGINAL: a message too short for its header is dropped; the entries of one cut short are taken as far as
    // they go (the original read on past its end)
    skate_net::read_scores(reader, message.get());
    if (reader.position() < skate_net::kScoresHeaderSize) {
        return 4;
    }
    auto* skate = static_cast<Skate*>(context->content());
    online::Hub* net = online::online_hub();
    if (!net->get_on_server()) {
        goals::game_mode()->set_goal_time_left(static_cast<int>(message->time_left));
    }
    // Each skater's score: the object's id, its score.
    for (int i = 0; i < message->count; ++i) {
        const int8_t id = core::at(message->entries, i).id;
        const int value = core::at(message->entries, i).score;
        objects::Entity* object = objects::ObjectHubBase::look_up_entity(static_cast<uint32_t>(static_cast<int>(id)));
        if (!object) {
            continue;
        }
        auto* skater = core::down_cast<objects::Rider>(object);
        modules::PointTally* score = nullptr;
        if (!goals::game_mode()->is_true(kShouldModulateColor) && goals::game_mode()->keeps_move_points()) {
            if (skater->is_local_skater()) { // (a local skater keeps its own)
                continue;
            }
            score = skater->get_points_total();
        } else {
            score = skater->get_points_total();
            if (goals::game_mode()->get_name_key() == kNetFirefight && !skater->is_local_skater() &&
                score->get_total_score() > 0 && value == 0) {
                // Eliminated.
                if (PlayerRecord* player = find_player_slot_by_obj(object->ident())) {
                    player->mark_eliminated();
                }
                script::StructPtr params = script::make_struct();
                params->append_word(kName, skater->fetch_shown_name());
                get_run(kAnnounceElimination, params.get());
                params.reset();
                modules::Skate::conceal_skater(skater, 1 != 0);
            }
        }
        score->set_grand_total(value);
    }
    if (message->final) {
        net->set_final_scores_received(true);
    }
    if (message->cheats != skate->get_cheats() && !net->cheating_noted()) {
        get_run(kNotifyCheating);
        net->note_cheating();
    }
    const bool final = message->final;
    std::array<char, kTextBytes> text{};
    if ((goals::game_mode()->is_squad_match() != 0) && goals::game_mode()->get_name_key() != kNetElimiskate) {
        // The leading team (none on a tie).
        const int teams = goals::game_mode()->current_num_teams();
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
        if (best == -1) {
            return 1;
        }
        PlayerRecord* local = net->get_own_player();
        if (final) {
            const objects::Entity* skater = player_skater(local);
            if (skater) {
                skater->post_own_event(local->team() == best ? kWonGame : kLostGame, nullptr);
            }
            return 1;
        }
        if (net->get_last_leading_team() == best || !goals::game_mode()->is_true(kShowLeaderMessages) ||
            net->has_match_ended()) {
            return 1;
        }
        if (player_skater(local) && local->team() == best) {
            get_run(kLeaderYourTeam, nullptr, player_skater(local));
        } else {
            text::format_to(text, "team_%d_name", best + 1);
            const script::StructPtr params = script::make_struct();
            params->get_clear();
            params->append_word(kString0, script::QbValueKind::kString, script::fetch_text(text.data()));
            get_run(kLeaderOtherTeam, params.get(), player_skater(local));
        }
        net->set_last_leading_team(best);
        return 1;
    }
    // The leading player (none on a tie).
    PlayerSearch search;
    int count = 0;
    PlayerRecord* best = nullptr;
    int best_score = 0;
    for (PlayerRecord* player = net->begin_player_walk(&search, false); player;
         player = net->step_player_walk(&search, false)) {
        const int score = net->get_entrant_points(player_skater(player)->ident());
        if (score > best_score) {
            best_score = score;
            best = player;
        } else if (score == best_score) {
            best = nullptr;
        }
        ++count;
    }
    if (!best) {
        return 1;
    }
    PlayerRecord* local = net->get_own_player();
    if (final) {
        // (the loser's event goes to the leader's skater too: the original's)
        if (is_own_player(best)) {
            player_skater(best)->post_own_event(kWonGame, nullptr);
        } else if (player_skater(local)) {
            player_skater(best)->post_own_event(kLostGame, nullptr);
        }
        return 1;
    }
    if (net->get_last_leader() == best || count <= 1 || !goals::game_mode()->is_true(kShowLeaderMessages) ||
        objects::simulation_halted()) {
        return 1;
    }
    if (is_own_player(best)) {
        get_run(kLeaderYou, nullptr, player_skater(best));
    } else {
        const script::StructPtr params = script::make_struct();
        params->get_clear();
        params->append_word(kString0, script::QbValueKind::kString, best->label().data());
        get_run(kLeaderOther, params.get(), player_skater(local));
    }
    net->set_last_leader(best);
    return 1;
}

// 0x0056D810
int SkateNet::on_run_start_msg(net::InboundMsgCtx* context)
{
    online::Hub* net = online::online_hub();
    net->set_entry_phase(online::EntryPhase::kOver); // the session brief received
    net->empty_server_listing();
    // (the message: wire/gamenet_messages.h; NOT ORIGINAL: one too short is read as zeros past its end - the
    // original read on past it)
    util::ByteReader reader(context->body(), context->body_size());
    online::SessionBrief brief;
    online::read_session_brief(reader, &brief);
    auto* skate = static_cast<Skate*>(context->content());
    net::Conn* conn = context->sender_conn(); // (the server's connection)
    conn->set_link_speed_class(brief.not_lan ? 0 : 1);
    conn->set_transmit_period(static_cast<uint32_t>((kMillisecondsPerSecond / game_config::frame_rate()) << 1));
    net->set_game_id(brief.game_id);
    net->change_arena(brief.level);
    net->set_crown_start_spot(static_cast<char>(brief.crown_start_slot));
    net->set_start_info_words(brief.start_points.data());
    script::StructPtr params = script::make_struct();
    params->get_clear();
    params->add_component_of_type(kLevel, script::QbValueKind::kName, brief.level);
    get_run(kRequestLevel, params.get());
    params.reset();
    get_run(kLeaveFrontEnd);
    get_run(kCleanupBeforeLoading);
    conn->stamp_last_heard(0);
    goals::game_mode()->set_rider_ceiling(static_cast<int>(static_cast<int8_t>(brief.max_players)));
    goals::game_mode()->apply_match_style(brief.game_type);
    skate->launch_game();
    goals::game_mode()->apply_match_style(brief.game_type);
    goals::game_mode()->set_num_teams(static_cast<int>(brief.teams));
    goals::game_mode()->set_goals_mode(brief.goals);
    goals::game_mode()->set_team_graffiti_mode(brief.team_graffiti_mode);
    goals::game_mode()->set_round(static_cast<int>(brief.round));
    net->set_preset_option_bits(static_cast<char>(brief.pro_option_bits));
    // The pro set geometry flags.
    for (int i = 0; i < kProSetGeoFlagCount; ++i) {
        if (!(static_cast<int>(static_cast<int8_t>(brief.pro_option_bits)) & (1 << i))) {
            continue;
        }
        const script::StructPtr flag = script::make_struct();
        flag->put_int(checksums::Name{kFlag}, script::fetch_int_value(kProSetGeoFlag) + i);
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
                condition->add_component_of_type(kScore, script::QbValueKind::kInteger, brief.target_score);
                victory->append_array_value(kVictoryConditions, conditions);
            }
            victory->add_component_of_type(kDefaultTimeLimit, script::QbValueKind::kInteger, 0);
            skate->set_unknown370(0);
        } else {
            victory->add_component_of_type(kDefaultTimeLimit, script::QbValueKind::kInteger, brief.time_limit);
            if (brief.game_type == kNetCtf) {
                gsl::owner<Array*> conditions = script::new_array();
                script::duplicate_array(conditions, script::fetch_list(kHighestScoreArray));
                victory->append_array_value(kVictoryConditions, conditions);
            }
            skate->set_unknown370(static_cast<int>(brief.time_limit));
            goals::game_mode()->set_goal_time_left(static_cast<int>(brief.time_left));
        }
        goals::game_mode()->force_match_settings(victory.get());
    }
    victory.reset();
    // The mode's goal_script.
    Array* types = script::fetch_list(net->in_net_game() ? kNetGameTypeInfo : kMpGameTypeInfo);
    for (int i = 0; i < ints::as_signed(types->extent()); ++i) {
        auto* type = types->struct_at(static_cast<uint32_t>(i));
        uint32_t checksum = 0;
        type->read_hash_value(kChecksum, &checksum, 1);
        if (checksum != brief.game_type) {
            continue;
        }
        script::StructPtr goal_params = script::make_struct();
        goal_params->put_int(checksums::Name{kTime}, static_cast<int>(brief.time_limit));
        goal_params->put_int(checksums::Name{kScore}, static_cast<int>(brief.target_score));
        goal_params->put_int(checksums::Name{kUnlimitedTime}, brief.time_limit == 0);
        // (the original keeps the script in its argument's slot: with no goal_script, the skate module's address)
        uint32_t script = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
        const uint8_t variant = brief.variant;
        if (!variant) {
            type->read_hash_value("goal_script", &script, 1);
        } else if (brief.game_type == kNetElimiskate) {
            script = variant == 1 ? kElimiskateInterval : kElimiskateBreak;
        } else if (brief.game_type == kNetScavengerHunt) {
            script = variant == 1 ? kScavengerIntervalGoal : kScavengerCollectGoal;
        }
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
    util::ByteReader reader(context->body(), context->body_size());
    online::RunScript message;
    if (!online::read_run_script(reader, &message)) {
        return 4;
    }
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap());
    objects::Entity* object = objects::ObjectHubBase::look_up_entity(message.object_id);
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
    stream.change_intake_contents(context->body(), online::kObjectUpdateBytes);
    const uint32_t objects = stream.read_bits(8);
    for (int i = 0; i < online::kPlayerSlotCount; ++i) {
        if (!(objects & (1U << i))) {
            continue;
        }
        auto time = static_cast<uint16_t>(stream.read_bits(online::kObjectUpdateTimeBits));
        const uint32_t fields = stream.read_bits(online::kObjectUpdateFieldBits);
        std::array<int16_t, 3> pos = {};
        std::array<float, 3> angles = {};
        uint8_t state = 0;
        uint8_t flag74 = 0;
        uint8_t value_7c = 0;
        uint8_t flag75 = 0;
        uint8_t flag76 = 0;
        uint32_t value68 = 0;
        uint32_t value_6c = 0;
        uint32_t value80 = 0;
        for (int k = 0; k < 3; ++k) {
            if (fields & (1U << k)) {
                core::at(pos, k) = static_cast<int16_t>(stream.read_signed_bits(online::kObjectUpdatePoseBits));
            }
        }
        for (int k = 0; k < 3; ++k) {
            if (fields & (online::kObjectUpdateAngleFieldBase << k)) {
                core::at(angles, k) = static_cast<float>(static_cast<int16_t>(stream.read_signed_bits(online::kObjectUpdatePoseBits)) * (1.0 / online::kObjectUpdateAngleScale));
            }
        }
        if (fields & online::kObjectUpdateHasState) {
            const uint32_t bits = stream.read_bits(4);
            state = static_cast<uint8_t>(bits & online::kObjectUpdateStateMask);
            flag74 = static_cast<uint8_t>((bits >> online::kObjectUpdateFlag74Shift) & 1); // (always 0: the original's)
            value_7c = static_cast<uint8_t>(stream.read_bits(online::kObjectUpdateValue7cBits));
            flag75 = static_cast<uint8_t>(stream.read_bits(1));
            flag76 = static_cast<uint8_t>(stream.read_bits(1));
        }
        if (fields & online::kObjectUpdateHasControls) {
            value68 = stream.read_bits(online::kObjectUpdateControlsBits);
            value_6c = stream.read_bits(3);
        }
        if (fields & online::kObjectUpdateHasWord) {
            value80 = stream.read_bits(online::kObjectUpdateWordBits);
        }

        objects::Entity* object = objects::ObjectHubBase::look_up_entity(static_cast<uint32_t>(i));
        if (!object || (context->wire_options() & 3)) {
            continue;
        }
        auto* skater = core::down_cast<objects::GameEntity>(object);
        objects::RiderFrameLog* history = history_of(object);
        int previous = 0;
        if (history->state_count() > 0) {
            previous = (history->state_count() - 1) % objects::RiderFrameLog::kEntries;
        } else {
            // The first: what came before is where the skater is now.
            maths::Mat4 matrix{};
            matrix = skater->basis_frame();
            maths::Vec4 eulers{};
            matrix.get_euler_angles(eulers);
            StateEntry& first = core::at(history->states(), 0);
            for (int k = 0; k < 3; ++k) {
                // (TP-R6: in a big level as sent there; THUGPRO's own left this one in halves / quarters)
                if (k == 1) {
                    core::at(first.pos, 1) = thugpro::pack_height(skater->at_point().y);
                } else {
                    core::at(first.pos, k) = thugpro::pack_horizontal(skater->at_point()[k]);
                }
                core::at(first.angles, k) = eulers[k];
            }
            previous = 0;
        }
        const StateEntry& prev = core::at(history->states(), previous);
        StateEntry& cur = core::at(history->states(), history->state_count() % objects::RiderFrameLog::kEntries);
        for (int k = 0; k < 3; ++k) {
            if (!(fields & (1U << k))) {
                core::at(pos, k) = core::at(prev.pos, k);
            }
        }
        for (int k = 0; k < 3; ++k) {
            core::at(cur.pos, k) = core::at(pos, k);
        }
        // (TP-R6: whole units in a big level)
        const std::array<float, 3> position = {{thugpro::unpack_horizontal(core::at(pos, 0)), thugpro::unpack_height(core::at(pos, 1)),
                                   thugpro::unpack_horizontal(core::at(pos, 2))}};
        for (int k = 0; k < 3; ++k) {
            if (!(fields & (online::kObjectUpdateAngleFieldBase << k))) {
                core::at(angles, k) = core::at(prev.angles, k);
            }
        }
        for (int k = 0; k < 3; ++k) {
            core::at(cur.angles, k) = core::at(angles, k);
        }
        maths::Mat4 matrix{};
        matrix.set_from_euler_angles(core::at(angles, 0), core::at(angles, 1), core::at(angles, 2));
        cur.matrix = matrix;
        cur.position = {.x = core::at(position, 0), .y = core::at(position, 1), .z = core::at(position, 2), .w = 0.0F};
        if (!(fields & online::kObjectUpdateHasState)) {
            state = static_cast<uint8_t>(prev.state);
            flag74 = prev.flag74;
            value_7c = prev.value_7c_;
            flag75 = prev.flag75;
            flag76 = prev.flag76;
        }
        if (!(fields & online::kObjectUpdateHasControls)) {
            value68 = prev.value68;
            value_6c = prev.value_6c_;
        }
        if (!(fields & online::kObjectUpdateHasWord)) {
            value80 = prev.value80;
        }
        cur.value68 = value68;
        cur.value_6c_ = value_6c;
        cur.state = int{static_cast<int8_t>(state)};
        cur.value_7c_ = value_7c;
        cur.flag74 = flag74;
        cur.flag76 = flag76;
        cur.flag75 = flag75;
        cur.value80 = value80;
        uint16_t wraps = prev.wraps;
        if (prev.time > time) { // (the time stamp went round)
            ++wraps;
        }
        cur.wraps = wraps;
        cur.time = time;
        ++history->state_count();
    }
    return 1;
}

namespace {

// A state entry's time stamp as 32 bits (0x005197D0 / 0x005197E0: +0x78 the low half, +0x7A the high).
uint32_t entry_time(const StateEntry& entry) { return static_cast<uint32_t>(entry.wraps) << kWrapShift | entry.time; }

// The server's frame count (its app: the network manager's +4, read through 0x005480C0 - a folded getter).
uint32_t server_frame()
{
    const net::AppHost* server = online::online_hub()->host();
    return static_cast<uint32_t>(server->frame_count());
}

int16_t pack_angle(float angle) { return static_cast<int16_t>(static_cast<int>(static_cast<double>(angle) * online::kObjectUpdateAngleScale)); }

} // namespace

// 0x005696C0
uint32_t SkateNet::get_relay_flags(RelayEntries relay_entries, uint8_t index)
{
    auto* const sent_record = relay_entries.sent_record_;
    auto* const state_entry = relay_entries.state_entry_;
    online::SentSkaterUpdates* sent = &static_cast<PlayerRecord*>(sent_record)->sent();
    auto* entry = static_cast<StateEntry*>(state_entry);
    uint32_t flags = 0;
    // Everything again once 60 of the server's frames have gone by.
    const uint32_t frame = server_frame();
    const uint32_t since = frame > core::at(sent->forced_frame_, index) ? server_frame() - core::at(sent->forced_frame_, index)
                                                              : core::at(sent->forced_frame_, index) - server_frame();
    const bool force = since >= 60;
    if (force) {
        core::at(sent->forced_frame_, index) = server_frame();
    }
    const std::array<float, 3> angles = {{core::at(entry->angles, 0), core::at(entry->angles, 1), core::at(entry->angles, 2)}};
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        const int16_t value =
            k == 1 ? thugpro::pack_height(entry->position[1]) : thugpro::pack_horizontal(entry->position[k]);
        if (value != core::at(core::at(sent->pos_, index), k) || force) {
            flags |= 1U << k;
        }
    }
    for (int k = 0; k < 3; ++k) {
        if (pack_angle(core::at(angles, k)) != core::at(core::at(sent->angles_, index), k) || force) {
            flags |= online::kObjectUpdateAngleFieldBase << k;
        }
    }
    if (static_cast<uint8_t>(entry->state) != core::at(sent->state_, index) ||
        entry->flag74 != core::at(sent->flag74_, index) || entry->value_7c_ != core::at(sent->value_7c_, index) ||
        entry->flag75 != core::at(sent->flag75_, index) || entry->flag76 != core::at(sent->flag76_, index) || force) {
        flags |= online::kObjectUpdateHasState;
    }
    if (entry->value68 != core::at(sent->value68_, index) || entry->value_6c_ != core::at(sent->value_6c_, index) ||
        force) {
        flags |= online::kObjectUpdateHasControls;
    }
    if (entry->value80 != core::at(sent->value80_, index) || force) {
        flags |= online::kObjectUpdateHasWord;
    }
    return flags;
}

// 0x005698F0
void SkateNet::write_object_update(objects::Entity* object, RelayStream* out)
{
    PlayerRecord* player = find_player_slot_by_obj(object->ident());
    objects::Entity* skater = player_skater(player);
    auto index = static_cast<uint8_t>(skater->ident());
    objects::RiderFrameLog* history = history_of(skater);
    StateEntry& entry = core::at(history->states(), (history->state_count() - 1) % objects::RiderFrameLog::kEntries);
    online::SentSkaterUpdates* sent = &static_cast<PlayerRecord*>(out->sent_)->sent();
    util::PackedBitBuffer& stream = out->feed_;
    core::at(sent->time_, index) = entry_time(entry);
    const std::array<float, 3> angles = {{core::at(entry.angles, 0), core::at(entry.angles, 1), core::at(entry.angles, 2)}};
    stream.write_bits({.value = entry.time, .count = online::kObjectUpdateTimeBits});
    const uint32_t flags = get_relay_flags({.sent_record_ = sent, .state_entry_ = &entry}, index);
    stream.write_bits({.value = flags, .count = online::kObjectUpdateFieldBits});
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        const int16_t value =
            k == 1 ? thugpro::pack_height(entry.position.y) : thugpro::pack_horizontal(entry.position[k]);
        if (flags & (1U << k)) {
            stream.write_bits({.value = static_cast<uint32_t>(static_cast<int>(value)), .count = online::kObjectUpdatePoseBits});
        }
        core::at(core::at(sent->pos_, index), k) = value;
    }
    for (int k = 0; k < 3; ++k) {
        const int16_t value = pack_angle(core::at(angles, k));
        if (flags & (online::kObjectUpdateAngleFieldBase << k)) {
            stream.write_bits({.value = static_cast<uint32_t>(static_cast<int>(value)), .count = online::kObjectUpdatePoseBits});
        }
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
    if (flags & online::kObjectUpdateHasState) {
        stream.write_bits({.value = signed_byte(flag74 ? static_cast<uint8_t>(state | online::kObjectUpdateFlag74Mask) : state), .count = 4});
        stream.write_bits({.value = signed_byte(value_7c), .count = online::kObjectUpdateValue7cBits});
        stream.write_bits({.value = signed_byte(flag75), .count = 1});
        stream.write_bits({.value = signed_byte(flag76), .count = 1});
        core::at(sent->state_, index) = state;
        core::at(sent->flag74_, index) = flag74;
        core::at(sent->value_7c_, index) = value_7c;
        core::at(sent->flag75_, index) = flag75;
        core::at(sent->flag76_, index) = flag76;
    }
    if (flags & online::kObjectUpdateHasControls) {
        core::at(sent->value68_, index) = entry.value68;
        core::at(sent->value_6c_, index) = entry.value_6c_;
        stream.write_bits({.value = entry.value68, .count = online::kObjectUpdateControlsBits});
        stream.write_bits({.value = entry.value_6c_, .count = 3});
    }
    if (flags & online::kObjectUpdateHasWord) {
        core::at(sent->value80_, index) = entry.value80;
        stream.write_bits({.value = entry.value80, .count = online::kObjectUpdateWordBits});
    }
}

// 0x0056BE60
int SkateNet::handle_client_object_update(net::InboundMsgCtx* context)
{
    util::PackedBitBuffer stream;
    PlayerRecord* player = online::online_hub()->find_player_by_sender(context->sender_conn());
    objects::Entity* object = player ? player_skater(player) : nullptr;
    if (!object || !(player->conn()->cur_standing() & 1) || (context->wire_options() & 3)) {
        // Not taken: the message's length worked out from its fields (a 32-bit time stamp, 9 field bits, then 16
        // bits for each position and angle, 12 for the state, 8 for the 0x80 pair, 32 for the last).
        stream.change_intake_contents(context->body(), online::kObjectUpdateBytes);
        stream.read_signed_bits(online::kObjectUpdateClockBits);
        const uint32_t fields = stream.read_bits(online::kObjectUpdateFieldBits);
        int bits = fields & 1 ? kBitsWithTime : kBitsWithoutTime;
        for (const uint32_t bit : {2U, 4U, 8U, 0x10U, 0x20U}) {
            if (fields & bit) {
                bits += online::kObjectUpdatePoseBits;
            }
        }
        if (fields & online::kObjectUpdateHasState) {
            bits += kStateFieldBits;
        }
        if (fields & online::kObjectUpdateHasControls) {
            bits += online::kObjectUpdateControlsFieldBits;
        }
        if (fields & online::kObjectUpdateHasWord) {
            bits += online::kObjectUpdateWordBits;
        }
        context->set_body_size(static_cast<uint32_t>((bits + kRoundUpToByte) >> 3));
        return 1;
    }
    objects::RiderFrameLog* history = history_of(object);
    StateEntry& cur = core::at(history->states(), history->state_count() % objects::RiderFrameLog::kEntries);
    // What isn't sent stays as it was (nothing yet: zero).
    const StateEntry* prev = nullptr;
    std::array<int16_t, 3> pos = {};
    std::array<float, 3> angles = {};
    uint8_t state = 0;
    uint8_t flag74 = 0;
    uint8_t value_7c = 0;
    uint8_t flag75 = 0;
    uint8_t flag76 = 0;
    uint32_t value68 = 0;
    uint32_t value_6c = 0;
    uint32_t value80 = 0;
    if (history->state_count() > 0) {
        prev = &core::at(history->states(), (history->state_count() + kEntriesBack) % objects::RiderFrameLog::kEntries);
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
    stream.change_intake_contents(context->body(), online::kObjectUpdateBytes);
    const uint32_t time = stream.read_bits(32);
    const uint32_t fields = stream.read_bits(online::kObjectUpdateFieldBits);
    for (int k = 0; k < 3; ++k) {
        if (fields & (1U << k)) {
            core::at(pos, k) = static_cast<int16_t>(stream.read_signed_bits(online::kObjectUpdatePoseBits));
        }
    }
    for (int k = 0; k < 3; ++k) {
        if (fields & (online::kObjectUpdateAngleFieldBase << k)) {
            core::at(angles, k) = static_cast<float>(static_cast<int16_t>(stream.read_signed_bits(online::kObjectUpdatePoseBits)) * (1.0 / online::kObjectUpdateAngleScale));
        }
    }
    if (fields & online::kObjectUpdateHasState) {
        const uint32_t bits = stream.read_bits(4);
        state = static_cast<uint8_t>(bits & online::kObjectUpdateStateMask);
        flag74 = static_cast<uint8_t>((bits >> online::kObjectUpdateFlag74Shift) & 1); // (always 0: the original's)
        value_7c = static_cast<uint8_t>(stream.read_bits(online::kObjectUpdateValue7cBits));
        flag75 = static_cast<uint8_t>(stream.read_bits(1));
        flag76 = static_cast<uint8_t>(stream.read_bits(1));
    }
    if (fields & online::kObjectUpdateHasControls) {
        value68 = stream.read_bits(online::kObjectUpdateControlsBits);
        value_6c = stream.read_bits(3);
    }
    if (fields & online::kObjectUpdateHasWord) {
        value80 = stream.read_bits(online::kObjectUpdateWordBits);
    }
    context->set_body_size(static_cast<uint32_t>(stream.get_byte_count()));
    if (history->state_count() > 0 && time < entry_time(*prev)) {
        return 4; // (an older update: dropped)
    }
    maths::Mat4 matrix{};
    matrix.set_from_euler_angles(core::at(angles, 0), core::at(angles, 1), core::at(angles, 2));
    cur.matrix = matrix;
    // (TP-R6: whole units in a big level)
    cur.position = {.x = thugpro::unpack_horizontal(core::at(pos, 0)),
                    .y = thugpro::unpack_height(core::at(pos, 1)),
                    .z = thugpro::unpack_horizontal(core::at(pos, 2)),
                    .w = 0.0F};
    cur.time = static_cast<uint16_t>(time);
    cur.wraps = static_cast<uint16_t>(time >> kWrapShift);
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
    ++history->state_count();
    return 1;
}

} // namespace modules

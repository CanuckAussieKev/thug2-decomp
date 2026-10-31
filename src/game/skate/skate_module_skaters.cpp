// The skate module's skaters (see skate_module.h): the list walked by index, added / removed / re-identified, sent to
// their restart points, cleaned up and reset with the level, the game launched, their collisions, runs and records,
// the per-player settings, the module's script, its destructor.

#include "game/skate/skate_module.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "engine/core/task_list.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "frontend/front_end.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "engine/gfx/tod_manager.h"
#include "engine/pad_input/device.h"
#include "engine/pad_input/input_hub.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/player_info.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/movie_manager.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/proximity.h"
#include "engine/objects/suspend_component.h"
#include "engine/objects/shadow_component.h"
#include "engine/objects/trick_objects.h"
#include "engine/objects/vibration_component.h"
#include "engine/script/preferences.h"
#include "game/skate/highscores.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "game/skate/career.h"
#include "game/skate/contest.h"
#include "game/skate/game_mode.h"
#include "game/skate/letter_match.h"
#include "game/skate/profile_manager.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_net.h"
#include "game/rider/end_run.h"
#include "engine/objects/input_component.h"
#include "engine/objects/node_components.h"
#include "engine/objects/rail_manager.h"
#include "game/rider/rider.h"
#include "game/rider/state_history.h"
#include "game/rider/trick_component.h"
#include "engine/sound/surface_kind.h"
#include "platform/stopwatch.h"
#include "engine/sys/model_viewer.h"
#include "engine/gfx/anim_cache.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/model_appearance.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/viewport.h"
#include "engine/objects/particle_component.h"
#include "engine/gfx/settings.h"

namespace modules {

int g_viewport_count;
script::Struct* g_cat_info;
script::Struct* g_stat_target_info;

namespace {

constexpr uint32_t kBailBoard = 0x884EF81B;                     // BailBoard (+ the skater's id)
constexpr uint32_t kSkaterId = 0x5B24FAAA;                      // SkaterId
constexpr uint32_t kRemoveClientSkater = 0x5B3FDCCB;            // (name unknown: a client's skater left)
constexpr uint32_t kLevel = 0x651533EC;                         // level
constexpr uint32_t kRequestLevel = 0x38028FC9;                  // request_level
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;                  // Load_MainMenu
constexpr uint32_t kPreserveSkaters = 0x7F07D3D4;               // preserve_skaters
constexpr uint32_t kChangeLevelGameFlow = 0xFF135E2A;           // ChangeLevelGameFlow
constexpr uint32_t kShadow = 0x8A897DD2;                        // shadow
constexpr uint32_t kBlobSizeFactor = 0x6F8CD62F;                // (name unknown: the shadow's scale)
constexpr uint32_t kShadowModel = 0x545F8172;                   // (name unknown: the shadow's model)
constexpr uint32_t kRailManagerType = 0x1CDFB2A3;               // railmanager
constexpr uint32_t kTeam = 0x3B1F59E0;                          // Team
constexpr uint32_t kNetLobby = 0x1C471C60;                      // NetLobby
constexpr uint32_t kNetCtf = 0x6C5FF266;                        // netctf
constexpr uint32_t kNetScavengerHunt = 0xFC13B531;              // netscavengerhunt
constexpr uint32_t kScavengerHunt = 0x846504FC;                 // scavengerhunt
constexpr uint32_t kScavenger = 0x1F3430CE;                     // scavenger
constexpr uint32_t kIsHorse = 0xD1993397;                       // is_horse
constexpr uint32_t kHorse = 0x9D65D0E7;                         // Horse
constexpr uint32_t kMultiplayer = 0xBAE0E4DA;                   // Multiplayer
constexpr uint32_t kPlayer1 = 0x41639CE5;                       // Player1
constexpr uint32_t kUninitLightManager = 0x03732450;            // uninit_light_manager
constexpr uint32_t kResetLevel = 0xEDEA4ECE;                    // reset_level
constexpr uint32_t kInitializeGameFlow = 0xBC945B1B;            // InitializeGameFlow
constexpr uint32_t kStandardGameFlow = 0xA4E32C9B;              // StandardGameFlow
constexpr uint32_t kNetFirefight = 0xBFF33600;                  // netfirefight
constexpr uint32_t kFirefight = 0x3D6D444F;                     // firefight
constexpr uint32_t kNetKing = 0x6EF8FDA0;                       // netking
constexpr uint32_t kKing = 0x5D32129C;                          // king
constexpr uint32_t kSkaterBalanceTrick = 0x589E5FB3;            // SkaterBalanceTrick
constexpr uint32_t kInput = 0x27D7CD28;                         // input
constexpr uint32_t kShowPanelStuff = 0xD97CE49F;                // show_panel_stuff
constexpr uint32_t kDestroySkaterParticles = 0xF429E0AC;        // DestroySkaterParticles
constexpr uint32_t kInitSkaterParticles = 0xAE5438AF;           // InitSkaterParticles
constexpr uint32_t kSkaterCam0 = 0x967C138C;                    // skatercam0
constexpr uint32_t kVibration = 0x404EB936;                     // vibration
constexpr uint32_t kPlayer = 0x67E6859A;                        // player
constexpr uint32_t kModelApplyCustomLogos = 0x27670630;         // model_apply_custom_logos
constexpr uint32_t kClientSkaterInit = 0x41FC854A;              // ClientSkaterInit
constexpr uint32_t kTrick = 0x270F56E1;                         // trick
constexpr uint32_t kSkaterEndRun = 0xB1D5A3E1;                  // SkaterEndRun
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;             // SkaterCorePhysics
constexpr uint32_t kFreeskate = 0x34861A16;                     // freeskate
constexpr uint32_t kDefaultSplitscreenPreferences = 0x0076EAE3; // default_splitscreen_preferences
constexpr uint8_t kMsgKing = 0x55;                              // the new king of the hill (its id)
constexpr uint8_t kGroupGame = 8;
// The CTF / scavenger hunt restart points by team.
constexpr uint32_t kTeamRestarts[4] = {0xACD3422F, 0x9AB434CC, 0x5C427A5C, 0x2C3ED607}; // TRG_CTF_Restart_<colour>

goals::MatchRules* get_game_mode() { return goals::g_game_mode; }
online::Hub* net_session_slot() { return online::g_online_hub; }
objects::MiscHub* entity_hub_handle() { return reinterpret_cast<objects::MiscHub*>(objects::g_composite_registry); }
objects::TrickSpotHub* get_trick_objects() { return objects::g_trick_spot_hub; }
script::Struct*& cat_info() { return g_cat_info; }
script::Struct*& stat_goal_params_ref() { return g_stat_target_info; }
// The C runtime's _ftol (0x006236FC): truncated toward zero, the low 32 bits of the 64-bit result.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
memory::Arena* rider_heap_at(int index) { return memory::Hub::get_pull()->skater_heaps_[index]; }

using PlayerSearch = online::PlayerSearch;
online::PlayerRecord* as_player(void* player) { return static_cast<online::PlayerRecord*>(player); }

script::Struct* spawn_param_table()
{
    script::Struct* made = script::spawn_param_bag();
    return made ? made->prepare_all() : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

// The skater list's node (lists::Elem<Rider>, +0x238 of the skater) out of the list.
void unlink_skater_node(memory::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

// --- the list -------------------------------------------------------------------------------------------------------

memory::ListNode* Skate::skater_node(uint32_t index)
{
    return reinterpret_cast<goals::GoalListHead*>(&riders_)->get_node_at(
        static_cast<int>(index)); // (lists::Foremost's fetch_entry)
}

// 0x005665C0
objects::GameEntity* Skate::get_rider_at(int index)
{
    if (static_cast<uint32_t>(count_riders()) <= static_cast<uint32_t>(index))
        return nullptr;
    return static_cast<objects::GameEntity*>(skater_node(static_cast<uint32_t>(index))->data);
}

// The skater at `index` as the module's loops find it (null past the end; the node's data otherwise).
objects::Rider* Skate::get_skater_at(uint32_t index)
{
    if (static_cast<uint32_t>(count_riders()) <= index)
        return nullptr;
    return static_cast<objects::Rider*>(skater_node(index)->data);
}

// 0x00566520
objects::Rider* Skate::get_home_rider()
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(count_riders()); ++i) {
        auto* skater = static_cast<objects::Rider*>(skater_node(i)->data); // (no check: as the original)
        if (skater->is_local_skater_)
            return skater;
    }
    return nullptr;
}

// 0x00566600
objects::Rider* Skate::get_rider_by_number(uint32_t id)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(count_riders()); ++i) {
        auto* skater = static_cast<objects::Rider*>(skater_node(i)->data);
        if (skater->ident_ == id)
            return skater;
    }
    return nullptr;
}

// --- adding and removing --------------------------------------------------------------------------------------------

// 0x00565790
void Skate::drop_skater_from_roster(objects::Rider* skater)
{
    if (objects::Entity* board = entity_hub_handle()->look_up_entity(skater->ident_ + kBailBoard)) {
        board->flag_as_dead();
        entity_hub_handle()->flush_dead_objects();
    }
    if (!skater->is_local_skater_) {
        script::Struct* params = spawn_param_table();
        params->put_name_key(kSkaterId, skater->ident_);
        script::launch_script(kRemoveClientSkater, params, nullptr, false, nullptr);
        delete_structure(params);
    }
    if (skater->is_local_skater_ && skater->view_slot_ == 0) {
        // The local player's created tricks and stat goals kept for the skater that replaces it.
        if (cat_info()) {
            delete_structure(cat_info());
            cat_info() = nullptr;
        }
        debug::printf("CAT: Storing Created Trick data\n"); // "CAT: Storing Created Trick data\n"
        cat_info() = spawn_param_table();
        skater->write_cat_info(cat_info());
        if (stat_goal_params_ref()) {
            delete_structure(stat_goal_params_ref());
            stat_goal_params_ref() = nullptr;
        }
        debug::printf("CAT: Storing Stat Goal data\n"); // "CAT: Storing Stat Goal data\n"
        stat_goal_params_ref() = spawn_param_table();
        skater->write_list_44b_66a_9f(stat_goal_params_ref());
    }
    unlink_skater_node(skater->get_skater_link());
    delete skater;
}

// 0x00567180
objects::Rider* Skate::spawn_skater_entity(objects::RiderIdentity* profile, bool local, uint32_t id, int viewport)
{
    online::Hub* net = net_session_slot();
    if (local) {
        objects::RiderIdentity* source = objects::g_profile_roster->fetch_account(
            static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)) ? viewport : 0);
        reinterpret_cast<gfx::ModelLook*>(profile)->copy_from(reinterpret_cast<gfx::ModelLook*>(source));
        script::launch_script(kShowPanelStuff, nullptr, nullptr, false, nullptr);
        objects::Rider* skater = get_home_rider();
        if (skater) {
            if (static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr))) {
                if (skater->ident_ == id)
                    return skater;
            } else {
                if (skater->ident_ == id)
                    return skater;
                // The local skater given the new id (its place in the list by it, its particles, its score's).
                auto* node = skater->get_skater_link();
                node->priority = static_cast<int>(id);
                unlink_skater_node(node);
                reinterpret_cast<lists::Foremost*>(&riders_)->insert_by_unique_order(node);
                memory::enter_heap_scope(rider_heap_at(skater->get_skater_number()));
                script::launch_script(kDestroySkaterParticles, nullptr, skater, false, nullptr);
                skater->change_serial(id);
                script::launch_script(kInitSkaterParticles, nullptr, skater, false, nullptr);
                memory::leave_heap_scope();
                static_cast<PointTally*>(skater->get_points_total())->set_rider_number(static_cast<int16_t>(id));
                return skater;
            }
        } else if (objects::Entity* camera = entity_hub_handle()->look_up_entity(kSkaterCam0)) {
            camera->object_flags_ &= ~2u;
            delete camera;
        }
    } else if (net->in_net_game()) {
        if (objects::Rider* skater = get_home_rider()) {
            if (auto* vibration = static_cast<objects::PadRumbleModule*>(skater->fetch_module(kVibration)))
                vibration->stop_all();
        }
    }
    net->in_net_game();                 // (its answer unused, as the original)
    memory::push_mem_profile("Skater"); // "Skater"
    int heap = find_free_rider_heap(local);
    memory::enter_heap_scope(rider_heap_at(heap));
    auto* skater =
        new objects::Rider(viewport, reinterpret_cast<objects::EntityHub*>(entity_hub_handle()), local, id, heap, 1);
    skater->init_from_profile(profile); // (no memory: a crash, as the original)
    if (static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr))) {
        script::Struct* params = spawn_param_table();
        params->put_int(kPlayer, 0);
        script::launch_script(kModelApplyCustomLogos, params, nullptr, false, nullptr);
        delete_structure(params);
    }
    if (!skater->is_local_skater_)
        script::launch_script(kClientSkaterInit, nullptr, skater, false, nullptr);
    auto* node = skater->get_skater_link();
    node->priority = static_cast<int>(skater->ident_);
    reinterpret_cast<lists::Foremost*>(&riders_)->insert_by_unique_order(node);
    bind_controllers();
    if (skater->is_local_skater_ && skater->view_slot_ == 0) {
        if (cat_info()) {
            debug::printf("CAT: Restoring Created Tricks\n"); // "CAT: Restoring Created Tricks\n"
            skater->load_cat_info(cat_info());
            delete_structure(cat_info());
            cat_info() = nullptr;
        }
        if (stat_goal_params_ref()) {
            debug::printf("CAT: Restoring Stat Goal data\n"); // "CAT: Restoring Stat Goal data\n"
            skater->load_list_44b_66a_9f(stat_goal_params_ref());
            delete_structure(stat_goal_params_ref());
            stat_goal_params_ref() = nullptr;
        }
    }
    if (skater->ident_ == 1 && static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)))
        static_cast<objects::MoveExecutor*>(skater->fetch_module(kTrick))->apply_trick_bindings(profile);
    memory::pop_mem_profile();
    memory::leave_heap_scope();
    return skater;
}

// 0x00565FF0
int Skate::find_free_rider_heap(bool local)
{
    int index = local ? 0 : 1;
    if (get_game_mode()->get_rider_ceiling() <= 0)
        return index;
    for (int tries = 0;;) {
        bool used = false;
        for (uint32_t i = 0; i < static_cast<uint32_t>(count_riders()); ++i) {
            if (static_cast<objects::Rider*>(skater_node(i)->data)->get_skater_number() == index) {
                used = true;
                break;
            }
        }
        if (!used)
            return index;
        index = static_cast<int>(static_cast<uint32_t>(index + 1) %
                                 static_cast<uint32_t>(get_game_mode()->get_rider_ceiling()));
        if (++tries >= get_game_mode()->get_rider_ceiling())
            return index;
    }
}

// 0x00565BF0
void Skate::set_skater_shadow(objects::Rider* skater)
{
    auto* shadow = static_cast<objects::GroundShadowModule*>(skater->fetch_module(kShadow));
    int type = render::g_hq_shadows ? 1 : 2;
    memory::enter_heap_scope(rider_heap_at(skater->get_skater_number()));
    if (static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)))
        type = 2;
    if (!skater->is_local_skater_)
        type = 2;
    script::Struct* params = spawn_param_table();
    params->push_float(kBlobSizeFactor, 1.0f);
    params->append_word(kShadowModel, "Ped_Shadow"); // "Ped_Shadow"
    shadow->configure_from_data(params);
    delete_structure(params);
    shadow->destroy_shadow();
    shadow->build_blob_shadow(type, "Ped_Shadow");
    memory::leave_heap_scope();
}

// 0x00567090
void Skate::bind_controllers()
{
    for (uint32_t i = 0; i < 8; ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (!skater || !skater->is_local_skater_)
            continue;
        int number = skater->get_skater_number();
        auto* input = pad_input::g_input_manager;
        int controller = input->get_player_controller(number);
        static_cast<objects::PadInputModule*>(skater->fetch_module(kInput))->link_to_pad(controller);
        if (auto* camera = static_cast<objects::GameEntity*>(skater->get_skater_camera())) {
            controller = pad_input::g_input_manager->get_player_controller(number);
            static_cast<objects::PadInputModule*>(camera->fetch_module(kInput))->link_to_pad(controller);
        }
    }
}

// --- the restart points ---------------------------------------------------------------------------------------------

// 0x00566100
uint32_t Skate::warp_to_restart(uint32_t skater_id)
{
    online::Hub* net = net_session_slot();
    auto generic = [] {
        debug::printf(
            "\n\nWARNING - no Player1 restart point, trying Generic\n\n"); // "WARNING - no Player1 restart point, trying Generic"
        uint32_t node = objects::get_spawn_node(0, 0, 0);
        if (!node)
            debug::printf("\n\nWARNING - No restart points found\n\n"); // "WARNING - No restart points found"
        return node;
    };
    auto found = [&](uint32_t node) { return node ? node : generic(); };
    auto multiplayer = [&](uint32_t start) {
        uint32_t node = objects::get_spawn_node(kMultiplayer,
                                                static_cast<int>(net->get_start_info_word(static_cast<int>(start))), 0);
        if (node)
            return node;
        debug::printf(
            "\n\nWARNING - no multiplayer restart point, trying player1\n\n"); // "WARNING - no multiplayer restart point, trying player1"
        return found(objects::get_spawn_node(kPlayer1, 0, 0));
    };
    if (!net->in_net_game() && get_game_mode()->get_starting_rider_count() <= 1)
        return found(objects::get_spawn_node(kPlayer1, static_cast<int>(skater_id), 0));
    if (static_cast<uint8_t>(get_game_mode()->is_squad_match())) {
        if (get_game_mode()->get_name_key() == kNetLobby)
            return found(objects::get_spawn_node(kTeam, 0, 0));
        if (get_game_mode()->get_name_key() == kNetCtf) {
            auto* local = as_player(net->get_own_player());
            if (!local || static_cast<uint32_t>(local->team_) > 3)
                return generic();
            return kTeamRestarts[local->team_];
        }
        if (get_game_mode()->get_name_key() == kNetScavengerHunt) {
            auto* local = as_player(net->get_own_player());
            if (!local)
                return generic();
            goals::Objective* hunt = goals::get_objective_board()->get_goal(kScavenger, 1);
            int team = hunt->is_operative() ? local->team_ : (local->team_ + 1) % get_game_mode()->current_num_teams();
            if (static_cast<uint32_t>(team) > 3)
                return generic();
            return kTeamRestarts[team];
        }
        return multiplayer(skater_id);
    }
    if (get_game_mode()->is_true(kIsHorse))
        return found(objects::get_spawn_node(kHorse, horse_->get_restart(), 0));
    uint32_t start = skater_id;
    if (get_game_mode()->get_name_key() == kNetScavengerHunt || get_game_mode()->get_name_key() == kScavengerHunt) {
        goals::Objective* hunt = goals::get_objective_board()->get_goal(kScavenger, 1);
        if (!hunt->is_operative())
            start = (skater_id + 1) % static_cast<uint32_t>(count_riders());
    }
    return multiplayer(start);
}

// 0x00566370
void Skate::teleport_to_spawn(objects::Rider* skater, uint32_t node, uint32_t walking)
{
    if (!node) {
        node = warp_to_restart(skater->ident_);
        if (!node)
            return;
    }
    skater->jump_to_spawn(node, walking);
}

// 0x00566980
void Skate::reset_skaters(uint32_t node, uint32_t walking)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(count_riders()); ++i) {
        objects::Rider* skater = get_skater_at(i); // (no check: as the original)
        if (skater->is_local_skater_) {
            uint32_t at = node ? node : warp_to_restart(skater->ident_);
            if (at)
                skater->jump_to_spawn(at, walking);
        }
        skater->reset_27c();
    }
    run_over_ = false;
}

} // namespace modules

namespace modules {

// --- the level's session --------------------------------------------------------------------------------------------

// 0x005666A0
void Skate::tidy_up()
{
    debug::printf(
        "Skate::Cleanup() - Deleting all session specific objects\n"); // "Skate::tidy_up() - Deleting all session specific objects\n"
    for (uint32_t i = 0; i < static_cast<uint32_t>(count_riders()); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (!skater)
            continue;
        skater->object_flags_ |= 2; // (kept through the flush below)
        if (skater->is_local_skater_) {
            skater->relaunch(0, 1);
            movies_->clear_movies();
        }
        skater->reset_components();
        skater->conceal_skater();
    }
    movies_->clear_movies();
    object_movies_->stop_all();
    object_movies_->clear_movies();
    if (Viewer* viewer = get_live_viewer())
        viewer->drop_viewer_entity();
    entity_hub_handle()->for_each(&objects::dispose_when_free, nullptr);
    entity_hub_handle()->flush_dead_objects();
    for (uint32_t i = 0; i < static_cast<uint32_t>(count_riders()); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (!skater)
            continue;
        skater->object_flags_ &= ~2u;
        skater->show_skater();
    }
    net_session_slot()->clear_crown();
    script::kill_all_child_scripts();
    get_trick_objects()->remove_all_clusters();
    static_cast<render::FlexibleParticleManager*>(render::g_flexible_particle_manager)->destroy_non_persistent();
    rail_manager_->tidy_up();
    objects::nearby_zone_shutdown();
    objects::emitter_cleanup();
    static_cast<render::EffectsDirector*>(static_cast<void*>(objects::g_particle_hub))->kill_every_particle();
}

// 0x00565A00
void Skate::change_level(uint32_t level)
{
    debug::printf("-----------------------------------------Skate::ChangeLevel(%d)\n",
                  level); // "-----...Skate::ChangeLevel(%d)\n"
    memory::enter_heap_scope(memory::Hub::get_pull()->skater_info_heap_);
    script::Struct* params = spawn_param_table();
    params->get_clear();
    params->add_component_of_type(kLevel, script::kQbvalName, level);
    script::launch_script(kRequestLevel, params, nullptr, false, nullptr);
    delete_structure(params);
    previous_level_ = level_;
    level_ = level;
    params = spawn_param_table();
    params->get_clear();
    if (level != kLoadMainMenu)
        params->add_component_of_type(0, script::kQbvalName, kPreserveSkaters);
    commands::cfunc_cleanup(params, nullptr);
    delete_structure(params);
    stage_switched_ = true;
    game_flow_->get_request(kChangeLevelGameFlow);
    memory::leave_heap_scope();
}

// 0x005668A0
void Skate::restart_level()
{
    run_over_ = false;
    if ((count_riders() || net_session_slot()->in_net_game()) && level_launched_) {
        career_->begin_level(-1);
        stubs::do_nothing(); // (a compiled-out call on the trick object manager)
    }
}

// 0x00566900
void Skate::reset_level()
{
    online::Hub* net = net_session_slot();
    tidy_up();
    commands::cfunc_parse_node_array(nullptr, nullptr);
    run_over_ = false;
    get_trick_objects()->apply_player_graffiti();
    goals::GoalManager* goals = goals::get_objective_board();
    if (net->in_net_game())
        goals->revoke_every_clear();
    else
        goals->prepare_every_objective();
    goals->spawn_objective_props();
    commands::g_tod_manager->clear_dead_objects();
    script::launch_script(kResetLevel, nullptr, nullptr, false, nullptr);
}

// 0x00566A60
void Skate::launch_game()
{
    online::Hub* net = net_session_slot();
    g_front_end_instance->get_pause_game(false);
    memory::enter_heap_scope(memory::bottom_up_heap());
    debug::printf("\nLAUNCH GAME CALLED with %d skaters \n\n",
                  count_riders()); // "\nLAUNCH GAME CALLED with %d skaters \n\n"
    if (!net->get_own_player()) {
        if (static_cast<uint32_t>(get_game_mode()->get_rider_ceiling()) > 1)
            memory::Hub::get_pull()->init_extra_skater_heaps(get_game_mode()->get_rider_ceiling());
        game_flow_->get_request(kInitializeGameFlow);
        game_flow_->tick_frame();
    } else {
        reset_level();
        game_flow_->get_request(kStandardGameFlow);
    }
    PlayerSearch search;
    if (auto* king = as_player(net->get_hill_ruler()))
        king->give_crown(false);
    for (void* player = net->begin_player_walk(&search, true); player; player = net->step_player_walk(&search, true))
        as_player(player)->clear_flags();
    if (get_game_mode()->get_name_key() == kNetLobby)
        net->build_team_flag_set(get_game_mode()->current_num_teams());
    memory::leave_heap_scope();
}

// 0x00566BE0
void Skate::test_rider_contacts()
{
    online::Hub* net = net_session_slot();
    if (net->has_match_ended())
        return;
    if (!net->players_can_collide() && get_game_mode()->get_name_key() != kNetFirefight &&
        get_game_mode()->get_name_key() != kFirefight && get_game_mode()->get_name_key() != kNetKing &&
        get_game_mode()->get_name_key() != kKing && get_game_mode()->get_name_key() != kNetCtf)
        return;
    for (uint32_t i = 0; i < 8; ++i) {
        if (objects::Rider* skater = get_skater_at(i))
            reinterpret_cast<objects::RiderFrameLog*>(skater->unknown27c_)->check_for_collisions(0);
    }
    if (get_game_mode()->get_name_key() != kNetKing && get_game_mode()->get_name_key() != kKing)
        return;
    auto* crown = static_cast<objects::CrownObject*>(net->get_crown_holder());
    if (!crown || crown->is_worn())
        return;
    // The crown to the first skater near the local one.
    objects::Rider* king = nullptr;
    for (uint32_t i = 0; i < 8 && !king; ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (skater && reinterpret_cast<objects::RiderFrameLog*>(skater->unknown27c_)->is_near_local_skater())
            king = skater;
    }
    if (!king)
        return;
    PlayerSearch search;
    as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(king->ident_)))->give_crown(true);
    for (void* player = net->begin_player_walk(&search, true); player; player = net->step_player_walk(&search, true)) {
        if (static_cast<uint8_t>(as_player(player)->is_own_player()))
            continue;
        net::NetMessageInfo desc;
        desc.make_new();
        net::AppHost* server = net->host_; // (0x005480C0)
        uint8_t id = static_cast<uint8_t>(king->ident_);
        desc.content_ = &id;
        desc.byte_count_ = 1;
        desc.ident_ = kMsgKing;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        server->put_on_outbox(as_player(player)->get_link_handle(), &desc);
    }
}

// 0x00566E70
bool __cdecl Skate::cfunc_retry(script::Struct*, script::Script*)
{
    memory::enter_heap_scope(memory::bottom_up_heap());
    sole()->launch_game();
    memory::leave_heap_scope();
    return true;
}

// 0x00566EA0
bool __cdecl Skate::cfunc_launch_game(script::Struct*, script::Script*)
{
    memory::enter_heap_scope(memory::bottom_up_heap());
    sole()->launch_game();
    memory::leave_heap_scope();
    return true;
}

// 0x00566ED0
bool __cdecl Skate::cfunc_reset_level(script::Struct*, script::Script* script)
{
    // (the calling script isn't spawned while the level resets: its flag put back after)
    uint8_t spawned = 0;
    Skate* skate = sole();
    if (script) {
        spawned = script->spawn_flags_ & script::kScriptFlagSpawned;
        script->spawn_flags_ &= ~script::kScriptFlagSpawned;
    }
    memory::enter_heap_scope(memory::bottom_up_heap());
    skate->reset_level();
    memory::leave_heap_scope();
    if (script)
        script->spawn_flags_ = static_cast<uint8_t>((script->spawn_flags_ & ~script::kScriptFlagSpawned) | spawned);
    return true;
}

// 0x00566F40
void Skate::update_records()
{
    new_record_ = false;
    highscores::StageBests* records = highscores::g_game_records->get_level_records(career_->fetch_stage());
    objects::Rider* skater = sole()->get_home_rider();
    if (!skater)
        return;
    auto* score = static_cast<PointTally*>(skater->get_points_total());
    highscores::BestEntryList* high_scores = records->high_scores_;
    new_record_ |= high_scores->try_insert_entry(score->get_total_score(), 0) != -1;
    new_record_ |= records->best_combos_->try_insert_entry(score->top_chain_, 0) != -1;
    auto* balance = reinterpret_cast<uint8_t*>(skater->fetch_module(kSkaterBalanceTrick));
    float grind = *reinterpret_cast<float*>(balance + 0xA8); // (the longest grind, manual and lip trick: seconds)
    float manual = *reinterpret_cast<float*>(balance + 0x50);
    float lip_trick = *reinterpret_cast<float*>(balance + 0x100);
    int tricks = score->best_tricks_;
    new_record_ |= records->grind_record_->try_beat_record(ftol(static_cast<double>(grind) * 100.0), 0);
    new_record_ |= records->longest_manual_->try_beat_record(ftol(static_cast<double>(manual) * 100.0), 0);
    new_record_ |= records->longest_lip_trick_->try_beat_record(ftol(static_cast<double>(lip_trick) * 100.0), 0);
    new_record_ |= records->biggest_chain_->try_beat_record(tricks, 0);
}

// 0x00567580
bool Skate::all_skaters_idle()
{
    bool idle = true;
    for (uint32_t i = 0; i < static_cast<uint32_t>(count_riders()); ++i) {
        objects::Rider* skater = get_skater_at(i); // (null past the end, as the original - never reached)
        if (get_game_mode()->is_true(kIsHorse) && static_cast<uint32_t>(horse_->skater_) != skater->ident_)
            continue; // (in HORSE only the skater taking the run)
        auto* end_run = static_cast<objects::RiderSessionEnd*>(skater->fetch_module(kSkaterEndRun));
        if (end_run->run_done())
            continue;
        idle = false;
        if (skater->is_local_skater_)
            end_run->end_run(false);
    }
    return idle;
}

// 0x00567690
bool Skate::first_trick_started()
{
    objects::Rider* skater = get_rider_by_number(static_cast<uint32_t>(horse_->skater_));
    auto* trick = reinterpret_cast<uint8_t*>(skater->fetch_module(kTrick));
    if (trick[0x13A8])
        static_cast<objects::RiderSessionEnd*>(skater->fetch_module(kSkaterEndRun))->end_run(false);
    return trick[0x13A8];
}

// 0x005676E0
bool Skate::first_trick_completed()
{
    objects::Rider* skater = get_rider_by_number(static_cast<uint32_t>(horse_->skater_));
    return reinterpret_cast<uint8_t*>(skater->fetch_module(kTrick))[0x13A9];
}

// --- the per-player settings ----------------------------------------------------------------------------------------

// 0x00567BD0
void Skate::set_turn_tap_count(int player, bool on)
{
    participants_[player].spin_taps_ = on;
    if (objects::GameEntity* skater = get_rider_at(player))
        reinterpret_cast<uint8_t*>(skater->fetch_module(kSkaterCorePhysics))[0x2DA] = on;
}

// 0x00567C00
void Skate::set_auto_eject(int player, bool on)
{
    participants_[player].auto_kick_ = on;
    if (objects::GameEntity* skater = get_rider_at(player))
        reinterpret_cast<uint8_t*>(skater->fetch_module(kSkaterCorePhysics))[0x2D9] = on;
}

// 0x00567C30
void Skate::change_rumble(int player, int on)
{
    participants_[player].vibration_ = static_cast<uint8_t>(on) != 0;
    if (objects::GameEntity* skater = get_rider_at(player)) {
        if (auto* vibration = static_cast<objects::PadRumbleModule*>(skater->fetch_module(kVibration)))
            vibration->change_live(static_cast<uint8_t>(on) != 0); // (the whole word passed: only its low byte read)
    }
}

// --- the module's script --------------------------------------------------------------------------------------------

// 0x00565ED0
void Skate::launch_script(uint32_t name, script::Struct* params)
{
    if (!own_script_) {
        void* memory = script::Script::operator new(sizeof(script::Script));
        own_script_ = memory ? static_cast<script::Script*>(memory)->make_new() : nullptr;
    }
    own_script_->bind_script(name, params, nullptr);
}

// 0x00565F80
bool Skate::kill_script()
{
    script::Script* script = own_script_;
    if (!script)
        return false;
    script->teardown();
    script::Script::operator delete(script);
    own_script_ = nullptr;
    return true;
}

// 0x00565DA0 (static): the rails drawn (compiled out: the stub calls stay, with what they were given).
void __cdecl Skate::debug_rails()
{
    Skate* skate = sole();
    if (!skate->debug_rails_)
        return;
    stubs::ignore_argument(0); // (the rail manager's debug drawing)
    auto* manager = reinterpret_cast<objects::EntityHub*>(entity_hub_handle());
    for (objects::ObjectModule* component = manager->find_first_module_of_kind(kRailManagerType); component;
         component = component->next_of_type_) {
        uint8_t* object = reinterpret_cast<uint8_t*>(component->entity_);
        float matrix[16];
        std::memcpy(matrix, object + 0x6C, sizeof(matrix));
        matrix[3] = 0.0f;
        matrix[7] = 0.0f;
        matrix[11] = 0.0f;
        std::memcpy(matrix + 12, object + 0x4C, 12);
        matrix[15] = 1.0f;
        void* rails = static_cast<objects::GrindPathLinkModule*>(component)->get_rail_groups();
        if (rails)
            stubs::ignore_argument(static_cast<int>(reinterpret_cast<uintptr_t>(matrix)));
    }
}

} // namespace modules

namespace modules {

// --- each frame -----------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kHideLoadingScreen = 0xC5C9373A;            // hide_loading_screen
constexpr uint32_t kRestoreSkaterCamera = 0x15674315;          // restore_skater_camera
constexpr uint32_t kGameUpdate = 0x1AEF674F;                   // Game_Update
constexpr uint32_t kIsChangingLevels = 0xE997DB9F;             // is_changing_levels
constexpr uint32_t kCheckForUnpluggedControllers = 0x77A5662C; // check_for_unplugged_controllers
constexpr uint32_t kRootWindow = 0x56A1EAE3;                   // root_window
constexpr uint32_t kUnpluggedDialogAnchor = 0x2EDB780F;        // controller_unplugged_dialog_anchor
constexpr uint32_t kUnpluggedFrameCount = 0xF7FABB50;          // controller_unplugged_frame_count
constexpr uint32_t kLeavingNetGame = 0xEAA48E14;               // leaving_net_game
constexpr uint32_t kDeviceNum = 0xC9428A08;                    // device_num
constexpr uint32_t kControllerUnplugged = 0xF77ACA62;          // controller_unplugged
constexpr uint32_t kSuspend = 0xCE0CA665;                      // suspend

// The game's cameras (0x004A09C0 the viewport's, 0x004A0950 the split screen's).
gfx::ViewCam* viewport_camera(int index) { return render::fetch_live_camera(index); }
gfx::ViewCam* screen_camera(int index) { return render::obtain_active_lens(index); }

// The screen element with this id (its smart pointer let go at once).
frontend::UiWidget* find_element(uint32_t id)
{
    frontend::ElementPtr found;
    return frontend::UiTree::sole()->fetch_part(&found, id, 0)->owner_object_;
}

} // namespace

// 0x00564D30 (an object walk's callback): the object's suspend component told to work out whether it's drawn.
void __cdecl Skate::update_suspend_visibility(objects::Entity* object, void*)
{
    if (auto* suspend =
            static_cast<objects::DormancyModule*>(static_cast<objects::GameEntity*>(object)->fetch_module(kSuspend)))
        suspend->refresh_shown_state();
}

// 0x00567710
void Skate::run_frame_logic()
{
    commands::g_tod_manager->tick_frame();
    objects::next_suspend_frame();
    if (!objects::g_simulation_halted && !objects::g_view_mode) {
        stubs::do_nothing();
        if (movies_->total_of()) {
            if (movies_->obtain_active_lens())
                render::choose_camera(0, static_cast<gfx::ViewCam*>(movies_->obtain_active_lens()));
            if (movie_camera_)
                script::launch_script(kHideLoadingScreen, nullptr, nullptr, false, nullptr);
            movies_->tick_frame();
            movie_camera_ = true;
        } else if (movie_camera_) {
            script::launch_script(kRestoreSkaterCamera, nullptr, nullptr, false, nullptr);
            movie_camera_ = false;
        }
        if (object_movies_->total_of())
            object_movies_->tick_frame();
    }
    prepare_entity_update_data();
    script::launch_script(kGameUpdate, nullptr, nullptr, false, nullptr);
    if (objects::g_simulation_halted)
        entity_hub_handle()->for_each(&update_suspend_visibility, nullptr);
    // The proximity nodes told where the players (and their cameras) are.
    if (get_home_rider()) {
        int screens = g_viewport_count;
        if (screens == 1) {
            if (viewport_camera(0)) {
                const maths::Vec4* camera = viewport_camera(0)->fetch_location();
                objects::proximity_notify(1, get_home_rider(), camera, 1);
                maths::Vec4* position = &get_home_rider()->at_point_;
                objects::proximity_notify(1, get_home_rider(), position, 2);
            }
        } else if (screens == 2) {
            if (screen_camera(0)) {
                objects::Entity* first = entity_hub_handle()->look_up_entity(0);
                objects::proximity_notify(1, first, screen_camera(0)->fetch_location(), 1);
                objects::proximity_notify(1, first, &static_cast<objects::GameEntity*>(first)->at_point_, 2);
            }
            if (screen_camera(1) && entity_hub_handle()->look_up_entity(1)) {
                objects::Entity* second = entity_hub_handle()->look_up_entity(1);
                objects::proximity_notify(2, second, screen_camera(1)->fetch_location(), 1);
                objects::proximity_notify(2, second, &static_cast<objects::GameEntity*>(second)->at_point_, 2);
            }
        }
    } else if (static_cast<uint8_t>(commands::cfunc_is_observing(nullptr, nullptr)) && g_viewport_count == 1 &&
               viewport_camera(0)) {
        objects::proximity_notify(1, nullptr, viewport_camera(0)->fetch_location(), 1);
    }
    if (stage_switched_)
        stage_switched_ = false;
    // An unplugged controller: the dialog after controller_unplugged_frame_count frames (leaving a net game).
    int changing = script::fetch_int_value(kIsChangingLevels);
    if (!script::fetch_int_value(kCheckForUnpluggedControllers) || changing || !watch_for_unplug_)
        return;
    if (!find_element(kRootWindow) || find_element(kUnpluggedDialogAnchor))
        return;
    bool unplugged = false;
    uint8_t* device = nullptr;
    for (int i = 0; i < 8; ++i) {
        auto* skater = static_cast<objects::Rider*>(get_rider_at(i));
        if (!skater || !skater->is_local_skater_)
            continue;
        device = *reinterpret_cast<uint8_t**>(skater->unknown264_ + 0x490);
        if (reinterpret_cast<sio::InputPort*>(*reinterpret_cast<void**>(device + 0x1C))->is_attached())
            continue;
        unplugged = true;
        if (unplugged_frames_ > script::fetch_int_value(kUnpluggedFrameCount)) {
            unplugged_frames_ = 0;
            script::Struct* params = script::spawn_param_bag();
            if (params)
                params = params->prepare_all();
            online::Hub* net = net_session_slot();
            if (net->in_net_game()) {
                goals::GoalManager* goals = goals::get_objective_board();
                goals->disengage_every_objective(false);
                goals->release_every_objective();
                params->put_name_key(0u, kLeavingNetGame);
                leave_server();
                if (static_cast<uint8_t>(net->is_client_side())) {
                    net->remove_players();
                    net->disconnect_clients();
                }
            }
            params->put_int(kDeviceNum, *reinterpret_cast<int*>(*reinterpret_cast<uint8_t**>(device + 0x1C) + 0xC));
            script::launch_script(kControllerUnplugged, params, nullptr, false, nullptr);
            delete_structure(params);
            ++unplugged_frames_;
            return;
        }
    }
    if (unplugged)
        ++unplugged_frames_;
    else
        unplugged_frames_ = 0;
}

// 0x00568C90 (the logic task's code)
void __cdecl Skate::front_end_tick(tasks::Task<Skate>* task)
{
    Skate* skate = task->content_;
    online::Hub* net = net_session_slot();
    skate->run_frame_logic();
    if (net->get_on_server() && skate->count_riders())
        skate->test_rider_contacts();
    if (get_game_mode()->get_name_key() != kNetKing && get_game_mode()->get_name_key() != kKing)
        return;
    if (get_game_mode()->finish_rules_reached())
        return;
    auto* king = as_player(net->get_hill_ruler());
    if (!king || king->skater_->is_halted_)
        return;
    // The king's time counts up; the beeps as the target comes near.
    auto* score = static_cast<PointTally*>(king->skater_->get_points_total());
    int total = score->get_total_score();
    score->set_grand_total(total -
                           ftol(frame_clock::get_slomo_frame_seconds() * 60.0 * static_cast<double>(-16.666666f)));
    int target = get_game_mode()->get_first_condition_value();
    int current = static_cast<uint8_t>(get_game_mode()->is_squad_match()) ? net->get_squad_points(king->team_)
                                                                          : score->get_total_score();
    skate->timer_beep(static_cast<float>(static_cast<double>(target - current) * static_cast<double>(0.001f)), 10.0f,
                      10.0f, "timer_runout_beep"); // "timer_runout_beep"
}

// --- made and destroyed ---------------------------------------------------------------------------------------------

// 0x00568DE0
Skate::Skate()
{
    riders_.list_link_.make_head();
    watch_for_unplug_ = false;
    unplugged_frames_ = 0;
    update_job_ = new tasks::Task<Skate>(&front_end_tick, this, 0);
    reinterpret_cast<SkateNet*>(this)->create_network_tasks();
    objects::g_profile_roster->reset_templates();
    game_flow_ = new goals::MatchFlow;
    void* memory = memory::alloc_zeroed(0x39C);
    goal_manager_ = memory ? static_cast<goals::GoalManager*>(memory)->make_new() : nullptr;
    render::engine::change_entity_hub(static_cast<void*>(objects::g_composite_registry));
    career_ = new objects::RiderStoryProgress;
    memory = memory::alloc_zeroed(0x54);
    rail_manager_ = memory ? static_cast<objects::GrindRailHub*>(memory)->prepare_all() : nullptr;
    memory = memory::alloc_zeroed(0x404);
    checksums_ = memory ? static_cast<objects::ChecksumSet*>(memory)->make_new() : nullptr;
    get_trick_objects()->remove_all_clusters();
    level_launched_ = false;
    match_style_ = kFreeskate;
    splitscreen_preferences_ = new prefs::Preferences;
    splitscreen_preferences_->read_in(kDefaultSplitscreenPreferences);
    memory = memory::alloc_zeroed(0x168);
    competition_ = memory ? static_cast<Contest*>(memory)->make_new(0) : nullptr;
    memory = memory::alloc_zeroed(0x438);
    horse_ = memory ? static_cast<LetterMatch*>(memory)->make_new() : nullptr;
    movies_ = new objects::VideoHub;
    object_movies_ = new objects::VideoHub;
    highscores::g_game_records->init_levels(0x15);
    for (uint32_t i = 0; i < 8; ++i) {
        participants_[i].unknown0_ = static_cast<int>(i); // (its index)
        participants_[i].vibration_ = true;
        participants_[i].auto_kick_ = true;
        if (objects::Rider* skater = get_skater_at(i))
            reinterpret_cast<uint8_t*>(skater->fetch_module(kSkaterCorePhysics))[0x2D9] = 1;
        participants_[i].spin_taps_ = false;
        if (objects::Rider* skater = get_skater_at(i))
            reinterpret_cast<uint8_t*>(skater->fetch_module(kSkaterCorePhysics))[0x2DA] = 0;
    }
    own_script_ = nullptr;
}

// 0x005663A0
Skate::~Skate()
{
    gfx::ClipStore::teardown();
    surface_kind::clear_terrain_types();
    script::launch_script(kUninitLightManager, nullptr, nullptr, false, nullptr);
    if (Contest* competition = competition_) {
        competition->destroy_body();
        memory::dealloc(competition);
    }
    memory::dealloc(horse_);
    delete movies_;
    delete object_movies_;
    if (objects::GrindRailHub* rails = rail_manager_) {
        rails->tidy_up();
        memory::dealloc(rails);
    }
    delete career_;
    reinterpret_cast<SkateNet*>(this)->delete_network_tasks();
    delete game_flow_;
    if (goals::GoalManager* goals = goal_manager_) {
        goals->destroy_body();
        memory::dealloc(goals);
    }
    delete update_job_;
    delete splitscreen_preferences_;
    memory::dealloc(checksums_);
    if (script::Script* script = own_script_) {
        script->teardown();
        script::Script::operator delete(script);
    }
    riders_.list_link_.uncouple(); // (0x00565730)
}

// 0x00569580 / 0x00569590: around the network manager's queued scripts.
void Skate::begin_queued_scripts() { running_queued_scripts_ = true; }
void Skate::end_queued_scripts() { running_queued_scripts_ = false; }

// 0x005695A0: the cheats on, as one value (their checksums OR-ed together, as the original).
uint32_t Skate::get_cheats()
{
    static const uint32_t kCheats[] = {0xEEFECF1B, 0xB38341C9, 0xCD09E062, 0x69A1CE96, 0x6B0F24BC,
                                       0x520A66EF, 0xEBA89179, 0xC4FFC672, 0xD5982173};
    uint32_t cheats = 0;
    for (uint32_t cheat : kCheats)
        if (career_->is_code_on(cheat))
            cheats |= cheat;
    return cheats;
}

} // namespace modules

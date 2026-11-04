// The skate module's skaters (see skate_module.h): the list walked by index, added / removed / re-identified, sent to
// their restart points, cleaned up and reset with the level, the game launched, their collisions, runs and records,
// the per-player settings, the module's script, its destructor.

#include "game/skate/skate_module.h"

#include <memory>
#include <cstdint>
#include <cstring>
#include <new>
#include <array>

#include "engine/memory/arena_alloc.h"
#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/core/stubs.h"
#include "engine/memory/task_list.h"
#include "engine/lists/chain_start.h"
#include "engine/objects/entity.h"
#include "engine/ui/screen_element.h"
#include "engine/ui/screen_element_manager.h"
#include "game/front_end/front_end.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "engine/gfx/tod_manager.h"
#include "engine/pad_input/device.h"
#include "engine/pad_input/input_hub.h"
#include "game/online/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "game/online/player_info.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/movie_manager.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/proximity.h"
#include "engine/objects/suspend_component.h"
#include "engine/objects/shadow_component.h"
#include "game/world/trick_objects.h"
#include "engine/objects/vibration_component.h"
#include "engine/script/preferences.h"
#include "game/skate/highscores.h"
#include "engine/script/cscript.h"
#include "game/commands/game_cfuncs.h"
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
#include "engine/sys/stopwatch.h"
#include "engine/sys/model_viewer.h"
#include "engine/gfx/anim_cache.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/model_appearance.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/viewport.h"
#include "engine/objects/particle_component.h"
#include "game/rider/core_physics.h"
#include "game/rider/balance_trick.h"
#include "base/core/ints.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/script/param_struct.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/view_setup.h"
#include "game/online/net_game_state.h"
#include "base/core/checksums.h"
#include "base/maths/vec4.h"
#include "wire/message_ids.h"
#include "base/maths/mat4.h"
#include <gsl/pointers>
#include <span>
#include <gsl/assert>

namespace modules {

namespace {

constexpr int kSkaterSlotCount = 8;
constexpr std::size_t kPointBytes = 12;              // three floats
constexpr int kMatrixCornerW = 15;
constexpr double kFramesPerSecond = 60.0;
constexpr float kMillisecondsPerFrame = 16.666666F; // one frame at 60 a second
constexpr float kMillisecondsToSeconds = 0.001F;
constexpr float kKingBeepStartTime = 10.0F; // the timer beeps start this many seconds before the target
constexpr float kKingBeepRate = 10.0F;
constexpr int kHighScoreLevelCount = 0x15;

} // namespace

namespace {
// NOT ORIGINAL grouping - skate_module_skaters.cpp's state (the original's globals). Made on first use, never destroyed
// (skate_module_skaters_state()).
struct SkateModuleSkatersState {
    script::StructPtr cat_info_;
    script::StructPtr stat_target_info_;
};

SkateModuleSkatersState& skate_module_skaters_state()
{
    static core::NoDestroy<SkateModuleSkatersState> state;
    return state.get();
}

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
constexpr uint32_t kDefaultSplitscreenPreferences = 0x0076EAE3; // default_splitscreen_preferences
// The CTF / scavenger hunt restart points by team.
constexpr std::array<uint32_t, 4> kTeamRestarts = {{0xACD3422F, 0x9AB434CC, 0x5C427A5C, 0x2C3ED607}}; // TRG_CTF_Restart_<colour>

objects::MiscHub* entity_hub_handle() { return static_cast<objects::MiscHub*>(objects::object_hub_slot().get()); }

// The C runtime's _ftol (0x006236FC): truncated toward zero, the low 32 bits of the 64-bit result.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
memory::Arena* rider_heap_at(int index) { return core::at(memory::Hub::get_pull()->skater_heaps(), index); }

using PlayerSearch = online::PlayerSearch;
online::PlayerRecord* as_player(void* player) { return static_cast<online::PlayerRecord*>(player); }

// The skater list's node (lists::Elem<Rider>, +0x238 of the skater) out of the list.
void unlink_skater_node(memory::ListNode* node)
{
    node->prev()->set_next(node->next());
    node->next()->set_prev(node->prev());
    node->set_next(node);
    node->set_prev(node);
}

} // namespace

// --- the list -------------------------------------------------------------------------------------------------------

memory::ListNode* Skate::skater_node(uint32_t index)
{
    return lists::node_at(riders_.list_link(), static_cast<int>(index)); // (lists::Foremost's fetch_entry)
}

// 0x005665C0
objects::GameEntity* Skate::get_rider_at(int index)
{
    if (static_cast<uint32_t>(count_riders()) <= static_cast<uint32_t>(index)) {
        return nullptr;
    }
    return static_cast<objects::GameEntity*>(skater_node(static_cast<uint32_t>(index))->data());
}

// The skater at `index` as the module's loops find it (null past the end; the node's data otherwise).
objects::Rider* Skate::get_skater_at(uint32_t index)
{
    if (ints::as_unsigned(count_riders()) <= index) {
        return nullptr;
    }
    return static_cast<objects::Rider*>(skater_node(index)->data());
}

// 0x00566520
objects::Rider* Skate::get_home_rider()
{
    for (uint32_t i = 0; i < ints::as_unsigned(count_riders()); ++i) {
        auto* skater = static_cast<objects::Rider*>(skater_node(i)->data()); // (no check: as the original)
        if (skater->is_local_skater() != 0U) {
            return skater;
        }
    }
    return nullptr;
}

// 0x00566600
objects::Rider* Skate::get_rider_by_number(uint32_t id)
{
    for (uint32_t i = 0; i < ints::as_unsigned(count_riders()); ++i) {
        auto* skater = static_cast<objects::Rider*>(skater_node(i)->data());
        if (skater->ident() == id) {
            return skater;
        }
    }
    return nullptr;
}

// --- adding and removing --------------------------------------------------------------------------------------------

// 0x00565790
void Skate::drop_skater_from_roster(objects::Rider* skater)
{
    if (objects::Entity* board = objects::MiscHub::look_up_entity(skater->ident() + kBailBoard)) {
        board->flag_as_dead();
        entity_hub_handle()->flush_dead_objects();
    }
    if (skater->is_local_skater() == 0U) {
        const script::StructPtr params = script::make_struct();
        params->put_name_key(checksums::Name{kSkaterId}, skater->ident());
        script::launch_script(kRemoveClientSkater, params.get(), nullptr, false, nullptr);
    }
    if ((skater->is_local_skater() != 0U) && skater->view_slot() == 0) {
        // The local player's created tricks and stat goals kept for the skater that replaces it.
        memory::destroy_then_clear(skate_module_skaters_state().cat_info_);
        debug::printf("CAT: Storing Created Trick data\n"); // "CAT: Storing Created Trick data\n"
        skate_module_skaters_state().cat_info_ = script::make_struct();
        skater->write_cat_info(skate_module_skaters_state().cat_info_.get());
        memory::destroy_then_clear(skate_module_skaters_state().stat_target_info_);
        debug::printf("CAT: Storing Stat Goal data\n"); // "CAT: Storing Stat Goal data\n"
        skate_module_skaters_state().stat_target_info_ = script::make_struct();
        skater->write_list_44b_66a_9f(skate_module_skaters_state().stat_target_info_.get());
    }
    unlink_skater_node(skater->get_skater_link());
    gsl::owner<decltype(skater)> owned = memory::owned_element(skater); // (the module's list's)
    delete owned;
}

// 0x00567180
gsl::owner<objects::Rider*> Skate::spawn_skater_entity(objects::RiderIdentity* profile, bool local, uint32_t id, int viewport)
{
    online::Hub* net = online::online_hub();
    if (local) {
        objects::RiderIdentity* source = objects::profile_roster()->fetch_account(
            (static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)) != 0U) ? viewport : 0);
        profile->appearance()->copy_from(source->appearance());
        script::launch_script(kShowPanelStuff, nullptr, nullptr, false, nullptr);
        objects::Rider* skater = get_home_rider();
        if (skater) {
            if (static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)) != 0U) {
                if (skater->ident() == id) {
                    return skater;
                }
            } else {
                if (skater->ident() == id) {
                    return skater;
                }
                // The local skater given the new id (its place in the list by it, its particles, its score's).
                auto* node = skater->get_skater_link();
                node->set_priority(static_cast<int>(id));
                unlink_skater_node(node);
                riders_.insert_by_unique_order(node);
                memory::enter_heap_scope(rider_heap_at(skater->get_skater_number()));
                script::launch_script(kDestroySkaterParticles, nullptr, skater, false, nullptr);
                skater->change_serial(id);
                script::launch_script(kInitSkaterParticles, nullptr, skater, false, nullptr);
                memory::leave_heap_scope();
                skater->get_points_total()->set_rider_number(static_cast<int16_t>(id));
                return skater;
            }
        } else if (objects::Entity* camera = objects::MiscHub::look_up_entity(kSkaterCam0)) {
            camera->object_flags() &= ~2U;
            gsl::owner<objects::Entity*> owned = memory::owned_element(camera); // (the object manager's)
            delete owned;
        }
    } else if (net->in_net_game()) {
        if (objects::Rider* skater = get_home_rider()) {
            if (auto* vibration = core::down_cast<objects::PadRumbleModule>(skater->fetch_module(kVibration))) {
                vibration->stop_all();
            }
        }
    }
    net->in_net_game();                 // (its answer unused, as the original)
    memory::push_mem_profile("Skater"); // "Skater"
    const int heap = find_free_rider_heap(local);
    memory::enter_heap_scope(rider_heap_at(heap));
    auto* skater =
        memory::make_owned<objects::Rider>(viewport, core::down_cast<objects::EntityHub>(entity_hub_handle()), local,
                                           objects::Rider::RiderSlot{.id = id, .skater_number_ = heap}, 1);
    skater->init_from_profile(profile); // (no memory: a crash, as the original)
    if (static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)) != 0U) {
        const script::StructPtr params = script::make_struct();
        params->put_int(checksums::Name{kPlayer}, 0);
        script::launch_script(kModelApplyCustomLogos, params.get(), nullptr, false, nullptr);
    }
    if (skater->is_local_skater() == 0U) {
        script::launch_script(kClientSkaterInit, nullptr, skater, false, nullptr);
    }
    auto* node = skater->get_skater_link();
    node->set_priority(static_cast<int>(skater->ident()));
    riders_.insert_by_unique_order(node);
    bind_controllers();
    if ((skater->is_local_skater() != 0U) && skater->view_slot() == 0) {
        if (skate_module_skaters_state().cat_info_) {
            debug::printf("CAT: Restoring Created Tricks\n"); // "CAT: Restoring Created Tricks\n"
            skater->load_cat_info(skate_module_skaters_state().cat_info_.get());
            memory::destroy_then_clear(skate_module_skaters_state().cat_info_);
        }
        if (skate_module_skaters_state().stat_target_info_) {
            debug::printf("CAT: Restoring Stat Goal data\n"); // "CAT: Restoring Stat Goal data\n"
            skater->load_list_44b_66a_9f(skate_module_skaters_state().stat_target_info_.get());
            memory::destroy_then_clear(skate_module_skaters_state().stat_target_info_);
        }
    }
    if (skater->ident() == 1 && (static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)) != 0U)) {
        core::down_cast<objects::MoveExecutor>(skater->fetch_module(kTrick))->apply_trick_bindings(profile);
    }
    memory::pop_mem_profile();
    memory::leave_heap_scope();
    return skater;
}

// 0x00565FF0
int Skate::find_free_rider_heap(bool local)
{
    int index = local ? 0 : 1;
    if (goals::game_mode()->get_rider_ceiling() <= 0) {
        return index;
    }
    for (int tries = 0;;) {
        bool used = false;
        for (uint32_t i = 0; i < ints::as_unsigned(count_riders()); ++i) {
            if (static_cast<objects::Rider*>(skater_node(i)->data())->get_skater_number() == index) {
                used = true;
                break;
            }
        }
        if (!used) {
            return index;
        }
        index = static_cast<int>(static_cast<uint32_t>(index + 1) %
                                 static_cast<uint32_t>(goals::game_mode()->get_rider_ceiling()));
        if (++tries >= goals::game_mode()->get_rider_ceiling()) {
            return index;
        }
    }
}

// 0x00565BF0
void Skate::set_skater_shadow(objects::Rider* skater)
{
    auto* shadow = core::down_cast<objects::GroundShadowModule>(skater->fetch_module(kShadow));
    int type = (render::view_setup().hq_shadows() != 0U) ? 1 : 2;
    memory::enter_heap_scope(rider_heap_at(skater->get_skater_number()));
    if (static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)) != 0U) {
        type = 2;
    }
    if (skater->is_local_skater() == 0U) {
        type = 2;
    }
    script::StructPtr params = script::make_struct();
    params->push_float(checksums::Name{kBlobSizeFactor}, 1.0F);
    params->append_word(kShadowModel, "Ped_Shadow"); // "Ped_Shadow"
    shadow->configure_from_data(params.get());
    params.reset();
    shadow->destroy_shadow();
    shadow->build_blob_shadow(type, "Ped_Shadow");
    memory::leave_heap_scope();
}

// 0x00567090
void Skate::bind_controllers()
{
    for (uint32_t i = 0; i < kSkaterSlotCount; ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (!skater || (skater->is_local_skater() == 0U)) {
            continue;
        }
        const int number = skater->get_skater_number();
        auto* input = pad_input::get_input_manager();
        int controller = input->get_player_controller(number);
        core::down_cast<objects::PadInputModule>(skater->fetch_module(kInput))->link_to_pad(controller);
        if (auto* camera = core::down_cast<objects::GameEntity>(skater->get_skater_camera())) {
            controller = pad_input::get_input_manager()->get_player_controller(number);
            core::down_cast<objects::PadInputModule>(camera->fetch_module(kInput))->link_to_pad(controller);
        }
    }
}

// --- the restart points ---------------------------------------------------------------------------------------------

// 0x00566100
uint32_t Skate::warp_to_restart(uint32_t skater_id)
{
    online::Hub* net = online::online_hub();
    auto generic = [] {
        debug::printf(
            "\n\nWARNING - no Player1 restart point, trying Generic\n\n"); // "WARNING - no Player1 restart point, trying Generic"
        const uint32_t node = objects::get_spawn_node(checksums::Name{0}, 0, 0);
        if (!node) {
            debug::printf("\n\nWARNING - No restart points found\n\n"); // "WARNING - No restart points found"
        }
        return node;
    };
    auto found = [&](uint32_t node) { return node ? node : generic(); };
    auto multiplayer = [&](uint32_t start) {
        const uint32_t node = objects::get_spawn_node(checksums::Name{kMultiplayer}, static_cast<int>(net->get_start_info_word(static_cast<int>(start))), 0);
        if (node) {
            return node;
        }
        debug::printf(
            "\n\nWARNING - no multiplayer restart point, trying player1\n\n"); // "WARNING - no multiplayer restart point, trying player1"
        return found(objects::get_spawn_node(checksums::Name{kPlayer1}, 0, 0));
    };
    if (!net->in_net_game() && goals::game_mode()->get_starting_rider_count() <= 1) {
        return found(objects::get_spawn_node(checksums::Name{kPlayer1}, static_cast<int>(skater_id), 0));
    }
    if (static_cast<uint8_t>(goals::game_mode()->is_squad_match()) != 0U) {
        if (goals::game_mode()->get_name_key() == kNetLobby) {
            return found(objects::get_spawn_node(checksums::Name{kTeam}, 0, 0));
        }
        if (goals::game_mode()->get_name_key() == kNetCtf) {
            auto* local = as_player(net->get_own_player());
            if (!local || static_cast<uint32_t>(local->team()) > 3) {
                return generic();
            }
            return core::at(kTeamRestarts, local->team());
        }
        if (goals::game_mode()->get_name_key() == kNetScavengerHunt) {
            auto* local = as_player(net->get_own_player());
            if (!local) {
                return generic();
            }
            goals::Objective* hunt = goals::get_objective_board()->get_goal(kScavenger, 1);
            const int team =
                hunt->is_operative() ? local->team() : (local->team() + 1) % goals::game_mode()->current_num_teams();
            if (static_cast<uint32_t>(team) > 3) {
                return generic();
            }
            return core::at(kTeamRestarts, team);
        }
        return multiplayer(skater_id);
    }
    if (goals::game_mode()->is_true(kIsHorse)) {
        return found(objects::get_spawn_node(checksums::Name{kHorse}, horse_->get_restart(), 0));
    }
    uint32_t start = skater_id;
    if (goals::game_mode()->get_name_key() == kNetScavengerHunt || goals::game_mode()->get_name_key() == kScavengerHunt) {
        goals::Objective* hunt = goals::get_objective_board()->get_goal(kScavenger, 1);
        if (!hunt->is_operative()) {
            start = (skater_id + 1) % static_cast<uint32_t>(count_riders());
        }
    }
    return multiplayer(start);
}

// 0x00566370
void Skate::teleport_to_spawn(objects::Rider* skater, uint32_t node, uint32_t walking)
{
    if (node == 0U) {
        node = warp_to_restart(skater->ident());
        if (node == 0U) {
            return;
        }
    }
    skater->jump_to_spawn({.node = node, .walking = walking});
}

// 0x00566980
void Skate::reset_skaters(SpawnChoice spawn_choice)
{
    const auto node = spawn_choice.node;
    const auto walking = spawn_choice.walking;
    for (uint32_t i = 0; i < ints::as_unsigned(count_riders()); ++i) {
        objects::Rider* skater = get_skater_at(i); // (no check: as the original)
        if (skater->is_local_skater() != 0U) {
            const uint32_t at = (node != 0U) ? node : warp_to_restart(skater->ident());
            if (at != 0U) {
                skater->jump_to_spawn({.node = at, .walking = walking});
            }
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
    for (uint32_t i = 0; i < ints::as_unsigned(count_riders()); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (!skater) {
            continue;
        }
        skater->object_flags() |= 2; // (kept through the flush below)
        if (skater->is_local_skater() != 0U) {
            skater->relaunch(0, 1);
            movies_->clear_movies();
        }
        skater->reset_components();
        skater->conceal_skater();
    }
    movies_->clear_movies();
    object_movies_->stop_all();
    object_movies_->clear_movies();
    if (Viewer* viewer = get_live_viewer()) {
        viewer->drop_viewer_entity();
    }
    entity_hub_handle()->for_each(&objects::dispose_when_free, nullptr);
    entity_hub_handle()->flush_dead_objects();
    for (uint32_t i = 0; i < ints::as_unsigned(count_riders()); ++i) {
        objects::Rider* skater = get_skater_at(i);
        if (!skater) {
            continue;
        }
        skater->object_flags() &= ~2U;
        skater->show_skater();
    }
    online::online_hub()->clear_crown();
    script::kill_all_child_scripts();
    objects::trick_spot_hub()->remove_all_clusters();
    static_cast<render::FlexibleParticleManager*>(render::flexible_particle_manager())->destroy_non_persistent();
    rail_manager_->tidy_up();
    objects::nearby_zone_shutdown();
    objects::emitter_cleanup();
    objects::particle_hub()->kill_every_particle();
}

// 0x00565A00
void Skate::change_level(uint32_t level)
{
    debug::printf("-----------------------------------------Skate::ChangeLevel(%d)\n",
                  level); // "-----...Skate::ChangeLevel(%d)\n"
    memory::enter_heap_scope(memory::Hub::get_pull()->skater_info_heap());
    gsl::owner<script::Struct*> params = script::new_struct();
    params->get_clear();
    params->add_component_of_type(kLevel, script::QbValueKind::kName, level);
    script::launch_script(kRequestLevel, params, nullptr, false, nullptr);
    script::free_struct(params);
    previous_level_ = level_;
    level_ = level;
    params = script::new_struct();
    params->get_clear();
    if (level != kLoadMainMenu) {
        params->add_component_of_type(0, script::QbValueKind::kName, kPreserveSkaters);
    }
    commands::cfunc_cleanup(params, nullptr);
    script::free_struct(params);
    stage_switched_ = true;
    game_flow_->get_request(kChangeLevelGameFlow);
    memory::leave_heap_scope();
}

// 0x005668A0
void Skate::restart_level()
{
    run_over_ = false;
    if (((count_riders() != 0) || online::online_hub()->in_net_game()) && level_launched_) {
        career_->begin_level(-1);
        stubs::do_nothing(); // (a compiled-out call on the trick object manager)
    }
}

// 0x00566900
void Skate::reset_level()
{
    online::Hub* net = online::online_hub();
    tidy_up();
    commands::cfunc_parse_node_array(nullptr, nullptr);
    run_over_ = false;
    objects::trick_spot_hub()->apply_player_graffiti();
    goals::GoalManager* goals = goals::get_objective_board();
    if (net->in_net_game()) {
        goals->revoke_every_clear();
    } else {
        goals->prepare_every_objective();
    }
    goals->spawn_objective_props();
    gfx::tod_manager()->clear_dead_objects();
    script::launch_script(kResetLevel, nullptr, nullptr, false, nullptr);
}

// 0x00566A60
void Skate::launch_game()
{
    online::Hub* net = online::online_hub();
    modules::front_end_instance()->get_pause_game(false);
    memory::enter_heap_scope(memory::bottom_up_heap());
    debug::printf("\nLAUNCH GAME CALLED with %d skaters \n\n",
                  count_riders()); // "\nLAUNCH GAME CALLED with %d skaters \n\n"
    if (!net->get_own_player()) {
        if (static_cast<uint32_t>(goals::game_mode()->get_rider_ceiling()) > 1) {
            memory::Hub::get_pull()->init_extra_skater_heaps(goals::game_mode()->get_rider_ceiling());
        }
        game_flow_->get_request(kInitializeGameFlow);
        game_flow_->tick_frame();
    } else {
        reset_level();
        game_flow_->get_request(kStandardGameFlow);
    }
    PlayerSearch search;
    if (auto* king = as_player(net->get_hill_ruler())) {
        king->give_crown(false);
    }
    for (void* player = net->begin_player_walk(&search, true); player; player = net->step_player_walk(&search, true)) {
        as_player(player)->clear_flags();
    }
    if (goals::game_mode()->get_name_key() == kNetLobby) {
        online::Hub::build_team_flag_set(goals::game_mode()->current_num_teams());
    }
    memory::leave_heap_scope();
}

// 0x00566BE0
void Skate::test_rider_contacts()
{
    online::Hub* net = online::online_hub();
    if (net->has_match_ended()) {
        return;
    }
    if (!net->players_can_collide() && goals::game_mode()->get_name_key() != kNetFirefight &&
        goals::game_mode()->get_name_key() != kFirefight && goals::game_mode()->get_name_key() != kNetKing &&
        goals::game_mode()->get_name_key() != kKing && goals::game_mode()->get_name_key() != kNetCtf) {
        return;
    }
    for (uint32_t i = 0; i < kSkaterSlotCount; ++i) {
        if (const objects::Rider* skater = get_skater_at(i)) {
            skater->state_history()->check_for_collisions(0);
        }
    }
    if (goals::game_mode()->get_name_key() != kNetKing && goals::game_mode()->get_name_key() != kKing) {
        return;
    }
    auto* crown = static_cast<objects::CrownObject*>(net->get_crown_holder());
    if (!crown || crown->is_worn()) {
        return;
    }
    // The crown to the first skater near the local one.
    const objects::Rider* king = nullptr;
    for (uint32_t i = 0; i < kSkaterSlotCount && !king; ++i) {
        const objects::Rider* skater = get_skater_at(i);
        if (skater && skater->state_history()->is_near_local_skater()) {
            king = skater;
        }
    }
    if (!king) {
        return;
    }
    PlayerSearch search;
    as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(king->ident())))->give_crown(true);
    for (void* player = net->begin_player_walk(&search, true); player; player = net->step_player_walk(&search, true)) {
        if (static_cast<uint8_t>(as_player(player)->is_own_player()) != 0U) {
            continue;
        }
        net::NetMessageInfo desc;
        net::AppHost* server = net->host(); // (0x005480C0)
        auto id = static_cast<uint8_t>(king->ident());
        desc.set_content(&id);
        desc.set_byte_count(1);
        desc.set_ident(online::MessageId::kKing);
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(online::MessageGroup::kGame);
        server->put_on_outbox(as_player(player)->get_link_handle(), &desc);
    }
}

// 0x00566E70
bool Skate::cfunc_retry(script::Struct* /*params*/, script::Script* /*script*/)
{
    memory::enter_heap_scope(memory::bottom_up_heap());
    sole()->launch_game();
    memory::leave_heap_scope();
    return true;
}

// 0x00566EA0
bool Skate::cfunc_launch_game(script::Struct* /*params*/, script::Script* /*script*/)
{
    memory::enter_heap_scope(memory::bottom_up_heap());
    sole()->launch_game();
    memory::leave_heap_scope();
    return true;
}

// 0x00566ED0
bool Skate::cfunc_reset_level(script::Struct* /*params*/, script::Script* script)
{
    // (the calling script isn't spawned while the level resets: its flag put back after)
    uint8_t spawned = 0;
    Skate* skate = sole();
    if (script) {
        spawned = script->spawn_flags() & script::kScriptFlagSpawned;
        script->spawn_flags() &= ~script::kScriptFlagSpawned;
    }
    memory::enter_heap_scope(memory::bottom_up_heap());
    skate->reset_level();
    memory::leave_heap_scope();
    if (script) {
        script->spawn_flags() = static_cast<uint8_t>((script->spawn_flags() & ~script::kScriptFlagSpawned) | spawned);
    }
    return true;
}

// 0x00566F40
void Skate::update_records()
{
    new_record_ = false;
    const highscores::StageBests* records = highscores::game_records()->get_level_records(career_->fetch_stage());
    objects::Rider* skater = sole()->get_home_rider();
    if (!skater) {
        return;
    }
    auto* score = skater->get_points_total();
    const highscores::BestEntryList* high_scores = records->high_scores();
    new_record_ |= high_scores->try_insert_entry({.value = score->get_total_score(), .number = 0}) != -1;
    new_record_ |= records->best_combos()->try_insert_entry({.value = score->top_chain(), .number = 0}) != -1;
    auto* balance = core::down_cast<objects::RiderTeeterMoves>(skater->fetch_module(kSkaterBalanceTrick));
    const float grind = balance->grind().best_time(); // (the longest grind, manual and lip trick: seconds)
    const float manual = balance->manual().best_time();
    const float lip_trick = balance->lip().best_time();
    const int tricks = score->best_tricks();
    new_record_ |= records->grind_record()->try_beat_record({.value = ftol(static_cast<double>(grind) * 100.0), .number = 0});
    new_record_ |= records->longest_manual()->try_beat_record({.value = ftol(static_cast<double>(manual) * 100.0), .number = 0});
    new_record_ |= records->longest_lip_trick()->try_beat_record({.value = ftol(static_cast<double>(lip_trick) * 100.0), .number = 0});
    new_record_ |= records->biggest_chain()->try_beat_record({.value = tricks, .number = 0});
}

// 0x00567580
bool Skate::all_skaters_idle()
{
    bool idle = true;
    for (uint32_t i = 0; i < ints::as_unsigned(count_riders()); ++i) {
        objects::Rider* skater = get_skater_at(i); // (null past the end, as the original - never reached)
        if (goals::game_mode()->is_true(kIsHorse) && ints::as_unsigned(horse_->skater()) != skater->ident()) {
            continue; // (in HORSE only the skater taking the run)
        }
        auto* end_run = core::down_cast<objects::RiderSessionEnd>(skater->fetch_module(kSkaterEndRun));
        if (end_run->run_done()) {
            continue;
        }
        idle = false;
        if (skater->is_local_skater() != 0U) {
            end_run->end_run(false);
        }
    }
    return idle;
}

// 0x00567690
bool Skate::first_trick_started()
{
    objects::Rider* skater = get_rider_by_number(static_cast<uint32_t>(horse_->skater()));
    auto* trick = core::down_cast<objects::MoveExecutor>(skater->fetch_module(kTrick));
    if (trick->bailed()) {
        core::down_cast<objects::RiderSessionEnd>(skater->fetch_module(kSkaterEndRun))->end_run(false);
    }
    return trick->bailed();
}

// 0x005676E0
bool Skate::first_trick_completed()
{
    objects::Rider* skater = get_rider_by_number(static_cast<uint32_t>(horse_->skater()));
    return core::down_cast<objects::MoveExecutor>(skater->fetch_module(kTrick))->bailed_panel();
}

// --- the per-player settings ----------------------------------------------------------------------------------------

// 0x00567BD0
void Skate::set_turn_tap_count(int player, bool on)
{
    core::at(participants_, player).spin_taps_ = on;
    if (objects::GameEntity* skater = get_rider_at(player)) {
        core::down_cast<skater::SkaterPhysicsModule>(skater->fetch_module(kSkaterCorePhysics))
            ->set_tap_spin(static_cast<uint8_t>(on));
    }
}

// 0x00567C00
void Skate::set_auto_eject(int player, bool on)
{
    core::at(participants_, player).auto_kick_ = on;
    if (objects::GameEntity* skater = get_rider_at(player)) {
        core::down_cast<skater::SkaterPhysicsModule>(skater->fetch_module(kSkaterCorePhysics))
            ->set_auto_kick(static_cast<uint8_t>(on));
    }
}

// 0x00567C30
void Skate::change_rumble(int player, int on)
{
    core::at(participants_, player).vibration_ = static_cast<uint8_t>(on) != 0;
    if (objects::GameEntity* skater = get_rider_at(player)) {
        if (auto* vibration = core::down_cast<objects::PadRumbleModule>(skater->fetch_module(kVibration))) {
            vibration->change_live(static_cast<uint8_t>(on) != 0); // (the whole word passed: only its low byte read)
        }
    }
}

// --- the module's script --------------------------------------------------------------------------------------------

// 0x00565ED0
void Skate::launch_script(uint32_t name, script::Struct* params)
{
    if (!own_script_) {
        own_script_ = script::Script::create();
    }
    own_script_->bind_script(name, params, nullptr);
}

// 0x00565F80
bool Skate::kill_script()
{
    script::Script* script = own_script_;
    if (!script) {
        return false;
    }
    script::Script::destroy(script);
    own_script_ = nullptr;
    return true;
}

// 0x00565DA0 (static): the rails drawn (compiled out: the stub calls stay, with what they were given).
void Skate::debug_rails()
{
    const Skate* skate = sole();
    if (!skate->debug_rails_) {
        return;
    }
    stubs::ignore_argument(0); // (the rail manager's debug drawing)
    auto* manager = core::down_cast<objects::EntityHub>(entity_hub_handle());
    for (objects::ObjectModule* component = manager->find_first_module_of_kind(kRailManagerType); component;
         component = component->next_of_type()) {
        objects::GameEntity* object = component->entity();
        std::array<float, maths::kMatrixFloats> matrix{};
        std::memcpy(matrix.data(), &object->basis_frame(), sizeof(matrix));
        core::at(matrix, 3) = 0.0F;
        core::at(matrix, maths::kMatYw) = 0.0F;
        core::at(matrix, maths::kMatZw) = 0.0F;
        std::memcpy(std::span(matrix).subspan(maths::kMatTx, 3).data(), &object->at_point(), kPointBytes);
        core::at(matrix, kMatrixCornerW) = 1.0F;
        const void* rails = core::down_cast<objects::GrindPathLinkModule>(component)->get_rail_groups();
        if (rails) {
            stubs::ignore_argument(matrix.data());
        }
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
    return frontend::UiTree::fetch_part(&found, id, 0)->get_pull();
}

} // namespace

// 0x00564D30 (an object walk's callback): the object's suspend component told to work out whether it's drawn.
void Skate::update_suspend_visibility(objects::Entity* object, void* /*unused*/)
{
    if (auto* suspend = core::down_cast<objects::DormancyModule>(
            core::down_cast<objects::GameEntity>(object)->fetch_module(kSuspend))) {
        suspend->refresh_shown_state();
    }
}

// 0x00567710
void Skate::run_frame_logic()
{
    gfx::tod_manager()->tick_frame();
    objects::next_suspend_frame();
    if (!objects::simulation_halted() && (objects::view_mode() == 0)) {
        stubs::do_nothing();
        if (movies_->total_of() != 0) {
            if (movies_->obtain_active_lens()) {
                render::choose_camera(0, static_cast<gfx::ViewCam*>(movies_->obtain_active_lens()));
            }
            if (movie_camera_) {
                script::launch_script(kHideLoadingScreen, nullptr, nullptr, false, nullptr);
            }
            movies_->tick_frame();
            movie_camera_ = true;
        } else if (movie_camera_) {
            script::launch_script(kRestoreSkaterCamera, nullptr, nullptr, false, nullptr);
            movie_camera_ = false;
        }
        if (object_movies_->total_of() != 0) {
            object_movies_->tick_frame();
        }
    }
    prepare_entity_update_data();
    script::launch_script(kGameUpdate, nullptr, nullptr, false, nullptr);
    if (objects::simulation_halted()) {
        entity_hub_handle()->for_each(&update_suspend_visibility, nullptr);
    }
    // The proximity nodes told where the players (and their cameras) are.
    if (get_home_rider()) {
        const int screens = viewport_count();
        if (screens == 1) {
            if (viewport_camera(0)) {
                const maths::Vec4* camera = viewport_camera(0)->fetch_location();
                objects::proximity_notify(1, get_home_rider(), camera, 1);
                const maths::Vec4* position = &get_home_rider()->at_point();
                objects::proximity_notify(1, get_home_rider(), position, 2);
            }
        } else if (screens == 2) {
            if (screen_camera(0)) {
                objects::Entity* first = objects::MiscHub::look_up_entity(0);
                objects::proximity_notify(1, first, screen_camera(0)->fetch_location(), 1);
                objects::proximity_notify(1, first, &core::down_cast<objects::GameEntity>(first)->at_point(), 2);
            }
            if (screen_camera(1) && objects::MiscHub::look_up_entity(1)) {
                objects::Entity* second = objects::MiscHub::look_up_entity(1);
                objects::proximity_notify(2, second, screen_camera(1)->fetch_location(), 1);
                objects::proximity_notify(2, second, &core::down_cast<objects::GameEntity>(second)->at_point(), 2);
            }
        }
    } else if ((static_cast<uint8_t>(commands::cfunc_is_observing(nullptr, nullptr)) != 0U) &&
               viewport_count() == 1 && viewport_camera(0)) {
        objects::proximity_notify(1, nullptr, viewport_camera(0)->fetch_location(), 1);
    }
    if (stage_switched_) {
        stage_switched_ = false;
    }
    // An unplugged controller: the dialog after controller_unplugged_frame_count frames (leaving a net game).
    const int changing = script::fetch_int_value(kIsChangingLevels);
    if ((script::fetch_int_value(kCheckForUnpluggedControllers) == 0) || (changing != 0) || !watch_for_unplug_) {
        return;
    }
    if (!find_element(kRootWindow) || find_element(kUnpluggedDialogAnchor)) {
        return;
    }
    bool unplugged = false;
    const objects::PadInputModule::Responder* device = nullptr;
    for (int i = 0; i < kSkaterSlotCount; ++i) {
        auto* skater = core::down_cast<objects::Rider>(get_rider_at(i));
        if (!skater || (skater->is_local_skater() == 0U)) {
            continue;
        }
        device = skater->pad_input()->handler();
        if (device->dev()->is_attached()) {
            continue;
        }
        unplugged = true;
        if (unplugged_frames_ > script::fetch_int_value(kUnpluggedFrameCount)) {
            show_unplugged_dialog(skater);
            return;
        }
    }
    if (unplugged) {
        ++unplugged_frames_;
    } else {
        unplugged_frames_ = 0;
    }
}

// (Part of 0x00567710) The controller unplugged dialog for `skater`'s pad: a net game left first.
void Skate::show_unplugged_dialog(objects::Rider* skater)
{
    const objects::PadInputModule::Responder* device = skater->pad_input()->handler();
    unplugged_frames_ = 0;
    gsl::owner<script::Struct*> params = script::spawn_param_bag();
    if (params) {
        params = new (params) script::Struct;
    }
    online::Hub* net = online::online_hub();
    if (net->in_net_game()) {
        goals::GoalManager* goals = goals::get_objective_board();
        goals->disengage_every_objective(false);
        goals->release_every_objective();
        Expects(params != nullptr);
        params->put_name_key(checksums::Name{0U}, kLeavingNetGame);
        leave_server();
        if (static_cast<uint8_t>(net->is_client_side()) != 0U) {
            net->remove_players();
            net->disconnect_clients();
        }
    }
    Expects(params != nullptr);
    params->put_int(checksums::Name{kDeviceNum}, device->dev()->port_no());
    script::launch_script(kControllerUnplugged, params, nullptr, false, nullptr);
    script::free_struct(params);
    ++unplugged_frames_;
}

// 0x00568C90 (the logic task's code)
void Skate::front_end_tick(tasks::Task<Skate>* task)
{
    Skate* skate = task->content();
    online::Hub* net = online::online_hub();
    skate->run_frame_logic();
    if (net->get_on_server() && (skate->count_riders() != 0)) {
        skate->test_rider_contacts();
    }
    if (goals::game_mode()->get_name_key() != kNetKing && goals::game_mode()->get_name_key() != kKing) {
        return;
    }
    if (goals::game_mode()->finish_rules_reached()) {
        return;
    }
    auto* king = as_player(net->get_hill_ruler());
    if (!king || king->rider()->is_halted()) {
        return;
    }
    // The king's time counts up; the beeps as the target comes near.
    auto* score = king->rider()->get_points_total();
    const int total = score->get_total_score();
    score->set_grand_total(total -
                           ftol(frame_clock::get_slomo_frame_seconds() * kFramesPerSecond * static_cast<double>(-kMillisecondsPerFrame)));
    const int target = goals::game_mode()->get_first_condition_value();
    const int current = (static_cast<uint8_t>(goals::game_mode()->is_squad_match()) != 0U)
                            ? net->get_squad_points(king->team())
                            : score->get_total_score();
    modules::Skate::timer_beep({.time_left_ = static_cast<float>(static_cast<double>(target - current) *
                                                                 static_cast<double>(kMillisecondsToSeconds)),
                                .start_time_ = kKingBeepStartTime,
                                .rate = kKingBeepRate},
                               "timer_runout_beep"); // "timer_runout_beep"
}

// --- made and destroyed ---------------------------------------------------------------------------------------------

// 0x00568DE0
Skate::Skate()
{
    riders_.list_link().make_head();
    update_job_ = std::make_unique<tasks::Task<Skate>>(&front_end_tick, this, 0);
    SkateNet::create_network_tasks(this);
    objects::profile_roster()->reset_templates();
    game_flow_ = std::make_unique<goals::MatchFlow>();
    goal_manager_ = memory::make_zeroed<goals::GoalManager>();
    render::engine::change_entity_hub(static_cast<void*>(objects::object_hub_slot().get()));
    career_ = std::make_unique<objects::RiderStoryProgress>();
    rail_manager_ = memory::make_zeroed<objects::GrindRailHub>(); // (0x54 bytes)
    checksums_ = memory::make_zeroed<objects::ChecksumSet>();     // (0x404 bytes)
    objects::trick_spot_hub()->remove_all_clusters();
    splitscreen_preferences_ = std::make_unique<prefs::Preferences>();
    splitscreen_preferences_->read_in(kDefaultSplitscreenPreferences);
    competition_ = memory::make_zeroed<Contest>();
    horse_ = memory::make_zeroed<LetterMatch>();
    movies_ = std::make_unique<objects::VideoHub>();
    object_movies_ = std::make_unique<objects::VideoHub>();
    highscores::game_records()->init_levels(kHighScoreLevelCount);
    for (uint32_t i = 0; i < kSkaterSlotCount; ++i) {
        core::at(participants_, i).unknown0_ = static_cast<int>(i); // (its index)
        core::at(participants_, i).vibration_ = true;
        core::at(participants_, i).auto_kick_ = true;
        if (objects::Rider* skater = get_skater_at(i)) {
            core::down_cast<skater::SkaterPhysicsModule>(skater->fetch_module(kSkaterCorePhysics))->set_auto_kick(1);
        }
        core::at(participants_, i).spin_taps_ = false;
        if (objects::Rider* skater = get_skater_at(i)) {
            core::down_cast<skater::SkaterPhysicsModule>(skater->fetch_module(kSkaterCorePhysics))->set_tap_spin(0);
        }
    }
    own_script_ = nullptr;
}

// 0x005663A0
Skate::~Skate()
{
    gfx::ClipStore::teardown();
    surface_kind::clear_terrain_types();
    script::launch_script(kUninitLightManager, nullptr, nullptr, false, nullptr);
    // (each destroyed while the module still points at it, as the original's deletes)
    memory::destroy_then_clear(competition_);
    memory::destroy_then_clear(horse_);
    memory::destroy_then_clear(movies_);
    memory::destroy_then_clear(object_movies_);
    memory::destroy_then_clear(rail_manager_);
    memory::destroy_then_clear(career_);
    SkateNet::delete_network_tasks(this);
    memory::destroy_then_clear(game_flow_);
    memory::destroy_then_clear(goal_manager_);
    memory::destroy_then_clear(update_job_);
    memory::destroy_then_clear(splitscreen_preferences_);
    memory::destroy_then_clear(checksums_);
    script::Script::destroy(own_script_);
    riders_.list_link().uncouple(); // (0x00565730)
}

// 0x00569580 / 0x00569590: around the network manager's queued scripts.
void Skate::begin_queued_scripts() { running_queued_scripts_ = true; }
void Skate::end_queued_scripts() { running_queued_scripts_ = false; }

// 0x005695A0: the cheats on, as one value (their checksums OR-ed together, as the original).
uint32_t Skate::get_cheats()
{
    static const auto kCheats = std::to_array<uint32_t>({0xEEFECF1B, 0xB38341C9, 0xCD09E062, 0x69A1CE96, 0x6B0F24BC,
                                       0x520A66EF, 0xEBA89179, 0xC4FFC672, 0xD5982173});
    uint32_t cheats = 0;
    for (const uint32_t cheat : kCheats) {
        if (career_->is_code_on(cheat)) {
            cheats |= cheat;
        }
    }
    return cheats;
}

} // namespace modules

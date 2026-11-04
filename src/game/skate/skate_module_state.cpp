// The skate module's state and its small commands (see skate_module.h): the singleton, the module callbacks, the
// game type, the level asked for / opened / changed, the preferences, the script commands that drive them.

#include "game/skate/skate_module.h"
#include "engine/net/net_lists.h"
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>

#include "base/core/checksums.h"
#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/task_list.h"
#include "game/park_editing/park_editor.h"
#include "engine/script/fwd.h"
#include "engine/script/host.h"
#include "game/goals/goal_manager.h"
#include "engine/gfx/view_cam.h"
#include "game/online/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "game/online/player_info.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/entity.h"
#include "engine/objects/proximity.h"
#include "game/world/trick_objects.h"
#include "engine/audio/continue_music.h"
#include "engine/audio/music_api.h"
#include "engine/script/preferences.h"
#include "engine/script/cscript.h"
#include "game/commands/game_cfuncs.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "engine/objects/node_components.h"
#include "engine/objects/rail_manager.h"
#include "game/rider/rider.h"
#include "engine/sound/surface_kind.h"
#include "engine/sys/stopwatch.h"
#include "game/park_editing/rail_editor.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/viewport.h"
#include "engine/assets/resource_registry.h"
#include "engine/gfx/engine.h"
#include "engine/sys/mlp.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "game/online/net_game_state.h"
#include "wire/message_ids.h"

namespace modules {

namespace {
constexpr float kBeepMinTimeLeft = 0.01F; // no beep this close to the target
constexpr float kMinBeepStep = 0.2F;      // seconds between beeps, at the least
constexpr int kStartupPauseMs = 2000;
constexpr int kClientHeardStampMs = 12000;     // the last-heard time a new connection is given on a client
constexpr int kServerHeardStampMs = 8000;      // ... on the server
constexpr int kParkMapMessageBytes = 15000;
constexpr int kParkRailsMessageBytes = 0x1132;
constexpr std::size_t kScriptNameBytes = 0x80;
// NOT ORIGINAL grouping - skate_module_state.cpp's state (the original's globals). Made on first use, never destroyed
// (skate_module_state_state()).
struct SkateModuleStateState {
    int skate_module_references_{};
    float next_beep_{};
};

SkateModuleStateState& skate_module_state_state()
{
    static core::NoDestroy<SkateModuleStateState> state;
    return state.get();
}

constexpr uint32_t kInitLightManager = 0x7004771D;      // init_light_manager
constexpr uint32_t kLoadPermanentPrefiles = 0x7690764F; // load_permanent_prefiles
constexpr uint32_t kNetworkDriverStartup = 0x6B1D08C3;  // network_driver_startup
constexpr uint32_t kInitLoadingBar = 0x36932FCA;        // init_loading_bar
constexpr uint32_t kStartupLoadingScreen = 0xAA37AEAE;  // startup_loading_screen
constexpr uint32_t kLoadPermSounds = 0x565D0F63;        // LoadPermSounds
constexpr uint32_t kLoadPermanentAssets = 0xAE754239;   // load_permanent_assets
constexpr uint32_t kInitEnvironment = 0x63BBA6AD;       // init_environment
constexpr uint32_t kDefaultSystemStartup = 0x738AB3D8;  // default_system_startup
constexpr uint32_t kSkater = 0x5B8AB877;                // skater
constexpr uint32_t kLoadNothing = 0x3A8E4A31;           // (name unknown: the level that loads nothing)
constexpr uint32_t kLevel = 0x651533EC;                 // level
constexpr uint32_t kFromSettingsMarker = 0x547CB588;    // use_preferences
constexpr uint32_t kShowWarning = 0xD8E34EDA;           // show_warning
constexpr uint32_t kLoadSk5EdGameplay = 0xB664035D;     // load_sk5ed_gameplay
constexpr uint32_t kFreeskate2p = 0x5C8F5D66;           // freeskate2p
constexpr uint32_t kNetwork = 0x9F7B7843;               // network
constexpr uint32_t kTaunt = 0x4839873C;                 // taunt
constexpr uint32_t kSplitscreen = 0x06AB02F2;           // splitscreen
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;          // Load_MainMenu
constexpr uint32_t kLoadCas = 0xA368B4F1;               // load_cas
constexpr uint32_t kPrefs = 0xB58E6556;                 // prefs
constexpr uint32_t kAbsentInNetGames = 0x68910AC6;      // AbsentInNetGames



int& get_refs() { return skate_module_state_state().skate_module_references_; }
float& next_beep() { return skate_module_state_state().next_beep_; }

using online::PlayerSearch;
online::PlayerRecord* as_player(void* player) { return static_cast<online::PlayerRecord*>(player); }

} // namespace

namespace {

std::unique_ptr<Skate>& stored_skate_module()
{
    static core::NoDestroy<std::unique_ptr<Skate>> skate_module;
    return skate_module.get();
}

} // namespace

Skate* Skate::sole() { return stored_skate_module().get(); }

// 0x00564C10
void Skate::discard_occurrence()
{
    if (static_cast<uint32_t>(get_refs()) > 1) {
        --get_refs();
        return;
    }
    memory::destroy_then_clear(stored_skate_module());
    get_refs() = 0;
}

// 0x005691A0
Skate* Skate::spawn_occurrence(bool create)
{
    Skate* skate = sole();
    if (!skate && create) {
        stored_skate_module() = std::make_unique<Skate>(); // (null when the heap is full)
        skate = stored_skate_module().get();
        if (skate) {
            const int refs = get_refs() + 1;
            get_refs() = refs;
            return skate;
        }
    }
    ++get_refs();
    return skate; // (the instance - null when none was made: main registers it as a module)
}

// 0x00564C50
void Skate::stop_callback() { update_job_->drop_out(); }

// 0x00564C60
void Skate::conceal_skater(objects::Rider* skater, bool hide)
{
    if (hide) {
        skater->get_freeze();
        skater->conceal(hide);
    } else {
        skater->unpause();
        skater->conceal(hide);
    }
}

// 0x00564CA0
void Skate::timer_beep(TimerBeep timer_beep_in, const char* script)
{
    const auto time_left = timer_beep_in.time_left_;
    const auto start_time = timer_beep_in.start_time_;
    const auto rate = timer_beep_in.rate;
    if (!(time_left > kBeepMinTimeLeft) || !(time_left <= start_time)) {
        next_beep() = start_time;
        return;
    }
    if (!(time_left < next_beep())) {
        return;
    }
    script::launch_script(script, nullptr, nullptr, false);
    double step = static_cast<double>(next_beep()) / rate;
    step = std::max(step, static_cast<double>(kMinBeepStep));
    next_beep() = static_cast<float>(next_beep() - step);
}

// 0x00564D50
void Skate::set_unknown370(int value) { unknown370_ = value; }

// 0x00564D60
void Skate::advance_match_flow()
{
    if (own_script_) {
        script::host().tick_frame(own_script_);
    }
    game_flow_->tick_frame();
}

// 0x00564D80
prefs::Preferences* Skate::get_split_view_settings() { return splitscreen_preferences_.get(); }

// 0x00564D90
goals::GoalManager* Skate::get_objective_board() { return goal_manager_.get(); }

// 0x00564DA0
void Skate::scan_pad_connections() { watch_for_unplug_ = true; }

// 0x00564DC0
void Skate::start_callback()
{
    surface_kind::load_terrain_types();
    script::launch_script(kInitLightManager, nullptr, nullptr, false, nullptr);
    script::launch_script(kLoadPermanentPrefiles, nullptr, nullptr, false, nullptr);
    forced_stat_ = 0.0F;
    frame_loop::Hub* main_loop = assets::get_main_loop_manager();
    objects::nearby_zone_startup();
    objects::emitter_init();
    update_job_->skip_bits() = 2;
    main_loop->logic_tasks().queue_job(update_job_.get());
    goals::game_mode()->get_reset();
    script::launch_script(kNetworkDriverStartup, nullptr, nullptr, false, nullptr);
    script::launch_script(kInitLoadingBar, nullptr, nullptr, false, nullptr);
    script::launch_script(kStartupLoadingScreen, nullptr, nullptr, false, nullptr);
    Sleep(kStartupPauseMs);
    if (!audio::music_off_requested() || !audio::streams_off()) {
        audio::prepare_all();
    }
    script::launch_script(kLoadPermSounds, nullptr, nullptr, false, nullptr);
    memory::enter_heap_scope(memory::bottom_up_heap());
    memory::push_mem_profile("Permanent Assets"); // "Permanent Assets"
    script::launch_script(kLoadPermanentAssets, nullptr, nullptr, false, nullptr);
    memory::pop_mem_profile();
    memory::leave_heap_scope();
    objects::profile_roster()->prepare_all();
    checksums_->get_fill();
    script::launch_script(kInitEnvironment, nullptr, nullptr, false, nullptr);
    script::launch_script(kDefaultSystemStartup, nullptr, nullptr, false, nullptr);
}

// 0x00564F20
void Skate::tidy_before_park_build()
{
    objects::trick_spot_hub()->remove_all_clusters();
    render::engine::kill_all_texture_splats();
    static_cast<render::FlexibleParticleManager*>(render::flexible_particle_manager())->destroy_non_persistent();
    rail_manager_->tidy_up();
    objects::nearby_zone_shutdown();
    objects::emitter_cleanup();
}

// 0x00564F60
void Skate::get_freeze(bool paused)
{
    if (paused) {
        sound::AudioMixer::update_voice_volumes();
    }
}

// 0x00564F80
objects::RiderIdentity* Skate::fetch_account(script::Struct* params)
{
    int skater = 0;
    if (params && params->fetch_int_value(kSkater, &skater, 0)) {
        return objects::profile_roster()->fetch_account(skater);
    }
    return objects::profile_roster()->fetch_active_account();
}

// 0x00564FC0
objects::RiderIdentity* Skate::fetch_account(int index) { return objects::profile_roster()->fetch_account(index); }

// 0x00564FD0
void Skate::set_current_game_type() const
{
    debug::printf(
        "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<Changing Game Mode\n"); // "<<<...Changing Game Mode\n"
    goals::game_mode()->apply_match_style(match_style_);
}

// 0x00565000
void Skate::enter_stage(uint32_t level)
{
    online::Hub* net = online::online_hub();
    memory::push_mem_profile("LaunchLevel"); // "LaunchLevel"
    if (net->in_net_game() && net->get_on_server()) {
        PlayerSearch search;
        for (void* player = net->begin_player_walk(&search, true); player;
             player = net->step_player_walk(&search, true)) {
            as_player(player)->await_ready_response(0);
        }
    }
    if (level != kLoadNothing) {
        script::launch_script(level, nullptr, nullptr, false, nullptr);
    }
    level_launched_ = true;
    level_ = level;
    if (net->in_net_game()) {
        for (int i = 0; i < 2; ++i) {
            net::ConnSearch search;
            if (net::AppHost* client = net->fetch_patron(i)) {
                if (net::Conn* conn = client->begin_conn_walk(&search)) {
                    conn->stamp_last_heard(kClientHeardStampMs);
                }
            }
        }
        if (net->get_on_server()) {
            net::ConnSearch search;
            if (net::AppHost* server = net->host()) { // (0x005480C0)
                for (net::Conn* conn = server->begin_conn_walk(&search); conn;
                     conn = net::AppHost::step_conn_walk(&search)) {
                    conn->stamp_last_heard(kServerHeardStampMs);
                }
            }
        } else {
            online::NewPlayerSearch search;
            if (!net->begin_joiner_walk(&search)) {
                net->send_ready_response();
            }
        }
    }
    memory::pop_mem_profile();
}

// 0x005651D0
void Skate::observe_next_skater()
{
    online::Hub* net = online::online_hub();
    net->spectate_player(net->next_cam_player());
}

// 0x005651F0
int Skate::is_multiplayer()
{
    if (online::online_hub()->in_net_game()) {
        return 1;
    }
    return goals::game_mode()->get_starting_rider_count() > 1 ? 1 : 0;
}

// 0x00565220
bool Skate::cfunc_set_game_type(script::Struct* params, script::Script* /*script*/)
{
    Skate* skate = sole();
    uint32_t type = 0;
    params->read_hash_value(0U, &type, 1);
    skate->match_style_ = type;
    return true;
}

// 0x00565250
// (the script runtime reads the result's low byte)
bool Skate::cfunc_in_team_game(script::Struct* /*params*/, script::Script* /*script*/)
{
    return static_cast<uint8_t>(goals::game_mode()->is_squad_match()) != 0;
}

// 0x00565260
bool Skate::cfunc_change_level(script::Struct* params, script::Script* /*script*/)
{
    debug::printf("ScriptChangeLevel\n"); // "ScriptChangeLevel\n"
    online::Hub* net = online::online_hub();
    uint32_t level = 0;
    params->read_hash_value(kLevel, &level, 1);
    if (level == kFromSettingsMarker) {
        level = net->get_level_preference();
    }
    net->change_arena(level);
    const bool show_warning = params->has_entry_named(kShowWarning);
    if (!audio::continue_music::is_switched_on()) { // NOT ORIGINAL - continue_music: the song goes on into the level
        audio::stop_music();
    }
    PlayerSearch search;
    net::AppHost* server = net->host(); // (0x005480C0)
    struct ChangeLevelMessage {
        uint32_t level;
        bool show_warning_;
        std::array<uint8_t, 3> pad;
    } message{};
    message.level = level;
    message.show_warning_ = show_warning;
    for (void* player = net->begin_player_walk(&search, true); player; player = net->step_player_walk(&search, true)) {
        online::PlayerRecord* info = as_player(player);
        net::NetMessageInfo desc;
        uint32_t now = frame_clock::get_time();
        if ((static_cast<uint8_t>(info->is_own_player()) == 0U) && level == kLoadSk5EdGameplay) {
            void* map = park_editing::ParkBuilderMode::get_packed_park_data(false)->bytes().data();
            int handle = info->get_link_handle();
            server->send_bulk_message(handle, {.msg_id_ = online::MessageId::kParkMap, .size = kParkMapMessageBytes}, map, "level data", online::MessageGroup::kGame, false,
                                      true);     // "level data"
            park_editing::RailAuthoring::sole(); // (the rail editor; this first answer unused, as the original)
            void* rails = park_editing::RailAuthoring::sole()->get_packed();
            handle = info->get_link_handle();
            server->send_bulk_message(handle, {.msg_id_ = online::MessageId::kRailsData, .size = kParkRailsMessageBytes}, rails, "rail data", online::MessageGroup::kGame, false,
                                      true); // "rail data"
        }
        desc.set_content(&message);
        desc.set_byte_count(sizeof(message));
        desc.set_ident(online::MessageId::kLevelSwitch);
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(online::MessageGroup::kGame);
        server->put_on_outbox(info->get_link_handle(), &desc);
        if ((static_cast<uint8_t>(info->is_own_player()) == 0U) && online::Hub::has_created_goals()) {
            net->prepare_created_goals(level);
            void* goals = net->get_created_goals_message();
            const uint32_t size = net->get_created_goals_size();
            server->send_bulk_message(info->get_link_handle(), {.msg_id_ = online::MessageId::kGoalsData, .size = static_cast<uint16_t>(size)}, goals,
                                      "goals data", online::MessageGroup::kGame, false, true); // "goals data"
        }
        desc.set_content(&now);
        desc.set_byte_count(4);
        desc.set_ident(online::MessageId::kReadyQuery);
        server->put_on_outbox(info->get_link_handle(), &desc);
        info->set_ready_query_owed(now);
    }
    if (!net->in_net_game()) {
        server->transmit(false);
    }
    return true;
}

// 0x005654B0
bool Skate::cfunc_launch_level(script::Struct* /*params*/, script::Script* /*script*/)
{
    Skate* skate = sole();
    skate->enter_stage(skate->pending_level_);
    return true;
}

// 0x005654D0
bool Skate::cfunc_request_level(script::Struct* params, script::Script* /*script*/)
{
    online::Hub* net = online::online_hub();
    Skate* skate = sole();
    uint32_t level = 0;
    if (!params->read_hash_value(0U, &level, 0)) {
        const char* name = nullptr;
        if (params->fetch_text(0U, &name, 1)) {
            std::array<char, kScriptNameBytes> script{};
            text::format_to(script, "load_%s", name); // "load_%s"
            level = checksums::hash_from_text(script.data());
        }
    }
    if (level == kFromSettingsMarker) {
        level = net->get_level_preference();
        net->change_arena(level);
    }
    skate->pending_level_ = level;
    return true;
}

// 0x005655B0
bool Skate::cfunc_init_skater_heaps(script::Struct* /*params*/, script::Script* /*script*/)
{
    memory::enter_heap_scope(memory::bottom_up_heap());
    int skaters = 0;
    if (sole()->match_style_ == kFreeskate2p) {
        skaters = 2;
    } else {
        skaters = online::online_hub()->get_player_limit();
        if (skaters <= 1) {
            memory::leave_heap_scope();
            return true;
        }
    }
    memory::Hub::get_pull()->delete_extra_skater_heaps();
    memory::Hub::get_pull()->init_extra_skater_heaps(skaters);
    memory::leave_heap_scope();
    return true;
}

// 0x00565620
void Skate::prepare_entity_update_data()
{
    if (viewport_count() <= 1 && render::fetch_live_camera(0)) {
        no_camera_ = false;
        viewer_pos_ = *render::fetch_live_camera(0)->fetch_location();
        return;
    }
    no_camera_ = true;
}

// 0x00565680
prefs::Preferences* Skate::get_player_options(uint32_t name)
{
    if (name == kNetwork) {
        return online::online_hub()->get_online_settings();
    }
    if (name == kTaunt) {
        return online::online_hub()->get_taunt_settings();
    }
    if (name == kSplitscreen) {
        return sole()->splitscreen_preferences_.get();
    }
    return nullptr;
}

// 0x00565D40
prefs::Preferences* Skate::get_preferences_from_params(script::Struct* params)
{
    uint32_t name = 0;
    params->read_hash_value(kPrefs, &name, 1);
    return get_player_options(name); // (inlined there)
}

// 0x005656C0
void* Skate::fetch_gap_list() { return career_->fetch_gap_list(-1); }

// 0x005656D0
int Skate::in_net_game_level() const
{
    if (online::online_hub()->in_net_game() && level_ != kLoadMainMenu) {
        return 1;
    }
    return 0;
}

// 0x00565700
bool Skate::in_front_end_or_net_mode() const
{
    if (static_cast<uint8_t>(online::online_hub()->is_network_game()) != 0U) {
        return true;
    }
    return pending_level_ == kLoadMainMenu || pending_level_ == kLoadCas;
}

// 0x00565BB0
int Skate::should_be_absent(script::Struct* params)
{
    if (!params->has_bare_flag(kAbsentInNetGames)) {
        return 0;
    }
    if (online::online_hub()->in_net_game()) {
        return 1;
    }
    return goals::game_mode()->get_starting_rider_count() > 1 ? 1 : 0;
}

} // namespace modules

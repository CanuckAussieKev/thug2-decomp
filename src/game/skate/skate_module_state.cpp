// The skate module's state and its small commands (see skate_module.h): the singleton, the module callbacks, the
// game type, the level asked for / opened / changed, the preferences, the script commands that drive them.

#include "game/skate/skate_module.h"
#include "engine/net/net_lists.h"
#include "game/rider/state_history.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "game/park_editing/park_editor.h"
#include "frontend/screen_element_manager.h"
#include "game/goals/goal_manager.h"
#include "engine/gfx/view_cam.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/player_info.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/entity.h"
#include "engine/objects/proximity.h"
#include "engine/objects/trick_objects.h"
#include "engine/audio/music_api.h"
#include "engine/script/preferences.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "engine/objects/node_components.h"
#include "engine/objects/rail_manager.h"
#include "game/rider/rider.h"
#include "engine/sound/surface_kind.h"
#include "platform/stopwatch.h"
#include "game/park_editing/rail_editor.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/viewport.h"
#include "engine/assets/resource_registry.h"
#include "engine/core/code_address.h"
#include "engine/gfx/engine.h"

namespace modules {

int g_skate_module_references;
float g_next_beep;

namespace {

constexpr uint32_t kInitLightManager = 0x7004771D;       // init_light_manager
constexpr uint32_t kLoadPermanentPrefiles = 0x7690764F;  // load_permanent_prefiles
constexpr uint32_t kNetworkDriverStartup = 0x6B1D08C3;   // network_driver_startup
constexpr uint32_t kInitLoadingBar = 0x36932FCA;         // init_loading_bar
constexpr uint32_t kStartupLoadingScreen = 0xAA37AEAE;   // startup_loading_screen
constexpr uint32_t kLoadPermSounds = 0x565D0F63;         // LoadPermSounds
constexpr uint32_t kLoadPermanentAssets = 0xAE754239;    // load_permanent_assets
constexpr uint32_t kInitEnvironment = 0x63BBA6AD;        // init_environment
constexpr uint32_t kDefaultSystemStartup = 0x738AB3D8;   // default_system_startup
constexpr uint32_t kSkater = 0x5B8AB877;                 // skater
constexpr uint32_t kLoadNothing = 0x3A8E4A31;            // (name unknown: the level that loads nothing)
constexpr uint32_t kLevel = 0x651533EC;                  // level
constexpr uint32_t kFromSettingsMarker = 0x547CB588;         // use_preferences
constexpr uint32_t kShowWarning = 0xD8E34EDA;            // show_warning
constexpr uint32_t kLoadSk5EdGameplay = 0xB664035D;      // load_sk5ed_gameplay
constexpr uint32_t kFreeskate2p = 0x5C8F5D66;            // freeskate2p
constexpr uint32_t kNetwork = 0x9F7B7843;                // network
constexpr uint32_t kTaunt = 0x4839873C;                  // taunt
constexpr uint32_t kSplitscreen = 0x06AB02F2;            // splitscreen
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;           // Load_MainMenu
constexpr uint32_t kLoadCas = 0xA368B4F1;                // load_cas
constexpr uint32_t kPrefs = 0xB58E6556;                  // prefs
constexpr uint32_t kAbsentInNetGames = 0x68910AC6;       // AbsentInNetGames

constexpr uint8_t kMsgParkMap = 0x6C;      // the park editor's map ("level data")
constexpr uint8_t kMsgParkRails = 0x88;    // its rails ("rail data")
constexpr uint8_t kMsgLevelSwitch = 0x46;  // the level changes (to the level)
constexpr uint8_t kMsgCreatedGoals = 0x87; // the created goals ("goals data")
constexpr uint8_t kMsgReadyQuery = 0x25;   // are you ready (a time stamp)
constexpr uint8_t kGroupGame = 8;

goals::MatchRules* get_game_mode() { return goals::g_game_mode; }
online::Hub* net_session_slot() { return online::g_online_hub; }
objects::ProfileRoster* roster_handle() { return objects::g_profile_roster; }
int& get_refs() { return g_skate_module_references; }
float& next_beep() { return g_next_beep; }

// A walk over the players (online::Hub::begin_player_walk / step_player_walk).
struct PlayerSearch {
    void** vtable_ = modules::kPlayerSearchVtable;
    void* list_link_ = nullptr;
};
// A walk over an app's connections (NetLink::AppHost::begin_conn_walk / step_conn_walk).
struct ConnSearch {
    void** vtable_ = net::kConnSearchVtable;
    void* list_link_ = nullptr;
};
online::PlayerRecord* as_player(void* player) { return static_cast<online::PlayerRecord*>(player); }

} // namespace

// 0x00564C10
void __cdecl Skate::discard_occurrence()
{
    if (static_cast<uint32_t>(get_refs()) > 1) {
        --get_refs();
        return;
    }
    if (Skate* skate = sole())
        reinterpret_cast<Skate*(__thiscall*)(Skate*, int)>(skate->vtable_[0])(skate, 1);
    g_skate_module = nullptr;
    get_refs() = 0;
}

// 0x005691A0
Skate* __cdecl Skate::spawn_occurrence(bool create)
{
    Skate* skate = sole();
    if (!skate && create) {
        void* memory = memory::alloc_zeroed(sizeof(Skate), nullptr, 1);
        if (memory) {
            skate = static_cast<Skate*>(memory)->make_new();
            int refs = get_refs() + 1;
            g_skate_module = skate;
            get_refs() = refs;
            return skate;
        }
        g_skate_module = nullptr;
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
        reinterpret_cast<void(__thiscall*)(objects::Rider*)>(skater->vtable_[0x2C / 4])(skater);
        skater->conceal(hide);
    } else {
        reinterpret_cast<void(__thiscall*)(objects::Rider*)>(skater->vtable_[0x30 / 4])(skater);
        skater->conceal(hide);
    }
}

// 0x00564CA0
void Skate::timer_beep(float time_left, float start_time, float rate, const char* script)
{
    if (!(time_left > 0.01f) || !(time_left <= start_time)) {
        next_beep() = start_time;
        return;
    }
    if (!(time_left < next_beep()))
        return;
    script::launch_script(script, nullptr, nullptr, false);
    double step = static_cast<double>(next_beep()) / rate;
    if (step < static_cast<double>(0.2f))
        step = static_cast<double>(0.2f);
    next_beep() = static_cast<float>(next_beep() - step);
}

// 0x00564D50
void Skate::set_unknown370(int value) { unknown370_ = value; }

// 0x00564D60
void Skate::advance_match_flow()
{
    if (own_script_)
        own_script_->tick_frame();
    game_flow_->tick_frame();
}

// 0x00564D80
prefs::Preferences* Skate::get_split_view_settings() { return splitscreen_preferences_; }

// 0x00564D90
goals::GoalManager* Skate::get_objective_board() { return goal_manager_; }

// 0x00564DA0
void Skate::scan_pad_connections() { watch_for_unplug_ = true; }

// 0x00564DB0 (tasks::Task<Skate>'s vtable slot 0): its code run with it.
void __fastcall Skate::run_task(tasks::SchedulerTask* task)
{
    auto* code = *reinterpret_cast<void(__cdecl**)(tasks::SchedulerTask*)>(reinterpret_cast<uint8_t*>(task) + 0x18);
    code(task);
}

// 0x00564DC0
void Skate::start_callback()
{
    surface_kind::load_terrain_types();
    script::launch_script(kInitLightManager, nullptr, nullptr, false, nullptr);
    script::launch_script(kLoadPermanentPrefiles, nullptr, nullptr, false, nullptr);
    unknown3d0_ = 0;
    auto* main_loop = reinterpret_cast<uint8_t*>(assets::g_main_loop_manager);
    objects::nearby_zone_startup();
    objects::emitter_init();
    update_job_->skip_bits_ = 2;
    reinterpret_cast<tasks::PileUp*>(main_loop + 4)->queue_job(update_job_); // the logic tasks
    get_game_mode()->get_reset();
    script::launch_script(kNetworkDriverStartup, nullptr, nullptr, false, nullptr);
    script::launch_script(kInitLoadingBar, nullptr, nullptr, false, nullptr);
    script::launch_script(kStartupLoadingScreen, nullptr, nullptr, false, nullptr);
    Sleep(2000);
    if (!audio::music_off_requested() || !audio::streams_off())
        audio::prepare_all();
    script::launch_script(kLoadPermSounds, nullptr, nullptr, false, nullptr);
    memory::enter_heap_scope(memory::bottom_up_heap());
    memory::push_mem_profile("Permanent Assets"); // "Permanent Assets"
    script::launch_script(kLoadPermanentAssets, nullptr, nullptr, false, nullptr);
    memory::pop_mem_profile();
    memory::leave_heap_scope();
    roster_handle()->prepare_all();
    checksums_->get_fill();
    script::launch_script(kInitEnvironment, nullptr, nullptr, false, nullptr);
    script::launch_script(kDefaultSystemStartup, nullptr, nullptr, false, nullptr);
}

// 0x00564F20
void Skate::tidy_before_park_build()
{
    objects::g_trick_spot_hub->remove_all_clusters();
    render::engine::kill_all_texture_splats();
    static_cast<render::FlexibleParticleManager*>(render::g_flexible_particle_manager)->destroy_non_persistent();
    rail_manager_->tidy_up();
    objects::nearby_zone_shutdown();
    objects::emitter_cleanup();
}

// 0x00564F60
void Skate::get_freeze(bool paused)
{
    if (paused)
        sound::g_audio_mixer->update_voice_volumes();
}

// 0x00564F80
objects::RiderIdentity* Skate::fetch_account(script::Struct* params)
{
    int skater;
    if (params && params->fetch_int_value(kSkater, &skater, 0))
        return roster_handle()->fetch_account(skater);
    return roster_handle()->fetch_active_account();
}

// 0x00564FC0
objects::RiderIdentity* Skate::fetch_account(int index) { return roster_handle()->fetch_account(index); }

// 0x00564FD0
void Skate::set_current_game_type()
{
    debug::printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<Changing Game Mode\n"); // "<<<...Changing Game Mode\n"
    get_game_mode()->apply_match_style(match_style_);
}

// 0x00565000
void Skate::enter_stage(uint32_t level)
{
    online::Hub* net = net_session_slot();
    memory::push_mem_profile("LaunchLevel"); // "LaunchLevel"
    if (net->in_net_game() && net->get_on_server()) {
        PlayerSearch search;
        for (void* player = net->begin_player_walk(&search, true); player; player = net->step_player_walk(&search, true))
            as_player(player)->await_ready_response(0);
    }
    if (level != kLoadNothing)
        script::launch_script(level, nullptr, nullptr, false, nullptr);
    level_launched_ = true;
    level_ = level;
    if (net->in_net_game()) {
        for (int i = 0; i < 2; ++i) {
            ConnSearch search;
            if (net::AppHost* client = net->fetch_patron(i)) {
                if (net::Conn* conn = client->begin_conn_walk(reinterpret_cast<net::ConnSearch*>(&search)))
                    conn->stamp_last_heard(12000);
            }
        }
        if (net->get_on_server()) {
            ConnSearch search;
            if (net::AppHost* server = net->host_) { // (0x005480C0)
                for (net::Conn* conn = server->begin_conn_walk(reinterpret_cast<net::ConnSearch*>(&search)); conn;
                     conn = server->step_conn_walk(reinterpret_cast<net::ConnSearch*>(&search)))
                    conn->stamp_last_heard(8000);
            }
        } else {
            struct NewPlayerSearch {
                void** vtable_ = online::kNewPlayerSearchVtable;
                void* list_link_ = nullptr;
            } search;
            if (!net->begin_joiner_walk(&search))
                net->send_ready_response();
        }
    }
    memory::pop_mem_profile();
}

// 0x005651D0
void __cdecl Skate::observe_next_skater()
{
    online::Hub* net = net_session_slot();
    net->spectate_player(static_cast<uint8_t*>(net->next_cam_player()));
}

// 0x005651F0
int __cdecl Skate::is_multiplayer()
{
    if (net_session_slot()->in_net_game())
        return 1;
    return get_game_mode()->get_starting_rider_count() > 1 ? 1 : 0;
}

// 0x00565220
bool __cdecl Skate::cfunc_set_game_type(script::Struct* params, script::Script*)
{
    Skate* skate = sole();
    uint32_t type;
    params->read_hash_value(0u, &type, true);
    skate->match_style_ = type;
    return true;
}

// 0x00565250
int __cdecl Skate::cfunc_in_team_game(script::Struct*, script::Script*) { return get_game_mode()->is_squad_match(); }

// 0x00565260
bool __cdecl Skate::cfunc_change_level(script::Struct* params, script::Script*)
{
    debug::printf("ScriptChangeLevel\n"); // "ScriptChangeLevel\n"
    online::Hub* net = net_session_slot();
    uint32_t level;
    params->read_hash_value(kLevel, &level, 1);
    if (level == kFromSettingsMarker)
        level = net->get_level_preference();
    net->change_arena(level);
    bool show_warning = params->has_entry_named(kShowWarning);
    audio::stop_music();
    PlayerSearch search;
    net::AppHost* server = net->host_; // (0x005480C0)
    struct ChangeLevelMessage {
        uint32_t level;
        bool show_warning_;
        uint8_t pad[3];
    } message;
    message.level = level;
    message.show_warning_ = show_warning;
    for (void* player = net->begin_player_walk(&search, true); player; player = net->step_player_walk(&search, true)) {
        online::PlayerRecord* info = as_player(player);
        net::NetMessageInfo desc;
        desc.make_new();
        uint32_t now = frame_clock::get_time();
        if (!static_cast<uint8_t>(info->is_own_player()) && level == kLoadSk5EdGameplay) {
            void* map = park_editing::g_park_editor->get_packed_park_data(false);
            int handle = info->get_link_handle();
            server->send_bulk_message(handle, kMsgParkMap, 15000, map, "level data", kGroupGame, false,
                                  true); // "level data"
            park_editing::RailAuthoring::sole(); // (the rail editor; this first answer unused, as the original)
            void* rails = park_editing::RailAuthoring::sole()->get_packed();
            handle = info->get_link_handle();
            server->send_bulk_message(handle, kMsgParkRails, 0x1132, rails, "rail data", kGroupGame, false,
                                  true); // "rail data"
        }
        desc.content_ = &message;
        desc.byte_count_ = sizeof(message);
        desc.ident_ = kMsgLevelSwitch;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        server->put_on_outbox(info->get_link_handle(), &desc);
        if (!static_cast<uint8_t>(info->is_own_player()) && net->has_created_goals()) {
            net->prepare_created_goals(level);
            void* goals = net->get_created_goals_message();
            uint32_t size = net->get_created_goals_size();
            server->send_bulk_message(info->get_link_handle(), kMsgCreatedGoals, static_cast<uint16_t>(size), goals,
                                  "goals data", kGroupGame, false, true); // "goals data"
        }
        desc.content_ = &now;
        desc.byte_count_ = 4;
        desc.ident_ = kMsgReadyQuery;
        server->put_on_outbox(info->get_link_handle(), &desc);
        info->set_ready_query_owed(now);
    }
    if (!net->in_net_game())
        server->transmit(false);
    return true;
}

// 0x005654B0
bool __cdecl Skate::cfunc_launch_level(script::Struct*, script::Script*)
{
    Skate* skate = sole();
    skate->enter_stage(skate->pending_level_);
    return true;
}

// 0x005654D0
bool __cdecl Skate::cfunc_request_level(script::Struct* params, script::Script*)
{
    online::Hub* net = net_session_slot();
    Skate* skate = sole();
    uint32_t level;
    if (!params->read_hash_value(0u, &level, false)) {
        const char* name;
        if (params->fetch_text(0u, &name, 1)) {
            char script[0x80];
            std::snprintf(script, sizeof(script), "load_%s", name); // "load_%s"
            level = checksums::hash_from_text(script);
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
bool __cdecl Skate::cfunc_init_skater_heaps(script::Struct*, script::Script*)
{
    memory::enter_heap_scope(memory::bottom_up_heap());
    int skaters;
    if (sole()->match_style_ == kFreeskate2p) {
        skaters = 2;
    } else {
        skaters = net_session_slot()->get_player_limit();
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
    if (g_viewport_count <= 1 && render::fetch_live_camera(0)) {
        no_camera_ = false;
        viewer_pos_ = *render::fetch_live_camera(0)->fetch_location();
        return;
    }
    no_camera_ = true;
}

// 0x00565680
prefs::Preferences* __cdecl Skate::get_player_options(uint32_t name)
{
    if (name == kNetwork)
        return net_session_slot()->get_online_settings();
    if (name == kTaunt)
        return net_session_slot()->get_taunt_settings();
    if (name == kSplitscreen)
        return sole()->splitscreen_preferences_;
    return nullptr;
}

// 0x00565D40
prefs::Preferences* __cdecl Skate::get_preferences_from_params(script::Struct* params)
{
    uint32_t name;
    params->read_hash_value(kPrefs, &name, true);
    return get_player_options(name); // (inlined there)
}

// 0x005656C0
void* Skate::fetch_gap_list() { return career_->fetch_gap_list(-1); }

// 0x005656D0
int Skate::in_net_game_level()
{
    if (net_session_slot()->in_net_game() && level_ != kLoadMainMenu)
        return 1;
    return 0;
}

// 0x00565700
bool Skate::in_front_end_or_net_mode()
{
    if (static_cast<uint8_t>(net_session_slot()->is_network_game()))
        return true;
    return pending_level_ == kLoadMainMenu || pending_level_ == kLoadCas;
}

// 0x00565BB0
int __stdcall Skate::should_be_absent(script::Struct* params)
{
    if (!params->has_bare_flag(kAbsentInNetGames))
        return 0;
    if (net_session_slot()->in_net_game())
        return 1;
    return get_game_mode()->get_starting_rider_count() > 1 ? 1 : 0;
}

// 0x00564D60's neighbours: the skater list's node and head (lists::Elem<Rider> 0x0064E544, lists::Foremost<Rider>
// 0x0064E5D4).
// 0x00565730: the head's destructor body (the node's).
void __fastcall skater_head_destroy_body(memory::ListNode* head) { head->unlink_as(kSkaterNodeVtable); }

static VtableSlot g_player_search_vtable_slots[] = {&objects::PlayerSearchDestructors::teardown};
void** const kPlayerSearchVtable = reinterpret_cast<void**>(g_player_search_vtable_slots);
static VtableSlot g_skate_vtable_slots[] = {&Skate::teardown, &Skate::start_callback, &Skate::stop_callback};
void** const kSkateVtable = reinterpret_cast<void**>(g_skate_vtable_slots);
static VtableSlot g_skater_node_vtable_slots[] = {Skate::destroy_skater_node};
void** const kSkaterNodeVtable = reinterpret_cast<void**>(g_skater_node_vtable_slots);
static VtableSlot g_skater_head_vtable_slots[] = {Skate::destroy_skater_head};
void** const kSkaterHeadVtable = reinterpret_cast<void**>(g_skater_head_vtable_slots);

} // namespace Mdl

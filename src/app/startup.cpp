// The game's main.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "app/startup.h"
#include "app/extensions.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "engine/assets/resource_registry.h"
#include "base/core/checked_index.h"
#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/memory_sizes.h"
#include "engine/memory/slab_bank.h"
#include "base/core/stubs.h"
#include "engine/memory/task_list.h"
#include "game/park_editing/park_editor.h"
#include "engine/file/async.h"
#include "engine/file/archive.h"
#include "game/front_end/front_end.h"
#include "engine/ui/screen_element_manager.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/tod_manager.h"
#include "game/save_cards/memcard.h"
#include "base/maths/maths.h"
#include "base/maths/random.h"
#include "game/online/gamenet_manager.h"
#include "engine/net/net_manager.h"
#include "app/component_types.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/path_manager.h"
#include "engine/objects/tracker.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cache.h"
#include "engine/script/script_startup.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/pad_input/device.h"
#include "engine/pad_input/input_hub.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "game/skate/highscores.h"
#include "game/skate/skate_module.h"
#include "engine/pad_input/key_names.h"
#include "engine/sys/mlp.h"
#include "engine/sys/game_module.h"
#include "app/pc_setup.h"
#include "engine/sys/stopwatch.h"
#include "engine/script/node_manager.h"
#include "engine/sys/game_config.h"
#include "engine/objects/collide_and_die.h"
#include "engine/gfx/engine_parts.h"
#include "game/online/net_game_state.h"
#include "base/core/language.h"

namespace sys {

namespace {
constexpr int kInitialRandomSeed = 0x06680BF3;
constexpr int kIgnoredHeapArgument = 0x10;
constexpr uint32_t kTextPoolSize = 350; // the engine's text pool

// Compiled-out debug code at the start of main (a call to the empty 0x005480A0 with 0).
inline void debug_set_up(int /*unused*/) {}

// The main loop's task manager (frame_loop::Hub): three task lists, at +0x04, +0x40 and +0x7C.
void queue_job(tasks::PileUp& list, void* task) { list.queue_job(static_cast<tasks::SchedulerTask*>(task)); }
void clear_job_list(tasks::PileUp& list) { list.clear_job_list(); }
void get_main_loop(frame_loop::Hub* mlp) { mlp->get_main_loop(); }

// The module manager (modules::Hub, 0x0044F6F0).
void enrol_unit(modules::Hub* mdl, modules::PartUnit* module) { mdl->enrol_unit(module); }
void launch_module(modules::Hub* mdl, modules::PartUnit* module) { mdl->launch_module(module); }
void halt_every_module(modules::Hub* mdl) { mdl->halt_every_module(); }
void remove_module(modules::Hub* /*mdl*/, modules::PartUnit* module) { modules::Hub::remove_module(module); }

// The bottom-up heap's own context pushed / popped (its enter_heap_scope / leave_heap_scope).
void push_bottom_up_heap() { memory::Hub::get_pull()->bottom_up_heap()->enter_heap_scope(); }
void pop_bottom_up_heap() { memory::Hub::get_pull()->bottom_up_heap()->leave_heap_scope(); }

bool& demo_mode()
{
    static bool demo_mode{};
    return demo_mode;
}
} // namespace

// 0x004E1EF0
int game_main(int argc, char** argv)
{
    debug_set_up(0);
    const game_config::Arguments args(argv, static_cast<size_t>(argc));
    game_config::prepare_all(args); // the command line options (Config)
    memory::Hub::get_pull()->init_standard_heaps();
    switch (game_config::get_language()) { // the language's start-up (German has its own)
    case 3:                            // French
        set_key_names();
        break;
    case core::kLanguageGerman: // German
        set_german_key_names();
        break;
    default:
        set_key_names();
        break;
    }
    debug::printf("\n Start of main()\n");
    debug::printf("argc = %d\n", argc);
    for (size_t i = 0; i < args.size(); ++i) {
        debug::printf("%2d: %s\n", static_cast<int>(i), core::at(args, i));
    }
    debug::printf("\n");
    memory::push_mem_profile("Everything");
    memory::push_mem_profile("Initialization");
    if (argc == 2 && std::strcmp(core::at(args, 1), "demo") == 0) {
        demo_mode() = true; // demo mode
    }
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    maths::seed_random(kInitialRandomSeed);
    maths::fill_sine_table();

    while (!render::engine_parts().quit_requested()) {
        push_bottom_up_heap();
        stubs::ignore_argument(kIgnoredHeapArgument); // (a memory manager method, compiled down to the shared stub)

        memory::push_mem_profile("Symbol Hash Table");
        script::create_symbol_table();
        memory::pop_mem_profile();
        memory::push_mem_profile("Hash Item Pool Manager");
        // Not original - the HashItem pool's size is ours (engine/memory/memory_sizes.h: pool sizes as THUGPRO's;
        // the main block grows to match). Original behaviour: 10000 items.
        memory::slot_pool_setup::set_up({.index = 0, .count = memory::sizes::at_hand().hash_items_});
        memory::pop_mem_profile();
        pre::create_manager(true);
        frame_clock::prepare_all();
        memory::push_mem_profile("File System");
        file::async_init(); // (passed "DATA", unused)
        memory::pop_mem_profile();

        memory::push_mem_profile("System Singletons");
        objects::create_tracker(true);
        frame_loop::Hub* mlp = frame_loop::create_manager(true);
        modules::Hub* mdl = modules::create_manager(true);
        const sio::Hub* sio = sio::create_manager(true);
        save_cards::create_manager(true);
        const pad_input::Hub* input = pad_input::create_manager(true);
        file::AsyncLoaderModule* async_loader = file::create_async_loader_module(true);
        memory::pop_mem_profile();
        pad_input::get_input_manager()->change_pad_follows_stick(true); // (+0x10C)
        script::allocate_pools();
        // Not original - the extensions' script commands and our game features' (app/extensions.h: the level
        // editor's LevelEditor_Start..., CREATE 100% CAS's Career100_Apply..., TeleportSkaterStanding, ThawBmx_...,
        // the new front end's pause items) registered after the game's own.
        // Original behaviour: only the game's table (0x0067F3A0) and its member functions are registered.
        app::register_added_script_commands();
        script::load_boot_script_files();

        memory::push_mem_profile("Game Singletons");
        objects::PathManager::sole();
        script::ScriptStore* script_cache = script::create_script_cache(true);
        assets::ResourceRegistry* assets = assets::create_asset_manager(true);
        sound::create_manager(true);
        net::create_manager(true);
        objects::create_composite_object_manager(true);
        objects::register_component_types();
        objects::acquire_trick_object_manager(true);
        goals::MatchRules::sole(true);
        objects::ProfileRoster::fabricate(true);
        highscores::BestResults::fabricate(true);
        modules::Skate* skate_module = modules::Skate::spawn_occurrence(true);
        modules::FrontEnd* front_end = modules::create_front_end(true);
        online::Hub::fabricate(true);
        objects::set_net_hub(online::net_game_state().online_hub()); // (the network manager)
        park_editing::ParkBuilderMode* park_editor = park_editing::ParkBuilderMode::fabricate(true);
        frontend::create_screen_element_manager(true);
        level_nodes::create_node_manager(true);
        gfx::create_tod_manager(true);
        memory::pop_mem_profile();

        memory::push_mem_profile("Resgistering and starting modules");
        queue_job(mlp->system_tasks(), mdl->task().get());
        queue_job(mlp->system_tasks(), sio->task().get());
        queue_job(mlp->logic_tasks(), input->task().get());
        enrol_unit(mdl, script_cache);
        enrol_unit(mdl, park_editor);
        enrol_unit(mdl, front_end);
        enrol_unit(mdl, skate_module);
        enrol_unit(mdl, async_loader);
        enrol_unit(mdl, assets);
        launch_module(mdl, script_cache);
        launch_module(mdl, front_end);
        launch_module(mdl, skate_module);
        launch_module(mdl, park_editor);
        launch_module(mdl, async_loader);
        launch_module(mdl, assets);
        memory::pop_mem_profile();
        memory::push_mem_profile("sStartEngine");
        render::engine::prepare_all(kTextPoolSize); // start the engine
        memory::pop_mem_profile();
        memory::pop_mem_profile(); // (Initialization)

        memory::push_mem_profile("MainLoop");
        get_main_loop(mlp);
        // Originally from PartyMod Fixes: the process ends here (fast_exit, in place of the original's PopMemProfile() - PartyMod's call
        // at 0x004E2492). The
        // original went on to tear everything down and, unless quitting, start again from the top.
        fast_exit();

        halt_every_module(mdl);
        remove_module(mdl, skate_module); // (the script cache isn't unregistered)
        remove_module(mdl, front_end);
        remove_module(mdl, park_editor);
        remove_module(mdl, async_loader);
        remove_module(mdl, assets);
        clear_job_list(mlp->display_tasks());
        clear_job_list(mlp->logic_tasks());
        clear_job_list(mlp->system_tasks());
        gfx::release_tod_manager();
        level_nodes::release_node_manager();
        frontend::delete_screen_element_manager();
        park_editing::ParkBuilderMode::get_release();
        online::release_manager();
        modules::release_front_end();
        modules::Skate::discard_occurrence();
        highscores::BestResults::get_release();
        objects::ProfileRoster::get_release();
        goals::MatchRules::get_release();
        objects::release_trick_object_manager();
        objects::release_composite_object_manager();
        net::release_manager();
        sound::release_manager();
        assets::release_asset_manager();
        script::release_script_cache();
        file::destroy_async_loader_module();
        pad_input::release_manager();
        save_cards::release_manager();
        sio::release_manager();
        modules::release_manager();
        frame_loop::release_manager();
        objects::release_tracker();
        pre::destroy_manager();
        memory::pop_mem_profile(); // (Everything)
        stubs::ignore_argument(kIgnoredHeapArgument);
        pop_bottom_up_heap();
    }
    return 0;
}

} // namespace sys

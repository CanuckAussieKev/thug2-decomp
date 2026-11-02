// Script commands (CFunctions) of the skate game, 0x0059FE50-0x005A0880 and 0x005A3B00-0x005A3B75: cleaning up
// between levels, object flags and exceptions by name or node prefix, skater scripts, pausing, the clock, the
// screen flash (see game_cfuncs.h).

#include "engine/script/game_cfuncs.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <span>
#include <string_view>
#include <array>

#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_lists.h"
#include "game/skate/career.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/stubs.h"
#include "game/park_editing/park_editor.h"
#include "engine/file/packs.h"
#include "frontend/front_end.h"
#include "engine/gfx/tod_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/path_manager.h"
#include "engine/script/cfuncs_scene.h"
#include "engine/script/cscript.h"
#include "engine/script/node_prefixes.h"
#include "engine/script/skate_cfuncs.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/audio/continue_music.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "engine/script/script_startup.h"
#include "game/goals/goal_manager.h"
#include "engine/gfx/engine_imposters.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/anim_cache.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/scene_lights.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/objects/anim_movies.h"
#include "engine/script/debug_cfuncs.h"
#include "engine/objects/movie_manager.h"
#include "engine/gfx/engine.h"
#include "formats/byte_stream.h"
#include "wire/play_messages.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "engine/core/format.h"
#include "engine/script/shared_state.h"
#include "engine/core/no_destroy.h"

namespace commands {

GameFlowCfuncsShared& game_flow_cfuncs_shared()
{
    static core::NoDestroy<GameFlowCfuncsShared> state;
    return state.get();
}

namespace {

constexpr uint32_t kParams = 0x7031F10C;           // "params"
constexpr uint32_t kSkater = 0x5B8AB877;           // "skater"
constexpr uint32_t kName = 0xA1DC81F9;             // "name"
constexpr uint32_t kPrefix = 0x6C4E7971;           // "prefix"
constexpr uint32_t kSkaterType = 0xBAF9B341;       // (name unknown) the skaters' object type
constexpr uint32_t kKeepSpawnerFile = 0x01917DF2;  // (name unknown) don't note the spawner's .qb file (as SpawnScript)
constexpr uint32_t kPausedAnswerKey = 0x3BA1FDB9;  // (name unknown) GameIsPaused's answer
constexpr uint32_t kFlagCommandFlagA = 0x2E7D5EE7; // (names unknown) flags the object flag commands look for and
constexpr uint32_t kFlagCommandFlagB = 0xBCEB479A; // ignore
constexpr uint32_t kCleanupScript1 = 0xFF958F9D;   // (names unknown) the scripts Cleanup runs
constexpr uint32_t kCleanupScript2 = 0xDCA9A9A0;
constexpr uint32_t kQbSymbol = 0xC472ECC5;         // (name unknown) the global whose value names a .qb file to unload
constexpr uint32_t kFlashTextureFlag = 0x70FFC649; // (names unknown) DoFlash's flags: 1, 2 or else 4, 8
constexpr uint32_t kFlashFlag2 = 0x19C43CF6;
constexpr uint32_t kFlashFlag4 = 0x387C9ED6;
constexpr uint32_t kFlashFlag8 = 0xDF6436D2;

// The skate module (modules::Skate, 0x007CE478) and its code we call through the game.

modules::Skate* skate_module() { return modules::Skate::sole(); }
objects::Rider* get_rider_at(int index) { return core::down_cast<objects::Rider>(skate_module()->get_rider_at(index)); }
objects::Rider* get_home_rider() { return skate_module()->get_home_rider(); }

// The object manager (0x006F0FB8).
objects::EntityHub* object_edit() { return objects::object_hub_slot().get(); }
// The game mode (0x007CE470) and two of its values (0x00553170 / 0x00553190: its integers 0x8D38A280 / 0x6DC3D6F1).
void* get_game_mode() { return static_cast<void*>(goals::game_mode()); }
goals::MatchRules* game_mode_object() { return static_cast<goals::MatchRules*>(get_game_mode()); }
int game_mode_time_limit() { return game_mode_object()->get_time_limit(); }
online::Hub* online_hub_handle() { return online::online_hub(); }
memory::Hub* mem_manager() { return memory::Hub::get_pull(); }

// Two argument-less calls of Cleanup's to the empty function 0x005480A0 (compiled-out debug code; what it did is
// unknown).
inline void compiled_out_debug_call() {}

// A .qb file's scripts unloaded and the global 0xC472ECC5 cleared (0x005A59B0).
void unload_qb_file(uint32_t file) { script::unload_qb_file(file); }
// The level's .qb files to unload with it (0x00696900, 15).
std::array<uint32_t, 15>& level_qb_files() { return debug_cfuncs_shared().level_qb_files_; }
constexpr int kLevelQbFiles = 15;

// The object's part of a manager walk's command (what 0x005A0120 is given).
struct FlagCommandData {
    uint32_t value; // a flag's number, a mask, or an exception
    int action;     // FlagAction
    bool result;
    bool found;
};

// What a manager walk does to the objects of a name: their id, and the command.
struct FlagCommand {
    uint32_t id;
    FlagCommandData data;
};

} // namespace

// 0x0059FE50: Cleanup [preserve_skaters]: everything of the level unloaded - the skaters' state, the time of day's
// dead objects, the park editor, two scripts (with the calling script's kScriptFlagSpawned off meanwhile), the
// skate module's objects, the asset tables, the level's .qb files, the geometry, without preserve_skaters the extra
// skater heaps, the unused .pre files, the paths, the network heaps (rebuilt after, as the network says); the
// physical memory logged. Always true.
bool cfunc_cleanup(script::Struct* params, script::Script* script)
{
    const audio::continue_music::Hold keep_music; // NOT ORIGINAL - continue_music: the sound sweep leaves the song
    stubs::do_nothing(); // 0x0048C1B0
    goals::GoalManager* skater_info = goals::get_objective_board();
    skater_info->disengage_every_objective(false);
    skater_info->wipe_objectives();
    gfx::tod_manager()->clear_dead_objects();
    park_editing::ParkBuilderMode::sole()->get_leave();
    compiled_out_debug_call();
    bool preserve_skaters = false;
    if (params)
        preserve_skaters = params->has_bare_flag("preserve_skaters"); // "preserve_skaters"
    uint8_t spawned = 0;
    if (script) {
        spawned = script->spawn_flags_ & script::kScriptFlagSpawned;
        script->spawn_flags_ &= ~script::kScriptFlagSpawned;
    }
    assets::ResourceRegistry* assets = assets::asset_manager();
    script::launch_script(kCleanupScript1, nullptr, nullptr, false, nullptr);
    script::launch_script(kCleanupScript2, nullptr, nullptr, false, nullptr);
    skate_module()->tidy_up(); // the skate module's objects
    if (script)
        script->spawn_flags_ = static_cast<uint8_t>((script->spawn_flags_ & ~script::kScriptFlagSpawned) | spawned);
    render::g_imposter_manager->delete_all();
    render::engine::kill_all_effects();
    gfx::ClipStore* particles = gfx::ClipStore::get_pull(); // (0x006F92B0)
    particles->drain(false);
    render::engine::enable_fog(false);
    script::delete_finished_scripts();
    compiled_out_debug_call();
    render::uninit_light_manager();
    sound::get_audio_mixer()->sweep_up();
    assets->unload_all_assets_of_type(false, 9);
    debug::printf("unloading assman tables ...........\n"); // "unloading assman tables ...........\n"
    assets->unload_all_assets(false);
    debug::printf("done unloading assman ...........\n"); // "done unloading assman ...........\n"
    if (const script::SymbolTableEntry* qb = script::find_symbol(kQbSymbol))
        unload_qb_file(qb->unknown8);
    for (int i = 0; i < kLevelQbFiles; ++i) {
        unload_qb_file(core::at(level_qb_files(), i));
        core::at(level_qb_files(), i) = 0;
    }
    debug::printf(
        "Starting to unload Geometry from the engine .....\n"); // "Starting to unload Geometry from the engine .....\n"
    cfunc_unload_all_level_geometry(nullptr, nullptr);
    debug::printf(
        "Done unloading Geometry from the engine .....\n"); // "Done unloading Geometry from the engine .....\n"
    if (!preserve_skaters) {
        debug::printf("Clean up without preserve skaters\n"); // "Clean up without preserve skaters\n"
        mem_manager()->delete_extra_skater_heaps();
    }
    // the .pre files nothing uses any more
    for (const char* pre = packs::take_next_finished_archive(nullptr); pre;) {
        const char* next = packs::take_next_finished_archive(pre);
        if (!packs::archive_is_busy(pre))
            packs::unload_pre(pre);
        pre = next;
    }
    render::purge_doomed_cameras();
    render::clear_scene_light_queue();
    render::drop_particle_textures(false);
    objects::PathManager::sole()->dealloc();
    if (!skate_module()->in_net_game_level())
        mem_manager()->release_net_scratch_zone();
    objects::destroy_coim_heap();
    MEMORYSTATUS memory;
    GlobalMemoryStatus(&memory);
    std::array<char, 0x400> text{};
    // (the original: wsprintfA, its integers as printf's)
    const size_t length = text::format_to(text, "%4d total kb of physical memory.\n", memory.dwTotalPhys >> 10);
    text::format_to(std::span(text).subspan(length), "%4d  free kb of physical memory.\n", memory.dwAvailPhys >> 10);
    OutputDebugStringA(text.data());
    if (skate_module()->in_net_game_level())
        mem_manager()->create_net_scratch_zone();
    if (skate_module()->in_front_end_or_net_mode())
        mem_manager()->init_internet_heaps();
    return true;
}

namespace {

// 0x005A0120: what FlagCommand asks, done to `object`: 0 sets flag `value` (of the 32 script flags), 1 clears it,
// 2 tests it, 3 / 4 set / clear the bits `value` of the object flags, 5 sends the object the exception `value`.
// The result is true for 0, 1, 5 and a set flag (2); 3 and 4 leave it.
void apply_flag_command(FlagCommandData* data, objects::Entity* object)
{
    data->found = true;
    switch (data->action) {
    case 0:
        object->flag_bits_ |= 1u << (data->value & 31);
        data->result = true;
        break;
    case 1:
        object->flag_bits_ &= ~(1u << (data->value & 31));
        data->result = true;
        break;
    case 2:
        if (object->flag_bits_ & (1u << (data->value & 31)))
            data->result = true;
        break;
    case 3:
        object->object_flags_ |= data->value;
        break;
    case 4:
        object->object_flags_ &= ~data->value;
        break;
    case 5:
        object->post_own_event(data->value, nullptr);
        data->result = true;
        break;
    default:
        break;
    }
}

} // namespace

// 0x005A01B0
void apply_flag_command_to_object(objects::Entity* object, void* data)
{
    auto* command = static_cast<FlagCommand*>(data);
    if (object->ident_ == command->id)
        apply_flag_command(&command->data, object);
}

// 0x005A01D0
bool flag_command_by_name(script::Struct* params, script::Script*, int action)
{
    uint32_t value = 0;
    if (action == kFlagActionException)
        params->read_hash_value(0u, &value, 0);
    else
        params->fetch_int_value(0u, &value, 0);
    // (two flags looked for and not used)
    if (!params->has_bare_flag(kFlagCommandFlagA))
        params->has_bare_flag(kFlagCommandFlagB);
    FlagCommand command{};
    uint32_t id = 0;
    if (params->read_hash_value(kName, &id, 0)) {
        command.data.value = value;
        command.id = id;
        command.data.action = action;
        command.data.found = false;
        command.data.result = false;
        object_edit()->for_each(&apply_flag_command_to_object, &command);
        return command.data.result;
    }
    const char* prefix = nullptr;
    if (!params->fetch_text(kPrefix, &prefix, 0))
        return false;
    const std::span<const uint32_t> nodes =
        level_nodes::collect_nodes_by_prefix(prefix); // (the original passes a third argument, 1)
    command.data.value = value;
    command.data.action = action;
    command.data.found = false;
    command.data.result = false;
    for (const uint32_t node : nodes) {
        command.id = node;
        object_edit()->for_each(&apply_flag_command_to_object, &command);
    }
    return command.data.result;
}

// 0x005A0300: MakeSkaterGoto <script> [params={...}] [skater=n]: the skater switches to the script - the calling
// script's own skater if it runs on one, else skater n, else the local skater; only a local skater (+0xBC).
bool cfunc_make_skater_goto(script::Struct* params, script::Script* script)
{
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    objects::Entity* object = script ? script->entity_.get_pull() : nullptr;
    objects::Rider* skater = nullptr;
    if (object && object->kind_tag_ == kSkaterType) {
        skater = core::down_cast<objects::Rider>(object);
    } else {
        int index = 0;
        if (params->fetch_int_value(kSkater, &index, 0))
            skater = get_rider_at(index);
        else
            skater = get_home_rider();
    }
    // Not original - THAW's BMX (game/rider/thaw_bmx.h skater_script): on the bike THAW's PedKnockDown takes its bike
    // branch. Original behaviour: the skater goes to `name` as given.
    if (skater && skater->is_local_skater_)
        name = skater::thaw_bmx::skater_script(skater, name, script);
    if (skater && skater->is_local_skater_ && name)
        skater->switch_to_script(name, script_params);
    return true;
}

// 0x005A03C0: MakeSkaterGosub <script> [params={...}] [skater=n (0)]: the skater (the calling script's own, else
// skater n) runs the script as a call.
bool cfunc_make_skater_gosub(script::Struct* params, script::Script* script)
{
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    objects::Entity* object = script ? script->entity_.get_pull() : nullptr;
    if (!object || object->kind_tag_ != kSkaterType) {
        int index = 0;
        params->fetch_int_value(kSkater, &index, 0);
        object = get_rider_at(index);
    }
    if (object)
        object->run_sub_script(name, script_params);
    return true;
}

// 0x005A0460: SpawnSound <script> [Id=id] [Params={...}]: the script spawned on the calling script's object (its
// id the calling script's spawn id unless given), remembering the caller's .qb file unless 0x01917DF2 is given. A
// script without an object: SpawnScript.
bool cfunc_spawn_sound(script::Struct* params, script::Script* script)
{
    const objects::Entity* object = script->entity_.get_pull();
    if (!object)
        return cfunc_spawn_script(params, script);
    uint32_t name = 0;
    if (params->read_hash_value(0u, &name, 0)) {
        uint32_t id = script::get_spawn_id(script);
        params->read_hash_value("Id", &id, 0); // "Id"
        script::Struct* script_params = nullptr;
        params->read_sub_struct("Params", &script_params, 0); // "Params"
        script::Script* spawned = script->entity_.get_pull()->spawn_script(name, script_params, id, false);
        if (!params->has_bare_flag(kKeepSpawnerFile))
            spawned->set_spawner_file(script);
    }
    return true;
}

// 0x005A0520: PauseSkater <n> [hide]: skater n paused (vtable +0x2C) and hidden or shown.
bool cfunc_pause_skater(script::Struct* params, script::Script*)
{
    const bool hide = params->has_bare_flag("hide"); // "hide"
    int index = 0;                             // (unset in the original when missing - an asserted parameter)
    params->fetch_int_value(0u, &index, true);
    if (objects::Rider* skater = get_rider_at(index)) {
        skater->get_freeze(); // (the original's vtable +0x2C)
        skater->conceal(hide);
    }
    return true;
}

// 0x005A0580: UnPauseSkater <n>: skater n unpaused (vtable +0x30) and shown.
bool cfunc_un_pause_skater(script::Struct* params, script::Script*)
{
    int index = 0; // (unset in the original when missing - an asserted parameter)
    params->fetch_int_value(0u, &index, true);
    if (objects::Rider* skater = get_rider_at(index)) {
        skater->unpause(); // (the original's vtable +0x30)
        skater->conceal(false);
    }
    return true;
}

// 0x005A05C0: PauseGame: the game paused, the calling script carrying on.
bool cfunc_pause_game(script::Struct*, script::Script* script)
{
    modules::front_end_instance()->get_pause_game(true);
    script::resume_child_script(script);
    return true;
}

// 0x005A05E0: UnPauseGame.
bool cfunc_un_pause_game(script::Struct*, script::Script*)
{
    modules::front_end_instance()->get_pause_game(false);
    return true;
}

// 0x005A05F0: GameIsPaused: whether the game is paused (0x006FA7D4) - then the flag 0x3BA1FDB9 is added to the
// command's own parameters.
bool cfunc_game_is_paused(script::Struct* params, script::Script*)
{
    if (!objects::simulation_halted())
        return false;
    params->put_name_key(kPausedAnswerKey, kPausedAnswerKey);
    return true;
}

// 0x005A0620 / 0x005A0630: PauseObjects / UnPauseObjects.
bool cfunc_pause_objects(script::Struct*, script::Script*)
{
    object_edit()->get_freeze(true);
    return true;
}

bool cfunc_un_pause_objects(script::Struct*, script::Script*)
{
    object_edit()->get_freeze(false);
    return true;
}

// 0x005A0640: ResetClock: on the server, the skate module's clock (+0x370) set to the game mode's time limit.
bool cfunc_reset_clock(script::Struct*, script::Script*)
{
    if (online_hub_handle()->get_on_server())
        skate_module()->set_unknown370(game_mode_time_limit());
    return true;
}

// 0x005A0670: PadsPluggedIn.
bool cfunc_pads_plugged_in(script::Struct*, script::Script*)
{
    return modules::front_end_instance()->pads_plugged_in();
}

// 0x005A0680: DoFlash duration=<seconds> [texture="<name>"] [z=<depth>] [skater=n (the calling object's viewport)]
// [start_r ... start_a] [end_r ... end_a] [flags 0x70FFC649 (1), 0x19C43CF6 (2) or else 0x387C9ED6 (4),
// 0xDF6436D2 (8)]: a screen flash (0x004923E0) from the start colour to the end one. Nothing unless duration > 0.
// The eight colour values share one integer (as in the original): a missing one repeats the one before.
bool cfunc_do_flash(script::Struct* params, script::Script* script)
{
    float duration = 0.0f;
    float z = 0.0f;
    int viewport = -1;
    const char* texture = nullptr;
    params->fetch_float("duration", &duration, 0); // "duration"
    if (!(duration > objects::kFloatZero))       // (0.0)
        return true;
    params->fetch_text("texture", &texture, 0); // "texture"
    params->fetch_float("z", &z, 0);            // "z"
    int flags = 0;
    if (params->has_bare_flag(kFlashTextureFlag))
        flags = 1;
    if (params->has_bare_flag(kFlashFlag2))
        flags |= 2;
    else if (params->has_bare_flag(kFlashFlag4))
        flags |= 4;
    params->fetch_int_value(kSkater, &viewport, 0);
    if (viewport == -1)
        viewport = core::down_cast<objects::Rider>(script->entity_.get_pull())->view_slot_;
    if (params->has_bare_flag(kFlashFlag8))
        flags |= 8;
    std::array<uint8_t, 4> start{};
    std::array<uint8_t, 4> end{};
    int value = 0;
    static const std::array<const char*, 4> kStart = {{"start_r", "start_g", "start_b", "start_a"}}; // "start_r" ... "start_a"
    static const std::array<const char*, 4> kEnd = {{"end_r", "end_g", "end_b", "end_a"}};           // "end_r" ... "end_a"
    for (int i = 0; i < 4; ++i) {
        params->fetch_int_value(core::at(kStart, i), &value, 0);
        core::at(start, i) = static_cast<uint8_t>(value);
    }
    for (int i = 0; i < 4; ++i) {
        params->fetch_int_value(core::at(kEnd, i), &value, 0);
        core::at(end, i) = static_cast<uint8_t>(value);
    }
    uint32_t start_colour = 0;
    uint32_t end_colour = 0;
    std::memcpy(&start_colour, start.data(), 4);
    std::memcpy(&end_colour, end.data(), 4);
    render::engine::add_flash(viewport, start_colour, end_colour, duration, z, static_cast<uint32_t>(flags), texture);
    return true;
}

// 0x005A3B00 / 0x005A3B20 / 0x005A3B40 / 0x005A3B60: ClearFlag / SendFlag / QueryFlag <flag> name=<id> or
// prefix="<nodes>"; SendException <exception> name= or prefix=: flag_command_by_name with 1 (clear), 0 (set), 2 (test),
// 5 (exception). Whether an object did it (QueryFlag: whether one has the flag).
bool cfunc_clear_flag(script::Struct* params, script::Script* script)
{
    return flag_command_by_name(params, script, kFlagActionClear);
}

bool cfunc_send_flag(script::Struct* params, script::Script* script)
{
    return flag_command_by_name(params, script, kFlagActionSet);
}

bool cfunc_query_flag(script::Struct* params, script::Script* script)
{
    return flag_command_by_name(params, script, kFlagActionTest);
}

bool cfunc_send_exception(script::Struct* params, script::Script* script)
{
    return flag_command_by_name(params, script, kFlagActionException);
}

// 0x005A11C0: RoundIncrement: the game mode's round (0x005536D0).
bool cfunc_round_increment(script::Struct*, script::Script*)
{
    game_mode_object()->increment_round();
    return true;
}

// 0x005A11D0: GameModeEquals <mode> ...: whether one of the bare names is the game mode's (0x00553220: its own
// integer of that name is set).
bool cfunc_game_mode_equals(script::Struct* params, script::Script*)
{
    for (script::Component* component = params->step_to_entry(nullptr); component;
         component = params->step_to_entry(component)) {
        if (component->name == 0 && component->kind_tag() == script::kQbvalName &&
            game_mode_object()->is_true(component->value.word()))
            return true;
    }
    return false;
}

// 0x005A1230: GetCurrentLevel: the skate module's level (+0x374, as 0x651533EC - a name) and level structure name
// (+0x380, 0x16A0B364) in the script's parameters.
bool cfunc_get_current_level(script::Struct*, script::Script* script)
{
    const modules::Skate* skate = modules::Skate::sole();
    script->args_->add_component_of_type(0x651533EC, script::kQbvalName, modules::Skate::sole()->pending_level_);
    script->args_->put_name_key(0x16A0B364, modules::Skate::sole()->level_params_);
    return true;
}

// 0x005A1270: SetLevelStructureName name=<structure>: the skate module's +0x380.
bool cfunc_set_level_structure_name(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (unset in the original when missing - an asserted parameter)
    params->read_hash_value(kName, &name, 1);
    modules::Skate::sole()->level_params_ = name;
    return true;
}

// 0x005A12A0: GetLevelStructureName: the skate module's +0x380 as the script's 0x63B1214E.
bool cfunc_get_level_structure_name(script::Struct*, script::Script* script)
{
    script->args_->put_name_key(0x63B1214E, modules::Skate::sole()->level_params_);
    return true;
}

// 0x005A12C0: RestartLevel (the skate module's 0x005668A0).
bool cfunc_restart_level(script::Struct*, script::Script*)
{
    skate_module()->restart_level();
    return true;
}

// 0x005A12E0: GameFlow <script>: the skate module's game flow (+0x388) runs it (0x00552E30).
bool cfunc_game_flow(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    modules::Skate::sole()->game_flow_->get_request(name);
    return true;
}

namespace {

// The globals naming the career's Omnigon flags ClearOmnigons clears (in this order).
constexpr auto kOmnigonFlags = std::to_array<uint32_t>({
    0x13D5B5DB, 0x8ADCE461, 0xFDDBD4F7, 0x63BF4154, 0x14B871C2, 0x8DB12078, 0xFAB610EE, 0x6A090D7F,
    0x1D0E3DE9, 0x37CBEE45, 0x40CCDED3, 0xD9C58F69, 0xAEC2BFFF, 0x30A62A5C, 0x47A11ACA, 0xDEA84B70,
    0xA9AF7BE6, 0x39106677, 0x4E1756E1, 0x1CE6BD86, 0x6BE18D10, 0xF2E8DCAA, 0x85EFEC3C,
});

constexpr uint32_t kOmnigon = 0xAE94C183;     // (name unknown) the Omnigon (as LastBroadcastedOmnigonWas)
constexpr uint32_t kOmnigonFlag = 0xF649D637; // (name unknown) sent with it
constexpr uint32_t kCheatText = 0x61414D56;   // (name unknown) TryCheatString's text
constexpr uint32_t kCheatFailed = 0x323D63FF; // (names unknown) its scripts: no cheat,
constexpr uint32_t kCheatFound = 0xB95B735C;  // a cheat (before the cheat's own script)
constexpr uint32_t kCheatList = 0x75A23DA4;   // (name unknown) the global array of {c1 c2 cheatscript}

uint32_t& last_broadcasted_omnigon() { return game_flow_cfuncs_shared().last_broadcasted_omnigon_; }

// A skater's score (0x00580480; null if none) and its display brought up to date (0x005626E0).
void update_score_display(objects::Rider* skater)
{
    if (void* score = skater->get_points_total())
        static_cast<modules::PointTally*>(score)->refresh_balance_meter();
}

} // namespace

// 0x005A1320: ClearOmnigons: the career's 23 Omnigon flags (the values of the globals above) cleared.
bool cfunc_clear_omnigons(script::Struct*, script::Script*)
{
    objects::RiderStoryProgress* career = modules::Skate::sole()->career_.get();
    for (const uint32_t flag : kOmnigonFlags)
        career->un_set_global_flag(script::fetch_int_value(flag));
    return true;
}

// 0x005A1540: BroadcastOmnigon <0xAE94C183=omnigon> [0xF649D637]: noted as the last one sent and, from the server,
// sent to every remote connection (message 0x7F, 8 bytes: the Omnigon and the flag; sequenced, group 8).
bool cfunc_broadcast_omnigon(script::Struct* params, script::Script*)
{
    const online::Hub* game_net = online_hub_handle();
    net::ConnSearch search;
    uint32_t omnigon = 0; // (unset in the original when missing - an asserted parameter)
    params->read_hash_value(kOmnigon, &omnigon, true);
    const bool flag = params->has_entry_named(kOmnigonFlag);
    last_broadcasted_omnigon() = omnigon;
    net::AppHost* server = game_net->host_;
    for (net::Conn* conn = server->begin_conn_walk(&search); conn; conn = server->step_conn_walk(&search)) {
        if (!conn->is_off_machine())
            continue;
        net::NetMessageInfo desc;
        online::ToggleCheat message; // (wire/play_messages.h)
        message.cheat = omnigon;
        message.on = flag;
        std::array<uint8_t, online::kToggleCheatSize> data{};
        util::ByteWriter writer(data.data(), sizeof(data));
        online::write_toggle_cheat(writer, message);
        desc.content_ = data.data();
        desc.ident_ = online::kMsgToggleCheat;
        desc.byte_count_ = sizeof(data);
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = 8;
        server->put_on_outbox(conn->hdl_, &desc);
        last_broadcasted_omnigon() = omnigon;
    }
    last_broadcasted_omnigon() = omnigon;
    return true;
}

// 0x005A1720: SetScreenMode <...>: the screen mode (0x004A0C20 with the command's parameters), then every skater's
// score display brought up to date.
bool cfunc_set_screen_mode(script::Struct* params, script::Script* script)
{
    render::cfunc_set_screen_mode(params, script);
    for (uint32_t i = 0; i < 8; ++i) {
        if (objects::Rider* skater = get_rider_at(static_cast<int>(i)))
            update_score_display(skater);
    }
    return true;
}

// 0x005A1770: SetScreenModeFromGameMode: a debug message per local skater; the game mode's screen mode
// (0x00553630) set (0x004A0AD0) unless the two split modes (1 and 2) would only swap; the local skaters' score
// displays brought up to date.
bool cfunc_set_screen_mode_from_game_mode(script::Struct*, script::Script*)
{
    for (uint32_t i = 0; i < 8; ++i) {
        const objects::Rider* skater = get_rider_at(static_cast<int>(i));
        if (skater && skater->is_local_skater_)
            debug::printf("cfuncs %d: SUTUBBBEDDDDDDDDDDDDDDDDDDDDDDDD\n", 0x16C0); // "cfuncs %d: SUTUBBBED..."
    }
    const int mode = game_mode_object()->get_view_layout();
    const int current = render::get_view_layout();
    if (!((mode == 1 && current == 2) || (mode == 2 && current == 1)))
        render::set_screen_mode(mode);
    for (uint32_t i = 0; i < 8; ++i) {
        objects::Rider* skater = get_rider_at(static_cast<int>(i));
        if (skater && skater->is_local_skater_)
            update_score_display(skater);
    }
    return true;
}

// 0x005A1840: TryCheatString text="<code>": a cheat code is two numbers made from the text: its checksum, then
// 100000 times the checksum of a buffer holding (round + the last checksum) as decimal text, its terminator
// overwritten with 'X' (so the bytes left behind by the longer strings before - the buffer starts as
// "12345678901234567890", then holds the text - follow it up to the next terminator), each character of it added
// times 0x3FF to a sum. c1 = the checksum of the text's last two thirds ^ the sum, c2 = the buffer's last checksum ^
// the sum (logged). The cheats in the global array 0x75A23DA4 with those c1 / c2 run 0xB95B735C and their
// cheatscript; none (or a text under 6 characters): 0x323D63FF. Always false. The text is used as a sprintf format
// (kept: a '%' in it is interpreted).
bool cfunc_try_cheat_string(script::Struct* params, script::Script*)
{
    const char* text = nullptr; // (unset in the original when missing - an asserted parameter)
    params->fetch_text(kCheatText, &text, true);
    if (std::strlen(text) < 6) {
        script::launch_script(kCheatFailed, nullptr, nullptr, false, nullptr);
        return false;
    }
    std::array<char, 0x80> buffer{};
    text::format_to(buffer, "12345678901234567890"); // "12345678901234567890"
    text::format_data_to(buffer, text);              // (the text is the format: as in the original)
    uint32_t checksum = checksums::hash_from_text(text);
    int sum = 0;
    for (int round = 0; round < 100000; ++round) {
        text::format_to(buffer, "%d", static_cast<int>(round + checksum));
        core::at(buffer, std::strlen(buffer.data())) = 'X';
        for (core::CStringCursor c(buffer.data()); *c; ++c)
            sum += static_cast<signed char>(*c) * 0x3FF;
        checksum = checksums::hash_from_text(buffer.data());
    }
    const uint32_t c1 = checksums::hash_from_text(std::next(text, static_cast<std::ptrdiff_t>(std::strlen(text) / 3))) ^ static_cast<uint32_t>(sum);
    const uint32_t c2 = checksums::hash_from_text(buffer.data()) ^ static_cast<uint32_t>(sum);
    debug::printf("\n{c1=%d c2=%d CheatScript=cheat_xxx },  ; %s\n\n", c1, c2,
                  text); // "{c1=%d c2=%d CheatScript=cheat_xxx }..."
    script::Array* cheats = script::fetch_list(kCheatList);
    const int count = static_cast<int>(cheats->extent_);
    bool found = false;
    for (int i = 0; i < count; ++i) {
        auto* cheat = cheats->struct_at(static_cast<uint32_t>(i));
        int cheat_c1 = 0;
        int cheat_c2 = 0;
        uint32_t script = 0;
        cheat->fetch_int_value("c1", &cheat_c1, true);        // "c1"
        cheat->fetch_int_value("c2", &cheat_c2, true);        // "c2"
        cheat->read_hash_value("cheatscript", &script, true); // "cheatscript"
        if (ints::as_unsigned(cheat_c1) == c1 && ints::as_unsigned(cheat_c2) == c2) {
            script::launch_script(kCheatFound, nullptr, nullptr, false, nullptr);
            script::launch_script(script, nullptr, nullptr, false, nullptr);
            found = true;
        }
    }
    if (!found)
        script::launch_script(kCheatFailed, nullptr, nullptr, false, nullptr);
    return false;
}

// 0x005A1A90: LevelIs <level>: whether it's the skate module's level (+0x378).
bool cfunc_level_is(script::Struct* params, script::Script*)
{
    uint32_t level = 0; // (unset in the original when missing - an asserted parameter)
    params->read_hash_value(0u, &level, true);
    return modules::Skate::sole()->level_ == level;
}

// 0x005A1F80: SetStatOverride [value (0)]: the skate module's forced stat value (+0x3D0).
bool cfunc_set_stat_override(script::Struct* params, script::Script*)
{
    float value = 0.0f;
    params->fetch_float(0u, &value, 0);
    modules::Skate::sole()->forced_stat_ = value;
    return true;
}

// 0x005A1FB0: ToggleRails: the skate module's rail display (+0x3D4) on / off.
bool cfunc_toggle_rails(script::Struct*, script::Script*)
{
    bool& rails = modules::Skate::sole()->debug_rails_;
    rails = !rails;
    return true;
}

// 0x005A1FD0: GetTimeLimit: the game mode's time limit as the script's time_limit.
bool cfunc_get_time_limit(script::Struct*, script::Script* script)
{
    script->args_->put_int(0xC51BF6E3, game_mode_time_limit()); // "time_limit"
    return true;
}

// 0x005A1FF0 / 0x005A2010: GetGoalsMode / GetTeamGraffitiMode: the same game mode value (0x005532D0) as the script's
// 0x38DBE1D0 / 0x6835B854.
bool cfunc_get_goals_mode(script::Struct*, script::Script* script)
{
    script->args_->put_name_key(0x38DBE1D0, game_mode_object()->get_goals_mode());
    return true;
}

bool cfunc_get_team_graffiti_mode(script::Struct*, script::Script* script)
{
    script->args_->put_name_key(0x6835B854, game_mode_object()->get_goals_mode());
    return true;
}

// 0x005A2030: GetGameMode: the game mode's name (0x005532A0) as the script's 0x3E04B26B.
bool cfunc_get_game_mode(script::Struct*, script::Script* script)
{
    script->args_->put_name_key(0x3E04B26B, game_mode_object()->get_name_key());
    return true;
}

// 0x005A2090: SetTesterScript <script> [params={...}] (the skate module's 0x00565ED0).
bool cfunc_set_tester_script(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    skate_module()->launch_script(name, script_params);
    return true;
}

// 0x005A20F0: KillTesterScript (the skate module's 0x00565F80: its answer).
bool cfunc_kill_tester_script(script::Struct*, script::Script*) { return skate_module()->kill_script(); }

// 0x005A21A0: AutoRail <...>: the skate module's rail object (+0x24) is given the parameters - a shared stub that
// ignores them.
bool cfunc_auto_rail(script::Struct* params, script::Script*)
{
    (void)modules::Skate::sole()->rail_manager_; // (the stub's `this`)
    stubs::ignore_argument(params);
    return true;
}

// 0x005A4170: SpawnSkaterScript <script> [params={...}] [Id=id] [Callback=<script>] [CallbackParams={...}]: the
// script spawned at the calling script's node, running on the local skater.
bool cfunc_spawn_skater_script(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    uint32_t id = 0;
    params->read_hash_value("Id", &id, 0); // "Id"
    uint32_t callback = 0;
    params->read_hash_value("Callback", &callback, 0); // "Callback"
    script::Struct* callback_params = nullptr;
    params->read_sub_struct("CallbackParams", &callback_params, 0); // "CallbackParams"
    script::Script* spawned = script::spawn_script(name, script_params, callback, callback_params, script->list_link_,
                                                   id, false, false, false, false);
    spawned->entity_.assign_value(get_home_rider()); // (not checked for null, as in the original)
    return true;
}

} // namespace commands

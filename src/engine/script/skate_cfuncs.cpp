// Script commands (CFunctions) of the skate game, 0x0059EDF0-0x0059F610 (see skate_cfuncs.h).

#include "engine/script/skate_cfuncs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/script/cscript.h"
#include "engine/script/node_prefixes.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "platform/stopwatch.h"
#include "engine/sys/model_viewer.h"
#include "engine/gfx/loading_console.h"
#include "engine/gfx/scene_lights.h"
#include "engine/gfx/engine_effects.h"
#include "engine/script/script_startup.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "engine/script/game_cfuncs.h"
#include "platform/compiler.h"

namespace commands {

char g_level_name[64];
uint32_t g_debounce_times[22];

namespace {

constexpr uint32_t kTime = 0x906B67BA;               // "time"
constexpr uint32_t kId = 0x40C698AF;                 // "id"
constexpr uint32_t kPrefix = 0x6C4E7971;             // "prefix"
constexpr uint32_t kFakeLightsExact = 0x80804991;    // (name unknown) FakeLights: the prefix's nodes, not a prefix
constexpr uint32_t kParams = 0x7031F10C;             // "params"
constexpr uint32_t kCallback = 0x86068BD9;           // "callback"
constexpr uint32_t kCallbackParams = 0x0DD85BDA;     // "callbackparams"
constexpr uint32_t kOnlineFlag = 0x20209C31;         // "online_mode_flag"
constexpr uint32_t kPermanent = 0x23627FD7;          // "permanent"
constexpr uint32_t kNotSessionSpecific = 0x2EBFF4BC; // "NotSessionSpecific"
constexpr uint32_t kKeepSpawnerFile = 0x01917DF2;    // (name unknown) SpawnScript: don't note the spawner's .qb file
constexpr uint32_t kFreeze = 0xB96E0BE5;             // "freeze"
constexpr uint32_t kBlank = 0xC3D43B9A;              // "blank"
constexpr uint32_t kOmnigon = 0xAE94C183;            // (name unknown) the Omnigon's checksum
constexpr uint32_t kDumpScriptsFlag = 0x2AB71EEF;    // (name unknown) passed to DumpScripts' compiled-out dump

// The button's index in the button table (0x0067D6D8, entries 1-21; 0 if it isn't one).
int glyph_for_button_tag(uint32_t button) { return pad_input::glyph_for_button_tag(button); }

// The last Omnigon checksum sent to the other players (0x007CEBC8).
uint32_t& last_broadcasted_omnigon() { return g_last_broadcasted_omnigon; }

// The current level's node array name (0x007CE6D8) and the level's name (0x007CE698, 64 characters).
char* node_table_key_text() { return g_node_list_name; }
char* get_level_name() { return g_level_name; }

// The viewer (the debug camera and the object it shows, 0x007CE4A8 through its getter 0x00576EA0).
void* get_viewer() { return modules::get_live_viewer(); }

// DumpScripts' list of the running scripts - compiled out of this build (an empty call, 0x005480A0).
inline void dump_scripts_list(bool /*flag*/) {}

} // namespace

uint32_t* debounce_times() { return g_debounce_times; }

// 0x0059EE80
bool __cdecl controller_pressed(const void* controller, uint32_t button)
{
    if (!controller)
        return false;
    auto* input = *reinterpret_cast<PadInputSample* const*>(static_cast<const uint8_t*>(controller) + 0x18);
    if (!input)
        return false;
    int index = glyph_for_button_tag(button);
    if (debounce_times()[index] > frame_clock::get_time())
        return false;
    switch (button) {
    case 0xBC6B118F:
        return input->up != 0; // up
    case 0xE3006FC4:
        return input->down != 0; // down
    case 0x85981897:
        return input->left != 0; // left
    case 0x4B358AEB:
        return input->right != 0; // right
    case 0xB7231A95:
        return input->up && input->left; // upleft
    case 0xA50950C5:
        return input->up && input->right; // upright
    case 0xD8847EFA:
        return input->down && input->left; // downleft
    case 0x786B8B68:
        return input->down && input->right; // downright
    case 0x7323E97C:
        return input->x != 0; // x
    case 0x20689278:
        return input->triangle != 0; // triangle
    case 0x321C9756:
        return input->square != 0; // square
    case 0x2B489A86:
        return input->circle != 0; // circle
    case 0x26B0C991:
        return input->l1 != 0; // l1
    case 0xBFB9982B:
        return input->l2 != 0; // l2
    case 0xF2F1F64E:
        return input->r1 != 0; // r1
    case 0x6BF8A7F4:
        return input->r2 != 0; // r2
    case 0x767A45D7:
        return input->black != 0; // black
    case 0xBD30325B:
        return input->white != 0; // white
    case 0x9D2D8850:
        return input->z != 0; // z
    default:
        return false;
    }
}

// 0x0059EDF0: ControllerDebounce <button> [time=seconds (1)]: the button doesn't count as pressed (for
// ControllerPressed) for `time` seconds from now. Always true.
bool __cdecl cfunc_controller_debounce(script::Struct* params, script::Script*)
{
    float time = 1.0f;
    params->fetch_float(kTime, &time, 0);
    uint32_t button = 0;
    if (params->read_hash_value(0u, &button, 0)) {
        int index = glyph_for_button_tag(button);
        // x87: the unsigned clock + time * 1000, truncated through 64 bits
        double until = static_cast<double>(frame_clock::get_time()) + static_cast<double>(time) * 1000.0;
        debounce_times()[index] = static_cast<uint32_t>(static_cast<int64_t>(until));
    }
    return true;
}

// 0x0059F0F0: FakeLights id=<light> <settings>: change one fake light (0x004901D0); or FakeLights
// prefix=<name> [time=n] <settings>: set up fake lights on the nodes named with that prefix (0x0045BF90 lists
// them; with the flag 0x80804991 the name isn't treated as a prefix) - 0x0048FE40. False without id or prefix.
bool __cdecl cfunc_fake_lights(script::Struct* params, script::Script*)
{
    uint32_t id = 0;
    if (params->read_hash_value(kId, &id, 0)) {
        render::update_scene_light(id, params);
        return true;
    }
    int time = 0;
    params->fetch_int_value(kTime, &time, 0);
    uint32_t prefix = 0;
    if (!params->hash_from_name_or_text(kPrefix, &prefix, 0))
        return false;
    uint32_t count = 0; // a 16-bit count in a zeroed 32-bit slot
    bool is_prefix = !params->has_bare_flag(kFakeLightsExact);
    // (the original passes is_prefix too: 0x0045BF90 takes two arguments and ignores it)
    (void)is_prefix;
    uint32_t* nodes = level_nodes::collect_nodes_by_prefix(prefix, reinterpret_cast<uint16_t*>(&count));
    render::queue_scene_lights(nodes, static_cast<uint16_t>(count), time, params);
    return true;
}

// 0x0059F1B0: UpdateFxLighting (0x00491F10).
bool __cdecl cfunc_update_fx_lighting(script::Struct*, script::Script*)
{
    render::engine::light_all_texture_splats();
    return true;
}

// 0x0059F1C0: NodeExists <name>: whether the level's node array has a node of that name (0x0045BE70). The
// name is asserted (a compiled-out assert: missing = 0).
bool __cdecl cfunc_node_exists(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 1);
    return level_nodes::node_exists(name);
}

// 0x0059F1F0: LoadNodeArray "<file>": kill the session's spawned scripts, note the name (0x007CE6D8, copied
// unchecked), load the .qb file (0x005A5960) and set up the node array (0x0045C210).
bool __cdecl cfunc_load_node_array(script::Struct* params, script::Script*)
{
    memory::push_mem_profile("LoadNodeArray"); // "LoadNodeArray"
    script::kill_all_child_scripts();
    const char* name = nullptr;
    params->fetch_text(0u, &name, 0);
    char* out = node_table_key_text(); // (no length limit, like strcpy)
    for (const char* in = name; (*out++ = *in++) != '\0';) {
    }
    script::load_qb_file(name, 0);
    level_nodes::get_last_node_array();
    memory::pop_mem_profile();
    return true;
}

// 0x0059F260: ResetCamera: not implemented on PC - for each of the two viewports with a camera (0x004A0950), a
// debug message. Always true.
bool __cdecl cfunc_reset_camera(script::Struct*, script::Script*)
{
    for (int viewport = 0; viewport < 2; ++viewport) {
        if (render::obtain_active_lens(viewport))
            debug::printf("TODO: ScriptResetCamera not implemented\n"); // "TODO: ScriptResetCamera not implemented\n"
    }
    return true;
}

// 0x0059F290: SetMovementVelocity [speed (400)]: the viewer's movement speed (0x00576E80).
bool __cdecl cfunc_set_movement_velocity(script::Struct* params, script::Script*)
{
    float speed = 400.0f;
    params->fetch_float(0u, &speed, 0);
    modules::set_movement_velocity(speed);
    return true;
}

// 0x0059F2C0: SetRotateVelocity [speed (120)]: the viewer's rotation speed (0x00576E90).
bool __cdecl cfunc_set_rotate_velocity(script::Struct* params, script::Script*)
{
    float speed = 120.0f;
    params->fetch_float(0u, &speed, 0);
    modules::set_rotate_velocity(speed);
    return true;
}

// 0x0059F2F0: ClearViewerObject: the viewer (if any) lets go of the object it shows (0x00576D40).
bool __cdecl cfunc_clear_viewer_object(script::Struct*, script::Script*)
{
    if (void* viewer = get_viewer())
        static_cast<modules::Viewer*>(viewer)->drop_viewer_entity();
    return true;
}

// 0x0059F310: CasArtist: whether the global integer cas_artist is set.
bool __cdecl cfunc_cas_artist(script::Struct*, script::Script*)
{
    return script::fetch_int_value("cas_artist") != 0; // "cas_artist"
}

// 0x0059F330: SpawnScript <name> [params={...}] [id=n] [callback=<script>] [callbackparams={...}]
// [online_mode_flag] [permanent] [NotSessionSpecific=n]: spawn the script at the calling script's node. The new
// script remembers the caller's .qb file (so killing by file kills it) unless the flag 0x01917DF2 is given.
// Always true.
bool __cdecl cfunc_spawn_script(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    uint32_t id = 0;
    params->read_hash_value(kId, &id, 0);
    uint32_t callback = 0;
    params->read_hash_value(kCallback, &callback, 0);
    script::Struct* callback_params = nullptr;
    params->read_sub_struct(kCallbackParams, &callback_params, 0);
    bool replicate_flag = params->has_bare_flag(kOnlineFlag);
    bool permanent = params->has_bare_flag(kPermanent);
    int not_session_specific = 0;
    params->fetch_int_value(kNotSessionSpecific, &not_session_specific, 0);
    script::Script* spawned = script::spawn_script(name, script_params, callback, callback_params, script->list_link_,
                                                   id, replicate_flag, permanent, not_session_specific != 0, false);
    // (as in the original, not checked for null - a C function spawns no script)
    if (!params->has_bare_flag(kKeepSpawnerFile))
        spawned->set_spawner_file(script);
    return true;
}

// 0x0059F440: UnBlockInterruptedScript: the level the calling script's interrupt interrupted goes on.
bool __cdecl cfunc_un_block_interrupted_script(script::Struct*, script::Script* script)
{
    script->unblock_interrupted_script();
    return true;
}

// 0x0059F450: KillSpawnedScript Name=<script> (kill the spawned scripts running it at any level) or
// Id=<id> (by spawn id, unique id or spawner file). Always true.
bool __cdecl cfunc_kill_spawned_script(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    if (params->read_hash_value("Name", &name, 0)) { // "Name"
        script::stop_scripts_targeting(name);
        return true;
    }
    uint32_t id = 0;
    if (params->read_hash_value("Id", &id, 0)) // "Id"
        script::stop_scripts_by_tag(id);
    return true;
}

// 0x0059F4C0 / 0x0059F4D0: PauseSpawnedScripts / UnPauseSpawnedScripts.
bool __cdecl cfunc_pause_spawned_scripts(script::Struct*, script::Script*)
{
    script::pause_spawned_scripts(true);
    return true;
}

bool __cdecl cfunc_un_pause_spawned_scripts(script::Struct*, script::Script*)
{
    script::pause_spawned_scripts(false);
    return true;
}

// 0x0059F4E0: DisplayLoadingScreen ["<image>" ("images\xxxxx")] [freeze] [blank] [seconds]: show the loading
// screen (0x00490890); with a number, the loading bar's time (0x00490770). Always true.
bool __cdecl cfunc_display_loading_screen(script::Struct* params, script::Script*)
{
    const char* image = "images\\xxxxx"; // "images\xxxxx"
    params->fetch_text(0u, &image, 0);
    bool blank = params->has_bare_flag(kBlank);
    bool freeze = params->has_bare_flag(kFreeze);
    render::display_loading_screen(image, freeze, blank);
    gfx::loading_console::screen_shown(freeze, blank); // not original: the loading console (gfx/loading_console.h)
    float seconds = 0.0f;                              // (the original leaves it unset - unused then)
    if (params->fetch_float(0u, &seconds, 0))
        render::set_loading_bar_time(seconds);
    return true;
}

// 0x0059F550: HideLoadingScreen (0x004908C0).
bool __cdecl cfunc_hide_loading_screen(script::Struct*, script::Script*)
{
    gfx::loading_console::screen_hidden(); // not original: the loading console (gfx/loading_console.h)
    render::remove_loading_screen();
    return true;
}

// 0x0059F560: LastBroadcastedOmnigonWas <0xAE94C183=checksum>: whether it's the last Omnigon sent.
bool __cdecl cfunc_last_broadcasted_omnigon_was(script::Struct* params, script::Script*)
{
    uint32_t omnigon = 0; // (the original leaves it unset when missing - an asserted parameter)
    params->read_hash_value(kOmnigon, &omnigon, 1);
    return last_broadcasted_omnigon() == omnigon;
}

// 0x0059F590: OmnigonAllowed <0xAE94C183=checksum>: reads the parameter, always true.
bool __cdecl cfunc_omnigon_allowed(script::Struct* params, script::Script*)
{
    uint32_t omnigon = 0;
    params->read_hash_value(kOmnigon, &omnigon, 1);
    return true;
}

// 0x0059F5B0: SetLevelName "<name>": the level's name (0x007CE698). The original passes the name to sprintf
// as the format (kept: a '%' in it is interpreted).
bool __cdecl cfunc_set_level_name(script::Struct* params, script::Script*)
{
    const char* name = nullptr;
    params->fetch_text(0u, &name, 1);
    THUG2_MSVC_WARNING(suppress : 4996) // (unbounded, and the name is the format: as in the original)
    std::sprintf(get_level_name(), name);
    return true;
}

// 0x0059F5E0: DumpScripts [just_stack]: the script list dump is compiled out of this build. Always true.
bool __cdecl cfunc_dump_scripts(script::Struct* params, script::Script*)
{
    if (!params->has_bare_flag("just_stack")) // "just_stack"
        dump_scripts_list(params->has_bare_flag(kDumpScriptsFlag));
    return true;
}

} // namespace commands

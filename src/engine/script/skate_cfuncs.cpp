// Script commands (CFunctions) of the skate game, 0x0059EDF0-0x0059F610 (see skate_cfuncs.h).

#include "engine/script/skate_cfuncs.h"

#include "base/core/str.h"

#include <cstddef>
#include <cstdint>
#include <array>

#include "base/core/checked_index.h"
#include "base/core/dbg.h"
#include "engine/memory/mem.h"
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
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/script/script_state.h"

namespace {
constexpr size_t kLevelNameBytes = 64;
constexpr float kDefaultMovementSpeed = 400.0F;
constexpr float kDefaultRotateSpeed = 120.0F;

// Names (the checksums of their text).
constexpr uint32_t kBlack = 0x767A45D7U;     // Black
constexpr uint32_t kCIRCLE = 0x2B489A86U;    // CIRCLE
constexpr uint32_t kDOWN = 0xE3006FC4U;      // DOWN
constexpr uint32_t kUP = 0xBC6B118FU;        // up
constexpr uint32_t kX = 0x7323E97CU;         // x
constexpr uint32_t kL1 = 0x26B0C991U;        // l1
constexpr uint32_t kL2 = 0xBFB9982BU;        // l2
constexpr uint32_t kR1 = 0xF2F1F64EU;        // r1
constexpr uint32_t kR2 = 0x6BF8A7F4U;        // r2
constexpr uint32_t kZ = 0x9D2D8850U;         // z
constexpr uint32_t kDownLeft = 0xD8847EFAU;  // DownLeft
constexpr uint32_t kDownRight = 0x786B8B68U; // DownRight
constexpr uint32_t kLEFT = 0x85981897U;      // LEFT
constexpr uint32_t kRIGHT = 0x4B358AEBU;     // RIGHT
constexpr uint32_t kSQUARE = 0x321C9756U;    // SQUARE
constexpr uint32_t kTRIANGLE = 0x20689278U;  // TRIANGLE
constexpr uint32_t kUpLeft = 0xB7231A95U;    // UpLeft
constexpr uint32_t kUpRight = 0xA50950C5U;   // UpRight
constexpr uint32_t kWhite = 0xBD30325BU;     // White

} // namespace

namespace commands {

namespace {
// NOT ORIGINAL grouping - skate_cfuncs.cpp's state (the original's globals). Made on first use, never destroyed
// (skate_cfuncs_state()).
constexpr size_t kDebounceTimesSize = 22;
constexpr size_t kLevelNameSize = 64;
struct SkateCfuncsState {
    std::array<char, kLevelNameSize> level_name_{};
    std::array<uint32_t, kDebounceTimesSize> debounce_times_{};
};

SkateCfuncsState& skate_cfuncs_state()
{
    static core::NoDestroy<SkateCfuncsState> state;
    return state.get();
}

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

// The current level's node array name (0x007CE6D8) and the level's name (0x007CE698, 64 characters).

std::array<char, kLevelNameBytes>& get_level_name() { return skate_cfuncs_state().level_name_; }

// The viewer (the debug camera and the object it shows, 0x007CE4A8 through its getter 0x00576EA0).
void* get_viewer() { return modules::get_live_viewer(); }

// DumpScripts' list of the running scripts - compiled out of this build (an empty call, 0x005480A0).
inline void dump_scripts_list(bool /*flag*/) {}

} // namespace

std::array<uint32_t, kDebounceSlotCount>& debounce_times() { return skate_cfuncs_state().debounce_times_; }

// 0x0059EE80
bool controller_pressed(const pad_input::InputHandler* controller, uint32_t button)
{
    if (!controller) {
        return false;
    }
    const pad_input::Payload* data = controller->control_data();
    if (!data) {
        return false;
    }
    // (the pressures of the d-pad and the buttons, by their places in the controller's data)
    const PadInputSample input{data};
    const int index = glyph_for_button_tag(button);
    if (core::at(debounce_times(), index) > frame_clock::get_time()) {
        return false;
    }
    switch (button) {
    case kUP:
        return input.up() != 0; // up
    case kDOWN:
        return input.down() != 0; // down
    case kLEFT:
        return input.left() != 0; // left
    case kRIGHT:
        return input.right() != 0; // right
    case kUpLeft:
        return (input.up() != 0U) && (input.left() != 0U); // upleft
    case kUpRight:
        return (input.up() != 0U) && (input.right() != 0U); // upright
    case kDownLeft:
        return (input.down() != 0U) && (input.left() != 0U); // downleft
    case kDownRight:
        return (input.down() != 0U) && (input.right() != 0U); // downright
    case kX:
        return input.x() != 0; // x
    case kTRIANGLE:
        return input.triangle() != 0; // triangle
    case kSQUARE:
        return input.square() != 0; // square
    case kCIRCLE:
        return input.circle() != 0; // circle
    case kL1:
        return input.l1() != 0; // l1
    case kL2:
        return input.l2() != 0; // l2
    case kR1:
        return input.r1() != 0; // r1
    case kR2:
        return input.r2() != 0; // r2
    case kBlack:
        return input.black() != 0; // black
    case kWhite:
        return input.white() != 0; // white
    case kZ:
        return input.z() != 0; // z
    default:
        return false;
    }
}

// 0x0059EDF0: ControllerDebounce <button> [time=seconds (1)]: the button doesn't count as pressed (for
// ControllerPressed) for `time` seconds from now. Always true.
bool cfunc_controller_debounce(script::Struct* params, script::Script* /*script*/)
{
    float time = 1.0F;
    params->fetch_float(kTime, &time, 0);
    uint32_t button = 0;
    if (params->read_hash_value(0U, &button, 0)) {
        const int index = glyph_for_button_tag(button);
        // x87: the unsigned clock + time * 1000, truncated through 64 bits
        const double until = static_cast<double>(frame_clock::get_time()) + (static_cast<double>(time) * 1000.0);
        core::at(debounce_times(), index) = static_cast<uint32_t>(static_cast<int64_t>(until));
    }
    return true;
}

// 0x0059F0F0: FakeLights id=<light> <settings>: change one fake light (0x004901D0); or FakeLights
// prefix=<name> [time=n] <settings>: set up fake lights on the nodes named with that prefix (0x0045BF90 lists
// them; with the flag 0x80804991 the name isn't treated as a prefix) - 0x0048FE40. False without id or prefix.
bool cfunc_fake_lights(script::Struct* params, script::Script* /*script*/)
{
    uint32_t id = 0;
    if (params->read_hash_value(kId, &id, 0)) {
        render::update_scene_light(id, params);
        return true;
    }
    int time = 0;
    params->fetch_int_value(kTime, &time, 0);
    uint32_t prefix = 0;
    if (!params->hash_from_name_or_text(kPrefix, &prefix, 0)) {
        return false;
    }
    const bool is_prefix = !params->has_bare_flag(kFakeLightsExact);
    // (the original passes is_prefix too: 0x0045BF90 takes two arguments and ignores it)
    (void)is_prefix;
    render::queue_scene_lights(level_nodes::collect_nodes_by_prefix(prefix), time, params);
    return true;
}

// 0x0059F1B0: UpdateFxLighting (0x00491F10).
bool cfunc_update_fx_lighting(script::Struct* /*params*/, script::Script* /*script*/)
{
    render::engine::light_all_texture_splats();
    return true;
}

// 0x0059F1C0: NodeExists <name>: whether the level's node array has a node of that name (0x0045BE70). The
// name is asserted (a compiled-out assert: missing = 0).
bool cfunc_node_exists(script::Struct* params, script::Script* /*script*/)
{
    uint32_t name = 0;
    params->read_hash_value(0U, &name, 1);
    return level_nodes::node_exists(name);
}

// 0x0059F1F0: LoadNodeArray "<file>": kill the session's spawned scripts, note the name (0x007CE6D8, copied
// unchecked), load the .qb file (0x005A5960) and set up the node array (0x0045C210).
bool cfunc_load_node_array(script::Struct* params, script::Script* /*script*/)
{
    memory::push_mem_profile("LoadNodeArray"); // "LoadNodeArray"
    script::kill_all_child_scripts();
    const char* name = nullptr;
    params->fetch_text(0U, &name, 0);
    text::copy_unbounded(script::script_state().node_list_name().data(), name); // (no length limit, as the original)
    script::load_qb_file(name, 0);
    level_nodes::get_last_node_array();
    memory::pop_mem_profile();
    return true;
}

// 0x0059F260: ResetCamera: not implemented on PC - for each of the two viewports with a camera (0x004A0950), a
// debug message. Always true.
bool cfunc_reset_camera(script::Struct* /*params*/, script::Script* /*script*/)
{
    for (int viewport = 0; viewport < 2; ++viewport) {
        if (render::obtain_active_lens(viewport)) {
            debug::printf("TODO: ScriptResetCamera not implemented\n"); // "TODO: ScriptResetCamera not implemented\n"
        }
    }
    return true;
}

// 0x0059F290: SetMovementVelocity [speed (400)]: the viewer's movement speed (0x00576E80).
bool cfunc_set_movement_velocity(script::Struct* params, script::Script* /*script*/)
{
    float speed = kDefaultMovementSpeed;
    params->fetch_float(0U, &speed, 0);
    modules::set_movement_velocity(speed);
    return true;
}

// 0x0059F2C0: SetRotateVelocity [speed (120)]: the viewer's rotation speed (0x00576E90).
bool cfunc_set_rotate_velocity(script::Struct* params, script::Script* /*script*/)
{
    float speed = kDefaultRotateSpeed;
    params->fetch_float(0U, &speed, 0);
    modules::set_rotate_velocity(speed);
    return true;
}

// 0x0059F2F0: ClearViewerObject: the viewer (if any) lets go of the object it shows (0x00576D40).
bool cfunc_clear_viewer_object(script::Struct* /*params*/, script::Script* /*script*/)
{
    if (void* viewer = get_viewer()) {
        static_cast<modules::Viewer*>(viewer)->drop_viewer_entity();
    }
    return true;
}

// 0x0059F310: CasArtist: whether the global integer cas_artist is set.
bool cfunc_cas_artist(script::Struct* /*params*/, script::Script* /*script*/)
{
    return script::fetch_int_value("cas_artist") != 0; // "cas_artist"
}

// 0x0059F330: SpawnScript <name> [params={...}] [id=n] [callback=<script>] [callbackparams={...}]
// [online_mode_flag] [permanent] [NotSessionSpecific=n]: spawn the script at the calling script's node. The new
// script remembers the caller's .qb file (so killing by file kills it) unless the flag 0x01917DF2 is given.
// Always true.
bool cfunc_spawn_script(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0U, &name, 0);
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    uint32_t id = 0;
    params->read_hash_value(kId, &id, 0);
    uint32_t callback = 0;
    params->read_hash_value(kCallback, &callback, 0);
    script::Struct* callback_params = nullptr;
    params->read_sub_struct(kCallbackParams, &callback_params, 0);
    const bool replicate_flag = params->has_bare_flag(kOnlineFlag);
    const bool permanent = params->has_bare_flag(kPermanent);
    int not_session_specific = 0;
    params->fetch_int_value(kNotSessionSpecific, &not_session_specific, 0);
    script::Script* spawned = script::spawn_script(name, script_params, {.callback = callback, .callback_params_ = callback_params, .node = script->list_link(), .id = id, .replicate = replicate_flag, .permanent = permanent, .not_session_specific_ = not_session_specific != 0});
    // (as in the original, not checked for null - a C function spawns no script)
    if (!params->has_bare_flag(kKeepSpawnerFile)) {
        spawned->set_spawner_file(script);
    }
    return true;
}

// 0x0059F440: UnBlockInterruptedScript: the level the calling script's interrupt interrupted goes on.
bool cfunc_un_block_interrupted_script(script::Struct* /*params*/, script::Script* script)
{
    script->unblock_interrupted_script();
    return true;
}

// 0x0059F450: KillSpawnedScript Name=<script> (kill the spawned scripts running it at any level) or
// Id=<id> (by spawn id, unique id or spawner file). Always true.
bool cfunc_kill_spawned_script(script::Struct* params, script::Script* /*script*/)
{
    uint32_t name = 0;
    if (params->read_hash_value("Name", &name, 0)) { // "Name"
        script::stop_scripts_targeting(name);
        return true;
    }
    uint32_t id = 0;
    if (params->read_hash_value("Id", &id, 0)) { // "Id"
        script::stop_scripts_by_tag(id);
    }
    return true;
}

// 0x0059F4C0 / 0x0059F4D0: PauseSpawnedScripts / UnPauseSpawnedScripts.
bool cfunc_pause_spawned_scripts(script::Struct* /*params*/, script::Script* /*script*/)
{
    script::pause_spawned_scripts(true);
    return true;
}

bool cfunc_un_pause_spawned_scripts(script::Struct* /*params*/, script::Script* /*script*/)
{
    script::pause_spawned_scripts(false);
    return true;
}

// 0x0059F4E0: DisplayLoadingScreen ["<image>" ("images\xxxxx")] [freeze] [blank] [seconds]: show the loading
// screen (0x00490890); with a number, the loading bar's time (0x00490770). Always true.
bool cfunc_display_loading_screen(script::Struct* params, script::Script* /*script*/)
{
    const char* image = "images\\xxxxx"; // "images\xxxxx"
    params->fetch_text(0U, &image, 0);
    const bool blank = params->has_bare_flag(kBlank);
    const bool freeze = params->has_bare_flag(kFreeze);
    render::display_loading_screen(image, freeze, blank);
    gfx::loading_console::screen_shown(freeze, blank); // not original: the loading console (gfx/loading_console.h)
    float seconds = 0.0F;                              // (the original leaves it unset - unused then)
    if (params->fetch_float(0U, &seconds, 0)) {
        render::set_loading_bar_time(seconds);
    }
    return true;
}

// 0x0059F550: HideLoadingScreen (0x004908C0).
bool cfunc_hide_loading_screen(script::Struct* /*params*/, script::Script* /*script*/)
{
    gfx::loading_console::screen_hidden(); // not original: the loading console (gfx/loading_console.h)
    render::remove_loading_screen();
    return true;
}

// 0x0059F560: LastBroadcastedOmnigonWas <0xAE94C183=checksum>: whether it's the last Omnigon sent.
bool cfunc_last_broadcasted_omnigon_was(script::Struct* params, script::Script* /*script*/)
{
    uint32_t omnigon = 0; // (the original leaves it unset when missing - an asserted parameter)
    params->read_hash_value(kOmnigon, &omnigon, 1);
    return script::script_state().last_broadcasted_omnigon() == omnigon;
}

// 0x0059F590: OmnigonAllowed <0xAE94C183=checksum>: reads the parameter, always true.
bool cfunc_omnigon_allowed(script::Struct* params, script::Script* /*script*/)
{
    uint32_t omnigon = 0;
    params->read_hash_value(kOmnigon, &omnigon, 1);
    return true;
}

// 0x0059F5B0: SetLevelName "<name>": the level's name (0x007CE698). The original passes the name to sprintf
// as the format (kept: a '%' in it is interpreted).
bool cfunc_set_level_name(script::Struct* params, script::Script* /*script*/)
{
    const char* name = nullptr;
    params->fetch_text(0U, &name, 1);
    text::format_data_to(get_level_name(), name); // (the name is the format: as in the original)
    return true;
}

// 0x0059F5E0: DumpScripts [just_stack]: the script list dump is compiled out of this build. Always true.
bool cfunc_dump_scripts(script::Struct* params, script::Script* /*script*/)
{
    if (!params->has_bare_flag("just_stack")) { // "just_stack"
        dump_scripts_list(params->has_bare_flag(kDumpScriptsFlag));
    }
    return true;
}

} // namespace commands

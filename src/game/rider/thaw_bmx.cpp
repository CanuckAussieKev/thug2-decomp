// NOT ORIGINAL - THAW's BMX ridden in THUG2 (game/rider/thaw_bmx.h).

#include "game/rider/thaw_bmx.h"
#include "platform/keyboard.h"

#include <cstdint>
#include <string>
#include <unordered_map>

#include <SDL2/SDL.h>
#include <array>

#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "engine/core/mem.h"
#include "engine/maths/vec4.h"
#include "frontend/front_end.h"
#include "editor/level_editor.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/shadow_base.h"
#include "engine/gfx/texture_cfuncs.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx_parked.h"
#include "platform/platform_config.h"
#include "engine/sys/mlp.h"
#include "platform/screenshot_files.h"
#include "platform/run_log.h"
#include "game/rider/core_physics.h"
#include "engine/core/checked_index.h"

namespace skater::thaw_bmx {

namespace {

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }


// The core physics component's fields read here (THUG2's).
constexpr uint32_t kObject = 0xC; // its skater
// The skater object's.
constexpr uint32_t kPosition = 0x4C;
constexpr uint32_t kFacing = 0x8C;

// The scripts and globals (thaw_bmx.ns, tools/pipeline/thaw_bmx/convert.ts).
constexpr const char* kPhysics = "thaw_bicycle_physics";
constexpr const char* kToggleScript = "thaw_bmx_toggle";
constexpr const char* kAfterResetScript = "thaw_bmx_after_reset";
constexpr const char* kSkeletonScript = "thaw_bmx_load_skeleton";
constexpr const char* kRiderScript = "animload_thaw_bmx_rider";
constexpr const char* kBikeScript = "animload_thaw_bmx_bike";
// What those load (an animation's asset is its descChecksum plus the reference checksum it's loaded under).
constexpr const char* kSkeleton = "THAW_SI_BMX";
constexpr const char* kRiderReference = "THPS6_human";
constexpr const char* kTestAnim = "bmx_idle";

// THAW's bike members (its core physics table, THAW 0x00732368) and ours.
constexpr auto kMembers = std::to_array<const char*>({
    "SkaterOnBike",
    "SkaterOffBike",
    "IsSkaterOnBike",
    "BikeIsInOverGrind",
    "GetBikeManualAngle",
    "ResetBikeManualAngle",
    "BikeInManual",
    "BikeInXup",
    "BikeEnterXup",
    "BikeExitXup",
    "BikeInTireTap",
    "BikeEnterSkid",
    "BikeExitSkid",
    "bikeinskid",
    "BikeCanSkid",
    "BikeExitTireTap",
    "exitbikeendo",
    "EnterWheelieFromAir",
    "ThawBmxLoadAssets",
    // (phase 4: the other THAW-only members THAW's bike scripts ask - thaw_bmx_tricks.cpp call_trick_member)
    "DisplayPitchGreaterThan",
    "LandedFromTireTap",
    "InTireTap",
    "PitchingForward",
    "PitchingBackward",
    "ShouldRunStallScript",
    "ClearStallScriptFlag",
    "ExitStallBoost",
    "OnStall",
    // (ours: bike_playturnanim's turn animation on / off - thaw_bmx_physics.cpp steer_anim)
    "ThawBmxSteerAnim",
    // (THAW's CameraLookAround members - the d-pad turns the camera on the bike: camera_lookaround.cpp, answered only
    // while the feature is on)
    "LookaroundOnDPad_On",
    "LookaroundOnDPad_Off",
});

std::unordered_map<const void*, BikeState>& get_states()
{
    static std::unordered_map<const void*, BikeState> states;
    return states;
}

bool run_if_there(const char* name, objects::Entity* object)
{
    const uint32_t checksum = name_hash(name);
    if (!script::script_exists(checksum)) {
        get_log("thaw_bmx: the script %s isn't loaded - make the overlay with "
                "tools/pipeline/menu_level/make_menu_level.ts after "
                "tools/pipeline/thaw_bmx/convert.ts",
                name);
        return false;
    }
    script::launch_script(checksum, nullptr, object, false, name);
    return true;
}

// The bike's skeleton and both animation sets, loaded once for good (permanent, on the bottom-up heap as the game's
// own start-up loads): LoadAnim skips one already there, LoadSkeleton wouldn't - each set only when missing.
bool load_assets()
{
    assets::ResourceRegistry* assets = assets::asset_manager();
    if (!assets)
        return false;
    const uint32_t skeleton = name_hash(kSkeleton);
    const uint32_t rider = name_hash(kTestAnim) + name_hash(kRiderReference);
    const uint32_t bike = name_hash(kTestAnim) + name_hash(kSkeleton);
    if (assets->asset_exists(skeleton) && assets->asset_exists(rider) && assets->asset_exists(bike))
        return true;
    const bool permanent = assets->get_permanent_by_default();
    assets->set_permanent_by_default(true);
    memory::enter_heap_scope(memory::bottom_up_heap());
    bool ok = true;
    if (!assets->asset_exists(skeleton))
        ok &= run_if_there(kSkeletonScript, nullptr);
    if (!assets->asset_exists(rider))
        ok &= run_if_there(kRiderScript, nullptr);
    if (!assets->asset_exists(bike))
        ok &= run_if_there(kBikeScript, nullptr);
    memory::leave_heap_scope();
    assets->set_permanent_by_default(permanent);
    ok = ok && assets->asset_exists(skeleton) && assets->asset_exists(rider) && assets->asset_exists(bike);
    get_log("thaw_bmx: the bike's skeleton and animations %s", ok ? "loaded" : "NOT loaded (see above)");
    return ok;
}

// ThawBmx_Available: the feature on (and the overlay's scripts there) - the pause menu's RIDE BMX item.
bool cfunc_available(script::Struct*, script::Script*)
{
    return is_switched_on() && script::script_exists(name_hash(kToggleScript));
}

// ThawBmx_LoadTexture "<folder/name>": ingest_skin unless the texture is loaded already (THAW's bike manual meter
// script, create_bike_manual_meter, loads its converted pictures with it each time a manual starts - THAW has them
// loaded from start-up). True when the texture is there.
bool cfunc_load_texture(script::Struct* params, script::Script* script)
{
    const char* file = nullptr;
    if (!params->fetch_text(0u, &file, 0) || !file)
        return false;
    return commands::texture_loaded(file) || commands::cfunc_load_texture(params, script);
}

int g_key = SDL_SCANCODE_F7;
bool g_key_up = true;

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "thaw_bmx", 0) != 0;
    return on;
}

BikeState* state_of(const SkaterPhysicsModule* core, bool create)
{
    auto& states = get_states();
    auto it = states.find(core);
    if (it != states.end())
        return &it->second;
    if (!create)
        return nullptr;
    return &states[core];
}

void get_forget(const SkaterPhysicsModule* core) { get_states().erase(core); }

bool on_bike(const SkaterPhysicsModule* core)
{
    const BikeState* state = core ? state_of(core, false) : nullptr;
    return state && state->on;
}

uint32_t bike_skater_script(uint32_t name, uint32_t caller_name, bool bike_script_loaded)
{
    static const uint32_t kPedKnockDown = name_hash("PedKnockDown");
    static const uint32_t kBikeKnockDown = name_hash("bike_PedKnockDown");
    static const uint32_t kKnockDownState = name_hash("STATECONTROL_knockdown");
    if (name != kPedKnockDown)
        return name;
    if (caller_name == kKnockDownState)
        return 0;
    return bike_script_loaded ? kBikeKnockDown : name;
}

uint32_t skater_script(const objects::Rider* skater, uint32_t name, const script::Script* caller)
{
    if (!is_switched_on() || !skater || !on_bike(skater->core_physics_))
        return name;
    static const uint32_t kBikeKnockDown = name_hash("bike_PedKnockDown");
    const uint32_t chosen =
        bike_skater_script(name, caller ? caller->script_title_ : 0, script::script_exists(kBikeKnockDown));
    if (chosen == 0)
        get_log("thaw_bmx: a ped's knock-down on the bike - not knocked over (THAW's STATESCRIPT_knockdown)");
    else if (chosen != name)
        get_log("thaw_bmx: knocked over by a ped on the bike - bike_PedKnockDown");
    return chosen;
}

bool half_stick_key()
{
    if (!is_switched_on())
        return false;
    for (const auto& entry : get_states())
        if (entry.second.on)
            return true;
    return false;
}

script::Struct* physics_structure(const SkaterPhysicsModule* core)
{
    static int s_logged = -1; // (what was logged last: -1 nothing, 0 the skater's, 1 the bike's, 2 the bike's missing)
    if (!on_bike(core)) {
        if (s_logged > 0) {
            get_log("thaw_bmx: off the bike - the skater's physics again");
            s_logged = 0;
        }
        return nullptr;
    }
    script::Struct* physics = script::read_sub_struct(name_hash(kPhysics));
    const int now = physics ? 1 : 2;
    if (now != s_logged) {
        if (physics)
            get_log("thaw_bmx: on the bike - %s in use", kPhysics);
        else
            get_log("thaw_bmx: on the bike, but %s isn't loaded (the overlay's specialitems.qb) - the skater's physics",
                    kPhysics);
        s_logged = now;
    }
    return physics;
}

void set_wheel_points(BikeState& state, const maths::Vec4& position, const maths::Vec4& facing)
{
    // (THAW: the front one along the facing, the rear one back; w as the sum's)
    state.front_wheel_ = {.x = position.x + facing.x * kWheelOffset, .y = position.y + facing.y * kWheelOffset,
                          .z = position.z + facing.z * kWheelOffset, .w = position.w + facing.w * kWheelOffset};
    state.rear_wheel_ = {.x = position.x - facing.x * kWheelOffset, .y = position.y - facing.y * kWheelOffset,
                         .z = position.z - facing.z * kWheelOffset, .w = position.w - facing.w * kWheelOffset};
}

int invoke_method_by_name(SkaterPhysicsModule* core, uint32_t name, script::Struct* params, script::Script* script)
{
    // (the names THAW's members have; ThawBmxLoadAssets is ours)
    static const uint32_t kOn = name_hash("SkaterOnBike"), kOff = name_hash("SkaterOffBike"),
                          kIsOn = name_hash("IsSkaterOnBike"), kOverGrind = name_hash("BikeIsInOverGrind"),
                          kGetAngle = name_hash("GetBikeManualAngle"), kResetAngle = name_hash("ResetBikeManualAngle"),
                          kInManual = name_hash("BikeInManual"), kInXup = name_hash("BikeInXup"),
                          kEnterXup = name_hash("BikeEnterXup"), kExitXup = name_hash("BikeExitXup"),
                          kInTireTap = name_hash("BikeInTireTap"), kEnterSkid = name_hash("BikeEnterSkid"),
                          kExitSkid = name_hash("BikeExitSkid"), kInSkid = name_hash("bikeinskid"),
                          kCanSkid = name_hash("BikeCanSkid"), kExitTireTap = name_hash("BikeExitTireTap"),
                          kExitEndo = name_hash("exitbikeendo"), kLoad = name_hash("ThawBmxLoadAssets"),
                          kEnterWheelie = name_hash("EnterWheelieFromAir"), kManualAngle = name_hash("manual_angle"),
                          kNose = name_hash("nose");
    auto nose = [&](bool set) {
        if (set && script && script->args_)
            script->args_->put_int(kNose, 1);
    };
    if (name == kIsOn)
        return on_bike(core) ? 1 : 0;
    if (name == kLoad)
        return load_assets() ? 1 : 0;
    if (name == kEnterWheelie) // (THAW 0x005CA3B0: phase 3, thaw_bmx_physics.cpp)
        return enter_wheelie_from_air(core, params);
    static const uint32_t kSteerAnim = name_hash("ThawBmxSteerAnim"), kOffFlag = name_hash("off");
    if (name == kSteerAnim) { // (ours: `on` / `off`)
        set_steer_anim(core, !(params && params->has_bare_flag(kOffFlag)));
        return 1;
    }
    // (the rest read or change the state; one is made on first use)
    const bool known = name == kOn || name == kOff || name == kOverGrind || name == kGetAngle || name == kResetAngle ||
                       name == kInManual || name == kInXup || name == kEnterXup || name == kExitXup ||
                       name == kInTireTap || name == kEnterSkid || name == kExitSkid || name == kInSkid ||
                       name == kCanSkid || name == kExitTireTap || name == kExitEndo;
    if (!known)
        return call_trick_member(core, name, params, script); // (phase 4's: thaw_bmx_tricks.cpp)
    BikeState& state = *state_of(core, true);
    if (name == kOn) { // 0x005C98E0
        state.on = true;
        const objects::GameEntity* skater = core->entity_;
        set_wheel_points(state, skater->at_point_, core::at(skater->basis_frame_.row, 2));
        return 1;
    }
    if (name == kOff) { // 0x005C74C0 (THAW clears only the flag; ours the bike's other states too)
        steer_anim_off(core);
        flip_anims_to_flag(core);
        BikeState fresh;
        fresh.front_wheel_ = state.front_wheel_;
        fresh.rear_wheel_ = state.rear_wheel_;
        state = fresh;
        return 1;
    }
    if (name == kOverGrind) // 0x005C74E0
        return state.over_grind_ ? 1 : 0;
    if (name == kGetAngle) { // 0x005C74F0: manual_angle into the script's parameters
        if (script && script->args_)
            script->args_->push_float(kManualAngle, state.manual_angle_);
        return 1;
    }
    if (name == kResetAngle) { // 0x005C7510
        state.manual_angle_ = kManualAngleReset;
        return 1;
    }
    if (name == kInManual) { // 0x005C7530: `nose` for a nose manual
        nose(state.nose_manual_);
        return state.manual ? 1 : 0;
    }
    if (name == kInXup) // 0x005C7570
        return state.xup ? 1 : 0;
    if (name == kEnterXup) { // 0x005C7580 (THAW returns what was in AL; true here)
        state.xup = true;
        return 1;
    }
    if (name == kExitXup) { // 0x005C7590
        state.xup = false;
        return 1;
    }
    if (name == kInTireTap) { // 0x005C75F0
        nose(state.tire_tap_nose_);
        return state.tire_tap_ ? 1 : 0;
    }
    if (name == kEnterSkid) { // 0x005C75C0 (as BikeEnterXup)
        state.skid = true;
        return 1;
    }
    if (name == kExitSkid) { // 0x005C75D0
        state.skid = false;
        state.can_skid_ = false;
        state.skid_timer_ = kSkidTimerReset;
        return 1;
    }
    if (name == kInSkid) // 0x005C75A0
        return state.skid ? 1 : 0;
    if (name == kCanSkid) // 0x005C75B0
        return state.can_skid_ ? 1 : 0;
    if (name == kExitTireTap) { // 0x005C7630 (THAW also stamps its state component's tire tap time)
        state.tire_tap_ = false;
        state.tire_tap_nose_ = false;
        state.tap_ledge_ = false; // (THAW: +0x54D)
        return 1;
    }
    // exitbikeendo, 0x005C8260
    state.endo = false;
    return 1;
}

void register_script_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {.name = "ThawBmx_Available", .function = &cfunc_available},
        {.name = "ThawBmx_LoadTexture", .function = &cfunc_load_texture},
    });
    script::install_script_functions(kFunctions);
    if (!is_switched_on())
        return;
    script::publish_method_table(kMembers);
    register_parked_functions(); // (the parked bikes: thaw_bmx_parked.h)
    render::set_shadow_companion(&shadow_companion);
    std::array<char, 64> setting = {""};
    build_info::fetch_text(build_info::kKeybinds, "ThawBmx", "", setting.data(), sizeof(setting));
    auto by_name = [](const std::string& text) { return static_cast<int>(SDL_GetScancodeFromName(text.c_str())); };
    g_key = sys::screenshot::parse_key(setting.data(), SDL_SCANCODE_F7, by_name);
    get_log("thaw_bmx: on - %s gets on / off THAW's BMX in a level (also the pause menu, or the script %s)",
            SDL_GetScancodeName(static_cast<SDL_Scancode>(g_key)), kToggleScript);
}

void on_skater_reset(objects::Rider* skater)
{
    BikeState* state = state_of(skater->core_physics_, false);
    if (!state || !state->on)
        return;
    // The bike (a special item) is gone: off it.
    get_log("thaw_bmx: skater reset on the bike - off it");
    steer_anim_off(skater->core_physics_);
    flip_anims_to_flag(skater->core_physics_);
    *state = BikeState{};
    run_if_there(kAfterResetScript, skater);
}

void tick_frame(modules::FrontEnd* front_end)
{
    if (!is_switched_on())
        return;
    update_parked(front_end); // (the level's bike: thaw_bmx_parked.h)
    // Left for the front end (no level) with the bike's d-pad camera on - no bike script turned it off: the menus get
    // the stick back (bike_start's SetAnalogStickActiveForMenus 0 undone).
    if (d_pad_camera_noted() && render::current_level() == 0 && front_end) {
        front_end->set_analog_stick_active_for_menus(true);
        note_d_pad_camera(false);
        get_log("thaw_bmx: no level with the bike's d-pad camera on - the stick moves through the menus again");
    }
    const platform::KeyState keys = platform::keyboard_keys();
    const bool down = !keys.empty() && g_key > 0 && g_key < SDL_NUM_SCANCODES && core::at(keys, g_key);
    const bool pressed = down && g_key_up;
    g_key_up = !down;
    if (!pressed || frame_loop::frames_run() == 0 || frontend::level_editor::live_now() ||
        (front_end && front_end->is_halted_) || render::current_level() == 0)
        return;
    run_if_there(kToggleScript, nullptr);
}

} // namespace skater::thaw_bmx

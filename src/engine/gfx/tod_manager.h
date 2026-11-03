#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <gsl/pointers>

// The time-of-day manager (the TODManager_* script commands): a career level's day passing through four states -
// morning, afternoon, evening, night - each a set of lighting parameters (the level's light colour, sky, fog,
// ambient and the two directional lights, the sun, bloom) that the manager blends between while time passes, and
// pushes to the engine: the scenes' and objects' colours (a few light groups and a part of the level's sectors a
// frame, so the change is spread over frames), the light manager, fog, bloom and the sun. A script (the level's
// TODManager_SetParams) gives each state's parameters and lengths; scripts are told when a state changes
// (tod_manager_state_transition).
//
// The singleton (0x007CE490, references 0x007CE494) is made by sys::Main and updated by modules::Skate (0x00567710).
// From the disassembly of 0x00571140-0x00574910 (methods __thiscall, RET n = 4 * arguments; the helpers that don't
// use the manager __cdecl).
namespace script {
class Script;
class Struct;
} // namespace script
namespace lists {
class KeyMap;
}

namespace gfx {

inline constexpr float kHalf{0.5F};

// One state's lighting (the parameters of a state's structure in the level's script, by the names below): 0xA4
// bytes, passed by value between the manager's methods.
struct TodParams {
    int bloom_on_;            // +0x00 bloom_on (the bloom parameters are read only when it's on)
    int bloom1;               // +0x04 bloom1
    int bloom2;               // +0x08 bloom2
    uint32_t bloom_strength_; // +0x0C bloom_strength [r g b]: bytes r, g, b, 0
    uint32_t bloom_color_;    // +0x10 bloom_color [r g b]
    float sun_theta_;         // +0x14 sun_theta (degrees in the script, kept in radians)
    float sun_phi_;           // +0x18 sun_phi
    float sun_red_;           // +0x1C sun_red (255 if not given)
    float sun_blue_;          // +0x20 sun_blue
    float sun_green_;         // +0x24 sun_green
    int lev_red_;             // +0x28 lev_red: the level's (scenes' and objects') colour
    int lev_green_;           // +0x2C lev_green
    int lev_blue_;            // +0x30 lev_blue
    int sky_red_;             // +0x34 sky_red: the sky scene's colour
    int sky_green_;           // +0x38 sky_green
    int sky_blue_;            // +0x3C sky_blue
    int fog_on_;              // +0x40 fog_on (never read from a script; given back by GetCurrentTODParams)
    int fog_red_;             // +0x44 fog_red
    int fog_green_;           // +0x48 fog_green
    int fog_blue_;            // +0x4C fog_blue
    int fog_alpha_;           // +0x50 fog_alpha
    int fog_dist_;            // +0x54 fog_dist
    int ambient_red_;         // +0x58 ambient_red
    int ambient_green_;       // +0x5C ambient_green
    int ambient_blue_;        // +0x60 ambient_blue
    float ambient_mod_lo_;    // +0x64 ambient_mod_factor_lo
    float ambient_mod_hi_;    // +0x68 ambient_mod_factor_hi
    float heading0;           // +0x6C heading_0: directional light 0 (degrees)
    float pitch0;             // +0x70 pitch_0
    int red0;                 // +0x74 red_0
    int green0;               // +0x78 green_0
    int blue0;                // +0x7C blue_0
    float mod_lo0_;           // +0x80 mod_factor_0_lo
    float mod_hi0_;           // +0x84 mod_factor_0_hi
    float heading_a_;         // +0x88 heading_1: directional light 1
    float pitch1;             // +0x8C pitch_1
    int red1;                 // +0x90 red_1
    int green1;               // +0x94 green_1
    int blue1;                // +0x98 blue_1
    float mod_lo1_;           // +0x9C mod_factor_1_lo
    float mod_hi1_;           // +0xA0 mod_factor_1_hi
};
constexpr std::size_t kTodParamsBytes = 0xA4;
constexpr std::size_t kTodParamsLevRedOffset = 0x28;
constexpr std::size_t kTodParamsFogOnOffset = 0x40;
constexpr std::size_t kTodParamsHeading0Offset = 0x6C;
constexpr std::size_t kTodParamsModHi1Offset = 0xA0;

static_assert(sizeof(TodParams) == kTodParamsBytes && offsetof(TodParams, lev_red_) == kTodParamsLevRedOffset &&
                  offsetof(TodParams, fog_on_) == kTodParamsFogOnOffset && offsetof(TodParams, heading0) == kTodParamsHeading0Offset &&
                  offsetof(TodParams, mod_hi1_) == kTodParamsModHi1Offset,
              "layout must match the original");

// The bloom parameters as the manager passes them on (the start of TODParams; also the temporary ones).
struct BloomParams {
    int on;
    int bloom1;
    int bloom2;
    uint32_t strength;
    uint32_t color;
};
constexpr std::size_t kBloomParamsBytes = 0x14;

static_assert(sizeof(BloomParams) == kBloomParamsBytes, "the first five words of TODParams");

// The states' checksums, in the order the day goes.
constexpr uint32_t kMorning = 0x4615C1C9;   // morning
constexpr uint32_t kAfternoon = 0x577B39EE; // afternoon
constexpr uint32_t kEvening = 0x83E3976F;   // evening
constexpr uint32_t kNight = 0xEE25F068;     // night
// The state after / before `state` (0 for anything else).
uint32_t next_tod_state(uint32_t state);
uint32_t previous_tod_state(uint32_t state);

// One "system" (the script's system=1 or 2): its own day - states, lengths, light groups. A sky system colours
// only the sky scene and doesn't drive the lights, fog, bloom or the scripts.
struct TodSystem {
    TodParams current;           // +0x000 (manager +0x008) the state's parameters
    TodParams next;              // +0x0A4 (+0x0AC) the next state's
    script::Struct* params;      // +0x148 (+0x150) a copy of the system's TODManager_SetParams parameters
    int time;                    // +0x14C (+0x154) milliseconds into the state, then into the transition
    uint32_t state;              // +0x150 (+0x158) the current state
    uint32_t next_state_;        // +0x154 (+0x15C)
    int frame;                   // +0x158 (+0x160) frames since this group's last part was coloured
    int wait_frames_;            // +0x15C (+0x164) the group's wait_frames: frames between its parts
    int group;                   // +0x160 (+0x168) the light group being coloured (an index into groups)
    int group_count_;            // +0x164 (+0x16C) how many groups there are
    int part;                    // +0x168 (+0x170) the part of the group being coloured
    int parts;                   // +0x16C (+0x174) the group's parts: each colours (100 / parts)% of the sectors
    uint32_t light_group_;       // +0x170 (+0x178) the group's light_group
    uint32_t reset_light_group_; // +0x174 (+0x17C) its reset_light_group (sectors set back to neutral)
    int transition_length_;      // +0x178 (+0x180) transition_length (ms)
    int state_length_;           // +0x17C (+0x184) the state's state_length (ms)
    int days_passed_;            // +0x180 (+0x188) nights ended
    bool transitioning;          // +0x184 (+0x18C) blending towards the next state (time counts the blend)
    bool script_run_;            // +0x185 (+0x18D) tod_manager_state_transition run for this transition
    bool sky;                    // +0x186 (+0x18E) a sky system
    uint8_t pad187;
};
constexpr std::size_t kTodSystemBytes = 0x188;
constexpr std::size_t kTodSystemParamsOffset = 0x148;
constexpr std::size_t kTodSystemTransitionLengthOffset = 0x178;
constexpr std::size_t kTodSystemSkyOffset = 0x186;

static_assert(sizeof(TodSystem) == kTodSystemBytes && offsetof(TodSystem, params) == kTodSystemParamsOffset &&
                  offsetof(TodSystem, transition_length_) == kTodSystemTransitionLengthOffset && offsetof(TodSystem, sky) == kTodSystemSkyOffset,
              "layout must match the original");

constexpr std::size_t kCtodManagerSystemsOffset = 8;
constexpr std::size_t kCtodManagerTempBloomOffset = 0x318;
constexpr std::size_t kCtodManagerTimeMultiplierOffset = 0x330;
constexpr std::size_t kCtodManagerParamsSetOffset = 0x33C;
constexpr std::size_t kCtodManagerBytes = 0x340;

class CtodManager {
public:
    static constexpr int kSystems = 2;

    // 0x00573620: the constructor: the dead-objects table, the structures, then Reset.
    CtodManager();
    // 0x00573770: the destructor body.
    ~CtodManager();
    CtodManager(const CtodManager&) = delete;
    CtodManager& operator=(const CtodManager&) = delete;
    // 0x005730E0: back to the defaults: afternoon, states of 60 s, transitions of 30 s, active, fog on, time
    // multiplier 1; the systems' parameters and the dead-objects table emptied.
    void get_reset();

    // 0x00574590: a frame (modules::Skate's update): in a career game, unless the script variable
    // tod_manager_inactive is 1, a temporary state is up, the manager is inactive or the front end is paused, time
    // moves on (the frame's length times the multiplier) and each system is updated.
    void tick_frame();
    // 0x00573810: system `index`'s frame while transitioning: the next part of the current light group coloured
    // with the blended parameters (when its wait is over), the lights / fog / bloom set, the transition script
    // run half-way (at once for evening), and the next state taken when the transition ends.
    void update_system(int index);
    // 0x005723C0: system `index` moves on to its next state (a night ending counts a day).
    void next_state(int index);
    // 0x00571290: system `index`'s light group number `group` read from its params' groups array.
    void read_group(int index);
    // 0x00573CE0: every system's light groups coloured at once with its current parameters, the lights set, and
    // tod_manager_state_transition run with instant_change (and temp_state with `temp_state`).
    void refresh_all(bool temp_state);
    // 0x00574670: jump to `state` now (time is moved on through the states in between).
    void set_tod_instant(uint32_t state);

    // 0x00572130: TODManager_SetParams system=1|2 current_state=... <state structures> transition_length=...
    // [overwrite_current_state={...}] [sky].
    void change_args(script::Struct* params);
    // 0x00571A00: the parameters in `structure` read into `out` (missing ones are left as they are).
    static void parse_params(script::Struct* structure, TodParams* out);
    // 0x00571340: `a` blended towards `b` by `t` (integers truncated). Fields +0x14..+0x24 and fog_on aren't set
    // by the original (stack garbage): here they're a's.
    static TodParams get_lerp(TodParams a, TodParams b, float t);
    // 0x00571E00: system `index`'s parameters now (blended while transitioning); -1: the first non-sky system.
    TodParams get_tod_params(int index);
    // 0x00571140: the current level colour (0x00BBGGRR) of the first non-sky system (GameObjects::CreateFromStructure
    // colours new objects with it).
    void get_level_color(uint32_t* out);
    // 0x00572A10: the parameters sent to the engine: ambient light, the two directional lights, fog (if on), the
    // level colour.
    void apply_tod_params(TodParams params) const;
    // 0x005724D0: the sun (theta, phi, colour, shown unless it's night), blended through system 0's day.
    void update_sun();
    // 0x005727F0 (0x00572821 is the part after its first test): colour the scenes (`start_percent`..`end_percent` of
    // their sectors), the level scene and (for the outdoor group or no group) the models; or only the sky scene.
    static void apply_color(uint32_t color, uint32_t sky_color, bool sky, uint32_t light_group,
                            uint32_t reset_light_group, bool part_only, int start_percent, int end_percent);
    // 0x00573500: bloom on (the parameters) or off.
    static void set_bloom(int on, int bloom1, int bloom2, uint32_t strength, uint32_t color);
    // 0x00572320: fog on (with the current fog colour and distance) or off.
    void set_fog_active(bool on);

    // 0x00571EF0: TODManager_Create name=<node> | prefix=<prefix>: the nodes' objects made (Obj's
    // create_from_node), except those marked permanently dead.
    void build_tod_objects(script::Struct* params, script::Script* script);
    // 0x00573400: TODManager_MarkPermanentlyDead name=<node> | prefix=<prefix>.
    void mark_permanently_dead(script::Struct* params);
    // 0x00572BD0: the dead-objects table emptied (level clean-up).
    void clear_dead_objects();
    // 0x005732D0: SetSceneColor's colouring: color=, [sky=], light_group=, reset_light_group= ...
    static void set_scene_color(script::Struct* params);
    // 0x00573BE0: TODManager_ResetLevelLights [state_name=]: the lights set from the current parameters (no name)
    // or from the named state's.
    void reset_level_lights(uint32_t state_name);
    // 0x005741B0 / 0x00574210: TODManager_SetTempTOD <params> / TODManager_RestoreCurrentTOD.
    void set_temp_tod(script::Struct* params);
    void restore_current_tod();
    // 0x00574270 / 0x00574400: TODManager_SetTempBloomParams / TODManager_EnableBloom.
    void set_temp_bloom_params(script::Struct* params);
    void enable_bloom(int on);

    // NOT ORIGINAL (THUGPRO's time of day in network games - engine/net/thugpro_peer.h): the first non-sky system's
    // state (0 if both are sky systems), and whether set_tod_instant(state) can run - it loops for ever unless the
    // current state and `state` are among the four, and reads each state's structure on the way.
    [[nodiscard]] uint32_t current_state() const;
    [[nodiscard]] bool can_set_tod_instant(uint32_t state) const;

    // The fields other code reads and sets.
    std::array<TodSystem, kSystems>& systems() { return systems_; }
    [[nodiscard]] const std::array<TodSystem, kSystems>& systems() const { return systems_; }
    [[nodiscard]] float time_multiplier() const { return time_multiplier_; }
    void set_time_multiplier(float value) { time_multiplier_ = value; }
    void set_transition_script(uint32_t value) { transition_script_ = value; }
    [[nodiscard]] bool fog_active() const { return fog_active_; }
    void assign_fog_active(bool value) { fog_active_ = value; }
    [[nodiscard]] bool level_lights_reset() const { return level_lights_reset_; }
    [[nodiscard]] bool active() const { return active_; }
    void set_active(bool value) { active_ = value; }
    [[nodiscard]] bool params_set() const { return params_set_; }

private:
    lists::KeyMap* dead_objects_; // +0x000 nodes marked permanently dead (value: the manager)
    gsl::owner<script::Struct*> unknown4_; // +0x004 made, emptied and freed, never filled
    std::array<TodSystem, kSystems> systems_{}; // +0x008
    BloomParams temp_bloom_{};      // +0x318 TODManager_SetTempBloomParams'
    int full_length_{};             // +0x32C milliseconds the manager has run
    float time_multiplier_{};       // +0x330 TODManager_SetTimeMultiplier
    uint32_t transition_script_{};  // +0x334 TODManager_SetTODScript: level_specific_transition_script
    bool fog_active_{};             // +0x338
    bool level_lights_reset_{};     // +0x339 reset_level_lights with a state: the lights aren't blended
    bool temp_tod_{};               // +0x33A a temporary state is up (time stops)
    bool active_{};                 // +0x33B TODManager_SetActive
    bool params_set_{};             // +0x33C
    std::array<uint8_t, 3> pad33d_{};

    // The first system that isn't a sky system (index 2 - past the end - if both are; never checked).
    [[nodiscard]] int current_system() const;
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(CtodManager, systems_) == kCtodManagerSystemsOffset && offsetof(CtodManager, temp_bloom_) == kCtodManagerTempBloomOffset &&
                          offsetof(CtodManager, time_multiplier_) == kCtodManagerTimeMultiplierOffset && offsetof(CtodManager, params_set_) == kCtodManagerParamsSetOffset &&
                          sizeof(CtodManager) == kCtodManagerBytes,
                      "layout must match the original (0x00574540 allocates 0x340 bytes)");
    }
};

// 0x005725F0: `color` given to `scene`'s sectors (the scene's sector table: those of `lightGroup` - and those of
// `resetLightGroup` set back to neutral - or every sector for no group), only start_percent..end_percent of them
// when start_percent >= 0.
void apply_color_to_scene(uint8_t* scene, uint32_t color, uint32_t light_group, uint32_t reset_light_group,
                          int start_percent, int end_percent);
// A light's heading and pitch.
struct LightAngles {
    float heading{};
    float pitch{};
};

// 0x00572980: directional light `index` pointed by heading and pitch (degrees).
void set_light_direction(int index, LightAngles angles);
// 0x00573180: the bloom parameters added to `out` (on, bloom1, bloom2, strength, color).
void add_bloom_params(BloomParams bloom, script::Struct* out);

// NOT ORIGINAL - an override of what the manager sends the engine (the level editor's level properties: an edited
// level's own ambient light and fog - level_editor_level_props.h). When set, apply_tod_params and set_fog_active pass the
// parameters through it first: it changes the ones it overrides and returns true when the fog must be on (its fog
// shown even where the manager's fog is off). Null (the default): the original behaviour.
using TodParamsFilter = bool (*)(TodParams* params);
void set_tod_params_filter(TodParamsFilter filter);

// NOT ORIGINAL - THUGPRO's time of day in network games (engine/net/thugpro_peer.h): in a multiplayer game the original
// leaves the lights as they are - reset_level_lights, set_tod_instant and enable_bloom do nothing. THUGPRO's DLL lifts
// that (its 4 byte patches at 0x00573BF8, 0x00574683, 0x00574410; its 4th, 0x00574375, makes set_temp_bloom_params
// always take the multiplayer branch - what it already does in a multiplayer game). When the unlock is set and says
// true, those three run in a multiplayer game too. Null (the default): the original behaviour.
using MultiplayerUnlock = bool (*)();
void set_multiplayer_unlock(MultiplayerUnlock unlock) noexcept;
MultiplayerUnlock get_multiplayer_unlock();
// What those three test: a multiplayer game (0x005651F0) not unlocked.
bool tod_lights_locked();

// The singleton (0x007CE490).
CtodManager*& tod_manager();
// 0x00574540: the singleton (made zeroed with `create` if there's none); a reference taken.
CtodManager* create_tod_manager(bool create);
// 0x00574910: a reference dropped; the last one deletes the manager.
void release_tod_manager();

// The script commands (0x00571FE0-0x00574870).
bool cfunc_tod_manager_set_active(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_get_number_of_days_passed(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_set_time_multiplier(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_get_time_multiplier(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_set_tod_script(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_create(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_set_params(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_get_current_tod_params(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_set_fog_active(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_mark_permanently_dead(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_reset(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_reset_level_lights(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_set_temp_tod(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_restore_current_tod(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_set_temp_bloom_params(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_enable_bloom(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_set_tod_instant(script::Struct* params, script::Script* script);
bool cfunc_tod_manager_advance_tod_state(script::Struct* params, script::Script* script);

inline constexpr float kThousand{1000.0F};
inline constexpr float kHundred{100.0F};
inline constexpr float kTwoTo32{4294967300.0F};
inline constexpr float kOneOver255{0.003921569F}; // 0x00645D70: 1 / 255

} // namespace gfx

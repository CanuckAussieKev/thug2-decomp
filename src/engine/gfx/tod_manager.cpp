// The time-of-day manager and its script commands. See tod_manager.h.

#include "engine/gfx/tod_manager.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/sector.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/park_editing/park_editor.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/lens_flare.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/screen_effects.h"
#include "engine/lists/key_map.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/model_component.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/script/node_prefixes.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/gfx/cag_cfuncs.h"
#include "engine/objects/anim_movies.h"
#include "engine/gfx/movie.h"
#include "engine/objects/particle_component.h"
#include "engine/gfx/engine_glue.h"
#include "frontend/front_end.h"

namespace gfx {

uint32_t g_tod_references;

float g_two_to32{4294967300.0f};
float g_half{0.5f};
float g_thousand{1000.0f};
float g_one_over255{0.003921569f};
float g_hundred{100.0f};

namespace {

// Names.
constexpr uint32_t kName = 0xA1DC81F9;                  // name
constexpr uint32_t kPrefix = 0x6C4E7971;                // prefix
constexpr uint32_t kNoPrefixWarning = 0x80804991;       // NoPrefixWarning
constexpr uint32_t kSystem = 0x36B2EE74;                // system
constexpr uint32_t kSystemIndex = 0x2B430364;           // system_index
constexpr uint32_t kCurrentState = 0xF8725A00;          // current_state
constexpr uint32_t kOverwriteCurrentState = 0x6136E863; // overwrite_current_state
constexpr uint32_t kStateLength = 0xF746BEE2;           // state_length
constexpr uint32_t kTransitionLength = 0x37BC278D;      // transition_length
constexpr uint32_t kSky = 0xF9D98B10;                   // sky
constexpr uint32_t kGroups = 0x0F92C68F;                // groups
constexpr uint32_t kParts = 0x96BF5801;                 // parts
constexpr uint32_t kWaitFrames = 0x3FAF7F7F;            // wait_frames
constexpr uint32_t kLightGroup = 0x9655CFF7;            // light_group
constexpr uint32_t kResetLightGroup = 0xEC50037D;       // reset_light_group
constexpr uint32_t kNumDaysPassed = 0x49EF4348;         // num_days_passed
constexpr uint32_t kTimeMultiplierName = 0x1868AFB2;    // TOD_Time_Multiplier
constexpr uint32_t kActive = 0xB4E103FD;                // active
constexpr uint32_t kCurrentStateName = 0xEA84C14D;      // current_state_name
constexpr uint32_t kTodParams = 0xDF3D6451;             // tod_params
constexpr uint32_t kStateName = 0x353F083F;             // state_name
constexpr uint32_t kTempTodChanged = 0x4AF50A5B;        // todmanager_temp_tod_changed (a script global)
constexpr uint32_t kInactive = 0xAAE2353A;              // tod_manager_inactive (a script global)
constexpr uint32_t kCareer = 0x4DA4937B;                // career
constexpr uint32_t kCurrentTod = 0xF200CB7E;            // current_tod
constexpr uint32_t kNextTod = 0x053D35AF;               // next_tod
constexpr uint32_t kTodStateParams = 0x48F1380F;        // tod_state_params
constexpr uint32_t kTransitionScript = 0x37D7E331;      // level_specific_transition_script
constexpr uint32_t kStateTransition = 0xD99EE043;       // tod_manager_state_transition (a script)
constexpr uint32_t kInstantChange = 0x43633E05;         // instant_change
constexpr uint32_t kTempState = 0xCACC8C05;             // temp_state
constexpr uint32_t kBloomParams = 0x19C45D14;           // bloom_params
constexpr uint32_t kOn = 0xF649D637;                    // on
constexpr uint32_t kBloom1 = 0xF68014D9;                // bloom1
constexpr uint32_t kBloom2 = 0x6F894563;                // bloom2
constexpr uint32_t kStrength = 0x29461E27;              // strength
constexpr uint32_t kColor = 0x99A9B716;                 // color
constexpr uint32_t kOutdoor = 0xE3714DC1;               // outdoor (the light group of everything outside)
constexpr uint32_t kModel = 0x286A8D26;                 // model (the component type)
// SetSceneColor's other parameters (names not recovered).
constexpr uint32_t kSceneColorFlag = 0xE5B6C568;  // the sectors only (not the scenes' own colour, the models)
constexpr uint32_t kSceneColorStart = 0xDF1A695F; // the first percent of the sectors
constexpr uint32_t kSceneColorEnd = 0x8F639F94;   // the last percent

// The parameters' names, in the order of TODParams from +0x28 (fog_on, +0x40, isn't read from a script).
constexpr uint32_t kLevRed = 0x966014FF, kLevGreen = 0x5F727131, kLevBlue = 0x1C5D554E;
constexpr uint32_t kSkyRed = 0xBFAC0E79, kSkyGreen = 0xCFC99E4A, kSkyBlue = 0x18AFBF41;
constexpr uint32_t kFogOn = 0x028CCF87, kFogRed = 0xCE0BD6E5, kFogGreen = 0x9A35701D, kFogBlue = 0xE167C7F6;
constexpr uint32_t kFogAlpha = 0x9A4FA756, kFogDist = 0xFE2DDDB5;
constexpr uint32_t kAmbientRed = 0xED664897, kAmbientGreen = 0x908BF647, kAmbientBlue = 0x5F4FBA78;
constexpr uint32_t kAmbientModLo = 0xB3B5E2EF, kAmbientModHi = 0x3EBA82DE;
constexpr uint32_t kHeading0 = 0xC15B55B1, kPitch0 = 0xD9FF5B75;
constexpr uint32_t kRed0 = 0xE213AEB7, kGreen0 = 0x6EF19631, kBlue0 = 0x5EC44213;
constexpr uint32_t kModLo0 = 0x05999531, kModHi0 = 0x8896F500;
constexpr uint32_t kHeading1 = 0xB65C6527, kPitch1 = 0xAEF86BE3;
constexpr uint32_t kRed1 = 0x95149E21, kGreen1 = 0x19F6A6A7, kBlue1 = 0x29C37285;
constexpr uint32_t kModLo1 = 0xBD25F254, kModHi1 = 0x302A9265;
constexpr uint32_t kSunTheta = 0xDF037B3B, kSunPhi = 0x9C2BF09E;
constexpr uint32_t kSunRed = 0x54B02600, kSunGreen = 0x04811F13, kSunBlue = 0x319D6AF1;
constexpr uint32_t kBloomOn = 0x400681B6, kBloomStrength = 0x743D0048, kBloomColor = 0x6AC75185;

// A sector's colour set back to neutral (r, g, b 0x80, alpha 0x81).
constexpr uint32_t kNeutralColor = 0x81808080;

// The exe's constants.
float degree_to_radian_conv() { return commands::g_degree_to_radian_ratio; } // 0.017453292
float get_zero() { return objects::g_float_zero; }                           // 0.0
float one_percent() { return render::g_one_percent; }                        // 0.01
float one_over255() { return g_one_over255; }                                // 1 / 255
float two_to32() { return g_two_to32; }                // 4294967296: unsigned integers' correction
float get_one() { return objects::g_anim_movies_one; } // 1.0
float get_half() { return g_half; }                    // 0.5
float hundred() { return g_hundred; }                  // 100.0
float thousand() { return g_thousand; }                // 1000.0

// The C runtime's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// An unsigned millisecond count over an int length, as the original divides (FILD, + 2^32 if negative, FIDIV).
double get_fraction(int time, int length)
{
    auto value = static_cast<double>(time);
    if (time < 0)
        value += two_to32();
    return value / static_cast<double>(length);
}

// A colour from three bytes (and the fourth).
uint32_t get_rgba(int r, int g, int b, uint8_t a)
{
    return static_cast<uint8_t>(r) | static_cast<uint8_t>(g) << 8 | static_cast<uint8_t>(b) << 16 |
           static_cast<uint32_t>(a) << 24;
}
// A colour from a script array [r g b] (elements truncated to bytes; the fourth byte 0).
uint32_t color_from_array(script::Array* array)
{
    auto r = static_cast<uint8_t>(array->fetch_part(0));
    auto g = static_cast<uint8_t>(array->fetch_part(1));
    auto b = static_cast<uint8_t>(array->fetch_part(2));
    return get_rgba(r, g, b, 0);
}

script::Struct* spawn_param_table()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? new (memory) script::Struct : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

// The engine (not rewritten yet).
void change_fog(bool on) { render::engine::enable_fog(on); }
void set_fog_color(uint32_t color) { render::engine::set_fog_color(color); }
void set_fog_distance(float distance) { render::engine::set_fog_distance(distance); }
void change_ambient_tint(uint32_t color) { render::set_light_ambient_color(color); }
void change_light_tint(int index, uint32_t color) { render::set_light_diffuse_color(index, color); }
void set_ambient_modulation(float lo, float hi) { render::change_ambient_scale(lo, hi); }
void set_light_modulation(int index, float lo, float hi) { render::change_diffuse_scale(index, lo, hi); }
void set_light_direction_vector(int index, const float* direction)
{
    render::set_light_direction(index, reinterpret_cast<const maths::Vec4*>(direction));
}
// The sun: its parameters (theta +0x00, phi +0x04, colour +0x18 as r g b 128, shown +0x40) by mask.
struct SunParams {
    float theta;
    float phi;
    uint32_t unknown08[4];
    float color[4];
    uint32_t unknown28[6];
    bool shown;
    uint8_t pad41[0xF];
};
static_assert(offsetof(SunParams, color) == 0x18 && offsetof(SunParams, shown) == 0x40, "0x004426D0's layout");
constexpr int kSunAngles = 0x01 | 0x02, kSunColor = 0x10, kSunShown = 0x80;
void set_sun(const SunParams* sun, int mask)
{
    static_assert(offsetof(SunParams, shown) == offsetof(sun::Settings, shown),
                  "the sun's settings (SetSun reads 0x44 bytes)");
    sun::set_sun(reinterpret_cast<const sun::Settings*>(sun), static_cast<uint32_t>(mask));
}
// The level colour as the renderer's material colour (0x006FA7C4's, 0x006FA7C0's, and a global copy).
void set_level_color(const float* color)
{
    static_cast<render::EffectsDirector*>(reinterpret_cast<void*>(objects::g_particle_hub))->change_colour(color);
    static_cast<render::FlexibleParticleManager*>(render::g_flexible_particle_manager)->set_colour_scale(color);
    render::engine::set_level_color(color);
}
void set_bloom_structure(script::Struct* params) { render::g_screen_effects->set_bloom_params(params, 0); }
// Whether this is a multiplayer game (a network game, or more than one local player): the lights stay as set.
bool is_multiplayer() { return modules::Skate::is_multiplayer() & 1; }
// The game mode's name (modules::Skate's game mode 0x007CE470).
uint32_t active_ruleset_label()
{
    return static_cast<goals::MatchRules*>(reinterpret_cast<void*>(goals::g_game_mode))->get_name_key();
}
// The front end (0x007CE464): +0x30 paused.
bool is_game_halted() { return reinterpret_cast<uint8_t*&>(modules::g_front_end_instance)[0x30] != 0; }
// Obj's create_from_node (0x005A32A0): the object of node `name` made.
void create_from_node(uint32_t name) { commands::create_from_node_name(name); }

// The scenes (render::CEngine's list 0x006FA7EC): nodes (memory::ListNode) {scene +0, priority, next +8}, ended by a node whose scene is -1.
struct SceneNode {
    render::WorldScene* scene;
    int priority;
    SceneNode* next;
};
static_assert(sizeof(SceneNode) == 0x0C, "memory::ListNode's data, priority and next");
bool is_end(const SceneNode* node) { return reinterpret_cast<intptr_t>(node->scene) == -1; }
SceneNode* first_scene() { return reinterpret_cast<SceneNode*>(render::first_scene_node()); }
render::WorldScene* sky_scene() { return render::get_sky_scene(); }
// The scene of the level being played (the park editor's shell checksum, looked up by 0x0048C700).
render::WorldScene* level_scene()
{
    uint32_t name = park_editing::ParkBuilderMode::get_shell_checksum();
    return static_cast<render::WorldScene*>(render::engine::find_scene(name));
}
// A scene's own colour (the original's vtable +0x20: slot 8, the "ignore one argument" stub in both the engine's and
// the PC scene's tables - nothing is done).
void set_scene_color_of(render::WorldScene* /*scene*/, uint32_t /*color*/) {}
// The sector named `name` in any scene (0x0048C510).
render::WorldCell* look_up_sector(uint32_t name)
{
    return static_cast<render::WorldCell*>(render::engine::look_up_sector(name));
}
// A model component's colour (0x00420700).
void set_model_color(objects::ObjectModule* model, uint32_t color)
{
    static_cast<objects::MeshInstanceModule*>(model)->change_tint(color);
}

TodSystem& system_at(CtodManager* manager, int index)
{
    // (no bounds check: index 2 is past the end, as in the original)
    return *reinterpret_cast<TodSystem*>(reinterpret_cast<uint8_t*>(manager->systems_) + index * sizeof(TodSystem));
}

BloomParams bloom_of(const TodParams& params)
{
    return {params.bloom_on_, params.bloom1, params.bloom2, params.bloom_strength_, params.bloom_color_};
}

// NOT ORIGINAL - set_tod_params_filter's (null: none).
TodParamsFilter g_params_filter = nullptr;
bool get_filter(TodParams* params) { return g_params_filter && g_params_filter(params); }

// NOT ORIGINAL - set_multiplayer_unlock's (null: none).
MultiplayerUnlock g_multiplayer_unlock = nullptr;
// What the original tests (is_multiplayer), then the unlock (THUGPRO's network games): the lights left alone.
bool lights_locked() { return is_multiplayer() && !(g_multiplayer_unlock && g_multiplayer_unlock()); }

} // namespace

void set_tod_params_filter(TodParamsFilter filter) { g_params_filter = filter; }
void set_multiplayer_unlock(MultiplayerUnlock unlock) { g_multiplayer_unlock = unlock; }
MultiplayerUnlock get_multiplayer_unlock() { return g_multiplayer_unlock; }
bool tod_lights_locked() { return lights_locked(); }

uint32_t next_tod_state(uint32_t state)
{
    switch (state) {
    case kMorning:
        return kAfternoon;
    case kAfternoon:
        return kEvening;
    case kEvening:
        return kNight;
    case kNight:
        return kMorning;
    default:
        return 0;
    }
}

uint32_t previous_tod_state(uint32_t state)
{
    switch (state) {
    case kMorning:
        return kNight;
    case kAfternoon:
        return kMorning;
    case kEvening:
        return kAfternoon;
    case kNight:
        return kEvening;
    default:
        return 0;
    }
}

int CtodManager::current_system() const
{
    int index = 0;
    while (index < kSystems && systems_[index].sky)
        ++index;
    return index;
}

// 0x00571140
void CtodManager::get_level_color(uint32_t* out)
{
    TodSystem& system = system_at(this, current_system());
    if (!system.transitioning) {
        const TodParams& p = system.current;
        *out = (((p.lev_blue_ << 8) + p.lev_green_) << 8) + p.lev_red_;
        return;
    }
    double t = 1.0;
    if (system.transition_length_) {
        t = get_fraction(system.time, system.transition_length_);
        if (!(t <= get_one()))
            t = get_one();
    }
    const TodParams& a = system.current;
    const TodParams& b = system.next;
    int blue = ftol(static_cast<double>(b.lev_blue_ - a.lev_blue_) * t + a.lev_blue_);
    int green = ftol(static_cast<double>(b.lev_green_ - a.lev_green_) * t + a.lev_green_);
    int red = ftol(static_cast<double>(b.lev_red_ - a.lev_red_) * t + a.lev_red_);
    *out = (((blue << 8) + green) << 8) + red;
}

// 0x00571290
void CtodManager::read_group(int index)
{
    TodSystem& system = system_at(this, index);
    script::Array* groups;
    system.params->fetch_list(kGroups, &groups, 1);
    system.group_count_ = static_cast<int>(groups->extent_);
    auto* group = reinterpret_cast<script::Struct*>(groups->fetch_part(static_cast<uint32_t>(system.group)));
    group->fetch_int_value(kParts, &system.parts, 1);
    group->fetch_int_value(kWaitFrames, &system.wait_frames_, 1);
    group->read_hash_value(kLightGroup, &system.light_group_, 1);
    if (!group->read_hash_value(kResetLightGroup, &system.reset_light_group_, 0))
        system.reset_light_group_ = 0;
}

// 0x00571340
TodParams CtodManager::get_lerp(TodParams a, TodParams b, float t)
{
    auto integer = [t](int from, int to) { return ftol(static_cast<double>(to - from) * t + from); };
    auto real = [t](float from, float to) { return static_cast<float>((static_cast<double>(to) - from) * t + from); };
    auto byte = [&](uint32_t from, uint32_t to, int shift) {
        int f = static_cast<int>(from >> shift & 0xFF);
        int g = static_cast<int>(to >> shift & 0xFF);
        return static_cast<uint32_t>(static_cast<uint8_t>(integer(f, g))) << shift;
    };
    TodParams r = a; // (+0x14..+0x24 and fog_on: not set by the original - stack garbage)
    r.lev_red_ = integer(a.lev_red_, b.lev_red_);
    r.lev_green_ = integer(a.lev_green_, b.lev_green_);
    r.lev_blue_ = integer(a.lev_blue_, b.lev_blue_);
    r.sky_red_ = integer(a.sky_red_, b.sky_red_);
    r.sky_green_ = integer(a.sky_green_, b.sky_green_);
    r.sky_blue_ = integer(a.sky_blue_, b.sky_blue_);
    r.fog_red_ = integer(a.fog_red_, b.fog_red_);
    r.fog_green_ = integer(a.fog_green_, b.fog_green_);
    r.fog_blue_ = integer(a.fog_blue_, b.fog_blue_);
    r.fog_alpha_ = integer(a.fog_alpha_, b.fog_alpha_);
    r.fog_dist_ = integer(a.fog_dist_, b.fog_dist_);
    r.ambient_red_ = integer(a.ambient_red_, b.ambient_red_);
    r.ambient_green_ = integer(a.ambient_green_, b.ambient_green_);
    r.ambient_blue_ = integer(a.ambient_blue_, b.ambient_blue_);
    r.ambient_mod_lo_ = real(a.ambient_mod_lo_, b.ambient_mod_lo_);
    r.ambient_mod_hi_ = real(a.ambient_mod_hi_, b.ambient_mod_hi_);
    r.heading0 = real(a.heading0, b.heading0);
    r.pitch0 = real(a.pitch0, b.pitch0);
    r.red0 = integer(a.red0, b.red0);
    r.green0 = integer(a.green0, b.green0);
    r.blue0 = integer(a.blue0, b.blue0);
    r.mod_lo0_ = real(a.mod_lo0_, b.mod_lo0_);
    r.mod_hi0_ = real(a.mod_hi0_, b.mod_hi0_);
    r.heading_a_ = real(a.heading_a_, b.heading_a_);
    r.pitch1 = real(a.pitch1, b.pitch1);
    r.red1 = integer(a.red1, b.red1);
    r.green1 = integer(a.green1, b.green1);
    r.blue1 = integer(a.blue1, b.blue1);
    r.mod_lo1_ = real(a.mod_lo1_, b.mod_lo1_);
    r.mod_hi1_ = real(a.mod_hi1_, b.mod_hi1_);
    r.bloom_on_ = a.bloom_on_;
    r.bloom1 = integer(a.bloom1, b.bloom1);
    r.bloom2 = integer(a.bloom2, b.bloom2);
    // The bloom colours byte by byte; their fourth byte is 0x80.
    r.bloom_strength_ = byte(a.bloom_strength_, b.bloom_strength_, 0) | byte(a.bloom_strength_, b.bloom_strength_, 8) |
                        byte(a.bloom_strength_, b.bloom_strength_, 16) | 0x80000000u;
    r.bloom_color_ = byte(a.bloom_color_, b.bloom_color_, 0) | byte(a.bloom_color_, b.bloom_color_, 8) |
                     byte(a.bloom_color_, b.bloom_color_, 16) | 0x80000000u;
    return r;
}

// 0x00571A00
void CtodManager::parse_params(script::Struct* structure, TodParams* out)
{
    structure->fetch_int_value(kLevRed, &out->lev_red_, 0);
    structure->fetch_int_value(kLevGreen, &out->lev_green_, 0);
    structure->fetch_int_value(kLevBlue, &out->lev_blue_, 0);
    structure->fetch_int_value(kSkyRed, &out->sky_red_, 0);
    structure->fetch_int_value(kSkyGreen, &out->sky_green_, 0);
    structure->fetch_int_value(kSkyBlue, &out->sky_blue_, 0);
    structure->fetch_int_value(kFogRed, &out->fog_red_, 0);
    structure->fetch_int_value(kFogGreen, &out->fog_green_, 0);
    structure->fetch_int_value(kFogBlue, &out->fog_blue_, 0);
    structure->fetch_int_value(kFogAlpha, &out->fog_alpha_, 0);
    structure->fetch_int_value(kFogDist, &out->fog_dist_, 0);
    structure->fetch_int_value(kAmbientRed, &out->ambient_red_, 0);
    structure->fetch_int_value(kAmbientGreen, &out->ambient_green_, 0);
    structure->fetch_int_value(kAmbientBlue, &out->ambient_blue_, 0);
    structure->fetch_float(kAmbientModLo, &out->ambient_mod_lo_, 0);
    structure->fetch_float(kAmbientModHi, &out->ambient_mod_hi_, 0);
    structure->fetch_float(kHeading0, &out->heading0, 0);
    structure->fetch_float(kPitch0, &out->pitch0, 0);
    structure->fetch_int_value(kRed0, &out->red0, 0);
    structure->fetch_int_value(kGreen0, &out->green0, 0);
    structure->fetch_int_value(kBlue0, &out->blue0, 0);
    structure->fetch_float(kModLo0, &out->mod_lo0_, 0);
    structure->fetch_float(kModHi0, &out->mod_hi0_, 0);
    structure->fetch_float(kHeading1, &out->heading_a_, 0);
    structure->fetch_float(kPitch1, &out->pitch1, 0);
    structure->fetch_int_value(kRed1, &out->red1, 0);
    structure->fetch_int_value(kGreen1, &out->green1, 0);
    structure->fetch_int_value(kBlue1, &out->blue1, 0);
    structure->fetch_float(kModLo1, &out->mod_lo1_, 0);
    structure->fetch_float(kModHi1, &out->mod_hi1_, 0);

    float value = 0.0f;
    if (structure->fetch_float(kSunTheta, &value, 0))
        out->sun_theta_ = static_cast<float>(static_cast<double>(value) * degree_to_radian_conv());
    if (structure->fetch_float(kSunPhi, &value, 0))
        out->sun_phi_ = static_cast<float>(static_cast<double>(value) * degree_to_radian_conv());
    out->sun_red_ = 255.0f;
    out->sun_green_ = 255.0f;
    out->sun_blue_ = 255.0f;
    if (structure->fetch_float(kSunRed, &value, 0))
        out->sun_red_ = value;
    if (structure->fetch_float(kSunGreen, &value, 0))
        out->sun_green_ = value;
    if (structure->fetch_float(kSunBlue, &value, 0))
        out->sun_blue_ = value;

    structure->fetch_int_value(kBloomOn, &out->bloom_on_, 0);
    if (!out->bloom_on_)
        return;
    structure->fetch_int_value(kBloom1, &out->bloom1, 0);
    structure->fetch_int_value(kBloom2, &out->bloom2, 0);
    script::Array* array = nullptr;
    if (structure->fetch_list(kBloomStrength, &array, 0))
        out->bloom_strength_ = color_from_array(array);
    array = nullptr;
    if (structure->fetch_list(kBloomColor, &array, 0))
        out->bloom_color_ = color_from_array(array);
}

// 0x00571E00
TodParams CtodManager::get_tod_params(int index)
{
    if (index == -1)
        index = current_system();
    TodSystem& system = system_at(this, index);
    if (!system.transitioning)
        return system.current;
    float t = 1.0f;
    if (system.transition_length_) {
        double fraction = get_fraction(system.time, system.transition_length_);
        t = static_cast<float>(fraction);
        if (!(fraction <= get_one()))
            t = 1.0f;
    }
    return get_lerp(system.current, system.next, t);
}

// 0x00571EF0
void CtodManager::build_tod_objects(script::Struct* params, script::Script* /*script*/)
{
    uint32_t name = 0;
    if (params->read_hash_value(kName, &name, 0)) {
        if (!dead_objects_->fetch_entry(name, true))
            create_from_node(name);
        return;
    }
    uint32_t prefix;
    if (!params->hash_from_name_or_text(kPrefix, &prefix, 0))
        return;
    // (NoPrefixWarning: passed on as the third argument of 0x0045BF90, which doesn't use it)
    params->has_bare_flag(kNoPrefixWarning);
    uint16_t count;
    uint32_t* nodes = level_nodes::collect_nodes_by_prefix(prefix, &count);
    for (int i = 0; i < count; ++i) {
        if (!dead_objects_->fetch_entry(nodes[i]))
            create_from_node(nodes[i]);
    }
}

// 0x00572130
void CtodManager::change_args(script::Struct* params)
{
    params_set_ = true;
    int number;
    params->fetch_int_value(kSystem, &number, 1);
    TodSystem& system = system_at(this, number - 1);
    system.params->get_clear();
    system.params->append_structure_if_any(params);
    params->read_hash_value(kCurrentState, &system.state, 0);
    system.next_state_ = next_tod_state(system.state);
    script::Struct* state;
    if (params->read_sub_struct(kOverwriteCurrentState, &state, 0)) {
        parse_params(state, &system.current);
    } else if (params->read_sub_struct(system.state, &state, 0)) {
        parse_params(state, &system.current);
        state->fetch_int_value(kStateLength, &system.state_length_, 1);
    }
    if (params->read_sub_struct(system.next_state_, &state, 0))
        parse_params(state, &system.next);
    params->fetch_int_value(kTransitionLength, &system.transition_length_, 0);
    system.sky = params->has_bare_flag(kSky);
}

// 0x00572320
void CtodManager::set_fog_active(bool on)
{
    bool was = fog_active_;
    fog_active_ = on;
    if (was) {
        if (!on)
            change_fog(false);
        return;
    }
    if (!on)
        return;
    change_fog(true);
    TodParams params = get_tod_params(-1);
    get_filter(&params); // (NOT ORIGINAL: an edited level's own fog - set_tod_params_filter)
    set_fog_color(
        get_rgba(params.fog_red_, params.fog_green_, params.fog_blue_, static_cast<uint8_t>(params.fog_alpha_)));
    set_fog_distance(static_cast<float>(params.fog_dist_));
}

// 0x005723C0
void CtodManager::next_state(int index)
{
    TodSystem& system = system_at(this, index);
    if (system.state == kNight)
        ++system.days_passed_;
    system.state = system.next_state_;
    system.next_state_ = next_tod_state(system.state);
    script::Struct* state;
    system.params->read_sub_struct(system.state, &state, 1);
    parse_params(state, &system.current);
    state->fetch_int_value(kStateLength, &system.state_length_, 1);
    system.params->read_sub_struct(system.next_state_, &state, 1);
    parse_params(state, &system.next);
    system.script_run_ = false;
    system.time = 0;
    system.part = 0;
    system.group = 0;
    system.frame = 0;
    system.transitioning = false;
}

// 0x005724D0
void CtodManager::update_sun()
{
    TodSystem& system = systems_[0];
    double t;
    if (system.transitioning)
        t = get_fraction(system.time + system.state_length_, system.transition_length_ + system.state_length_);
    else
        t = get_fraction(system.time, system.state_length_ + system.transition_length_);
    auto blend = [t](float from, float to) { return static_cast<float>((static_cast<double>(to) - from) * t + from); };
    const TodParams& a = system.current;
    const TodParams& b = system.next;
    SunParams sun = {};
    sun.theta = blend(a.sun_theta_, b.sun_theta_);
    sun.phi = blend(a.sun_phi_, b.sun_phi_);
    sun.color[0] = blend(a.sun_red_, b.sun_red_);
    sun.color[1] = blend(a.sun_green_, b.sun_green_);
    sun.color[2] = blend(a.sun_blue_, b.sun_blue_);
    sun.color[3] = 128.0f;
    sun.shown = system.state != kNight;
    set_sun(&sun, kSunAngles | kSunColor | kSunShown);
}

// 0x005725F0
void apply_color_to_scene(render::WorldScene* scene, uint32_t color, uint32_t light_group, uint32_t reset_light_group,
                          int start_percent, int end_percent)
{
    // The scene's sectors: their light group and geometry (change_tint: its colour).
    lists::KeyMap* table = scene->sectors_;
    if (!table)
        return;
    auto colour = [](render::WorldCell* sector, uint32_t value) {
        void* geometry = sector->geom_handle_;
        if (geometry) {
            static_cast<render::Geom*>(geometry)->change_tint(value);
        }
    };
    auto next = [table]() -> render::WorldCell* {
        if (!table->step_iterator() || !table->iter_item_)
            return nullptr;
        return static_cast<render::WorldCell*>(table->iter_item_->value);
    };
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    render::WorldCell* sector = nullptr;
    if (table->step_iterator() && table->iter_item_)
        sector = static_cast<render::WorldCell*>(table->iter_item_->value);
    int count = table->extent_;
    int remaining = count;
    if (start_percent >= 0) {
        int skip = ftol(static_cast<double>(start_percent) * count * one_percent());
        if (start_percent < end_percent) {
            int last = ftol(static_cast<double>(end_percent) * count * one_percent());
            if (count < last)
                last = count;
            remaining = last - skip;
        }
        for (int i = skip; i != 0; --i)
            sector = next();
    }
    if (!sector)
        return;
    if (!light_group) {
        do {
            if (remaining-- == 0)
                return;
            colour(sector, color);
        } while ((sector = next()) != nullptr);
    } else if (!reset_light_group) {
        do {
            if (remaining-- == 0)
                return;
            if (sector->light_group_ == light_group)
                colour(sector, color);
        } while ((sector = next()) != nullptr);
    } else {
        do {
            if (remaining-- == 0)
                return;
            uint32_t group = sector->light_group_;
            if (group == light_group)
                colour(sector, color);
            else if (group == reset_light_group)
                colour(sector, kNeutralColor);
        } while ((sector = next()) != nullptr);
    }
}

// 0x005727F0 (and 0x00572821)
void CtodManager::apply_color(uint32_t color, uint32_t sky_color, bool sky, uint32_t light_group,
                              uint32_t reset_light_group, bool part_only, int start_percent, int end_percent)
{
    if (sky) {
        render::WorldScene* scene = sky_scene();
        if (scene)
            apply_color_to_scene(scene, sky_color, light_group, 0, -1, -1);
        return;
    }
    bool whole_level = light_group == kOutdoor || light_group == 0;
    for (SceneNode* node = first_scene(); node;) {
        SceneNode* next = is_end(node->next) ? nullptr : node->next;
        render::WorldScene* scene = node->scene;
        if (!scene->is_sky_) {
            apply_color_to_scene(scene, color, light_group, reset_light_group, start_percent, end_percent);
            if (whole_level && !part_only)
                set_scene_color_of(scene, color);
        }
        node = next;
    }
    render::WorldScene* level = level_scene();
    if (level) {
        apply_color_to_scene(level, color, light_group, reset_light_group, start_percent, end_percent);
        if (whole_level && !part_only)
            set_scene_color_of(level, color);
    }
    if ((whole_level || reset_light_group) && !part_only) {
        // The models: those of objects without a sector of their name, or an outdoor one, get the colour.
        for (objects::ObjectModule* model = objects::object_hub_slot()->find_first_module_of_kind(kModel); model;
             model = model->next_of_type_) {
            render::WorldCell* sector = look_up_sector(model->entity_->ident_);
            if (!sector || sector->light_group_ == kOutdoor)
                set_model_color(model, color);
            if (reset_light_group && sector && sector->light_group_ == reset_light_group)
                set_model_color(model, kNeutralColor);
        }
    }
}

// 0x00572980
void set_light_direction(int index, float heading, float pitch)
{
    double p = static_cast<double>(pitch) * degree_to_radian_conv();
    double sin_pitch = std::sin(p);
    double cos_pitch = std::cos(p);
    float direction[4];
    direction[3] = 0.0f;
    direction[1] = static_cast<float>(static_cast<double>(get_zero()) * cos_pitch - sin_pitch);
    double horizontal = cos_pitch + sin_pitch * get_zero();
    double h = static_cast<double>(heading) * degree_to_radian_conv();
    auto sin_heading = static_cast<float>(std::sin(h));
    double cos_heading = std::cos(h);
    auto cos_heading_float = static_cast<float>(std::cos(h));
    direction[2] = static_cast<float>(cos_heading * horizontal + static_cast<double>(sin_heading) * get_zero());
    direction[0] = static_cast<float>(horizontal * sin_heading + static_cast<double>(cos_heading_float) * get_zero());
    set_light_direction_vector(index, direction);
}

// 0x00572A10
void CtodManager::apply_tod_params(TodParams params)
{
    // (NOT ORIGINAL: an edited level's own ambient light and fog put over the state's - set_tod_params_filter; its fog
    // shown even when the manager's is off)
    bool fog_forced = get_filter(&params);
    change_ambient_tint(get_rgba(params.ambient_red_, params.ambient_green_, params.ambient_blue_, 0x80));
    set_light_direction(0, params.heading0, params.pitch0);
    set_light_direction(1, params.heading_a_, params.pitch1);
    change_light_tint(0, get_rgba(params.red0, params.green0, params.blue0, 0x80));
    change_light_tint(1, get_rgba(params.red1, params.green1, params.blue1, 0x80));
    set_ambient_modulation(params.ambient_mod_lo_, params.ambient_mod_hi_);
    set_light_modulation(0, params.mod_lo0_, params.mod_hi0_);
    set_light_modulation(1, params.mod_lo1_, params.mod_hi1_);
    if (fog_active_ || fog_forced) {
        change_fog(true);
        set_fog_color(
            get_rgba(params.fog_red_, params.fog_green_, params.fog_blue_, static_cast<uint8_t>(params.fog_alpha_)));
        set_fog_distance(static_cast<float>(params.fog_dist_));
    }
    // The level colour, 0..255 as 0..2.
    auto channel = [](int value) {
        double scaled = static_cast<double>(value) * one_over255();
        return static_cast<float>(scaled + scaled);
    };
    float color[4] = {channel(params.lev_red_), channel(params.lev_green_), channel(params.lev_blue_), 1.0f};
    set_level_color(color);
}

// 0x00572BD0
void CtodManager::clear_dead_objects() { dead_objects_->empty_table(); }

// 0x005730E0
void CtodManager::get_reset()
{
    params_set_ = false;
    full_length_ = 0;
    time_multiplier_ = 1.0f;
    transition_script_ = 0;
    for (TodSystem& system : systems_) {
        system.time = 0;
        system.state_length_ = 60000;
        system.transition_length_ = 30000;
        system.state = kAfternoon;
        system.frame = 0;
        system.days_passed_ = 0;
        system.transitioning = false;
        system.script_run_ = false;
        system.params->get_clear();
    }
    temp_tod_ = false;
    active_ = true;
    fog_active_ = true;
    level_lights_reset_ = false;
    unknown4_->get_clear();
    dead_objects_->empty_table();
}

// 0x00573180
void add_bloom_params(int on, int bloom1, int bloom2, uint32_t strength, uint32_t color, script::Struct* out)
{
    out->put_int(kOn, on);
    out->put_int(kBloom1, bloom1);
    out->put_int(kBloom2, bloom2);
    script::Array* memory = script::alloc_script_array();
    script::Array* array = memory ? new (memory) script::Array : nullptr;
    array->configure_size_kind(3, script::kQbvalInteger);
    array->set_element(0, strength & 0xFF);
    array->set_element(1, strength >> 8 & 0xFF);
    array->set_element(2, strength >> 16 & 0xFF);
    out->append_array_value(kStrength, array);
    array->set_element(0, color & 0xFF);
    array->set_element(1, color >> 8 & 0xFF);
    array->set_element(2, color >> 16 & 0xFF);
    out->append_array_value(kColor, array);
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
}

// 0x005732D0
void CtodManager::set_scene_color(script::Struct* params)
{
    uint32_t light_group = 0;
    params->read_hash_value(kLightGroup, &light_group, 0);
    uint32_t reset_light_group = 0;
    params->read_hash_value(kResetLightGroup, &reset_light_group, 0);
    bool scene_only = params->has_bare_flag(kSceneColorFlag);
    int value;
    params->fetch_int_value(kColor, &value, 1);
    // (the colour's fourth byte isn't set by the original - stack garbage; nor the sky colour's, the top byte of the
    // parameters' pointer whose argument slot it's kept in)
    uint32_t color = static_cast<uint32_t>(value) & 0xFFFFFF;
    uint32_t sky_color = 0;
    int sky = value;
    bool is_sky = false;
    if (params->fetch_int_value(kSky, &sky, 0)) {
        is_sky = true;
        sky_color = static_cast<uint32_t>(sky) & 0xFFFFFF;
    }
    int start = -1;
    int end = -1;
    params->fetch_int_value(kSceneColorStart, &start, 0);
    params->fetch_int_value(kSceneColorEnd, &end, 0);
    apply_color(color, sky_color, is_sky, light_group, reset_light_group, scene_only, start, end);
}

// 0x00573400
void CtodManager::mark_permanently_dead(script::Struct* params)
{
    uint32_t name;
    if (params->read_hash_value(kName, &name, 0)) {
        if (!dead_objects_->fetch_entry(name, true))
            dead_objects_->store_entry(name, this);
        return;
    }
    uint32_t prefix;
    if (!params->hash_from_name_or_text(kPrefix, &prefix, 0))
        return;
    params->has_bare_flag(kNoPrefixWarning); // (see build_tod_objects)
    uint16_t count = 0;
    uint32_t* nodes = level_nodes::collect_nodes_by_prefix(prefix, &count);
    for (int i = 0; i < count; ++i) {
        if (!dead_objects_->fetch_entry(nodes[i]))
            dead_objects_->store_entry(nodes[i], this);
    }
}

// 0x00573500
void CtodManager::set_bloom(int on, int bloom1, int bloom2, uint32_t strength, uint32_t color)
{
    script::Struct* params = spawn_param_table();
    if (on)
        add_bloom_params(on, bloom1, bloom2, strength, color, params);
    else
        params->put_int(kOn, 0);
    set_bloom_structure(params);
    delete_structure(params);
}

// 0x00573620
CtodManager::CtodManager()
{
    unknown4_ = spawn_param_table();
    void* table = memory::alloc(sizeof(lists::KeyMap));
    dead_objects_ = table ? new (table) lists::KeyMap(8) : nullptr;
    for (TodSystem& system : systems_)
        system.params = spawn_param_table();
    get_reset();
}

// 0x00573770
CtodManager::~CtodManager()
{
    for (TodSystem& system : systems_) {
        delete_structure(system.params);
        system.params = nullptr;
    }
    delete_structure(unknown4_);
    dead_objects_->empty_table();
    if (dead_objects_) {
        dead_objects_->~KeyMap();
        memory::Hub::get_pull()->dealloc(dead_objects_);
    }
}

// 0x00573810
void CtodManager::update_system(int index)
{
    TodSystem& system = system_at(this, index);
    int part = system.part;
    ++system.frame;
    if (part != 0 && system.frame <= system.wait_frames_)
        return;

    // This part's share of the sectors.
    double step = hundred() / static_cast<double>(system.parts);
    int start = ftol(part * step);
    int end = ftol((part + 1) * step);
    float t = 1.0f;
    if (system.transition_length_) {
        double fraction = get_fraction(system.time, system.transition_length_);
        t = static_cast<float>(fraction);
        if (!(fraction <= get_one()))
            t = 1.0f;
    }
    TodParams params = get_lerp(system.current, system.next, t);
    uint32_t level_color = get_rgba(params.lev_red_, params.lev_green_, params.lev_blue_, 0x80);
    uint32_t sky_color = get_rgba(params.sky_red_, params.sky_green_, params.sky_blue_, 0x80);
    bool sky = system.sky;
    apply_color(level_color, sky_color, sky, system.light_group_, system.reset_light_group_, part > 0, start, end);
    if (!sky && !level_lights_reset_)
        apply_tod_params(params);

    // The next part (or the next group, after the last part).
    system.frame = 0;
    if (++system.part >= system.parts) {
        system.part = 0;
        if (system.group < system.group_count_ - 1)
            ++system.group;
        else
            system.group = 0;
        read_group(index);
    }
    if (!sky && !temp_bloom_.on)
        set_bloom(params.bloom_on_, params.bloom1, params.bloom2, params.bloom_strength_, params.bloom_color_);

    // The transition script: half-way through (evening's at once).
    float threshold = system.state == kEvening ? get_zero() : get_half();
    if (!system.script_run_ && t > threshold) {
        system.script_run_ = true;
        script::Struct* script_params = spawn_param_table();
        script_params->put_name_key(kCurrentTod, system.state);
        script_params->put_name_key(kNextTod, system.next_state_);
        script_params->put_sub_struct(kTodStateParams, system.params);
        if (!sky) {
            if (transition_script_)
                script_params->put_name_key(kTransitionScript, transition_script_);
            script::spawn_script(kStateTransition, script_params, 0, nullptr, 0, 0, false, false, false, false);
        }
        delete_structure(script_params);
    }

    if (system.time > system.transition_length_) {
        next_state(index);
        if (!system.sky && !temp_bloom_.on) {
            const TodParams& current = system.current;
            set_bloom(current.bloom_on_, current.bloom1, current.bloom2, current.bloom_strength_, current.bloom_color_);
        }
    }
}

// 0x00573BE0
void CtodManager::reset_level_lights(uint32_t state_name)
{
    if (lights_locked()) // (the unlock: NOT ORIGINAL - set_multiplayer_unlock)
        return;
    TodParams params;
    if (!state_name) {
        params = get_tod_params(-1);
        apply_tod_params(params);
        level_lights_reset_ = false;
    } else {
        std::memset(&params, 0, sizeof(params)); // (the original's are uninitialised: what the state doesn't give)
        script::Struct* state;
        system_at(this, current_system()).params->read_sub_struct(state_name, &state, 1);
        parse_params(state, &params);
        apply_tod_params(params);
        level_lights_reset_ = true;
    }
    set_bloom(params.bloom_on_, params.bloom1, params.bloom2, params.bloom_strength_, params.bloom_color_);
}

// 0x00573CE0
void CtodManager::refresh_all(bool temp_state)
{
    bool was_active = active_;
    active_ = true;
    for (TodSystem& system : systems_) {
        const TodParams& current = system.current;
        uint32_t level_color = get_rgba(current.lev_red_, current.lev_green_, current.lev_blue_, 0x80);
        uint32_t sky_color = get_rgba(current.sky_red_, current.sky_green_, current.sky_blue_, 0x80);
        bool sky = system.sky;
        script::Array* groups;
        system.params->fetch_list(kGroups, &groups, 1);
        for (int g = 0; g < system.group_count_; ++g) {
            auto* group = reinterpret_cast<script::Struct*>(groups->fetch_part(static_cast<uint32_t>(g)));
            uint32_t light_group;
            group->read_hash_value(kLightGroup, &light_group, 1);
            uint32_t reset_light_group = 0;
            group->read_hash_value(kResetLightGroup, &reset_light_group, 0);
            auto step = static_cast<float>(hundred() / static_cast<double>(system.parts));
            for (int part = 0; part < system.parts; ++part) {
                int start = ftol(static_cast<double>(part) * step);
                int end = ftol(static_cast<double>(part + 1) * step);
                apply_color(level_color, sky_color, sky, light_group, reset_light_group, part > 0, start, end);
            }
            if (!sky && !level_lights_reset_)
                apply_tod_params(system.current);
        }
        if (sky)
            continue;
        script::Struct* params = spawn_param_table();
        params->put_name_key(kCurrentTod, previous_tod_state(system.state));
        params->put_name_key(kNextTod, system.state);
        params->put_sub_struct(kTodStateParams, system.params);
        if (transition_script_)
            params->put_name_key(kTransitionScript, transition_script_);
        params->put_name_key(0u, kInstantChange);
        if (temp_state)
            params->put_name_key(0u, kTempState);
        script::Struct* bloom = spawn_param_table();
        add_bloom_params(current.bloom_on_, current.bloom1, current.bloom2, current.bloom_strength_,
                         current.bloom_color_, bloom);
        params->put_sub_struct(kBloomParams, bloom);
        delete_structure(bloom);
        script::spawn_script(kStateTransition, params, 0, nullptr, 0, 0, false, false, false, false);
        delete_structure(params);
    }
    active_ = was_active;
    const TodParams& current = system_at(this, current_system()).current;
    set_bloom(current.bloom_on_, current.bloom1, current.bloom2, current.bloom_strength_, current.bloom_color_);
    update_sun();
}

// 0x005741B0
void CtodManager::set_temp_tod(script::Struct* params)
{
    temp_tod_ = true;
    script::find_symbol(kTempTodChanged)->integer = 1;
    for (TodSystem& system : systems_)
        parse_params(params, &system.current);
    refresh_all(true);
}

// 0x00574210
void CtodManager::restore_current_tod()
{
    for (TodSystem& system : systems_) {
        script::Struct* state;
        system.params->read_sub_struct(system.state, &state, 1);
        parse_params(state, &system.current);
    }
    refresh_all(false);
    temp_tod_ = false;
}

// 0x00574270
void CtodManager::set_temp_bloom_params(script::Struct* params)
{
    params->fetch_int_value(kOn, &temp_bloom_.on, 0);
    params->fetch_int_value(kBloom1, &temp_bloom_.bloom1, 0);
    params->fetch_int_value(kBloom2, &temp_bloom_.bloom2, 0);
    script::Array* array = nullptr;
    if (params->fetch_list(kStrength, &array, 0))
        temp_bloom_.strength = color_from_array(array);
    array = nullptr;
    if (params->fetch_list(kColor, &array, 0))
        temp_bloom_.color = color_from_array(array);
    // Off: back to the state's bloom (in a single player game).
    if (!temp_bloom_.on && !is_multiplayer()) {
        BloomParams bloom = bloom_of(system_at(this, current_system()).current);
        set_bloom(bloom.on, bloom.bloom1, bloom.bloom2, bloom.strength, bloom.color);
        return;
    }
    set_bloom(temp_bloom_.on, temp_bloom_.bloom1, temp_bloom_.bloom2, temp_bloom_.strength, temp_bloom_.color);
}

// 0x00574400
void CtodManager::enable_bloom(int on)
{
    if (lights_locked()) // (the unlock: NOT ORIGINAL - set_multiplayer_unlock)
        return;
    TodParams& current = system_at(this, current_system()).current;
    if (on == current.bloom_on_)
        return;
    current.bloom_on_ = on;
    set_bloom(current.bloom_on_, current.bloom1, current.bloom2, current.bloom_strength_, current.bloom_color_);
}

// 0x00574590
void CtodManager::tick_frame()
{
    if (active_ruleset_label() != kCareer || script::fetch_int_value(kInactive) == 1 || temp_tod_ || !active_ ||
        is_game_halted())
        return;
    int elapsed = ftol(frame_clock::get_slomo_frame_seconds() * time_multiplier_ * thousand());
    full_length_ += elapsed;
    update_sun();
    for (int index = 0; index < kSystems; ++index) {
        TodSystem& system = systems_[index];
        system.time += elapsed;
        if (!system.transitioning && system.time > system.state_length_) {
            system.time = 0;
            system.transitioning = true;
            read_group(index);
        }
        if (system.transitioning)
            update_system(index);
    }
}

// 0x00574670 (a state that never comes - not one of the four - loops forever, as in the original)
void CtodManager::set_tod_instant(uint32_t state)
{
    if (lights_locked()) // (the unlock: NOT ORIGINAL - set_multiplayer_unlock)
        return;
    // How long until `state`, from the first non-sky system.
    TodSystem& current = system_at(this, current_system());
    int remaining = current.transitioning ? current.transition_length_ - current.time
                                          : current.state_length_ - current.time + current.transition_length_;
    for (uint32_t s = next_tod_state(current.state); s != state; s = next_tod_state(s)) {
        script::Struct* structure;
        current.params->read_sub_struct(s, &structure, 1);
        int length;
        structure->fetch_int_value(kStateLength, &length, 1);
        remaining += current.transition_length_ + length;
    }
    // Every system moved on by that long.
    for (int index = 0; index < kSystems; ++index) {
        TodSystem& system = systems_[index];
        int left = remaining;
        while (left > 0) {
            if (system.transitioning) {
                if (left < system.transition_length_ - system.time) {
                    system.time += left;
                    break;
                }
                left += system.time - system.transition_length_;
                next_state(index);
                read_group(index);
            } else {
                if (left < system.state_length_ - system.time) {
                    system.time += left;
                    break;
                }
                left += system.time - system.state_length_;
                system.transitioning = true;
                system.time = 0;
            }
        }
    }
    refresh_all(false);
}

// NOT ORIGINAL (engine/net/thugpro_peer.h)
uint32_t CtodManager::current_state() const
{
    int index = current_system();
    return index < kSystems ? systems_[index].state : 0;
}

// NOT ORIGINAL (engine/net/thugpro_peer.h): set_tod_instant's walk, without moving - at most once round the four states:
// `state` is reached (an unknown current state or target never is) and every state on the way has its structure.
bool CtodManager::can_set_tod_instant(uint32_t state) const
{
    int index = current_system();
    if (index >= kSystems)
        return false;
    const TodSystem& current = systems_[index];
    if (!current.params)
        return false;
    uint32_t s = next_tod_state(current.state);
    for (int step = 0; step < 4 && s; ++step, s = next_tod_state(s)) {
        if (s == state)
            return true;
        script::Struct* structure = nullptr;
        if (!current.params->read_sub_struct(s, &structure, 0) || !structure)
            return false;
    }
    return false;
}

CtodManager*& tod_manager() { return commands::g_tod_manager; }
namespace {
uint32_t& ref_tally() { return g_tod_references; }
} // namespace

// 0x00574540
CtodManager* create_tod_manager(bool create)
{
    CtodManager*& manager = tod_manager();
    if (!manager && create) {
        void* memory = memory::alloc_zeroed(sizeof(CtodManager), nullptr, 1);
        manager = memory ? new (memory) CtodManager : nullptr;
    }
    ++ref_tally();
    return manager;
}

// 0x00574910
void release_tod_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    CtodManager* manager = tod_manager();
    if (manager) {
        manager->~CtodManager();
        memory::Hub::get_pull()->dealloc(manager);
    }
    tod_manager() = nullptr;
    ref_tally() = 0;
}

// --- the script commands ------------------------------------------------------------------------------------

// 0x00571FE0: TODManager_SetActive <0|1>
bool cfunc_tod_manager_set_active(script::Struct* params, script::Script* /*script*/)
{
    CtodManager* manager = tod_manager();
    int active;
    params->fetch_int_value(0u, &active, 1);
    manager->active_ = active != 0;
    return true;
}

// 0x00572010: TODManager_GetNumberOfDaysPassed [system_index=n] -> num_days_passed
bool cfunc_tod_manager_get_number_of_days_passed(script::Struct* params, script::Script* script)
{
    CtodManager* manager = tod_manager();
    int index = 0;
    params->fetch_int_value(kSystemIndex, &index, 0);
    script->args_->put_int(kNumDaysPassed, system_at(manager, index).days_passed_);
    return true;
}

// 0x00572060: TODManager_SetTimeMultiplier <multiplier> (default 1)
bool cfunc_tod_manager_set_time_multiplier(script::Struct* params, script::Script* /*script*/)
{
    float multiplier = 1.0f;
    params->fetch_float(0u, &multiplier, 0);
    if (!tod_manager())
        return false;
    tod_manager()->time_multiplier_ = multiplier;
    return true;
}

// 0x005720A0: TODManager_GetTimeMultiplier -> TOD_Time_Multiplier
bool cfunc_tod_manager_get_time_multiplier(script::Struct* /*params*/, script::Script* script)
{
    if (!tod_manager())
        return false;
    script->args_->push_float(kTimeMultiplierName, tod_manager()->time_multiplier_);
    return true;
}

// 0x005720D0: TODManager_SetTODScript <script>
bool cfunc_tod_manager_set_tod_script(script::Struct* params, script::Script* /*script*/)
{
    CtodManager* manager = tod_manager();
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    manager->transition_script_ = name;
    return true;
}

// 0x00572110: TODManager_Create name= | prefix=
bool cfunc_tod_manager_create(script::Struct* params, script::Script* script)
{
    tod_manager()->build_tod_objects(params, script);
    return true;
}

// 0x00572BE0: TODManager_SetParams
bool cfunc_tod_manager_set_params(script::Struct* params, script::Script* /*script*/)
{
    tod_manager()->change_args(params);
    return true;
}

// 0x00572C00: TODManager_GetCurrentTODParams [system_index=n] -> tod_params={active current_state_name lev_red ...}
bool cfunc_tod_manager_get_current_tod_params(script::Struct* params, script::Script* script)
{
    int index = -1;
    params->fetch_int_value(kSystemIndex, &index, 0);
    CtodManager* manager = tod_manager();
    TodParams p = manager->get_tod_params(index);
    script::Struct* out = spawn_param_table();
    out->put_int(kActive, manager->active_);
    int current = 0;
    while (current < CtodManager::kSystems && manager->systems_[current].sky)
        ++current;
    out->put_name_key(kCurrentStateName, system_at(manager, current).state);
    out->put_int(kLevRed, p.lev_red_);
    out->put_int(kLevGreen, p.lev_green_);
    out->put_int(kLevBlue, p.lev_blue_);
    out->put_int(kSkyRed, p.sky_red_);
    out->put_int(kSkyGreen, p.sky_green_);
    out->put_int(kSkyBlue, p.sky_blue_);
    out->put_int(kFogOn, p.fog_on_);
    out->put_int(kFogRed, p.fog_red_);
    out->put_int(kFogGreen, p.fog_green_);
    out->put_int(kFogBlue, p.fog_blue_);
    out->put_int(kFogAlpha, p.fog_alpha_);
    out->put_int(kFogDist, p.fog_dist_);
    out->put_int(kAmbientRed, p.ambient_red_);
    out->put_int(kAmbientGreen, p.ambient_green_);
    out->put_int(kAmbientBlue, p.ambient_blue_);
    out->push_float(kAmbientModLo, p.ambient_mod_lo_);
    out->push_float(kAmbientModHi, p.ambient_mod_hi_);
    out->push_float(kHeading0, p.heading0);
    out->push_float(kPitch0, p.pitch0);
    out->put_int(kRed0, p.red0);
    out->put_int(kGreen0, p.green0);
    out->put_int(kBlue0, p.blue0);
    out->push_float(kModLo0, p.mod_lo0_);
    out->push_float(kModHi0, p.mod_hi0_);
    out->push_float(kHeading1, p.heading_a_);
    out->push_float(kPitch1, p.pitch1);
    out->put_int(kRed1, p.red1);
    out->put_int(kGreen1, p.green1);
    out->put_int(kBlue1, p.blue1);
    out->push_float(kModLo1, p.mod_lo1_);
    out->push_float(kModHi1, p.mod_hi1_);
    out->put_int(kBloomOn, p.bloom_on_);
    out->put_int(kBloom1, p.bloom1);
    out->put_int(kBloom2, p.bloom2);
    script::Array* memory = script::alloc_script_array();
    script::Array* array = memory ? new (memory) script::Array : nullptr;
    array->configure_size_kind(3, script::kQbvalInteger);
    array->set_element(0, p.bloom_strength_ & 0xFF);
    array->set_element(1, p.bloom_strength_ >> 8 & 0xFF);
    array->set_element(2, p.bloom_strength_ >> 16 & 0xFF);
    out->append_array_value(kBloomStrength, array);
    array->set_element(0, p.bloom_color_ & 0xFF);
    array->set_element(1, p.bloom_color_ >> 8 & 0xFF);
    array->set_element(2, p.bloom_color_ >> 16 & 0xFF);
    out->append_array_value(kBloomColor, array);
    script::release_array_storage(array);
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
    script->args_->append_struct_ref(kTodParams, out);
    return true;
}

// 0x005730A0: TODManager_SetFogActive [0|1] (default 1)
bool cfunc_tod_manager_set_fog_active(script::Struct* params, script::Script* /*script*/)
{
    CtodManager* manager = tod_manager();
    int on = 1;
    params->fetch_int_value(0u, &on, 0);
    manager->set_fog_active(on != 0);
    return true;
}

// 0x005735F0: TODManager_MarkPermanentlyDead name= | prefix=
bool cfunc_tod_manager_mark_permanently_dead(script::Struct* params, script::Script* /*script*/)
{
    tod_manager()->mark_permanently_dead(params);
    return true;
}

// 0x00573610: TODManager_Reset
bool cfunc_tod_manager_reset(script::Struct* /*params*/, script::Script* /*script*/)
{
    tod_manager()->get_reset();
    return true;
}

// 0x00574480: TODManager_ResetLevelLights [state_name=]
bool cfunc_tod_manager_reset_level_lights(script::Struct* params, script::Script* /*script*/)
{
    CtodManager* manager = tod_manager();
    uint32_t name = 0;
    params->read_hash_value(kStateName, &name, 0);
    manager->reset_level_lights(name);
    return true;
}

// 0x005744C0: TODManager_SetTempTOD <params>
bool cfunc_tod_manager_set_temp_tod(script::Struct* params, script::Script* /*script*/)
{
    tod_manager()->set_temp_tod(params);
    return true;
}

// 0x005744E0: TODManager_RestoreCurrentTOD
bool cfunc_tod_manager_restore_current_tod(script::Struct* /*params*/, script::Script* /*script*/)
{
    tod_manager()->restore_current_tod();
    return true;
}

// 0x005744F0: TODManager_SetTempBloomParams on= bloom1= bloom2= strength=[] color=[]
bool cfunc_tod_manager_set_temp_bloom_params(script::Struct* params, script::Script* /*script*/)
{
    tod_manager()->set_temp_bloom_params(params);
    return true;
}

// 0x00574510: TODManager_EnableBloom <0|1>
bool cfunc_tod_manager_enable_bloom(script::Struct* params, script::Script* /*script*/)
{
    CtodManager* manager = tod_manager();
    int on;
    params->fetch_int_value(0u, &on, 1);
    manager->enable_bloom(on);
    return true;
}

// 0x00574840: TODManager_SetTODInstant <state>
bool cfunc_tod_manager_set_tod_instant(script::Struct* params, script::Script* /*script*/)
{
    uint32_t state;
    params->read_hash_value(0u, &state, 1);
    if (!tod_manager())
        return false;
    tod_manager()->set_tod_instant(state);
    return true;
}

// 0x00574870: TODManager_AdvanceTODState: straight to the next state.
bool cfunc_tod_manager_advance_tod_state(script::Struct* /*params*/, script::Script* /*script*/)
{
    CtodManager* manager = tod_manager();
    if (!manager)
        return false;
    int current = 0;
    while (current < CtodManager::kSystems && manager->systems_[current].sky)
        ++current;
    manager->set_tod_instant(next_tod_state(system_at(manager, current).state));
    return true;
}

} // namespace gfx

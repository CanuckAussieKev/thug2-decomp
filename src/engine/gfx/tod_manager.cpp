// The time-of-day manager and its script commands. See tod_manager.h.

#include "engine/gfx/tod_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <array>

#include "engine/memory/arena_alloc.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/sector.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
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
#include "engine/script/node_hooks.h"
#include "engine/gfx/game_hooks.h"
#include "engine/script/node_prefixes.h"
#include "engine/script/param_struct.h"
#include "engine/sys/stopwatch.h"
#include "engine/script/degree_ratio.h"
#include "engine/script/context.h"
#include "engine/objects/anim_movies.h"
#include "engine/gfx/render_constants.h"
#include "engine/objects/particle_component.h"
#include "engine/gfx/engine_glue.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/core/checksums.h"
#include "base/maths/vec4.h"
#include <gsl/pointers>
#include <gsl/assert>

namespace gfx {

constexpr int kByte1Shift = 8;
constexpr int kByte2Shift = 16;
constexpr int kByte3Shift = 24;
constexpr uint32_t kBloomAlphaBits = 0x80000000U;
constexpr float kSunChannelMax = 255.0F;
constexpr float kHalfAlphaF = 128.0F;
constexpr int kHalfAlpha = 0x80;
constexpr int kDefaultStateLength = 60000;      // milliseconds
constexpr int kDefaultTransitionLength = 30000;
constexpr int kByteMask = 0xFF;
constexpr int kRgbMask = 0xFFFFFF;
constexpr int kDeadObjectBuckets = 8;

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
float degree_to_radian_conv() { return commands::kDegreeToRadianRatio; } // 0.017453292
float get_zero() { return objects::kFloatZero; }                           // 0.0
float one_percent() { return render::kOnePercent; }                        // 0.01
float one_over255() { return kOneOver255; }                                // 1 / 255
float two_to32() { return kTwoTo32; }                // 4294967296: unsigned integers' correction
float get_one() { return objects::kAnimMoviesOne; } // 1.0
float get_half() { return kHalf; }                    // 0.5
float hundred() { return kHundred; }                  // 100.0
float thousand() { return kThousand; }                // 1000.0

// The C runtime's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A time into a period and the period's length.
struct TimeSpan {
    int time{};
    int length{};
};

// An unsigned millisecond count over an int length, as the original divides (FILD, + 2^32 if negative, FIDIV).
double get_fraction(TimeSpan span)
{
    const auto time = span.time;
    const auto length = span.length;
    auto value = static_cast<double>(time);
    if (time < 0) {
        value += two_to32();
    }
    return value / static_cast<double>(length);
}

// A colour from three bytes (and the fourth).
uint32_t get_rgba(int red, int green, int b, uint8_t a)
{
    return static_cast<uint8_t>(red) | static_cast<uint8_t>(green) << kByte1Shift |
           static_cast<uint8_t>(b) << kByte2Shift | static_cast<uint32_t>(a) << kByte3Shift;
}
// A colour from a script array [r g b] (elements truncated to bytes; the fourth byte 0).
uint32_t color_from_array(script::Array* array)
{
    auto red = static_cast<uint8_t>(array->fetch_part(0));
    auto green = static_cast<uint8_t>(array->fetch_part(1));
    auto blue = static_cast<uint8_t>(array->fetch_part(2));
    return get_rgba(red, green, blue, 0);
}

// The engine (not rewritten yet).
void change_fog(bool on) { render::engine::enable_fog(on); }
void set_fog_color(uint32_t color) { render::engine::set_fog_color(color); }
void set_fog_distance(float distance) { render::engine::set_fog_distance(distance); }
void change_ambient_tint(uint32_t color) { render::set_light_ambient_color(color); }
void change_light_tint(int index, uint32_t color) { render::set_light_diffuse_color(index, color); }
void set_ambient_modulation(float lo, float hi) { render::change_ambient_scale(lo, hi); }
void set_light_modulation(int index, float lo, float hi) { render::change_diffuse_scale(index, lo, hi); }
void set_light_direction_vector(int index, const maths::Vec4* direction) { render::set_light_direction(index, direction); }
// The sun: its parameters (theta, phi, colour as r g b 128, shown) by mask.
constexpr uint32_t kSunAngles = sun::kTheta | sun::kPhi, kSunColor = sun::kColor, kSunShown = 0x80;
// The level colour as the renderer's material colour (0x006FA7C4's, 0x006FA7C0's, and a global copy).
void set_level_color(std::span<const float, 4> color)
{
    objects::particle_hub()->change_colour(color);
    static_cast<render::FlexibleParticleManager*>(render::flexible_particle_manager())->set_colour_scale(color);
    render::engine::set_level_color(color);
}
void set_bloom_structure(script::Struct* params) { render::screen_effects()->set_bloom_params(params, 0); }
// Whether this is a multiplayer game (a network game, or more than one local player): the lights stay as set.
// (the skate module, the game mode, the front end and the park editor through engine/gfx/game_hooks.h)
bool is_multiplayer() { return render::game_hooks().multiplayer(); }
// The game mode's name (modules::Skate's game mode 0x007CE470).
uint32_t active_ruleset_label() { return render::game_hooks().game_mode_name(); }
// The front end (0x007CE464): +0x30 paused.
bool is_game_halted() { return render::game_hooks().game_halted(); }
// Obj's create_from_node (0x005A32A0): the object of node `name` made.
void create_from_node(uint32_t name) { script::create_object_from_node(name); }

// The scenes (render::CEngine's list 0x006FA7EC): nodes (memory::ListNode) {scene +0, priority, next +8}, ended by a node whose scene is -1.
// A node of the scene list (memory::ListNode): its scene, and whether it is the list's head.
using SceneNode = memory::ListNode;
render::WorldScene* scene_of(const SceneNode* node) { return static_cast<render::WorldScene*>(node->data()); }
bool is_end(const SceneNode* node) { return node->is_head(); }
SceneNode* first_scene() { return (render::first_scene_node()); }
render::WorldScene* sky_scene() { return render::get_sky_scene(); }
// The scene of the level being played (the park editor's shell checksum, looked up by 0x0048C700).
render::WorldScene* level_scene()
{
    const uint32_t name = render::game_hooks().park_shell();
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
    core::down_cast<objects::MeshInstanceModule>(model)->change_tint(color);
}

TodSystem& system_at(CtodManager* manager, int index)
{
    return core::at(manager->systems(), index); // (the original had no bounds check)
}

BloomParams bloom_of(const TodParams& params)
{
    return {.on = params.bloom_on_, .bloom1 = params.bloom1, .bloom2 = params.bloom2, .strength = params.bloom_strength_, .color = params.bloom_color_};
}

// NOT ORIGINAL grouping - tod_manager.cpp's state (the original's globals). Made on first use, never destroyed
// (tod_manager_state()).
struct TodManagerState {
    // NOT ORIGINAL - set_tod_params_filter's (null: none).
    TodParamsFilter params_filter_ = nullptr;
    // NOT ORIGINAL - set_multiplayer_unlock's (null: none).
    MultiplayerUnlock multiplayer_unlock_ = nullptr;
};

TodManagerState& tod_manager_state()
{
    static core::NoDestroy<TodManagerState> state;
    return state.get();
}

bool get_filter(TodParams* params)
{
    return (tod_manager_state().params_filter_ != nullptr) && tod_manager_state().params_filter_(params);
}

// What the original tests (is_multiplayer), then the unlock (THUGPRO's network games): the lights left alone.
bool lights_locked()
{
    return is_multiplayer() &&
           !((tod_manager_state().multiplayer_unlock_ != nullptr) && tod_manager_state().multiplayer_unlock_());
}

} // namespace

void set_tod_params_filter(TodParamsFilter filter) { tod_manager_state().params_filter_ = filter; }
void set_multiplayer_unlock(MultiplayerUnlock unlock) noexcept { tod_manager_state().multiplayer_unlock_ = unlock; }
MultiplayerUnlock get_multiplayer_unlock() { return tod_manager_state().multiplayer_unlock_; }
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
    while (index < kSystems && core::at(systems_, index).sky) {
        ++index;
    }
    return index;
}

// 0x00571140
void CtodManager::get_level_color(uint32_t* out)
{
    const TodSystem& system = system_at(this, current_system());
    if (!system.transitioning) {
        const TodParams& params = system.current;
        *out = (((params.lev_blue_ << kByte1Shift) + params.lev_green_) << kByte1Shift) + params.lev_red_;
        return;
    }
    double fraction = 1.0;
    if (system.transition_length_ != 0) {
        fraction = get_fraction({.time = system.time, .length = system.transition_length_});
        if (!(fraction <= get_one())) {
            fraction = get_one();
        }
    }
    const TodParams& current_params = system.current;
    const TodParams& next_params = system.next;
    const int blue = ftol((static_cast<double>(next_params.lev_blue_ - current_params.lev_blue_) * fraction) +
                          current_params.lev_blue_);
    const int green = ftol((static_cast<double>(next_params.lev_green_ - current_params.lev_green_) * fraction) +
                           current_params.lev_green_);
    const int red = ftol((static_cast<double>(next_params.lev_red_ - current_params.lev_red_) * fraction) +
                         current_params.lev_red_);
    *out = (((blue << kByte1Shift) + green) << kByte1Shift) + red;
}

// 0x00571290
void CtodManager::read_group(int index)
{
    TodSystem& system = system_at(this, index);
    script::Array* groups = nullptr;
    system.params->fetch_list(kGroups, &groups, 1);
    system.group_count_ = static_cast<int>(groups->extent());
    auto* group = groups->struct_at(static_cast<uint32_t>(system.group));
    group->fetch_int_value(kParts, &system.parts, 1);
    group->fetch_int_value(kWaitFrames, &system.wait_frames_, 1);
    group->read_hash_value(kLightGroup, &system.light_group_, 1);
    if (!group->read_hash_value(kResetLightGroup, &system.reset_light_group_, 0)) {
        system.reset_light_group_ = 0;
    }
}

// 0x00571340
TodParams CtodManager::get_lerp(TodParams a, TodParams b, float fraction)
{
    auto integer = [fraction](int from, int to) { return ftol((static_cast<double>(to - from) * fraction) + from); };
    auto real = [fraction](float from, float to) {
        return static_cast<float>(((static_cast<double>(to) - from) * fraction) + from);
    };
    auto byte = [&](uint32_t from, uint32_t to, int shift) {
        const int from_channel = static_cast<int>(from >> shift & 0xFF);
        const int to_channel = static_cast<int>(to >> shift & 0xFF);
        return static_cast<uint32_t>(static_cast<uint8_t>(integer(from_channel, to_channel))) << shift;
    };
    TodParams result = a; // (+0x14..+0x24 and fog_on: not set by the original - stack garbage)
    result.lev_red_ = integer(a.lev_red_, b.lev_red_);
    result.lev_green_ = integer(a.lev_green_, b.lev_green_);
    result.lev_blue_ = integer(a.lev_blue_, b.lev_blue_);
    result.sky_red_ = integer(a.sky_red_, b.sky_red_);
    result.sky_green_ = integer(a.sky_green_, b.sky_green_);
    result.sky_blue_ = integer(a.sky_blue_, b.sky_blue_);
    result.fog_red_ = integer(a.fog_red_, b.fog_red_);
    result.fog_green_ = integer(a.fog_green_, b.fog_green_);
    result.fog_blue_ = integer(a.fog_blue_, b.fog_blue_);
    result.fog_alpha_ = integer(a.fog_alpha_, b.fog_alpha_);
    result.fog_dist_ = integer(a.fog_dist_, b.fog_dist_);
    result.ambient_red_ = integer(a.ambient_red_, b.ambient_red_);
    result.ambient_green_ = integer(a.ambient_green_, b.ambient_green_);
    result.ambient_blue_ = integer(a.ambient_blue_, b.ambient_blue_);
    result.ambient_mod_lo_ = real(a.ambient_mod_lo_, b.ambient_mod_lo_);
    result.ambient_mod_hi_ = real(a.ambient_mod_hi_, b.ambient_mod_hi_);
    result.heading0 = real(a.heading0, b.heading0);
    result.pitch0 = real(a.pitch0, b.pitch0);
    result.red0 = integer(a.red0, b.red0);
    result.green0 = integer(a.green0, b.green0);
    result.blue0 = integer(a.blue0, b.blue0);
    result.mod_lo0_ = real(a.mod_lo0_, b.mod_lo0_);
    result.mod_hi0_ = real(a.mod_hi0_, b.mod_hi0_);
    result.heading_a_ = real(a.heading_a_, b.heading_a_);
    result.pitch1 = real(a.pitch1, b.pitch1);
    result.red1 = integer(a.red1, b.red1);
    result.green1 = integer(a.green1, b.green1);
    result.blue1 = integer(a.blue1, b.blue1);
    result.mod_lo1_ = real(a.mod_lo1_, b.mod_lo1_);
    result.mod_hi1_ = real(a.mod_hi1_, b.mod_hi1_);
    result.bloom_on_ = a.bloom_on_;
    result.bloom1 = integer(a.bloom1, b.bloom1);
    result.bloom2 = integer(a.bloom2, b.bloom2);
    // The bloom colours byte by byte; their fourth byte is 0x80.
    result.bloom_strength_ = byte(a.bloom_strength_, b.bloom_strength_, 0) |
                             byte(a.bloom_strength_, b.bloom_strength_, kByte1Shift) |
                             byte(a.bloom_strength_, b.bloom_strength_, kByte2Shift) | kBloomAlphaBits;
    result.bloom_color_ = byte(a.bloom_color_, b.bloom_color_, 0) | byte(a.bloom_color_, b.bloom_color_, kByte1Shift) |
                          byte(a.bloom_color_, b.bloom_color_, kByte2Shift) | kBloomAlphaBits;
    return result;
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

    float value = 0.0F;
    if (structure->fetch_float(kSunTheta, &value, 0)) {
        out->sun_theta_ = static_cast<float>(static_cast<double>(value) * degree_to_radian_conv());
    }
    if (structure->fetch_float(kSunPhi, &value, 0)) {
        out->sun_phi_ = static_cast<float>(static_cast<double>(value) * degree_to_radian_conv());
    }
    out->sun_red_ = kSunChannelMax;
    out->sun_green_ = kSunChannelMax;
    out->sun_blue_ = kSunChannelMax;
    if (structure->fetch_float(kSunRed, &value, 0)) {
        out->sun_red_ = value;
    }
    if (structure->fetch_float(kSunGreen, &value, 0)) {
        out->sun_green_ = value;
    }
    if (structure->fetch_float(kSunBlue, &value, 0)) {
        out->sun_blue_ = value;
    }

    structure->fetch_int_value(kBloomOn, &out->bloom_on_, 0);
    if (out->bloom_on_ == 0) {
        return;
    }
    structure->fetch_int_value(kBloom1, &out->bloom1, 0);
    structure->fetch_int_value(kBloom2, &out->bloom2, 0);
    script::Array* array = nullptr;
    if (structure->fetch_list(kBloomStrength, &array, 0)) {
        out->bloom_strength_ = color_from_array(array);
    }
    array = nullptr;
    if (structure->fetch_list(kBloomColor, &array, 0)) {
        out->bloom_color_ = color_from_array(array);
    }
}

// 0x00571E00
TodParams CtodManager::get_tod_params(int index)
{
    if (index == -1) {
        index = current_system();
    }
    const TodSystem& system = system_at(this, index);
    if (!system.transitioning) {
        return system.current;
    }
    float blend = 1.0F;
    if (system.transition_length_ != 0) {
        const double fraction = get_fraction({.time = system.time, .length = system.transition_length_});
        blend = static_cast<float>(fraction);
        if (!(fraction <= get_one())) {
            blend = 1.0F;
        }
    }
    return get_lerp(system.current, system.next, blend);
}

// 0x00571EF0
void CtodManager::build_tod_objects(script::Struct* params, script::Script* /*script*/)
{
    uint32_t name = 0;
    if (params->read_hash_value(kName, &name, 0)) {
        if (!dead_objects_->fetch_entry(name, true)) {
            create_from_node(name);
        }
        return;
    }
    uint32_t prefix = 0;
    if (!params->hash_from_name_or_text(kPrefix, &prefix, 0)) {
        return;
    }
    // (NoPrefixWarning: passed on as the third argument of 0x0045BF90, which doesn't use it)
    params->has_bare_flag(kNoPrefixWarning);
    for (const uint32_t node : level_nodes::collect_nodes_by_prefix(prefix)) {
        if (!dead_objects_->fetch_entry(node)) {
            create_from_node(node);
        }
    }
}

// 0x00572130
void CtodManager::change_args(script::Struct* params)
{
    params_set_ = true;
    int number = 0;
    params->fetch_int_value(kSystem, &number, 1);
    TodSystem& system = system_at(this, number - 1);
    system.params->get_clear();
    system.params->append_structure_if_any(params);
    params->read_hash_value(kCurrentState, &system.state, 0);
    system.next_state_ = next_tod_state(system.state);
    script::Struct* state = nullptr;
    if (params->read_sub_struct(kOverwriteCurrentState, &state, 0)) {
        parse_params(state, &system.current);
    } else if (params->read_sub_struct(system.state, &state, 0)) {
        parse_params(state, &system.current);
        state->fetch_int_value(kStateLength, &system.state_length_, 1);
    }
    if (params->read_sub_struct(system.next_state_, &state, 0)) {
        parse_params(state, &system.next);
    }
    params->fetch_int_value(kTransitionLength, &system.transition_length_, 0);
    system.sky = params->has_bare_flag(kSky);
}

// 0x00572320
void CtodManager::set_fog_active(bool on)
{
    const bool was = fog_active_;
    fog_active_ = on;
    if (was) {
        if (!on) {
            change_fog(false);
        }
        return;
    }
    if (!on) {
        return;
    }
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
    if (system.state == kNight) {
        ++system.days_passed_;
    }
    system.state = system.next_state_;
    system.next_state_ = next_tod_state(system.state);
    script::Struct* state = nullptr;
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
    const TodSystem& system = core::at(systems_, 0);
    double fraction = 0.0;
    if (system.transitioning) {
        fraction = get_fraction(
            {.time = system.time + system.state_length_, .length = system.transition_length_ + system.state_length_});
    } else {
        fraction = get_fraction({.time = system.time, .length = system.state_length_ + system.transition_length_});
    }
    auto blend = [fraction](float from, float to) {
        return static_cast<float>(((static_cast<double>(to) - from) * fraction) + from);
    };
    const TodParams& current_params = system.current;
    const TodParams& next_params = system.next;
    sun::Settings sun = {};
    sun.theta = blend(current_params.sun_theta_, next_params.sun_theta_);
    sun.phi = blend(current_params.sun_phi_, next_params.sun_phi_);
    sun.color = {.x = blend(current_params.sun_red_, next_params.sun_red_),
                 .y = blend(current_params.sun_green_, next_params.sun_green_),
                 .z = blend(current_params.sun_blue_, next_params.sun_blue_),
                 .w = kHalfAlphaF};
    sun.shown = system.state != kNight;
    sun::set_sun(&sun, kSunAngles | kSunColor | kSunShown);
}

namespace {
// Which light groups a colour goes to (set and reset) and over which part of the day.
struct ColourSpan {
    uint32_t light_group_{};
    uint32_t reset_light_group_{};
    int start_percent_{};
    int end_percent_{};
};

// 0x005725F0
void apply_color_to_scene(render::WorldScene* scene, uint32_t color, ColourSpan colour_span)
{
    const auto light_group = colour_span.light_group_;
    const auto reset_light_group = colour_span.reset_light_group_;
    const auto start_percent = colour_span.start_percent_;
    const auto end_percent = colour_span.end_percent_;
    // The scene's sectors: their light group and geometry (change_tint: its colour).
    lists::KeyMap* table = scene->sectors();
    if (!table) {
        return;
    }
    auto colour = [](render::WorldCell* sector, uint32_t value) {
        void* geometry = sector->geom_handle();
        if (geometry) {
            static_cast<render::Geom*>(geometry)->change_tint(value);
        }
    };
    auto next = [table]() -> render::WorldCell* {
        if (!table->step_iterator() || !table->iter_item()) {
            return nullptr;
        }
        return static_cast<render::WorldCell*>(table->iter_item()->value);
    };
    table->set_iter_bucket(-1);
    table->set_iter_item(nullptr);
    render::WorldCell* sector = nullptr;
    if (table->step_iterator() && table->iter_item()) {
        sector = static_cast<render::WorldCell*>(table->iter_item()->value);
    }
    const int count = table->extent();
    int remaining = count;
    if (start_percent >= 0) {
        const int skip = ftol(static_cast<double>(start_percent) * count * one_percent());
        if (start_percent < end_percent) {
            int last = ftol(static_cast<double>(end_percent) * count * one_percent());
            last = std::min(last, count);
            remaining = last - skip;
        }
        for (int i = skip; i != 0; --i) {
            sector = next();
        }
    }
    if (!sector) {
        return;
    }
    if (light_group == 0U) {
        for (bool again = true; again; again = (sector = next()) != nullptr) {
            if (remaining-- == 0) {
                return;
            }
            colour(sector, color);
        }
    } else if (reset_light_group == 0U) {
        for (bool again = true; again; again = (sector = next()) != nullptr) {
            if (remaining-- == 0) {
                return;
            }
            if (sector->light_group() == light_group) {
                colour(sector, color);
            }
        }
    } else {
        for (bool again = true; again; again = (sector = next()) != nullptr) {
            if (remaining-- == 0) {
                return;
            }
            const uint32_t group = sector->light_group();
            if (group == light_group) {
                colour(sector, color);
            } else if (group == reset_light_group) {
                colour(sector, kNeutralColor);
            }
        }
    }
}
} // namespace

// 0x005727F0 (and 0x00572821)
void CtodManager::apply_color(uint32_t color, uint32_t sky_color, bool sky, uint32_t light_group,
                              uint32_t reset_light_group, bool part_only, int start_percent, int end_percent)
{
    if (sky) {
        render::WorldScene* scene = sky_scene();
        if (scene) {
            apply_color_to_scene(scene, sky_color, {.light_group_ = light_group, .reset_light_group_ = 0, .start_percent_ = -1, .end_percent_ = -1});
        }
        return;
    }
    const bool whole_level = light_group == kOutdoor || light_group == 0;
    for (const SceneNode* node = first_scene(); node;) {
        const SceneNode* next = is_end(node->next()) ? nullptr : node->next();
        render::WorldScene* scene = scene_of(node);
        if (!scene->is_sky()) {
            apply_color_to_scene(scene, color, {.light_group_ = light_group, .reset_light_group_ = reset_light_group, .start_percent_ = start_percent, .end_percent_ = end_percent});
            if (whole_level && !part_only) {
                set_scene_color_of(scene, color);
            }
        }
        node = next;
    }
    render::WorldScene* level = level_scene();
    if (level) {
        apply_color_to_scene(level, color, {.light_group_ = light_group, .reset_light_group_ = reset_light_group, .start_percent_ = start_percent, .end_percent_ = end_percent});
        if (whole_level && !part_only) {
            set_scene_color_of(level, color);
        }
    }
    if ((whole_level || (reset_light_group != 0U)) && !part_only) {
        // The models: those of objects without a sector of their name, or an outdoor one, get the colour.
        for (objects::ObjectModule* model = objects::object_hub_slot()->find_first_module_of_kind(kModel); model;
             model = model->next_of_type()) {
            const render::WorldCell* sector = look_up_sector(model->entity()->ident());
            if (!sector || sector->light_group() == kOutdoor) {
                set_model_color(model, color);
            }
            if ((reset_light_group != 0U) && sector && sector->light_group() == reset_light_group) {
                set_model_color(model, kNeutralColor);
            }
        }
    }
}

// 0x00572980
void set_light_direction(int index, LightAngles angles)
{
    const auto heading = angles.heading;
    const auto pitch = angles.pitch;
    const double pitch_radians = static_cast<double>(pitch) * degree_to_radian_conv();
    const double sin_pitch = std::sin(pitch_radians);
    const double cos_pitch = std::cos(pitch_radians);
    maths::Vec4 direction{};
    direction.w = 0.0F;
    direction.y = static_cast<float>((static_cast<double>(get_zero()) * cos_pitch) - sin_pitch);
    const double horizontal = cos_pitch + (sin_pitch * get_zero());
    const double heading_radians = static_cast<double>(heading) * degree_to_radian_conv();
    auto sin_heading = static_cast<float>(std::sin(heading_radians));
    const double cos_heading = std::cos(heading_radians);
    auto cos_heading_float = static_cast<float>(std::cos(heading_radians));
    direction.z = static_cast<float>((cos_heading * horizontal) + (static_cast<double>(sin_heading) * get_zero()));
    direction.x =
        static_cast<float>((horizontal * sin_heading) + (static_cast<double>(cos_heading_float) * get_zero()));
    set_light_direction_vector(index, &direction);
}

// 0x00572A10
void CtodManager::apply_tod_params(TodParams params) const
{
    // (NOT ORIGINAL: an edited level's own ambient light and fog put over the state's - set_tod_params_filter; its fog
    // shown even when the manager's is off)
    const bool fog_forced = get_filter(&params);
    change_ambient_tint(get_rgba(params.ambient_red_, params.ambient_green_, params.ambient_blue_, kHalfAlpha));
    set_light_direction(0, {.heading = params.heading0, .pitch = params.pitch0});
    set_light_direction(1, {.heading = params.heading_a_, .pitch = params.pitch1});
    change_light_tint(0, get_rgba(params.red0, params.green0, params.blue0, kHalfAlpha));
    change_light_tint(1, get_rgba(params.red1, params.green1, params.blue1, kHalfAlpha));
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
        const double scaled = static_cast<double>(value) * one_over255();
        return static_cast<float>(scaled + scaled);
    };
    std::array<float, 4> color = {
        {channel(params.lev_red_), channel(params.lev_green_), channel(params.lev_blue_), 1.0F}};
    set_level_color(color);
}

// 0x00572BD0
void CtodManager::clear_dead_objects() { dead_objects_->empty_table(); }

// 0x005730E0
void CtodManager::get_reset()
{
    params_set_ = false;
    full_length_ = 0;
    time_multiplier_ = 1.0F;
    transition_script_ = 0;
    for (TodSystem& system : systems_) {
        system.time = 0;
        system.state_length_ = kDefaultStateLength;
        system.transition_length_ = kDefaultTransitionLength;
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
void add_bloom_params(BloomParams bloom, script::Struct* out)
{
    const auto on = bloom.on;
    const auto bloom1 = bloom.bloom1;
    const auto bloom2 = bloom.bloom2;
    const auto strength = bloom.strength;
    const auto color = bloom.color;
    out->put_int(checksums::Name{kOn}, on);
    out->put_int(checksums::Name{kBloom1}, bloom1);
    out->put_int(checksums::Name{kBloom2}, bloom2);
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    script::Array* array = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    Expects(array != nullptr);
    array->configure_size_kind(3, script::QbValueKind::kInteger);
    array->set_element(0, strength & kByteMask);
    array->set_element(1, strength >> kByte1Shift & kByteMask);
    array->set_element(2, strength >> kByte2Shift & kByteMask);
    out->append_array_value(kStrength, array);
    array->set_element(0, color & kByteMask);
    array->set_element(1, color >> kByte1Shift & kByteMask);
    array->set_element(2, color >> kByte2Shift & kByteMask);
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
    const bool scene_only = params->has_bare_flag(kSceneColorFlag);
    int value = 0;
    params->fetch_int_value(kColor, &value, 1);
    // (the colour's fourth byte isn't set by the original - stack garbage; nor the sky colour's, the top byte of the
    // parameters' pointer whose argument slot it's kept in)
    const uint32_t color = static_cast<uint32_t>(value) & 0xFFFFFF;
    uint32_t sky_color = 0;
    int sky = value;
    bool is_sky = false;
    if (params->fetch_int_value(kSky, &sky, 0)) {
        is_sky = true;
        sky_color = static_cast<uint32_t>(sky) & kRgbMask;
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
    uint32_t name = 0;
    if (params->read_hash_value(kName, &name, 0)) {
        if (!dead_objects_->fetch_entry(name, true)) {
            dead_objects_->store_entry(name, this);
        }
        return;
    }
    uint32_t prefix = 0;
    if (!params->hash_from_name_or_text(kPrefix, &prefix, 0)) {
        return;
    }
    params->has_bare_flag(kNoPrefixWarning); // (see build_tod_objects)
    for (const uint32_t node : level_nodes::collect_nodes_by_prefix(prefix)) {
        if (!dead_objects_->fetch_entry(node)) {
            dead_objects_->store_entry(node, this);
        }
    }
}

// 0x00573500
void CtodManager::set_bloom(int on, int bloom1, int bloom2, uint32_t strength, uint32_t color)
{
    const script::StructPtr params = script::make_struct();
    if (on != 0) {
        add_bloom_params({.on = on, .bloom1 = bloom1, .bloom2 = bloom2, .strength = strength, .color = color}, params.get());
    } else {
        params->put_int(checksums::Name{kOn}, 0);
    }
    set_bloom_structure(params.get());
}

// 0x00573620
CtodManager::CtodManager() : unknown4_(script::new_struct())
{
    void* table = memory::alloc(sizeof(lists::KeyMap));
    dead_objects_ = table ? memory::construct_in<lists::KeyMap>(table, kDeadObjectBuckets) : nullptr;
    for (TodSystem& system : systems_) {
        gsl::owner<script::Struct*> params = script::new_struct(); // (the system's: the destructor frees it)
        system.params = params;
    }
    get_reset();
}

// 0x00573770
CtodManager::~CtodManager()
{
    for (TodSystem& system : systems_) {
        script::free_struct(memory::owned_element(system.params));
        system.params = nullptr;
    }
    script::free_struct(unknown4_);
    dead_objects_->empty_table();
    if (dead_objects_) {
        dead_objects_->~KeyMap();
        memory::Hub::dealloc(dead_objects_);
    }
}

// 0x00573810
void CtodManager::update_system(int index)
{
    TodSystem& system = system_at(this, index);
    const int part = system.part;
    ++system.frame;
    if (part != 0 && system.frame <= system.wait_frames_) {
        return;
    }

    // This part's share of the sectors.
    const double step = hundred() / static_cast<double>(system.parts);
    const int start = ftol(part * step);
    const int end = ftol((part + 1) * step);
    float blend = 1.0F;
    if (system.transition_length_ != 0) {
        const double fraction = get_fraction({.time = system.time, .length = system.transition_length_});
        blend = static_cast<float>(fraction);
        if (!(fraction <= get_one())) {
            blend = 1.0F;
        }
    }
    const TodParams params = get_lerp(system.current, system.next, blend);
    const uint32_t level_color = get_rgba(params.lev_red_, params.lev_green_, params.lev_blue_, 0x80);
    const uint32_t sky_color = get_rgba(params.sky_red_, params.sky_green_, params.sky_blue_, 0x80);
    const bool sky = system.sky;
    apply_color(level_color, sky_color, sky, system.light_group_, system.reset_light_group_, part > 0, start, end);
    if (!sky && !level_lights_reset_) {
        apply_tod_params(params);
    }

    // The next part (or the next group, after the last part).
    system.frame = 0;
    if (++system.part >= system.parts) {
        system.part = 0;
        if (system.group < system.group_count_ - 1) {
            ++system.group;
        } else {
            system.group = 0;
        }
        read_group(index);
    }
    if (!sky && (temp_bloom_.on == 0)) {
        set_bloom(params.bloom_on_, params.bloom1, params.bloom2, params.bloom_strength_, params.bloom_color_);
    }

    // The transition script: half-way through (evening's at once).
    const float threshold = system.state == kEvening ? get_zero() : get_half();
    if (!system.script_run_ && blend > threshold) {
        system.script_run_ = true;
        const script::StructPtr script_params = script::make_struct();
        script_params->put_name_key(checksums::Name{kCurrentTod}, system.state);
        script_params->put_name_key(checksums::Name{kNextTod}, system.next_state_);
        script_params->put_sub_struct(kTodStateParams, system.params);
        if (!sky) {
            if (transition_script_ != 0U) {
                script_params->put_name_key(checksums::Name{kTransitionScript}, transition_script_);
            }
            script::spawn_script(kStateTransition, script_params.get());
        }
    }

    if (system.time > system.transition_length_) {
        next_state(index);
        if (!system.sky && (temp_bloom_.on == 0)) {
            const TodParams& current = system.current;
            set_bloom(current.bloom_on_, current.bloom1, current.bloom2, current.bloom_strength_, current.bloom_color_);
        }
    }
}

// 0x00573BE0
void CtodManager::reset_level_lights(uint32_t state_name)
{
    if (lights_locked()) { // (the unlock: NOT ORIGINAL - set_multiplayer_unlock)
        return;
    }
    TodParams params{};
    if (state_name == 0U) {
        params = get_tod_params(-1);
        apply_tod_params(params);
        level_lights_reset_ = false;
    } else {
        std::memset(&params, 0, sizeof(params)); // (the original's are uninitialised: what the state doesn't give)
        script::Struct* state = nullptr;
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
    const bool was_active = active_;
    active_ = true;
    for (TodSystem& system : systems_) {
        const TodParams& current = system.current;
        const uint32_t level_color = get_rgba(current.lev_red_, current.lev_green_, current.lev_blue_, 0x80);
        const uint32_t sky_color = get_rgba(current.sky_red_, current.sky_green_, current.sky_blue_, 0x80);
        const bool sky = system.sky;
        script::Array* groups = nullptr;
        system.params->fetch_list(kGroups, &groups, 1);
        for (int group_index = 0; group_index < system.group_count_; ++group_index) {
            auto* group = groups->struct_at(static_cast<uint32_t>(group_index));
            uint32_t light_group = 0;
            group->read_hash_value(kLightGroup, &light_group, 1);
            uint32_t reset_light_group = 0;
            group->read_hash_value(kResetLightGroup, &reset_light_group, 0);
            auto step = static_cast<float>(hundred() / static_cast<double>(system.parts));
            for (int part = 0; part < system.parts; ++part) {
                const int start = ftol(static_cast<double>(part) * step);
                const int end = ftol(static_cast<double>(part + 1) * step);
                apply_color(level_color, sky_color, sky, light_group, reset_light_group, part > 0, start, end);
            }
            if (!sky && !level_lights_reset_) {
                apply_tod_params(system.current);
            }
        }
        if (sky) {
            continue;
        }
        const script::StructPtr params = script::make_struct();
        params->put_name_key(checksums::Name{kCurrentTod}, previous_tod_state(system.state));
        params->put_name_key(checksums::Name{kNextTod}, system.state);
        params->put_sub_struct(kTodStateParams, system.params);
        if (transition_script_ != 0U) {
            params->put_name_key(checksums::Name{kTransitionScript}, transition_script_);
        }
        params->put_name_key(checksums::Name{0U}, kInstantChange);
        if (temp_state) {
            params->put_name_key(checksums::Name{0U}, kTempState);
        }
        script::StructPtr bloom = script::make_struct();
        add_bloom_params({.on = current.bloom_on_, .bloom1 = current.bloom1, .bloom2 = current.bloom2, .strength = current.bloom_strength_, .color = current.bloom_color_}, bloom.get());
        params->put_sub_struct(kBloomParams, bloom.get());
        bloom.reset();
        script::spawn_script(kStateTransition, params.get());
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
    script::find_symbol(kTempTodChanged)->value().set_integer(1);
    for (TodSystem& system : systems_) {
        parse_params(params, &system.current);
    }
    refresh_all(true);
}

// 0x00574210
void CtodManager::restore_current_tod()
{
    for (TodSystem& system : systems_) {
        script::Struct* state = nullptr;
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
    if (params->fetch_list(kStrength, &array, 0)) {
        temp_bloom_.strength = color_from_array(array);
    }
    array = nullptr;
    if (params->fetch_list(kColor, &array, 0)) {
        temp_bloom_.color = color_from_array(array);
    }
    // Off: back to the state's bloom (in a single player game).
    if ((temp_bloom_.on == 0) && !is_multiplayer()) {
        const BloomParams bloom = bloom_of(system_at(this, current_system()).current);
        set_bloom(bloom.on, bloom.bloom1, bloom.bloom2, bloom.strength, bloom.color);
        return;
    }
    set_bloom(temp_bloom_.on, temp_bloom_.bloom1, temp_bloom_.bloom2, temp_bloom_.strength, temp_bloom_.color);
}

// 0x00574400
void CtodManager::enable_bloom(int on)
{
    if (lights_locked()) { // (the unlock: NOT ORIGINAL - set_multiplayer_unlock)
        return;
    }
    TodParams& current = system_at(this, current_system()).current;
    if (on == current.bloom_on_) {
        return;
    }
    current.bloom_on_ = on;
    set_bloom(current.bloom_on_, current.bloom1, current.bloom2, current.bloom_strength_, current.bloom_color_);
}

// 0x00574590
void CtodManager::tick_frame()
{
    if (active_ruleset_label() != kCareer || script::fetch_int_value(kInactive) == 1 || temp_tod_ || !active_ ||
        is_game_halted()) {
        return;
    }
    // (not original: on the 60 Hz grid - the time of day steps once each 1/60 s whatever the tick rate, its truncated
    // milliseconds and its systems' frame counts (update_system's wait_frames_) as the 60 Hz run's; every tick at 60.
    // Original behaviour: once a tick, the tick's own step)
    if (frame_clock::grid_frames() == 0) {
        return;
    }
    const int elapsed = ftol(frame_clock::get_slomo_grid_seconds() * time_multiplier_ * thousand());
    full_length_ += elapsed;
    update_sun();
    for (int index = 0; index < kSystems; ++index) {
        TodSystem& system = core::at(systems_, index);
        system.time += elapsed;
        if (!system.transitioning && system.time > system.state_length_) {
            system.time = 0;
            system.transitioning = true;
            read_group(index);
        }
        if (system.transitioning) {
            update_system(index);
        }
    }
}

// 0x00574670 (a state that never comes - not one of the four - loops forever, as in the original)
void CtodManager::set_tod_instant(uint32_t state)
{
    if (lights_locked()) { // (the unlock: NOT ORIGINAL - set_multiplayer_unlock)
        return;
    }
    // How long until `state`, from the first non-sky system.
    TodSystem& current = system_at(this, current_system());
    int remaining = current.transitioning ? current.transition_length_ - current.time
                                          : current.state_length_ - current.time + current.transition_length_;
    for (uint32_t state_id = next_tod_state(current.state); state_id != state; state_id = next_tod_state(state_id)) {
        script::Struct* structure = nullptr;
        current.params->read_sub_struct(state_id, &structure, 1);
        int length = 0;
        structure->fetch_int_value(kStateLength, &length, 1);
        remaining += current.transition_length_ + length;
    }
    // Every system moved on by that long.
    for (int index = 0; index < kSystems; ++index) {
        TodSystem& system = core::at(systems_, index);
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

// NOT ORIGINAL (game/online/thugpro_peer.h)
uint32_t CtodManager::current_state() const
{
    const int index = current_system();
    return index < kSystems ? core::at(systems_, index).state : 0;
}

// NOT ORIGINAL (game/online/thugpro_peer.h): set_tod_instant's walk, without moving - at most once round the four states:
// `state` is reached (an unknown current state or target never is) and every state on the way has its structure.
bool CtodManager::can_set_tod_instant(uint32_t state) const
{
    const int index = current_system();
    if (index >= kSystems) {
        return false;
    }
    const TodSystem& current = core::at(systems_, index);
    if (!current.params) {
        return false;
    }
    uint32_t state_id = next_tod_state(current.state);
    for (int step = 0; step < 4 && (state_id != 0U); ++step, state_id = next_tod_state(state_id)) {
        if (state_id == state) {
            return true;
        }
        script::Struct* structure = nullptr;
        if (!current.params->read_sub_struct(state_id, &structure, 0) || !structure) {
            return false;
        }
    }
    return false;
}

CtodManager*& tod_manager()
{
    static gfx::CtodManager* tod_manager{};
    return tod_manager;
}
namespace {
uint32_t& ref_tally()
{
    static uint32_t tod_references{};
    return tod_references;
}
} // namespace

// 0x00574540
CtodManager* create_tod_manager(bool create)
{
    CtodManager*& manager = tod_manager();
    if (!manager && create) {
        void* memory = memory::alloc_zeroed(sizeof(CtodManager), nullptr, 1);
        manager = memory ? memory::construct_in<CtodManager>(memory) : nullptr;
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
        memory::Hub::dealloc(manager);
    }
    tod_manager() = nullptr;
    ref_tally() = 0;
}

// --- the script commands ------------------------------------------------------------------------------------

// 0x00571FE0: TODManager_SetActive <0|1>
bool cfunc_tod_manager_set_active(script::Struct* params, script::Context /*context*/)
{
    CtodManager* manager = tod_manager();
    int active = 0;
    params->fetch_int_value(0U, &active, 1);
    manager->set_active(active != 0);
    return true;
}

// 0x00572010: TODManager_GetNumberOfDaysPassed [system_index=n] -> num_days_passed
bool cfunc_tod_manager_get_number_of_days_passed(script::Struct* params, script::Context context)
{
    CtodManager* manager = tod_manager();
    int index = 0;
    params->fetch_int_value(kSystemIndex, &index, 0);
    context.args()->put_int(checksums::Name{kNumDaysPassed}, system_at(manager, index).days_passed_);
    return true;
}

// 0x00572060: TODManager_SetTimeMultiplier <multiplier> (default 1)
bool cfunc_tod_manager_set_time_multiplier(script::Struct* params, script::Context /*context*/)
{
    float multiplier = 1.0F;
    params->fetch_float(0U, &multiplier, 0);
    if (!tod_manager()) {
        return false;
    }
    tod_manager()->set_time_multiplier(multiplier);
    return true;
}

// 0x005720A0: TODManager_GetTimeMultiplier -> TOD_Time_Multiplier
bool cfunc_tod_manager_get_time_multiplier(script::Struct* /*params*/, script::Context context)
{
    if (!tod_manager()) {
        return false;
    }
    context.args()->push_float(checksums::Name{kTimeMultiplierName}, tod_manager()->time_multiplier());
    return true;
}

// 0x005720D0: TODManager_SetTODScript <script>
bool cfunc_tod_manager_set_tod_script(script::Struct* params, script::Context /*context*/)
{
    CtodManager* manager = tod_manager();
    uint32_t name = 0;
    params->read_hash_value(0U, &name, 0);
    manager->set_transition_script(name);
    return true;
}

// 0x00572110: TODManager_Create name= | prefix=
bool cfunc_tod_manager_create(script::Struct* params, script::Context context)
{
    tod_manager()->build_tod_objects(params, context.script());
    return true;
}

// 0x00572BE0: TODManager_SetParams
bool cfunc_tod_manager_set_params(script::Struct* params, script::Context /*context*/)
{
    tod_manager()->change_args(params);
    return true;
}

// 0x00572C00: TODManager_GetCurrentTODParams [system_index=n] -> tod_params={active current_state_name lev_red ...}
bool cfunc_tod_manager_get_current_tod_params(script::Struct* params, script::Context context)
{
    int index = -1;
    params->fetch_int_value(kSystemIndex, &index, 0);
    CtodManager* manager = tod_manager();
    const TodParams tod_params = manager->get_tod_params(index);
    gsl::owner<script::Struct*> out = script::new_struct();
    out->put_int(checksums::Name{kActive}, static_cast<int>(manager->active()));
    int current = 0;
    while (current < CtodManager::kSystems && core::at(manager->systems(), current).sky) {
        ++current;
    }
    out->put_name_key(checksums::Name{kCurrentStateName}, system_at(manager, current).state);
    out->put_int(checksums::Name{kLevRed}, tod_params.lev_red_);
    out->put_int(checksums::Name{kLevGreen}, tod_params.lev_green_);
    out->put_int(checksums::Name{kLevBlue}, tod_params.lev_blue_);
    out->put_int(checksums::Name{kSkyRed}, tod_params.sky_red_);
    out->put_int(checksums::Name{kSkyGreen}, tod_params.sky_green_);
    out->put_int(checksums::Name{kSkyBlue}, tod_params.sky_blue_);
    out->put_int(checksums::Name{kFogOn}, tod_params.fog_on_);
    out->put_int(checksums::Name{kFogRed}, tod_params.fog_red_);
    out->put_int(checksums::Name{kFogGreen}, tod_params.fog_green_);
    out->put_int(checksums::Name{kFogBlue}, tod_params.fog_blue_);
    out->put_int(checksums::Name{kFogAlpha}, tod_params.fog_alpha_);
    out->put_int(checksums::Name{kFogDist}, tod_params.fog_dist_);
    out->put_int(checksums::Name{kAmbientRed}, tod_params.ambient_red_);
    out->put_int(checksums::Name{kAmbientGreen}, tod_params.ambient_green_);
    out->put_int(checksums::Name{kAmbientBlue}, tod_params.ambient_blue_);
    out->push_float(checksums::Name{kAmbientModLo}, tod_params.ambient_mod_lo_);
    out->push_float(checksums::Name{kAmbientModHi}, tod_params.ambient_mod_hi_);
    out->push_float(checksums::Name{kHeading0}, tod_params.heading0);
    out->push_float(checksums::Name{kPitch0}, tod_params.pitch0);
    out->put_int(checksums::Name{kRed0}, tod_params.red0);
    out->put_int(checksums::Name{kGreen0}, tod_params.green0);
    out->put_int(checksums::Name{kBlue0}, tod_params.blue0);
    out->push_float(checksums::Name{kModLo0}, tod_params.mod_lo0_);
    out->push_float(checksums::Name{kModHi0}, tod_params.mod_hi0_);
    out->push_float(checksums::Name{kHeading1}, tod_params.heading_a_);
    out->push_float(checksums::Name{kPitch1}, tod_params.pitch1);
    out->put_int(checksums::Name{kRed1}, tod_params.red1);
    out->put_int(checksums::Name{kGreen1}, tod_params.green1);
    out->put_int(checksums::Name{kBlue1}, tod_params.blue1);
    out->push_float(checksums::Name{kModLo1}, tod_params.mod_lo1_);
    out->push_float(checksums::Name{kModHi1}, tod_params.mod_hi1_);
    out->put_int(checksums::Name{kBloomOn}, tod_params.bloom_on_);
    out->put_int(checksums::Name{kBloom1}, tod_params.bloom1);
    out->put_int(checksums::Name{kBloom2}, tod_params.bloom2);
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    script::Array* array = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    array->configure_size_kind(3, script::QbValueKind::kInteger);
    array->set_element(0, tod_params.bloom_strength_ & kByteMask);
    array->set_element(1, tod_params.bloom_strength_ >> kByte1Shift & kByteMask);
    array->set_element(2, tod_params.bloom_strength_ >> kByte2Shift & kByteMask);
    out->append_array_value(kBloomStrength, array);
    array->set_element(0, tod_params.bloom_color_ & kByteMask);
    array->set_element(1, tod_params.bloom_color_ >> kByte1Shift & kByteMask);
    array->set_element(2, tod_params.bloom_color_ >> kByte2Shift & kByteMask);
    out->append_array_value(kBloomColor, array);
    script::release_array_storage(array);
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
    context.args()->append_struct_ref(kTodParams, out);
    return true;
}

// 0x005730A0: TODManager_SetFogActive [0|1] (default 1)
bool cfunc_tod_manager_set_fog_active(script::Struct* params, script::Context /*context*/)
{
    CtodManager* manager = tod_manager();
    int on = 1;
    params->fetch_int_value(0U, &on, 0);
    manager->set_fog_active(on != 0);
    return true;
}

// 0x005735F0: TODManager_MarkPermanentlyDead name= | prefix=
bool cfunc_tod_manager_mark_permanently_dead(script::Struct* params, script::Context /*context*/)
{
    tod_manager()->mark_permanently_dead(params);
    return true;
}

// 0x00573610: TODManager_Reset
bool cfunc_tod_manager_reset(script::Struct* /*params*/, script::Context /*context*/)
{
    tod_manager()->get_reset();
    return true;
}

// 0x00574480: TODManager_ResetLevelLights [state_name=]
bool cfunc_tod_manager_reset_level_lights(script::Struct* params, script::Context /*context*/)
{
    CtodManager* manager = tod_manager();
    uint32_t name = 0;
    params->read_hash_value(kStateName, &name, 0);
    manager->reset_level_lights(name);
    return true;
}

// 0x005744C0: TODManager_SetTempTOD <params>
bool cfunc_tod_manager_set_temp_tod(script::Struct* params, script::Context /*context*/)
{
    tod_manager()->set_temp_tod(params);
    return true;
}

// 0x005744E0: TODManager_RestoreCurrentTOD
bool cfunc_tod_manager_restore_current_tod(script::Struct* /*params*/, script::Context /*context*/)
{
    tod_manager()->restore_current_tod();
    return true;
}

// 0x005744F0: TODManager_SetTempBloomParams on= bloom1= bloom2= strength=[] color=[]
bool cfunc_tod_manager_set_temp_bloom_params(script::Struct* params, script::Context /*context*/)
{
    tod_manager()->set_temp_bloom_params(params);
    return true;
}

// 0x00574510: TODManager_EnableBloom <0|1>
bool cfunc_tod_manager_enable_bloom(script::Struct* params, script::Context /*context*/)
{
    CtodManager* manager = tod_manager();
    int on = 0;
    params->fetch_int_value(0U, &on, 1);
    manager->enable_bloom(on);
    return true;
}

// 0x00574840: TODManager_SetTODInstant <state>
bool cfunc_tod_manager_set_tod_instant(script::Struct* params, script::Context /*context*/)
{
    uint32_t state = 0;
    params->read_hash_value(0U, &state, 1);
    if (!tod_manager()) {
        return false;
    }
    tod_manager()->set_tod_instant(state);
    return true;
}

// 0x00574870: TODManager_AdvanceTODState: straight to the next state.
bool cfunc_tod_manager_advance_tod_state(script::Struct* /*params*/, script::Context /*context*/)
{
    CtodManager* manager = tod_manager();
    if (!manager) {
        return false;
    }
    int current = 0;
    while (current < CtodManager::kSystems && core::at(manager->systems(), current).sky) {
        ++current;
    }
    manager->set_tod_instant(next_tod_state(system_at(manager, current).state));
    return true;
}

} // namespace gfx

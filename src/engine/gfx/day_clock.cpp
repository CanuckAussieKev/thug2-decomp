// NOT ORIGINAL - the time of day's clock in the game (day_clock.h).
#include "engine/gfx/day_clock.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/format.h"
#include "base/maths/vec4.h"
#include "engine/config/settings_state.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/game_hooks.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/lens_flare.h"
#include "engine/gfx/relit_data.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_light_census.h"
#include "engine/gfx/rt_light_field.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/snow.h"
#include "engine/gfx/tod_manager.h"
#include "engine/net/session_query.h"
#include "engine/sys/stopwatch.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"

namespace render::day_clock {

namespace {

constexpr double kRadians = std::numbers::pi / 180.0;
constexpr std::array<uint32_t, rt::kTodStates> kStateNames{gfx::kMorning, gfx::kAfternoon, gfx::kEvening, gfx::kNight};
constexpr int kSystems = gfx::CtodManager::kSystems;
// The clock's light steps (hours): 1/120 of an hour - 30 seconds of the day; a 24-minute cycle steps twice a second.
constexpr double kStepsPerHour = 120.0;
constexpr double kSecondsPerMinute = 60.0;
// The eye's adaptation runs by the simulation's steps too (one is 1/60 s - the loop's rate), slow motion or not.
constexpr double kStepSeconds = 1.0 / 60.0;
// The level's colours sent again once a channel has moved this much (the manager's 0 - 255).
constexpr int kColourStep = 2;
constexpr uint8_t kHalfAlpha = 0x80;
constexpr uint32_t kByteMask = 0xFFU;
constexpr uint32_t kGreenShift = 8U;
constexpr uint32_t kBlueShift = 16U;
constexpr int kMostByte = 255;

uint32_t rgba(int red, int green, int blue)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(red)) |
           static_cast<uint32_t>(static_cast<uint8_t>(green)) << kGreenShift |
           static_cast<uint32_t>(static_cast<uint8_t>(blue)) << kBlueShift | static_cast<uint32_t>(kHalfAlpha) << 24U;
}

bool near_colour(uint32_t a, uint32_t b)
{
    for (const uint32_t shift : {0U, kGreenShift, kBlueShift}) {
        const int one = static_cast<int>((a >> shift) & kByteMask);
        const int other = static_cast<int>((b >> shift) & kByteMask);
        if (std::abs(one - other) >= kColourStep) {
            return false;
        }
    }
    return true;
}

rt::TodState state_of(uint32_t name)
{
    for (std::size_t i = 0; i < kStateNames.size(); ++i) {
        if (core::at(kStateNames, i) == name) {
            return static_cast<rt::TodState>(i);
        }
    }
    return rt::TodState::kAfternoon;
}

uint32_t name_of(rt::TodState state) { return core::at(kStateNames, static_cast<std::size_t>(state)); }

// The level's main scene (the first that isn't a sky), its checksum; 0: none.
uint32_t level_scene()
{
    constexpr int kMostScenes = 256;
    const memory::ListNode* node = first_scene_node();
    for (int guard = 0; node != nullptr && !node->is_head() && guard < kMostScenes; ++guard) {
        const auto* scene = static_cast<const WorldScene*>(node->data());
        if (scene != nullptr && !scene->is_sky()) {
            return scene->checksum();
        }
        node = node->next();
    }
    return 0;
}

// A system's colouring: the colours wanted, those the sectors have, and a sweep through the groups' parts (one a
// frame) that brings them there.
struct Colouring {
    uint32_t level = 0;
    uint32_t sky = 0;
    uint32_t sent_level = 0;
    uint32_t sent_sky = 0;
    bool sent = false;
    bool sweeping = false;
    uint32_t sweep_level = 0;
    uint32_t sweep_sky = 0;
    int group = 0;
    int part = 0;
    // (the relit mode) the models' colour - the level's own, lit where each stands (colour_models_by_place)
    bool models_own = false;
    uint32_t models = 0;
};

// The level's relit data, as far as the clock needs it (its file's start - relit_data.h read_relit_head): read once
// when the level arrives.
struct LevelHead {
    bool read = false;
    bool found = false; // a file of this level's scene
    rt::RelitLevelHead head;
    bool north_set = false; // (the level's compass) its north given by its entry
    double north_heading = 0.0;
};

struct Clock {
    uint32_t level = 0;
    LevelHead data;
    bool driving = false;
    bool native_known = false;
    rt::NativeDay native;
    uint32_t own_state = 0;   // the manager's state when the clock took the level over
    sun::Placed own_sun{};    // the sun then
    bool started = false;
    rt::TodChoice choice;
    double hours = 0.0;
    std::array<std::array<gfx::TodParams, rt::kTodStates>, kSystems> params{};
    std::array<bool, kSystems> have_params{};
    bool params_read = false;
    gfx::TodParams sent_params{};
    bool params_sent = false;
    std::array<Colouring, kSystems> colouring{};
    uint64_t sets_seen = 0; // (tod_manager.h tod_state_sets) the manager's sets when the clock took over or last set
    bool scripts_took_over = false; // a script set the level's state since (the park editor's lighting, a network peer)
    uint64_t took_over_settings = 0;
    bool sun_placed = false;
    rt::DayVec placed_sun{};
    float exposure = 0.0F; // (the eye adapting) the exposure now; 0: none yet - the first is the target
    std::string status = "tod: level (the level as it is)";
};

// The exposure this step: toward the relit level's light now over its own (the light field's whole - everything the
// level shows, its lamps with it) where it is relit, else the clock's model's; adapted over the steps. (Presentation
// only: nothing the simulation reads.)
float exposure_now(Clock& own, const rt::DayLight& light, double seconds)
{
    float target = light.exposure;
    if (rt::light_field() != nullptr) {
        constexpr float kLumaRed = 0.299F;
        constexpr float kLumaGreen = 0.587F;
        constexpr float kLumaBlue = 0.114F;
        const rt::LightRatio level = rt::level_light_ratio();
        target = rt::adapted_exposure((kLumaRed * std::get<0>(level.whole)) + (kLumaGreen * std::get<1>(level.whole)) +
                                      (kLumaBlue * std::get<2>(level.whole)));
    }
    own.exposure = own.exposure > 0.0F ? rt::adapt_exposure(own.exposure, target, seconds) : target;
    return own.exposure;
}

Clock& clock()
{
    static Clock state;
    return state;
}

// The level's own sun as the game shows it (the sprite's theta and phi when the clock took over), if shown.
bool own_game_sun(const Clock& own, rt::DayVec* toward)
{
    if (!own.own_sun.shown) {
        return false;
    }
    const double theta = own.own_sun.theta;
    const double phi = own.own_sun.phi;
    *toward = {static_cast<float>(std::sin(theta) * std::cos(phi)), static_cast<float>(std::sin(phi)),
               static_cast<float>(std::cos(theta) * std::cos(phi))};
    return std::sin(phi) > 0.0;
}

// The level's relit data read from the start of its file (relit_data.h: its scene, verdict and baked sun), and what it
// says published to the settings model (whether the time of day has data to light the level by) - the simulation's
// facts about the level, known when it arrives: the renderer adds whether the data still matches the level's meshes
// on its first frame (render_pc/rt_relit_world.cpp), which only the relit mode reads. With it the level's north
// (rt_relit.h relit_level_entry: rt_relit_levels.json's "north", the level's compass) - level data, not the renderer's.
void read_level_data(Clock& own)
{
    own.data = LevelHead{.read = true};
    const std::string path = text::format("%s%s\\%s", build_info::executable_directory(), rt::kRelitDataFolder,
                                          rt::relit_data_name(own.level).c_str());
    std::array<char, rt::kRelitHeadBytes> bytes{};
    std::size_t got = 0;
    if (const fs::OwnedStream stream = fs::open_stream(path.c_str(), "rb")) {
        got = fs::disk().stream_read(stream.get(), bytes.data(), 1, bytes.size());
    }
    const bool read = got > 0 && rt::read_relit_head(std::string_view(bytes.data(), got), &own.data.head);
    const bool stale = read && own.data.head.stale; // (lighting round A: an older relit bake's file - out of date)
    own.data.found = read && !stale && own.data.head.scene == own.level;
    if (const rt::RelitLevel* entry = rt::relit_level_entry(own.level); entry != nullptr && entry->north_set) {
        own.data.north_set = true;
        own.data.north_heading = static_cast<double>(entry->north_heading);
    }
    config::LevelLight light = stale ? config::LevelLight::kStale : config::LevelLight::kMissing;
    if (own.data.found) {
        light = rt::relit_verdict_relights(own.data.head.verdict) ? config::LevelLight::kBuilt
                                                                  : config::LevelLight::kNoModel;
    }
    if (config::set_level_light(light)) {
        graphics_options::reload_live("rt_relit"); // (the time of day's settings with it: they read the level's facts)
    }
}

// The level's own day, from its data's baked sun - else the sun the game shows (when the clock took over), else none.
void know_native(Clock& own)
{
    if (own.native_known) {
        return;
    }
    rt::NativeInputs in{.state = state_of(own.own_state)};
    if (own.data.found && own.data.head.sun_found) {
        in.sun = own.data.head.toward;
        in.have_sun = true;
        in.from_bake = true;
    } else {
        in.have_sun = own_game_sun(own, &in.sun);
    }
    in.north_set = own.data.north_set;
    in.north_heading = own.data.north_heading;
    own.native = rt::fit_native_day(in);
    own.native_known = true;
    const rt::SkyPlace sun = rt::heading_of(own.native.toward);
    get_log("tod: level %08x's own time %s - its sun %s (heading %.0f, %.0f degrees up), latitude %.1f, north at "
            "heading %.1f%s",
            own.level, rt::tod_text({.preset = rt::TodPreset::kClock, .hours = static_cast<float>(own.native.hours)}).c_str(),
            in.from_bake ? "baked" : (in.have_sun ? "the game's" : "assumed"), sun.azimuth, sun.elevation,
            own.native.path.latitude, own.native.path.north_heading, in.north_set ? " (the level's data)" : "");
}

void read_params(Clock& own, gfx::CtodManager& manager)
{
    if (own.params_read) {
        return;
    }
    own.params_read = true;
    for (int system = 0; system < kSystems; ++system) {
        bool all = true;
        for (std::size_t state = 0; state < rt::kTodStates; ++state) {
            all = all && manager.state_params(system, core::at(kStateNames, state),
                                              &core::at(core::at(own.params, system), state));
        }
        core::at(own.have_params, system) = all;
    }
}

gfx::TodParams blended(const Clock& own, int system, const rt::TodBlend& blend)
{
    const auto& states = core::at(own.params, system);
    const gfx::TodParams& from = core::at(states, static_cast<std::size_t>(blend.from));
    if (blend.fraction <= 0.0F || blend.from == blend.to) {
        return from;
    }
    return gfx::CtodManager::get_lerp(from, core::at(states, static_cast<std::size_t>(blend.to)), blend.fraction);
}

// One part of a system's sweep a step; a new sweep when its colours moved.
void colour_step(Clock& own, gfx::CtodManager& manager, int system)
{
    Colouring& colours = core::at(own.colouring, system);
    if (!colours.sweeping) {
        if (colours.sent && near_colour(colours.level, colours.sent_level) && near_colour(colours.sky, colours.sent_sky)) {
            return;
        }
        colours.sweeping = true;
        colours.sweep_level = colours.level;
        colours.sweep_sky = colours.sky;
        colours.group = 0;
        colours.part = 0;
    }
    const int groups = manager.light_groups(system);
    // (a part a step while the clock runs - tod_cycle -, as the original's own transitions; a time set - a level
    // loaded, tod changed - all at once, so no frame shows the level's colour arriving group by group)
    const bool at_once = !rt::tod_settings().cycle;
    do {
        if (colours.group >= groups) {
            break;
        }
        manager.colour_group_part(system, colours.group, colours.part, colours.sweep_level, colours.sweep_sky);
        if (++colours.part >= manager.group_parts(system, colours.group)) {
            colours.part = 0;
            ++colours.group;
        }
    } while (at_once);
    if (colours.group >= groups) {
        colours.sweeping = false;
        colours.sent = true;
        colours.sent_level = colours.sweep_level;
        colours.sent_sky = colours.sweep_sky;
    }
}

// (the relit mode - the one lighting path) The models given the level's own colour times the light where each stands
// (CtodManager::colour_models_by_place) once a sweep is done, every step: they move, objects are made, the light
// changes.
void models_step(gfx::CtodManager& manager, const Colouring& colours)
{
    if (!colours.models_own || colours.sweeping) {
        return;
    }
    manager.colour_models_by_place(colours.models);
}

// (the relit mode - the one lighting path) The objects' and characters' lights: the level's own state's, as the bake
// was lit - the light now reaches them where they are (pc_lights.cpp: the key light times the lighting state's direct
// light, the fill and the ambient times the light field's indirect ratio there - engine/gfx/rt_light_field.h).
void own_lights(const gfx::TodParams& own, gfx::TodParams* params)
{
    params->ambient_red_ = own.ambient_red_;
    params->ambient_green_ = own.ambient_green_;
    params->ambient_blue_ = own.ambient_blue_;
    params->ambient_mod_lo_ = own.ambient_mod_lo_;
    params->ambient_mod_hi_ = own.ambient_mod_hi_;
    params->heading0 = own.heading0;
    params->pitch0 = own.pitch0;
    params->red0 = own.red0;
    params->green0 = own.green0;
    params->blue0 = own.blue0;
    params->mod_lo0_ = own.mod_lo0_;
    params->mod_hi0_ = own.mod_hi0_;
    params->heading_a_ = own.heading_a_;
    params->pitch1 = own.pitch1;
    params->red1 = own.red1;
    params->green1 = own.green1;
    params->blue1 = own.blue1;
    params->mod_lo1_ = own.mod_lo1_;
    params->mod_hi1_ = own.mod_hi1_;
}

// The original's side at the clock's time: its state (not in a network session - lighting_only), its parameters, its
// colours.
void apply_original(Clock& own, gfx::CtodManager& manager, const rt::DayLight& light, bool lighting_only)
{
    read_params(own, manager);
    const rt::TodBlend& blend = light.blend;
    const uint32_t nearest = name_of(blend.fraction < 0.5F ? blend.from : blend.to);
    if (!lighting_only && nearest != manager.current_state() && !gfx::tod_lights_locked() &&
        manager.can_set_tod_instant(nearest)) {
        constexpr std::array<const char*, rt::kTodStates> kNames{"morning", "afternoon", "evening", "night"};
        get_log("tod: the level's %s state set (its scripts for it run)",
                core::at(kNames, static_cast<std::size_t>(state_of(nearest))));
        manager.set_tod_instant(nearest);
        own.sets_seen = gfx::tod_state_sets(); // (its own set)
        own.params_sent = false;
        for (Colouring& colours : own.colouring) {
            colours.sent = false; // (set_tod_instant coloured every sector with its state's colours)
            colours.sweeping = false;
        }
    }
    const bool relit = rt::sun_relight().relights;
    const bool sky_drawn = rt::sky_replacement().on;
    const rt::LightRatio level_light = rt::level_light_ratio(); // (the one lighting path: the level's whole)
    rt::note_light_path(rt::LightPath::kParticlesWhole); // (the light energy audit's census: one value for every particle)
    const auto native = static_cast<std::size_t>(own.native.state);
    for (int system = 0; system < kSystems; ++system) {
        if (!core::at(own.have_params, system)) {
            continue;
        }
        const bool sky_system = manager.systems().at(static_cast<std::size_t>(system)).sky;
        gfx::TodParams params = blended(own, system, blend);
        const gfx::TodParams& own_params = core::at(core::at(own.params, system), native);
        Colouring& colours = core::at(own.colouring, system);
        colours.level = rgba(params.lev_red_, params.lev_green_, params.lev_blue_);
        // (the relit mode lights the bake itself: the scenes' colour stays the level's; what it doesn't relight vertex
        // by vertex is lit by the same light where it is - the one lighting path, engine/gfx/rt_light_field.h: the
        // objects' models their own colour times the light where each stands, the objects and characters lit by the
        // game's lights the level's own state's lights (pc_lights.cpp scales them where they are), the particles -
        // params' lev, theirs alone - the level's own colour times the level's light as a whole)
        colours.models_own = relit && !sky_system;
        if (relit) {
            colours.level = rgba(own_params.lev_red_, own_params.lev_green_, own_params.lev_blue_);
            colours.models = colours.level;
            own_lights(own_params, &params);
            const auto lit = [](int value, float ratio) {
                return std::clamp(static_cast<int>(std::lround(static_cast<float>(value) * ratio)), 0, kMostByte);
            };
            params.lev_red_ = lit(own_params.lev_red_, std::get<0>(level_light.whole));
            params.lev_green_ = lit(own_params.lev_green_, std::get<1>(level_light.whole));
            params.lev_blue_ = lit(own_params.lev_blue_, std::get<2>(level_light.whole));
        }
        // (the backend's sky has its own night: the dome's colour stays the level's)
        if (sky_drawn) {
            params.sky_red_ = own_params.sky_red_;
            params.sky_green_ = own_params.sky_green_;
            params.sky_blue_ = own_params.sky_blue_;
        }
        colours.sky = rgba(params.sky_red_, params.sky_green_, params.sky_blue_);
        if (!sky_system && !manager.level_lights_reset() &&
            (!own.params_sent || std::memcmp(&params, &own.sent_params, sizeof(params)) != 0)) {
            manager.apply_tod_params(params);
            own.sent_params = params;
            own.params_sent = true;
        }
        colour_step(own, manager, system);
        models_step(manager, colours);
    }
}

// The original's sun sprite and lens flare at the clock's sun (hidden while it is down).
void place_sun(Clock& own, const rt::DayLight& light)
{
    if (own.sun_placed && own.placed_sun == light.sun) {
        return;
    }
    own.sun_placed = true;
    own.placed_sun = light.sun;
    const rt::SkyPlace place = rt::heading_of(light.sun);
    sun::Settings settings{};
    settings.theta = static_cast<float>(place.azimuth * kRadians);
    settings.phi = static_cast<float>(place.elevation * kRadians);
    settings.shown = place.elevation > 0.0;
    sun::set_sun(&settings, sun::kTheta | sun::kPhi | sun::kShown);
}

// The level's own state and sun back (the clock turned off in it) - its state only where the clock moved it.
void give_back(Clock& own, gfx::CtodManager* manager)
{
    if (!own.driving) {
        return;
    }
    if (manager != nullptr && manager->params_set() && own.own_state != 0 &&
        manager->current_state() != own.own_state && !rt::tod_settings().lighting_only && !gfx::tod_lights_locked() &&
        manager->can_set_tod_instant(own.own_state)) {
        manager->set_tod_instant(own.own_state);
    }
    sun::Settings settings{};
    settings.theta = own.own_sun.theta;
    settings.phi = own.own_sun.phi;
    settings.color = own.own_sun.color;
    settings.shown = own.own_sun.shown;
    sun::set_sun(&settings, sun::kTheta | sun::kPhi | sun::kColor | sun::kShown);
    get_log("tod: off - the level's own time of day back");
    const uint32_t level = own.level;
    const LevelHead data = own.data;
    own = Clock{.level = level, .data = data};
}

} // namespace

void step()
{
    Clock& own = clock();
    // (a network session: the settings model limits the time of day to its lighting - settings_model.h limits)
    if (config::set_network_game(net::in_net_game())) {
        graphics_options::reload_live("tod");
    }
    gfx::CtodManager* manager = gfx::tod_manager();
    const uint32_t level = level_scene();
    if (level != own.level) {
        // (a new level sets its own time of day as it loads: nothing to give back)
        own = Clock{.level = level};
    }
    const GameHooks& hooks = game_hooks();
    // (the front end: the skate module's level is load_mainmenu - or there is none)
    constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu
    const uint32_t script = hooks.level_script == nullptr ? 0U : hooks.level_script();
    const bool front_end = script == 0U || script == kLoadMainMenu;
    if (!own.data.read && level != 0 && !front_end) {
        read_level_data(own); // (the level's facts first: the settings read next may move with them)
    }
    const rt::TodSettings settings = rt::tod_settings();
    const rt::TodChoice choice = rt::tod_choice_for(settings, level, &checksums::hash_from_text);
    const bool wanted = level != 0 && !front_end && manager != nullptr && manager->params_set() &&
                        (choice.preset != rt::TodPreset::kLevel || settings.cycle);
    if (!wanted) {
        give_back(own, manager);
        own.status = "tod: level (the level as it is)";
        rt::set_day_light({});
        return;
    }
    // (the original runs its own day - a career level's cycle, a cutscene's temporary state: it wins, the clock waits)
    if (manager->runs_its_day() || manager->temporary_state()) {
        own.status = "tod: waiting - the level runs its own time of day";
        rt::set_day_light({});
        return;
    }
    // (a script set the level's state since the clock took it over or last set it - the park editor's lighting,
    // THUGPRO's time of day from a peer, a goal: tod_state_sets counted it; the original wins until tod is changed again)
    if (own.driving && gfx::tod_state_sets() != own.sets_seen) {
        own.scripts_took_over = true;
        own.took_over_settings = rt::tod_settings_serial();
        own.driving = false;
        get_log("tod: the level's scripts set its time of day - the clock leaves it to them until tod changes");
    }
    if (own.scripts_took_over) {
        if (rt::tod_settings_serial() == own.took_over_settings) {
            own.status = "tod: paused - the level's scripts set its time of day";
            rt::set_day_light({});
            return;
        }
        // (tod changed: the clock takes the level over again, from its state now)
        own = Clock{.level = level, .data = own.data};
    }
    if (!own.driving) {
        own.own_state = manager->current_state();
        own.own_sun = sun::placed();
        own.sets_seen = gfx::tod_state_sets();
        own.driving = true;
        get_log("tod: %s%s in level %08x (its own state %08x)%s", rt::tod_text(choice).c_str(),
                settings.cycle ? " - the clock running" : "", level, own.own_state,
                settings.lighting_only ? " - a network session: the lighting only" : "");
    }
    know_native(own);
    // the time: from the choice when it changes, then running with the cycle by the simulation's steps - game time, slow
    // motion with it, stopped while the game is paused (as the original's own day: CtodManager::tick_frame)
    if (!own.started || choice != own.choice) {
        own.hours = rt::preset_hours(choice, own.native);
        own.choice = choice;
        own.started = true;
    } else if (settings.cycle && (hooks.game_halted == nullptr || !hooks.game_halted())) {
        own.hours += frame_clock::get_slomo_frame_seconds() * rt::kHoursPerDay /
                     (static_cast<double>(settings.cycle_minutes) * kSecondsPerMinute);
        own.hours = std::fmod(own.hours, rt::kHoursPerDay);
    }
    const double stepped = std::round(own.hours * kStepsPerHour) / kStepsPerHour;
    rt::DayLight light = rt::day_light_at(own.native, stepped);
    rt::apply_overcast(&light, snow::overcast_share()); // (NOT ORIGINAL - the snow's winter sky: rt_daylight.h)
    light.exposure = exposure_now(own, light, kStepSeconds);
    rt::set_day_light(light);
    apply_original(own, *manager, light, settings.lighting_only);
    place_sun(own, light);
    const rt::SkyPlace sun = rt::heading_of(light.sun);
    own.status = text::format("tod: %s%s - %s, the sun %.1f degrees up%s; light %.2f (%s), sky %.2f, lamps %.2f, "
                              "exposure %.2f%s",
                              rt::tod_text(choice).c_str(), settings.cycle ? " (running)" : "",
                              rt::tod_text({.preset = rt::TodPreset::kClock, .hours = static_cast<float>(light.hours)}).c_str(),
                              sun.elevation, light.moonlit ? ", moonlit" : "", static_cast<double>(light.direct),
                              light.moonlit ? "the moon" : "the sun", static_cast<double>(std::get<1>(light.sky)),
                              static_cast<double>(light.lamps), static_cast<double>(light.exposure),
                              settings.lighting_only ? " (online: lighting only)" : "");
}

const char* status() { return clock().status.c_str(); }

bool light_ready() { return true; }

std::string state_line()
{
    const Clock& own = clock();
    if (!own.driving || !own.started || !own.sun_placed) {
        return {};
    }
    const gfx::CtodManager* manager = gfx::tod_manager();
    return text::format("%.17g %08x %.9g %.9g %.9g", own.hours, manager != nullptr ? manager->current_state() : 0U,
                        static_cast<double>(std::get<0>(own.placed_sun)), static_cast<double>(std::get<1>(own.placed_sun)),
                        static_cast<double>(std::get<2>(own.placed_sun)));
}

} // namespace render::day_clock

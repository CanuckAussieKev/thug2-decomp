// NOT ORIGINAL - the time of day's clock in the game (day_clock.h).
#include "engine/gfx/day_clock.h"

#include <algorithm>
#include <array>
#include <chrono>
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
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/game_hooks.h"
#include "engine/gfx/lens_flare.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/tod_manager.h"
#include "platform/run_log.h"

namespace render::day_clock {

namespace {

constexpr double kRadians = std::numbers::pi / 180.0;
constexpr std::array<uint32_t, rt::kTodStates> kStateNames{gfx::kMorning, gfx::kAfternoon, gfx::kEvening, gfx::kNight};
constexpr int kSystems = gfx::CtodManager::kSystems;
// Frames the clock waits for the level's baked sun (rt_sun.h's fit, with ray tracing) before it takes the game's.
constexpr int kBakeWaitFrames = 600;
// The clock's light steps (hours): 1/120 of an hour - 30 seconds of the day; a 24-minute cycle steps twice a second.
constexpr double kStepsPerHour = 120.0;
constexpr double kLongestFrame = 0.25; // seconds: a longer gap (a load, a stall) counts as this
constexpr double kSecondsPerMinute = 60.0;
// The level's colours sent again once a channel has moved this much (the manager's 0 - 255).
constexpr int kColourStep = 2;
constexpr uint8_t kHalfAlpha = 0x80;
constexpr uint32_t kByteMask = 0xFFU;
constexpr uint32_t kGreenShift = 8U;
constexpr uint32_t kBlueShift = 16U;

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
};

struct Clock {
    uint32_t level = 0;
    bool driving = false;
    bool native_known = false;
    int waited = 0;
    uint64_t stale_fit = 0; // the baked sun's fit when the level changed (the level before's: not this one's)
    rt::NativeDay native;
    uint32_t own_state = 0;   // the manager's state when the clock took the level over
    sun::Placed own_sun{};    // the sun then
    bool started = false;
    rt::TodChoice choice;
    double hours = 0.0;
    std::chrono::steady_clock::time_point last{};
    bool have_last = false;
    std::array<std::array<gfx::TodParams, rt::kTodStates>, kSystems> params{};
    std::array<bool, kSystems> have_params{};
    bool params_read = false;
    gfx::TodParams sent_params{};
    bool params_sent = false;
    std::array<Colouring, kSystems> colouring{};
    uint32_t set_state = 0; // the state the clock last set (set_tod_instant)
    bool scripts_took_over = false; // a script set the level's state since (the park editor's lighting, a network peer)
    uint64_t took_over_settings = 0;
    bool sun_placed = false;
    rt::DayVec placed_sun{};
    std::string status = "tod: level (the level as it is)";
};

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

// The level's own day (once its baked sun is known, or it has none): false while it waits for the fit.
bool know_native(Clock& own)
{
    const rt::BakedSun& baked = rt::baked_sun();
    const bool tracing = rt::ray_tracing() != nullptr;
    if (tracing) {
        rt::set_baked_sun_wanted(true);
    }
    const bool fitted = baked.scene != 0 && baked.scene != own.stale_fit; // (this level's fit)
    const bool baked_now = fitted && baked.found;
    if (own.native_known && (own.native.from_bake || !baked_now)) {
        return true;
    }
    if (!own.native_known && tracing && !fitted && own.waited < kBakeWaitFrames) {
        ++own.waited;
        return false;
    }
    rt::NativeInputs in{.state = state_of(own.own_state)};
    if (baked_now) {
        in.sun = baked.toward;
        in.have_sun = true;
        in.from_bake = true;
    } else {
        in.have_sun = own_game_sun(own, &in.sun);
    }
    const bool refit = own.native_known;
    own.native = rt::fit_native_day(in);
    own.native_known = true;
    const rt::SkyPlace sun = rt::heading_of(own.native.toward);
    get_log("tod: level %08x's own time %s - its sun %s (heading %.0f, %.0f degrees up), latitude %.1f, north at "
            "heading %.0f%s",
            own.level, rt::tod_text({.preset = rt::TodPreset::kClock, .hours = static_cast<float>(own.native.hours)}).c_str(),
            in.from_bake ? "baked" : (in.have_sun ? "the game's" : "assumed"), sun.azimuth, sun.elevation,
            own.native.path.latitude, own.native.path.north_heading, refit ? " (the baked sun found: fitted again)" : "");
    if (refit) {
        own.started = false; // (the time again from the choice, on the new day)
    }
    return true;
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

// One part of a system's sweep a frame; a new sweep when its colours moved.
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
    if (colours.group < groups) {
        manager.colour_group_part(system, colours.group, colours.part, colours.sweep_level, colours.sweep_sky);
        if (++colours.part >= manager.group_parts(system, colours.group)) {
            colours.part = 0;
            ++colours.group;
        }
    }
    if (colours.group >= groups) {
        colours.sweeping = false;
        colours.sent = true;
        colours.sent_level = colours.sweep_level;
        colours.sent_sky = colours.sweep_sky;
    }
}

// The original's side at the clock's time: its state, its parameters, its colours.
void apply_original(Clock& own, gfx::CtodManager& manager, const rt::DayLight& light)
{
    read_params(own, manager);
    const rt::TodBlend& blend = light.blend;
    const uint32_t nearest = name_of(blend.fraction < 0.5F ? blend.from : blend.to);
    if (nearest != own.set_state && nearest != manager.current_state() && !gfx::tod_lights_locked() &&
        manager.can_set_tod_instant(nearest)) {
        constexpr std::array<const char*, rt::kTodStates> kNames{"morning", "afternoon", "evening", "night"};
        get_log("tod: the level's %s state set (its scripts for it run)",
                core::at(kNames, static_cast<std::size_t>(state_of(nearest))));
        manager.set_tod_instant(nearest);
        own.params_sent = false;
        for (Colouring& colours : own.colouring) {
            colours.sent = false; // (set_tod_instant coloured every sector with its state's colours)
            colours.sweeping = false;
        }
    }
    own.set_state = manager.current_state();
    const bool relit = rt::sun_relight().relights;
    const bool sky_drawn = rt::sky_replacement().on;
    const auto native = static_cast<std::size_t>(own.native.state);
    for (int system = 0; system < kSystems; ++system) {
        if (!core::at(own.have_params, system)) {
            continue;
        }
        const bool sky_system = manager.systems().at(static_cast<std::size_t>(system)).sky;
        gfx::TodParams params = blended(own, system, blend);
        const gfx::TodParams& own_params = core::at(core::at(own.params, system), native);
        // (the relit mode dims the bake itself; the backend's sky has its own night: their colours stay the level's)
        if (relit) {
            params.lev_red_ = own_params.lev_red_;
            params.lev_green_ = own_params.lev_green_;
            params.lev_blue_ = own_params.lev_blue_;
        }
        if (sky_drawn) {
            params.sky_red_ = own_params.sky_red_;
            params.sky_green_ = own_params.sky_green_;
            params.sky_blue_ = own_params.sky_blue_;
        }
        Colouring& colours = core::at(own.colouring, system);
        colours.level = rgba(params.lev_red_, params.lev_green_, params.lev_blue_);
        colours.sky = rgba(params.sky_red_, params.sky_green_, params.sky_blue_);
        if (!sky_system && !manager.level_lights_reset() &&
            (!own.params_sent || std::memcmp(&params, &own.sent_params, sizeof(params)) != 0)) {
            manager.apply_tod_params(params);
            own.sent_params = params;
            own.params_sent = true;
        }
        colour_step(own, manager, system);
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

// The level's own state and sun back (the clock turned off in it).
void give_back(Clock& own, gfx::CtodManager* manager)
{
    if (!own.driving) {
        return;
    }
    if (manager != nullptr && manager->params_set() && own.own_state != 0 && !gfx::tod_lights_locked() &&
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
    own = Clock{};
    own.level = level;
}

} // namespace

void frame()
{
    Clock& own = clock();
    gfx::CtodManager* manager = gfx::tod_manager();
    const uint32_t level = level_scene();
    if (level != own.level) {
        // (a new level sets its own time of day as it loads: nothing to give back)
        own = Clock{};
        own.level = level;
        own.stale_fit = rt::baked_sun().scene;
    }
    const rt::TodSettings settings = rt::tod_settings();
    const rt::TodChoice choice = rt::tod_choice_for(settings, level, &checksums::hash_from_text);
    const GameHooks& hooks = game_hooks();
    // (the front end: the skate module's level is load_mainmenu - or there is none)
    constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu
    const uint32_t script = hooks.level_script == nullptr ? 0U : hooks.level_script();
    const bool front_end = script == 0U || script == kLoadMainMenu;
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
    // (a script set the level's state over the clock's - the park editor's lighting, THUGPRO's time of day from a peer,
    // a goal: the original wins until tod is changed again)
    if (own.driving && own.set_state != 0 && manager->current_state() != own.set_state) {
        own.scripts_took_over = true;
        own.took_over_settings = rt::tod_settings_serial();
        own.driving = false;
        own.set_state = 0;
        get_log("tod: the level's scripts set its time of day - the clock leaves it to them until tod changes");
    }
    if (own.scripts_took_over) {
        if (rt::tod_settings_serial() == own.took_over_settings) {
            own.status = "tod: paused - the level's scripts set its time of day";
            rt::set_day_light({});
            return;
        }
        own = Clock{.level = level}; // (tod changed: the clock takes the level over again, from its state now)
    }
    if (!own.driving) {
        own.own_state = manager->current_state();
        own.own_sun = sun::placed();
        own.driving = true;
        get_log("tod: %s%s in level %08x (its own state %08x)", rt::tod_text(choice).c_str(),
                settings.cycle ? " - the clock running" : "", level, own.own_state);
    }
    if (!know_native(own)) {
        own.status = "tod: waiting for the level's baked sun";
        rt::set_day_light({});
        return;
    }
    // the time: from the choice when it changes, then running with the cycle (stopped while the game is paused)
    const auto now = std::chrono::steady_clock::now();
    double seconds = own.have_last ? std::chrono::duration<double>(now - own.last).count() : 0.0;
    seconds = std::clamp(seconds, 0.0, kLongestFrame);
    own.last = now;
    own.have_last = true;
    if (!own.started || choice != own.choice) {
        own.hours = rt::preset_hours(choice, own.native);
        own.choice = choice;
        own.started = true;
    } else if (settings.cycle && (hooks.game_halted == nullptr || !hooks.game_halted())) {
        own.hours += seconds * rt::kHoursPerDay / (static_cast<double>(settings.cycle_minutes) * kSecondsPerMinute);
        own.hours = std::fmod(own.hours, rt::kHoursPerDay);
    }
    const double stepped = std::round(own.hours * kStepsPerHour) / kStepsPerHour;
    const rt::DayLight light = rt::day_light_at(own.native, stepped);
    rt::set_day_light(light);
    apply_original(own, *manager, light);
    place_sun(own, light);
    const rt::SkyPlace sun = rt::heading_of(light.sun);
    own.status = text::format("tod: %s%s - %s, the sun %.1f degrees up%s; light %.2f (%s), sky %.2f, lamps %.2f",
                              rt::tod_text(choice).c_str(), settings.cycle ? " (running)" : "",
                              rt::tod_text({.preset = rt::TodPreset::kClock, .hours = static_cast<float>(light.hours)}).c_str(),
                              sun.elevation, light.moonlit ? ", moonlit" : "", static_cast<double>(light.direct),
                              light.moonlit ? "the moon" : "the sun", static_cast<double>(std::get<1>(light.sky)),
                              static_cast<double>(light.lamps));
}

const char* status() { return clock().status.c_str(); }

} // namespace render::day_clock

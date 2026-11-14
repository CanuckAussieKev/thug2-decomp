// NOT ORIGINAL - the time of day's clock, sun path and light (rt_daylight.h).
#include "engine/gfx/rt_daylight.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_sky.h"

namespace render::rt {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kRadians = kPi / 180.0;
constexpr double kDegrees = 180.0 / kPi;
constexpr double kDisplay = 1.0 / kDisplayGamma;
constexpr std::array<double, 3> kLuma{0.2126, 0.7152, 0.0722};
constexpr double kMinutesPerHour = 60.0;
constexpr double kHalfDay = 12.0;
constexpr double kRightAngle = 90.0;
constexpr double kFullTurn = 360.0;

double smoothstep(double low, double high, double x)
{
    const double t = std::clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - (2.0 * t));
}

double wrap_hours(double hours)
{
    const double wrapped = std::fmod(hours, kHoursPerDay);
    return wrapped < 0.0 ? wrapped + kHoursPerDay : wrapped;
}

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

// A whole number of digits (at most `most` of them); false: none, or anything else.
bool digits_of(std::string_view text, std::size_t most, int* out)
{
    if (text.empty() || text.size() > most) {
        return false;
    }
    int value = 0;
    constexpr int kTen = 10;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = (value * kTen) + (ch - '0');
    }
    *out = value;
    return true;
}

// A place's direction with the path's north at its heading.
DayVec direction_of(double heading, double elevation)
{
    const double h = heading * kRadians;
    const double e = elevation * kRadians;
    return {static_cast<float>(std::sin(h) * std::cos(e)), static_cast<float>(std::sin(e)),
            static_cast<float>(std::cos(h) * std::cos(e))};
}

// The place on the path at hour angle `hour_angle` (degrees) for `declination`.
SkyPlace place_at(double latitude, double declination, double hour_angle)
{
    const double phi = latitude * kRadians;
    const double delta = declination * kRadians;
    const double h = hour_angle * kRadians;
    const double sine = (std::sin(phi) * std::sin(delta)) + (std::cos(phi) * std::cos(delta) * std::cos(h));
    const double elevation = std::asin(std::clamp(sine, -1.0, 1.0));
    // (from north through east: the sun east of the meridian - h < 0 - in the morning)
    const double azimuth = std::atan2(-std::sin(h) * std::cos(delta),
                                      (std::cos(phi) * std::sin(delta)) - (std::sin(phi) * std::cos(delta) * std::cos(h)));
    double degrees = azimuth * kDegrees;
    degrees = degrees < 0.0 ? degrees + kFullTurn : degrees;
    return {.elevation = elevation * kDegrees, .azimuth = degrees};
}

// The light's direction: `toward` held at least kLowestLight up (its heading kept).
DayVec held_up(const DayVec& toward)
{
    const SkyPlace place = heading_of(toward);
    return direction_of(place.azimuth, std::max(place.elevation, kLowestLight));
}

float luma_of(const DayRgb& colour)
{
    return static_cast<float>((kLuma[0] * std::get<0>(colour)) + (kLuma[1] * std::get<1>(colour)) +
                              (kLuma[2] * std::get<2>(colour)));
}

// --- the sky's light, made once -----------------------------------------------------------------------------------------
constexpr double kTableLowest = -20.0; // degrees: the table's first entry (the sky's light gone below it)
constexpr std::size_t kTableEntries = 111; // one a degree, -20 to 90
constexpr std::size_t kRings = 6;          // the hemisphere's rings of elevation ...
constexpr std::size_t kAzimuths = 8;       // ... and directions round each (one side: the sky is symmetric about the sun)
constexpr SkySteps kIrradianceSteps{.view = 16, .light = 8};
constexpr uint32_t kTransmittanceSteps = 64;

std::vector<double> make_sky_table()
{
    std::vector<double> table(kTableEntries, 0.0);
    const double ring = (kPi / 2.0) / static_cast<double>(kRings);
    const double across = kPi / static_cast<double>(kAzimuths);
    for (std::size_t entry = 0; entry < kTableEntries; ++entry) {
        const Dir sun = sun_direction_at((kTableLowest + static_cast<double>(entry)) * kRadians);
        double sum = 0.0;
        for (std::size_t r = 0; r < kRings; ++r) {
            const double e = (static_cast<double>(r) + 0.5) * ring;
            for (std::size_t a = 0; a < kAzimuths; ++a) {
                const double azimuth = (static_cast<double>(a) + 0.5) * across;
                const Dir view{std::cos(e) * std::cos(azimuth), std::sin(e), std::cos(e) * std::sin(azimuth)};
                const Rgb radiance = sky_radiance(view, sun, kIrradianceSteps);
                double luminance = 0.0;
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    luminance += core::at(kLuma, ch) * core::at(radiance, ch);
                }
                // (cosine to the zenith x the solid angle; both sides of the sun)
                sum += 2.0 * luminance * std::sin(e) * std::cos(e) * ring * across;
            }
        }
        core::at(table, entry) = sum;
    }
    const double overhead = table.back();
    for (double& value : table) {
        value = overhead > 0.0 ? value / overhead : 0.0;
    }
    return table;
}

const std::vector<double>& sky_table()
{
    static const std::vector<double> table = make_sky_table();
    return table;
}

struct Published {
    std::mutex mutex;
    TodSettings settings;
    uint64_t settings_serial = 0;
    DayLight light;
};
Published& published()
{
    static Published slot;
    return slot;
}

bool same_light(const DayLight& a, const DayLight& b)
{
    return a.on == b.on && a.hours == b.hours && a.sun == b.sun && a.moon == b.moon && a.light == b.light &&
           a.native == b.native && a.moonlit == b.moonlit && a.direct == b.direct && a.direct_tint == b.direct_tint &&
           a.sky == b.sky && a.shadow == b.shadow && a.lamps == b.lamps && a.stars == b.stars &&
           a.moon_disc == b.moon_disc && a.exposure == b.exposure && a.night_sky == b.night_sky &&
           a.stars_pole == b.stars_pole && a.blend == b.blend;
}

} // namespace

// --- the settings ----------------------------------------------------------------------------------------------------

bool parse_tod(std::string_view text, TodChoice* out)
{
    const std::string word = lower(trimmed(text));
    struct Named {
        std::string_view name;
        TodPreset preset;
    };
    constexpr std::array kNamed{Named{"level", TodPreset::kLevel},   Named{"dawn", TodPreset::kDawn},
                                Named{"morning", TodPreset::kMorning}, Named{"noon", TodPreset::kNoon},
                                Named{"afternoon", TodPreset::kAfternoon}, Named{"dusk", TodPreset::kDusk},
                                Named{"night", TodPreset::kNight}};
    for (const Named& named : kNamed) {
        if (word == named.name) {
            *out = {.preset = named.preset};
            return true;
        }
    }
    const std::size_t colon = word.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    int hours = 0;
    int minutes = 0;
    constexpr int kLastHour = 23;
    constexpr int kLastMinute = 59;
    if (!digits_of(std::string_view(word).substr(0, colon), 2, &hours) ||
        !digits_of(std::string_view(word).substr(colon + 1), 2, &minutes) || colon + 3 != word.size() ||
        hours > kLastHour || minutes > kLastMinute) {
        return false;
    }
    *out = {.preset = TodPreset::kClock,
            .hours = static_cast<float>(hours + (static_cast<double>(minutes) / kMinutesPerHour))};
    return true;
}

std::string tod_text(const TodChoice& choice)
{
    switch (choice.preset) {
    case TodPreset::kLevel:
        return "level";
    case TodPreset::kDawn:
        return "dawn";
    case TodPreset::kMorning:
        return "morning";
    case TodPreset::kNoon:
        return "noon";
    case TodPreset::kAfternoon:
        return "afternoon";
    case TodPreset::kDusk:
        return "dusk";
    case TodPreset::kNight:
        return "night";
    case TodPreset::kClock:
        break;
    }
    const double hours = wrap_hours(static_cast<double>(choice.hours));
    auto minutes = static_cast<int>(std::lround(hours * kMinutesPerHour));
    constexpr int kMinutesPerDay = 24 * 60;
    minutes %= kMinutesPerDay;
    std::array<char, 8> text{};
    std::snprintf(text.data(), text.size(), "%02d:%02d", minutes / 60, minutes % 60);
    return text.data();
}

bool parse_tod_levels(std::string_view text, std::vector<TodLevel>* out)
{
    text = trimmed(text);
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view entry = trimmed(text.substr(0, comma));
        text = comma == std::string_view::npos ? std::string_view{} : trimmed(text.substr(comma + 1));
        if (entry.empty()) {
            continue;
        }
        const std::size_t colon = entry.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            return false;
        }
        TodLevel level{.level = std::string(trimmed(entry.substr(0, colon)))};
        if (level.level.empty() || !parse_tod(entry.substr(colon + 1), &level.choice)) {
            return false;
        }
        out->push_back(std::move(level));
    }
    return true;
}

void set_tod_settings(const TodSettings& settings)
{
    Published& slot = published();
    const std::scoped_lock lock(slot.mutex);
    slot.settings = settings;
    slot.settings.cycle_minutes = std::isfinite(settings.cycle_minutes)
                                      ? std::clamp(settings.cycle_minutes, kLeastCycleMinutes, kMostCycleMinutes)
                                      : kDefaultCycleMinutes;
    ++slot.settings_serial;
}

TodSettings tod_settings()
{
    Published& slot = published();
    const std::scoped_lock lock(slot.mutex);
    return slot.settings;
}

uint64_t tod_settings_serial()
{
    Published& slot = published();
    const std::scoped_lock lock(slot.mutex);
    return slot.settings_serial;
}

bool tod_may_run(const TodSettings& settings)
{
    if (settings.choice.preset != TodPreset::kLevel || settings.cycle) {
        return true;
    }
    return std::ranges::any_of(settings.levels,
                               [](const TodLevel& level) { return level.choice.preset != TodPreset::kLevel; });
}

TodChoice tod_choice_for(const TodSettings& settings, uint32_t level, TodNameHash hash)
{
    if (hash != nullptr && level != 0) {
        for (const TodLevel& entry : settings.levels) {
            if (hash(entry.level.c_str()) == level) {
                return entry.choice;
            }
        }
    }
    return settings.choice;
}

// --- the path ----------------------------------------------------------------------------------------------------------

SkyPlace sun_place(const SunPath& path, double hours)
{
    return place_at(path.latitude, path.declination, (hours - kNoonHours) * kDegreesPerHour);
}

SkyPlace moon_place(const SunPath& path, double hours)
{
    return place_at(path.latitude, -path.declination, (hours - kNoonHours + kHalfDay) * kDegreesPerHour);
}

DayVec toward_place(const SunPath& path, const SkyPlace& place)
{
    return direction_of(path.north_heading + place.azimuth, place.elevation);
}

double hours_at_elevation(const SunPath& path, double elevation, bool afternoon)
{
    const double phi = path.latitude * kRadians;
    const double delta = path.declination * kRadians;
    const double below = std::cos(phi) * std::cos(delta);
    if (!(std::abs(below) > 1e-9)) {
        return -1.0;
    }
    const double cosine = (std::sin(elevation * kRadians) - (std::sin(phi) * std::sin(delta))) / below;
    if (cosine > 1.0 || cosine < -1.0) {
        return -1.0;
    }
    const double hour_angle = std::acos(cosine) * kDegrees / kDegreesPerHour;
    return afternoon ? kNoonHours + hour_angle : kNoonHours - hour_angle;
}

SkyPlace heading_of(const DayVec& toward)
{
    const double x = std::get<0>(toward);
    const double y = std::get<1>(toward);
    const double z = std::get<2>(toward);
    const double across = std::hypot(x, z);
    double heading = across > 0.0 ? std::atan2(x, z) * kDegrees : 0.0;
    heading = heading < 0.0 ? heading + kFullTurn : heading;
    return {.elevation = std::atan2(y, across) * kDegrees, .azimuth = heading};
}

NativeDay fit_native_day(const NativeInputs& in)
{
    NativeDay day;
    day.state = in.state;
    day.from_bake = in.have_sun && in.from_bake;
    SkyPlace sun = in.have_sun ? heading_of(in.sun) : SkyPlace{.elevation = kDefaultSunElevation, .azimuth = 0.0};
    if (!in.have_sun || !(sun.elevation > 0.0)) {
        sun = {.elevation = kDefaultSunElevation, .azimuth = sun.azimuth};
    }
    SunPath& path = day.path;
    // (a sun higher than the path's noon at the latitude: the latitude lowered until the noon is just above it)
    const double highest = kRightAngle - path.latitude + path.declination;
    if (sun.elevation > highest - kHighestMargin) {
        path.latitude = std::max(path.declination, kRightAngle + path.declination - sun.elevation - kHighestMargin);
    }
    switch (in.state) {
    case TodState::kMorning:
        day.hours = hours_at_elevation(path, sun.elevation, false);
        break;
    case TodState::kEvening:
        day.hours = hours_at_elevation(path, kDuskElevation, true);
        break;
    case TodState::kNight:
        day.hours = kNightHours;
        break;
    case TodState::kAfternoon:
        day.hours = hours_at_elevation(path, sun.elevation, true);
        break;
    }
    if (day.hours < 0.0) {
        day.hours = kAfternoonHours;
    }
    // north turned so that the path's sun at the level's time is the level's sun (its heading)
    const SkyPlace at = sun_place(path, day.hours);
    if (in.state == TodState::kAfternoon || in.state == TodState::kMorning) {
        path.north_heading = std::fmod(sun.azimuth - at.azimuth + kFullTurn, kFullTurn);
    } else {
        path.north_heading = 0.0;
    }
    day.toward = toward_place(path, sun_place(path, day.hours));
    return day;
}

double preset_hours(const TodChoice& choice, const NativeDay& native)
{
    double hours = native.hours;
    switch (choice.preset) {
    case TodPreset::kLevel:
        break;
    case TodPreset::kDawn:
        hours = hours_at_elevation(native.path, kDuskElevation, false);
        break;
    case TodPreset::kDusk:
        hours = hours_at_elevation(native.path, kDuskElevation, true);
        break;
    case TodPreset::kMorning:
        hours = kMorningHours;
        break;
    case TodPreset::kNoon:
        hours = kNoonHours;
        break;
    case TodPreset::kAfternoon:
        hours = kAfternoonHours;
        break;
    case TodPreset::kNight:
        hours = kNightHours;
        break;
    case TodPreset::kClock:
        hours = static_cast<double>(choice.hours);
        break;
    }
    return hours < 0.0 ? native.hours : wrap_hours(hours);
}

// --- the light -----------------------------------------------------------------------------------------------------------

double sky_light_at(double elevation)
{
    const std::vector<double>& table = sky_table();
    const double at = std::clamp(elevation - kTableLowest, 0.0, static_cast<double>(kTableEntries - 1));
    const auto below = static_cast<std::size_t>(std::floor(at));
    const std::size_t above = std::min(below + 1, kTableEntries - 1);
    const double fraction = at - static_cast<double>(below);
    return (core::at(table, below) * (1.0 - fraction)) + (core::at(table, above) * fraction);
}

double sun_light_at(double elevation)
{
    const Rgb through = sun_transmittance(elevation * kRadians, kTransmittanceSteps);
    double luminance = 0.0;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        luminance += core::at(kLuma, ch) * core::at(through, ch);
    }
    return luminance;
}

TodBlend tod_blend(double sun_elevation, bool afternoon)
{
    const auto smooth = [](double t) { return static_cast<float>(smoothstep(0.0, 1.0, t)); };
    if (sun_elevation >= kStateDay) {
        return {};
    }
    if (sun_elevation >= kStateLow) {
        const float t = smooth((kStateDay - sun_elevation) / (kStateDay - kStateLow));
        return afternoon ? TodBlend{.from = TodState::kAfternoon, .to = TodState::kEvening, .fraction = t}
                         : TodBlend{.from = TodState::kAfternoon, .to = TodState::kMorning, .fraction = t};
    }
    if (sun_elevation >= kStateNight) {
        const float t = smooth((kStateLow - sun_elevation) / (kStateLow - kStateNight));
        return afternoon ? TodBlend{.from = TodState::kEvening, .to = TodState::kNight, .fraction = t}
                         : TodBlend{.from = TodState::kMorning, .to = TodState::kNight, .fraction = t};
    }
    return {.from = TodState::kNight, .to = TodState::kNight, .fraction = 0.0F};
}

DayLight day_light_at(const NativeDay& native, double hours)
{
    DayLight light;
    light.on = true;
    light.hours = wrap_hours(hours);
    const SkyPlace sun = sun_place(native.path, light.hours);
    const SkyPlace moon = moon_place(native.path, light.hours);
    light.sun = toward_place(native.path, sun);
    light.moon = toward_place(native.path, moon);
    light.native = native.toward;
    const SkyPlace own = heading_of(native.toward);

    // the sun's direct light, over the level's own (display-referred), its colour the model's over the level's sun's
    const double own_sun = sun_light_at(own.elevation);
    const double sun_now = sun_light_at(sun.elevation) * smoothstep(kSunGoneLow, kSunGoneHigh, sun.elevation);
    const double sun_ratio = own_sun > 0.0 ? std::pow(std::max(sun_now / own_sun, 0.0), kDisplay) : 0.0;
    const std::array<float, 3> colour_now = sun_colour_at(std::max(sun.elevation, 0.0) * kRadians);
    const std::array<float, 3> colour_own = sun_colour_at(std::max(own.elevation, 0.0) * kRadians);
    constexpr float kMostTint = 4.0F;
    DayRgb sun_tint{};
    for (std::size_t ch = 0; ch < sun_tint.size(); ++ch) {
        core::at(sun_tint, ch) = core::at(colour_own, ch) > 0.0F
                                     ? std::clamp(core::at(colour_now, ch) / core::at(colour_own, ch), 0.0F, kMostTint)
                                     : 1.0F;
    }
    // the moon's, once the sun is down
    const double night = 1.0 - smoothstep(kMoonFull, kMoonFrom, sun.elevation);
    const double moon_up = smoothstep(0.0, kStateDay, moon.elevation);
    if (sun.elevation >= kMoonFrom) {
        light.light = held_up(light.sun);
        light.direct = static_cast<float>(sun_ratio);
        light.direct_tint = sun_tint;
    } else {
        light.moonlit = true;
        light.light = held_up(light.moon);
        light.direct = static_cast<float>(static_cast<double>(kMoonLight) * night * moon_up);
        light.direct_tint = kMoonTint;
    }
    // the sky's light, over the level's own, and the night's floor under it
    const double own_sky = sky_light_at(own.elevation);
    const double sky_ratio = own_sky > 0.0 ? std::pow(std::max(sky_light_at(sun.elevation) / own_sky, 0.0), kDisplay) : 1.0;
    const double floor = 1.0 - smoothstep(kNightFull, kNightFrom, sun.elevation);
    for (std::size_t ch = 0; ch < light.sky.size(); ++ch) {
        core::at(light.sky, ch) =
            static_cast<float>(sky_ratio + (static_cast<double>(core::at(kNightSkyLight, ch)) * floor));
        core::at(light.night_sky, ch) = static_cast<float>(static_cast<double>(core::at(kNightSky, ch)) * floor);
    }
    // the shadows: the sun's share of the light now over its share in the level's own light
    const float direct = light.direct * luma_of(light.direct_tint);
    const float sky = luma_of(light.sky);
    const float share = (kDirectShare * direct) / std::max((kDirectShare * direct) + ((1.0F - kDirectShare) * sky), 1e-6F);
    light.shadow = std::clamp(share / kDirectShare, 0.0F, 1.0F);
    light.lamps = static_cast<float>(1.0 - smoothstep(kLampsFull, kLampsOn, sun.elevation));
    light.stars = static_cast<float>(1.0 - smoothstep(kStarsFull, kStarsFrom, sun.elevation));
    light.moon_disc = static_cast<float>(night * smoothstep(-1.0, 3.0, moon.elevation));
    // the exposure: night brought partly up (the level's own light 1)
    const float total = (kDirectShare * direct) + ((1.0F - kDirectShare) * sky);
    light.exposure = std::clamp(static_cast<float>(std::pow(1.0 / std::max(static_cast<double>(total), 1e-3), kExposurePower)),
                                1.0F, kMostExposure);
    // the stars turn about the celestial pole (north, as high as the latitude) with the hours
    const DayVec pole = direction_of(native.path.north_heading, native.path.latitude);
    light.stars_pole = {std::get<0>(pole), std::get<1>(pole), std::get<2>(pole),
                        static_cast<float>(light.hours / kHoursPerDay * 2.0 * kPi)};
    light.blend = tod_blend(sun.elevation, light.hours >= kNoonHours);
    return light;
}

void set_day_light(const DayLight& light)
{
    Published& slot = published();
    const std::scoped_lock lock(slot.mutex);
    if (same_light(light, slot.light)) {
        return;
    }
    const uint64_t serial = slot.light.serial + 1;
    slot.light = light;
    slot.light.serial = serial;
}

DayLight day_light()
{
    Published& slot = published();
    const std::scoped_lock lock(slot.mutex);
    return slot.light;
}

} // namespace render::rt

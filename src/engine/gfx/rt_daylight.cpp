// NOT ORIGINAL - the time of day's clock, sun path and light (rt_daylight.h).
#include "engine/gfx/rt_daylight.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_clouds.h"
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


float luma_of(const DayRgb& colour)
{
    return static_cast<float>((kLuma[0] * std::get<0>(colour)) + (kLuma[1] * std::get<1>(colour)) +
                              (kLuma[2] * std::get<2>(colour)));
}

// What follows from the direct light and the sky's (day_light_at's, and again after apply_overcast changed them): the
// bounce and the shadows' strength.
void finish_light(DayLight* light)
{
    // the bounce: what the surroundings send back is the direct light's and the sky's, in the level's own shares
    const float own = std::clamp(light->direct_share, 1e-3F, 1.0F);
    for (std::size_t ch = 0; ch < light->bounce.size(); ++ch) {
        core::at(light->bounce, ch) =
            (own * core::at(light->light_colour, ch)) + ((1.0F - own) * core::at(light->sky, ch));
    }
    // the shadows: the sun's share of the light now over its share in the level's own light
    const float direct = light->direct * luma_of(light->direct_tint);
    const float sky = luma_of(light->sky);
    const float share = (own * direct) / std::max((own * direct) + ((1.0F - own) * sky), 1e-6F);
    light->shadow = std::clamp(share / own, 0.0F, 1.0F);
}

// (round S) The display-referred ratios over the level's own light (`own`: the clear light at its own sun) from the light
// now in the global unit: the direct light's strength and colour, the sky's per channel; then the bounce and the shadows.
void set_ratios(DayLight* light, const AbsoluteLight& own)
{
    constexpr float kMostTint = 4.0F;
    const double own_sun = luma_of(own.sun);
    const double ratio = own_sun > 0.0 ? std::max(static_cast<double>(luma_of(light->sun_light)) / own_sun, 0.0) : 0.0;
    light->direct = static_cast<float>(std::pow(ratio, kDisplay));
    for (std::size_t ch = 0; ch < light->direct_tint.size(); ++ch) {
        const double own_ch = core::at(own.sun, ch);
        core::at(light->direct_tint, ch) =
            own_ch > 0.0 && ratio > 0.0
                ? std::clamp(static_cast<float>((static_cast<double>(core::at(light->sun_light, ch)) / own_ch) / ratio),
                             0.0F, kMostTint)
                : 1.0F;
        core::at(light->light_colour, ch) = light->direct * core::at(light->direct_tint, ch);
        const double own_sky = core::at(own.sky, ch);
        core::at(light->sky, ch) =
            own_sky > 0.0
                ? static_cast<float>(std::pow(std::max(static_cast<double>(core::at(light->sky_light, ch)) / own_sky, 0.0),
                                              kDisplay))
                : 0.0F;
    }
    finish_light(light);
}

// --- the sky's light, made once -----------------------------------------------------------------------------------------
constexpr double kTableLowest = -20.0; // degrees: the table's first entry (the sky's light gone below it)
constexpr std::size_t kTableEntries = 111; // one a degree, -20 to 90
constexpr std::size_t kRings = 18;         // the hemisphere's rings of elevation ... (changed by the physical air: the
                                           // aerosol's forward lobe sampled finely enough that the light rises smoothly)
constexpr std::size_t kAzimuths = 24;      // ... and directions round each (one side: the sky is symmetric about the sun)
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
    return table;
}

// (the table unnormalised - the sky's irradiance per unit of the sun's - and normalised to the sun overhead's)
const std::vector<double>& sky_irradiance_table()
{
    static const std::vector<double> table = make_sky_table();
    return table;
}

const std::vector<double>& sky_table()
{
    static const std::vector<double> table = [] {
        std::vector<double> out = sky_irradiance_table();
        const double overhead = out.back();
        for (double& value : out) {
            value = overhead > 0.0 ? value / overhead : 0.0;
        }
        return out;
    }();
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
           a.light_colour == b.light_colour && a.light_radius == b.light_radius && a.bounce == b.bounce &&
           a.sky == b.sky && a.shadow == b.shadow && a.lamps == b.lamps && a.stars == b.stars &&
           a.moon_disc == b.moon_disc && a.night_sky == b.night_sky && a.direct_share == b.direct_share &&
           a.stars_pole == b.stars_pole && a.blend == b.blend && a.sun_light == b.sun_light &&
           a.sky_light == b.sky_light && a.moon_age == b.moon_age && a.moon_radiance == b.moon_radiance &&
           a.cloud_cover == b.cloud_cover && a.cloud_beam == b.cloud_beam && a.cloud_sky == b.cloud_sky &&
           a.illuminance_lux == b.illuminance_lux && a.pre_exposure == b.pre_exposure;
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

bool parse_moon_age(std::string_view text, double* out)
{
    const std::string word = lower(trimmed(text));
    struct Named {
        std::string_view name;
        double age;
    };
    // (the four phases: new, the first quarter a quarter of the month on, full half way, the last quarter)
    constexpr double kQuarter = 0.25 * kSynodicMonth;
    constexpr std::array kNamed{Named{"new", 0.0}, Named{"first", kQuarter}, Named{"full", kMoonFullAge},
                                Named{"last", 3.0 * kQuarter}};
    for (const Named& named : kNamed) {
        if (word == named.name) {
            *out = named.age;
            return true;
        }
    }
    const std::string number(word);
    char* end = nullptr;
    const double value = std::strtod(number.c_str(), &end);
    if (number.empty() || end != number.c_str() + number.size() || !std::isfinite(value) || value < 0.0 ||
        value > kSynodicMonth) {
        return false;
    }
    *out = value;
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

SkyPlace moon_place(const SunPath& path, double hours, double age)
{
    // (round S) east of the sun by its elongation: its hour angle the sun's less that, its declination the sun's x the
    // elongation's cosine (full: opposite the sun, its declination negated)
    const double elongation = moon_elongation(age);
    return place_at(path.latitude, path.declination * std::cos(elongation * kRadians),
                    ((hours - kNoonHours) * kDegreesPerHour) - elongation);
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
    if (in.north_set) {
        // (added by the level's compass) the level's data says where north is
        path.north_heading = std::fmod(std::fmod(in.north_heading, kFullTurn) + kFullTurn, kFullTurn);
    } else if (in.state == TodState::kAfternoon || in.state == TodState::kMorning) {
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

float direct_share_at(double elevation)
{
    // (lighting round A2: from the light in the global unit - every order of the sky's scattering, the frame's air - so
    // the shares and the light the relit world is lit by are one model)
    const AbsoluteLight at = absolute_light_at(elevation);
    const double direct =
        elevation > 0.0 ? static_cast<double>(luma_of(at.sun)) * std::sin(elevation * kRadians) : 0.0;
    const double whole = direct + static_cast<double>(luma_of(at.sky));
    return whole > 0.0 ? static_cast<float>(direct / whole) : 0.0F;
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

double disc_above(double elevation, double radius)
{
    if (!(radius > 0.0)) {
        return elevation > 0.0 ? 1.0 : 0.0;
    }
    // (the area of a disc of radius 1 above a chord at height h = elevation / radius from its centre, over pi)
    const double h = std::clamp(elevation / radius, -1.0, 1.0);
    return 0.5 + ((std::asin(h) + (h * std::sqrt(1.0 - (h * h)))) / kPi);
}

double moon_elongation(double age_days)
{
    const double cycles = age_days / kSynodicMonth;
    return (cycles - std::floor(cycles)) * kFullTurn;
}

double moon_phase_law(double phase_angle_degrees)
{
    constexpr double kLinear = 0.026;
    constexpr double kQuartic = 4e-9;
    constexpr double kMagnitude = -0.4;
    const double a = std::abs(phase_angle_degrees);
    return std::pow(10.0, kMagnitude * ((kLinear * a) + (kQuartic * a * a * a * a)));
}

DayRgb moon_irradiance(double age_days)
{
    // (the phase angle: 180 degrees less the elongation's distance from the sun either way)
    const double elongation = moon_elongation(age_days);
    const double apart = std::min(elongation, kFullTurn - elongation);
    const double law = moon_phase_law(2.0 * kRightAngle - apart);
    const double solid = kMoonAngularRadius * kMoonAngularRadius; // (its radius over its distance, squared)
    DayRgb out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) = static_cast<float>(core::at(kMoonAlbedo, ch) * solid * law);
    }
    return out;
}

DayLight day_light_at(const NativeDay& native, double hours) { return day_light_at(native, hours, DayWeather{}); }

DayLight day_light_at(const NativeDay& native, double hours, const DayWeather& weather)
{
    DayLight light;
    light.on = true;
    light.hours = wrap_hours(hours);
    light.moon_age = weather.moon_age;
    const SkyPlace sun = sun_place(native.path, light.hours);
    const SkyPlace moon = moon_place(native.path, light.hours, weather.moon_age);
    light.sun = toward_place(native.path, sun);
    light.moon = toward_place(native.path, moon);
    light.native = native.toward;
    // (lighting round A) the ratios are over the level's own sun's light - or, where the level's own time has none (a
    // night level: its sun under the horizon), over a reference day's, a sun kDefaultSunElevation up
    SkyPlace own = heading_of(native.toward);
    if (!(own.elevation > 0.0)) {
        own.elevation = kDefaultSunElevation;
    }
    light.direct_share = direct_share_at(own.elevation); // (added by the physical air)
    const AbsoluteLight own_light = absolute_light_at(own.elevation);

    // the sources, clear: the sun's light and its sky; the moon's (the sun's reflected at its phase) and its sky; the
    // night sky's own
    const AbsoluteLight by_sun = absolute_light_at(sun.elevation);
    const AbsoluteLight moon_unit = absolute_light_at(moon.elevation);
    const DayRgb moon_share = moon_irradiance(weather.moon_age);
    const double night_floor = kNightSkyIlluminance / unit_illuminance();
    // the clouds over each source (rt_clouds.h: the beam they keep, the deck's diffuse light)
    const double cover = std::clamp(static_cast<double>(weather.cloud_cover), 0.0, 1.0);
    const double sun_sine = std::sin(sun.elevation * kRadians);
    const double moon_sine = std::sin(moon.elevation * kRadians);
    const CloudLayerLight sun_clouds = cloud_layer_light(cover, sun_sine);
    const CloudLayerLight moon_clouds = cloud_layer_light(cover, moon_sine);
    DayRgb sun_direct{};
    DayRgb moon_direct{};
    DayRgb sky{};
    DayRgb clear_sky{};
    for (std::size_t ch = 0; ch < sky.size(); ++ch) {
        const double sun_beam = core::at(by_sun.sun, ch);
        const double moon_beam = static_cast<double>(core::at(moon_unit.sun, ch)) * core::at(moon_share, ch);
        const double moon_sky = static_cast<double>(core::at(moon_unit.sky, ch)) * core::at(moon_share, ch);
        const double floor = night_floor * core::at(kNightSkyColour, ch);
        core::at(sun_direct, ch) = static_cast<float>(sun_beam * sun_clouds.beam);
        core::at(moon_direct, ch) = static_cast<float>(moon_beam * moon_clouds.beam);
        core::at(sky, ch) = static_cast<float>((sun_beam * sun_clouds.sun_diffuse) +
                                               (core::at(by_sun.sky, ch) * sun_clouds.sky_kept) +
                                               (moon_beam * moon_clouds.sun_diffuse) + (moon_sky * moon_clouds.sky_kept) +
                                               (floor * sun_clouds.sky_kept));
        core::at(clear_sky, ch) = static_cast<float>(core::at(by_sun.sky, ch) + moon_sky + floor);
        // the night sky drawn: the moonlit sky's and the night sky's own light as a radiance overhead (a uniform sky of
        // radiance L gives L on a level surface in the unit)
        core::at(light.night_sky, ch) = static_cast<float>(moon_sky + floor);
    }
    // the scene's light: the brighter direct light on a level surface - the sun, or the moon once the sun is down
    const double sun_on_ground = luma_of(sun_direct) * std::max(sun_sine, 0.0);
    const double moon_on_ground = luma_of(moon_direct) * std::max(moon_sine, 0.0);
    light.moonlit = moon_on_ground > sun_on_ground;
    light.light = light.moonlit ? light.moon : light.sun;
    light.sun_light = light.moonlit ? moon_direct : sun_direct;
    light.sky_light = sky;
    light.light_radius = light.moonlit ? kMoonRadiusDegrees : kSunRadiusDegrees;
    light.cloud_cover = static_cast<float>(cover);
    light.cloud_beam = static_cast<float>(light.moonlit ? moon_clouds.beam : sun_clouds.beam);
    {
        const double clear = luma_of(clear_sky);
        light.cloud_sky = clear > 0.0 ? static_cast<float>(luma_of(sky) / clear) : 1.0F;
    }
    // the light on a level surface (lux): the lamps' photocells read it
    const double ground = std::max(sun_on_ground, moon_on_ground) + luma_of(sky);
    light.illuminance_lux = static_cast<float>(ground * unit_illuminance());
    light.lamps = static_cast<float>(1.0 - smoothstep(kLampsFullLux, kLampsOnLux, light.illuminance_lux));
    // the stars are always there (the sky's light hides them by day); the moon's disc: its light at the top of the
    // atmosphere through the air at its elevation, spread over its solid angle (pi r^2), in the sky's unit
    light.stars = 1.0F;
    light.moon_disc = 1.0F;
    {
        const Rgb through = sun_transmittance(moon.elevation * kRadians, kTransmittanceSteps);
        const double disc = sky_exposure() / (kPi * kMoonAngularRadius * kMoonAngularRadius);
        for (std::size_t ch = 0; ch < light.moon_radiance.size(); ++ch) {
            core::at(light.moon_radiance, ch) =
                static_cast<float>(core::at(through, ch) * static_cast<double>(core::at(moon_share, ch)) * disc);
        }
    }
    set_ratios(&light, own_light);
    // the stars turn about the celestial pole (north, as high as the latitude) with the hours
    const DayVec pole = direction_of(native.path.north_heading, native.path.latitude);
    light.stars_pole = {std::get<0>(pole), std::get<1>(pole), std::get<2>(pole),
                        static_cast<float>(light.hours / kHoursPerDay * 2.0 * kPi)};
    light.blend = tod_blend(sun.elevation, light.hours >= kNoonHours);
    return light;
}

AbsoluteLight absolute_light_at(double elevation)
{
    struct Cache {
        std::mutex mutex;
        bool made = false;
        Aerosol air;
        std::vector<GroundLightRgb> table;
    };
    static Cache cache;
    const Aerosol air = aerosol();
    const std::scoped_lock lock(cache.mutex);
    if (!cache.made || !(cache.air == air)) {
        cache.table = ground_light_table(kTableLowest, kTableEntries, air);
        cache.air = air;
        cache.made = true;
    }
    const double at = std::clamp(elevation - kTableLowest, 0.0, static_cast<double>(kTableEntries - 1));
    const auto below = static_cast<std::size_t>(std::floor(at));
    const std::size_t above = std::min(below + 1, kTableEntries - 1);
    const double fraction = at - static_cast<double>(below);
    AbsoluteLight out;
    // (round S: the disc's share above the horizon - the sun's own size -, not a ramp; the transmittance does the rest)
    const double up = disc_above(elevation, kSunRadiusDegrees);
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const GroundLightRgb& low = core::at(cache.table, below);
        const GroundLightRgb& high = core::at(cache.table, above);
        const double sun = (core::at(low.sun, ch) * (1.0 - fraction)) + (core::at(high.sun, ch) * fraction);
        core::at(out.sun, ch) = static_cast<float>(sun * up);
        core::at(out.sky, ch) =
            static_cast<float>((core::at(low.sky, ch) * (1.0 - fraction)) + (core::at(high.sky, ch) * fraction));
    }
    return out;
}

float direct_share_now(const DayLight& day, bool own_sun, double own_elevation)
{
    if (day.on) {
        // (the light in the global unit: the direct light on a level surface over its whole light)
        const float direct = luma_of(day.sun_light) * std::max(0.0F, core::at(day.light, 1));
        const float whole = direct + luma_of(day.sky_light);
        return whole > 0.0F ? std::clamp(direct / whole, 0.0F, 1.0F) : 0.0F;
    }
    return own_sun && own_elevation > 0.0 ? direct_share_at(own_elevation) : 0.0F;
}

void pre_expose(DayLight* light)
{
    if (!light->on) {
        light->pre_exposure = 1.0F;
        return;
    }
    const double ground = static_cast<double>(light->illuminance_lux) / unit_illuminance();
    const double scale = ground > 0.0 ? std::clamp(1.0 / ground, 1.0, kMostPreExposure) : kMostPreExposure;
    const auto by = static_cast<float>(scale);
    light->pre_exposure = by;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        core::at(light->sun_light, ch) *= by;
        core::at(light->sky_light, ch) *= by;
        core::at(light->night_sky, ch) *= by;
        core::at(light->moon_radiance, ch) *= by;
    }
    // (the display-referred ratios: r^(1 / 2.2), so x scale^(1 / 2.2))
    const auto display = static_cast<float>(std::pow(scale, kDisplay));
    light->direct *= display;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        core::at(light->light_colour, ch) *= display;
        core::at(light->sky, ch) *= display;
    }
    finish_light(light);
}

float frame_pre_exposure()
{
    const DayLight day = day_light();
    return day.on && day.pre_exposure > 0.0F ? day.pre_exposure : 1.0F;
}

CloudLight cloud_light(const DayLight& light)
{
    CloudLight out;
    if (!light.on) {
        return out;
    }
    out.clock = true;
    out.toward = light.light;
    const DayRgb moon = moon_irradiance(light.moon_age);
    for (std::size_t ch = 0; ch < out.light.size(); ++ch) {
        core::at(out.light, ch) = (light.moonlit ? core::at(moon, ch) : 1.0F) * light.pre_exposure;
    }
    return out;
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

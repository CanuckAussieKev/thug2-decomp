// NOT ORIGINAL - cel shading: see rt_cel.h.
#include "engine/gfx/rt_cel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <system_error>
#include <string>
#include <string_view>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_sun.h"

namespace render::rt {

namespace {

constexpr float kBytesPerLight = 128.0F;
constexpr float kMaxByte = 255.0F;
constexpr uint32_t kByteMask = 0xFF;
constexpr int kAlphaShift = 24;
constexpr int kRedShift = 16;
constexpr int kGreenShift = 8;
constexpr float kHalf = 0.5F;
constexpr float kHermiteThree = 3.0F;
constexpr float kHermiteTwo = 2.0F;
constexpr float kLeastLuma = 1.0e-3F;
constexpr float kLeastLog = 1.0e-4F;
constexpr std::array<float, 3> kLuma{0.2126F, 0.7152F, 0.0722F};
constexpr int kHexDigits = 6;
constexpr int kHexBase = 16;
constexpr float kBakeStopsSpan = 2.0F; // the bake's steps: this many stops over bands - 1

struct Slot {
    std::mutex mutex;
    CelSettings settings;
    CelWorld world;
};
Slot& slot()
{
    static Slot instance;
    return instance;
}
std::atomic<bool>& on_flag()
{
    static std::atomic<bool> value{false};
    return value;
}
std::atomic<uint64_t>& world_serial()
{
    static std::atomic<uint64_t> value{1};
    return value;
}
std::atomic<uint64_t>& serial()
{
    static std::atomic<uint64_t> value{1};
    return value;
}

float smoothstep(float low, float high, float x)
{
    if (!(high > low)) {
        return x < low ? 0.0F : 1.0F;
    }
    const float tt = std::clamp((x - low) / (high - low), 0.0F, 1.0F);
    return tt * tt * (kHermiteThree - (kHermiteTwo * tt));
}

float luma(const std::array<float, 3>& colour)
{
    return (core::at(kLuma, 0) * core::at(colour, 0)) + (core::at(kLuma, 1) * core::at(colour, 1)) +
           (core::at(kLuma, 2) * core::at(colour, 2));
}

uint32_t to_byte(float value, float scale)
{
    const float v = std::isfinite(value) ? std::clamp(value * scale, 0.0F, kMaxByte) : 0.0F;
    return static_cast<uint32_t>(std::lround(v));
}

uint32_t word(const std::array<float, 3>& rgb, float rgb_scale, float alpha)
{
    return (to_byte(alpha, kMaxByte) << kAlphaShift) | (to_byte(std::get<0>(rgb), rgb_scale) << kRedShift) |
           (to_byte(std::get<1>(rgb), rgb_scale) << kGreenShift) | to_byte(std::get<2>(rgb), rgb_scale);
}

uint32_t clamped_bands(uint32_t bands) { return std::clamp(bands, kCelLeastBands, kCelMostBands); }

bool parse_number(std::string_view text, float* out)
{
    const std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str() || !std::isfinite(value)) {
        return false;
    }
    *out = static_cast<float>(value);
    return true;
}

} // namespace

void set_cel_settings(const CelSettings& settings)
{
    {
        const std::scoped_lock lock(slot().mutex);
        CelSettings& kept = slot().settings;
        kept = settings;
        kept.bands = clamped_bands(settings.bands);
        for (float& channel : kept.shadow_colour) {
            channel = std::isfinite(channel) ? std::clamp(channel, 0.0F, 1.0F) : 1.0F;
        }
        kept.rim = std::isfinite(settings.rim) ? std::clamp(settings.rim, 0.0F, kCelMostRim) : 0.0F;
        kept.outline_width = std::isfinite(settings.outline_width) ? std::clamp(settings.outline_width, 0.0F, kCelMostOutlineWidth) : 0.0F;
        kept.flatten = std::isfinite(settings.flatten) ? std::clamp(settings.flatten, 0.0F, 1.0F) : 0.0F;
    }
    on_flag().store(settings.on, std::memory_order_relaxed);
    serial().fetch_add(1, std::memory_order_relaxed);
}
bool cel_shading_on() { return on_flag().load(std::memory_order_relaxed); }

CelSettings cel_settings()
{
    const std::scoped_lock lock(slot().mutex);
    return slot().settings;
}

uint64_t cel_settings_serial() { return serial().load(std::memory_order_relaxed); }

bool parse_cel_colour(std::string_view text, std::array<float, 3>* out)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
        if (text.size() != kHexDigits) {
            return false;
        }
        uint32_t value = 0;
        const std::from_chars_result read = std::from_chars(std::to_address(text.begin()), std::to_address(text.end()), value, kHexBase);
        if (read.ec != std::errc{} || read.ptr != std::to_address(text.end())) {
            return false;
        }
        *out = {static_cast<float>((value >> kRedShift) & kByteMask) / kMaxByte,
                static_cast<float>((value >> kGreenShift) & kByteMask) / kMaxByte,
                static_cast<float>(value & kByteMask) / kMaxByte};
        return true;
    }
    std::array<float, 3> parts{};
    std::size_t count = 0;
    while (count < parts.size()) {
        const std::size_t comma = text.find(',');
        if (!parse_number(text.substr(0, comma), &core::at(parts, count))) {
            return false;
        }
        ++count;
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    if (count != parts.size()) {
        return false;
    }
    const bool bytes = std::ranges::any_of(parts, [](float value) { return value > 1.0F; });
    for (float& part : parts) {
        part = std::clamp(bytes ? part / kMaxByte : part, 0.0F, 1.0F);
    }
    *out = parts;
    return true;
}

void set_cel_world(const CelWorld& world)
{
    {
        const std::scoped_lock lock(slot().mutex);
        slot().world = world;
    }
    world_serial().fetch_add(1, std::memory_order_relaxed);
}

uint64_t cel_world_serial() { return world_serial().load(std::memory_order_relaxed); }

CelWorld cel_world()
{
    const std::scoped_lock lock(slot().mutex);
    return slot().world;
}

CelWords cel_stream_words(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting)
{
    if (!model.valid || !vertex.used) {
        return kCelNoStream;
    }
    std::array<float, 3> ambient{};
    std::array<float, 3> tint{};
    for (std::size_t ch = 0; ch < ambient.size(); ++ch) {
        core::at(ambient, ch) = (core::at(model.sky, ch) * vertex.sky) + (core::at(model.bounce, ch) * core::at(vertex.bounce, ch));
        core::at(tint, ch) = model.scale * core::at(vertex.tint, ch);
    }
    const float length = std::sqrt(dot(lighting.toward, lighting.toward));
    const float n_dot_l = length > 0.0F ? std::max(0.0F, dot(vertex.normal, lighting.toward) / length) : 0.0F;
    const std::array<float, 3> residual =
        lighting.residual ? vertex.residual : std::array<float, 3>{0.0F, 0.0F, 0.0F};
    return {word(ambient, kBytesPerLight, n_dot_l), word(tint, kBytesPerLight, std::clamp(vertex.sun, 0.0F, 1.0F)),
            word(residual, kBytesPerLight, 1.0F)};
}

float cel_round(float steps, CelEdge edge)
{
    const float whole = std::floor(steps);
    const float part = steps - whole;
    if (!(edge.width > 0.0F)) {
        return whole + (part >= kHalf ? 1.0F : 0.0F);
    }
    return whole + smoothstep(kHalf - edge.width, kHalf + edge.width, part);
}

float cel_sun_band(float lit, CelEdge edge, uint32_t bands)
{
    const auto steps = static_cast<float>(clamped_bands(bands) - 1);
    const float x = std::clamp(std::isfinite(lit) ? lit : 0.0F, 0.0F, 1.0F);
    return std::clamp(cel_round((x * steps) + kCelBandBias, edge) / steps, 0.0F, 1.0F);
}

float cel_log_factor(float value, CelEdge edge, float stops)
{
    if (!(value > kLeastLuma) || !(stops > 0.0F)) {
        return 1.0F;
    }
    const float steps = std::log2(std::max(value, kLeastLog)) / stops;
    return std::exp2(cel_round(steps, edge) * stops) / value;
}

float cel_bake_stops(uint32_t bands) { return kBakeStopsSpan / static_cast<float>(clamped_bands(bands) - 1); }

std::array<float, 3> cel_world_light(const CelSettings& settings, const std::array<float, 3>& sun,
                                     const CelStreamPixel& pixel, float visible, CelEdge edge)
{
    const float lit = pixel.n_dot_l * visible;
    const float band = cel_sun_band(lit, edge, settings.bands);
    const float ambient_factor = cel_log_factor(luma(pixel.ambient), edge, kCelAmbientStops);
    const float residual_luma = luma(pixel.residual);
    const float residual_factor =
        residual_luma > kLeastLuma ? cel_round(residual_luma / kCelResidualStep, edge) * kCelResidualStep / residual_luma : 0.0F;
    std::array<float, 3> out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        const float shade = core::at(settings.shadow_colour, ch) + ((1.0F - core::at(settings.shadow_colour, ch)) * band);
        const float light = (core::at(pixel.ambient, ch) * ambient_factor * shade) + (core::at(sun, ch) * band);
        core::at(out, ch) = (core::at(pixel.tint, ch) * light) + (core::at(pixel.residual, ch) * residual_factor);
    }
    return out;
}

std::array<float, 3> cel_bake_light(const CelSettings& settings, const std::array<float, 3>& bake, CelEdge edge)
{
    const float y = luma(bake);
    if (!(y > kLeastLuma)) {
        return bake;
    }
    const float factor = cel_log_factor(y, edge, cel_bake_stops(settings.bands));
    const float shadow = 1.0F - smoothstep(kCelShadowLow, kCelShadowHigh, y * factor);
    std::array<float, 3> out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        const float tint = 1.0F + ((core::at(settings.shadow_colour, ch) - 1.0F) * shadow);
        core::at(out, ch) = core::at(bake, ch) * factor * tint;
    }
    return out;
}

std::array<float, 3> cel_ink(const std::array<float, 3>& colour)
{
    const float y = luma(colour);
    std::array<float, 3> out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        const float saturated = y + ((core::at(colour, ch) - y) * kCelInkChroma);
        core::at(out, ch) = std::clamp(saturated * kCelInkLightness, 0.0F, 1.0F);
    }
    return out;
}

} // namespace render::rt

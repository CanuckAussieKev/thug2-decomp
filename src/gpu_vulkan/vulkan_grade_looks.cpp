// NOT ORIGINAL - the colour grade's looks (vulkan_camera_fx.h step 4): their names, their formulas, the tables made from
// them, rt_grade_levels, and the grade pass's twin on the processor. Every look is our own formula - no table files.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_camera_fx.h"
#include "gpu_vulkan/vulkan_style_kit.h"

namespace render::vulkan {

namespace {

constexpr std::array<const char*, kGradeLookCount> kLookNames{"off",    "neutral", "warm",        "cool",
                                                              "filmic", "bleach",  "skate_video", "level"};
constexpr CameraRgb kLumaWeights{0.2126F, 0.7152F, 0.0722F}; // (Rec. 709)
constexpr float kGamma = 2.2F;

float luma(const CameraRgb& c)
{
    return (core::at(kLumaWeights, 0) * core::at(c, 0)) + (core::at(kLumaWeights, 1) * core::at(c, 1)) +
           (core::at(kLumaWeights, 2) * core::at(c, 2));
}

float unit(float value) { return std::clamp(value, 0.0F, 1.0F); }

CameraRgb unit3(const CameraRgb& c) { return {unit(core::at(c, 0)), unit(core::at(c, 1)), unit(core::at(c, 2))}; }

float smooth(float low, float high, float value) { return film_kit::smoothstep(low, high, value); }

// A gentle S about mid-grey: x mixed toward smoothstep(0, 1, x) by k (monotone for k in 0 - 1).
float s_curve(float x, float k)
{
    constexpr float kThree = 3.0F;
    constexpr float kTwo = 2.0F;
    const float s = x * x * (kThree - (kTwo * x));
    return x + ((s - x) * k);
}

CameraRgb s_curve3(const CameraRgb& c, float k)
{
    return {s_curve(core::at(c, 0), k), s_curve(core::at(c, 1), k), s_curve(core::at(c, 2), k)};
}

// The chroma times s about the colour's luma.
CameraRgb saturate(const CameraRgb& c, float s)
{
    const float l = luma(c);
    return {l + ((core::at(c, 0) - l) * s), l + ((core::at(c, 1) - l) * s), l + ((core::at(c, 2) - l) * s)};
}

// A white balance in linear light: each channel times its gain, the luma kept.
CameraRgb balance(const CameraRgb& c, const CameraRgb& gain)
{
    CameraRgb linear{};
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(linear, i) = std::pow(unit(core::at(c, i)), kGamma);
    }
    const float before = luma(linear);
    CameraRgb out{};
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(out, i) = core::at(linear, i) * core::at(gain, i);
    }
    const float after = luma(out);
    const float keep = after > 0.0F ? before / after : 1.0F;
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(out, i) = std::pow(unit(core::at(out, i) * keep), 1.0F / kGamma);
    }
    return out;
}

// A tint added: `shadows` weighted by (1 - l)^2, `highlights` by l^2 (split toning).
CameraRgb split_tone(const CameraRgb& c, const CameraRgb& shadows, const CameraRgb& highlights)
{
    const float l = unit(luma(c));
    const float dark = (1.0F - l) * (1.0F - l);
    const float bright = l * l;
    return {core::at(c, 0) + (core::at(shadows, 0) * dark) + (core::at(highlights, 0) * bright),
            core::at(c, 1) + (core::at(shadows, 1) * dark) + (core::at(highlights, 1) * bright),
            core::at(c, 2) + (core::at(shadows, 2) * dark) + (core::at(highlights, 2) * bright)};
}

CameraRgb warm(const CameraRgb& c)
{
    constexpr CameraRgb kGain{1.08F, 1.0F, 0.86F};
    constexpr CameraRgb kGold{1.0F, 0.9F, 0.7F};
    constexpr float kGoldShare = 0.1F;
    constexpr float kGoldFrom = 0.5F;
    CameraRgb out = balance(c, kGain);
    const float l = luma(out);
    const float gold = kGoldShare * smooth(kGoldFrom, 1.0F, l);
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(out, i) += ((core::at(kGold, i) * l) - core::at(out, i)) * gold;
    }
    return out;
}

CameraRgb cool(const CameraRgb& c)
{
    constexpr CameraRgb kGain{0.9F, 1.0F, 1.1F};
    constexpr CameraRgb kTeal{-0.012F, 0.006F, 0.02F};
    constexpr float kShadowTo = 0.4F;
    CameraRgb out = balance(c, kGain);
    const float dark = 1.0F - smooth(0.0F, kShadowTo, luma(out));
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(out, i) += core::at(kTeal, i) * dark;
    }
    return out;
}

CameraRgb filmic(const CameraRgb& c)
{
    constexpr float kContrast = 0.35F;
    constexpr float kFade = 0.015F;          // the print's blacks, lifted a little
    constexpr float kHighlightCalm = 0.25F;  // the highlights' saturation taken off
    constexpr float kCalmFrom = 0.7F;
    constexpr CameraRgb kShadows{-0.01F, 0.0F, 0.015F};
    constexpr CameraRgb kHighlights{0.015F, 0.005F, -0.012F};
    CameraRgb out = s_curve3(unit3(c), kContrast);
    out = saturate(out, 1.0F - (kHighlightCalm * smooth(kCalmFrom, 1.0F, luma(out))));
    out = split_tone(out, kShadows, kHighlights);
    for (float& channel : out) {
        channel = kFade + ((1.0F - kFade) * channel);
    }
    return out;
}

CameraRgb bleach(const CameraRgb& c)
{
    constexpr float kOverlayShare = 0.6F;
    constexpr float kSaturation = 0.55F;
    constexpr float kContrast = 0.25F;
    constexpr float kHalf = 0.5F;
    constexpr float kTwo = 2.0F;
    const CameraRgb in = unit3(c);
    const float l = luma(in);
    CameraRgb out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const float a = core::at(in, i);
        const float overlay = l < kHalf ? kTwo * l * a : 1.0F - (kTwo * (1.0F - l) * (1.0F - a));
        core::at(out, i) = a + ((overlay - a) * kOverlayShare);
    }
    return s_curve3(saturate(out, kSaturation), kContrast);
}

CameraRgb skate_video(const CameraRgb& c)
{
    constexpr float kSaturation = 1.3F;
    constexpr float kContrast = 0.45F;
    constexpr float kCrush = 0.03F; // the blacks crushed below this
    constexpr CameraRgb kShadows{-0.02F, 0.01F, 0.025F};
    constexpr CameraRgb kHighlights{0.025F, 0.008F, -0.02F};
    CameraRgb out = s_curve3(unit3(saturate(unit3(c), kSaturation)), kContrast);
    for (float& channel : out) {
        channel = std::max(0.0F, (channel - kCrush) / (1.0F - kCrush));
    }
    return split_tone(out, kShadows, kHighlights);
}

std::string lowered(std::string_view text)
{
    std::string out;
    for (const char ch : text) {
        if (ch != ' ' && ch != '\t' && ch != '-') {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return out;
}

} // namespace

const char* grade_look_name(GradeLook look)
{
    const auto index = static_cast<std::size_t>(look);
    return index < kLookNames.size() ? core::at(kLookNames, index) : "off";
}

bool parse_grade_look(const std::string& text, GradeLook* look)
{
    std::string name = lowered(text);
    if (name == "skatevideo" || name == "skate_video" || name == "video") {
        *look = GradeLook::kSkateVideo;
        return true;
    }
    if (name == "0" || name.empty() || name == "none") {
        *look = GradeLook::kOff;
        return true;
    }
    if (name == "bleachbypass" || name == "bleach_bypass") {
        name = "bleach";
    }
    for (std::size_t i = 0; i < kLookNames.size(); ++i) {
        if (name == core::at(kLookNames, i)) {
            *look = static_cast<GradeLook>(i);
            return true;
        }
    }
    return false;
}

CameraRgb grade_look_colour(GradeLook look, CameraRgb colour)
{
    switch (look) {
    case GradeLook::kWarm:
        return unit3(warm(colour));
    case GradeLook::kCool:
        return unit3(cool(colour));
    case GradeLook::kFilmic:
        return unit3(filmic(colour));
    case GradeLook::kBleach:
        return unit3(bleach(colour));
    case GradeLook::kSkateVideo:
        return unit3(skate_video(colour));
    case GradeLook::kOff:
    case GradeLook::kNeutral:
    case GradeLook::kLevel:
        break;
    }
    return unit3(colour);
}

std::vector<CameraRgb> grade_table(GradeLook look, uint32_t size)
{
    size = std::max(size, 2U);
    std::vector<CameraRgb> table(static_cast<std::size_t>(size) * size * size);
    const float step = 1.0F / static_cast<float>(size - 1);
    std::size_t at = 0;
    for (uint32_t blue = 0; blue < size; ++blue) {
        for (uint32_t green = 0; green < size; ++green) {
            for (uint32_t red = 0; red < size; ++red) {
                core::at(table, at++) = grade_look_colour(
                    look, {static_cast<float>(red) * step, static_cast<float>(green) * step, static_cast<float>(blue) * step});
            }
        }
    }
    return table;
}

bool parse_grade_levels(const std::string& text, std::vector<GradeLevel>* levels)
{
    levels->clear();
    bool all = true;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = std::min(text.find(',', start), text.size());
        const std::string pair = text.substr(start, comma - start);
        start = comma + 1;
        if (pair.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }
        const std::size_t colon = pair.find(':');
        GradeLook look = GradeLook::kOff;
        if (colon == std::string::npos || !parse_grade_look(pair.substr(colon + 1), &look) || look == GradeLook::kLevel) {
            all = false;
            continue;
        }
        std::string scene = pair.substr(0, colon);
        scene.erase(0, scene.find_first_not_of(" \t"));
        scene.erase(scene.find_last_not_of(" \t") + 1);
        if (scene.empty()) {
            all = false;
            continue;
        }
        levels->push_back({.scene = checksums::hash_from_text(scene.c_str()), .look = look});
    }
    return all;
}

CameraRgb grade_table_colour(const std::vector<CameraRgb>& table, uint32_t size, CameraRgb colour)
{
    const auto entry = [&](uint32_t r, uint32_t g, uint32_t b) -> const CameraRgb& {
        return core::at(table, r + (size * (g + (size * b))));
    };
    std::array<uint32_t, 3> low{};
    std::array<float, 3> part{};
    for (std::size_t i = 0; i < 3; ++i) {
        const float place = unit(core::at(colour, i)) * static_cast<float>(size - 1);
        core::at(low, i) = std::min(static_cast<uint32_t>(place), size - 2);
        core::at(part, i) = place - static_cast<float>(core::at(low, i));
    }
    const uint32_t r = core::at(low, 0);
    const uint32_t g = core::at(low, 1);
    const uint32_t b = core::at(low, 2);
    CameraRgb out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto lerp = [&](const CameraRgb& from, const CameraRgb& to, float t) {
            return core::at(from, i) + ((core::at(to, i) - core::at(from, i)) * t);
        };
        const auto lerp1 = [](float from, float to, float t) { return from + ((to - from) * t); };
        const float c00 = lerp(entry(r, g, b), entry(r + 1, g, b), core::at(part, 0));
        const float c10 = lerp(entry(r, g + 1, b), entry(r + 1, g + 1, b), core::at(part, 0));
        const float c01 = lerp(entry(r, g, b + 1), entry(r + 1, g, b + 1), core::at(part, 0));
        const float c11 = lerp(entry(r, g + 1, b + 1), entry(r + 1, g + 1, b + 1), core::at(part, 0));
        core::at(out, i) = lerp1(lerp1(c00, c10, core::at(part, 1)), lerp1(c01, c11, core::at(part, 1)), core::at(part, 2));
    }
    return out;
}

std::vector<CameraRgb> grade_reference(const GradeParameters& parameters, const std::vector<CameraRgb>& colour, Extent size)
{
    const std::vector<CameraRgb> table = grade_table(parameters.look);
    constexpr float kHalf = 0.5F;
    constexpr float kVignetteFrom = 0.3F;
    constexpr float kGrainFloor = 0.35F;
    constexpr float kGrainMid = 2.6F;
    const float width = static_cast<float>(std::max(size.width, 1U));
    const float height = static_cast<float>(std::max(size.height, 1U));
    const float aspect = width / height;
    const float corner = std::sqrt((aspect * aspect) + 1.0F) * kHalf;
    std::vector<CameraRgb> out(colour.size());
    for (uint32_t row = 0; row < size.height; ++row) {
        for (uint32_t column = 0; column < size.width; ++column) {
            const std::size_t at = (static_cast<std::size_t>(row) * size.width) + column;
            const CameraRgb c = film_kit::finite3(core::at(colour, at));
            const CameraRgb g = grade_table_colour(table, kGradeTableSize, c);
            CameraRgb o{};
            for (std::size_t i = 0; i < 3; ++i) {
                core::at(o, i) = core::at(c, i) + ((core::at(g, i) - core::at(c, i)) * parameters.strength);
            }
            const float px = static_cast<float>(column) + kHalf;
            const float py = static_cast<float>(row) + kHalf;
            if (parameters.vignette > 0.0F) {
                const float qx = ((px / width) - kHalf) * aspect;
                const float qy = (py / height) - kHalf;
                const float r = std::sqrt((qx * qx) + (qy * qy)) / corner;
                const float v = smooth(kVignetteFrom, 1.0F, r);
                const float keep = 1.0F - (parameters.vignette * kGradeVignetteDepth * v);
                for (float& channel : o) {
                    channel *= keep;
                }
            }
            if (parameters.grain > 0.0F) {
                const auto cx = static_cast<int32_t>(std::floor(px / parameters.cell));
                const auto cy = static_cast<int32_t>(std::floor(py / parameters.cell));
                const float n = film_kit::hash01(film_kit::hash2(cx, cy, parameters.seed)) - kHalf;
                const float l = unit(luma(o));
                const float amount = parameters.grain * kGradeGrainAmount * (kGrainFloor + (kGrainMid * l * (1.0F - l)));
                for (float& channel : o) {
                    channel += n * amount;
                }
            }
            core::at(out, at) = film_kit::finite3(o);
        }
    }
    return out;
}

} // namespace render::vulkan

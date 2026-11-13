// NOT ORIGINAL - the vhs art style: a 90s camcorder skate video (vulkan_style_film.h; docs/design/vulkan.md "Art
// style"). One full-screen pass; vhs_reference is its twin on the processor.
//
// FilmConstants:
//   info: x the frame (30 a second of game time), y the tape counter's digits (4 bits each: hours from bit 16, minutes'
//         tens, units, seconds' tens, units at bit 0), z flags (1 the overlay, 2 its REC shown - the blink, 4 a tracking
//         band now);
//   a: the fisheye's k, the vignette, s (a tape line, pixels: the viewport's height / 480), the aspect (width / height);
//   b: the wobble (pixels of one tape pixel x rt_style_vhs_wobble), the bend's phase (0 - 1), the band's centre (0 - 1
//      of the height), its strength (0 - 1);
//   c: the luma's tap spacing, the chroma's, the chroma's shift (pixels), the saturation;
//   d: the grain, the scanlines (faded by s), the strength, the overlay's font pixel (pixels);
//   e: the REC's top-left x, y, the bottom row's y, the counter's x (pixels from the viewport's corner);
//   f: x 1 / s (floor(y x f.x) is a pixel's tape line - a product, the same on the device and the processor).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_style_kit.h"
#include "gpu_vulkan/vulkan_style_film.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportPipeline = 0x53545911U;
constexpr uint64_t kReportFirstPass = 0x53545912U;

constexpr float kTapeLines = 480.0F;
constexpr float kFramesPerSecond = 30.0F;
constexpr uint32_t kFrameMask = 0xFFFFFFU;
constexpr float kBandSlot = 9.0F;     // seconds: one band at most in each
constexpr float kBandLength = 2.4F;   // seconds it takes to roll down the picture
constexpr float kBandChance = 0.9F;   // the share of the slots with one
constexpr float kBandStart = -0.1F;   // its centre's way down the picture (0 - 1 of the height)
constexpr float kBandTravel = 1.2F;
constexpr float kBendRate = 0.07F;    // the bend's turns a second
constexpr float kTau = 6.28318530718F;
constexpr float kPi = 3.14159265359F;
constexpr float kBandHalf = 0.06F;    // the band's half height (of the viewport's)
constexpr float kJitter = 0.7F;       // the line jitter's span (tape pixels)
constexpr float kBend = 0.6F;         // the bend's (tape pixels, either way)
constexpr float kBendWaves = 3.0F;    // the bend's waves down the picture
constexpr float kTearLeast = 4.0F;    // the band's tear (tape pixels)
constexpr float kTearSpan = 10.0F;
constexpr float kFisheye = 0.5F;      // the barrel's k at rt_style_vhs_fisheye 1
constexpr float kVignette = 1.2F;     // the vignette at rt_style_vhs_fisheye 1 (at most 1)
constexpr float kVignetteFrom = 0.45F; // (r^2)
constexpr float kFrameEdge = 1.5F;    // the frame's soft edge (tape pixels)
constexpr float kLumaSpacing = 0.6F;  // tape pixels
constexpr float kChromaSpacing = 1.5F; // tape pixels at rt_style_vhs_bleed 1
constexpr float kChromaShift = 2.0F;
constexpr float kGrain = 0.07F;
constexpr float kBandGrain = 0.5F;
constexpr float kBlackLift = 0.05F;   // the tape's black level
constexpr float kLumaRange = 0.9F;
constexpr float kScanDepth = 0.4F;
constexpr float kFontPixel = 2.0F;    // the overlay's font pixel (tape pixels)
constexpr float kMarginX = 0.07F;     // the overlay's margins (of the width, the height)
constexpr float kMarginY = 0.08F;
constexpr int kGlyphColumns = 6;      // a glyph's cell: 5 columns and a gap
constexpr int kGlyphRows = 7;
constexpr int kGlyphWidth = 5;
constexpr int kTopRows = 4;           // the rows in a glyph's first word
constexpr int kCounterGlyphs = 7;     // h:mm:ss
constexpr std::size_t kCounterDigits = 5;
constexpr int kRecGlyphs = 5;         // (dot) REC
constexpr int kSpGlyphs = 2;
constexpr float kShadow = 0.6F;
constexpr std::array<float, 3> kInk{0.94F, 0.94F, 0.9F};
constexpr std::array<float, 3> kRecRed{0.92F, 0.12F, 0.1F};
constexpr uint32_t kFlagOverlay = 1U;
constexpr uint32_t kFlagBlink = 2U;
constexpr uint32_t kFlagBand = 4U;
constexpr int kSecondsPerHour = 3600;
constexpr int kSecondsPerMinute = 60;
constexpr int kDecimal = 10;
constexpr uint32_t kDigitBits = 4;

// The 5 x 7 dot font (rows top first, '#' a dot): digits, a colon, R E C S P, the record dot and a space - glyph codes
// in this order.
constexpr int kColon = 10;
constexpr int kR = 11;
constexpr int kE = 12;
constexpr int kC = 13;
constexpr int kS = 14;
constexpr int kP = 15;
constexpr int kDot = 16;
constexpr int kSpace = 17;
constexpr std::array<std::array<const char*, kGlyphRows>, 18> kGlyphs{{
    {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}, // 0
    {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."},
    {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"},
    {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."},
    {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."},
    {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."},
    {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."},
    {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."},
    {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."},
    {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}, // 9
    {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}, // :
    {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}, // R
    {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}, // E
    {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}, // C
    {".####", "#....", "#....", ".###.", "....#", "....#", "####."}, // S
    {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}, // P
    {".....", ".###.", "#####", "#####", "#####", ".###.", "....."}, // the record dot
    {".....", ".....", ".....", ".....", ".....", ".....", "....."}, // space
}};
constexpr int kGlyphCount = static_cast<int>(kGlyphs.size());

// A glyph's rows as two words: rows 0 - 3 (5 bits each from bit 0, the leftmost column the highest), rows 4 - 6.
struct GlyphWords {
    uint32_t top = 0;
    uint32_t bottom = 0;
};
GlyphWords glyph_words(int code)
{
    GlyphWords words;
    const auto& rows = core::at(kGlyphs, static_cast<std::size_t>(code));
    for (int row = 0; row < kGlyphRows; ++row) {
        const std::string line = core::at(rows, static_cast<std::size_t>(row));
        uint32_t bits = 0;
        for (int column = 0; column < kGlyphWidth; ++column) {
            bits = (bits << 1U) | (line.at(static_cast<std::size_t>(column)) == '#' ? 1U : 0U);
        }
        const int place = row < kTopRows ? row : row - kTopRows;
        uint32_t& word = row < kTopRows ? words.top : words.bottom;
        word |= bits << static_cast<uint32_t>(place * kGlyphWidth);
    }
    return words;
}

bool glyph_bit(int code, int column, int row)
{
    if (code < 0 || code >= kGlyphCount || column < 0 || column >= kGlyphWidth || row < 0 || row >= kGlyphRows) {
        return false;
    }
    const GlyphWords words = glyph_words(code);
    const uint32_t rows = row < kTopRows ? words.top : words.bottom;
    const int place = row < kTopRows ? row : row - kTopRows;
    return ((rows >> static_cast<uint32_t>((place * kGlyphWidth) + (kGlyphWidth - 1) - column)) & 1U) != 0U;
}

// The font as GLSL constants (the twin reads the same table).
std::string font_glsl()
{
    std::string top = "const uint kFontTop[" + std::to_string(kGlyphCount) + "] = uint[](";
    std::string bottom = "const uint kFontBottom[" + std::to_string(kGlyphCount) + "] = uint[](";
    for (int code = 0; code < kGlyphCount; ++code) {
        const GlyphWords words = glyph_words(code);
        const char* comma = code + 1 < kGlyphCount ? ", " : "";
        top += std::to_string(words.top) + "u" + comma;
        bottom += std::to_string(words.bottom) + "u" + comma;
    }
    return top + ");\n" + bottom + ");\nconst int kGlyphCount = " + std::to_string(kGlyphCount) + ";\n";
}

constexpr const char* kBody = R"(
layout(location = 0) out vec4 o_colour;

const float kTau = 6.28318530718;
const vec3 kToY = vec3(0.299, 0.587, 0.114);
const vec3 kToI = vec3(0.596, -0.274, -0.322);
const vec3 kToQ = vec3(0.211, -0.523, 0.312);
const float kLumaWeights[5] = float[](0.0625, 0.25, 0.375, 0.25, 0.0625);
const float kChromaWeights[7] = float[](0.0625, 0.125, 0.1875, 0.25, 0.1875, 0.125, 0.0625);

bool glyph_bit(int code, int column, int row)
{
    if (code < 0 || code >= kGlyphCount || column < 0 || column >= 5 || row < 0 || row >= 7) {
        return false;
    }
    uint rows = row < 4 ? kFontTop[code] : kFontBottom[code];
    int place = row < 4 ? row : row - 4;
    return ((rows >> uint(place * 5 + 4 - column)) & 1u) != 0u;
}

// The overlay's items: 0 the record dot and REC, 1 SP, 2 the tape counter (h:mm:ss).
int item_code(int item, int index)
{
    if (item == 0) {
        return index == 0 ? 16 : (index == 1 ? 17 : (index == 2 ? 11 : (index == 3 ? 12 : 13)));
    }
    if (item == 1) {
        return index == 0 ? 14 : 15;
    }
    if (index == 1 || index == 4) {
        return 10;
    }
    int shift = index == 0 ? 16 : (index == 2 ? 12 : (index == 3 ? 8 : (index == 5 ? 4 : 0)));
    return (pc.info.y >> shift) & 15;
}

// The overlay at a place: x a glyph's dot there, y its shadow's, z the dot is the record dot.
vec3 overlay_at(vec2 at)
{
    float g = pc.d.w;
    vec3 found = vec3(0.0);
    for (int item = 0; item < 3; ++item) {
        if (item == 0 && (pc.info.z & 2) == 0) {
            continue;
        }
        vec2 origin = item == 0 ? pc.e.xy : (item == 1 ? vec2(pc.e.x, pc.e.z) : vec2(pc.e.w, pc.e.z));
        int count = item == 0 ? 5 : (item == 1 ? 2 : 7);
        ivec2 cell = ivec2(floor((at - origin) / g));
        for (int shadow = 0; shadow < 2; ++shadow) {
            ivec2 c = cell - ivec2(shadow);
            if (c.x < 0 || c.y < 0 || c.x >= count * 6 || c.y >= 7) {
                continue;
            }
            int index = c.x / 6;
            if (glyph_bit(item_code(item, index), c.x - index * 6, c.y)) {
                if (shadow == 0) {
                    found.x = 1.0;
                    found.z = (item == 0 && index == 0) ? 1.0 : found.z;
                } else {
                    found.y = 1.0;
                }
            }
        }
    }
    return found;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 size = vec2(pc.rect.zw);
    vec2 local = vec2(p - pc.rect.xy) + 0.5;
    float s = pc.a.z;
    uint frame = uint(pc.info.x);
    // 1. the tape's wobble
    int line = int(floor(local.y * pc.f.x));
    float jitter = (hash01(uint(line) * 0x9e3779b9u + frame * 0x632be5abu) - 0.5) * 0.7;
    float bend = sin(kTau * fract(local.y / size.y * 3.0 + pc.b.y)) * 0.6;
    float band = 0.0;
    if ((pc.info.z & 4) != 0) {
        float d = (local.y / size.y - pc.b.z) / 0.06;
        band = pc.b.w * max(0.0, 1.0 - d * d);
    }
    float tear = band * (4.0 + 10.0 * hash01(uint(line) ^ (frame * 0x2c1b3c6du + 81u)));
    vec2 at = vec2(local.x + (jitter + bend + tear) * pc.b.x, local.y);
    // 2. the fisheye
    vec2 centred = at / size * 2.0 - 1.0;
    vec2 square = centred * vec2(pc.a.w, 1.0);
    float diagonal = pc.a.w * pc.a.w + 1.0;
    float r2 = dot(square, square) / diagonal;
    float edge_r2 = pc.a.w * pc.a.w / diagonal;
    float k = pc.a.x;
    vec2 source = (centred * ((1.0 + k * r2) / (1.0 + k * edge_r2)) * 0.5 + 0.5) * size;
    float inside = min(min(source.x, size.x - source.x), min(source.y, size.y - source.y));
    float frame_edge = clamp(inside / (1.5 * s) + 0.5, 0.0, 1.0);
    float vignette = 1.0 - pc.a.y * smoothstep(0.45, 1.0, r2);
    // 3. luma soft, chroma smeared and moved right
    float y = 0.0;
    for (int i = 0; i < 5; ++i) {
        y += kLumaWeights[i] * dot(source_at(source + vec2((float(i) - 2.0) * pc.c.x, 0.0)), kToY);
    }
    vec2 iq = vec2(0.0);
    for (int i = 0; i < 7; ++i) {
        vec3 t = source_at(source + vec2((float(i) - 3.0) * pc.c.y - pc.c.z, 0.0));
        iq += kChromaWeights[i] * vec2(dot(t, kToI), dot(t, kToQ));
    }
    iq *= pc.c.w;
    // 4. the tape's levels and grain
    int column = int(floor(at.x * pc.f.x));
    float grain = (hash01(hash2(column, line, frame * 0x165667b1u)) - 0.5) * (0.07 + band * 0.5) * pc.d.x;
    y = 0.05 + y * 0.9 + grain;
    vec3 rgb = vec3(y + 0.956 * iq.x + 0.621 * iq.y, y - 0.272 * iq.x - 0.647 * iq.y, y - 1.106 * iq.x + 1.703 * iq.y);
    rgb = clamp(rgb, 0.0, 1.0);
    // 5. the overlay
    if ((pc.info.z & 1) != 0) {
        vec3 ink = overlay_at(at);
        rgb = mix(rgb, vec3(0.0), ink.y * 0.6);
        rgb = mix(rgb, ink.z > 0.5 ? vec3(0.92, 0.12, 0.1) : vec3(0.94, 0.94, 0.9), ink.x);
    }
    // 6. scanlines, the lens' corners
    float scan = 1.0 - pc.d.y * 0.4 * (0.5 + 0.5 * cos(kTau * fract(local.y * pc.f.x)));
    rgb = clamp(rgb * (scan * vignette * frame_edge), 0.0, 1.0);
    vec3 own = finite3(texelFetch(u_source, p, 0).rgb);
    o_colour = vec4(mix(own, rgb, pc.d.z), 1.0);
}
)";

struct VhsState {
    std::vector<film_kit::FragmentPipeline> pipelines;
    std::string source;
    bool failed = false;
};

VhsState& vhs()
{
    static VhsState state;
    return state;
}

float unit_or(float value, float fallback, float most)
{
    return std::isfinite(value) ? std::clamp(value, 0.0F, most) : fallback;
}

} // namespace

FilmConstants vhs_constants(const VhsSettings& settings, float strength, const std::array<uint32_t, 4>& shown,
                            double seconds)
{
    constexpr float kMostTwo = 2.0F;
    FilmConstants constants;
    const float width = static_cast<float>(std::max(1U, core::at(shown, 2)));
    const float height = static_cast<float>(std::max(1U, core::at(shown, 3)));
    constants.rect = {static_cast<int32_t>(core::at(shown, 0)), static_cast<int32_t>(core::at(shown, 1)),
                      static_cast<int32_t>(core::at(shown, 2)), static_cast<int32_t>(core::at(shown, 3))};
    const double time = std::isfinite(seconds) ? std::max(seconds, 0.0) : 0.0;
    const float line_height = height / kTapeLines;
    // the frame, the counter, the blink, the band
    const auto frame = static_cast<uint32_t>(static_cast<uint64_t>(std::floor(time * kFramesPerSecond)) & kFrameMask);
    const auto whole = static_cast<int64_t>(std::floor(time));
    const auto hours = static_cast<int32_t>((whole / kSecondsPerHour) % kDecimal);
    const auto minutes = static_cast<int32_t>((whole / kSecondsPerMinute) % kSecondsPerMinute);
    const auto secs = static_cast<int32_t>(whole % kSecondsPerMinute);
    const std::array<int32_t, kCounterDigits> digits{hours, minutes / kDecimal, minutes % kDecimal, secs / kDecimal, secs % kDecimal};
    uint32_t packed = 0;
    for (const int32_t digit : digits) {
        packed = (packed << kDigitBits) | static_cast<uint32_t>(digit);
    }
    const auto slot = static_cast<int64_t>(std::floor(time / kBandSlot));
    const auto within = static_cast<float>(time - (static_cast<double>(slot) * kBandSlot));
    const bool band = within < kBandLength && film_kit::hash01(static_cast<uint32_t>(slot)) < kBandChance;
    const float progress = within / kBandLength;
    const bool blink = (static_cast<int64_t>(std::floor(time * 2.0)) % 2) == 0;
    uint32_t flags = 0;
    flags |= settings.overlay ? kFlagOverlay : 0U;
    flags |= blink ? kFlagBlink : 0U;
    flags |= band ? kFlagBand : 0U;
    constants.info = {static_cast<int32_t>(frame), static_cast<int32_t>(packed), static_cast<int32_t>(flags), 0};
    const float fisheye = unit_or(settings.fisheye, kVhsFisheye, 1.0F);
    const float bleed = unit_or(settings.bleed, 1.0F, kMostTwo);
    const float wobble = unit_or(settings.wobble, 1.0F, kMostTwo);
    const float noise = unit_or(settings.noise, 1.0F, kMostTwo);
    const float scanlines = unit_or(settings.scanlines, kVhsScanlines, 1.0F);
    const float saturation = unit_or(settings.saturation, kVhsSaturation, kMostTwo);
    const double bend_turns = time * static_cast<double>(kBendRate);
    const auto bend_phase = static_cast<float>(bend_turns - std::floor(bend_turns));
    auto& values = constants.values;
    core::at(values, 0) = {fisheye * kFisheye, std::min(1.0F, fisheye * kVignette), line_height, width / height};
    core::at(values, 1) = {wobble * line_height, bend_phase, kBandStart + (kBandTravel * progress), band ? std::sin(kPi * progress) : 0.0F};
    core::at(values, 2) = {kLumaSpacing * line_height, bleed * kChromaSpacing * line_height, bleed * kChromaShift * line_height, saturation};
    const float font = std::max(1.0F, std::round(kFontPixel * line_height));
    core::at(values, 3) = {noise * kGrain, scanlines * std::clamp(line_height - 1.0F, 0.0F, 1.0F), std::clamp(strength, 0.0F, 1.0F),
                           font};
    const float margin_x = std::round(kMarginX * width);
    const float margin_y = std::round(kMarginY * height);
    const float bottom = height - margin_y - (static_cast<float>(kGlyphRows) * font);
    const float counter = width - margin_x - (static_cast<float>((kCounterGlyphs * kGlyphColumns) - 1) * font);
    core::at(values, 4) = {margin_x, margin_y, bottom, counter};
    constexpr std::size_t kPerLine = 5; // (f)
    core::at(values, kPerLine) = {kTapeLines / height, 0.0F, 0.0F, 0.0F};
    return constants;
}

namespace {

// The overlay at a place (overlay_at's twin): its dot, its shadow's, the record dot.
struct OverlayInk {
    bool ink = false;
    bool shadow = false;
    bool red = false;
};

// A glyph of the overlay: its item (0 REC, 1 SP, 2 the counter) and its place in it.
struct GlyphPlace {
    int item = 0;
    int index = 0;
};

int item_code(const FilmConstants& constants, GlyphPlace place)
{
    const int item = place.item;
    const int index = place.index;
    constexpr std::array<int, kRecGlyphs> kRec{kDot, kSpace, kR, kE, kC};
    constexpr std::array<int, kSpGlyphs> kSp{kS, kP};
    constexpr std::array<uint32_t, kCounterGlyphs> kShifts{16, 0, 12, 8, 0, 4, 0};
    constexpr uint32_t kDigitMask = 15;
    if (item == 0) {
        return core::at(kRec, static_cast<std::size_t>(std::min(index, kRecGlyphs - 1)));
    }
    if (item == 1) {
        return core::at(kSp, static_cast<std::size_t>(std::min(index, kSpGlyphs - 1)));
    }
    if (index == 1 || index == 4) {
        return kColon;
    }
    const auto packed = static_cast<uint32_t>(core::at(constants.info, 1));
    return static_cast<int>((packed >> core::at(kShifts, static_cast<std::size_t>(index))) & kDigitMask);
}

OverlayInk overlay_at(const FilmConstants& constants, film_kit::FilmPoint at)
{
    constexpr std::array<int, 3> kCounts{kRecGlyphs, kSpGlyphs, kCounterGlyphs};
    const auto& values = constants.values;
    const float font = core::at(core::at(values, 3), 3);
    const auto& places = core::at(values, 4);
    OverlayInk found;
    for (int item = 0; item < 3; ++item) {
        if (item == 0 && (static_cast<uint32_t>(core::at(constants.info, 2)) & kFlagBlink) == 0) {
            continue;
        }
        const float origin_x = item == 2 ? core::at(places, 3) : core::at(places, 0);
        const float origin_y = item == 0 ? core::at(places, 1) : core::at(places, 2);
        const int count = core::at(kCounts, static_cast<std::size_t>(item));
        const auto cell_x = static_cast<int>(std::floor((at.x - origin_x) / font));
        const auto cell_y = static_cast<int>(std::floor((at.y - origin_y) / font));
        for (int shadow = 0; shadow < 2; ++shadow) {
            const int column = cell_x - shadow;
            const int row = cell_y - shadow;
            if (column < 0 || row < 0 || column >= count * kGlyphColumns || row >= kGlyphRows) {
                continue;
            }
            const int index = column / kGlyphColumns;
            if (glyph_bit(item_code(constants, {.item = item, .index = index}), column - (index * kGlyphColumns), row)) {
                if (shadow == 0) {
                    found.ink = true;
                    found.red = found.red || (item == 0 && index == 0);
                } else {
                    found.shadow = true;
                }
            }
        }
    }
    return found;
}

float dot3(const FilmRgb& a, const std::array<float, 3>& b)
{
    return (core::at(a, 0) * core::at(b, 0)) + (core::at(a, 1) * core::at(b, 1)) + (core::at(a, 2) * core::at(b, 2));
}

} // namespace

std::vector<FilmRgb> vhs_reference(const FilmConstants& constants, const std::vector<FilmRgb>& colour)
{
    constexpr std::array<float, 3> kToY{0.299F, 0.587F, 0.114F};
    constexpr std::array<float, 3> kToI{0.596F, -0.274F, -0.322F};
    constexpr std::array<float, 3> kToQ{0.211F, -0.523F, 0.312F};
    constexpr std::array<float, 5> kLumaWeights{0.0625F, 0.25F, 0.375F, 0.25F, 0.0625F};
    constexpr std::array<float, 7> kChromaWeights{0.0625F, 0.125F, 0.1875F, 0.25F, 0.1875F, 0.125F, 0.0625F};
    constexpr uint32_t kLineMultiplier = 0x9e3779b9U;
    constexpr uint32_t kFrameMultiplier = 0x632be5abU;
    constexpr uint32_t kTearMultiplier = 0x2c1b3c6dU;
    constexpr uint32_t kTearSeed = 81U;
    constexpr uint32_t kGrainMultiplier = 0x165667b1U;
    constexpr float kHalf = 0.5F;
    constexpr float kTwo = 2.0F;
    constexpr float kLumaMiddle = 2.0F;
    constexpr float kChromaMiddle = 3.0F;
    constexpr std::array<float, 3> kFromI{0.956F, -0.272F, -1.106F};
    constexpr std::array<float, 3> kFromQ{0.621F, -0.647F, 1.703F};
    const Extent size = film_kit::size_of(constants);
    const auto& values = constants.values;
    const auto& lens = core::at(values, 0);
    const auto& tape = core::at(values, 1);
    const auto& taps = core::at(values, 2);
    const auto& grain_values = core::at(values, 3);
    const auto width = static_cast<float>(size.width);
    const auto height = static_cast<float>(size.height);
    const float line_height = core::at(lens, 2);
    const float per_line = core::at(core::at(values, 5), 0);
    const auto frame = static_cast<uint32_t>(core::at(constants.info, 0));
    const auto flags = static_cast<uint32_t>(core::at(constants.info, 2));
    std::vector<FilmRgb> out(colour.size());
    for (uint32_t row = 0; row < size.height; ++row) {
        for (uint32_t column = 0; column < size.width; ++column) {
            const float local_x = static_cast<float>(column) + kHalf;
            const float local_y = static_cast<float>(row) + kHalf;
            // 1. the tape'line_height wobble
            const auto line = static_cast<int32_t>(std::floor(local_y * per_line));
            const float jitter =
                (film_kit::hash01((static_cast<uint32_t>(line) * kLineMultiplier) + (frame * kFrameMultiplier)) - kHalf) *
                kJitter;
            const float bend_at = (local_y / height * kBendWaves) + core::at(tape, 1);
            const float bend = std::sin(kTau * (bend_at - std::floor(bend_at))) * kBend;
            float band = 0.0F;
            if ((flags & kFlagBand) != 0) {
                const float distance = ((local_y / height) - core::at(tape, 2)) / kBandHalf;
                band = core::at(tape, 3) * std::max(0.0F, 1.0F - (distance * distance));
            }
            const float tear =
                band * (kTearLeast + (kTearSpan * film_kit::hash01(static_cast<uint32_t>(line) ^
                                                                   ((frame * kTearMultiplier) + kTearSeed))));
            const float at_x = local_x + ((jitter + bend + tear) * core::at(tape, 0));
            const float at_y = local_y;
            // 2. the fisheye
            const float centred_x = (at_x / width * kTwo) - 1.0F;
            const float centred_y = (at_y / height * kTwo) - 1.0F;
            const float square_x = centred_x * core::at(lens, 3);
            const float diagonal = (core::at(lens, 3) * core::at(lens, 3)) + 1.0F;
            const float r2 = ((square_x * square_x) + (centred_y * centred_y)) / diagonal;
            const float edge_r2 = core::at(lens, 3) * core::at(lens, 3) / diagonal;
            const float barrel = core::at(lens, 0);
            const float scale = (1.0F + (barrel * r2)) / (1.0F + (barrel * edge_r2));
            const float source_x = ((centred_x * scale * kHalf) + kHalf) * width;
            const float source_y = ((centred_y * scale * kHalf) + kHalf) * height;
            const float inside = std::min({source_x, width - source_x, source_y, height - source_y});
            const float frame_edge = std::clamp((inside / (kFrameEdge * line_height)) + kHalf, 0.0F, 1.0F);
            const float vignette = 1.0F - (core::at(lens, 1) * film_kit::smoothstep(kVignetteFrom, 1.0F, r2));
            // 3. luma and chroma
            const auto source_at = [&](float x, float y) {
                const float clamped_x = std::clamp(x, kHalf, width - kHalf);
                const float clamped_y = std::clamp(y, kHalf, height - kHalf);
                return film_kit::finite3(film_kit::bilinear(colour, size, {.x = clamped_x, .y = clamped_y}));
            };
            float luma = 0.0F;
            for (std::size_t tap = 0; tap < kLumaWeights.size(); ++tap) {
                const float offset = (static_cast<float>(tap) - kLumaMiddle) * core::at(taps, 0);
                luma += core::at(kLumaWeights, tap) * dot3(source_at(source_x + offset, source_y), kToY);
            }
            float in_phase = 0.0F;
            float quadrature = 0.0F;
            for (std::size_t tap = 0; tap < kChromaWeights.size(); ++tap) {
                const float offset = ((static_cast<float>(tap) - kChromaMiddle) * core::at(taps, 1)) - core::at(taps, 2);
                const FilmRgb sample = source_at(source_x + offset, source_y);
                in_phase += core::at(kChromaWeights, tap) * dot3(sample, kToI);
                quadrature += core::at(kChromaWeights, tap) * dot3(sample, kToQ);
            }
            in_phase *= core::at(taps, 3);
            quadrature *= core::at(taps, 3);
            // 4. levels and grain
            const auto grain_column = static_cast<int32_t>(std::floor(at_x * per_line));
            const float grain = (film_kit::hash01(film_kit::hash2(grain_column, line, frame * kGrainMultiplier)) - kHalf) *
                                 (kGrain + (band * kBandGrain)) * core::at(grain_values, 0);
            luma = kBlackLift + (luma * kLumaRange) + grain;
            FilmRgb rgb{};
            for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
                core::at(rgb, channel) = std::clamp(
                    luma + (core::at(kFromI, channel) * in_phase) + (core::at(kFromQ, channel) * quadrature), 0.0F, 1.0F);
            }
            // 5. the overlay
            if ((flags & kFlagOverlay) != 0) {
                const OverlayInk ink = overlay_at(constants, {.x = at_x, .y = at_y});
                rgb = film_kit::mix3(rgb, {0.0F, 0.0F, 0.0F}, ink.shadow ? kShadow : 0.0F);
                rgb = film_kit::mix3(rgb, ink.red ? kRecRed : kInk, ink.ink ? 1.0F : 0.0F);
            }
            // 6. scanlines, the corners
            const float scan_at = local_y * per_line;
            const float scan =
                1.0F - (core::at(grain_values, 1) * kScanDepth * (kHalf + (kHalf * std::cos(kTau * (scan_at - std::floor(scan_at))))));
            const float shade = scan * vignette * frame_edge;
            for (float& channel : rgb) {
                channel = std::clamp(channel * shade, 0.0F, 1.0F);
            }
            const std::size_t at = (static_cast<std::size_t>(row) * size.width) + column;
            core::at(out, at) = film_kit::mix3(film_kit::finite3(core::at(colour, at)), rgb, core::at(grain_values, 2));
        }
    }
    return out;
}

namespace film_kit {

bool record_vhs(Backend& state, const rt::ViewDrawn& view, const VhsSettings& settings, float strength,
                const FilmTarget& target)
{
    VhsState& own = vhs();
    if (own.failed) {
        return false;
    }
    if (own.source.empty()) {
        own.source = std::string(glsl_common()) + font_glsl() + kBody;
    }
    std::string error;
    const VkPipeline pipeline =
        ensure_common(state, &error)
            ? fragment_pipeline(state, &own.pipelines, own.source, target.colour->format, target.colour->samples, &error)
            : VK_NULL_HANDLE;
    if (pipeline == VK_NULL_HANDLE) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_style vhs: the pass couldn't be made (%s) - no art style", error.c_str());
        }
        return false;
    }
    const FilmConstants constants = vhs_constants(settings, strength, target.shown, view.game_seconds);
    const VkCommandBuffer commands = begin(state, target, constants, {});
    if (commands == VK_NULL_HANDLE) {
        return false;
    }
    draw(commands, pipeline, {.source = source()}, target);
    end(state, commands, target, "vhs", {});
    if (first_report(state, kReportFirstPass)) {
        get_log("vulkan: rt_style vhs: the first pass (%ux%u, %u sample%s; fisheye %.2f, bleed %.2f, wobble %.2f, noise "
                "%.2f, scanlines %.2f, saturation %.2f, overlay %s, strength %.2f)",
                core::at(target.shown, 2), core::at(target.shown, 3), static_cast<unsigned>(target.colour->samples),
                target.colour->samples == 1 ? "" : "s", static_cast<double>(settings.fisheye),
                static_cast<double>(settings.bleed), static_cast<double>(settings.wobble),
                static_cast<double>(settings.noise), static_cast<double>(settings.scanlines),
                static_cast<double>(settings.saturation), settings.overlay ? "on" : "off", static_cast<double>(strength));
    }
    return true;
}

void release_vhs(Backend& state)
{
    VhsState& own = vhs();
    destroy_pipelines(state, &own.pipelines);
    own.failed = false;
}

} // namespace film_kit

} // namespace render::vulkan

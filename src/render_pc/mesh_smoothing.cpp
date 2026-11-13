// NOT ORIGINAL - smoothed curved surfaces at load (mesh_smoothing.h).
#include "render_pc/mesh_smoothing.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ratio>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <span>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/smooth_tess.h"
#include "engine/gfx/surface_material.h"
#include "formats/scene_format.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"

namespace render::mesh_smoothing {

namespace {

constexpr uint32_t kPassTransparent = 0x40; // a material's first pass: drawn with the transparent meshes
constexpr uint8_t kCutOutAlphaRef = 8;      // an alpha reference above this: a cut-out (foliage, fences)
constexpr uint32_t kFvfPositionMask = 0x400E;
constexpr uint32_t kFvfKnown = kFvfXyz | kFvfNormal | kFvfDiffuse | 0xF00U; // position, normal, colour, sets
constexpr uint32_t kFvfTexShift = 8;
constexpr uint32_t kFvfTexMask = 0xF;
constexpr uint32_t kMaxSets = 4;
constexpr uint32_t kPositionBytes = 12;
constexpr uint32_t kColourBytes = 4;
constexpr uint32_t kUvBytes = 8;
constexpr int kHighestLevel = 8;
constexpr float kMiddle = 0.5F; // (a height map's surface; a test ridge's half)
constexpr double kHalf = 0.5;

struct Settings {
    bool on = false;
    tess::Options options;
    bool test_heights = false;
    bool log_meshes = false; // [debug] tess_log: a line a smoothed mesh
};

HeightForMaterial& height_slot()
{
    static HeightForMaterial lookup = nullptr;
    return lookup;
}

// A [Graphics] number with a fraction (`fallback` when it's missing or not a number).
float ini_float(const char* key, float fallback)
{
    constexpr std::size_t kLongest = 32;
    std::array<char, kLongest> text{};
    build_info::fetch_text(build_info::kGraphics, key, "", text.data(), text.size());
    char* end = nullptr;
    const double value = std::strtod(text.data(), &end);
    return end == text.data() || !std::isfinite(value) ? fallback : static_cast<float>(value);
}

// A test height map (rt_tess_test_heights=1, until the generated height maps are there): ridges across the
// texture's u, four to a repeat.
float test_height(const void* /*context*/, float u, float /*v*/)
{
    constexpr float kRidges = 4.0F;
    return kMiddle + (kMiddle * std::sin(u * kRidges * std::numbers::pi_v<float> * 2));
}

const Settings& settings()
{
    static const Settings kSettings = [] {
        Settings read;
        read.on = build_info::fetch_flag(build_info::kGraphics, "rt_tessellation", false);
        read.options.max_level = std::clamp(
            build_info::get_int(build_info::kGraphics, "rt_tess_level", tess::kDefaultLevel), 0, kHighestLevel);
        read.options.max_deviation = std::max(0.0F, ini_float("rt_tess_max_deviation", tess::kDefaultMaxDeviation));
        read.options.height.scale = std::max(0.0F, ini_float("rt_displacement", 0.0F));
        read.test_heights = build_info::fetch_flag(build_info::kGraphics, "rt_tess_test_heights", false);
        read.log_meshes = build_info::fetch_flag("debug", "tess_log", false);
        if (read.on) {
            get_log("rt_tessellation: on - level up to %d, deviation at most %.2f, displacement %.2f%s",
                    read.options.max_level, static_cast<double>(read.options.max_deviation),
                    static_cast<double>(read.options.height.scale), read.test_heights ? " (test heights)" : "");
        }
        return read;
    }();
    return kSettings;
}

struct Totals {
    uint32_t meshes = 0;
    uint32_t looked_at = 0;
    uint32_t triangles_before = 0;
    uint32_t triangles_smoothed = 0;
    uint32_t vertices_added = 0;
    uint32_t indices_before = 0;
    uint32_t indices_after = 0;
    uint32_t capped = 0;
    float deviation = 0.0F;
    float uncapped = 0.0F;
    float displaced = 0.0F;
    double milliseconds = 0.0;
};
Totals& totals()
{
    static Totals made;
    return made;
}

// The vertex layout a format and the body's offsets describe, or false (anything else in a vertex).
bool layout_of(uint32_t fvf, const scene_format::MeshBody& body, tess::VertexLayout* out)
{
    if ((fvf & kFvfPositionMask) != kFvfXyz || (fvf & ~kFvfKnown) != 0U) {
        return false;
    }
    const uint32_t sets = (fvf >> kFvfTexShift) & kFvfTexMask;
    if (sets > kMaxSets) {
        return false;
    }
    uint32_t at = kPositionBytes;
    out->stride = body.stride;
    if ((fvf & kFvfNormal) != 0U) {
        if (core::at(body.offsets, 0) != at) {
            return false;
        }
        out->normal_offset = static_cast<int>(at);
        at += kPositionBytes;
    }
    if ((fvf & kFvfDiffuse) != 0U) {
        if (core::at(body.offsets, 1) != at) {
            return false;
        }
        out->colour_offset = static_cast<int>(at);
        at += kColourBytes;
    }
    if (sets > 0) {
        if (core::at(body.offsets, 2) != at) {
            return false;
        }
        out->uv_offset = static_cast<int>(at);
        out->uv_sets = sets;
        at += kUvBytes * sets;
    }
    return at == body.stride;
}

} // namespace

bool on() { return settings().on; }

void set_height_lookup(HeightForMaterial lookup) { height_slot() = lookup; }

void smooth_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material, bool dynamic)
{
    if (!on() || body == nullptr || material == nullptr || dynamic || body->has_vertex_bytes || !body->scaled.empty() ||
        fvf == 0 || core::at(body->index_counts, 0) < 3) {
        return;
    }
    if ((core::at(material->pass_flags(), 0) & kPassTransparent) != 0U || material->alpha_ref() > kCutOutAlphaRef) {
        return;
    }
    tess::VertexLayout layout;
    if (!layout_of(fvf, *body, &layout) || !scene_format::vertices_fit(*body, body->stride)) {
        return;
    }
    // The material's heights: the installed lookup's (the generated height maps), else the test pattern when asked.
    tess::Options options = settings().options;
    const float scale = options.height.scale;
    options.height = {};
    if (scale > 0.0F) {
        if (height_slot() != nullptr && height_slot()(material, &options.height)) {
            options.height.scale = scale;
        } else if (settings().test_heights) {
            options.height = {.lookup = test_height, .context = nullptr, .scale = scale};
        }
    }
    Totals& sum = totals();
    ++sum.looked_at;
    const auto started = std::chrono::steady_clock::now();
    std::vector<uint16_t>& strip = core::at(body->indices, 0);
    const tess::Result result =
        tess::smooth(body->vertices, body->vertex_count, layout,
                     std::span<const uint16_t>(strip.data(), core::at(body->index_counts, 0)), options);
    sum.milliseconds +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (!result.changed) {
        return;
    }
    ++sum.meshes;
    sum.triangles_before += result.triangles;
    sum.triangles_smoothed += result.smoothed;
    sum.vertices_added += result.vertex_count - body->vertex_count;
    sum.indices_before += core::at(body->index_counts, 0);
    sum.indices_after += static_cast<uint32_t>(result.strip.size());
    sum.capped += result.capped ? 1U : 0U;
    sum.deviation = std::max(sum.deviation, result.deviation);
    sum.uncapped = std::max(sum.uncapped, result.uncapped);
    sum.displaced = std::max(sum.displaced, result.displaced);
    if (settings().log_meshes) {
        std::array<float, 3> low{};
        std::array<float, 3> high{};
        for (uint32_t i = 0; i < body->vertex_count; ++i) {
            std::array<float, 3> point{};
            std::memcpy(point.data(),
                        std::span(body->vertices).subspan(std::size_t{body->stride} * i, sizeof(point)).data(),
                        sizeof(point));
            for (std::size_t k = 0; k < 3; ++k) {
                core::at(low, k) = i == 0 ? core::at(point, k) : std::min(core::at(low, k), core::at(point, k));
                core::at(high, k) = i == 0 ? core::at(point, k) : std::max(core::at(high, k), core::at(point, k));
            }
        }
        get_log("rt_tessellation: mesh at %.0f %.0f %.0f (size %.0f x %.0f x %.0f): %u of %u triangles split, level %d, "
                "deviation %.3f%s, displacement %.3f",
                kHalf * (core::at(low, 0) + core::at(high, 0)), kHalf * (core::at(low, 1) + core::at(high, 1)),
                kHalf * (core::at(low, 2) + core::at(high, 2)), static_cast<double>(core::at(high, 0) - core::at(low, 0)),
                static_cast<double>(core::at(high, 1) - core::at(low, 1)),
                static_cast<double>(core::at(high, 2) - core::at(low, 2)), result.smoothed, result.triangles,
                result.level, static_cast<double>(result.deviation), result.capped ? " (capped)" : "",
                static_cast<double>(result.displaced));
    }
    // (the new vertices replace the body's: the first level of detail's strip too; the others keep the original
    // vertices, still first)
    body->vertices = result.vertices;
    body->vertex_count = static_cast<uint16_t>(result.vertex_count);
    strip = result.strip;
    core::at(body->index_counts, 0) = static_cast<uint16_t>(result.strip.size());
}

void log_totals(const char* scene)
{
    Totals& sum = totals();
    if (sum.meshes != 0) {
        get_log("rt_tessellation: %s - %u of %u meshes smoothed: %u of their %u triangles split, +%u vertices, indices "
                "%u -> %u; deviation at most %.3f (%.3f uncapped, %u capped), displacement %.3f; %.1f ms",
                scene != nullptr ? scene : "(scene)", sum.meshes, sum.looked_at, sum.triangles_smoothed,
                sum.triangles_before, sum.vertices_added, sum.indices_before, sum.indices_after,
                static_cast<double>(sum.deviation), static_cast<double>(sum.uncapped), sum.capped,
                static_cast<double>(sum.displaced), sum.milliseconds);
    }
    sum = Totals{};
}

} // namespace render::mesh_smoothing

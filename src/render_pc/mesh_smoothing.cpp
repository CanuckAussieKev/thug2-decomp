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
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/facade_relief.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/smooth_tess.h"
#include "engine/gfx/surface_material.h"
#include "formats/scene_format.h"
#include "engine/config/settings_model.h"
#include "engine/config/settings_state.h"
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

// The smoothed meshes' original strips (original_strip).
std::unordered_map<const IndexBufferData*, std::vector<uint16_t>>& originals()
{
    static std::unordered_map<const IndexBufferData*, std::vector<uint16_t>> kept;
    return kept;
}

// (added by building relief) the relieved meshes' made vertices' sources (remember_sources)
std::unordered_map<const IndexBufferData*, std::vector<relief::VertexSource>>& made_sources()
{
    static std::unordered_map<const IndexBufferData*, std::vector<relief::VertexSource>> kept;
    return kept;
}

// (round LV) the changed meshes' file vertices and made vertices' sources (remember_made)
struct Made {
    uint32_t original = 0;
    std::vector<relief::VertexSource> made;
};
std::unordered_map<const IndexBufferData*, Made>& made_vertices()
{
    static std::unordered_map<const IndexBufferData*, Made> kept;
    return kept;
}

HeightForMaterial& height_slot()
{
    static HeightForMaterial lookup = nullptr;
    return lookup;
}

// A test height map (rt_tess_test_heights=1, until the generated height maps are there): ridges across the
// texture's u, four to a repeat.
float test_height(const void* /*context*/, float u, float /*v*/)
{
    constexpr float kRidges = 4.0F;
    return kMiddle + (kMiddle * std::sin(u * kRidges * std::numbers::pi_v<float> * 2));
}

// (round CS) The settings as the model has them now: read again whenever its running values are (a change is put to use
// by loading the level again - Apply::kReload: every mesh of the level made with the same).
const Settings& settings()
{
    static Settings kept;
    static uint64_t seen = ~uint64_t{0};
    if (seen == config::running_refreshes()) {
        return kept;
    }
    seen = config::running_refreshes();
    const bool was_on = kept.on;
    kept = [] {
        // (the settings model's running values - engine/config/settings_state.h: a start-up setting, rt_displacement
        // out of effect, 0, without height maps or the test pattern)
        const config::Resolution& in_effect = config::running();
        Settings read;
        read.on = in_effect.on("rt_tessellation");
        read.options.max_level = std::clamp(in_effect.integer("rt_tess_level", tess::kDefaultLevel), 0, kHighestLevel);
        read.options.max_deviation =
            std::max(0.0F, in_effect.number("rt_tess_max_deviation", tess::kDefaultMaxDeviation));
        read.options.height.scale = std::max(0.0F, in_effect.number("rt_displacement", 0.0F));
        read.test_heights = in_effect.on("rt_tess_test_heights");
        // (smoothing: the mesh's normals and the prisms decide the hard edges too, the strength scales the pull -
        // smooth_tess.h)
        read.options.smooth = in_effect.on("rt_tess_smooth");
        read.options.smooth_strength =
            std::clamp(in_effect.number("rt_tess_smooth_strength", tess::kDefaultSmoothStrength), 0.0F, 1.0F);
        read.log_meshes = in_effect.on("tess_log");
        return read;
    }();
    if (kept.on && !was_on) {
        const Settings& read = kept;
        {
            get_log("rt_tessellation: on - level up to %d, deviation at most %.2f, displacement %.2f%s",
                    read.options.max_level, static_cast<double>(read.options.max_deviation),
                    static_cast<double>(read.options.height.scale), read.test_heights ? " (test heights)" : "");
            if (read.options.smooth) {
                get_log("rt_tess_smooth: on - strength %.2f", static_cast<double>(read.options.smooth_strength));
            }
        }
    }
    return kept;
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
    uint32_t prism_edges = 0;
    uint32_t normal_smooth = 0;
    uint32_t normal_hard = 0;
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

} // namespace

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

bool on() { return settings().on; }

void set_height_lookup(HeightForMaterial lookup) { height_slot() = lookup; }

void remember_original(const IndexBufferData* drawn, std::vector<uint16_t> original)
{
    if (drawn != nullptr && !original.empty()) {
        originals()[drawn] = std::move(original);
    }
}

std::span<const uint16_t> original_strip(const IndexBufferData* drawn)
{
    const auto found = originals().find(drawn);
    return found == originals().end() ? std::span<const uint16_t>{} : std::span<const uint16_t>(found->second);
}

void remember_sources(const IndexBufferData* drawn, uint32_t vertex_count, std::vector<relief::VertexSource> sources)
{
    if (drawn != nullptr && !sources.empty() && sources.size() <= vertex_count) {
        made_sources()[drawn] = std::move(sources);
    }
}

std::span<const relief::VertexSource> sources_of(const IndexBufferData* drawn)
{
    const auto found = made_sources().find(drawn);
    return found == made_sources().end() ? std::span<const relief::VertexSource>{}
                                         : std::span<const relief::VertexSource>(found->second);
}

void remember_made(const IndexBufferData* drawn, uint32_t original, std::vector<relief::VertexSource> made)
{
    if (drawn != nullptr && original > 0 && !made.empty()) {
        made_vertices()[drawn] = Made{.original = original, .made = std::move(made)};
    }
}

MadeVertices made_of(const IndexBufferData* drawn)
{
    const auto found = made_vertices().find(drawn);
    return found == made_vertices().end() ? MadeVertices{}
                                          : MadeVertices{.original = found->second.original, .made = found->second.made};
}

void copy_original(const IndexBufferData* from, const IndexBufferData* to)
{
    const std::span<const relief::VertexSource> made = sources_of(from);
    if (!made.empty() && to != nullptr) {
        made_sources()[to] = std::vector<relief::VertexSource>(made.begin(), made.end());
    }
    if (const auto kept = made_vertices().find(from); kept != made_vertices().end() && to != nullptr) {
        Made copy = kept->second;
        made_vertices()[to] = std::move(copy);
    }
    const std::span<const uint16_t> original = original_strip(from);
    if (!original.empty() && to != nullptr) {
        originals()[to] = std::vector<uint16_t>(original.begin(), original.end());
    }
}

void forget_original(const IndexBufferData* drawn)
{
    if (!made_sources().empty()) {
        made_sources().erase(drawn);
    }
    if (!made_vertices().empty()) {
        made_vertices().erase(drawn);
    }
    if (!originals().empty()) {
        originals().erase(drawn);
    }
}

std::vector<uint16_t> smooth_body(scene_format::MeshBody* body, uint32_t fvf, const MaterialRef* material, bool dynamic,
                                  std::vector<relief::VertexSource>* made)
{
    if (!on() || body == nullptr || material == nullptr || dynamic || body->has_vertex_bytes || !body->scaled.empty() ||
        fvf == 0 || core::at(body->index_counts, 0) < 3) {
        return {};
    }
    if ((core::at(material->pass_flags(), 0) & kPassTransparent) != 0U || material->alpha_ref() > kCutOutAlphaRef) {
        return {};
    }
    tess::VertexLayout layout;
    if (!layout_of(fvf, *body, &layout) || !scene_format::vertices_fit(*body, body->stride)) {
        return {};
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
        return {};
    }
    ++sum.meshes;
    sum.triangles_before += result.triangles;
    sum.triangles_smoothed += result.smoothed;
    sum.vertices_added += result.vertex_count - body->vertex_count;
    sum.indices_before += core::at(body->index_counts, 0);
    sum.indices_after += static_cast<uint32_t>(result.strip.size());
    sum.capped += result.capped ? 1U : 0U;
    sum.prism_edges += result.prism_edges;
    sum.normal_smooth += result.normal_smooth;
    sum.normal_hard += result.normal_hard;
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
    if (made != nullptr) { // (round LV: each new vertex's sources among the original ones)
        made->clear();
        made->reserve(result.made.size());
        for (const tess::Result::Made& one : result.made) {
            made->push_back({.from = one.from, .weight = one.weight, .count = static_cast<uint8_t>(one.from.size())});
        }
    }
    body->vertices = result.vertices;
    body->vertex_count = static_cast<uint16_t>(result.vertex_count);
    std::vector<uint16_t> original(strip.begin(), strip.begin() + core::at(body->index_counts, 0));
    strip = result.strip;
    core::at(body->index_counts, 0) = static_cast<uint16_t>(result.strip.size());
    return original;
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
        if (settings().options.smooth) {
            get_log("rt_tess_smooth: %s - %u prism edges rounded, %u edges joined by their shared normals, %u kept hard "
                    "by split ones", scene != nullptr ? scene : "(scene)", sum.prism_edges, sum.normal_smooth,
                    sum.normal_hard);
        }
    }
    sum = Totals{};
}

} // namespace render::mesh_smoothing

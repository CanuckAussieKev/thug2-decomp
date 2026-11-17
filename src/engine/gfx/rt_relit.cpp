// NOT ORIGINAL - the relit mode: see rt_relit.h.
#include "engine/gfx/rt_relit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/lamp_fixtures.h"
#include "engine/gfx/light_store.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "rt_relit_levels.h"

namespace render::rt {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadiansPerDegree = kPi / 180.0;
constexpr double kGoldenFraction = 0.6180339887498949; // (the azimuths' step)
constexpr double kHalf = 0.5;
constexpr std::size_t kBatchRays = std::size_t{1} << 20; // rays a probe_many call
constexpr float kLumaRed = 0.299F;                       // (Rec. 601, as the baked sun's samples)
constexpr float kLumaGreen = 0.587F;
constexpr float kLumaBlue = 0.114F;
constexpr uint32_t kByteMask = 0xFF;
constexpr int kRedShift = 16;
constexpr int kGreenShift = 8;
constexpr int kAlphaShift = 24;
constexpr float kBytesPerLight = 128.0F;
constexpr float kMaxByte = 255.0F;
constexpr std::size_t kMinFitVertices = 100;
constexpr float kDeadSunChannel = 0.1F; // (fit_relit_model) a channel's sun under this share of the strongest's: none
constexpr float kMinSunLight = kRelitMinSunLight;
constexpr float kBlackLight = 1.0F / 128.0F; // a colour this dark (luma) isn't fitted: unlit or painted black
constexpr int kFitRounds = 6; // the pairs' fit, then reweighted (and its light per edge made again) five times
constexpr double kLeastEdgeLight = 0.01; // an edge's light (its terms' mean x beta, beta summing to 1) at least this
constexpr int kLampRounds =
    4; // the lamp-lit vertices looked for at most this often           // the pairs' fit, then reweighted three times
constexpr double kSingular = 1e-12;
constexpr float kLeastRatio = 1e-3F; // a tint's least value before its logarithm
constexpr float kLeastLight = 1e-4F;
constexpr float kLeastWeight = 1e-6F;
constexpr float kUpNormal = 0.7F;          // a face this much up (or down) counts toward the world's winding
constexpr uint32_t kHashOne = 0x7FEB352DU; // (a 32-bit integer hash's steps - Wellons's lowbias32)
constexpr uint32_t kHashTwo = 0x846CA68BU;
constexpr int kHashShiftOne = 16;
constexpr int kHashShiftTwo = 15;
constexpr double kHashScale = 1.0 / 4294967296.0;
constexpr uint32_t kSunSalt = 0x9E3779B9U;
constexpr float kNearlyFlat = 0.9999F;
constexpr float kHermiteThree = 3.0F; // (smoothstep: t^2 (3 - 2t))
constexpr float kHermiteTwo = 2.0F;
constexpr double kThird = 1.0 / 3.0;
constexpr uint32_t kMeshSalt = 0x10000U; // (a vertex's hash: its mesh's index times this, plus its own)
constexpr float kShadowEdgeFacing = 0.95F; // (the fit's report) an edge's vertices facing the same way ...
constexpr float kShadowEdgeStep = 0.5F;    // ... and the rays' sun on one, not the other
constexpr float kFacingSun = 0.7F;         // (the fit's report) a face toward the sun (N.L above this) ...
constexpr float kSunlitShare = 0.75F;      // ... in the rays' sun
constexpr std::size_t kFacingLeast = 4;    // (facing_calibration) a mesh's vertices on each side at least
constexpr std::size_t kFacingMeshes = 16;  // meshes showing both sides at least (a median of a few tells little)
constexpr float kFacingMostFactor = 16.0F; // the sun's factor within 1/this - this
constexpr int kFacingSteps = 24;

struct SettingsSlot {
    std::mutex mutex;
    RelitSettings settings;
    std::string status = "relit: off";
};
SettingsSlot& settings_slot()
{
    static SettingsSlot slot;
    return slot;
}

[[nodiscard]] Vec3 normalised(const Vec3& vi)
{
    const float length = std::sqrt(dot(vi, vi));
    if (!(length > 0.0F) || !std::isfinite(length)) {
        return {0.0F, 1.0F, 0.0F};
    }
    return scaled(vi, 1.0F / length);
}

[[nodiscard]] float luma(const Rgbf& ch)
{
    return (kLumaRed * std::get<0>(ch)) + (kLumaGreen * std::get<1>(ch)) + (kLumaBlue * std::get<2>(ch));
}

uint32_t hashed(uint32_t x)
{
    x ^= x >> kHashShiftOne;
    x *= kHashOne;
    x ^= x >> kHashShiftTwo;
    x *= kHashTwo;
    x ^= x >> kHashShiftOne;
    return x;
}

// Two vectors at right angles to `n` (normalised), making a basis with it.
std::pair<Vec3, Vec3> basis_of(const Vec3& nv)
{
    const Vec3 helper = std::abs(y_of(nv)) < kNearlyFlat ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
    const Vec3 tt = normalised(cross(helper, nv));
    return {tt, cross(nv, tt)};
}

// The k-th of `count` cosine-weighted directions over the hemisphere round `n`, turned by `turn` (0 - 1 of a circle).
// The k-th ray of a set of `count`, the set turned by `turn` (0 - 1 of a circle).
struct RayPick {
    uint32_t index = 0;
    uint32_t count = 1;
    double turn = 0.0;
};

Vec3 hemisphere_direction(const Vec3& nv, const std::pair<Vec3, Vec3>& sides, const RayPick& pick)
{
    const uint32_t idx = pick.index;
    const uint32_t count = pick.count;
    const double turn = pick.turn;
    const double height = (static_cast<double>(idx) + kHalf) / static_cast<double>(count);
    const double across = std::sqrt(height);
    const double angle = 2.0 * kPi * (std::fmod(static_cast<double>(idx) * kGoldenFraction, 1.0) + turn);
    const double up = std::sqrt(std::max(0.0, 1.0 - height));
    return normalised(plus(plus(scaled(sides.first, static_cast<float>(across * std::cos(angle))),
                                scaled(sides.second, static_cast<float>(across * std::sin(angle)))),
                           scaled(nv, static_cast<float>(up))));
}

// The k-th of `count` directions over the sun's disc (its radius `radius` radians), turned by `turn`.
Vec3 sun_direction(const Vec3& toward, const std::pair<Vec3, Vec3>& sides, const RayPick& pick, double radius)
{
    const uint32_t idx = pick.index;
    const uint32_t count = pick.count;
    const double turn = pick.turn;
    if (count <= 1) {
        return toward;
    }
    const double out = std::tan(radius) * std::sqrt((static_cast<double>(idx) + kHalf) / static_cast<double>(count));
    const double angle = 2.0 * kPi * (std::fmod(static_cast<double>(idx) * kGoldenFraction, 1.0) + turn);
    return normalised(plus(toward, plus(scaled(sides.first, static_cast<float>(out * std::cos(angle))),
                                        scaled(sides.second, static_cast<float>(out * std::sin(angle))))));
}

// Where each mesh is among `meshes`, by its id.
std::unordered_map<MeshId, std::size_t> index_of_ids(std::span<const RelitMesh> meshes)
{
    std::unordered_map<MeshId, std::size_t> out;
    for (std::size_t iv = 0; iv < meshes.size(); ++iv) {
        if (core::at(meshes, iv).id != 0) {
            out.emplace(core::at(meshes, iv).id, iv);
        }
    }
    return out;
}

bool is_wanted(std::span<const uint8_t> wanted, std::size_t mesh)
{
    return wanted.empty() || (mesh < wanted.size() && core::at(wanted, mesh) != 0);
}

// The colour (as light) at a point of a triangle: its corners' blended by the point's barycentrics.
Rgbf colour_at(const RelitMesh& mesh, uint32_t primitive, const Vec3& point)
{
    if (primitive >= mesh.triangles.size()) {
        return {};
    }
    const auto [one, two, three] = core::at(mesh.triangles, primitive);
    const std::size_t count = std::min(mesh.positions.size(), mesh.colours.size());
    if (one >= count || two >= count || three >= count) {
        return {};
    }
    const Vec3& pa = core::at(mesh.positions, one);
    const Vec3 e1 = minus(core::at(mesh.positions, two), pa);
    const Vec3 e2 = minus(core::at(mesh.positions, three), pa);
    const Vec3 pv = minus(point, pa);
    const float d11 = dot(e1, e1);
    const float d12 = dot(e1, e2);
    const float d22 = dot(e2, e2);
    const float p1 = dot(pv, e1);
    const float p2 = dot(pv, e2);
    const float det = (d11 * d22) - (d12 * d12);
    float vi = 0.0F;
    float w = 0.0F;
    if (std::abs(det) > kLeastLight) {
        vi = std::clamp(((d22 * p1) - (d12 * p2)) / det, 0.0F, 1.0F);
        w = std::clamp(((d11 * p2) - (d12 * p1)) / det, 0.0F, 1.0F - vi);
    }
    const float u = 1.0F - vi - w;
    // (round L) the light the vertices show when the bounce gives it (physical), else the baked colours'
    const bool shown = mesh.light.size() >= count;
    const Rgbf ca = shown ? core::at(mesh.light, one) : colour_light(core::at(mesh.colours, one));
    const Rgbf cb = shown ? core::at(mesh.light, two) : colour_light(core::at(mesh.colours, two));
    const Rgbf cc = shown ? core::at(mesh.light, three) : colour_light(core::at(mesh.colours, three));
    Rgbf out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) = (core::at(ca, ch) * u) + (core::at(cb, ch) * vi) + (core::at(cc, ch) * w);
    }
    return out;
}

// The vertices of the wanted meshes in batches of whole vertices with at most kBatchRays rays: `rays_of` says a
// vertex's count, `make` writes them, `take` reads their answers. (added for the moving light) With `ask`: one half -
// the rays recorded into it (asked), or its answers taken (answered).
using VertexRef = RelitRun;

template <typename RaysOf, typename Make, typename Take>
void trace_in_batches(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, RayTracing& tracer,
                      RelitStats* stats, RaysOf rays_of, Make make, Take take, RelitAsk* ask)
{
    if (ask != nullptr && ask->answered) {
        const bool whole = ask->answers.size() == ask->requests.size();
        for (const VertexRef& ref : ask->runs) {
            take(ref, whole ? std::span<const ProbeResult>(ask->answers).subspan(ref.first, ref.count)
                            : std::span<const ProbeResult>());
        }
        stats->vertices += ask->runs.size();
        stats->rays += ask->requests.size();
        return;
    }
    if (ask != nullptr) {
        std::vector<RelitSpan> spans = ask->spans;
        if (spans.empty()) {
            for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
                if (is_wanted(wanted, mi)) {
                    spans.push_back({.mesh = mi, .first = 0, .end = core::at(meshes, mi).positions.size()});
                }
            }
        }
        for (const RelitSpan& span : spans) {
            const std::size_t mi = span.mesh;
            const std::size_t vertices = std::min(span.end, mi < meshes.size() ? core::at(meshes, mi).positions.size() : 0);
            for (std::size_t vi = span.first; vi < vertices; ++vi) {
                const uint32_t count = rays_of(mi, vi);
                if (count == 0) {
                    continue;
                }
                const VertexRef ref{
                    .mesh = mi, .vertex = vi, .first = static_cast<uint32_t>(ask->requests.size()), .count = count};
                make(ref, &ask->requests);
                ask->runs.push_back(ref);
            }
        }
        return;
    }
    std::vector<ProbeRequest> requests;
    std::vector<ProbeResult> results;
    std::vector<VertexRef> batch;
    const auto flush = [&]() {
        if (batch.empty()) {
            return;
        }
        tracer.probe_many(requests, &results);
        stats->rays += requests.size();
        const bool answered = results.size() == requests.size();
        for (const VertexRef& ref : batch) {
            take(ref, answered ? std::span<const ProbeResult>(results).subspan(ref.first, ref.count)
                               : std::span<const ProbeResult>());
        }
        requests.clear();
        batch.clear();
    };
    for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
        if (!is_wanted(wanted, mi)) {
            continue;
        }
        const RelitMesh& mesh = core::at(meshes, mi);
        const std::size_t vertices = mesh.positions.size();
        const bool made = mesh.sources.size() == vertices;
        for (std::size_t vi = 0; vi < vertices; ++vi) {
            // (building relief's made vertices take their sources' light: relit_derive)
            const uint32_t count = made && core::at(mesh.sources, vi).count != 0 ? 0U : rays_of(mi, vi);
            if (count == 0) {
                continue;
            }
            if (requests.size() + count > kBatchRays) {
                flush();
            }
            const VertexRef ref{
                .mesh = mi, .vertex = vi, .first = static_cast<uint32_t>(requests.size()), .count = count};
            make(ref, &requests);
            batch.push_back(ref);
            ++stats->vertices;
        }
    }
    flush();
}

// (added by building relief) The made vertices of the meshes `wanted` says given their sources' light.
void derive_wanted(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, std::vector<RelitVertices>* light)
{
    for (std::size_t mi = 0; mi < meshes.size() && mi < light->size(); ++mi) {
        if (is_wanted(wanted, mi) && !core::at(meshes, mi).sources.empty()) {
            relit_derive(core::at(meshes, mi), &core::at(*light, mi));
        }
    }
}

// (added for the moving light) derive_wanted after a trace: at once, or once an asked trace's answers are taken - the
// meshes its spans are in (a mesh's made vertices are its last: derived again when its last span is taken).
void derive_traced(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, std::vector<RelitVertices>* light,
                   const RelitAsk* ask)
{
    if (ask == nullptr || (ask->answered && ask->spans.empty())) {
        derive_wanted(meshes, wanted, light);
        return;
    }
    if (!ask->answered) {
        return;
    }
    std::vector<uint8_t> spanned(meshes.size(), 0);
    for (const RelitSpan& span : ask->spans) {
        if (span.mesh < spanned.size()) {
            core::at(spanned, span.mesh) = 1;
        }
    }
    derive_wanted(meshes, spanned, light);
}

// The sun's direct term of a vertex toward `toward`.
float sun_term(const RelitVertex& vertex, const Vec3& toward)
{
    return vertex.sun * std::max(0.0F, dot(vertex.normal, toward));
}

// The model's light at a vertex with the sun's colour `a` (per channel).
Rgbf model_light(const RelitModel& model, const Rgbf& pa, const RelitVertex& vertex, const Vec3& toward)
{
    const float sv = sun_term(vertex, toward);
    Rgbf out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) = (core::at(pa, ch) * sv) + (core::at(model.sky, ch) * vertex.sky) +
                            (core::at(model.bounce, ch) * core::at(vertex.bounce, ch)) +
                            (core::at(model.lamps, ch) * core::at(vertex.lamp, ch)); // (the lamps' term: 0 until fitted)
    }
    return out;
}

// How far an edge counts in the fit: 1, but an edge across a traced shadow's border (one vertex's sun share
// kShadowEdgeStep over the other's) counts only where the bake has a shadow there too - its colours step down into the
// traced shade, by kRelitLampLow or more (from none at a step of 1). Where the bake has no such shadow (the traced
// geometry's own shadows - its cut-outs, its night pieces, its other meshes - or the bake's shadows left out or
// softened away) the traced border says nothing of the sun's strength, and they are most of them: Berlin's and
// Boston's ~16000 traced borders step 1.00 at the median in the bake, and a fit told by them took the sun to a fifth
// of what the bake's faces show (the faces toward the sun over those turned away: 2.17 in Berlin's bake, 1.43 in that
// fit). (The bake's shadows the rays don't see are edges whose terms don't change: errors the fit weighs down.)
float shadow_edge_trust(const RelitMesh& mesh, const RelitVertices& vertices, std::pair<uint32_t, uint32_t> edge)
{
    const RelitVertex& first = core::at(vertices, edge.first);
    const RelitVertex& second = core::at(vertices, edge.second);
    if (std::abs(first.sun - second.sun) < kShadowEdgeStep) {
        return 1.0F;
    }
    const bool first_lit = first.sun > second.sun;
    const float lit = luma(colour_light(core::at(mesh.colours, first_lit ? edge.first : edge.second)));
    const float shade = luma(colour_light(core::at(mesh.colours, first_lit ? edge.second : edge.first)));
    const float step = lit / std::max(shade, kLeastLight);
    return std::clamp((step - 1.0F) / (kRelitLampLow - 1.0F), 0.0F, 1.0F);
}

// Whether a mesh's used vertices are all one colour (within a byte a channel): painted, not lit by the bake.
bool one_colour(const RelitMesh& mesh, const RelitVertices& vertices)
{
    const std::size_t count = std::min(vertices.size(), mesh.colours.size());
    bool first = true;
    std::array<uint32_t, 3> low{};
    std::array<uint32_t, 3> high{};
    for (std::size_t vi = 0; vi < count; ++vi) {
        if (!core::at(vertices, vi).used) {
            continue;
        }
        const uint32_t colour = core::at(mesh.colours, vi);
        const std::array<uint32_t, 3> bytes{(colour >> kRedShift) & kByteMask, (colour >> kGreenShift) & kByteMask,
                                            colour & kByteMask};
        for (std::size_t ch = 0; ch < 3; ++ch) {
            core::at(low, ch) = first ? core::at(bytes, ch) : std::min(core::at(low, ch), core::at(bytes, ch));
            core::at(high, ch) = first ? core::at(bytes, ch) : std::max(core::at(high, ch), core::at(bytes, ch));
        }
        first = false;
    }
    for (std::size_t ch = 0; ch < 3; ++ch) {
        if (core::at(high, ch) > core::at(low, ch) + 1U) {
            return false;
        }
    }
    return true;
}

// The sun's strength against the rest of the light, told by which way faces turn: in each mesh (one tint) with faces
// toward the bake's sun in the rays' sun (N.L over kFacingSun, sun share over kSunlitShare) and faces turned from it
// (N.L under 0), the bake's colours' medians over the two give how much brighter the sun makes the faces it reaches;
// the factor on the model's sun that makes its light's medians the same, the meshes' median. The edges' fit tells the
// light's parts by their changes along each mesh - but the sun's changes are mostly its traced shadows' borders, and
// where the bake has no such shadow they say nothing (shadow_edge_trust), so the few left can't be told from colour
// changes that happen to lie on a border: alone they made Berlin's sun too strong (its faces 3.48 x against the
// bake's 2.17), all of them too weak (1.43 x). 1 when fewer than kFacingMeshes meshes show both sides. Channel by
// channel (`channel`: 0 red, 1 green, 2 blue) - a tint's colour weighs the sun's and the sky's differently.
float facing_calibration(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light, const RelitModel& model,
                         std::size_t channel)
{
    struct Toward {
        float sun = 0.0F;  // the model's sun here (luma)
        float rest = 0.0F; // the rest of its light
    };
    struct Sides {
        std::vector<Toward> toward;
        float toward_baked = 0.0F; // the bake's median there
        float away_ratio = 0.0F;   // log(bake's median over the model's) on the side turned away
    };
    const auto median_of = [](std::vector<float>* values) {
        const auto middle = values->begin() + static_cast<std::ptrdiff_t>(values->size() / 2);
        std::ranges::nth_element(*values, middle);
        return *middle;
    };
    const float sun_light = core::at(model.sun, channel);
    std::vector<Sides> sides;
    for (std::size_t mi = 0; mi < meshes.size() && mi < light.size(); ++mi) {
        const RelitMesh& mesh = core::at(meshes, mi);
        const RelitVertices& vertices = core::at(light, mi);
        if (one_colour(mesh, vertices)) {
            continue;
        }
        Sides made;
        std::vector<float> toward_baked;
        std::vector<float> away_baked;
        std::vector<float> away_model;
        const std::size_t count = std::min(vertices.size(), mesh.colours.size());
        for (std::size_t vi = 0; vi < count; ++vi) {
            const RelitVertex& vertex = core::at(vertices, vi);
            const Rgbf colour = colour_light(core::at(mesh.colours, vi));
            const float y = core::at(colour, channel);
            if (!vertex.used || y < kBlackLight) {
                continue;
            }
            const float nl = dot(vertex.normal, model.toward);
            RelitVertex shaded = vertex;
            shaded.sun = 0.0F;
            const Rgbf unlit = model_light(model, model.sun, shaded, model.toward);
            const float rest = core::at(unlit, channel);
            if (nl > kFacingSun && vertex.sun > kSunlitShare) {
                made.toward.push_back({.sun = sun_light * vertex.sun * nl, .rest = rest});
                toward_baked.push_back(y);
            } else if (nl < 0.0F && rest > kLeastLight) {
                away_baked.push_back(y);
                away_model.push_back(rest);
            }
        }
        if (made.toward.size() < kFacingLeast || away_baked.size() < kFacingLeast) {
            continue;
        }
        made.toward_baked = median_of(&toward_baked);
        made.away_ratio = std::log(median_of(&away_baked) / median_of(&away_model));
        sides.push_back(std::move(made));
    }
    if (sides.size() < kFacingMeshes) {
        return 1.0F;
    }
    // the meshes' median of log(the model's toward over away / the bake's) at a factor k on the sun: rises with k
    std::vector<float> lights;
    std::vector<float> misses(sides.size());
    const auto miss_at = [&](float factor) {
        for (std::size_t si = 0; si < sides.size(); ++si) {
            const Sides& each = core::at(sides, si);
            lights.clear();
            for (const Toward& vertex : each.toward) {
                lights.push_back((factor * vertex.sun) + vertex.rest);
            }
            // (the bake's toward over away against the model's: log(model toward) - log(bake toward) + away_ratio)
            core::at(misses, si) = std::log(std::max(median_of(&lights), kLeastLight) / each.toward_baked) + each.away_ratio;
        }
        return median_of(&misses);
    };
    float low = 1.0F / kFacingMostFactor;
    float high = kFacingMostFactor;
    if (miss_at(low) >= 0.0F) {
        return low;
    }
    if (miss_at(high) <= 0.0F) {
        return high;
    }
    for (int step = 0; step < kFacingSteps; ++step) {
        const float middle = std::sqrt(low * high);
        (miss_at(middle) < 0.0F ? low : high) = middle;
    }
    return std::sqrt(low * high);
}

// The model's light at a vertex at the bake (its sun toward the bake's): what its tint is taken over.
Rgbf baked_light(const RelitModel& model, const RelitVertex& vertex)
{
    RelitVertex at_bake = vertex;
    at_bake.sun = vertex.sun_baked;
    return model_light(model, model.sun, at_bake, model.toward);
}

float smoothstep(float low, float high, float x)
{
    const float tt = std::clamp((x - low) / (high - low), 0.0F, 1.0F);
    return tt * tt * (kHermiteThree - (kHermiteTwo * tt));
}

// A mesh's tints in logarithms: each vertex's own (its colour over the model's light), its neighbourhood's, and how far
// the model vouches for it.
struct TintField {
    std::vector<Rgbf> own;
    std::vector<Rgbf> nearby;
    std::vector<float> confidence; // (the vertex's own: an excluded vertex's too - it only spreads nothing)
    std::vector<uint8_t> spread;   // its neighbourhood gave a tint (else nearby is its own)
};

// The level's light as the model gives it: the floor under which no vertex's light is taken (a share of the mean),
// and the median (the confidence's scale).
struct LightScale {
    Rgbf floor{kLeastLight, kLeastLight, kLeastLight};
    float median = 1.0F;
};

LightScale light_scale(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light, const RelitModel& model)
{
    LightScale out;
    Rgbf mean{};
    std::vector<float> lumas;
    for (std::size_t mi = 0; mi < meshes.size() && mi < light.size(); ++mi) {
        for (const RelitVertex& vertex : core::at(light, mi)) {
            if (!vertex.used) {
                continue;
            }
            const Rgbf en = model_light(model, model.sun, vertex, model.toward);
            for (std::size_t ch = 0; ch < mean.size(); ++ch) {
                core::at(mean, ch) += core::at(en, ch);
            }
            lumas.push_back(luma(en));
        }
    }
    if (lumas.empty()) {
        return out;
    }
    for (std::size_t ch = 0; ch < mean.size(); ++ch) {
        core::at(out.floor, ch) =
            std::max(kLeastLight, kRelitFloorShare * core::at(mean, ch) / static_cast<float>(lumas.size()));
    }
    const auto middle = lumas.begin() + static_cast<std::ptrdiff_t>(lumas.size() / 2);
    std::ranges::nth_element(lumas, middle);
    out.median = std::max(*middle, kLeastLight);
    return out;
}

// The tints of a mesh: each vertex's own, and its neighbourhood's - a normalised convolution over the mesh's edges,
// kRelitSmoothPasses rings out, each vertex weighted by its confidence (its light over kRelitConfidence x the level's
// median, at most 1; 0 for the vertices `excluded` marks), so the vertices the model vouches for spread their tints
// into those it can't.
TintField local_tints(const RelitMesh& mesh, const RelitVertices& vertices, const RelitModel& model,
                      const LightScale& scale, std::span<const uint8_t> excluded)
{
    const std::size_t count = std::min(vertices.size(), mesh.colours.size());
    TintField out;
    out.own.assign(count, Rgbf{});
    out.confidence.assign(count, 0.0F);
    std::vector<float> weight(count, 0.0F); // (what each spreads: its confidence, 0 when excluded)
    for (std::size_t vi = 0; vi < count; ++vi) {
        const RelitVertex& vertex = core::at(vertices, vi);
        if (!vertex.used) {
            continue;
        }
        const Rgbf en = model_light(model, model.sun, vertex, model.toward);
        const Rgbf y = colour_light(core::at(mesh.colours, vi));
        for (std::size_t ch = 0; ch < en.size(); ++ch) {
            const float ratio = core::at(y, ch) / std::max(core::at(en, ch), core::at(scale.floor, ch));
            core::at(core::at(out.own, vi), ch) = std::log(std::max(ratio, kLeastRatio));
        }
        const bool left_out = vi < excluded.size() && core::at(excluded, vi) != 0;
        core::at(out.confidence, vi) = std::min(1.0F, luma(en) / (kRelitConfidence * scale.median));
        core::at(weight, vi) = left_out ? 0.0F : core::at(out.confidence, vi);
    }
    std::vector<std::vector<uint32_t>> neighbours(count);
    for (const auto& [one, two, three] : mesh.triangles) {
        if (one >= count || two >= count || three >= count) {
            continue;
        }
        for (const auto& [from, to] : {std::pair{one, two}, std::pair{two, three}, std::pair{three, one}}) {
            if (from != to) {
                core::at(neighbours, from).push_back(to);
                core::at(neighbours, to).push_back(from);
            }
        }
    }
    std::vector<Rgbf> numerator(count);
    std::vector<float> denominator(count);
    for (std::size_t vi = 0; vi < count; ++vi) {
        for (std::size_t ch = 0; ch < 3; ++ch) {
            core::at(core::at(numerator, vi), ch) = core::at(core::at(out.own, vi), ch) * core::at(weight, vi);
        }
        core::at(denominator, vi) = core::at(weight, vi);
    }
    for (uint32_t pass = 0; pass < kRelitSmoothPasses; ++pass) {
        std::vector<Rgbf> next_numerator = numerator;
        std::vector<float> next_denominator = denominator;
        for (std::size_t vi = 0; vi < count; ++vi) {
            const std::vector<uint32_t>& around = core::at(neighbours, vi);
            if (around.empty()) {
                continue;
            }
            Rgbf nv = core::at(numerator, vi);
            float dv = core::at(denominator, vi);
            for (const uint32_t other : around) {
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    core::at(nv, ch) += core::at(core::at(numerator, other), ch);
                }
                dv += core::at(denominator, other);
            }
            const auto share = static_cast<float>(around.size() + 1);
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(core::at(next_numerator, vi), ch) = core::at(nv, ch) / share;
            }
            core::at(next_denominator, vi) = dv / share;
        }
        numerator = std::move(next_numerator);
        denominator = std::move(next_denominator);
    }
    out.nearby = out.own;
    out.spread.assign(count, 0);
    for (std::size_t vi = 0; vi < count; ++vi) {
        if (core::at(vertices, vi).used && core::at(denominator, vi) > kLeastWeight) {
            core::at(out.spread, vi) = 1;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(core::at(out.nearby, vi), ch) =
                    core::at(core::at(numerator, vi), ch) / core::at(denominator, vi);
            }
        }
    }
    return out;
}

// A colour of logarithms back as a colour.
Rgbf exp_of(const Rgbf& logs)
{
    return {std::exp(std::get<0>(logs)), std::exp(std::get<1>(logs)), std::exp(std::get<2>(logs))};
}

Rgbf times(const Rgbf& one, const Rgbf& other)
{
    return {std::get<0>(one) * std::get<0>(other), std::get<1>(one) * std::get<1>(other),
            std::get<2>(one) * std::get<2>(other)};
}

// M x = 1 solved on the coefficients `use` keeps (the others 0): false when singular.
bool solve_square(const std::array<std::array<double, 3>, 3>& mi, const std::array<bool, 3>& use,
                  std::array<double, 3>* out)
{
    std::array<std::size_t, 3> map{};
    std::size_t nv = 0;
    for (std::size_t idx = 0; idx < use.size(); ++idx) {
        if (core::at(use, idx)) {
            core::at(map, nv++) = idx;
        }
    }
    std::array<std::array<double, 4>, 3> pa{}; // [row][column], the last column the right side
    for (std::size_t row = 0; row < nv; ++row) {
        for (std::size_t ch = 0; ch < nv; ++ch) {
            core::at(core::at(pa, row), ch) = core::at(core::at(mi, core::at(map, row)), core::at(map, ch));
        }
        core::at(core::at(pa, row), 3) = 1.0;
    }
    for (std::size_t col = 0; col < nv; ++col) {
        std::size_t pivot = col;
        for (std::size_t row = col + 1; row < nv; ++row) {
            if (std::abs(core::at(core::at(pa, row), col)) > std::abs(core::at(core::at(pa, pivot), col))) {
                pivot = row;
            }
        }
        if (std::abs(core::at(core::at(pa, pivot), col)) < kSingular) {
            return false;
        }
        std::swap(core::at(pa, col), core::at(pa, pivot));
        const double inverse = 1.0 / core::at(core::at(pa, col), col);
        for (double& value : core::at(pa, col)) {
            value *= inverse;
        }
        for (std::size_t row = 0; row < nv; ++row) {
            if (row == col) {
                continue;
            }
            const double factor = core::at(core::at(pa, row), col);
            for (std::size_t ch = 0; ch < core::at(pa, row).size(); ++ch) {
                core::at(core::at(pa, row), ch) -= factor * core::at(core::at(pa, col), ch);
            }
        }
    }
    *out = {};
    for (std::size_t row = 0; row < nv; ++row) {
        core::at(*out, core::at(map, row)) = core::at(core::at(pa, row), 3);
    }
    return true;
}

// The direction of one channel's coefficients (a, b, g) from neighbouring vertices: two vertices joined by an edge
// share their surface's colour, so their colours' ratio is their light's - y_i E_j = y_j E_i, a condition on the
// coefficients that no tint enters: (a, b, g) . (y_i x_j - y_j x_i) = 0, x = (S, K, B). Each edge gives one (its
// difference d, over y_i + y_j so a dark pair counts as much as a bright one); the coefficients minimise the sum of
// (beta . d)^2 with beta at least 0 and summing to 1 (the channel's scale isn't in the condition), each edge's over
// (beta . c)^2 - c = y_i x_j + y_j x_i, beta the round before's -: (y_i E_j - y_j E_i) / (y_i E_j + y_j E_i) is half the
// difference of the light's and the colours' log ratios along the edge (nearly), so an edge across a change of colour
// (a painted stripe, the bake's own unevenness) misses by the same under any light. Measured in the light's own units
// instead, the coefficients whose terms are small everywhere (the bounce's) made every such edge cheapest and took the
// light from the sun (Berlin's fit found a fifth of its bake's sun so); over the light's mean, those that make the
// light's contrast across edges large (the sun's) took it from the sky. Reweighted
// kFitRounds - 1 times toward the median residual (an edge across a change of colour counts little). Also its
// objective (the last round's weights) and the least one without the sun's coefficient (with nearly as small an
// objective, the bake's light needs no sun: an indoor level), and the median residual (relative).
struct PairEdge {
    std::array<float, 3> difference{}; // d
    std::array<float, 3> across{};     // c: (y_i x_j + y_j x_i), over y_i + y_j as d
    float trust = 1.0F;                // how far the edge counts (a traced shadow's edge: the bake's agreement there)
};
struct PairFit {
    std::array<double, 3> beta{};
    double objective = 0.0;
    double without_sun = 0.0;
    double spread = 0.0;
};
PairFit pair_direction(std::span<const PairEdge> edges)
{
    PairFit fit;
    std::vector<double> robust(edges.size(), 1.0);
    std::vector<double> weights(edges.size(), 1.0);
    std::array<double, 3> beta{kThird, kThird, kThird};
    const auto dot3 = [](const std::array<double, 3>& coefficients, const std::array<float, 3>& terms) {
        return (core::at(coefficients, 0) * core::at(terms, 0)) + (core::at(coefficients, 1) * core::at(terms, 1)) +
               (core::at(coefficients, 2) * core::at(terms, 2));
    };
    const auto light_of = [&dot3](const std::array<double, 3>& coefficients, const std::array<float, 3>& terms) {
        return std::max(dot3(coefficients, terms), kLeastEdgeLight);
    };
    // an edge's error under `coefficients`: |beta . d| / (beta . c), at most 1 (d's parts are never more than c's);
    // 1 where the light is none - colours the model gives no light to are its worst miss, not a free one
    const auto relative = [&dot3](const std::array<double, 3>& coefficients, const PairEdge& edge) {
        const double light = dot3(coefficients, edge.across);
        return light > kSingular ? std::min(std::abs(dot3(coefficients, edge.difference)) / light, 1.0) : 1.0;
    };
    for (int round = 0; round < kFitRounds; ++round) {
        for (std::size_t idx = 0; idx < edges.size(); ++idx) {
            const double light = light_of(beta, core::at(edges, idx).across);
            core::at(weights, idx) = core::at(robust, idx) * core::at(edges, idx).trust / (light * light);
        }
        std::array<std::array<double, 3>, 3> mi{};
        for (std::size_t idx = 0; idx < edges.size(); ++idx) {
            const std::array<float, 3>& dv = core::at(edges, idx).difference;
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    core::at(core::at(mi, row), ch) += core::at(weights, idx) * core::at(dv, row) * core::at(dv, ch);
                }
            }
        }
        // every subset of the coefficients (the others 0): beta = M^-1 1 / (1 . M^-1 1) on it - the least objective
        // among those with no coefficient below 0
        double best = -1.0;
        double best_without_sun = -1.0;
        constexpr uint32_t kSubsets = 8;
        for (uint32_t subset = 1; subset < kSubsets; ++subset) {
            const std::array<bool, 3> use{(subset & 1U) != 0U, (subset & 2U) != 0U, (subset & 4U) != 0U};
            std::array<double, 3> solved{};
            if (!solve_square(mi, use, &solved)) {
                continue;
            }
            const double total = core::at(solved, 0) + core::at(solved, 1) + core::at(solved, 2);
            if (!(std::abs(total) > kSingular)) {
                continue;
            }
            const std::array<double, 3> candidate{core::at(solved, 0) / total, core::at(solved, 1) / total,
                                                  core::at(solved, 2) / total};
            if (std::ranges::any_of(candidate, [](double pb) { return pb < 0.0; })) {
                continue;
            }
            // (judged by the edges' own relative errors under it: the least squares' weights are the round before's)
            double objective = 0.0;
            for (std::size_t idx = 0; idx < edges.size(); ++idx) {
                const double error = relative(candidate, core::at(edges, idx));
                objective += core::at(robust, idx) * core::at(edges, idx).trust * error * error;
            }
            if (best < 0.0 || objective < best) {
                best = objective;
                beta = candidate;
            }
            if ((subset & 1U) == 0U && (best_without_sun < 0.0 || objective < best_without_sun)) {
                best_without_sun = objective;
            }
        }
        std::vector<double> residuals(edges.size());
        for (std::size_t idx = 0; idx < edges.size(); ++idx) {
            core::at(residuals, idx) = relative(beta, core::at(edges, idx));
        }
        std::vector<double> sorted = residuals;
        const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2);
        std::ranges::nth_element(sorted, middle);
        const double floor = std::max(*middle, kSingular);
        fit.spread = *middle;
        fit.objective = std::max(best, 0.0);
        fit.without_sun = std::max(best_without_sun, 0.0);
        for (std::size_t idx = 0; idx < edges.size(); ++idx) {
            core::at(robust, idx) = 1.0 / std::max(core::at(residuals, idx), floor);
        }
    }
    fit.beta = beta;
    return fit;
}

// --- a small JSON reader: objects, strings, numbers, true / false / null, arrays skipped -----------------------------

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    bool skip_space()
    {
        while (at_ < text_.size() && (core::at(text_, at_) == ' ' || core::at(text_, at_) == '\n' ||
                                      core::at(text_, at_) == '\r' || core::at(text_, at_) == '\t')) {
            ++at_;
        }
        return at_ < text_.size();
    }
    bool take(char ch)
    {
        if (skip_space() && core::at(text_, at_) == ch) {
            ++at_;
            return true;
        }
        return false;
    }
    [[nodiscard]] bool peek(char ch) { return skip_space() && core::at(text_, at_) == ch; }
    bool string(std::string* out)
    {
        if (!take('"')) {
            return false;
        }
        out->clear();
        while (at_ < text_.size() && core::at(text_, at_) != '"') {
            if (core::at(text_, at_) == '\\' && at_ + 1 < text_.size()) {
                ++at_;
            }
            out->push_back(core::at(text_, at_++));
        }
        return take('"');
    }
    // A value of any kind passed over; `number` / `flag` set when it was one.
    struct Scalar {
        bool is_number = false;
        double number = 0.0;
        bool is_flag = false;
        bool flag = false;
        bool is_text = false; // (added by the scene's sun: "sun": "level")
        std::string text;
    };
    bool value(Scalar* scalar)
    {
        *scalar = {};
        if (!skip_space()) {
            return false;
        }
        const char ch = core::at(text_, at_);
        if (ch == '"') {
            scalar->is_text = true;
            return string(&scalar->text);
        }
        if (ch == '{' || ch == '[') {
            return skip_container();
        }
        if (text_.substr(at_).starts_with("true") || text_.substr(at_).starts_with("false")) {
            scalar->is_flag = true;
            scalar->flag = ch == 't';
            at_ += scalar->flag ? std::string_view("true").size() : std::string_view("false").size();
            return true;
        }
        if (text_.substr(at_).starts_with("null")) {
            at_ += std::string_view("null").size();
            return true;
        }
        const std::size_t start = at_;
        while (at_ < text_.size() &&
               (std::string_view("+-.eE0123456789").find(core::at(text_, at_)) != std::string_view::npos)) {
            ++at_;
        }
        if (at_ == start) {
            return false;
        }
        scalar->is_number = true;
        scalar->number = std::strtod(std::string(text_.substr(start, at_ - start)).c_str(), nullptr);
        return true;
    }

private:
    bool skip_container()
    {
        const char open = core::at(text_, at_);
        const char close = open == '{' ? '}' : ']';
        ++at_;
        if (take(close)) {
            return true;
        }
        while (true) {
            if (open == '{') {
                std::string key;
                if (!string(&key) || !take(':')) {
                    return false;
                }
            }
            Scalar ignored;
            if (!value(&ignored)) {
                return false;
            }
            if (take(close)) {
                return true;
            }
            if (!take(',')) {
                return false;
            }
        }
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

bool parse_level(JsonReader& reader, RelitLevel* level)
{
    if (!reader.take('{')) {
        return false;
    }
    if (reader.take('}')) {
        return true;
    }
    while (true) {
        std::string key;
        JsonReader::Scalar scalar;
        if (!reader.string(&key) || !reader.take(':') || !reader.value(&scalar)) {
            return false;
        }
        if (key == "relit" && scalar.is_flag) {
            level->off = !scalar.flag;
        } else if (key == "north" && scalar.is_number && std::isfinite(scalar.number)) {
            constexpr double kFullTurn = 360.0;
            level->north_set = true;
            level->north_heading = static_cast<float>(std::fmod(std::fmod(scalar.number, kFullTurn) + kFullTurn, kFullTurn));
        }
        if (reader.take('}')) {
            return true;
        }
        if (!reader.take(',')) {
            return false;
        }
    }
}

} // namespace

void set_relit_settings(const RelitSettings& settings)
{
    RelitSettings kept = settings;
    kept.sky_rays = std::clamp(kept.sky_rays, 1U, kRelitMaxSkyRays);
    kept.sun_rays = std::clamp(kept.sun_rays, 1U, kRelitMaxSunRays);
    SettingsSlot& slot = settings_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.settings = kept;
}

RelitSettings relit_settings()
{
    SettingsSlot& slot = settings_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.settings;
}

void set_relit_status(std::string text)
{
    SettingsSlot& slot = settings_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.status = std::move(text);
}

std::string relit_status()
{
    SettingsSlot& slot = settings_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.status;
}

namespace {
ScreenProbe& screen_probe_slot()
{
    static ScreenProbe probe = nullptr;
    return probe;
}
} // namespace

void set_screen_probe(ScreenProbe probe) { screen_probe_slot() = probe; }

std::string screen_probe(float across, float down)
{
    const ScreenProbe probe = screen_probe_slot();
    return probe != nullptr ? probe(across, down) : std::string("rt probe: no ray tracing");
}

bool parse_relit_levels(std::string_view json, std::vector<RelitLevel>* out)
{
    JsonReader reader(json);
    if (!reader.take('{')) {
        return false;
    }
    if (reader.take('}')) {
        return true;
    }
    while (true) {
        std::string key;
        if (!reader.string(&key) || !reader.take(':')) {
            return false;
        }
        if (key == "levels" && reader.peek('{')) {
            reader.take('{');
            if (!reader.take('}')) {
                while (true) {
                    RelitLevel level;
                    if (!reader.string(&level.name) || !reader.take(':') || !parse_level(reader, &level)) {
                        return false;
                    }
                    out->push_back(std::move(level));
                    if (reader.take('}')) {
                        break;
                    }
                    if (!reader.take(',')) {
                        return false;
                    }
                }
            }
        } else {
            JsonReader::Scalar ignored;
            if (!reader.value(&ignored)) {
                return false;
            }
        }
        if (reader.take('}')) {
            return true;
        }
        if (!reader.take(',')) {
            return false;
        }
    }
}

std::string_view built_in_relit_levels() { return kRelitLevelsJson; }

const std::vector<RelitLevel>& relit_levels()
{
    static const std::vector<RelitLevel> kLevels = [] {
        std::vector<RelitLevel> levels;
        if (!parse_relit_levels(built_in_relit_levels(), &levels)) {
            get_log("rt_relit: the built-in level list isn't valid JSON of its layout");
        }
        std::string text;
        const std::string path = std::string(build_info::executable_directory()) + "thug2-decomp-relit.json";
        if (fs::read_text(path, &text)) {
            const bool read = parse_relit_levels(text, &levels);
            get_log("rt_relit: %s %s", read ? "read" : "couldn't read (not JSON of the layout)", path.c_str());
        }
        return levels;
    }();
    return kLevels;
}

const RelitLevel* relit_level_entry(uint32_t scene)
{
    const RelitLevel* found = nullptr;
    for (const RelitLevel& entry : relit_levels()) {
        if (checksums::hash_from_text(entry.name.c_str()) == scene) {
            found = &entry;
        }
    }
    return found;
}

Rgbf colour_light(uint32_t argb)
{
    // (lighting round A2 with round F's linear frame: the game's colours are display-referred - rt_display.h - so a
    // colour's light is its byte over 128 decoded; a table of the 256)
    static const std::array<float, 256> kLinear = [] {
        std::array<float, 256> table{};
        for (std::size_t byte = 0; byte < table.size(); ++byte) {
            core::at(table, byte) = linear_from_display(static_cast<float>(byte) * kRelitPerByte);
        }
        return table;
    }();
    return {core::at(kLinear, (argb >> kRedShift) & kByteMask), core::at(kLinear, (argb >> kGreenShift) & kByteMask),
            core::at(kLinear, argb & kByteMask)};
}

void relit_normals(const RelitMesh& mesh, float facing, RelitVertices* out)
{
    const std::size_t count = mesh.positions.size();
    out->assign(count, RelitVertex{});
    std::vector<Vec3> sums(count, Vec3{});
    std::vector<Vec3> centres(count, Vec3{}); // (round A3) the triangles' centres less the vertex, x area ...
    std::vector<float> areas(count, 0.0F);    // ... and their areas
    for (const auto& [one, two, three] : mesh.triangles) {
        if (one >= count || two >= count || three >= count || one == two || two == three || one == three) {
            continue;
        }
        const Vec3& pa = core::at(mesh.positions, one);
        const Vec3 face =
            scaled(cross(minus(core::at(mesh.positions, two), pa), minus(core::at(mesh.positions, three), pa)), facing);
        if (!std::isfinite(dot(face, face))) {
            continue;
        }
        const Vec3 centre = scaled(plus(plus(pa, core::at(mesh.positions, two)), core::at(mesh.positions, three)), 1.0F / 3.0F);
        const float area = std::sqrt(dot(face, face));
        for (const uint32_t corner : {one, two, three}) {
            core::at(sums, corner) = plus(core::at(sums, corner), face); // (its length twice the area: area-weighted)
            core::at(*out, corner).used = true;
            core::at(centres, corner) =
                plus(core::at(centres, corner), scaled(minus(centre, core::at(mesh.positions, corner)), area));
            core::at(areas, corner) += area;
        }
    }
    // (added by building relief) a made vertex's sources are traced for it - corners of no triangle any more (the
    // relief's pieces replaced theirs): used, their normal the made vertices' that come from them
    if (mesh.sources.size() == count) {
        for (std::size_t vi = 0; vi < count; ++vi) {
            const RelitMesh::Source& source = core::at(mesh.sources, vi);
            for (uint8_t k = 0; k < source.count; ++k) {
                const uint32_t from = core::at(source.from, k);
                if (from < count && core::at(mesh.sources, from).count == 0 && !core::at(*out, from).used) {
                    core::at(sums, from) = plus(core::at(sums, from), core::at(sums, vi));
                }
            }
        }
        for (std::size_t vi = 0; vi < count; ++vi) {
            const RelitMesh::Source& source = core::at(mesh.sources, vi);
            for (uint8_t k = 0; k < source.count; ++k) {
                if (core::at(source.from, k) < count) {
                    core::at(*out, core::at(source.from, k)).used = true;
                }
            }
        }
    }
    const bool given = mesh.normals.size() == count;
    for (std::size_t vi = 0; vi < count; ++vi) {
        core::at(*out, vi).normal = normalised(given ? core::at(mesh.normals, vi) : core::at(sums, vi));
        // (round A3) into its own surface: its triangles' centres' mean less it, at most kRelitInward
        if (core::at(areas, vi) > 0.0F) {
            const Vec3 mean = scaled(core::at(centres, vi), 1.0F / core::at(areas, vi));
            const float length = std::sqrt(dot(mean, mean));
            if (std::isfinite(length) && length > 0.0F) {
                core::at(*out, vi).inward = scaled(mean, std::min(length, kRelitInward) / length);
            }
        }
    }
}

Vec3 relit_ray_origin(const Vec3& position, const RelitVertex& vertex)
{
    return plus(plus(position, scaled(vertex.normal, kRelitOffset)), vertex.inward);
}

void relit_derive(const RelitMesh& mesh, RelitVertices* light)
{
    const std::size_t count = std::min(mesh.sources.size(), light->size());
    for (std::size_t vi = 0; vi < count; ++vi) {
        const RelitMesh::Source& source = core::at(mesh.sources, vi);
        if (source.count == 0) {
            continue;
        }
        RelitVertex blended = core::at(*light, vi);
        blended.sun = 0.0F;
        blended.sky = 0.0F;
        blended.bounce = {};
        blended.lamp = {};
        blended.sun_baked = 0.0F;
        for (uint8_t k = 0; k < source.count; ++k) {
            const uint32_t from = core::at(source.from, k);
            if (from >= light->size()) {
                continue;
            }
            const RelitVertex& own = core::at(*light, from);
            const float w = core::at(source.weight, k);
            blended.sun += own.sun * w;
            blended.sky += own.sky * w;
            blended.bounce = plus(blended.bounce, scaled(own.bounce, w));
            blended.lamp = plus(blended.lamp, scaled(own.lamp, w));
            blended.sun_baked += own.sun_baked * w;
        }
        core::at(*light, vi) = blended;
    }
}

float relit_facing(std::span<const RelitMesh> meshes)
{
    double up = 0.0;
    for (const RelitMesh& mesh : meshes) {
        const std::size_t count = mesh.positions.size();
        for (const auto& [one, two, three] : mesh.triangles) {
            if (one >= count || two >= count || three >= count) {
                continue;
            }
            const Vec3& pa = core::at(mesh.positions, one);
            const Vec3 face =
                cross(minus(core::at(mesh.positions, two), pa), minus(core::at(mesh.positions, three), pa));
            const float area = std::sqrt(dot(face, face));
            if (area > 0.0F && std::isfinite(area) && std::abs(y_of(face)) >= kUpNormal * area) {
                up += y_of(face) > 0.0F ? area : -area;
            }
        }
    }
    return up < 0.0 ? -1.0F : 1.0F;
}

void trace_relit_light(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const Vec3& toward,
                       const RelitParams& params, RayTracing& tracer, std::vector<RelitVertices>* light,
                       RelitStats* stats)
{
    light->resize(meshes.size());
    const float facing = relit_facing(meshes);
    for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
        if (is_wanted(wanted, mi)) {
            relit_normals(core::at(meshes, mi), facing, &core::at(*light, mi));
        }
    }
    trace_relit_sky(meshes, wanted, params, tracer, light, stats);
    trace_relit_sun(meshes, wanted, toward, params, tracer, light, stats);
}

void trace_relit_sky(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitParams& params,
                     RayTracing& tracer, std::vector<RelitVertices>* light, RelitStats* stats, RelitAsk* ask)
{
    if (light->size() != meshes.size()) {
        return;
    }
    const std::unordered_map<MeshId, std::size_t> ids = index_of_ids(meshes);
    const uint32_t sky_rays = std::max(1U, params.sky_rays);
    trace_in_batches(
        meshes, wanted, tracer, stats,
        [&](std::size_t mi, std::size_t vi) { return core::at(core::at(*light, mi), vi).used ? sky_rays : 0U; },
        [&](const VertexRef& ref, std::vector<ProbeRequest>* requests) {
            const RelitVertex& vertex = core::at(core::at(*light, ref.mesh), ref.vertex);
            const Vec3 origin =
                relit_ray_origin(core::at(core::at(meshes, ref.mesh).positions, ref.vertex), vertex);
            const std::pair<Vec3, Vec3> basis = basis_of(vertex.normal);
            const double turn =
                static_cast<double>(hashed(static_cast<uint32_t>((ref.mesh * kMeshSalt) + ref.vertex))) * kHashScale;
            for (uint32_t idx = 0; idx < sky_rays; ++idx) {
                requests->push_back({.origin = origin,
                                     .direction = hemisphere_direction(vertex.normal, basis,
                                                                       {.index = idx, .count = sky_rays, .turn = turn}),
                                     .max_distance = kRelitRayLength,
                                     .cull_mask = kRelitWorldMask,
                                     .cutouts = true});
            }
        },
        [&](const VertexRef& ref, std::span<const ProbeResult> answers) {
            RelitVertex& vertex = core::at(core::at(*light, ref.mesh), ref.vertex);
            if (answers.size() != ref.count) {
                return;
            }
            const Vec3 origin =
                relit_ray_origin(core::at(core::at(meshes, ref.mesh).positions, ref.vertex), vertex);
            const std::pair<Vec3, Vec3> basis = basis_of(vertex.normal);
            const double turn =
                static_cast<double>(hashed(static_cast<uint32_t>((ref.mesh * kMeshSalt) + ref.vertex))) * kHashScale;
            uint32_t open = 0;
            Rgbf bounce{};
            for (uint32_t idx = 0; idx < ref.count; ++idx) {
                const ProbeResult& answer = core::at(answers, idx);
                if (!answer.hit) {
                    ++open;
                    continue;
                }
                const auto found = ids.find(answer.mesh);
                if (found == ids.end()) {
                    continue;
                }
                const Vec3 direction =
                    hemisphere_direction(vertex.normal, basis, {.index = idx, .count = ref.count, .turn = turn});
                const Vec3 point = plus(origin, scaled(direction, answer.distance));
                const Rgbf there = colour_at(core::at(meshes, found->second), answer.primitive, point);
                for (std::size_t ch = 0; ch < bounce.size(); ++ch) {
                    core::at(bounce, ch) += core::at(there, ch);
                }
            }
            if (!params.keep_sky) {
                vertex.sky = static_cast<float>(open) / static_cast<float>(ref.count);
            }
            for (float& ch : bounce) {
                ch /= static_cast<float>(ref.count);
            }
            vertex.bounce = bounce;
        },
        ask);
    derive_traced(meshes, wanted, light, ask); // (added by building relief: the made vertices' light)
}

void trace_relit_sun(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const Vec3& toward,
                     const RelitParams& params, RayTracing& tracer, std::vector<RelitVertices>* light,
                     RelitStats* stats, RelitAsk* ask)
{
    if (light->size() != meshes.size()) {
        return;
    }
    const Vec3 sun = normalised(toward);
    const std::pair<Vec3, Vec3> sides = basis_of(sun);
    const uint32_t sun_rays = std::max(1U, params.sun_rays);
    const double radius = static_cast<double>(params.sun_radius_degrees) * kRadiansPerDegree;
    // (the wanted meshes' sun cleared where it is taken - not when the rays are only asked; only the asked spans' when
    // the ask has spans)
    if (ask != nullptr && ask->answered && !ask->spans.empty()) {
        for (const RelitSpan& span : ask->spans) {
            RelitVertices& vertices = core::at(*light, span.mesh);
            for (std::size_t vi = span.first; vi < span.end && vi < vertices.size(); ++vi) {
                core::at(vertices, vi).sun = 0.0F;
            }
        }
    }
    for (std::size_t mi = 0; mi < meshes.size() && (ask == nullptr || (ask->answered && ask->spans.empty())); ++mi) {
        if (!is_wanted(wanted, mi)) {
            continue;
        }
        for (RelitVertex& vertex : core::at(*light, mi)) {
            vertex.sun = 0.0F;
        }
    }
    trace_in_batches(
        meshes, wanted, tracer, stats,
        [&](std::size_t mi, std::size_t vi) {
            const RelitVertex& vertex = core::at(core::at(*light, mi), vi);
            return vertex.used && dot(vertex.normal, sun) > 0.0F ? sun_rays : 0U;
        },
        [&](const VertexRef& ref, std::vector<ProbeRequest>* requests) {
            const RelitVertex& vertex = core::at(core::at(*light, ref.mesh), ref.vertex);
            const Vec3 origin =
                relit_ray_origin(core::at(core::at(meshes, ref.mesh).positions, ref.vertex), vertex);
            const double turn =
                static_cast<double>(hashed(static_cast<uint32_t>((ref.mesh * kMeshSalt) + ref.vertex) ^ kSunSalt)) *
                kHashScale;
            for (uint32_t idx = 0; idx < sun_rays; ++idx) {
                requests->push_back(
                    {.origin = origin,
                     .direction = sun_direction(sun, sides, {.index = idx, .count = sun_rays, .turn = turn}, radius),
                     .max_distance = kRelitRayLength,
                     .cull_mask = kRelitWorldMask,
                     .cutouts = true});
            }
        },
        [&](const VertexRef& ref, std::span<const ProbeResult> answers) {
            if (answers.size() != ref.count) {
                return;
            }
            const auto open = std::ranges::count_if(answers, [](const ProbeResult& answer) { return !answer.hit; });
            core::at(core::at(*light, ref.mesh), ref.vertex).sun =
                static_cast<float>(open) / static_cast<float>(ref.count);
        },
        ask);
    derive_traced(meshes, wanted, light, ask); // (added by building relief: the made vertices' light)
}

RelitModel fit_relit_model(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light, const Vec3& toward,
                           bool sunless)
{
    RelitModel model;
    model.toward = normalised(toward);
    model.sunless = sunless;
    const std::size_t count = std::min(meshes.size(), light.size());
    // each mesh edge's difference (pair_direction), per channel; the vertices' terms and colours for the scale
    std::array<std::vector<PairEdge>, 3> differences;
    struct Term {
        std::array<Rgbf, 3> x{}; // per channel: S, K, B
        Rgbf y{};
    };
    std::vector<Term> terms;
    for (std::size_t mi = 0; mi < count; ++mi) {
        const RelitMesh& mesh = core::at(meshes, mi);
        const RelitVertices& vertices = core::at(light, mi);
        const std::size_t vertex_count = std::min(vertices.size(), mesh.colours.size());
        const auto lit = [&](uint32_t vi) {
            return vi < vertex_count && core::at(vertices, vi).used &&
                   luma(colour_light(core::at(mesh.colours, vi))) >= kBlackLight;
        };
        const auto x_of_vertex = [&](uint32_t vi, std::size_t ch) {
            const RelitVertex& vertex = core::at(vertices, vi);
            return std::array<float, 3>{sun_term(vertex, model.toward), vertex.sky, core::at(vertex.bounce, ch)};
        };
        // (a mesh of one colour all over wasn't lit by the bake - painted one colour - and tells nothing of its light:
        // its edges left out; its vertices still count toward the scale)
        const bool painted = one_colour(mesh, vertices);
        model.one_colour_meshes += painted ? 1 : 0;
        const std::span<const std::array<uint32_t, 3>> triangles =
            painted ? std::span<const std::array<uint32_t, 3>>() : std::span<const std::array<uint32_t, 3>>(mesh.triangles);
        for (const auto& [one, two, three] : triangles) {
            for (const auto& [iv, ja] : {std::pair{one, two}, std::pair{two, three}, std::pair{three, one}}) {
                if (iv == ja || !lit(iv) || !lit(ja)) {
                    continue;
                }
                const Rgbf yi = colour_light(core::at(mesh.colours, iv));
                const Rgbf yj = colour_light(core::at(mesh.colours, ja));
                const float trust = shadow_edge_trust(mesh, vertices, {iv, ja});
                if (!(trust > 0.0F)) {
                    continue;
                }
                for (std::size_t ch = 0; ch < differences.size(); ++ch) {
                    const std::array<float, 3> xi = x_of_vertex(iv, ch);
                    const std::array<float, 3> xj = x_of_vertex(ja, ch);
                    const float over = 1.0F / std::max(core::at(yi, ch) + core::at(yj, ch), kLeastLight);
                    PairEdge edge;
                    edge.trust = trust;
                    for (std::size_t idx = 0; idx < edge.difference.size(); ++idx) {
                        core::at(edge.difference, idx) =
                            ((core::at(yi, ch) * core::at(xj, idx)) - (core::at(yj, ch) * core::at(xi, idx))) * over;
                        core::at(edge.across, idx) =
                            ((core::at(yi, ch) * core::at(xj, idx)) + (core::at(yj, ch) * core::at(xi, idx))) * over;
                    }
                    core::at(differences, ch).push_back(edge);
                }
            }
        }
        for (uint32_t vi = 0; vi < vertex_count; ++vi) {
            if (lit(vi)) {
                terms.push_back({.x = {x_of_vertex(vi, 0), x_of_vertex(vi, 1), x_of_vertex(vi, 2)},
                                 .y = colour_light(core::at(mesh.colours, vi))});
            }
        }
    }
    model.vertices = terms.size();
    if (terms.size() < kMinFitVertices || differences.front().size() < kMinFitVertices) {
        return model;
    }
    // each channel: the direction from the pairs, the scale so that the median tint is 1
    double spread = 0.0;
    double with_sun = 0.0;
    double without_sun = 0.0;
    constexpr std::array<float, 3> kWeights{kLumaRed, kLumaGreen, kLumaBlue};
    for (std::size_t ch = 0; ch < differences.size(); ++ch) {
        const PairFit fit = pair_direction(core::at(differences, ch));
        const std::array<double, 3>& beta = fit.beta;
        spread += core::at(kWeights, ch) * fit.spread;
        with_sun += core::at(kWeights, ch) * fit.objective;
        without_sun += core::at(kWeights, ch) * fit.without_sun;
        std::vector<double> ratios;
        for (const Term& term : terms) {
            const std::array<float, 3>& x = core::at(term.x, ch);
            const double en = (core::at(beta, 0) * core::at(x, 0)) + (core::at(beta, 1) * core::at(x, 1)) +
                              (core::at(beta, 2) * core::at(x, 2));
            if (en > kSingular) {
                ratios.push_back(core::at(term.y, ch) / en);
            }
        }
        if (ratios.empty()) {
            return model;
        }
        const auto middle = ratios.begin() + static_cast<std::ptrdiff_t>(ratios.size() / 2);
        std::ranges::nth_element(ratios, middle);
        const double scale = *middle;
        core::at(model.sun, ch) = static_cast<float>(core::at(beta, 0) * scale);
        core::at(model.sky, ch) = static_cast<float>(core::at(beta, 1) * scale);
        core::at(model.bounce, ch) = static_cast<float>(core::at(beta, 2) * scale);
    }
    model.error = static_cast<float>(spread);
    model.sun_needed = with_sun > 0.0 ? static_cast<float>(without_sun / with_sun) : 0.0F;
    if (sunless) {
        // (lighting round A) a bake with no sun: its light the sky and the bounce (the lamps fitted next) - valid when
        // they light it; its sun the daylight the clock brings
        model.sun = {};
        model.valid = luma(model.sky) + luma(model.bounce) >= kMinSunLight;
        return model;
    }
    // the sun's strength against the rest told by which way the faces turn (facing_calibration), then each channel's
    // scale again (the median tint 1)
    bool seeded = false; // (the offline relit data: every channel's sun seeded - the faces' turning alone tells it)
    // (added by the light energy audit) A channel whose edges gave the sun nothing - their changes explained by the sky or
    // the bounce alone (AU: red 0; sch: red and blue 0; Berlin in a workspace: red 0) - can't be put right by a factor:
    // it starts from the other channels' sun, and the faces' turning in that channel tells its strength. A sun lights
    // every channel; a fit that left one at nothing gave a green sun.
    {
        const float strongest = std::max({core::at(model.sun, 0), core::at(model.sun, 1), core::at(model.sun, 2)});
        float seed = 0.0F;
        int alive = 0;
        for (std::size_t ch = 0; ch < 3; ++ch) {
            if (core::at(model.sun, ch) >= kDeadSunChannel * strongest) {
                seed += core::at(model.sun, ch);
                ++alive;
            }
        }
        // (the offline relit data) no channel's edges gave the sun anything - the bake's sun shows mostly in which way the
        // faces turn, little along the edges (TR, thps3_la, hof: a baked sun found with confidence 0.38 - 0.47, the fit's
        // sun 0): every channel starts from a share of the sky's (grey), and the faces' turning below sets its strength (a bake
        // whose faces don't turn with the sun takes it down to nothing: no model)
        seeded = !(strongest > 0.0F);
        if (seeded) {
            constexpr float kSunSeedOfSky = 0.5F;
            // (a white sun: one channel's sky can be fitted to nothing too - z_lv's green)
            const float grey = (core::at(model.sky, 0) + core::at(model.sky, 1) + core::at(model.sky, 2)) / 3.0F;
            model.sun = {kSunSeedOfSky * grey, kSunSeedOfSky * grey, kSunSeedOfSky * grey};
            model.dead_channels = 3;
        }
        if (strongest > 0.0F && alive > 0 && alive < 3) {
            seed /= static_cast<float>(alive);
            for (std::size_t ch = 0; ch < 3; ++ch) {
                if (core::at(model.sun, ch) < kDeadSunChannel * strongest) {
                    core::at(model.sun, ch) = seed;
                    model.dead_channels += 1;
                }
            }
        }
    }
    Rgbf factors{1.0F, 1.0F, 1.0F};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        core::at(factors, ch) = facing_calibration(meshes.first(count), light.first(count), model, ch);
    }
    model.sun_calibration = luma(factors);
    if (seeded && factors == Rgbf{1.0F, 1.0F, 1.0F}) {
        model.sun = {}; // (too few meshes show both sides to tell the seeded sun's strength: none - no model)
    }
    if (factors != Rgbf{1.0F, 1.0F, 1.0F}) {
        for (std::size_t ch = 0; ch < 3; ++ch) {
            core::at(model.sun, ch) *= core::at(factors, ch);
        }
        for (std::size_t ch = 0; ch < 3; ++ch) {
            std::vector<double> ratios;
            for (const Term& term : terms) {
                const std::array<float, 3>& x = core::at(term.x, ch);
                const double en = (core::at(model.sun, ch) * core::at(x, 0)) + (core::at(model.sky, ch) * core::at(x, 1)) +
                                  (core::at(model.bounce, ch) * core::at(x, 2));
                if (en > kSingular) {
                    ratios.push_back(core::at(term.y, ch) / en);
                }
            }
            if (ratios.empty()) {
                continue;
            }
            const auto middle = ratios.begin() + static_cast<std::ptrdiff_t>(ratios.size() / 2);
            std::ranges::nth_element(ratios, middle);
            const auto again = static_cast<float>(*middle);
            core::at(model.sun, ch) *= again;
            core::at(model.sky, ch) *= again;
            core::at(model.bounce, ch) *= again;
        }
    }
    // (sun_needed is only reported: a bake with no sun is told by rt_sun.h's fit, before this one runs)
    model.valid = luma(model.sun) >= kMinSunLight;
    // (reported: the traced sun's edges - a vertex in the rays' sun beside one in their shade, facing the same way -
    // their colours' ratio in the bake and their light's in the model, the medians: the sun's strength told from the
    // shadows alone)
    std::vector<float> baked_ratios;
    std::vector<float> model_ratios;
    for (std::size_t mi = 0; mi < count; ++mi) {
        const RelitMesh& mesh = core::at(meshes, mi);
        const RelitVertices& vertices = core::at(light, mi);
        if (one_colour(mesh, vertices)) {
            continue;
        }
        const std::size_t vertex_count = std::min(vertices.size(), mesh.colours.size());
        for (const auto& [one, two, three] : mesh.triangles) {
            for (const auto& [iv, ja] : {std::pair{one, two}, std::pair{two, three}, std::pair{three, one}}) {
                if (iv >= vertex_count || ja >= vertex_count || iv == ja) {
                    continue;
                }
                const RelitVertex& first = core::at(vertices, iv);
                const RelitVertex& second = core::at(vertices, ja);
                const float yi = luma(colour_light(core::at(mesh.colours, iv)));
                const float yj = luma(colour_light(core::at(mesh.colours, ja)));
                if (!first.used || !second.used || yi < kBlackLight || yj < kBlackLight ||
                    dot(first.normal, second.normal) < kShadowEdgeFacing) {
                    continue;
                }
                const float si = sun_term(first, model.toward);
                const float sj = sun_term(second, model.toward);
                if (std::abs(si - sj) < kShadowEdgeStep) {
                    continue;
                }
                if (shadow_edge_trust(mesh, vertices, {iv, ja}) >= 1.0F) {
                    ++model.shadow_edges_kept;
                }
                const bool first_lit = si > sj;
                const float lit_light = luma(model_light(model, model.sun, first_lit ? first : second, model.toward));
                const float shade_light = luma(model_light(model, model.sun, first_lit ? second : first, model.toward));
                baked_ratios.push_back(first_lit ? yi / yj : yj / yi);
                model_ratios.push_back(lit_light / std::max(shade_light, kLeastLight));
            }
        }
    }
    const auto median_of = [](std::vector<float>* values) {
        if (values->empty()) {
            return 0.0F;
        }
        const auto middle = values->begin() + static_cast<std::ptrdiff_t>(values->size() / 2);
        std::ranges::nth_element(*values, middle);
        return *middle;
    };
    // (reported) the faces toward the sun in the rays' sun against those turned from it: their colours' medians' ratio
    // in the bake, and their light's in the model - the sun's strength told by which way surfaces face, not by shadows
    {
        std::vector<float> toward_baked;
        std::vector<float> toward_model;
        std::vector<float> away_baked;
        std::vector<float> away_model;
        for (std::size_t mi = 0; mi < count; ++mi) {
            const RelitMesh& mesh = core::at(meshes, mi);
            const RelitVertices& vertices = core::at(light, mi);
            if (one_colour(mesh, vertices)) {
                continue;
            }
            const std::size_t vertex_count = std::min(vertices.size(), mesh.colours.size());
            for (std::size_t vi = 0; vi < vertex_count; ++vi) {
                const RelitVertex& vertex = core::at(vertices, vi);
                const float y = luma(colour_light(core::at(mesh.colours, vi)));
                if (!vertex.used || y < kBlackLight) {
                    continue;
                }
                const float nl = dot(vertex.normal, model.toward);
                const float lit = luma(model_light(model, model.sun, vertex, model.toward));
                if (nl > kFacingSun && vertex.sun > kSunlitShare) {
                    toward_baked.push_back(y);
                    toward_model.push_back(lit);
                } else if (nl < 0.0F) {
                    away_baked.push_back(y);
                    away_model.push_back(lit);
                }
            }
        }
        const auto median_of = [](std::vector<float>* values) {
            if (values->empty()) {
                return 0.0F;
            }
            const auto middle = values->begin() + static_cast<std::ptrdiff_t>(values->size() / 2);
            std::ranges::nth_element(*values, middle);
            return *middle;
        };
        const float away_bake = median_of(&away_baked);
        const float away_lit = median_of(&away_model);
        model.facing_baked = away_bake > 0.0F ? median_of(&toward_baked) / away_bake : 0.0F;
        model.facing_model = away_lit > 0.0F ? median_of(&toward_model) / away_lit : 0.0F;
    }
    model.shadow_edges = baked_ratios.size();
    model.shadow_baked = median_of(&baked_ratios);
    model.shadow_model = median_of(&model_ratios);
    return model;
}

float relit_edge_error(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light, const RelitModel& model)
{
    constexpr std::array<float, 3> kWeights{kLumaRed, kLumaGreen, kLumaBlue};
    std::vector<float> errors;
    const std::size_t count = std::min(meshes.size(), light.size());
    for (std::size_t mi = 0; mi < count; ++mi) {
        const RelitMesh& mesh = core::at(meshes, mi);
        const RelitVertices& vertices = core::at(light, mi);
        if (one_colour(mesh, vertices)) {
            continue;
        }
        const std::size_t vertex_count = std::min(vertices.size(), mesh.colours.size());
        const auto lit = [&](uint32_t vi) {
            return vi < vertex_count && core::at(vertices, vi).used &&
                   luma(colour_light(core::at(mesh.colours, vi))) >= kBlackLight;
        };
        for (const auto& [one, two, three] : mesh.triangles) {
            for (const auto& [iv, ja] : {std::pair{one, two}, std::pair{two, three}, std::pair{three, one}}) {
                if (iv >= ja || !lit(iv) || !lit(ja)) { // (each edge once: from its lower vertex)
                    continue;
                }
                const Rgbf yi = colour_light(core::at(mesh.colours, iv));
                const Rgbf yj = colour_light(core::at(mesh.colours, ja));
                const Rgbf ei = model_light(model, model.sun, core::at(vertices, iv), model.toward);
                const Rgbf ej = model_light(model, model.sun, core::at(vertices, ja), model.toward);
                float error = 0.0F;
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    const float cross_one = core::at(yi, ch) * core::at(ej, ch);
                    const float cross_two = core::at(yj, ch) * core::at(ei, ch);
                    const float sum = cross_one + cross_two;
                    error += core::at(kWeights, ch) * (sum > kLeastLight ? std::abs(cross_one - cross_two) / sum : 1.0F);
                }
                errors.push_back(error);
            }
        }
    }
    if (errors.empty()) {
        return 1.0F;
    }
    // (the mean, not the median: the sun's evidence - its shadows' borders, the faces turned to it or from it - is in a
    // minority of the edges; a median over them all is the flat surfaces', the same for every model)
    double sum = 0.0;
    for (const float error : errors) {
        sum += static_cast<double>(error);
    }
    return static_cast<float>(sum / static_cast<double>(errors.size()));
}

float relit_sun_share(const RelitModel& model)
{
    if (model.sunless || !model.valid) {
        return 0.0F;
    }
    // (an open level surface: the sun's light there - a x its sine - over the whole, the sky's b with it)
    const float sine = std::max(0.0F, y_of(model.toward));
    const float direct = luma(model.sun) * sine;
    const float whole = direct + luma(model.sky);
    return whole > 0.0F ? direct / whole : 0.0F;
}

void decompose_relit(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitParams& params,
                     RelitModel* model, std::vector<RelitVertices>* light)
{
    if (!model->valid) {
        return;
    }
    model->brighter = 0;
    model->darker = 0;
    model->across = 0;
    const std::size_t count = std::min(meshes.size(), light->size());
    const LightScale scale =
        light_scale(meshes.first(count), std::span<const RelitVertices>(*light).first(count), *model);
    // each vertex's bake over its tint x the model's light (1: they agree) - the evidence relit_light_across reads
    std::vector<std::vector<float>> differs(count);
    for (std::size_t mi = 0; mi < count; ++mi) {
        const RelitMesh& mesh = core::at(meshes, mi);
        RelitVertices& vertices = core::at(*light, mi);
        const std::size_t vertex_count = std::min(vertices.size(), mesh.colours.size());
        core::at(differs, mi).assign(vertex_count, 1.0F);
        if (!is_wanted(wanted, mi)) {
            continue;
        }
        std::vector<Rgbf> lights(vertex_count);
        for (std::size_t vi = 0; vi < vertex_count; ++vi) {
            RelitVertex& vertex = core::at(vertices, vi);
            vertex.sun_baked = vertex.sun; // (traced toward the bake's sun for the analysis)
            core::at(lights, vi) = baked_light(*model, vertex);
        }
        // The neighbourhoods' tints, then again without the vertices whose bake differs from what theirs predicts -
        // brighter (lamps the list doesn't hold, the bake's sun where the rays saw a caster) or darker (the bake's
        // shadows where the rays saw the sun, its darkened corners): their colour holds light the model doesn't, which
        // mustn't spread. kLampRounds times: a wide pool of light or shade moves its neighbourhood's tint, so its edges
        // show first.
        std::vector<uint8_t> differing(vertex_count, 0);
        TintField field = local_tints(mesh, vertices, *model, scale, differing);
        for (int round = 0; round < kLampRounds; ++round) {
            bool more = false;
            for (std::size_t vi = 0; vi < vertex_count; ++vi) {
                const float predicted = luma(times(exp_of(core::at(field.nearby, vi)), core::at(lights, vi)));
                const float y = luma(colour_light(core::at(mesh.colours, vi)));
                if (core::at(differing, vi) == 0 && core::at(field.spread, vi) != 0 &&
                    (y > kRelitLampLow * predicted || y * kRelitLampLow < predicted)) {
                    core::at(differing, vi) = 1;
                    more = true;
                }
            }
            if (!more) {
                break;
            }
            field = local_tints(mesh, vertices, *model, scale, differing);
        }
        for (std::size_t vi = 0; vi < vertex_count; ++vi) {
            RelitVertex& vertex = core::at(vertices, vi);
            if (!vertex.used) {
                continue;
            }
            const Rgbf& en = core::at(lights, vi);
            const Rgbf y = colour_light(core::at(mesh.colours, vi));
            const Rgbf& nearby = core::at(field.nearby, vi);
            const Rgbf near_light = times(exp_of(nearby), en);
            // how far the bake differs from the surroundings' prediction, either way (1: not at all): from kRelitLampLow
            // on a share of the vertex's own tint gives way to its surroundings', all of it from kRelitLampHigh
            const float ratio = luma(y) / std::max(luma(near_light), kLeastLight);
            const float apart = std::max(ratio, 1.0F / std::max(ratio, kLeastRatio));
            const bool spread = core::at(field.spread, vi) != 0;
            const float differ = spread ? smoothstep(kRelitLampLow, kRelitLampHigh, apart) : 0.0F;
            if (differ > 0.0F) {
                ++(ratio > 1.0F ? model->brighter : model->darker);
            }
            const float keep = spread ? params.detail * core::at(field.confidence, vi) * (1.0F - differ) : 1.0F;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                const float own =
                    std::log(std::max(core::at(y, ch) / std::max(core::at(en, ch), core::at(scale.floor, ch)), kLeastRatio));
                const float log_tint = core::at(nearby, ch) + ((own - core::at(nearby, ch)) * keep);
                core::at(vertex.tint, ch) = std::clamp(std::exp(log_tint), 0.0F, kRelitMaxTint);
            }
            const float drawn = luma(times(vertex.tint, en));
            core::at(core::at(differs, mi), vi) = drawn > kLeastLight ? luma(y) / drawn : 1.0F;
        }
    }
    // the light a mesh holds whole (a lamp's over a recessed door, a shadow over a small patch) told by its neighbours
    model->across = relit_light_across(meshes.first(count), wanted, differs, std::span<RelitVertices>(*light).first(count));
}

std::size_t relit_light_across(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted,
                               std::span<const std::vector<float>> differs, std::span<RelitVertices> light)
{
    std::size_t changed = 0;
    const std::size_t count = std::min({meshes.size(), light.size(), differs.size()});
    // every vertex's difference (a factor on its light: light adds, so its neighbours' are averaged as they are), in a
    // grid kRelitLocalReach across
    struct Evidence {
        Vec3 at{};
        Vec3 normal{};
        float differ = 1.0F;
        std::size_t mesh = 0;
    };
    std::vector<Evidence> evidence;
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    const auto cell_of = [](float value) { return static_cast<int64_t>(std::floor(value / kRelitLocalReach)); };
    const auto key_of = [](int64_t x, int64_t y, int64_t z) {
        constexpr int64_t kBits = 21;
        constexpr int64_t kMask = (int64_t{1} << kBits) - 1;
        constexpr int64_t kBias = int64_t{1} << (kBits - 1);
        return ((x + kBias) & kMask) | (((y + kBias) & kMask) << kBits) | (((z + kBias) & kMask) << (2 * kBits));
    };
    for (std::size_t mi = 0; mi < count; ++mi) {
        const RelitMesh& mesh = core::at(meshes, mi);
        const RelitVertices& vertices = core::at(light, mi);
        const std::vector<float>& differ = core::at(differs, mi);
        const std::size_t vertex_count = std::min({vertices.size(), mesh.positions.size(), differ.size()});
        for (std::size_t vi = 0; vi < vertex_count; ++vi) {
            const RelitVertex& vertex = core::at(vertices, vi);
            if (!vertex.used) {
                continue;
            }
            const Vec3& at = core::at(mesh.positions, vi);
            grid[key_of(cell_of(x_of(at)), cell_of(y_of(at)), cell_of(z_of(at)))].push_back(
                static_cast<uint32_t>(evidence.size()));
            evidence.push_back({.at = at,
                                .normal = vertex.normal,
                                .differ = core::at(differ, vi),
                                .mesh = mi});
        }
    }

    for (std::size_t mi = 0; mi < count; ++mi) {
        if (!is_wanted(wanted, mi)) {
            continue;
        }
        const RelitMesh& mesh = core::at(meshes, mi);
        RelitVertices& vertices = core::at(light, mi);
        const std::vector<float>& differ = core::at(differs, mi);
        const std::size_t vertex_count = std::min({vertices.size(), mesh.positions.size(), differ.size()});
        for (std::size_t vi = 0; vi < vertex_count; ++vi) {
            RelitVertex& vertex = core::at(vertices, vi);
            if (!vertex.used) {
                continue;
            }
            const float own = std::max(core::at(differ, vi), kLeastRatio);
            const Vec3& at = core::at(mesh.positions, vi);
            const int64_t cx = cell_of(x_of(at));
            const int64_t cy = cell_of(y_of(at));
            const int64_t cz = cell_of(z_of(at));
            float sum = 0.0F;
            float weights = 0.0F;
            for (int64_t dz = -1; dz <= 1; ++dz) {
                for (int64_t dy = -1; dy <= 1; ++dy) {
                    for (int64_t dx = -1; dx <= 1; ++dx) {
                        const auto found = grid.find(key_of(cx + dx, cy + dy, cz + dz));
                        if (found == grid.end()) {
                            continue;
                        }
                        for (const uint32_t index : found->second) {
                            const Evidence& other = core::at(evidence, index);
                            if (other.mesh == mi) {
                                continue; // (its own mesh's word is the vertex's own)
                            }
                            const float facing = dot(other.normal, vertex.normal);
                            if (facing <= kRelitLocalFacing) {
                                continue;
                            }
                            const Vec3 apart = minus(other.at, at);
                            const float closeness = 1.0F - (dot(apart, apart) / (kRelitLocalReach * kRelitLocalReach));
                            if (closeness <= 0.0F) {
                                continue;
                            }
                            const float weight = facing * facing * closeness * closeness;
                            sum += weight * other.differ;
                            weights += weight;
                        }
                    }
                }
            }
            if (!(weights > kRelitLocalLeastWeight)) {
                continue;
            }
            const float around = std::max(sum / weights, kLeastRatio);
            if (around <= own * kRelitLocalOver && around * kRelitLocalOver >= own) {
                continue; // (the vertex's own surroundings differ as much: nothing it holds whole)
            }
            // its tint over the difference its neighbours show (its light then as theirs: the lamp's, the shadow's)
            const float factor = own / around;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(vertex.tint, ch) = std::clamp(core::at(vertex.tint, ch) * factor, 0.0F, kRelitMaxTint);
            }
            ++changed;
        }
    }
    return changed;
}
// --- (the offline relit data) the lamps' term -----------------------------------------------------------------------

void trace_relit_lamps(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted,
                       std::span<const RelitLamp> lamps, RayTracing& tracer, std::vector<RelitVertices>* light,
                       RelitStats* stats, RelitLampLinks* links)
{
    if (light->size() != meshes.size()) {
        return;
    }
    // the lamps by where they are: a grid of cells kLampCell across, each lamp in every cell its reach touches
    constexpr float kLampCell = 256.0F;
    constexpr float kLeastFalloff = 1e-3F; // (a lamp this faint at the vertex isn't traced)
    const auto cell_of = [](float value) { return static_cast<int64_t>(std::floor(value / kLampCell)); };
    const auto key_of = [](int64_t x, int64_t y, int64_t z) {
        constexpr int64_t kBits = 21;
        constexpr int64_t kMask = (int64_t{1} << kBits) - 1;
        constexpr int64_t kBias = int64_t{1} << (kBits - 1);
        return ((x + kBias) & kMask) | (((y + kBias) & kMask) << kBits) | (((z + kBias) & kMask) << (2 * kBits));
    };
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    for (std::size_t li = 0; li < lamps.size(); ++li) {
        const RelitLamp& lamp = core::at(lamps, li);
        if (!(lamp.reach > 0.0F) || !std::isfinite(lamp.reach)) {
            continue;
        }
        const Vec3& at = lamp.position;
        for (int64_t z = cell_of(z_of(at) - lamp.reach); z <= cell_of(z_of(at) + lamp.reach); ++z) {
            for (int64_t y = cell_of(y_of(at) - lamp.reach); y <= cell_of(y_of(at) + lamp.reach); ++y) {
                for (int64_t x = cell_of(x_of(at) - lamp.reach); x <= cell_of(x_of(at) + lamp.reach); ++x) {
                    grid[key_of(x, y, z)].push_back(static_cast<uint32_t>(li));
                }
            }
        }
    }
    const std::vector<uint32_t> none;
    const auto lamps_near = [&](const Vec3& at) -> const std::vector<uint32_t>& {
        const auto found = grid.find(key_of(cell_of(x_of(at)), cell_of(y_of(at)), cell_of(z_of(at))));
        return found != grid.end() ? found->second : none;
    };
    // a vertex's lamps: those it faces within their reach, bright enough to count (lamp_geometry: the shape's)
    struct Reach {
        uint32_t lamp = 0;
        float weight = 0.0F; // lamp_geometry
        Vec3 from{};         // the point the light comes from
    };
    std::vector<LampShape> shapes;
    shapes.reserve(lamps.size());
    for (const RelitLamp& lamp : lamps) {
        shapes.push_back(lamp_shape(lamp));
    }
    const auto reached = [&](std::size_t mi, std::size_t vi, std::vector<Reach>* out) {
        out->clear();
        const RelitVertex& vertex = core::at(core::at(*light, mi), vi);
        if (!vertex.used) {
            return;
        }
        const Vec3& at = core::at(core::at(meshes, mi).positions, vi);
        for (const uint32_t li : lamps_near(at)) {
            Vec3 from{};
            const float weight = lamp_geometry(core::at(shapes, li), at, vertex.normal, &from);
            if (weight > kLeastFalloff) {
                out->push_back({.lamp = li, .weight = weight, .from = from});
            }
        }
    };
    // (round A2) the links kept per vertex while the answers come, then packed
    std::vector<std::vector<std::vector<RelitLampLink>>> gathered;
    if (links != nullptr) {
        gathered.resize(meshes.size());
        for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
            if (is_wanted(wanted, mi)) {
                core::at(gathered, mi).resize(core::at(*light, mi).size());
            }
        }
    }
    for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
        if (is_wanted(wanted, mi)) {
            for (RelitVertex& vertex : core::at(*light, mi)) {
                vertex.lamp = {};
            }
        }
    }
    std::vector<Reach> scratch;
    trace_in_batches(
        meshes, wanted, tracer, stats,
        [&](std::size_t mi, std::size_t vi) {
            reached(mi, vi, &scratch);
            return static_cast<uint32_t>(scratch.size());
        },
        [&](const VertexRef& ref, std::vector<ProbeRequest>* requests) {
            reached(ref.mesh, ref.vertex, &scratch);
            const RelitVertex& vertex = core::at(core::at(*light, ref.mesh), ref.vertex);
            const Vec3 origin =
                relit_ray_origin(core::at(core::at(meshes, ref.mesh).positions, ref.vertex), vertex);
            for (const Reach& reach : scratch) {
                const RelitLamp& lamp = core::at(lamps, reach.lamp);
                const Vec3 apart = minus(reach.from, origin);
                const float distance = std::sqrt(dot(apart, apart));
                requests->push_back({.origin = origin,
                                     .direction = normalised(apart),
                                     .max_distance = std::max(0.0F, distance - lamp.size),
                                     .cull_mask = kRelitWorldMask,
                                     .cutouts = true});
            }
        },
        [&](const VertexRef& ref, std::span<const ProbeResult> answers) {
            reached(ref.mesh, ref.vertex, &scratch);
            if (answers.size() != ref.count || scratch.size() != ref.count) {
                return;
            }
            RelitVertex& vertex = core::at(core::at(*light, ref.mesh), ref.vertex);
            for (std::size_t k = 0; k < scratch.size(); ++k) {
                if (core::at(answers, k).hit) {
                    continue;
                }
                const Reach& reach = core::at(scratch, k);
                vertex.lamp = plus(vertex.lamp, scaled(core::at(lamps, reach.lamp).light, reach.weight));
                if (links != nullptr) {
                    core::at(core::at(gathered, ref.mesh), ref.vertex).push_back({.lamp = reach.lamp, .weight = reach.weight});
                }
            }
        },
        nullptr);
    derive_wanted(meshes, wanted, light); // (building relief's made vertices: their sources' light)
    if (links != nullptr) {
        links->assign(meshes.size(), {});
        for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
            RelitMeshLinks& packed = core::at(*links, mi);
            const std::vector<std::vector<RelitLampLink>>& per_vertex = core::at(gathered, mi);
            if (per_vertex.empty()) {
                continue;
            }
            packed.first.reserve(per_vertex.size() + 1);
            for (const std::vector<RelitLampLink>& vertex_links : per_vertex) {
                packed.first.push_back(static_cast<uint32_t>(packed.links.size()));
                packed.links.insert(packed.links.end(), vertex_links.begin(), vertex_links.end());
            }
            packed.first.push_back(static_cast<uint32_t>(packed.links.size()));
        }
    }
}

LampShape lamp_shape(const RelitLamp& lamp)
{
    return {.position = lamp.position,
            .shape = lamp.shape,
            .axis = lamp.axis,
            .half_length = lamp.half_length,
            .core = lamp.core,
            .reach = lamp.reach};
}

namespace {

// One edge of the lamps' solve, per channel: sum_l I_l x coefficient_l = target, weighed by `weight`.
struct LampEdge {
    double target = 0.0;
    double weight = 0.0;
    uint32_t first = 0; // its coefficients in the solve's list
    uint32_t count = 0;
};
struct LampTerm {
    uint32_t lamp = 0;
    double value = 0.0;
    // (lighting round A3) its instrument: the edge's mean colour in place of each end's own (the same geometry). The
    // coefficient y1 G2 - y2 G1 is a difference of two near-equal products, so the colours' own noise (8-bit art) is a
    // large share of it where the lamp's light is most of the light - an indoor level's - and least squares on it shrinks
    // the intensity toward 0 (errors in the variables: a hall lit by its panel came out at 0.08 of it). The instrument
    // carries the same geometry with none of that noise in the difference: the solve takes sum z r / sum z v.
    double instrument = 0.0;
};

} // namespace

void relit_mesh_lamp_light(const RelitMesh& mesh, RelitVertices* light, const RelitMeshLinks& links,
                           std::span<const RelitLamp> lamps, std::span<const float> on)
{
    if (links.first.empty()) {
        return;
    }
    RelitVertices& vertices = *light;
    for (std::size_t vi = 0; vi < vertices.size() && vi + 1 < links.first.size(); ++vi) {
        Rgbf sum{};
        for (uint32_t k = core::at(links.first, vi); k < core::at(links.first, vi + 1); ++k) {
            const RelitLampLink& link = core::at(links.links, k);
            if (link.lamp >= lamps.size()) {
                continue;
            }
            const float share = link.lamp < on.size() ? core::at(on, link.lamp) : 1.0F;
            sum = plus(sum, scaled(core::at(lamps, link.lamp).light, link.weight * share));
        }
        core::at(vertices, vi).lamp = sum;
    }
    if (!mesh.sources.empty()) {
        relit_derive(mesh, &vertices); // (building relief's made vertices: their sources' light)
    }
}

void relit_lamp_light(std::span<const RelitMesh> meshes, std::span<RelitVertices> light, const RelitLampLinks& links,
                      std::span<const RelitLamp> lamps, std::span<const float> on)
{
    const std::size_t count = std::min({meshes.size(), light.size(), links.size()});
    for (std::size_t mi = 0; mi < count; ++mi) {
        relit_mesh_lamp_light(core::at(meshes, mi), &core::at(light, mi), core::at(links, mi), lamps, on);
    }
}

void relit_lamp_switches(std::span<const RelitVertices> light, const RelitLampLinks& links, std::span<RelitLamp> lamps,
                         double unit_lux)
{
    std::vector<double> weights(lamps.size(), 0.0);
    std::vector<double> sky(lamps.size(), 0.0);
    const std::size_t count = std::min(light.size(), links.size());
    for (std::size_t mi = 0; mi < count; ++mi) {
        const RelitVertices& vertices = core::at(light, mi);
        const RelitMeshLinks& mesh_links = core::at(links, mi);
        for (std::size_t vi = 0; vi < vertices.size() && vi + 1 < mesh_links.first.size(); ++vi) {
            const RelitVertex& vertex = core::at(vertices, vi);
            for (uint32_t k = core::at(mesh_links.first, vi); k < core::at(mesh_links.first, vi + 1); ++k) {
                const RelitLampLink& link = core::at(mesh_links.links, k);
                if (link.lamp >= lamps.size()) {
                    continue;
                }
                // (each surface it lights counts alike - the area it lights, not its own fixture next to it)
                core::at(weights, link.lamp) += 1.0;
                core::at(sky, link.lamp) += static_cast<double>(vertex.sky);
            }
        }
    }
    for (std::size_t li = 0; li < lamps.size(); ++li) {
        RelitLamp& lamp = core::at(lamps, li);
        const double weight = core::at(weights, li);
        if (!(weight > 0.0) || !(luma(lamp.light) > 0.0F)) {
            lamp.switch_sky = 0.0F; // (it lights nothing: not known - the open sky's rule)
            continue;
        }
        const double open = core::at(sky, li) / weight; // those surfaces' sky share
        lamp.switch_sky = open < static_cast<double>(kRelitIndoorSky) || !(unit_lux > 0.0)
                              ? kLampAlwaysOn
                              : static_cast<float>(static_cast<double>(kPhotocellLux) / unit_lux / open);
    }
}

namespace {

constexpr uint8_t kAllChannels = 7U; // (a lamp told in every channel: bits 0 - 2)

// A vertex's lamp links (RelitMeshLinks: `first` indexes `links`).
std::span<const RelitLampLink> links_of(const RelitLampLinks& links, std::size_t mi, uint32_t vi)
{
    const RelitMeshLinks& mesh_links = core::at(links, mi);
    if (vi + 1 >= mesh_links.first.size()) {
        return {};
    }
    const uint32_t from = core::at(mesh_links.first, vi);
    const uint32_t to = core::at(mesh_links.first, vi + 1);
    return std::span<const RelitLampLink>(mesh_links.links).subspan(from, to - from);
}

// The lamps' solve's equations in channel `ch`: one an edge of two lit vertices with lamps (one-colour meshes left out).
void lamp_edges(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light, const RelitLampLinks& links,
                const RelitModel& without, std::size_t ch, std::vector<LampEdge>* out_edges,
                std::vector<LampTerm>* out_terms)
{
    std::vector<LampEdge>& edges = *out_edges;
    std::vector<LampTerm>& terms = *out_terms;
    const std::size_t count = std::min({meshes.size(), light.size(), links.size()});
    for (std::size_t mi = 0; mi < count; ++mi) {
        const RelitMesh& mesh = core::at(meshes, mi);
        const RelitVertices& vertices = core::at(light, mi);
        if (core::at(links, mi).first.empty() || one_colour(mesh, vertices)) {
            continue;
        }
        const std::size_t vertex_count = std::min(vertices.size(), mesh.colours.size());
        for (const auto& [one, two, three] : mesh.triangles) {
            for (const auto& [iv, ja] : {std::pair{one, two}, std::pair{two, three}, std::pair{three, one}}) {
                if (iv >= ja || ja >= vertex_count) { // (each edge once)
                    continue;
                }
                const RelitVertex& first = core::at(vertices, iv);
                const RelitVertex& second = core::at(vertices, ja);
                const std::span<const RelitLampLink> li = links_of(links, mi, iv);
                const std::span<const RelitLampLink> lj = links_of(links, mi, ja);
                if (!first.used || !second.used || (li.empty() && lj.empty())) {
                    continue;
                }
                const Rgbf colour_one = colour_light(core::at(mesh.colours, iv));
                const Rgbf colour_two = colour_light(core::at(mesh.colours, ja));
                const double y1 = core::at(colour_one, ch);
                const double y2 = core::at(colour_two, ch);
                if (y1 < kBlackLight || y2 < kBlackLight) {
                    continue;
                }
                const Rgbf light_one = model_light(without, without.sun, first, without.toward);
                const Rgbf light_two = model_light(without, without.sun, second, without.toward);
                const double e1 = core::at(light_one, ch);
                const double e2 = core::at(light_two, ch);
                // y1 (e2 + sum I G2) = y2 (e1 + sum I G1)  ->  sum I (y1 G2 - y2 G1) = y2 e1 - y1 e2
                LampEdge edge;
                edge.first = static_cast<uint32_t>(terms.size());
                for (const RelitLampLink& link : lj) {
                    terms.push_back({.lamp = link.lamp,
                                     .value = y1 * static_cast<double>(link.weight),
                                     .instrument = 0.5 * (y1 + y2) * static_cast<double>(link.weight)});
                }
                for (const RelitLampLink& link : li) {
                    terms.push_back({.lamp = link.lamp,
                                     .value = -y2 * static_cast<double>(link.weight),
                                     .instrument = -0.5 * (y1 + y2) * static_cast<double>(link.weight)});
                }
                edge.count = static_cast<uint32_t>(terms.size()) - edge.first;
                edge.target = (y2 * e1) - (y1 * e2);
                const double whole = (y1 * e2) + (y2 * e1) + kSingular;
                edge.weight = 1.0 / (whole * whole);
                edges.push_back(edge);
            }
        }
    }
}

// The non-negative least squares over the lamps' columns: projected coordinate descent, the residuals kept; reweighted
// toward the median relative error (Cauchy) between rounds - an edge across a baked shadow or a lamp the list doesn't
// hold counts little. Each lamp's intensity; `told` set for the lamps with kRelitLampEdges edges or more.
std::vector<double> solve_lamp_columns(std::span<const LampEdge> edges, std::span<const LampTerm> terms,
                                       std::size_t lamp_count, std::vector<uint8_t>* told,
                                       std::span<const uint8_t> excluded)
{
    told->assign(lamp_count, 0);
    // each lamp's column: its edges and coefficients
    struct Entry {
        uint32_t edge = 0;
        double value = 0.0;
        double instrument = 0.0;
    };
    std::vector<std::vector<Entry>> columns(lamp_count);
    for (std::size_t ei = 0; ei < edges.size(); ++ei) {
        const LampEdge& edge = core::at(edges, ei);
        for (uint32_t k = 0; k < edge.count; ++k) {
            const LampTerm& term = core::at(terms, edge.first + k);
            core::at(columns, term.lamp).push_back(
                {.edge = static_cast<uint32_t>(ei), .value = term.value, .instrument = term.instrument});
        }
    }
    std::vector<double> intensity(lamp_count, 0.0);
    std::vector<double> residual(edges.size()); // target - sum I x coefficient
    std::vector<double> robust(edges.size(), 1.0);
    for (std::size_t ei = 0; ei < edges.size(); ++ei) {
        core::at(residual, ei) = core::at(edges, ei).target;
    }
    constexpr int kLampRounds = 4;
    constexpr int kSweeps = 40;
    for (int round = 0; round < kLampRounds; ++round) {
        for (int sweep = 0; sweep < kSweeps; ++sweep) {
            for (std::size_t li = 0; li < lamp_count; ++li) {
                const auto& column = core::at(columns, li);
                if (column.size() < kRelitLampEdges || (li < excluded.size() && core::at(excluded, li) != 0)) {
                    continue;
                }
                double numerator = 0.0;
                double denominator = 0.0;
                double plain_numerator = 0.0; // (least squares: where the instrument tells too little)
                double plain_denominator = 0.0;
                double instrument_power = 0.0;
                for (const Entry& entry : column) {
                    const double weight = core::at(edges, entry.edge).weight * core::at(robust, entry.edge);
                    numerator += weight * entry.instrument * core::at(residual, entry.edge);
                    denominator += weight * entry.instrument * entry.value;
                    plain_numerator += weight * entry.value * core::at(residual, entry.edge);
                    plain_denominator += weight * entry.value * entry.value;
                    instrument_power += weight * entry.instrument * entry.instrument;
                }
                // (the instrument used only where it goes with the coefficient - their correlation at least
                // kInstrumentLeast: a weak one makes the estimate blow up)
                constexpr double kInstrumentLeast = 0.5;
                if (!(denominator > kInstrumentLeast * std::sqrt(instrument_power * plain_denominator))) {
                    numerator = plain_numerator;
                    denominator = plain_denominator;
                }
                if (!(denominator > 0.0)) {
                    continue;
                }
                const double was = core::at(intensity, li);
                const double now = std::max(0.0, was + (numerator / denominator));
                if (now != was) {
                    for (const Entry& entry : column) {
                        core::at(residual, entry.edge) -= (now - was) * entry.value;
                    }
                    core::at(intensity, li) = now;
                }
            }
        }
        // the errors relative to each edge's light; their median sets the Cauchy scale
        std::vector<double> errors;
        errors.reserve(edges.size());
        for (std::size_t ei = 0; ei < edges.size(); ++ei) {
            errors.push_back(std::abs(core::at(residual, ei)) * std::sqrt(core::at(edges, ei).weight));
        }
        std::vector<double> sorted = errors;
        const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2);
        std::ranges::nth_element(sorted, middle);
        const double scale = std::max(*middle, kSingular) * 2.0;
        for (std::size_t ei = 0; ei < edges.size(); ++ei) {
            const double relative = core::at(errors, ei) / scale;
            core::at(robust, ei) = 1.0 / (1.0 + (relative * relative));
        }
    }
    for (std::size_t li = 0; li < lamp_count; ++li) {
        core::at(*told, li) = core::at(columns, li).size() >= kRelitLampEdges ? 1 : 0;
    }
    return intensity;
}

} // namespace

void solve_relit_lamps(std::span<const RelitMesh> meshes, std::span<RelitVertices> light, const RelitLampLinks& links,
                       std::span<RelitLamp> lamps, RelitModel* model, const BakeTime& bake_time,
                       std::vector<RelitLampTold>* told_out)
{
    // (round L) each lamp's verdict - what the bake knew of its light (no edges until a solve says more)
    std::vector<RelitLampTold> verdicts(lamps.size(), RelitLampTold::kNoEdges);
    struct Hand {
        std::vector<RelitLampTold>* out;
        std::vector<RelitLampTold>* verdicts;
        Hand(const Hand&) = delete;
        Hand& operator=(const Hand&) = delete;
        Hand(Hand&&) = delete;
        Hand& operator=(Hand&&) = delete;
        Hand(std::vector<RelitLampTold>* to, std::vector<RelitLampTold>* from) : out(to), verdicts(from) {}
        ~Hand()
        {
            if (out != nullptr) {
                *out = *verdicts;
            }
        }
    } hand(told_out, &verdicts);
    const double unit_lux = bake_time.unit_lux;
    model->lamps = {1.0F, 1.0F, 1.0F};
    // (a lamp the bake doesn't tell shines by its own surface: pi x the radiance its glow shows - its colour
    // display-referred, decoded (rt_display.h); the 8-bit data's range - a vertex colour's 2 x a pass's - is all the
    // radiance the original's art can hold: a real lamp is 10^2 - 10^4 x brighter, and only a bake with it lit tells
    // that)
    for (RelitLamp& lamp : lamps) {
        for (float& channel : lamp.light) {
            channel = kRelitLambertian * linear_from_display(channel);
        }
        lamp.solved = false;
    }
    // (round A3) a candidate - a bright surface or texture - whose light the bake doesn't show is no lamp
    const auto unconfirmed = [&lamps]() {
        for (RelitLamp& lamp : lamps) {
            if (!lamp.solved && candidate_kind(lamp.kind)) {
                lamp.light = {};
            }
        }
    };
    model->lamps_solved = 0;
    model->lamps_dark = 0;
    model->lamps_zero = 0;
    model->lamps_off_in_bake = 0;
    model->lamp_votes = {};
    model->lamp_median = {};
    if (lamps.empty()) {
        return;
    }
    if (!model->valid) {
        unconfirmed();
        relit_lamp_light(meshes, light, links, lamps);
        return;
    }
    RelitModel without = *model; // (the model's light without the lamps)
    without.lamps = {};
    // (round A3) twice at most: a candidate the first solve gives light but the bake doesn't confirm is left out of the
    // second - the light it took from the confirmed lamps given back to them
    std::vector<uint8_t> excluded(lamps.size(), 0);
    const std::vector<Rgbf> listed = [&lamps]() {
        std::vector<Rgbf> out;
        for (const RelitLamp& lamp : lamps) {
            out.push_back(lamp.light);
        }
        return out;
    }();
    for (int pass = 0; pass < 2; ++pass) {
    for (std::size_t li = 0; li < lamps.size(); ++li) {
        core::at(lamps, li).light = core::at(listed, li);
        core::at(lamps, li).solved = false;
    }
    model->lamps_solved = 0;
    model->lamps_dark = 0;
    model->lamps_zero = 0;
    model->lamps_off_in_bake = 0;
    model->lamps_implausible = 0;
    std::vector<Rgbf> solved(lamps.size(), Rgbf{});
    std::vector<uint8_t> told(lamps.size(), 0);
    for (std::size_t ch = 0; ch < 3; ++ch) {
        std::vector<LampEdge> edges;
        std::vector<LampTerm> terms;
        lamp_edges(meshes, light, links, without, ch, &edges, &terms);
        core::at(model->lamp_votes, ch) = edges.size();
        if (edges.size() < kRelitLampEdges) {
            continue;
        }
        std::vector<uint8_t> channel_told;
        const std::vector<double> intensity = solve_lamp_columns(edges, terms, lamps.size(), &channel_told, excluded);
        for (std::size_t li = 0; li < lamps.size(); ++li) {
            if (core::at(channel_told, li) != 0) {
                core::at(core::at(solved, li), ch) = static_cast<float>(core::at(intensity, li));
                core::at(told, li) |= static_cast<uint8_t>(1U << ch);
            }
        }
    }
    // (reported) the solved lamps against their own surfaces' light (pi x radiance): those the bake says are dark, the
    // median and the 95th percentile of the rest
    Rgbf median_ratio{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        std::vector<float> ratios;
        for (std::size_t li = 0; li < lamps.size(); ++li) {
            const float own = core::at(core::at(lamps, li).light, ch);
            if (core::at(told, li) == kAllChannels && own > kLeastLight && luma(core::at(solved, li)) > 0.0F) {
                ratios.push_back(core::at(core::at(solved, li), ch) / own);
            }
        }
        if (!ratios.empty()) {
            const auto middle = ratios.begin() + static_cast<std::ptrdiff_t>(ratios.size() / 2);
            std::ranges::nth_element(ratios, middle);
            core::at(median_ratio, ch) = *middle;
            constexpr double kHigh = 0.95;
            const auto high = ratios.begin() + static_cast<std::ptrdiff_t>(static_cast<double>(ratios.size() - 1) * kHigh);
            std::ranges::nth_element(ratios, high);
            core::at(model->lamp_high, ch) = *high;
        }
    }
    model->lamp_median = median_ratio; // (reported: the solved over their own surfaces' pi x radiance)
    // (round A3) each lamp's solved light against the light where it lights (the model's without the lamps, and the
    // lamps' own there): its share - told only above the fit's noise
    std::vector<double> part(lamps.size(), 0.0);
    std::vector<double> whole(lamps.size(), 0.0);
    // (and its own light and the sky share over the surfaces it lights, each alike - its switch in the bake's light)
    std::vector<double> reached(lamps.size(), 0.0);
    std::vector<double> own(lamps.size(), 0.0);
    std::vector<double> open(lamps.size(), 0.0);
    // (and the surface colours its light leaves where it is most of the light - the bake's colour over the light there -
    // against the darkest the level's surfaces no lamp reaches have: a solve run off along the flat valley of a lamp's
    // own pool (its size untold there) leaves them black)
    std::vector<std::array<std::vector<float>, 3>> implied(lamps.size()); // (per channel)
    std::array<std::vector<float>, 3> unlit;
    {
        const std::size_t count = std::min({meshes.size(), light.size(), links.size()});
        for (std::size_t mi = 0; mi < count; ++mi) {
            const RelitVertices& vertices = core::at(light, mi);
            for (std::size_t vi = 0; vi < vertices.size(); ++vi) {
                const std::span<const RelitLampLink> here = links_of(links, mi, static_cast<uint32_t>(vi));
                const RelitVertex& vertex = core::at(vertices, vi);
                const RelitMesh& mesh = core::at(meshes, mi);
                const Rgbf colour = vi < mesh.colours.size() ? colour_light(core::at(mesh.colours, vi)) : Rgbf{};
                const float shown = luma(colour);
                // (each channel's surface colour where that channel shows: one channel's run-off shows as that
                // channel black)
                const auto tints_into = [&colour](const Rgbf& light_there, std::array<std::vector<float>, 3>* into) {
                    for (std::size_t ch = 0; ch < 3; ++ch) {
                        if (core::at(colour, ch) >= kBlackLight && core::at(light_there, ch) > kLeastLight) {
                            core::at(*into, ch).push_back(core::at(colour, ch) / core::at(light_there, ch));
                        }
                    }
                };
                const Rgbf model_there = model_light(without, without.sun, vertex, without.toward);
                if (here.empty() && vertex.used) {
                    tints_into(model_there, &unlit);
                }
                if (here.empty() || !vertex.used) {
                    continue;
                }
                double lamps_here = 0.0;
                Rgbf light_there = model_there;
                for (const RelitLampLink& link : here) {
                    lamps_here += static_cast<double>(luma(core::at(solved, link.lamp))) * link.weight;
                    light_there = plus(light_there, scaled(core::at(solved, link.lamp), link.weight));
                }
                const double base =
                    static_cast<double>(luma(model_light(without, without.sun, vertex, without.toward))) + lamps_here;
                // (weighted by its geometry: where it lights most - not diluted by the far edge of its reach)
                for (const RelitLampLink& link : here) {
                    const double mine = static_cast<double>(luma(core::at(solved, link.lamp))) * link.weight;
                    if (shown > kBlackLight && base > 0.0 && mine >= 0.5 * base) {
                        tints_into(light_there, &core::at(implied, link.lamp));
                    }
                    const double weight = link.weight;
                    core::at(part, link.lamp) +=
                        weight * static_cast<double>(luma(core::at(solved, link.lamp))) * link.weight;
                    core::at(whole, link.lamp) += weight * base;
                    core::at(reached, link.lamp) += 1.0;
                    core::at(own, link.lamp) += static_cast<double>(luma(core::at(solved, link.lamp))) * link.weight;
                    core::at(open, link.lamp) += static_cast<double>(vertex.sky);
                }
            }
        }
    }
    // the fit's own noise: the median relative error of the edges no lamp reaches (a lamp-lit bake's error over every
    // edge counts the lamps' own light - untold yet - as error: New Orleans' 0.18)
    double noise = static_cast<double>(model->error);
    {
        std::vector<float> errors;
        const std::size_t count = std::min({meshes.size(), light.size(), links.size()});
        for (std::size_t mi = 0; mi < count; ++mi) {
            const RelitMesh& mesh = core::at(meshes, mi);
            const RelitVertices& vertices = core::at(light, mi);
            if (one_colour(mesh, vertices)) {
                continue;
            }
            const std::size_t vertex_count = std::min(vertices.size(), mesh.colours.size());
            const auto quiet = [&](uint32_t vi) {
                return vi < vertex_count && core::at(vertices, vi).used && links_of(links, mi, vi).empty() &&
                       luma(colour_light(core::at(mesh.colours, vi))) >= kBlackLight;
            };
            for (const auto& [one, two, three] : mesh.triangles) {
                for (const auto& [iv, ja] : {std::pair{one, two}, std::pair{two, three}, std::pair{three, one}}) {
                    if (iv >= ja || !quiet(iv) || !quiet(ja)) {
                        continue;
                    }
                    const float yi = luma(colour_light(core::at(mesh.colours, iv)));
                    const float yj = luma(colour_light(core::at(mesh.colours, ja)));
                    const float ei = luma(model_light(without, without.sun, core::at(vertices, iv), without.toward));
                    const float ej = luma(model_light(without, without.sun, core::at(vertices, ja), without.toward));
                    const float sum = (yi * ej) + (yj * ei);
                    if (sum > kLeastLight) {
                        errors.push_back(std::abs((yi * ej) - (yj * ei)) / sum);
                    }
                }
            }
        }
        if (errors.size() >= kRelitLampEdges) {
            const auto middle = errors.begin() + static_cast<std::ptrdiff_t>(errors.size() / 2);
            std::ranges::nth_element(errors, middle);
            noise = *middle;
        }
    }
    noise = std::max(noise, kSingular);
    model->lamp_noise = static_cast<float>(noise);
    Rgbf darkest{}; // (the unlit surfaces' 1st percentile, per channel)
    for (std::size_t ch = 0; ch < 3; ++ch) {
        std::vector<float>& values = core::at(unlit, ch);
        if (!values.empty()) {
            constexpr double kDarkest = 0.01;
            const auto at = values.begin() + static_cast<std::ptrdiff_t>(static_cast<double>(values.size() - 1) * kDarkest);
            std::ranges::nth_element(values, at);
            core::at(darkest, ch) = *at;
        }
    }
    for (std::size_t li = 0; li < lamps.size(); ++li) {
        RelitLamp& lamp = core::at(lamps, li);
        const bool told_all = core::at(told, li) == kAllChannels;
        const double share = core::at(whole, li) > 0.0 ? core::at(part, li) / core::at(whole, li) : 0.0;
        // (lighting round A3) and a candidate - brightness alone, no artist's mark - on in the bake by its own switch
        // (rt_lights.h lamp_on: the photocell's rule the game switches it by) against the bake's own sky light: one
        // outdoors whose solved light is under the daylight where it lights would have been off - that light is the
        // fit's residue (a sunlit level's), not a lamp's
        const double reach = core::at(reached, li);
        const double sky_share = reach > 0.0 ? core::at(open, li) / reach : 0.0;
        const float switch_sky = !(reach > 0.0) || !(unit_lux > 0.0)       ? 0.0F
                                 : sky_share < static_cast<double>(kRelitIndoorSky) ? kLampAlwaysOn
                                                                         : static_cast<float>(static_cast<double>(kPhotocellLux) / unit_lux / sky_share);
        // (at the bake's own time: its physical open sky and the open sky's lamps - the game's switch then)
        const bool on_in_bake = !candidate_kind(lamp.kind) || !(unit_lux > 0.0) ||
                                lamp_on(switch_sky, bake_time.sky, bake_time.lamps) >= 0.5F;
        bool plausible = true;
        for (std::size_t ch = 0; ch < 3; ++ch) {
            if (std::vector<float>& tints = core::at(core::at(implied, li), ch); !tints.empty()) {
                const auto middle = tints.begin() + static_cast<std::ptrdiff_t>(tints.size() / 2);
                std::ranges::nth_element(tints, middle);
                plausible = plausible && *middle >= core::at(darkest, ch);
            }
        }
        {
            // (and a light's colour: a lamp's - its weakest channel at least kTexelLeastShare of its brightest, a
            // blackbody's run or a white; a solve run off in one channel isn't)
            const Rgbf& colour = core::at(solved, li);
            const float most = std::max({std::get<0>(colour), std::get<1>(colour), std::get<2>(colour)});
            const float least = std::min({std::get<0>(colour), std::get<1>(colour), std::get<2>(colour)});
            plausible = plausible && (!(most > 0.0F) || least >= kTexelLeastShare * most);
        }
        model->lamps_implausible += told_all && share >= noise && on_in_bake && !plausible ? 1 : 0;
        lamp.solved = told_all && share >= noise && on_in_bake && plausible;
        {
            // (round L) the verdict: off at the bake's time by its own switch - any kind, a glow too (the street lamp
            // the afternoon bake can't see) -, no edges, a run-off, or dark
            const bool off = unit_lux > 0.0 && lamp_on(switch_sky, bake_time.sky, bake_time.lamps) < 0.5F;
            core::at(verdicts, li) = lamp.solved                          ? RelitLampTold::kSolved
                                     : !told_all                           ? RelitLampTold::kNoEdges
                                     : off                                 ? RelitLampTold::kOffInBake
                                     : share >= noise && !plausible        ? RelitLampTold::kRunOff
                                                                           : RelitLampTold::kDark;
        }
        model->lamps_off_in_bake += told_all && share >= noise && !on_in_bake ? 1 : 0;
        model->lamps_dark += told_all && !lamp.solved ? 1 : 0;
        model->lamps_zero += told_all && !lamp.solved && !(luma(core::at(solved, li)) > 0.0F) ? 1 : 0;
        model->lamps_solved += lamp.solved ? 1 : 0;
        if (lamp.solved) {
            lamp.light = core::at(solved, li);
        }
    }
    bool again = false;
    for (std::size_t li = 0; li < lamps.size(); ++li) {
        if (!core::at(lamps, li).solved && candidate_kind(core::at(lamps, li).kind) && core::at(excluded, li) == 0 &&
            luma(core::at(solved, li)) > 0.0F) {
            core::at(excluded, li) = 1;
            again = true;
        }
    }
    if (!again) {
        break;
    }
    }
    unconfirmed();
    relit_lamp_light(meshes, light, links, lamps);
}

void relit_to_unit(RelitModel* model, std::span<RelitVertices> light, std::span<RelitLamp> lamps)
{
    // (per channel: the balance holds in each - the fit's per-channel scales, its tints' medians 1, put right too)
    Rgbf scale{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const float coefficient = core::at(model->bounce, ch);
        core::at(scale, ch) = std::isfinite(coefficient) && coefficient > 0.0F ? coefficient : 1.0F;
    }
    for (RelitVertices& vertices : light) {
        for (RelitVertex& vertex : vertices) {
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(vertex.tint, ch) =
                    std::clamp(core::at(vertex.tint, ch) * core::at(scale, ch), 0.0F, kRelitMaxTint);
                core::at(vertex.lamp, ch) /= core::at(scale, ch);
            }
        }
    }
    for (std::size_t ch = 0; ch < 3; ++ch) {
        for (RelitLamp& lamp : lamps) {
            if (lamp.solved) {
                core::at(lamp.light, ch) /= core::at(scale, ch);
            }
        }
        core::at(model->sun, ch) /= core::at(scale, ch);
        core::at(model->sky, ch) /= core::at(scale, ch);
    }
    model->bounce = {1.0F, 1.0F, 1.0F};
    model->lamps = {1.0F, 1.0F, 1.0F};
    model->albedo_scale = luma(scale);
}

void relit_lamp_photometry(RelitLamp* lamp, double unit_lux)
{
    constexpr double kMetresPerUnit = 1.0 / 39.37; // (the world's units are inches)
    constexpr double kPiD = 3.14159265358979323846;
    const double radius = static_cast<double>(lamp->core) * kMetresPerUnit;
    const double y = (0.2126 * static_cast<double>(std::get<0>(lamp->light))) +
                     (0.7152 * static_cast<double>(std::get<1>(lamp->light))) +
                     (0.0722 * static_cast<double>(std::get<2>(lamp->light)));
    // the light right at it (lux) over its core's area: E = I r^2 / (r^2 + d^2) -> I r^2 far off
    double candela = y * unit_lux * radius * radius;
    double lumens = 4.0 * kPiD * candela; // a sphere
    if (lamp->shape == EmitterShape::kPanel) {
        lumens = 2.0 * kPiD * candela; // two Lambertian sides
    } else if (lamp->shape == EmitterShape::kLine) {
        // a line: E -> I core h / d^2 far off, broadside; a Lambertian cylinder sends pi^2 x that
        candela = y * unit_lux * radius * static_cast<double>(lamp->half_length) * kMetresPerUnit;
        lumens = kPiD * kPiD * candela;
    }
    lamp->candela = static_cast<float>(candela);
    lamp->lumens = static_cast<float>(lumens);
    // the colour temperature: linear sRGB -> XYZ -> xy, McCamy's cubic (meaningful from about 2000 to 12500 K)
    const double r = std::get<0>(lamp->light);
    const double g = std::get<1>(lamp->light);
    const double b = std::get<2>(lamp->light);
    const double big_x = (0.4124 * r) + (0.3576 * g) + (0.1805 * b);
    const double big_y = (0.2126 * r) + (0.7152 * g) + (0.0722 * b);
    const double big_z = (0.0193 * r) + (0.1192 * g) + (0.9505 * b);
    const double sum = big_x + big_y + big_z;
    lamp->kelvin = 0.0F;
    if (sum > 1e-9) {
        const double x = big_x / sum;
        const double yy = big_y / sum;
        const double slope = (x - 0.3320) / (0.1858 - yy);
        const double cct = (449.0 * slope * slope * slope) + (3525.0 * slope * slope) + (6823.3 * slope) + 5520.33;
        constexpr double kLeastKelvin = 1000.0;
        constexpr double kMostKelvin = 25000.0;
        lamp->kelvin = cct >= kLeastKelvin && cct <= kMostKelvin ? static_cast<float>(cct) : 0.0F;
    }
}

const char* relit_lamp_source_name(RelitLampSource source)
{
    switch (source) {
    case RelitLampSource::kNone:
        return "none";
    case RelitLampSource::kSolved:
        return "solved";
    case RelitLampSource::kModel:
        return "model";
    case RelitLampSource::kImplausible:
        return "implausible solve - model";
    case RelitLampSource::kOwnSurface:
        return "own surface";
    }
    return "?";
}

float relit_lamp_intensity_for(const RelitLamp& lamp, double lumens, double unit_lux)
{
    constexpr double kMetresPerUnit = 1.0 / 39.37; // (the world's units are inches)
    const double radius = static_cast<double>(lamp.core) * kMetresPerUnit;
    if (!(radius > 0.0) || !(unit_lux > 0.0) || !(lumens > 0.0)) {
        return 0.0F;
    }
    // (relit_lamp_photometry's sums, the other way: lumens -> candela -> the light right at it in the unit)
    if (lamp.shape == EmitterShape::kLine) {
        const double half = static_cast<double>(lamp.half_length) * kMetresPerUnit;
        const double candela = lumens / (kPi * kPi);
        return half > 0.0 ? static_cast<float>(candela / (unit_lux * radius * half)) : 0.0F;
    }
    const double candela = lumens / ((lamp.shape == EmitterShape::kPanel ? 2.0 : 4.0) * kPi);
    return static_cast<float>(candela / (unit_lux * radius * radius));
}

namespace {

Rgbf hue_of(const Rgbf& colour)
{
    // (the photometric luminance, Rec. 709 - relit_lamp_photometry's - not the Rec. 601 luma the fit uses)
    const float y = (0.2126F * std::get<0>(colour)) + (0.7152F * std::get<1>(colour)) + (0.0722F * std::get<2>(colour));
    return y > 0.0F ? scaled(colour, 1.0F / y) : Rgbf{1.0F, 1.0F, 1.0F};
}

} // namespace

LampModelCounts relit_lamp_models(std::span<RelitLamp> lamps, std::span<const RelitLampTold> told,
                                  std::span<const float> areas, std::span<const Rgbf> own_colours, double unit_lux)
{
    LampModelCounts counts;
    for (std::size_t li = 0; li < lamps.size(); ++li) {
        RelitLamp& lamp = core::at(lamps, li);
        relit_lamp_photometry(&lamp, unit_lux);
        const RelitLampTold verdict = li < told.size() ? core::at(told, li) : RelitLampTold::kNoEdges;
        const float area = li < areas.size() ? core::at(areas, li) : 0.0F;
        const Rgbf own = li < own_colours.size() ? core::at(own_colours, li) : lamp.light;
        const bool indoors = lamp.switch_sky >= 0.5F * kLampAlwaysOn;
        const Rgbf evidence_colour = lamp.solved && luma(lamp.light) > 0.0F ? lamp.light : own;
        const LampFixture fixture =
            lamp.kind == EmitterKind::kPoint
                ? LampFixture::kUnknown
                : fixture_of({.shape = static_cast<uint8_t>(lamp.shape),
                              .axis = {x_of(lamp.axis), y_of(lamp.axis), z_of(lamp.axis)},
                              .colour = evidence_colour,
                              .indoors = indoors});
        const FixtureValues& values = fixture_values(fixture);
        lamp.fixture = static_cast<uint8_t>(fixture);
        lamp.area = area;
        RelitLampSource source = RelitLampSource::kNone;
        bool model = false;
        const bool candidate = candidate_kind(lamp.kind);
        if (lamp.kind == EmitterKind::kPoint) {
            source = luma(lamp.light) > 0.0F ? RelitLampSource::kOwnSurface : RelitLampSource::kNone;
        } else if (verdict == RelitLampTold::kSolved) {
            source = RelitLampSource::kSolved;
            const double luminance = area > 0.0F ? static_cast<double>(lamp.lumens) / (kPi * static_cast<double>(area)) : 0.0;
            lamp.luminance = static_cast<float>(luminance);
            if (area > 0.0F && (luminance < static_cast<double>(values.least) || luminance > static_cast<double>(values.most))) {
                source = RelitLampSource::kImplausible; // (a failed measurement: the model, not a clamp)
                model = true;
            }
        } else if (!candidate) {
            // a glow (the artists' mark of a lamp): the model where the bake can't tell it - no edges, off in the bake by
            // its own switch, a run-off; told dark while on: its own surface (A3's lower bound)
            if (area > 0.0F && verdict != RelitLampTold::kDark) {
                source = verdict == RelitLampTold::kRunOff ? RelitLampSource::kImplausible : RelitLampSource::kModel;
                model = true;
            } else {
                source = luma(lamp.light) > 0.0F ? RelitLampSource::kOwnSurface : RelitLampSource::kNone;
            }
        } else if (verdict == RelitLampTold::kOffInBake && !indoors && area > 0.0F && fixture != LampFixture::kSign &&
                   fixture != LampFixture::kNeon) {
            // a candidate the bake couldn't see lit - off by its own switch then - shaped like a luminaire outdoors
            source = RelitLampSource::kModel;
            model = true;
        } else {
            lamp.light = {}; // (a bright surface the bake doesn't confirm: no lamp - A3's rule)
        }
        if (model) {
            // Phi = pi L A, in its colour: a sign's or a neon's own, a light's own where it is one, else its kind's
            const double lumens = kPi * static_cast<double>(values.luminance) * static_cast<double>(area);
            const bool own_light = colour_kelvin(own) > 0.0F && !saturated_colour(own) && luma(own) > 0.0F;
            const Rgbf hue = values.kelvin <= 0.0F || own_light ? hue_of(own) : blackbody_colour(values.kelvin);
            lamp.light = scaled(hue, relit_lamp_intensity_for(lamp, lumens, unit_lux));
            lamp.luminance = values.luminance;
            lamp.solved = false;
        }
        lamp.source = static_cast<uint8_t>(source);
        relit_lamp_photometry(&lamp, unit_lux);
        core::at(counts.by_source, static_cast<std::size_t>(source)) += 1;
        core::at(counts.by_fixture, static_cast<std::size_t>(fixture)) += luma(lamp.light) > 0.0F ? 1 : 0;
    }
    return counts;
}

RelitLighting relit_own_lighting(const RelitModel& model, float lamps)
{
    return {.toward = model.toward, .sun = model.sunless ? Rgbf{} : model.sun, .sky = model.sky, .lamps = lamps};
}

RelitParts relit_parts(const RelitModel& /*model*/, const RelitVertex& vertex, const RelitLighting& lighting)
{
    const Vec3 toward = normalised(lighting.toward);
    const float nl = std::max(0.0F, dot(vertex.normal, toward));
    RelitParts out;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        // the direct light, as much of it as the vertex sees; the sky's by its share K; the surroundings' radiance B
        core::at(out.direct, ch) = core::at(lighting.sun, ch) * nl * vertex.sun;
        core::at(out.indirect, ch) = (core::at(lighting.sky, ch) * vertex.sky) + core::at(vertex.bounce, ch);
        core::at(out.lamps, ch) = core::at(vertex.lamp, ch) * lighting.lamps;
    }
    return out;
}

Rgbf relit_light(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting)
{
    const RelitParts parts = relit_parts(model, vertex, lighting);
    Rgbf out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) = core::at(parts.direct, ch) + core::at(parts.indirect, ch) + core::at(parts.lamps, ch);
    }
    return out;
}

Rgbf relit_reflected(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting)
{
    const Rgbf en = relit_light(model, vertex, lighting);
    Rgbf out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        const float value = core::at(vertex.tint, ch) * core::at(en, ch);
        core::at(out, ch) = std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
    }
    return out;
}

Rgbf relit_shown(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting)
{
    Rgbf out = relit_reflected(model, vertex, lighting);
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        const float emit = core::at(vertex.emit, ch);
        core::at(out, ch) += std::isfinite(emit) ? std::max(emit, 0.0F) : 0.0F;
    }
    return out;
}

uint32_t relit_colour(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting)
{
    // (round L: the light as it is - no display encoding, no hold at 2, no pre-exposure: light_store.h)
    return store_light(relit_shown(model, vertex, lighting));
}
} // namespace render::rt

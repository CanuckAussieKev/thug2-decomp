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
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
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
constexpr float kMinSunLight = 0.05F;        // the fit's sun (luma) at least this: the bake shows a sun
constexpr float kBlackLight = 1.0F / 128.0F; // a colour this dark (luma) isn't fitted: unlit or painted black
constexpr int kFitRounds = 4;
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
    const Rgbf ca = colour_light(core::at(mesh.colours, one));
    const Rgbf cb = colour_light(core::at(mesh.colours, two));
    const Rgbf cc = colour_light(core::at(mesh.colours, three));
    Rgbf out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) = (core::at(ca, ch) * u) + (core::at(cb, ch) * vi) + (core::at(cc, ch) * w);
    }
    return out;
}

// The vertices of the wanted meshes in batches of whole vertices with at most kBatchRays rays: `rays_of` says a
// vertex's count, `make` writes them, `take` reads their answers.
struct VertexRef {
    std::size_t mesh = 0;
    std::size_t vertex = 0;
    uint32_t first = 0; // its first ray in the batch
    uint32_t count = 0;
};

template <typename RaysOf, typename Make, typename Take>
void trace_in_batches(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, RayTracing& tracer,
                      RelitStats* stats, RaysOf rays_of, Make make, Take take)
{
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
        const std::size_t vertices = core::at(meshes, mi).positions.size();
        for (std::size_t vi = 0; vi < vertices; ++vi) {
            const uint32_t count = rays_of(mi, vi);
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
                            (core::at(model.bounce, ch) * core::at(vertex.bounce, ch));
    }
    return out;
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
// (beta . d)^2 with beta at least 0 and summing to 1 (the channel's scale isn't in the condition), reweighted
// kFitRounds - 1 times toward the median residual (an edge across a change of colour - a painted stripe, a decal's
// border - counts little). Also its objective (the last round's weights) and the least one without the sun's
// coefficient (with nearly as small an objective, the bake's light needs no sun: an indoor level), and the median
// residual (relative).
struct PairFit {
    std::array<double, 3> beta{};
    double objective = 0.0;
    double without_sun = 0.0;
    double spread = 0.0;
};
PairFit pair_direction(std::span<const std::array<float, 3>> differences)
{
    PairFit fit;
    std::vector<double> weights(differences.size(), 1.0);
    std::array<double, 3> beta{kThird, kThird, kThird};
    for (int round = 0; round < kFitRounds; ++round) {
        std::array<std::array<double, 3>, 3> mi{};
        for (std::size_t idx = 0; idx < differences.size(); ++idx) {
            const std::array<float, 3>& dv = core::at(differences, idx);
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
            double objective = 0.0;
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    objective += core::at(candidate, row) * core::at(core::at(mi, row), ch) * core::at(candidate, ch);
                }
            }
            if (best < 0.0 || objective < best) {
                best = objective;
                beta = candidate;
            }
            if ((subset & 1U) == 0U && (best_without_sun < 0.0 || objective < best_without_sun)) {
                best_without_sun = objective;
            }
        }
        std::vector<double> residuals(differences.size());
        for (std::size_t idx = 0; idx < differences.size(); ++idx) {
            const std::array<float, 3>& dv = core::at(differences, idx);
            core::at(residuals, idx) =
                std::abs((core::at(beta, 0) * core::at(dv, 0)) + (core::at(beta, 1) * core::at(dv, 1)) +
                         (core::at(beta, 2) * core::at(dv, 2)));
        }
        std::vector<double> sorted = residuals;
        const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2);
        std::ranges::nth_element(sorted, middle);
        const double floor = std::max(*middle, kSingular);
        fit.spread = *middle;
        fit.objective = std::max(best, 0.0);
        fit.without_sun = std::max(best_without_sun, 0.0);
        for (std::size_t idx = 0; idx < differences.size(); ++idx) {
            core::at(weights, idx) = 1.0 / std::max(core::at(residuals, idx), floor);
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
        } else if (key == "strength" && scalar.is_number) {
            level->strength = std::clamp(static_cast<float>(scalar.number), 0.0F, 1.0F);
        } else if (key == "sun" && scalar.is_text) {
            level->keeps_baked_sun = scalar.text == "level";
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
    kept.strength = std::isfinite(kept.strength) ? std::clamp(kept.strength, 0.0F, 1.0F) : 1.0F;
    kept.detail = std::isfinite(kept.detail) ? std::clamp(kept.detail, 0.0F, 1.0F) : 1.0F;
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

Rgbf colour_light(uint32_t argb)
{
    return {static_cast<float>((argb >> kRedShift) & kByteMask) * kRelitPerByte,
            static_cast<float>((argb >> kGreenShift) & kByteMask) * kRelitPerByte,
            static_cast<float>(argb & kByteMask) * kRelitPerByte};
}

void relit_normals(const RelitMesh& mesh, float facing, RelitVertices* out)
{
    const std::size_t count = mesh.positions.size();
    out->assign(count, RelitVertex{});
    std::vector<Vec3> sums(count, Vec3{});
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
        for (const uint32_t corner : {one, two, three}) {
            core::at(sums, corner) = plus(core::at(sums, corner), face); // (its length twice the area: area-weighted)
            core::at(*out, corner).used = true;
        }
    }
    const bool given = mesh.normals.size() == count;
    for (std::size_t vi = 0; vi < count; ++vi) {
        core::at(*out, vi).normal = normalised(given ? core::at(mesh.normals, vi) : core::at(sums, vi));
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
                     RayTracing& tracer, std::vector<RelitVertices>* light, RelitStats* stats)
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
                plus(core::at(core::at(meshes, ref.mesh).positions, ref.vertex), scaled(vertex.normal, kRelitOffset));
            const std::pair<Vec3, Vec3> basis = basis_of(vertex.normal);
            const double turn =
                static_cast<double>(hashed(static_cast<uint32_t>((ref.mesh * kMeshSalt) + ref.vertex))) * kHashScale;
            for (uint32_t idx = 0; idx < sky_rays; ++idx) {
                requests->push_back({.origin = origin,
                                     .direction = hemisphere_direction(vertex.normal, basis,
                                                                       {.index = idx, .count = sky_rays, .turn = turn}),
                                     .max_distance = kRelitRayLength,
                                     .cull_mask = kRelitWorldMask});
            }
        },
        [&](const VertexRef& ref, std::span<const ProbeResult> answers) {
            RelitVertex& vertex = core::at(core::at(*light, ref.mesh), ref.vertex);
            if (answers.size() != ref.count) {
                return;
            }
            const Vec3 origin =
                plus(core::at(core::at(meshes, ref.mesh).positions, ref.vertex), scaled(vertex.normal, kRelitOffset));
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
                const Rgbf there = colour_at(core::at(meshes, found->second), answer.primitive,
                                             plus(origin, scaled(direction, answer.distance)));
                for (std::size_t ch = 0; ch < bounce.size(); ++ch) {
                    core::at(bounce, ch) += core::at(there, ch);
                }
            }
            vertex.sky = static_cast<float>(open) / static_cast<float>(ref.count);
            for (float& ch : bounce) {
                ch /= static_cast<float>(ref.count);
            }
            vertex.bounce = bounce;
        });
}

void trace_relit_sun(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const Vec3& toward,
                     const RelitParams& params, RayTracing& tracer, std::vector<RelitVertices>* light,
                     RelitStats* stats)
{
    if (light->size() != meshes.size()) {
        return;
    }
    const Vec3 sun = normalised(toward);
    const std::pair<Vec3, Vec3> sides = basis_of(sun);
    const uint32_t sun_rays = std::max(1U, params.sun_rays);
    const double radius = static_cast<double>(params.sun_radius_degrees) * kRadiansPerDegree;
    for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
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
                plus(core::at(core::at(meshes, ref.mesh).positions, ref.vertex), scaled(vertex.normal, kRelitOffset));
            const double turn =
                static_cast<double>(hashed(static_cast<uint32_t>((ref.mesh * kMeshSalt) + ref.vertex) ^ kSunSalt)) *
                kHashScale;
            for (uint32_t idx = 0; idx < sun_rays; ++idx) {
                requests->push_back(
                    {.origin = origin,
                     .direction = sun_direction(sun, sides, {.index = idx, .count = sun_rays, .turn = turn}, radius),
                     .max_distance = kRelitRayLength,
                     .cull_mask = kRelitWorldMask});
            }
        },
        [&](const VertexRef& ref, std::span<const ProbeResult> answers) {
            if (answers.size() != ref.count) {
                return;
            }
            const auto open = std::ranges::count_if(answers, [](const ProbeResult& answer) { return !answer.hit; });
            core::at(core::at(*light, ref.mesh), ref.vertex).sun =
                static_cast<float>(open) / static_cast<float>(ref.count);
        });
}

RelitModel fit_relit_model(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light, const Vec3& toward)
{
    RelitModel model;
    model.toward = normalised(toward);
    const std::size_t count = std::min(meshes.size(), light.size());
    // each mesh edge's difference (pair_direction), per channel; the vertices' terms and colours for the scale
    std::array<std::vector<std::array<float, 3>>, 3> differences;
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
        for (const auto& [one, two, three] : mesh.triangles) {
            for (const auto& [iv, ja] : {std::pair{one, two}, std::pair{two, three}, std::pair{three, one}}) {
                if (iv == ja || !lit(iv) || !lit(ja)) {
                    continue;
                }
                const Rgbf yi = colour_light(core::at(mesh.colours, iv));
                const Rgbf yj = colour_light(core::at(mesh.colours, ja));
                for (std::size_t ch = 0; ch < differences.size(); ++ch) {
                    const std::array<float, 3> xi = x_of_vertex(iv, ch);
                    const std::array<float, 3> xj = x_of_vertex(ja, ch);
                    const float over = 1.0F / std::max(core::at(yi, ch) + core::at(yj, ch), kLeastLight);
                    std::array<float, 3> dv{};
                    for (std::size_t idx = 0; idx < dv.size(); ++idx) {
                        core::at(dv, idx) =
                            ((core::at(yi, ch) * core::at(xj, idx)) - (core::at(yj, ch) * core::at(xi, idx))) * over;
                    }
                    core::at(differences, ch).push_back(dv);
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
    // (sun_needed is only reported: a bake with no sun is told by rt_sun.h's fit, before this one runs)
    model.valid = luma(model.sun) >= kMinSunLight;
    return model;
}

void decompose_relit(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitParams& params,
                     RelitModel* model, std::vector<RelitVertices>* light, bool rescale)
{
    if (!model->valid) {
        return;
    }
    const std::size_t count = std::min(meshes.size(), light->size());
    const LightScale scale =
        light_scale(meshes.first(count), std::span<const RelitVertices>(*light).first(count), *model);
    double baked_sum = 0.0;
    double relit_sum = 0.0;
    for (std::size_t mi = 0; mi < count; ++mi) {
        if (!is_wanted(wanted, mi)) {
            continue;
        }
        const RelitMesh& mesh = core::at(meshes, mi);
        RelitVertices& vertices = core::at(*light, mi);
        const std::size_t vertex_count = std::min(vertices.size(), mesh.colours.size());
        std::vector<Rgbf> lights(vertex_count);
        for (std::size_t vi = 0; vi < vertex_count; ++vi) {
            core::at(lights, vi) = model_light(*model, model->sun, core::at(vertices, vi), model->toward);
        }
        // the neighbourhoods' tints, then again without the vertices brighter than theirs predicts (lit by lamps: their
        // own tints hold the lamps' light, which mustn't spread)
        // (kLampRounds times: a wide pool of lamp light raises its neighbourhood's tint, so its edges show first)
        std::vector<uint8_t> lamp_lit(vertex_count, 0);
        TintField field = local_tints(mesh, vertices, *model, scale, lamp_lit);
        for (int round = 0; round < kLampRounds; ++round) {
            bool more = false;
            for (std::size_t vi = 0; vi < vertex_count; ++vi) {
                const Rgbf predicted = times(exp_of(core::at(field.nearby, vi)), core::at(lights, vi));
                const Rgbf y = colour_light(core::at(mesh.colours, vi));
                if (core::at(lamp_lit, vi) == 0 && luma(y) > kRelitLampLow * luma(predicted)) {
                    core::at(lamp_lit, vi) = 1;
                    more = true;
                }
            }
            if (!more) {
                break;
            }
            field = local_tints(mesh, vertices, *model, scale, lamp_lit);
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
            const float ratio = luma(y) / std::max(luma(near_light), kLeastLight);
            const float lamp = smoothstep(kRelitLampLow, kRelitLampHigh, ratio);
            // the lamp's share of the light over what the neighbourhood's tint explains is the residual; the rest
            // gives the vertex's own tint, kept by `detail` x its confidence (the neighbourhood's otherwise)
            const float keep = core::at(field.spread, vi) != 0 ? params.detail * core::at(field.confidence, vi) : 1.0F;
            Rgbf predicted{};
            for (std::size_t ch = 0; ch < 3; ++ch) {
                const float lamp_light = lamp * std::max(0.0F, core::at(y, ch) - core::at(near_light, ch));
                const float rest = std::max(core::at(y, ch) - lamp_light, 0.0F);
                const float own =
                    std::log(std::max(rest / std::max(core::at(en, ch), core::at(scale.floor, ch)), kLeastRatio));
                const float log_tint = core::at(nearby, ch) + ((own - core::at(nearby, ch)) * keep);
                core::at(vertex.tint, ch) = std::clamp(std::exp(log_tint), 0.0F, kRelitMaxTint);
                core::at(predicted, ch) = core::at(vertex.tint, ch) * core::at(en, ch);
                // (and what a clamped tint can't carry: light the model has no source for)
                core::at(vertex.residual, ch) = lamp_light + (core::at(vertex.tint, ch) >= kRelitMaxTint
                                                                  ? std::max(0.0F, rest - core::at(predicted, ch))
                                                                  : 0.0F);
            }
            baked_sum += luma(y);
            relit_sum += luma(predicted) + luma(vertex.residual);
        }
    }
    if (rescale) {
        model->scale = relit_sum > 0.0 ? std::clamp(static_cast<float>(baked_sum / relit_sum),
                                                    core::at(kRelitScaleRange, 0), core::at(kRelitScaleRange, 1))
                                       : 1.0F;
    }
}

Rgbf relit_light(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting)
{
    Rgbf pa{};
    for (std::size_t ch = 0; ch < pa.size(); ++ch) {
        core::at(pa, ch) = core::at(model.sun, ch) * core::at(lighting.sun_colour, ch);
    }
    constexpr Rgbf kOnes{1.0F, 1.0F, 1.0F};
    if (lighting.direct_scale == kOnes && lighting.sky_scale == kOnes && lighting.bounce_scale == kOnes) {
        return model_light(model, pa, vertex, normalised(lighting.toward));
    }
    // (added by the time of day) each term at the clock's strength
    RelitModel scaled = model;
    for (std::size_t ch = 0; ch < pa.size(); ++ch) {
        core::at(pa, ch) *= core::at(lighting.direct_scale, ch);
        core::at(scaled.sky, ch) *= core::at(lighting.sky_scale, ch);
        core::at(scaled.bounce, ch) *= core::at(lighting.bounce_scale, ch);
    }
    return model_light(scaled, pa, vertex, normalised(lighting.toward));
}

uint32_t relit_colour(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting,
                      uint32_t original)
{
    if (!model.valid || !vertex.used) {
        return original;
    }
    const Rgbf baked = colour_light(original);
    const Rgbf en = relit_light(model, vertex, lighting);
    std::array<uint32_t, 3> bytes{};
    for (std::size_t ch = 0; ch < bytes.size(); ++ch) {
        const float relit = (model.scale * core::at(vertex.tint, ch) * core::at(en, ch)) +
                            (lighting.residual ? core::at(vertex.residual, ch) : 0.0F);
        float value = core::at(baked, ch) + ((relit - core::at(baked, ch)) * lighting.strength);
        value = std::isfinite(value) ? std::clamp(value, 0.0F, kRelitMaxValue) : core::at(baked, ch);
        core::at(bytes, ch) = static_cast<uint32_t>(std::lround(std::min(value * kBytesPerLight, kMaxByte)));
    }
    return (original & (kByteMask << kAlphaShift)) | (std::get<0>(bytes) << kRedShift) |
           (std::get<1>(bytes) << kGreenShift) | std::get<2>(bytes);
}

} // namespace render::rt

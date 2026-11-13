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
constexpr float kLumaRed = 0.299F; // (Rec. 601, as the baked sun's samples)
constexpr float kLumaGreen = 0.587F;
constexpr float kLumaBlue = 0.114F;
constexpr uint32_t kByteMask = 0xFF;
constexpr int kRedShift = 16;
constexpr int kGreenShift = 8;
constexpr int kAlphaShift = 24;
constexpr float kBytesPerLight = 128.0F;
constexpr float kMaxByte = 255.0F;
constexpr std::size_t kMinFitVertices = 100;
constexpr float kMinSunLight = 0.05F;   // the fit's sun (luma) at least this: the bake shows a sun
constexpr float kBlackLight = 1.0F / 128.0F; // a colour this dark (luma) isn't fitted: unlit or painted black
constexpr int kFitRounds = 4;
constexpr int kLampRounds = 4;          // the lamp-lit vertices looked for at most this often           // the pairs' fit, then reweighted three times
constexpr double kSingular = 1e-12;
constexpr float kLeastRatio = 1e-3F;    // a tint's least value before its logarithm
constexpr float kLeastLight = 1e-4F;
constexpr float kLeastWeight = 1e-6F;
constexpr float kUpNormal = 0.7F;       // a face this much up (or down) counts toward the world's winding
constexpr uint32_t kHashOne = 0x7FEB352DU; // (a 32-bit integer hash's steps - Wellons's lowbias32)
constexpr uint32_t kHashTwo = 0x846CA68BU;
constexpr int kHashShiftOne = 16;
constexpr int kHashShiftTwo = 15;
constexpr double kHashScale = 1.0 / 4294967296.0;
constexpr uint32_t kSunSalt = 0x9E3779B9U;
constexpr float kNearlyFlat = 0.9999F;
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

[[nodiscard]] Vec3 normalised(const Vec3& v)
{
    const float length = std::sqrt(dot(v, v));
    if (!(length > 0.0F) || !std::isfinite(length)) {
        return {0.0F, 1.0F, 0.0F};
    }
    return scaled(v, 1.0F / length);
}

[[nodiscard]] float luma(const Rgbf& c)
{
    return (kLumaRed * std::get<0>(c)) + (kLumaGreen * std::get<1>(c)) + (kLumaBlue * std::get<2>(c));
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
std::pair<Vec3, Vec3> basis_of(const Vec3& n)
{
    const Vec3 helper = std::abs(y_of(n)) < kNearlyFlat ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
    const Vec3 t = normalised(cross(helper, n));
    return {t, cross(n, t)};
}

// The k-th of `count` cosine-weighted directions over the hemisphere round `n`, turned by `turn` (0 - 1 of a circle).
Vec3 hemisphere_direction(const Vec3& n, const std::pair<Vec3, Vec3>& sides, uint32_t k, uint32_t count, double turn)
{
    const double height = (static_cast<double>(k) + kHalf) / static_cast<double>(count);
    const double across = std::sqrt(height);
    const double angle = 2.0 * kPi * (std::fmod(static_cast<double>(k) * kGoldenFraction, 1.0) + turn);
    const double up = std::sqrt(std::max(0.0, 1.0 - height));
    return normalised(plus(plus(scaled(sides.first, static_cast<float>(across * std::cos(angle))),
                                scaled(sides.second, static_cast<float>(across * std::sin(angle)))),
                           scaled(n, static_cast<float>(up))));
}

// The k-th of `count` directions over the sun's disc (its radius `radius` radians), turned by `turn`.
Vec3 sun_direction(const Vec3& toward, const std::pair<Vec3, Vec3>& sides, uint32_t k, uint32_t count, double radius,
                   double turn)
{
    if (count <= 1) {
        return toward;
    }
    const double out = std::tan(radius) * std::sqrt((static_cast<double>(k) + kHalf) / static_cast<double>(count));
    const double angle = 2.0 * kPi * (std::fmod(static_cast<double>(k) * kGoldenFraction, 1.0) + turn);
    return normalised(plus(toward, plus(scaled(sides.first, static_cast<float>(out * std::cos(angle))),
                                        scaled(sides.second, static_cast<float>(out * std::sin(angle))))));
}

// Where each mesh is among `meshes`, by its id.
std::unordered_map<MeshId, std::size_t> index_of_ids(std::span<const RelitMesh> meshes)
{
    std::unordered_map<MeshId, std::size_t> out;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        if (core::at(meshes, i).id != 0) {
            out.emplace(core::at(meshes, i).id, i);
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
    const Vec3& a = core::at(mesh.positions, one);
    const Vec3 e1 = minus(core::at(mesh.positions, two), a);
    const Vec3 e2 = minus(core::at(mesh.positions, three), a);
    const Vec3 p = minus(point, a);
    const float d11 = dot(e1, e1);
    const float d12 = dot(e1, e2);
    const float d22 = dot(e2, e2);
    const float p1 = dot(p, e1);
    const float p2 = dot(p, e2);
    const float det = (d11 * d22) - (d12 * d12);
    float v = 0.0F;
    float w = 0.0F;
    if (std::abs(det) > kLeastLight) {
        v = std::clamp(((d22 * p1) - (d12 * p2)) / det, 0.0F, 1.0F);
        w = std::clamp(((d11 * p2) - (d12 * p1)) / det, 0.0F, 1.0F - v);
    }
    const float u = 1.0F - v - w;
    const Rgbf ca = colour_light(core::at(mesh.colours, one));
    const Rgbf cb = colour_light(core::at(mesh.colours, two));
    const Rgbf cc = colour_light(core::at(mesh.colours, three));
    Rgbf out{};
    for (std::size_t c = 0; c < out.size(); ++c) {
        core::at(out, c) = (core::at(ca, c) * u) + (core::at(cb, c) * v) + (core::at(cc, c) * w);
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
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        if (!is_wanted(wanted, m)) {
            continue;
        }
        const std::size_t vertices = core::at(meshes, m).positions.size();
        for (std::size_t v = 0; v < vertices; ++v) {
            const uint32_t count = rays_of(m, v);
            if (count == 0) {
                continue;
            }
            if (requests.size() + count > kBatchRays) {
                flush();
            }
            const VertexRef ref{.mesh = m, .vertex = v, .first = static_cast<uint32_t>(requests.size()), .count = count};
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
Rgbf model_light(const RelitModel& model, const Rgbf& a, const RelitVertex& vertex, const Vec3& toward)
{
    const float s = sun_term(vertex, toward);
    Rgbf out{};
    for (std::size_t c = 0; c < out.size(); ++c) {
        core::at(out, c) = (core::at(a, c) * s) + (core::at(model.sky, c) * vertex.sky) +
                           (core::at(model.bounce, c) * core::at(vertex.bounce, c));
    }
    return out;
}

float smoothstep(float low, float high, float x)
{
    const float t = std::clamp((x - low) / (high - low), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

// A mesh's tints in logarithms: each vertex's own (its colour over the model's light), its neighbourhood's, and how far
// the model vouches for it.
struct TintField {
    std::vector<Rgbf> own;
    std::vector<Rgbf> nearby;
    std::vector<float> confidence;
    std::vector<uint8_t> spread; // its neighbourhood gave a tint (else nearby is its own)
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
    for (std::size_t m = 0; m < meshes.size() && m < light.size(); ++m) {
        for (const RelitVertex& vertex : core::at(light, m)) {
            if (!vertex.used) {
                continue;
            }
            const Rgbf e = model_light(model, model.sun, vertex, model.toward);
            for (std::size_t c = 0; c < mean.size(); ++c) {
                core::at(mean, c) += core::at(e, c);
            }
            lumas.push_back(luma(e));
        }
    }
    if (lumas.empty()) {
        return out;
    }
    for (std::size_t c = 0; c < mean.size(); ++c) {
        core::at(out.floor, c) =
            std::max(kLeastLight, kRelitFloorShare * core::at(mean, c) / static_cast<float>(lumas.size()));
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
    for (std::size_t v = 0; v < count; ++v) {
        const RelitVertex& vertex = core::at(vertices, v);
        if (!vertex.used) {
            continue;
        }
        const Rgbf e = model_light(model, model.sun, vertex, model.toward);
        const Rgbf y = colour_light(core::at(mesh.colours, v));
        for (std::size_t c = 0; c < e.size(); ++c) {
            const float ratio = core::at(y, c) / std::max(core::at(e, c), core::at(scale.floor, c));
            core::at(core::at(out.own, v), c) = std::log(std::max(ratio, kLeastRatio));
        }
        const bool left_out = v < excluded.size() && core::at(excluded, v) != 0;
        core::at(out.confidence, v) =
            left_out ? 0.0F : std::min(1.0F, luma(e) / (kRelitConfidence * scale.median));
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
    for (std::size_t v = 0; v < count; ++v) {
        for (std::size_t c = 0; c < 3; ++c) {
            core::at(core::at(numerator, v), c) = core::at(core::at(out.own, v), c) * core::at(out.confidence, v);
        }
        core::at(denominator, v) = core::at(out.confidence, v);
    }
    for (uint32_t pass = 0; pass < kRelitSmoothPasses; ++pass) {
        std::vector<Rgbf> next_numerator = numerator;
        std::vector<float> next_denominator = denominator;
        for (std::size_t v = 0; v < count; ++v) {
            const std::vector<uint32_t>& around = core::at(neighbours, v);
            if (around.empty()) {
                continue;
            }
            Rgbf n = core::at(numerator, v);
            float d = core::at(denominator, v);
            for (const uint32_t other : around) {
                for (std::size_t c = 0; c < 3; ++c) {
                    core::at(n, c) += core::at(core::at(numerator, other), c);
                }
                d += core::at(denominator, other);
            }
            const auto share = static_cast<float>(around.size() + 1);
            for (std::size_t c = 0; c < 3; ++c) {
                core::at(core::at(next_numerator, v), c) = core::at(n, c) / share;
            }
            core::at(next_denominator, v) = d / share;
        }
        numerator = std::move(next_numerator);
        denominator = std::move(next_denominator);
    }
    out.nearby = out.own;
    out.spread.assign(count, 0);
    for (std::size_t v = 0; v < count; ++v) {
        if (core::at(vertices, v).used && core::at(denominator, v) > kLeastWeight) {
            core::at(out.spread, v) = 1;
            for (std::size_t c = 0; c < 3; ++c) {
                core::at(core::at(out.nearby, v), c) = core::at(core::at(numerator, v), c) / core::at(denominator, v);
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
bool solve_square(const std::array<std::array<double, 3>, 3>& m, const std::array<bool, 3>& use,
                  std::array<double, 3>* out)
{
    std::array<std::size_t, 3> map{};
    std::size_t n = 0;
    for (std::size_t k = 0; k < use.size(); ++k) {
        if (core::at(use, k)) {
            core::at(map, n++) = k;
        }
    }
    std::array<std::array<double, 4>, 3> a{}; // [row][column], the last column the right side
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            core::at(core::at(a, r), c) = core::at(core::at(m, core::at(map, r)), core::at(map, c));
        }
        core::at(core::at(a, r), 3) = 1.0;
    }
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r) {
            if (std::abs(core::at(core::at(a, r), col)) > std::abs(core::at(core::at(a, pivot), col))) {
                pivot = r;
            }
        }
        if (std::abs(core::at(core::at(a, pivot), col)) < kSingular) {
            return false;
        }
        std::swap(core::at(a, col), core::at(a, pivot));
        const double inverse = 1.0 / core::at(core::at(a, col), col);
        for (double& value : core::at(a, col)) {
            value *= inverse;
        }
        for (std::size_t r = 0; r < n; ++r) {
            if (r == col) {
                continue;
            }
            const double factor = core::at(core::at(a, r), col);
            for (std::size_t c = 0; c < core::at(a, r).size(); ++c) {
                core::at(core::at(a, r), c) -= factor * core::at(core::at(a, col), c);
            }
        }
    }
    *out = {};
    for (std::size_t r = 0; r < n; ++r) {
        core::at(*out, core::at(map, r)) = core::at(core::at(a, r), 3);
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
    std::array<double, 3> beta{1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0};
    for (int round = 0; round < kFitRounds; ++round) {
        std::array<std::array<double, 3>, 3> m{};
        for (std::size_t k = 0; k < differences.size(); ++k) {
            const std::array<float, 3>& d = core::at(differences, k);
            for (std::size_t r = 0; r < 3; ++r) {
                for (std::size_t c = 0; c < 3; ++c) {
                    core::at(core::at(m, r), c) += core::at(weights, k) * core::at(d, r) * core::at(d, c);
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
            if (!solve_square(m, use, &solved)) {
                continue;
            }
            const double total = solved[0] + solved[1] + solved[2];
            if (!(std::abs(total) > kSingular)) {
                continue;
            }
            const std::array<double, 3> candidate{solved[0] / total, solved[1] / total, solved[2] / total};
            if (std::ranges::any_of(candidate, [](double b) { return b < 0.0; })) {
                continue;
            }
            double objective = 0.0;
            for (std::size_t r = 0; r < 3; ++r) {
                for (std::size_t c = 0; c < 3; ++c) {
                    objective += core::at(candidate, r) * core::at(core::at(m, r), c) * core::at(candidate, c);
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
        for (std::size_t k = 0; k < differences.size(); ++k) {
            const std::array<float, 3>& d = core::at(differences, k);
            core::at(residuals, k) = std::abs((beta[0] * d[0]) + (beta[1] * d[1]) + (beta[2] * d[2]));
        }
        std::vector<double> sorted = residuals;
        const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2);
        std::ranges::nth_element(sorted, middle);
        const double floor = std::max(*middle, kSingular);
        fit.spread = *middle;
        fit.objective = std::max(best, 0.0);
        fit.without_sun = std::max(best_without_sun, 0.0);
        for (std::size_t k = 0; k < differences.size(); ++k) {
            core::at(weights, k) = 1.0 / std::max(core::at(residuals, k), floor);
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
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' || text_[at_] == '\r' ||
                                       text_[at_] == '\t')) {
            ++at_;
        }
        return at_ < text_.size();
    }
    bool take(char c)
    {
        if (skip_space() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }
    [[nodiscard]] bool peek(char c)
    {
        return skip_space() && text_[at_] == c;
    }
    bool string(std::string* out)
    {
        if (!take('"')) {
            return false;
        }
        out->clear();
        while (at_ < text_.size() && text_[at_] != '"') {
            if (text_[at_] == '\\' && at_ + 1 < text_.size()) {
                ++at_;
            }
            out->push_back(text_[at_++]);
        }
        return take('"');
    }
    // A value of any kind passed over; `number` / `flag` set when it was one.
    struct Scalar {
        bool is_number = false;
        double number = 0.0;
        bool is_flag = false;
        bool flag = false;
    };
    bool value(Scalar* scalar)
    {
        *scalar = {};
        if (!skip_space()) {
            return false;
        }
        const char c = text_[at_];
        if (c == '"') {
            std::string ignored;
            return string(&ignored);
        }
        if (c == '{' || c == '[') {
            return skip_container();
        }
        if (text_.substr(at_).starts_with("true") || text_.substr(at_).starts_with("false")) {
            scalar->is_flag = true;
            scalar->flag = c == 't';
            at_ += scalar->flag ? std::string_view("true").size() : std::string_view("false").size();
            return true;
        }
        if (text_.substr(at_).starts_with("null")) {
            at_ += std::string_view("null").size();
            return true;
        }
        const std::size_t start = at_;
        while (at_ < text_.size() && (std::string_view("+-.eE0123456789").find(text_[at_]) != std::string_view::npos)) {
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
        const char open = text_[at_];
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
        const Vec3& a = core::at(mesh.positions, one);
        const Vec3 face = scaled(cross(minus(core::at(mesh.positions, two), a), minus(core::at(mesh.positions, three), a)),
                                 facing);
        if (!std::isfinite(dot(face, face))) {
            continue;
        }
        for (const uint32_t corner : {one, two, three}) {
            core::at(sums, corner) = plus(core::at(sums, corner), face); // (its length twice the area: area-weighted)
            core::at(*out, corner).used = true;
        }
    }
    const bool given = mesh.normals.size() == count;
    for (std::size_t v = 0; v < count; ++v) {
        core::at(*out, v).normal = normalised(given ? core::at(mesh.normals, v) : core::at(sums, v));
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
            const Vec3& a = core::at(mesh.positions, one);
            const Vec3 face = cross(minus(core::at(mesh.positions, two), a), minus(core::at(mesh.positions, three), a));
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
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        if (is_wanted(wanted, m)) {
            relit_normals(core::at(meshes, m), facing, &core::at(*light, m));
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
        [&](std::size_t m, std::size_t v) { return core::at(core::at(*light, m), v).used ? sky_rays : 0U; },
        [&](const VertexRef& ref, std::vector<ProbeRequest>* requests) {
            const RelitVertex& vertex = core::at(core::at(*light, ref.mesh), ref.vertex);
            const Vec3 origin = plus(core::at(core::at(meshes, ref.mesh).positions, ref.vertex),
                                     scaled(vertex.normal, kRelitOffset));
            const std::pair<Vec3, Vec3> basis = basis_of(vertex.normal);
            const double turn = static_cast<double>(hashed(static_cast<uint32_t>((ref.mesh * kMeshSalt) + ref.vertex))) *
                                kHashScale;
            for (uint32_t k = 0; k < sky_rays; ++k) {
                requests->push_back({.origin = origin,
                                     .direction = hemisphere_direction(vertex.normal, basis, k, sky_rays, turn),
                                     .max_distance = kRelitRayLength,
                                     .cull_mask = kRelitWorldMask});
            }
        },
        [&](const VertexRef& ref, std::span<const ProbeResult> answers) {
            RelitVertex& vertex = core::at(core::at(*light, ref.mesh), ref.vertex);
            if (answers.size() != ref.count) {
                return;
            }
            const Vec3 origin = plus(core::at(core::at(meshes, ref.mesh).positions, ref.vertex),
                                     scaled(vertex.normal, kRelitOffset));
            const std::pair<Vec3, Vec3> basis = basis_of(vertex.normal);
            const double turn = static_cast<double>(hashed(static_cast<uint32_t>((ref.mesh * kMeshSalt) + ref.vertex))) *
                                kHashScale;
            uint32_t open = 0;
            Rgbf bounce{};
            for (uint32_t k = 0; k < ref.count; ++k) {
                const ProbeResult& answer = core::at(answers, k);
                if (!answer.hit) {
                    ++open;
                    continue;
                }
                const auto found = ids.find(answer.mesh);
                if (found == ids.end()) {
                    continue;
                }
                const Vec3 direction = hemisphere_direction(vertex.normal, basis, k, ref.count, turn);
                const Rgbf there = colour_at(core::at(meshes, found->second), answer.primitive,
                                             plus(origin, scaled(direction, answer.distance)));
                for (std::size_t c = 0; c < bounce.size(); ++c) {
                    core::at(bounce, c) += core::at(there, c);
                }
            }
            vertex.sky = static_cast<float>(open) / static_cast<float>(ref.count);
            for (float& c : bounce) {
                c /= static_cast<float>(ref.count);
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
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        if (!is_wanted(wanted, m)) {
            continue;
        }
        for (RelitVertex& vertex : core::at(*light, m)) {
            vertex.sun = 0.0F;
        }
    }
    trace_in_batches(
        meshes, wanted, tracer, stats,
        [&](std::size_t m, std::size_t v) {
            const RelitVertex& vertex = core::at(core::at(*light, m), v);
            return vertex.used && dot(vertex.normal, sun) > 0.0F ? sun_rays : 0U;
        },
        [&](const VertexRef& ref, std::vector<ProbeRequest>* requests) {
            const RelitVertex& vertex = core::at(core::at(*light, ref.mesh), ref.vertex);
            const Vec3 origin = plus(core::at(core::at(meshes, ref.mesh).positions, ref.vertex),
                                     scaled(vertex.normal, kRelitOffset));
            const double turn =
                static_cast<double>(hashed(static_cast<uint32_t>((ref.mesh * kMeshSalt) + ref.vertex) ^ kSunSalt)) *
                kHashScale;
            for (uint32_t k = 0; k < sun_rays; ++k) {
                requests->push_back({.origin = origin,
                                     .direction = sun_direction(sun, sides, k, sun_rays, radius, turn),
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
    for (std::size_t m = 0; m < count; ++m) {
        const RelitMesh& mesh = core::at(meshes, m);
        const RelitVertices& vertices = core::at(light, m);
        const std::size_t vertex_count = std::min(vertices.size(), mesh.colours.size());
        const auto lit = [&](uint32_t v) {
            return v < vertex_count && core::at(vertices, v).used &&
                   luma(colour_light(core::at(mesh.colours, v))) >= kBlackLight;
        };
        const auto x_of_vertex = [&](uint32_t v, std::size_t c) {
            const RelitVertex& vertex = core::at(vertices, v);
            return std::array<float, 3>{sun_term(vertex, model.toward), vertex.sky, core::at(vertex.bounce, c)};
        };
        for (const auto& [one, two, three] : mesh.triangles) {
            for (const auto& [i, j] : {std::pair{one, two}, std::pair{two, three}, std::pair{three, one}}) {
                if (i == j || !lit(i) || !lit(j)) {
                    continue;
                }
                const Rgbf yi = colour_light(core::at(mesh.colours, i));
                const Rgbf yj = colour_light(core::at(mesh.colours, j));
                for (std::size_t c = 0; c < differences.size(); ++c) {
                    const std::array<float, 3> xi = x_of_vertex(i, c);
                    const std::array<float, 3> xj = x_of_vertex(j, c);
                    const float over = 1.0F / std::max(core::at(yi, c) + core::at(yj, c), kLeastLight);
                    std::array<float, 3> d{};
                    for (std::size_t k = 0; k < d.size(); ++k) {
                        core::at(d, k) = ((core::at(yi, c) * core::at(xj, k)) - (core::at(yj, c) * core::at(xi, k))) * over;
                    }
                    core::at(differences, c).push_back(d);
                }
            }
        }
        for (uint32_t v = 0; v < vertex_count; ++v) {
            if (lit(v)) {
                terms.push_back({.x = {x_of_vertex(v, 0), x_of_vertex(v, 1), x_of_vertex(v, 2)},
                                 .y = colour_light(core::at(mesh.colours, v))});
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
    for (std::size_t c = 0; c < differences.size(); ++c) {
        const PairFit fit = pair_direction(core::at(differences, c));
        const std::array<double, 3>& beta = fit.beta;
        spread += core::at(kWeights, c) * fit.spread;
        with_sun += core::at(kWeights, c) * fit.objective;
        without_sun += core::at(kWeights, c) * fit.without_sun;
        std::vector<double> ratios;
        for (const Term& term : terms) {
            const std::array<float, 3>& x = core::at(term.x, c);
            const double e = (beta[0] * x[0]) + (beta[1] * x[1]) + (beta[2] * x[2]);
            if (e > kSingular) {
                ratios.push_back(core::at(term.y, c) / e);
            }
        }
        if (ratios.empty()) {
            return model;
        }
        const auto middle = ratios.begin() + static_cast<std::ptrdiff_t>(ratios.size() / 2);
        std::ranges::nth_element(ratios, middle);
        const double scale = *middle;
        core::at(model.sun, c) = static_cast<float>(beta[0] * scale);
        core::at(model.sky, c) = static_cast<float>(beta[1] * scale);
        core::at(model.bounce, c) = static_cast<float>(beta[2] * scale);
    }
    model.error = static_cast<float>(spread);
    model.sun_needed = with_sun > 0.0 ? static_cast<float>(without_sun / with_sun) : 0.0F;
    model.valid = luma(model.sun) >= kMinSunLight && model.sun_needed >= kRelitSunNeeded;
    return model;
}

void decompose_relit(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitParams& params,
                     RelitModel* model, std::vector<RelitVertices>* light, bool rescale)
{
    if (!model->valid) {
        return;
    }
    const std::size_t count = std::min(meshes.size(), light->size());
    const LightScale scale = light_scale(meshes.first(count), std::span<const RelitVertices>(*light).first(count), *model);
    double baked_sum = 0.0;
    double relit_sum = 0.0;
    for (std::size_t m = 0; m < count; ++m) {
        if (!is_wanted(wanted, m)) {
            continue;
        }
        const RelitMesh& mesh = core::at(meshes, m);
        RelitVertices& vertices = core::at(*light, m);
        const std::size_t vertex_count = std::min(vertices.size(), mesh.colours.size());
        std::vector<Rgbf> lights(vertex_count);
        for (std::size_t v = 0; v < vertex_count; ++v) {
            core::at(lights, v) = model_light(*model, model->sun, core::at(vertices, v), model->toward);
        }
        // the neighbourhoods' tints, then again without the vertices brighter than theirs predicts (lit by lamps: their
        // own tints hold the lamps' light, which mustn't spread)
        // (kLampRounds times: a wide pool of lamp light raises its neighbourhood's tint, so its edges show first)
        std::vector<uint8_t> lamp_lit(vertex_count, 0);
        TintField field = local_tints(mesh, vertices, *model, scale, lamp_lit);
        for (int round = 0; round < kLampRounds; ++round) {
            bool more = false;
            for (std::size_t v = 0; v < vertex_count; ++v) {
                const Rgbf predicted = times(exp_of(core::at(field.nearby, v)), core::at(lights, v));
                const Rgbf y = colour_light(core::at(mesh.colours, v));
                if (core::at(lamp_lit, v) == 0 && luma(y) > kRelitLampLow * luma(predicted)) {
                    core::at(lamp_lit, v) = 1;
                    more = true;
                }
            }
            if (!more) {
                break;
            }
            field = local_tints(mesh, vertices, *model, scale, lamp_lit);
        }
        for (std::size_t v = 0; v < vertex_count; ++v) {
            RelitVertex& vertex = core::at(vertices, v);
            if (!vertex.used) {
                continue;
            }
            const Rgbf& e = core::at(lights, v);
            const Rgbf y = colour_light(core::at(mesh.colours, v));
            const Rgbf& nearby = core::at(field.nearby, v);
            const Rgbf near_light = times(exp_of(nearby), e);
            const float ratio = luma(y) / std::max(luma(near_light), kLeastLight);
            const float lamp = smoothstep(kRelitLampLow, kRelitLampHigh, ratio);
            // the lamp's share of the light over what the neighbourhood's tint explains is the residual; the rest
            // gives the vertex's own tint, kept by `detail` x its confidence (the neighbourhood's otherwise)
            const float keep = core::at(field.spread, v) != 0 ? params.detail * core::at(field.confidence, v) : 1.0F;
            Rgbf predicted{};
            for (std::size_t c = 0; c < 3; ++c) {
                const float lamp_light = lamp * std::max(0.0F, core::at(y, c) - core::at(near_light, c));
                const float rest = std::max(core::at(y, c) - lamp_light, 0.0F);
                const float own = std::log(std::max(rest / std::max(core::at(e, c), core::at(scale.floor, c)), kLeastRatio));
                const float log_tint = core::at(nearby, c) + ((own - core::at(nearby, c)) * keep);
                core::at(vertex.tint, c) = std::clamp(std::exp(log_tint), 0.0F, kRelitMaxTint);
                core::at(predicted, c) = core::at(vertex.tint, c) * core::at(e, c);
                // (and what a clamped tint can't carry: light the model has no source for)
                core::at(vertex.residual, c) =
                    lamp_light + (core::at(vertex.tint, c) >= kRelitMaxTint ? std::max(0.0F, rest - core::at(predicted, c))
                                                                           : 0.0F);
            }
            baked_sum += luma(y);
            relit_sum += luma(predicted) + luma(vertex.residual);
        }
    }
    if (rescale) {
        model->scale = relit_sum > 0.0 ? std::clamp(static_cast<float>(baked_sum / relit_sum), kRelitScaleRange[0],
                                                    kRelitScaleRange[1])
                                       : 1.0F;
    }
}

Rgbf relit_light(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting)
{
    Rgbf a{};
    for (std::size_t c = 0; c < a.size(); ++c) {
        core::at(a, c) = core::at(model.sun, c) * core::at(lighting.sun_colour, c);
    }
    return model_light(model, a, vertex, normalised(lighting.toward));
}

uint32_t relit_colour(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting,
                      uint32_t original)
{
    if (!model.valid || !vertex.used) {
        return original;
    }
    const Rgbf baked = colour_light(original);
    const Rgbf e = relit_light(model, vertex, lighting);
    std::array<uint32_t, 3> bytes{};
    for (std::size_t c = 0; c < bytes.size(); ++c) {
        const float relit = (model.scale * core::at(vertex.tint, c) * core::at(e, c)) +
                            (lighting.residual ? core::at(vertex.residual, c) : 0.0F);
        float value = core::at(baked, c) + ((relit - core::at(baked, c)) * lighting.strength);
        value = std::isfinite(value) ? std::clamp(value, 0.0F, kRelitMaxValue) : core::at(baked, c);
        core::at(bytes, c) = static_cast<uint32_t>(std::lround(std::min(value * kBytesPerLight, kMaxByte)));
    }
    return (original & (kByteMask << kAlphaShift)) | (std::get<0>(bytes) << kRedShift) |
           (std::get<1>(bytes) << kGreenShift) | std::get<2>(bytes);
}

} // namespace render::rt

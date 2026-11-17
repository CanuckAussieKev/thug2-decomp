// NOT ORIGINAL - the light cards lying on surfaces: see light_cards.h.
#include "engine/gfx/light_cards.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_sun.h"

namespace render::rt {

namespace {

constexpr float kCell = 256.0F;        // the cards' grid (world units)
constexpr float kInside = 1e-4F;       // a barycentric weight this far under 0 still inside (a shared edge)
constexpr float kLeastSurface = 1.0F / 255.0F; // a texture's channel at least this (one byte) when divided by
constexpr float kByte = 1.0F / 255.0F;
constexpr float kPerByte = 1.0F / 128.0F;
constexpr float kMostByte = 255.0F;
constexpr uint32_t kByteMask = 0xFF;
constexpr int kRedShift = 16;
constexpr int kGreenShift = 8;
constexpr uint32_t kAlphaMask = 0xFF000000U;
constexpr float kLumaRed = 0.299F;
constexpr float kLumaGreen = 0.587F;
constexpr float kLumaBlue = 0.114F;

int64_t cell_of(float value) { return static_cast<int64_t>(std::floor(value / kCell)); }
uint64_t cell_key(int64_t x, int64_t y, int64_t z)
{
    constexpr uint64_t kSpan = 1U << 21U;
    constexpr uint64_t kField = kSpan - 1U;
    const auto field = [](int64_t v) { return static_cast<uint64_t>(v + static_cast<int64_t>(kSpan / 2U)) & kField; };
    return (field(x) << 42U) | (field(y) << 21U) | field(z);
}

float length_of(const Vec3& v) { return std::sqrt(dot(v, v)); }
float display_luma(const Rgbf& colour)
{
    return (kLumaRed * std::get<0>(colour)) + (kLumaGreen * std::get<1>(colour)) + (kLumaBlue * std::get<2>(colour));
}

// A card triangle with what its sampling needs: its plane's unit normal and its corners' barycentric frame.
struct Card {
    const CardTriangle* triangle = nullptr;
    Vec3 normal{};
    Vec3 edge_one{};
    Vec3 edge_two{};
    float d00 = 0.0F;
    float d01 = 0.0F;
    float d11 = 0.0F;
    float denominator = 0.0F;
};

// The barycentric weights of `point` (in the card's plane) - false when outside.
bool weights_of(const Card& card, const Vec3& point, std::array<float, 3>* out)
{
    const Vec3 v2 = minus(point, std::get<0>(card.triangle->corners));
    const float d20 = dot(v2, card.edge_one);
    const float d21 = dot(v2, card.edge_two);
    const float b = ((card.d11 * d20) - (card.d01 * d21)) / card.denominator;
    const float c = ((card.d00 * d21) - (card.d01 * d20)) / card.denominator;
    const float a = 1.0F - b - c;
    if (a < -kInside || b < -kInside || c < -kInside) {
        return false;
    }
    *out = {a, b, c};
    return true;
}

// The card's light at the point of weights `w` (display-referred; its luminous core left out unless `whole`).
Rgbf card_light_at(const Card& card, const LightCards& cards, const std::array<float, 3>& w, bool whole)
{
    const CardTriangle& t = *card.triangle;
    Rgbf colour{};
    float alpha = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    for (std::size_t k = 0; k < 3; ++k) {
        const float weight = core::at(w, k);
        for (std::size_t ch = 0; ch < 3; ++ch) {
            core::at(colour, ch) += weight * core::at(core::at(t.colours, k), ch);
        }
        alpha += weight * core::at(t.alphas, k);
        u += weight * std::get<0>(core::at(t.uvs, k));
        v += weight * std::get<1>(core::at(t.uvs, k));
    }
    Rgbf texel{1.0F, 1.0F, 1.0F};
    float texel_alpha = 1.0F;
    if (t.image < cards.images.size() && core::at(cards.images, t.image) != nullptr &&
        !core::at(cards.images, t.image)->empty()) {
        const std::array<uint8_t, 4> at = core::at(cards.images, t.image)->at(u, v);
        texel = {static_cast<float>(std::get<0>(at)) * kByte, static_cast<float>(std::get<1>(at)) * kByte,
                 static_cast<float>(std::get<2>(at)) * kByte};
        texel_alpha = static_cast<float>(std::get<3>(at)) * kByte;
        // (round L) the card's luminous core - the lamp it is drawn round, saturated in the art
        const float most = std::max({std::get<0>(texel), std::get<1>(texel), std::get<2>(texel)});
        if (!whole && linear_from_display(most) >= kTexelBright) {
            return {};
        }
    }
    const float factor = t.texel_alpha ? std::clamp(texel_alpha * alpha, 0.0F, 1.0F) : 1.0F;
    return {std::get<0>(texel) * std::get<0>(colour) * factor, std::get<1>(texel) * std::get<1>(colour) * factor,
            std::get<2>(texel) * std::get<2>(colour) * factor};
}

} // namespace

std::vector<std::vector<Rgbf>> card_light_on_surfaces(std::span<const RelitMesh> meshes,
                                                      std::span<const RelitVertices> light,
                                                      std::span<const uint8_t> cards_of, const LightCards& cards,
                                                      float facing, CardReport* report)
{
    (void)facing; // (the vertices' normals are turned by it already: relit_normals)
    std::vector<std::vector<Rgbf>> out(meshes.size());
    CardReport own;
    own.cards = cards.triangles.size();
    if (cards.triangles.empty()) {
        if (report != nullptr) {
            *report = own;
        }
        return out;
    }
    // the cards, each in the cells its box (grown by kCardOnSurface x kCardNearMiss) touches
    std::vector<Card> made;
    made.reserve(cards.triangles.size());
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    const float grow = kCardOnSurface * kCardNearMiss;
    for (const CardTriangle& triangle : cards.triangles) {
        const auto& [a, b, c] = triangle.corners;
        Card card;
        card.triangle = &triangle;
        card.edge_one = minus(b, a);
        card.edge_two = minus(c, a);
        const Vec3 n = cross(card.edge_one, card.edge_two);
        const float length = length_of(n);
        card.d00 = dot(card.edge_one, card.edge_one);
        card.d01 = dot(card.edge_one, card.edge_two);
        card.d11 = dot(card.edge_two, card.edge_two);
        card.denominator = (card.d00 * card.d11) - (card.d01 * card.d01);
        if (!(length > 0.0F) || !std::isfinite(length) || !(std::abs(card.denominator) > 0.0F)) {
            continue;
        }
        card.normal = scaled(n, 1.0F / length);
        const auto index = static_cast<uint32_t>(made.size());
        made.push_back(card);
        const Vec3 low{std::min({x_of(a), x_of(b), x_of(c)}) - grow, std::min({y_of(a), y_of(b), y_of(c)}) - grow,
                       std::min({z_of(a), z_of(b), z_of(c)}) - grow};
        const Vec3 high{std::max({x_of(a), x_of(b), x_of(c)}) + grow, std::max({y_of(a), y_of(b), y_of(c)}) + grow,
                        std::max({z_of(a), z_of(b), z_of(c)}) + grow};
        for (int64_t x = cell_of(x_of(low)); x <= cell_of(x_of(high)); ++x) {
            for (int64_t y = cell_of(y_of(low)); y <= cell_of(y_of(high)); ++y) {
                for (int64_t z = cell_of(z_of(low)); z <= cell_of(z_of(high)); ++z) {
                    grid[cell_key(x, y, z)].push_back(index);
                }
            }
        }
    }
    const std::unordered_map<MeshId, Rgbf> surfaces(cards.surface_colours.begin(), cards.surface_colours.end());
    std::vector<uint8_t> card_used(made.size(), 0);
    std::vector<float> card_nearest(made.size(), std::numeric_limits<float>::max());
    double light_sum = 0.0;
    for (std::size_t mi = 0; mi < meshes.size() && mi < light.size(); ++mi) {
        if (mi < cards_of.size() && core::at(cards_of, mi) != 0) {
            continue;
        }
        const RelitMesh& mesh = core::at(meshes, mi);
        const RelitVertices& vertices = core::at(light, mi);
        const std::size_t count = std::min(mesh.positions.size(), vertices.size());
        // each vertex's own area: a disc of half its edges' mean length
        std::vector<float> edge_sum(count, 0.0F);
        std::vector<uint16_t> edge_count(count, 0);
        for (const auto& [one, two, three] : mesh.triangles) {
            for (const auto& [i, j] : {std::pair{one, two}, std::pair{two, three}, std::pair{three, one}}) {
                if (i >= count || j >= count || i == j) {
                    continue;
                }
                const float length = length_of(minus(core::at(mesh.positions, i), core::at(mesh.positions, j)));
                for (const uint32_t v : {i, j}) {
                    core::at(edge_sum, v) += length;
                    core::at(edge_count, v) = static_cast<uint16_t>(std::min<uint32_t>(core::at(edge_count, v) + 1U, 0xFFFFU));
                }
            }
        }
        std::vector<Rgbf> added;
        for (std::size_t vi = 0; vi < count; ++vi) {
            const RelitVertex& vertex = core::at(vertices, vi);
            if (!vertex.used) {
                continue;
            }
            const Vec3& p = core::at(mesh.positions, vi);
            const auto found = grid.find(cell_key(cell_of(x_of(p)), cell_of(y_of(p)), cell_of(z_of(p))));
            if (found == grid.end()) {
                continue;
            }
            // the cards lying on the surface here: parallel, the vertex within kCardOnSurface of their plane
            std::vector<uint32_t> lying;
            bool near_miss = false;
            for (const uint32_t ci : found->second) {
                const Card& card = core::at(made, ci);
                if (std::abs(dot(card.normal, vertex.normal)) < kCardParallel) {
                    continue;
                }
                const float signed_apart = dot(minus(p, std::get<0>(card.triangle->corners)), card.normal);
                const float apart = std::abs(signed_apart);
                if (apart > grow) {
                    continue;
                }
                std::array<float, 3> w{};
                const bool under = weights_of(card, minus(p, scaled(card.normal, signed_apart)), &w);
                if (under) {
                    core::at(card_nearest, ci) = std::min(core::at(card_nearest, ci), apart);
                }
                if (apart <= kCardOnSurface) {
                    lying.push_back(ci);
                } else {
                    near_miss = near_miss || under;
                }
            }
            if (lying.empty()) {
                own.near_misses += near_miss ? 1 : 0;
                continue;
            }
            // the vertex's own area sampled: a grid over its disc in its plane
            const float radius = core::at(edge_count, vi) > 0
                                     ? 0.5F * core::at(edge_sum, vi) / static_cast<float>(core::at(edge_count, vi))
                                     : 0.0F;
            const Vec3& n = vertex.normal;
            const Vec3 helper = std::abs(y_of(n)) < 0.9F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
            Vec3 side = cross(n, helper);
            side = scaled(side, 1.0F / std::max(length_of(side), 1e-6F));
            const Vec3 other = cross(n, side);
            const bool floor = y_of(n) >= kCardFloor; // (no lamp lies on a floor: the whole card is its pool)
            Rgbf sum{};
            uint32_t samples = 0;
            const float step = kCardSamples > 1 ? 2.0F / static_cast<float>(kCardSamples - 1) : 0.0F;
            for (uint32_t i = 0; i < kCardSamples; ++i) {
                for (uint32_t j = 0; j < kCardSamples; ++j) {
                    const float s = kCardSamples > 1 ? -1.0F + (step * static_cast<float>(i)) : 0.0F;
                    const float t = kCardSamples > 1 ? -1.0F + (step * static_cast<float>(j)) : 0.0F;
                    if ((s * s) + (t * t) > 1.0F) {
                        continue;
                    }
                    ++samples;
                    const Vec3 q = plus(p, plus(scaled(side, s * radius), scaled(other, t * radius)));
                    std::vector<MeshId> counted; // (a point on two triangles' shared edge: its card's light once)
                    for (const uint32_t ci : lying) {
                        const Card& card = core::at(made, ci);
                        if (std::ranges::find(counted, card.triangle->mesh) != counted.end()) {
                            continue;
                        }
                        const Vec3 onto =
                            minus(q, scaled(card.normal, dot(minus(q, std::get<0>(card.triangle->corners)), card.normal)));
                        std::array<float, 3> w{};
                        if (!weights_of(card, onto, &w)) {
                            continue;
                        }
                        core::at(card_used, ci) = 1;
                        counted.push_back(card.triangle->mesh);
                        sum = plus(sum, card_light_at(card, cards, w, floor));
                    }
                }
            }
            if (samples == 0 || !(display_luma(sum) > 0.0F)) {
                continue;
            }
            // over the surface's own texture: what its vertex colour would be had the bake put the light in it
            const auto own_surface = surfaces.find(mesh.id);
            const Rgbf surface = own_surface != surfaces.end() ? own_surface->second : Rgbf{1.0F, 1.0F, 1.0F};
            Rgbf light_here{};
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(light_here, ch) =
                    core::at(sum, ch) / static_cast<float>(samples) / std::max(core::at(surface, ch), kLeastSurface);
            }
            if (added.empty()) {
                added.assign(mesh.positions.size(), Rgbf{});
            }
            core::at(added, vi) = light_here;
            ++own.vertices;
            light_sum += static_cast<double>(display_luma(light_here));
        }
        core::at(out, mi) = std::move(added);
    }
    own.on_surface = static_cast<std::size_t>(std::ranges::count(card_used, uint8_t{1}));
    // (reported) each card mesh: its area, which way it faces, and the nearest surface vertex under it
    std::unordered_map<MeshId, std::size_t> seen_at;
    for (std::size_t ci = 0; ci < made.size(); ++ci) {
        const Card& card = core::at(made, ci);
        const auto [at, added] = seen_at.try_emplace(card.triangle->mesh, own.seen.size());
        if (added) {
            own.seen.push_back({.mesh = card.triangle->mesh});
        }
        CardSeen& seen = core::at(own.seen, at->second);
        const float area = 0.5F * length_of(cross(card.edge_one, card.edge_two));
        const Vec3 centre = scaled(plus(plus(std::get<0>(card.triangle->corners), std::get<1>(card.triangle->corners)),
                                        std::get<2>(card.triangle->corners)),
                                   1.0F / 3.0F);
        seen.centre = plus(seen.centre, scaled(centre, area));
        seen.up += std::abs(y_of(card.normal)) * area;
        seen.area += area;
        seen.nearest = std::min(seen.nearest, core::at(card_nearest, ci));
        seen.lying += core::at(card_used, ci) != 0 ? 1 : 0;
    }
    for (CardSeen& seen : own.seen) {
        if (seen.area > 0.0F) {
            seen.centre = scaled(seen.centre, 1.0F / seen.area);
            seen.up /= seen.area;
        }
    }
    own.light = own.vertices > 0 ? light_sum / static_cast<double>(own.vertices) : 0.0;
    if (report != nullptr) {
        *report = own;
    }
    return out;
}

ConeFit cone_fit(const RelitMesh& mesh, const Vec3& apex)
{
    const std::span<const Vec3> positions = mesh.positions;
    ConeFit fit;
    if (positions.size() < 3) {
        return fit;
    }
    Vec3 mean{};
    for (const Vec3& p : positions) {
        mean = plus(mean, p);
    }
    mean = scaled(mean, 1.0F / static_cast<float>(positions.size()));
    Vec3 axis = minus(mean, apex);
    const float length = length_of(axis);
    if (!(length > 0.0F) || !std::isfinite(length)) {
        return fit;
    }
    axis = scaled(axis, 1.0F / length);
    const Vec3 helper = std::abs(y_of(axis)) < 0.9F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
    Vec3 side = cross(axis, helper);
    side = scaled(side, 1.0F / std::max(length_of(side), 1e-6F));
    const Vec3 other = cross(axis, side);
    // the radius against the distance along the axis: a line by least squares
    double n = 0.0;
    double st = 0.0;
    double sr = 0.0;
    double stt = 0.0;
    double str = 0.0;
    fit.from = 1e30F;
    fit.to = -1e30F;
    std::vector<double> azimuths;
    std::vector<std::pair<float, float>> points;
    points.reserve(positions.size());
    for (const Vec3& p : positions) {
        const Vec3 d = minus(p, apex);
        const float t = dot(d, axis);
        const Vec3 across = minus(d, scaled(axis, t));
        const float r = length_of(across);
        points.emplace_back(t, r);
        fit.from = std::min(fit.from, t);
        fit.to = std::max(fit.to, t);
        n += 1.0;
        st += t;
        sr += r;
        stt += static_cast<double>(t) * t;
        str += static_cast<double>(t) * r;
    }
    const double spread = (n * stt) - (st * st);
    if (!(spread > 0.0) || !(sr > 0.0)) {
        return fit;
    }
    const double slope = ((n * str) - (st * sr)) / spread;
    const double offset = (sr - (slope * st)) / n;
    double misfit = 0.0;
    for (const auto& [t, r] : points) {
        const double off = static_cast<double>(r) - (offset + (slope * t));
        misfit += off * off;
    }
    fit.radius_near = static_cast<float>(offset + (slope * fit.from));
    fit.radius_far = static_cast<float>(offset + (slope * fit.to));
    fit.residual = static_cast<float>(std::sqrt(misfit / n) / (sr / n));
    // its walls: the triangles facing across the axis (not caps), by area, and the directions they face round it
    double wall_area = 0.0;
    double all_area = 0.0;
    for (const auto& [one, two, three] : mesh.triangles) {
        if (one >= positions.size() || two >= positions.size() || three >= positions.size()) {
            continue;
        }
        const Vec3& a = core::at(positions, one);
        const Vec3 normal = cross(minus(core::at(positions, two), a), minus(core::at(positions, three), a));
        const float twice = length_of(normal);
        if (!(twice > 0.0F)) {
            continue;
        }
        all_area += twice;
        if (std::abs(dot(normal, axis)) > kConeWallAlong * twice) {
            continue;
        }
        wall_area += twice;
        // (its outward direction: away from the axis at its centre - two-sided cards wind either way)
        const Vec3 centre = scaled(plus(plus(a, core::at(positions, two)), core::at(positions, three)), 1.0F / 3.0F);
        const Vec3 d = minus(centre, apex);
        const Vec3 across = minus(d, scaled(axis, dot(d, axis)));
        if (length_of(across) > 0.0F) {
            azimuths.push_back(std::atan2(static_cast<double>(dot(across, other)), static_cast<double>(dot(across, side))));
        }
    }
    fit.walls = all_area > 0.0 ? static_cast<float>(wall_area / all_area) : 0.0F;
    // round the axis: the largest gap between its walls' directions from it
    constexpr double kTau = 6.283185307179586;
    std::ranges::sort(azimuths);
    double gap = azimuths.empty() ? kTau : kTau - (azimuths.back() - azimuths.front());
    for (std::size_t k = 1; k < azimuths.size(); ++k) {
        gap = std::max(gap, core::at(azimuths, k) - core::at(azimuths, k - 1));
    }
    fit.gap_degrees = static_cast<float>(gap * 360.0 / kTau);
    fit.fitted = true;
    return fit;
}

uint32_t colour_with_light(uint32_t argb, const Rgbf& added)
{
    const auto channel = [&](int shift, float more) {
        const float byte = static_cast<float>((argb >> shift) & kByteMask) + (more / kPerByte);
        return static_cast<uint32_t>(std::lround(std::clamp(byte, 0.0F, kMostByte)));
    };
    return (argb & kAlphaMask) | (channel(kRedShift, std::get<0>(added)) << kRedShift) |
           (channel(kGreenShift, std::get<1>(added)) << kGreenShift) | channel(0, std::get<2>(added));
}

} // namespace render::rt

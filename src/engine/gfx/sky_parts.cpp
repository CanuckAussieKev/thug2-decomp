// NOT ORIGINAL - what each mesh of a level's sky scene is (sky_parts.h).
#include "engine/gfx/sky_parts.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "base/core/checked_index.h"

namespace render::sky_parts {

namespace {

constexpr float kByte = 255.0F;
constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;
constexpr float kFullTurn = 360.0F;
constexpr float kThird = 1.0F / 3.0F;

bool is_glow(uint32_t blend)
{
    // 1 add, 2 add fixed, 3 subtract, 4 subtract fixed, 7 modulate, 8 modulate fixed, 9 brighten, 10 brighten fixed
    constexpr uint32_t kAdd = 1;
    constexpr uint32_t kSubtractFixed = 4;
    constexpr uint32_t kModulate = 7;
    constexpr uint32_t kBrightenFixed = 10;
    return (blend >= kAdd && blend <= kSubtractFixed) || (blend >= kModulate && blend <= kBrightenFixed);
}

float elevation(const std::array<float, 3>& p)
{
    return std::atan2(std::get<1>(p), std::hypot(std::get<0>(p), std::get<2>(p))) * kDegrees;
}

std::size_t azimuth_bin(const std::array<float, 3>& p)
{
    const float degrees = (std::atan2(std::get<0>(p), std::get<2>(p)) * kDegrees) + kFullTurn;
    const auto bin = static_cast<std::size_t>(std::floor(degrees / (kFullTurn / static_cast<float>(kAzimuthBins))));
    return bin % kAzimuthBins;
}

float alpha_of(const Coverage& coverage, uint32_t x, uint32_t y)
{
    return static_cast<float>(core::at(coverage.alpha, (static_cast<std::size_t>(y) * coverage.width) + x)) / kByte;
}

// Which way the texture's v runs on the mesh: the sign of its correlation with the elevation (kLeastCorrelation of it).
int v_direction(const MeshInput& mesh)
{
    const std::size_t n = std::min(mesh.positions.size(), mesh.uvs.size());
    if (n < 2) {
        return 0;
    }
    double e_sum = 0.0;
    double v_sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        e_sum += elevation(core::at(mesh.positions, i));
        v_sum += std::get<1>(core::at(mesh.uvs, i));
    }
    const double e_mean = e_sum / static_cast<double>(n);
    const double v_mean = v_sum / static_cast<double>(n);
    double together = 0.0;
    double e_spread = 0.0;
    double v_spread = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double de = elevation(core::at(mesh.positions, i)) - e_mean;
        const double dv = std::get<1>(core::at(mesh.uvs, i)) - v_mean;
        together += de * dv;
        e_spread += de * de;
        v_spread += dv * dv;
    }
    if (e_spread <= 0.0 || v_spread <= 0.0) {
        return 0;
    }
    const double r = together / std::sqrt(e_spread * v_spread);
    if (r <= -kLeastCorrelation) {
        return 1;
    }
    return r >= kLeastCorrelation ? -1 : 0;
}

} // namespace

const char* part_name(SkyPart part)
{
    switch (part) {
    case SkyPart::kPainted:
        return "painted";
    case SkyPart::kGlow:
        return "glow";
    case SkyPart::kCloud:
        return "cloud";
    case SkyPart::kScenery:
        return "scenery";
    }
    return "?";
}

float Coverage::at(float u, float v) const
{
    if (empty() || width == 0 || height == 0) {
        return 1.0F;
    }
    const float fu = u - std::floor(u);
    const float fv = v - std::floor(v);
    const uint32_t x = std::min(static_cast<uint32_t>(fu * static_cast<float>(width)), width - 1);
    const uint32_t y = std::min(static_cast<uint32_t>(fv * static_cast<float>(height)), height - 1);
    return alpha_of(*this, x, y);
}

float hole_share(const Coverage& coverage)
{
    if (coverage.empty()) {
        return 0.0F;
    }
    std::size_t holes = 0;
    for (const uint8_t a : coverage.alpha) {
        holes += static_cast<float>(a) / kByte < kHoleAlpha ? 1U : 0U;
    }
    return static_cast<float>(holes) / static_cast<float>(coverage.alpha.size());
}

float faint_share(const Coverage& coverage)
{
    std::size_t covered = 0;
    std::size_t faint = 0;
    for (const uint8_t byte : coverage.alpha) {
        const float a = static_cast<float>(byte) / kByte;
        covered += a >= kFaintAlpha ? 1U : 0U;
        faint += a >= kFaintAlpha && a < kHoleAlpha ? 1U : 0U;
    }
    return covered > 0 ? static_cast<float>(faint) / static_cast<float>(covered) : 0.0F;
}

float standing_share(const Coverage& coverage, int down)
{
    if (down == 0 || coverage.empty()) {
        return 0.0F;
    }
    std::size_t opaque = 0;
    std::vector<uint32_t> feet;
    std::vector<uint32_t> runs;
    for (uint32_t x = 0; x < coverage.width; ++x) {
        bool footed = false;
        bool running = false;
        uint32_t foot = 0;
        uint32_t run = 0;
        for (uint32_t k = 0; k < coverage.height; ++k) {
            const uint32_t y = down > 0 ? coverage.height - 1 - k : k;
            const bool solid = alpha_of(coverage, x, y) >= kHoleAlpha;
            if (!solid) {
                running = false;
                continue;
            }
            ++opaque;
            if (!footed) {
                footed = true;
                running = true;
                foot = k;
            }
            run += running ? 1U : 0U;
        }
        if (footed) {
            feet.push_back(foot);
            runs.push_back(run);
        }
    }
    if (opaque == 0 || feet.empty()) {
        return 0.0F;
    }
    std::vector<uint32_t> sorted = feet;
    const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2);
    std::ranges::nth_element(sorted, middle);
    const auto line = static_cast<float>(*middle);
    const float spread = kBaseSpread * static_cast<float>(coverage.height);
    if (line > spread) { // (the ground line is the card's foot: a shape whose feet are up the card hangs or floats)
        return 0.0F;
    }
    std::size_t standing = 0;
    for (std::size_t i = 0; i < feet.size(); ++i) {
        if (std::abs(static_cast<float>(core::at(feet, i)) - line) <= spread) {
            standing += core::at(runs, i);
        }
    }
    return static_cast<float>(standing) / static_cast<float>(opaque);
}

MeshFacts measure(const MeshInput& mesh)
{
    MeshFacts facts;
    facts.blend = mesh.blend;
    const bool covered = mesh.coverage != nullptr && !mesh.coverage->empty();
    facts.down = v_direction(mesh);
    if (covered) {
        facts.holes = hole_share(*mesh.coverage);
        facts.faint = faint_share(*mesh.coverage);
        facts.standing = standing_share(*mesh.coverage, facts.down);
    }
    facts.opaque_lowest = kNone;
    facts.opaque_highest = -kNone;
    const std::size_t vertices = std::min(mesh.positions.size(), mesh.uvs.size());
    for (const std::array<uint32_t, 3>& triangle : mesh.triangles) {
        const auto [ia, ib, ic] = triangle;
        if (ia >= vertices || ib >= vertices || ic >= vertices) {
            continue;
        }
        for (uint32_t s = 0; s < kSamplesPerSide; ++s) {
            for (uint32_t t = 0; s + t < kSamplesPerSide; ++t) {
                const float wb = (static_cast<float>(s) + kThird) / static_cast<float>(kSamplesPerSide);
                const float wc = (static_cast<float>(t) + kThird) / static_cast<float>(kSamplesPerSide);
                const float wa = 1.0F - wb - wc;
                std::array<float, 3> p{};
                for (std::size_t k = 0; k < 3; ++k) {
                    core::at(p, k) = (wa * core::at(core::at(mesh.positions, ia), k)) +
                                     (wb * core::at(core::at(mesh.positions, ib), k)) +
                                     (wc * core::at(core::at(mesh.positions, ic), k));
                }
                const auto& uva = core::at(mesh.uvs, ia);
                const auto& uvb = core::at(mesh.uvs, ib);
                const auto& uvc = core::at(mesh.uvs, ic);
                const float u = (wa * std::get<0>(uva)) + (wb * std::get<0>(uvb)) + (wc * std::get<0>(uvc));
                const float v = (wa * std::get<1>(uva)) + (wb * std::get<1>(uvb)) + (wc * std::get<1>(uvc));
                if (covered && mesh.coverage->at(u, v) < kHoleAlpha) {
                    continue;
                }
                const float e = elevation(p);
                facts.opaque_lowest = std::min(facts.opaque_lowest, e);
                facts.opaque_highest = std::max(facts.opaque_highest, e);
                facts.azimuths.set(azimuth_bin(p));
            }
        }
    }
    return facts;
}

std::vector<SkyPart> classify(std::span<const MeshFacts> meshes)
{
    std::vector<SkyPart> parts(meshes.size(), SkyPart::kCloud);
    std::vector<uint8_t> candidate(meshes.size(), 0);
    std::vector<uint8_t> grounded(meshes.size(), 0);
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        const MeshFacts& mesh = core::at(meshes, i);
        if (is_glow(mesh.blend)) {
            core::at(parts, i) = SkyPart::kGlow;
        } else if (mesh.holes < kLeastHoles) {
            core::at(parts, i) = SkyPart::kPainted;
        } else {
            core::at(candidate, i) = static_cast<uint8_t>(
                mesh.faint <= kMostFaint && mesh.standing >= kLeastStanding && mesh.opaque_lowest < kNone);
            core::at(grounded, i) = static_cast<uint8_t>(core::at(candidate, i) != 0 && mesh.opaque_lowest <= 0.0F);
        }
    }
    // (a range standing behind grounded scenery: its foot hidden by that scenery's top where they share an azimuth)
    for (bool more = true; more;) {
        more = false;
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            if (core::at(candidate, i) == 0 || core::at(grounded, i) != 0) {
                continue;
            }
            for (std::size_t j = 0; j < meshes.size(); ++j) {
                if (core::at(grounded, j) != 0 && (core::at(meshes, i).azimuths & core::at(meshes, j).azimuths).any() &&
                    core::at(meshes, i).opaque_lowest <= core::at(meshes, j).opaque_highest) {
                    core::at(grounded, i) = 1;
                    more = true;
                    break;
                }
            }
        }
    }
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        if (core::at(grounded, i) != 0) {
            core::at(parts, i) = SkyPart::kScenery;
        }
    }
    return parts;
}

} // namespace render::sky_parts

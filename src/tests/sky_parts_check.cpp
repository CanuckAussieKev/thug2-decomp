// NOT ORIGINAL - thug2_sky_parts_check.exe: the rule that tells a sky scene's meshes apart (engine/gfx/sky_parts.h) on
// made-up meshes, with no device and no game: a glow by its blend; a painted dome (no holes); a mountain range - a solid
// silhouette on a ground line reaching below the horizon - scenery, whichever way its texture's v runs; the same card
// with a cloud's soft texture a cloud; a solid disc floating up the sky (a painted moon) a cloud; a range whose foot is
// above the horizon scenery only where it stands behind a grounded skyline. Exit code 0 when every check passes.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/sky_parts.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace sp = render::sky_parts;

constexpr uint32_t kSide = 64;
constexpr uint8_t kSolid = 255;
constexpr float kDistance = 10000.0F;
constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
constexpr uint32_t kBlendAlpha = 5;

int& failures()
{
    static int count = 0;
    return count;
}

void expect(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) {
        ++failures();
    }
}

// A texture of a range of peaks: solid from row `top(x)` down to the last row (rows top first), a cut edge.
sp::Coverage mountains()
{
    sp::Coverage c{.width = kSide, .height = kSide, .alpha = std::vector<uint8_t>(std::size_t{kSide} * kSide, 0)};
    for (uint32_t x = 0; x < kSide; ++x) {
        const auto top = static_cast<uint32_t>(20.0 + (12.0 * std::abs(std::sin(static_cast<double>(x) * 0.15))));
        for (uint32_t y = top; y < kSide; ++y) {
            core::at(c.alpha, (std::size_t{y} * kSide) + x) = kSolid;
        }
    }
    return c;
}

// A cloud: soft round blobs whose alpha thins out from their middles over many texels.
sp::Coverage clouds()
{
    sp::Coverage c{.width = kSide, .height = kSide, .alpha = std::vector<uint8_t>(std::size_t{kSide} * kSide, 0)};
    const std::array<std::array<float, 3>, 3> blobs{{{16.0F, 30.0F, 14.0F}, {38.0F, 26.0F, 16.0F}, {54.0F, 34.0F, 10.0F}}};
    for (uint32_t y = 0; y < kSide; ++y) {
        for (uint32_t x = 0; x < kSide; ++x) {
            float a = 0.0F;
            for (const auto& [bx, by, r] : blobs) {
                const float d = std::hypot(static_cast<float>(x) - bx, static_cast<float>(y) - by) / r;
                a = std::max(a, std::clamp(1.0F - d, 0.0F, 1.0F));
            }
            core::at(c.alpha, (std::size_t{y} * kSide) + x) = static_cast<uint8_t>(std::lround(a * kSolid));
        }
    }
    return c;
}

// A disc, solid with a cut edge.
sp::Coverage disc()
{
    sp::Coverage c{.width = kSide, .height = kSide, .alpha = std::vector<uint8_t>(std::size_t{kSide} * kSide, 0)};
    for (uint32_t y = 0; y < kSide; ++y) {
        for (uint32_t x = 0; x < kSide; ++x) {
            const float d = std::hypot(static_cast<float>(x) - 32.0F, static_cast<float>(y) - 32.0F);
            core::at(c.alpha, (std::size_t{y} * kSide) + x) = d < 24.0F ? kSolid : 0;
        }
    }
    return c;
}

sp::Coverage opaque()
{
    return {.width = kSide, .height = kSide, .alpha = std::vector<uint8_t>(std::size_t{kSide} * kSide, kSolid)};
}

// A card facing the origin: azimuth `from` - `to` degrees, elevation `low` - `high`; its v down the card (`v_down`) or up.
sp::MeshInput card(float from, float to, float low, float high, const sp::Coverage* coverage, bool v_down = true,
                   uint32_t blend = kBlendAlpha)
{
    sp::MeshInput mesh;
    mesh.blend = blend;
    mesh.coverage = coverage;
    const auto at = [](float azimuth, float elevation) {
        const float a = azimuth * kRadians;
        const float e = elevation * kRadians;
        return std::array<float, 3>{kDistance * std::cos(e) * std::sin(a), kDistance * std::sin(e),
                                    kDistance * std::cos(e) * std::cos(a)};
    };
    const float top_v = v_down ? 0.0F : 1.0F;
    const float bottom_v = 1.0F - top_v;
    mesh.positions = {at(from, high), at(to, high), at(to, low), at(from, low)};
    mesh.uvs = {{0.0F, top_v}, {1.0F, top_v}, {1.0F, bottom_v}, {0.0F, bottom_v}};
    mesh.triangles = {{0, 1, 2}, {0, 2, 3}};
    return mesh;
}

sp::SkyPart part_alone(const sp::MeshInput& mesh)
{
    const std::array<sp::MeshFacts, 1> facts{sp::measure(mesh)};
    const std::vector<sp::SkyPart> parts = sp::classify(facts);
    return core::at(parts, 0);
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    const sp::Coverage peaks = mountains();
    const sp::Coverage soft = clouds();
    const sp::Coverage round = disc();
    const sp::Coverage dome = opaque();
    std::printf("the pieces:\n");
    expect(sp::faint_share(peaks) == 0.0F && sp::faint_share(soft) > 0.3F, "a cut edge has no faint texels, a cloud's many");
    expect(sp::standing_share(peaks, 1) > 0.99F && sp::standing_share(peaks, -1) < 0.1F,
           "a range stands on its bottom row (v down), not on its top");
    expect(sp::hole_share(dome) == 0.0F && sp::hole_share(peaks) > 0.3F, "a dome has no holes, a range's sky is holes");
    std::printf("each alone:\n");
    expect(part_alone(card(-20.0F, 20.0F, -3.0F, 30.0F, &dome, true, 0)) == sp::SkyPart::kPainted, "a dome: painted");
    expect(part_alone(card(-20.0F, 20.0F, 0.0F, 30.0F, &soft, true, 1)) == sp::SkyPart::kGlow, "an added glow: a glow");
    expect(part_alone(card(-20.0F, 20.0F, -4.0F, 8.0F, &peaks)) == sp::SkyPart::kScenery,
           "a range from 4 degrees under the horizon: scenery");
    expect(part_alone(card(-20.0F, 20.0F, -4.0F, 8.0F, &peaks, false)) == sp::SkyPart::kCloud,
           "the same texture upside down (hanging from the card's top): not scenery");
    expect(part_alone(card(-20.0F, 20.0F, -4.0F, 30.0F, &soft)) == sp::SkyPart::kCloud,
           "a cloud bank card on the horizon: a cloud");
    expect(part_alone(card(-5.0F, 5.0F, 20.0F, 30.0F, &round)) == sp::SkyPart::kCloud,
           "a solid disc up the sky (a painted moon): not scenery");
    expect(part_alone(card(-20.0F, 20.0F, 2.0F, 14.0F, &peaks)) == sp::SkyPart::kCloud,
           "a range floating 2 degrees up with nothing in front: not scenery");
    std::printf("a range behind a skyline:\n");
    const std::array<sp::MeshFacts, 3> scene{sp::measure(card(-20.0F, 20.0F, -1.0F, 6.0F, &peaks)),
                                             sp::measure(card(-10.0F, 10.0F, 2.0F, 14.0F, &peaks)),
                                             sp::measure(card(100.0F, 120.0F, 2.0F, 14.0F, &peaks))};
    const std::vector<sp::SkyPart> parts = sp::classify(scene);
    expect(core::at(parts, 0) == sp::SkyPart::kScenery && core::at(parts, 1) == sp::SkyPart::kScenery,
           "the skyline and the range standing behind it (its foot under the skyline's top): scenery");
    expect(core::at(parts, 2) == sp::SkyPart::kCloud, "the same range where no skyline stands: not scenery");
    std::printf("%s: %d failure(s)\n", failures() == 0 ? "ok" : "FAILED", failures());
    return failures() == 0 ? 0 : 1;
}

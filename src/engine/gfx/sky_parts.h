#pragma once

// NOT ORIGINAL - what each mesh of a level's sky scene is, read from its own data (docs/design/vulkan.md "The sky
// scene's parts"; tools/game/sky_survey.ts is this rule's twin over the levels' files). With the renderer's own sky
// (rt_scene.h SkyReplacement) the sky is the atmosphere's and the clouds' - what the painted scene shows of the sky
// itself goes; what it shows standing in front of the sky (mountains, a skyline, a tree line) stays, lit by the light
// now (render_pc/sky_scenery.h draws it). API-neutral and pure: the meshes' facts in, their parts out.
//
// The rule, for each mesh (its first pass's blend mode; its first texture's coverage - alpha - where it lies seen from
// the sky's camera at the origin):
//   - kGlow: a pass that adds, subtracts, multiplies or brightens (blend modes 1 - 4, 7 - 10) - light of the air (a sun's
//     glow, haze bands); nothing stands in front of the sky that way;
//   - kPainted: no silhouette - under kLeastHoles of its texels are holes (alpha under kHoleAlpha): the painted dome, its
//     bands, a painted ground ring;
//   - kScenery: a solid silhouette standing on the ground - its edges a cut, not a medium thinning out (at most
//     kMostFaint of its covered texels only faintly covered: a cloud's or a haze's edge thins out over many texels, a
//     mountain's or a building's is a texel or two of antialiasing), at least kLeastStanding of its opaque texels in
//     columns standing on one ground line at the card's foot (each column's lowest opaque texel within kBaseSpread of the
//     texture's height of the columns' median, and that within kBaseSpread of the bottom edge - mountains, a skyline, a
//     tree line, its base faded or not; not shapes each with sky under it, nor one hanging from the top), and grounded: its
//     opaque part reaches the horizon or below (elevation 0 from the origin), or the top of grounded scenery in front of
//     which it stands (sharing an azimuth) - a range behind a skyline;
//   - kCloud: anything else with a silhouette - clouds, a painted moon, haze cards.
#include <array>
#include <bitset>
#include <cstdint>
#include <span>
#include <vector>

namespace render::sky_parts {

enum class SkyPart : uint8_t { kPainted, kGlow, kCloud, kScenery };
[[nodiscard]] const char* part_name(SkyPart part);

inline constexpr float kHoleAlpha = 0.5F;      // a texel under this alpha is a hole: the sky shows through
inline constexpr float kFaintAlpha = 0.1F;     // covered at all from here; under kHoleAlpha only faintly
inline constexpr float kLeastHoles = 0.02F;    // a silhouette: at least this share of the texels are holes
inline constexpr float kMostFaint = 0.05F;     // a solid's edge: at most this share of its covered texels faint
inline constexpr float kLeastStanding = 0.6F;  // scenery: at least this share of its opaque texels standing
inline constexpr float kBaseSpread = 0.1F;     // a ground line: feet within this share of the texture's height
inline constexpr float kLeastCorrelation = 0.5F; // the texture's v follows the elevation at least this much
inline constexpr uint32_t kSamplesPerSide = 8;   // a triangle's coverage sampled at kSamplesPerSide^2 / 2 points
inline constexpr uint32_t kAzimuthBins = 72;     // 5 degrees each

// A texture's coverage: its first level's alpha (0 - 255), rows top first.
struct Coverage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> alpha;
    [[nodiscard]] bool empty() const { return alpha.empty(); }
    // The texel at (u, v), wrapped (nearest) - 0 - 1.
    [[nodiscard]] float at(float u, float v) const;
};

// A mesh as the sky's camera sees it: its first pass's blend mode, its texture's coverage (null or empty: none - opaque),
// its vertices (positions from the origin, first texture coordinates) and triangles.
struct MeshInput {
    uint32_t blend = 0;
    const Coverage* coverage = nullptr;
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 2>> uvs;
    std::vector<std::array<uint32_t, 3>> triangles;
};

// What the rule reads of a mesh.
struct MeshFacts {
    uint32_t blend = 0;
    float holes = 0.0F;      // share of its texture's texels that are holes
    float faint = 0.0F;      // share of its texture's covered texels only faintly covered
    float standing = 0.0F;   // share of its opaque texels standing on one ground line
    int down = 0;            // +1: v grows downward on the mesh, -1 upward, 0 neither
    float opaque_lowest = 0.0F;  // degrees: the lowest elevation of its opaque samples (none: kNone)
    float opaque_highest = 0.0F; // ... the highest
    std::bitset<kAzimuthBins> azimuths; // where its opaque samples lie round the origin
};
inline constexpr float kNone = 1.0e9F;

// The facts of one mesh.
[[nodiscard]] MeshFacts measure(const MeshInput& mesh);
// Pieces of the measure (the survey's and the check's): the share of the coverage's texels that are holes, of its covered
// texels that are faint, and of its opaque texels standing on one ground line (`down` as MeshFacts's).
[[nodiscard]] float hole_share(const Coverage& coverage);
[[nodiscard]] float faint_share(const Coverage& coverage);
[[nodiscard]] float standing_share(const Coverage& coverage, int down);
// The scene's meshes' parts (the grounding is the scene's: a mesh may stand behind another).
[[nodiscard]] std::vector<SkyPart> classify(std::span<const MeshFacts> meshes);

} // namespace render::sky_parts

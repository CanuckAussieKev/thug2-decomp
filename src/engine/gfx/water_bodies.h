#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// NOT ORIGINAL - the level's water bodies measured from its geometry (docs/design/vulkan.md "Water"; water_waves.h says
// what the measures make): for each water mesh, from the level's collision and every water mesh together -
//   - its FETCH: how far the wind has blown over water before it reaches the mesh - walked upwind over the water (any of
//     the level's water meshes at the same level, kSameWater) from points of the mesh to where the water ends. Where it
//     ends at something (any collision: a bank, a wall, the floor round a puddle) that is the shore; where it ends at
//     nothing at all (the level's world stops there - the sea goes on past its last mesh) the water is open to the
//     horizon (kOpenFetch);
//   - its DEPTH: down to the highest collision face under the water's surface (none: deep water);
//   - how OPEN it is to the sky - the share of the sky's hemisphere seen from points of the mesh past the level's
//     collision seen from above (a height field of its highest faces: every face's area and edges), 0 under a roof (a
//     face whose shadow covers the point, kOverhead above it). The
//     wind over the water is the weather's x this: a sheltered pond sees less, a puddle indoors none.
// The level is laid on a grid over its water (kMostCells a side at most, cells no smaller than kLeastCell units), padded
// by kSkyReach for the sky's horizon round it. A BODY is the water's surface joined cell to cell at about one height
// (kSameWater) - a sea drawn as many tiles is one, a mesh far under another (a sea floor's caustics) another; each body
// is measured at up to kSamples of its cells (the fetch and depth their medians, the openness their mean) and every
// mesh of it gets the same - no seams between tiles. Deterministic: the same level gives the same answer.
namespace render::water {

using Corner = std::array<float, 3>;
using Triangle = std::array<Corner, 3>;

inline constexpr uint32_t kMostCells = 1024;
inline constexpr float kLeastCell = 8.0F;      // world units
inline constexpr float kSkyReach = 2000.0F;    // world units (about 50 m): how far round the sky's horizon is looked for
inline constexpr float kSameWater = 24.0F;     // world units: water this close in height is the same body's surface
inline constexpr float kBedGap = 0.1F;         // a face this far under the surface is its bed (the water's own collision is not)
inline constexpr float kOverhead = 24.0F;      // a face this far above the surface covers it (a puddle's own rim is not)
inline constexpr uint32_t kSamples = 64;
inline constexpr uint32_t kSkyDirections = 16;

struct BodyMesh {
    std::vector<Triangle> triangles; // world space
};
struct BodyMeasure {
    bool measured = false;  // false: the mesh covered no cell (nothing to measure)
    double fetch = 0.0;     // metres
    double depth = 0.0;     // metres
    double openness = 0.0;  // 0 - 1
    uint32_t samples = 0;
    uint32_t open_samples = 0; // samples whose water runs off the world upwind
    uint32_t cells = 0;         // the cells its triangles cover (facing up)
    uint32_t surface_cells = 0; // the cells of the body it is part of (its samples come from these)
    uint32_t body = 0;          // which body (meshes of one body share its measure)
};
struct BodiesReport {
    uint32_t columns = 0;
    uint32_t rows = 0;
    float cell = 0.0F;
    uint32_t water_cells = 0;
    uint32_t collision_faces = 0;
};
// `downwind`: the wind's way over the ground (x, z; unit length).
[[nodiscard]] std::vector<BodyMeasure> measure_bodies(std::span<const BodyMesh> meshes, std::span<const Triangle> collision,
                                                      std::array<float, 2> downwind, BodiesReport* report = nullptr);

} // namespace render::water

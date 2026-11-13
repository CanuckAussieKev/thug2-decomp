#pragma once

// NOT ORIGINAL - grass: real blades where a level's ground is grass ([Graphics] grass; docs/design/vulkan.md "Grass").
// API-neutral: render_pc finds the grass (the collision's terrain types) and hands the field of blades to a Sink - the
// Vulkan Gpu's grass pass (gpu_vulkan/vulkan_grass.h), installed while [Graphics] grass=1 -, with the things that push
// the blades aside each frame (the skater, the peds). Direct3D 9 installs none: nothing is gathered or drawn there.
//
// The field: every collision face whose terrain is TERRAIN_GRASS or TERRAIN_GRASSDRIED (the terrain the skater's
// wheels sound on - engine/sound/surface_kind.h), facing up (its normal's y at least kLeastUp), collidable, gets blade
// roots scattered over it - the count its area x kBladesPerSquareUnit x the density (the fraction rounded by the face's
// own hash), each root where two more hashes put it (a uniform point on the triangle). The hashes are the face's
// corners' bits and the blade's number: the same field every run, whatever order the sectors load in. A root is dropped
// when a face that isn't grass covers it (seen from above) between kBlockFloor and kBlockReach above it: a ramp, a box, a
// bench, a path or a rail over the lawn - grass never grows through them. Then the roots are put in tiles (kTileSize
// squares of the ground), each tile's in the order of their rank - a hash: the level-of-detail order, the first
// kept furthest away -, so a tile's far draw is its first so many blades.
//
// Grass never touches collision or physics: the field is read from the collision and drawn, nothing else.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace render::grass {

inline constexpr float kTileSize = 256.0F;            // a tile's side (world units: inches)
inline constexpr float kBladesPerSquareUnit = 0.155F; // at density 1 (about 240 a square metre)
inline constexpr float kLeastUp = 0.7F;               // the steepest ground grass grows on (its normal's y)
inline constexpr float kBlockFloor = 0.5F;            // a face this far above a root (or more) and ...
inline constexpr float kBlockReach = 120.0F;          // ... at most this far covers it (a ramp, a box, a bench, a rail)
inline constexpr std::size_t kMostBlades = 4000000;   // a field's (beyond: the density lowered, logged)
inline constexpr float kLeastDensity = 0.1F;
inline constexpr float kMostDensity = 4.0F;

// A collision face as the field reads it.
enum class Kind : uint8_t {
    kBlocker, // any other collidable face: grass under it (within kBlockReach) is dropped
    kGrass,   // TERRAIN_GRASS
    kDried,   // TERRAIN_GRASSDRIED
};
struct Face {
    std::array<std::array<float, 3>, 3> corners{}; // world (y up)
    Kind kind = Kind::kBlocker;
};

// A blade as the GPU reads it (16 bytes): its root, and packed - bits 0-7 its rank (the level-of-detail order: rank /
// 256 below the share kept at a distance draws it), 8-15 its height (0 - 255: kShortest - kTallest of the base), 16-23
// the way it faces (turns / 256), 24-31 flags (kDriedFlag; the rest a hash for its shade).
struct Blade {
    std::array<float, 3> root{};
    uint32_t packed = 0;
};
inline constexpr std::size_t kBladeBytes = 16;
static_assert(sizeof(Blade) == kBladeBytes, "the grass shaders' blade");
inline constexpr uint32_t kDriedFlag = 1U << 24U;

// A tile: its blades (first, count - in rank order) and their roots' box.
struct Tile {
    std::array<float, 3> low{};
    std::array<float, 3> high{};
    uint32_t first = 0;
    uint32_t count = 0;
};

struct Field {
    uint64_t serial = 0;  // a new number per field made (0: none)
    float density = 0.0F; // what it was made at
    std::vector<Blade> blades;
    std::vector<Tile> tiles;
    // (the log's) the faces read, the ground's area (square units), the roots dropped as covered
    uint32_t grass_faces = 0;
    uint32_t dried_faces = 0;
    uint32_t blocker_faces = 0;
    uint32_t steep_faces = 0;
    double area = 0.0;
    uint32_t covered = 0;
    bool capped = false; // more than kMostBlades: the density was lowered
};

// The field of `faces` at `density` (kLeastDensity - kMostDensity; the same faces and density: the same field).
[[nodiscard]] Field make_field(std::span<const Face> faces, float density);

// What pushes the blades aside this frame (a skater or a ped: where it stands, how far its reach).
struct Pusher {
    std::array<float, 3> at{};
    float radius = 0.0F;
};
inline constexpr std::size_t kMostPushers = 8;
inline constexpr float kCharacterReach = 30.0F; // a skater's or a ped's (its board's length about)

// The backend that draws grass.
class Sink {
public:
    virtual ~Sink() = default;
    // The density the field is wanted at (0: none wanted - the field is let go).
    [[nodiscard]] virtual float wanted_density() const = 0;
    // A new field (the world loaded, the density changed); null: none.
    virtual void set_field(std::shared_ptr<const Field> field) = 0;
    // This frame's pushers (at most kMostPushers; the nearest the camera first).
    virtual void set_pushers(std::span<const Pusher> pushers) = 0;
};
void set_sink(Sink* sink);
Sink* sink();

} // namespace render::grass

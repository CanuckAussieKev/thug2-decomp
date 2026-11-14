#pragma once

// NOT ORIGINAL - decals and clutter: the wear and the small things a lived-in place has, laid over a level's surfaces
// ([Graphics] rt_decals, clutter; docs/design/vulkan.md "Decals and clutter"). API-neutral, like the grass (grass.h):
// render_pc reads the loaded world's collision and its rails (scatter_world.h) and hands the result to a Sink - the
// Vulkan Gpu's scatter passes (gpu_vulkan/vulkan_scatter.h), installed while either setting is on. Direct3D 9 installs
// none: nothing is gathered or drawn there.
//
// Everything is read from the collision - the terrain each face's wheels sound on (engine/sound/surface_kind.h), its
// flags, its shape - and the level's grind rails, and placed by hashes of the faces' corners, the item's number and
// the seed (the level's): the same decals and clutter every run, whatever order the sectors load in.
//
// The decals (flat marks drawn over the surface, multiplying its colour - visual only):
//   - on flat ground (normal y >= kFlatUp): asphalt gets cracks, oil spots, tyre marks, stains and wet patches;
//     concrete (smooth, rough, wet, and the levels' default terrain) stains, gum, cracks and wet patches; paving
//     (sidewalk, tiles) gum, stains and cracks; brick and rock floors stains; dirt wet patches. Wet patches sit in dips
//     (the ground round them no lower, some of it higher) or in the gutter by a kerb; nothing on grass, gravel, metal,
//     wood, water, ice, snow, glass;
//   - at the foot of a wall (normal y within kWallUp of level) of brick, rock, concrete or the default terrain that
//     stands on flat ground: grime, darkest at the ground, fading up with drips.
//   Each decal is a quad on its face's plane cut to the faces of the same plane and surface it overlaps (seams between
//   coplanar triangles don't cut it; an edge - a kerb's lip, a step - does): triangles in the decal's coordinates.
//
// The clutter (small instanced props - visual only, no collision):
//   - leaves and litter (paper scraps, cans) on hard ground within kNearWall of where a wall or a kerb rises (more the
//     nearer, more in corners), leaves also scattered on grass and dirt;
//   - weeds at the very foot of walls and kerbs (within kWeedReach) and on dirt;
//   - pebbles on dirt and gravel.
//
// Skate lines are kept clear (decals and clutter alike): nothing within kRampReach of a ramp, quarter pipe, bank or vert
// face (a face neither flat nor a wall, or flagged vert), nothing within kDropReach of an edge that drops away (a ledge
// top, a stair tread, a kerb's top, a platform's edge), nothing within kRailReach of a grind rail (seen from above,
// from kRailBelow under it to kRailAbove over it), nothing under anything (a face kCoverFloor - kCoverReach above it:
// a ramp, a bench, a table), nothing on a raised platform (its ground ends in a drop within 160 units in at least six
// of eight ways: a wide ledge's, a box's or a planter's top); clutter also never at the foot of stairs (a low rise with
// another rise beyond it).
// Steep faces get no ground decals and no clutter.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace render::scatter {

using Vec3 = std::array<float, 3>;

inline constexpr float kTileSize = 512.0F;  // a tile's side (world units: inches)
inline constexpr float kFlatUp = 0.96F;     // ground: its normal's y at least this
inline constexpr float kWallUp = 0.25F;     // a wall: its normal's y within this of level
inline constexpr float kNearWall = 30.0F;   // leaves and litter this near a rise
inline constexpr float kWeedReach = 8.0F;   // weeds this near a rise
inline constexpr float kLeastRise = 3.0F;   // a rise (a kerb at least)
inline constexpr float kLowRise = 12.0F;    // a rise lower than this may be a stair's riser
inline constexpr float kStairLook = 20.0F;  // ... a stair when another rise is this near beyond its top
inline constexpr float kRampReach = 40.0F;  // nothing this near a ramp, a quarter pipe, a bank, a vert face
inline constexpr float kRampHeight = 48.0F; // (a ramp's part from just below to this far above the point)
inline constexpr float kDropReach = 20.0F;  // nothing this near an edge that drops away
inline constexpr float kDropDepth = 3.0F;   // (an edge drops at least this)
inline constexpr float kRailReach = 30.0F;  // nothing this near a grind rail (across, seen from above) ...
inline constexpr float kRailBelow = 12.0F;  // ... from this far below it ...
inline constexpr float kRailAbove = 72.0F;  // ... to this far above it (a rail this high over the ground counts)
inline constexpr float kCoverFloor = 0.5F;  // a face this far above a point (or more) and ...
inline constexpr float kCoverReach = 120.0F; // ... at most this far covers it
inline constexpr float kLeastDensity = 0.1F;
inline constexpr float kMostDensity = 4.0F;
inline constexpr std::size_t kMostItems = 1000000;       // clutter (beyond: the density lowered, logged)
inline constexpr std::size_t kMostDecalVertices = 3000000; // decals' (beyond: no more decals, logged)

// A collision face's surface, from its terrain.
enum class Surface : uint8_t {
    kOther,    // metal, wood, glass, water, ice, snow, carpet...: nothing on it, nothing at its foot
    kAsphalt,  // ASPHALT
    kConcrete, // CONCSMOOTH, CONCROUGH, WETCONC, DEFAULT (the levels' commonest: their concrete and paint)
    kPaving,   // SIDEWALK, TILE
    kBrick,    // BRICK, ROCK
    kDirt,     // DIRT, DIRTPACKED, SAND
    kGravel,   // GRAVEL
    kGrass,    // GRASS, GRASSDRIED
    kCount,
};
inline constexpr std::size_t kSurfaceCount = static_cast<std::size_t>(Surface::kCount);
// The surface a terrain's name gives (TERRAIN_ASPHALT...; anything else: kOther).
[[nodiscard]] Surface surface_of_terrain(const std::string& name);

struct Face {
    std::array<Vec3, 3> corners{}; // world (y up)
    Surface surface = Surface::kOther;
    bool vert = false; // flagged vert (a quarter pipe's face): a skate line
};

// A grind rail's segment (its two nodes, world), or a lone node (a == b).
struct Rail {
    Vec3 a{};
    Vec3 b{};
};

// --- decals --------------------------------------------------------------------------------------------------------

enum class DecalKind : uint8_t {
    kCrack,
    kStain,
    kOil,
    kTyre,
    kGum,
    kWet,
    kGrime,
    kCount,
};
inline constexpr std::size_t kDecalKinds = static_cast<std::size_t>(DecalKind::kCount);
inline constexpr uint32_t kDecalVariants = 4; // the art's versions of each kind (the atlas's columns)

// A decal vertex as the GPU reads it (24 bytes): where, its place in the decal (0 - 1 across, 0 - 1 along/up), and
// packed - bits 0-3 the kind, 4-5 the variant, 8-15 its opacity (/255), 16-23 where it has faded out (x 32 units).
struct DecalVertex {
    Vec3 position{};
    std::array<float, 2> uv{};
    uint32_t packed = 0;
};
inline constexpr std::size_t kDecalVertexBytes = 24;
static_assert(sizeof(DecalVertex) == kDecalVertexBytes, "the decal shaders' vertex");
inline constexpr uint32_t kDecalFadeUnits = 32;

// A placed decal (the log's and the check's): its centre, kind, size (across, along), the face it was put on, and its
// triangles [first_vertex, first_vertex + vertex_count) (in its tile's run).
struct Decal {
    Vec3 centre{};
    Vec3 normal{};
    DecalKind kind = DecalKind::kStain;
    uint8_t variant = 0;
    std::array<float, 2> size{};
    uint32_t face = 0;
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
};

// --- clutter -------------------------------------------------------------------------------------------------------

enum class ItemKind : uint8_t {
    kLeaf,
    kPaper,
    kCan,
    kWeed,
    kPebble,
    kCount,
};
inline constexpr std::size_t kItemKinds = static_cast<std::size_t>(ItemKind::kCount);

// An item as the GPU reads it (32 bytes): its root, packed - bits 0-7 its rank (the level-of-detail order), 8-11 the
// kind, 12-15 a variant, 16-23 the way it faces (turns / 256), 24-31 a hash for its colour -, its size (world units: the
// mesh's scale), its tilt (radians, about its own across), and two spare words.
struct Item {
    Vec3 root{};
    uint32_t packed = 0;
    float size = 0.0F;
    float tilt = 0.0F;
    std::array<uint32_t, 2> spare{};
};
inline constexpr std::size_t kItemBytes = 32;
static_assert(sizeof(Item) == kItemBytes, "the clutter shaders' item");
inline constexpr uint32_t kItemKindShift = 8;
inline constexpr uint32_t kItemVariantShift = 12;
inline constexpr uint32_t kItemFacingShift = 16;
inline constexpr uint32_t kItemHashShift = 24;

// A tile: its decal vertices and items (each in its own arrays: first, count - the items in rank order) and its box.
struct Tile {
    std::array<float, 3> low{};
    std::array<float, 3> high{};
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
    uint32_t first_item = 0;
    uint32_t item_count = 0;
};

// Why candidates were turned away (the log's and the check's).
struct Rejected {
    uint32_t covered = 0;
    uint32_t ramp = 0;
    uint32_t drop = 0;
    uint32_t rail = 0;
    uint32_t stairs = 0;
    uint32_t far_from_wall = 0; // (clutter on hard ground: no rise near enough)
    uint32_t no_dip = 0;        // (a wet patch: not in a dip)
};

struct Scatter {
    uint64_t serial = 0; // a new number per scatter made
    uint32_t seed = 0;
    float decal_density = 0.0F;   // what it was made at (0: no decals)
    float clutter_density = 0.0F; // (0: no clutter)
    std::vector<DecalVertex> decal_vertices; // triangle lists, by tile
    std::vector<Decal> decals;
    std::vector<Item> items; // by tile, each tile's in rank order
    std::vector<Tile> tiles;
    std::array<uint32_t, kDecalKinds> decals_of{};
    std::array<uint32_t, kItemKinds> items_of{};
    std::array<uint32_t, kSurfaceCount> faces_of{}; // (the faces read, by surface)
    uint32_t rails = 0;
    Rejected decal_rejected;
    Rejected item_rejected;
    bool items_capped = false;
    bool decals_capped = false;
};

struct Input {
    std::span<const Face> faces;
    std::span<const Rail> rails;
    uint32_t seed = 0;            // the level's (its name's checksum): another level, other placements
    float decal_density = 0.0F;   // 0: none; else kLeastDensity - kMostDensity
    float clutter_density = 0.0F; // likewise
};

// The decals and clutter of `input` (the same input: the same result, byte for byte).
[[nodiscard]] Scatter make_scatter(const Input& input);

// Whether a point on the ground would be a skate line's (the placement's rules - the check's question): within reach
// of a ramp, an edge that drops, a rail, or covered. `faces` / `rails` as given to make_scatter.
struct Placement {
    bool flat = false;
    bool covered = false;
    bool ramp = false;
    bool drop = false;
    bool rail = false;
    float rise_distance = -1.0F; // the nearest rise's distance (-1: none within kNearWall)
};
[[nodiscard]] Placement placement_at(std::span<const Face> faces, std::span<const Rail> rails, const Vec3& point);

// --- the art (procedural: nothing fetched, nothing third-party) ---------------------------------------------------

// The decals' atlas: kDecalVariants columns x kDecalKinds rows of kDecalTile squares, RGBA8 - rgb what the surface's
// colour is multiplied by, a how much (0: untouched); row k is DecalKind k (each row's art drawn for its kind's shape:
// a tyre mark's runs along v). The same bytes every time.
inline constexpr uint32_t kDecalTile = 256;
inline constexpr uint32_t kAtlasWidth = kDecalTile * kDecalVariants;
inline constexpr uint32_t kAtlasHeight = kDecalTile * static_cast<uint32_t>(kDecalKinds);
[[nodiscard]] std::vector<uint8_t> make_decal_atlas();

// The clutter's meshes: kMeshVertices vertices each (a triangle list; unused ones collapse to the origin), by ItemKind:
// x, y, z in a unit item (y up, its root at 0) and the vertex's shade (0 - 1: the mesh's own light - a leaf's veins, a
// can's rim), then the normal and the part (0: body, 1: a second colour - a can's label, a weed's flower).
inline constexpr uint32_t kMeshVertices = 48;
struct MeshVertex {
    std::array<float, 4> position{}; // x, y, z, shade
    std::array<float, 4> normal{};   // x, y, z, part
};
[[nodiscard]] std::vector<MeshVertex> make_meshes();

// --- the backend -------------------------------------------------------------------------------------------------

class Sink {
public:
    virtual ~Sink() = default;
    // The densities wanted (0: none - decals / clutter off).
    [[nodiscard]] virtual float wanted_decals() const = 0;
    [[nodiscard]] virtual float wanted_clutter() const = 0;
    // A new scatter (the world loaded, a density changed); null: none.
    virtual void set_scatter(std::shared_ptr<const Scatter> scatter) = 0;
};
void set_sink(Sink* sink);
Sink* sink();

} // namespace render::scatter

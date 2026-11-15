#pragma once

// NOT ORIGINAL - snow ([Graphics] snow, snow_depth, snow_trail_quality, snowfall; docs/design/vulkan.md "Snow"): a thin
// layer of snow over every outdoor level's open ground, which the skaters, peds and vehicles part as they go through it.
// API-neutral: render_pc reads the world (its collision, its instances) and hands it here; a backend that draws snow -
// the Vulkan Gpu's (gpu_vulkan/vulkan_snow.h) - reads what is kept here. Direct3D 9 draws none: nothing is gathered.
//
// Purely visual. Nothing here is read by the game: the collision, the physics, the scripts and the game's random numbers
// are untouched (the skater rides on the level's own collision under the snow; the snow's spray has its own random
// numbers). Given the same positions it is the same every run: everything below is worked out on the processor, once a
// simulation step, from what the step left.
//
// Three parts:
//   1. The sky map (make_sky_map: once a level, from its collision): where snow settles. The level seen from above on a
//      grid of cells - each cell the topmost collidable face's height at its centre, and how much snow that face holds:
//      none on walls, a slope's share thinning from kLeastUp to kFullUp (its normal's y), none on a face of an indoor
//      sector (its light group). A surface lower than the cell's top is under something (a roof, a bridge, an awning):
//      no snow. The drawn world takes it per pixel (the Vulkan world shader's snow bit: the surface's own slope from the
//      screen, open to the sky by the map), so the cover follows the drawn meshes, not the collision's coarser shapes.
//   2. The trails (TrailField): a heightfield of what was pressed into the snow, in tiles of kTileTexels x kTileTexels
//      texels kept for the whole level session (a tile is made where something first pressed). A texel holds four bytes:
//      the press (0 - 255: the share of the snow pushed down), the berm (the snow pushed up beside a trail), the activity
//      (where the layer is raised over the ground: a soft area round every trail - beyond it the ground is the snow's top)
//      and a spare. Stamped each step from the pressers (the skaters' and peds' feet or boards, vehicles' wheels); filled
//      in while snow falls (the press and berm lowered by one step a kFillSeconds / 255).
//   3. The spray (SprayParticle): puffs kicked up where a board carves, bigger on a landing, scaled by the speed - their
//      starts made here with the snow's own random numbers (a PCG32 seeded per level session), each moving on its own
//      (a throw under gravity) so the backend draws them from their start and age alone.
//
// A backend draws the layer only where the trails raised it (the activity): the ground's snow is the drawn world's own
// surface; inside the active area the layer stands snow_depth over the ground and the trails cut into it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace render::snow {

// --- the settings ---------------------------------------------------------------------------------------------------
inline constexpr float kDefaultDepth = 4.0F; // world units (inches): a few inches - the skater rides on the collision
inline constexpr float kLeastDepth = 1.0F;
inline constexpr float kMostDepth = 8.0F;
inline constexpr int kLeastQuality = 1;
inline constexpr int kDefaultQuality = 2;
inline constexpr int kMostQuality = 3;
struct Settings {
    bool on = false;      // [Graphics] snow (a backend that draws it set it: Direct3D 9 never does)
    float depth = kDefaultDepth;   // snow_depth
    int quality = kDefaultQuality; // snow_trail_quality: the trails' texel and window (trail_texel / window_tiles)
    bool falling = true;  // snowfall: snow falls (drawn by the backend); the trails fill in while it does
    bool operator==(const Settings&) const = default;
};
void set_settings(const Settings& settings);
[[nodiscard]] Settings settings();
// The trails' texel (world units) and how many tiles a side the backend's window round the camera has, per quality.
[[nodiscard]] float trail_texel(int quality);
[[nodiscard]] int window_tiles(int quality);

// --- the sky map ----------------------------------------------------------------------------------------------------
inline constexpr float kLeastCell = 4.0F;     // the sky map's finest cell (world units)
inline constexpr uint32_t kMostCells = 2048;  // cells a side at most (a bigger level: coarser cells)
inline constexpr float kLeastUp = 0.5F;       // a face's normal y: no snow below (walls, steep banks) ...
inline constexpr float kFullUp = 0.8F;        // ... all of it from here (flat ground, gentle slopes)
inline constexpr float kOpenSlack = 2.0F;     // a surface this far below its cell's top ...
inline constexpr float kOpenCellShare = 0.6F; // ... + this share of the cell (a slope within it) is still the top

// A collision face as the sky map reads it.
struct Face {
    std::array<std::array<float, 3>, 3> corners{}; // world (y up)
    bool settles = true; // false: an indoor sector's face (it covers what is under it, holds no snow)
};
// A cell: its top's height (16 bits over [low, low + span], 0: no face over the cell - open sky down to nothing) and
// the cover there (16 bits, 0 - 1) - as the backend's RG16 texture holds it.
struct SkyMap {
    uint64_t serial = 0; // a new number per map made (0: none)
    float x0 = 0.0F;     // the first cell's corner (world x, z)
    float z0 = 0.0F;
    float cell = kLeastCell;
    uint32_t columns = 0;
    uint32_t rows = 0;
    float low = 0.0F;    // the heights' range
    float span = 1.0F;
    std::vector<uint32_t> cells; // row by row (z), low 16 bits the height, high 16 bits the cover
    // (the log's) faces read, those that hold snow somewhere on the map, the open snowy area (square units)
    uint32_t faces = 0;
    uint32_t indoor_faces = 0;
    double snowy_area = 0.0;
};
[[nodiscard]] SkyMap make_sky_map(std::span<const Face> faces);
// The ground under (x, z) as the trails' layer takes it (the backend's vertex shader is its twin): the four cells round
// the point blended where their tops agree within kStepHeight (a continuous surface), else the nearest cell's (a kerb's
// or a ledge's step stays a step). `found` false off the map or where no face is.
inline constexpr float kStepHeight = 6.0F;
struct Ground {
    float height = 0.0F;
    float cover = 0.0F; // 0 - 1
    bool found = false;
};
[[nodiscard]] Ground ground_at(const SkyMap& map, float x, float z);

// --- the trails -----------------------------------------------------------------------------------------------------
inline constexpr int kTileTexels = 64;
inline constexpr std::size_t kTileTexelCount = static_cast<std::size_t>(kTileTexels) * kTileTexels;
inline constexpr float kActiveReach = 24.0F; // the layer is raised this far round a trail (then falls to the ground)
inline constexpr float kBermWidth = 5.0F;    // the berm's ring outside a press
inline constexpr float kBermHeight = 0.6F;   // a full berm: this share of the depth over the snow's top
inline constexpr double kFillSeconds = 180.0; // a full trail filled in this long while snow falls
// A texel's bytes (RGBA8 as the backend's texture holds it): press, berm, activity, spare.
inline constexpr uint32_t kPressShift = 0;
inline constexpr uint32_t kBermShift = 8;
inline constexpr uint32_t kActiveShift = 16;
struct Tile {
    int x = 0; // the tile's place (texels / kTileTexels from the world's origin)
    int z = 0;
    uint32_t version = 0; // a new number each time a texel changes
    std::array<uint32_t, kTileTexelCount> texels{}; // row by row (z)
};
// A press: a capsule from `from` to `to` (x, z) of half width `radius`: the press within it (to `press`, 0 - 1, its
// edge softened over the outer 40%), a berm in the ring kBermWidth outside it (to `berm`), the activity out to
// kActiveReach beyond that. Each byte keeps the most it was given.
struct Stamp {
    std::array<float, 2> from{};
    std::array<float, 2> to{};
    float radius = 0.0F;
    float press = 0.0F;
    float berm = 0.0F;
};
class TrailField {
public:
    explicit TrailField(float texel = 2.0F) : texel_(texel) {}
    [[nodiscard]] float texel() const { return texel_; }
    void stamp(const Stamp& stamp);
    // Every press and berm lowered by `steps` (0 - 255; the activity stays).
    void fill(uint32_t steps);
    // The tile at (x, z), or null when nothing was pressed there.
    [[nodiscard]] const Tile* tile(int x, int z) const;
    [[nodiscard]] std::size_t tile_count() const { return tiles_.size(); }
    // A texel's bytes (0 where no tile is).
    [[nodiscard]] uint32_t texel_at(int x, int z) const;
    // The texel holding world (x, z).
    [[nodiscard]] std::array<int, 2> texel_of(float x, float z) const;
    // The heaviest press, berm and activity anywhere (the checks', the log's).
    void totals(uint64_t* press, uint64_t* berm, uint64_t* active) const;

private:
    Tile& tile_for(int x, int z);
    float texel_;
    std::vector<std::unique_ptr<Tile>> tiles_; // (in the order made: a session's are few hundred at most)
    std::vector<std::pair<int64_t, std::size_t>> index_; // sorted by key: the tile's place
};
// The surface's height over the ground for a texel's bytes (world units; the backend's shaders are its twin): the
// activity's depth less what was pressed, plus the berm.
[[nodiscard]] float layer_height(uint32_t texel, float depth, float cover);

// --- the pressers -----------------------------------------------------------------------------------------------------
enum class PresserKind : uint8_t {
    kCharacter, // a skater or a ped (an instance with a skeleton): its board when moving fast, else its feet
    kWheel,     // a vehicle's wheel (its hub)
};
struct Presser {
    uint64_t id = 0; // what it is (its instance and wheel): its own memory - strides, in the air - is kept by it
    PresserKind kind = PresserKind::kCharacter;
    std::array<float, 3> at{};     // where it stands (a character's origin; a wheel's hub)
    std::array<float, 2> facing{}; // the way it faces (x, z; for the feet when standing still)
};
inline constexpr float kRideSpeed = 150.0F;     // units a second: faster is riding (the board), slower walking (feet)
inline constexpr float kBoardHalfWidth = 6.0F;  // (the board and the snow it shoves aside)
inline constexpr float kFootRadius = 3.0F;
inline constexpr float kFootSide = 3.5F;        // a foot's distance from the middle
inline constexpr float kStride = 26.0F;
inline constexpr float kWheelHalfWidth = 4.0F;
inline constexpr float kWheelReach = 20.0F;     // a hub this far over the ground: its wheel on it
inline constexpr float kFootReach = 6.0F;       // a character this far over the snow's top: on it
inline constexpr float kAirborne = 18.0F;       // ... this far over it: in the air
inline constexpr float kLandingRadius = 12.0F;
inline constexpr float kTeleport = 240.0F;      // a move this long in a step: not a press (a restart, a respawn)

// --- the spray --------------------------------------------------------------------------------------------------------
inline constexpr std::size_t kMostSpray = 2048; // particles alive at most (the oldest give way)
inline constexpr float kSprayGravity = 386.0F;  // units a second squared (inches: 9.8 m/s^2)
struct SprayParticle {
    std::array<float, 3> at{}; // where it started
    float born = 0.0F;         // the snow clock then (seconds)
    std::array<float, 3> velocity{};
    float life = 0.0F; // seconds
    float size = 0.0F; // its radius at the start (world units)
    float shade = 0.0F; // 0 - 1: its own brightness's hash
    std::array<float, 2> spare{};
};
static_assert(sizeof(SprayParticle) == 48, "the backend's instance data");
// A particle's place `age` seconds after its start (the shaders' twin).
[[nodiscard]] std::array<float, 3> spray_place(const SprayParticle& particle, float age);

// --- the state, stepped ---------------------------------------------------------------------------------------------
// The level session: a new sky map (a new level loaded) starts it again - no trails, no spray, the random numbers
// seeded afresh.
void set_sky_map(std::shared_ptr<const SkyMap> map);
[[nodiscard]] std::shared_ptr<const SkyMap> sky_map();
// One simulation step of `seconds`: the pressers' presses stamped and their spray started, the trails filled in while
// snow falls. Nothing while snow is off or no sky map is made.
void step(std::span<const Presser> pressers, double seconds, bool snowing);
// What the backend reads, under the state's lock: the trails, the spray alive now, the clock.
void read_trails(const std::function<void(const TrailField& field)>& reader);
[[nodiscard]] std::vector<SprayParticle> live_spray();
[[nodiscard]] double clock();
// (the log's, the checks') stamps made, spray started, fill steps, since the session began
struct Counts {
    uint64_t stamps = 0;
    uint64_t landings = 0;
    uint64_t spray = 0;
    uint64_t fills = 0;
    uint64_t steps = 0;
};
[[nodiscard]] Counts counts();
// The session cleared (the checks'; a new sky map does it too).
void reset();

// --- the drawn world --------------------------------------------------------------------------------------------------
// render_pc says, for each draw, whether it is the world's ground or a scene object's - a mesh that isn't skinned (the
// characters keep their own colours): only those take the snow's cover. Cleared after the draw.
void set_current_ground(bool ground);
[[nodiscard]] bool current_ground();
// An instance with this many bones or more is a character (a skater, a ped - their skeleton): render_pc says while one
// is drawn (Singleton::render), and none of its draws is the ground.
inline constexpr int kSkeletonBones = 24;
void set_current_character(bool character);
[[nodiscard]] bool current_character();

} // namespace render::snow

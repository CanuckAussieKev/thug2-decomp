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
//      the press (0 - 255: the share of the snow pushed down), the berm (the snow pushed up beside a trail) and two
//      spares. Stamped each step from the pressers (the skaters' and peds' feet or boards, bodies, vehicles' wheels);
//      filled in while snow falls (the press and berm lowered by one step a kFillSeconds / 255).
//   3. The spray (SprayParticle): puffs kicked up where a board carves, bigger on a landing, scaled by the speed - their
//      starts made here with the snow's own random numbers (a PCG32 seeded per level session), each moving on its own
//      (a throw under gravity) so the backend draws them from their start and age alone.
//
// A backend draws the snow's layer over all the open ground near the camera: the surface (surface_at) snow_depth over
// the ground where it is covered, banked against what stands higher round it, and the trails cut into it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "engine/gfx/surface_trail.h"

namespace render::snow {

// --- the settings ---------------------------------------------------------------------------------------------------
inline constexpr float kDefaultDepth = 8.0F; // world units (inches): the skater rides on the collision, in the snow
inline constexpr float kLeastDepth = 1.0F;
inline constexpr float kMostDepth = 24.0F;
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
// How much snow a surface holds by its slope (its normal's y, `up`): slope_cover. Full on flat ground and gentle slopes
// (kFullUp and over), thinning up ramps and banks to a light coat (kCoatShare) on steep ones - a quarter pipe's face -,
// none on a near-vertical wall (kLeastUp and under). The world shader's cover is its twin.
inline constexpr float kLeastUp = 0.08F;  // (about 85 degrees: a wall) no snow below ...
inline constexpr float kCoatUp = 0.3F;    // ... the light coat whole from here (about 72 degrees) ...
inline constexpr float kThinUp = 0.4F;    // ... thickening from here (66 degrees) ...
inline constexpr float kFullUp = 0.93F;   // ... to all of it here (22 degrees)
inline constexpr float kCoatShare = 0.3F; // the light coat: this share of the full cover
[[nodiscard]] float slope_cover(float up);
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
// the point blended where their tops agree within kStepHeight, or where they are one sloped face's - every cell holding
// some snow and less than all of it (a ramp, a bank: snow lies on it by its slope) - rising no more than kSlopeSpan cells
// across them (a continuous surface), else the nearest cell's (a kerb's
// or a ledge's step stays a step). `found` false off the map or where no face is.
inline constexpr float kStepHeight = 6.0F;
inline constexpr float kSlopeSpan = 2.0F;
struct Ground {
    float height = 0.0F;
    float cover = 0.0F; // 0 - 1
    bool found = false;
};
[[nodiscard]] Ground ground_at(const SkyMap& map, float x, float z);
// The snow's top where nothing pressed it (the layer's surface; the backend's vertex shader is its twin): the ground's own
// snow (the depth x its cover) banked against what stands higher round it - a cell kBankRings x the reach away holds its
// own top there (a flat top: a kerb's, a wall's, a roof's - kBankCover), falling kBankSlope a unit toward the point, and
// the point takes the highest of them, at most
// kBankShare of its own snow over its own top. So a kerb lower than the snow is buried in a smooth bank, a wall gets a
// drift at its foot, and ground that holds no snow (under a roof, indoors, a wall's face) is never raised.
inline constexpr float kBankShare = 1.0F;  // a bank's most over the snow's own top: this share of the snow there
inline constexpr float kBankSlope = 0.6F;  // its fall a unit away from what it leans on (about 31 degrees)
inline constexpr float kBankLeastReach = 4.0F; // the rings' reach: the depth, at least this (units)
inline constexpr float kBankCover = 0.99F;     // what banks snow: a flat top (all the snow) - a ramp sheds its own
inline constexpr std::array<float, 3> kBankRings{0.5F, 1.0F, 1.7F};
struct Surface {
    float ground = 0.0F; // the ground's height (ground_at's)
    float snow = 0.0F;   // the snow's thickness over it (0 where it holds none)
    bool found = false;
};
[[nodiscard]] Surface surface_at(const SkyMap& map, float x, float z, float depth);

// --- the trails -----------------------------------------------------------------------------------------------------
// (the trails' field and its tiles are the surface trails' - engine/gfx/surface_trail.h: one plane a texel here)
inline constexpr int kTileTexels = surface_trail::kTileTexels;
inline constexpr std::size_t kTileTexelCount = surface_trail::kTileTexelCount;
inline constexpr float kBermWidth = 5.0F;    // the berm's ring outside a press
inline constexpr float kBermHeight = 0.6F;   // a full berm: this share of the depth over the snow's top
inline constexpr double kFillSeconds = 180.0; // a full trail filled in this long while snow falls
// A texel's bytes (RGBA8 as the backend's texture holds it): press, berm, two spares.
inline constexpr uint32_t kPressShift = 0;
inline constexpr uint32_t kBermShift = 8;
using Tile = surface_trail::Tile;
// A press: a capsule from `from` to `to` (x, z) of half width `radius`: the press within it (to `press`, 0 - 1, its
// edge softened over the outer 40%), a berm in the ring kBermWidth outside it (to `berm`). Each byte keeps the most it
// was given.
struct Stamp {
    std::array<float, 2> from{};
    std::array<float, 2> to{};
    float radius = 0.0F;
    float press = 0.0F;
    float berm = 0.0F;
};
// The snow's field: the ground seen from above (x, z), one word a texel - the snow's rules for a press and the filling in.
class TrailField : public surface_trail::Field {
public:
    explicit TrailField(float texel = 2.0F) : Field(texel, 1) {}
    void stamp(const Stamp& stamp);
    // Every press and berm lowered by `steps` (0 - 255).
    void fill(uint32_t steps);
    // The sums of every press and berm anywhere (the checks', the log's).
    void totals(uint64_t* press, uint64_t* berm) const;

private:
    std::vector<uint32_t> settled_; // each tile (in the order made): its version + 1 when it was last found filled in
};
// The layer's height over the ground for a texel's bytes where the snow there is `snow` thick (surface_at's; world
// units - the backend's shaders are its twin): the snow less what was pressed, plus the berm (kBermHeight of the snow).
[[nodiscard]] float layer_height(uint32_t texel, float snow);

// --- the pressers -----------------------------------------------------------------------------------------------------
enum class PresserKind : uint8_t {
    kCharacter, // a skater or a ped (an instance with a skeleton): its board when moving fast, else its feet
    kWheel,     // a vehicle's wheel (its hub)
    kBody,      // a joint of a character's body (not its feet): where it touches the snow - a bail, a hand plant
    kMover,     // an instance with no wheels found (no bones) moving over the ground: its two tracks
};
struct Presser {
    uint64_t id = 0; // what it is (its instance and wheel): its own memory - strides, in the air - is kept by it
    PresserKind kind = PresserKind::kCharacter;
    std::array<float, 3> at{};     // where it stands (a character's origin; a wheel's hub)
    std::array<float, 2> facing{}; // the way it faces (x, z; for the feet when standing still); a mover's sideways axis
    float size = 0.0F;             // a body joint's radius; a mover's half track (units)
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
inline constexpr float kBodyReach = 5.0F;       // a body joint this near the snow's top (over or under it): touching it
inline constexpr float kBodyPress = 0.85F;
inline constexpr float kBodyBerm = 0.45F;
inline constexpr float kMoverSpeed = 20.0F;     // units a second: a mover slower than this presses nothing (props)
inline constexpr float kMoverReach = 30.0F;     // its origin this far over the ground (or under it): on it
inline constexpr float kMoverHalfWidth = 5.0F;  // a mover's tyre

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
// How far the level's wind (engine/gfx/wind.h) has carried the falling snow since the session began (world units
// downwind: its gusting speed added up step by step on the snow's clock - the flakes' drift, the same every run).
[[nodiscard]] double wind_drift();
// The winter sky the lighting state takes while there is snow (day_clock: rt::apply_overcast): kOvercastFalling while
// it snows (the last step's), kOvercastLying with snow on the ground and none falling, 0 with snow off or no sky map
// (indoors, or not made yet).
inline constexpr float kOvercastFalling = 1.0F;
inline constexpr float kOvercastLying = 0.55F;
[[nodiscard]] float overcast_share();
// (the log's, the checks') stamps made, spray started, fill steps, since the session began
struct Counts {
    uint64_t stamps = 0;
    uint64_t landings = 0;
    uint64_t spray = 0;
    uint64_t fills = 0;
    uint64_t steps = 0;
    uint64_t body = 0; // (of the stamps) a body joint's
};
[[nodiscard]] Counts counts();
// The trails' tiles made this session (the log's).
[[nodiscard]] std::size_t tile_count();
// The session cleared (the checks'; a new sky map does it too).
void reset();

} // namespace render::snow

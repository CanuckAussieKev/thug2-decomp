#pragma once

// NOT ORIGINAL - the air's way in from the open sky (docs/design/vulkan.md "Round W: the air and the wind"): a level-wide
// sparse voxel volume saying, for every place, how much of the weather's wind reaches it and whether what falls from
// the sky straight down reaches it - DERIVED from the level's geometry and what its surfaces are made of, never flagged
// per level or per object. Light and air are separate transports: glass lets light through but no air; a chain-link
// fence or a grating lets air (and rain) through; a wall or a roof neither. So a room behind a window is "indoors" for
// the wind (no draught through glass) while the light store still lights it through the glass.
//
// The model (built once per level at load from the collision - render_pc/air_world.h - and published):
//   1. The level is cut into cells kCell units across, kept in bricks of kBrickCells^3: a brick no face touches is one
//      value (a FAR brick: open air, at least a brick from any face); a brick a face touches keeps its cells (MIXED).
//   2. Each face is rasterised into the cells it touches (conservatively - a wall never leaks between its cells) with
//      the share of its area open to air (rt_material_table.h air_transmission by the face's terrain: glass 0, a
//      chain-link fence 0.86, a bar grating 0.79, everything solid 0); a cell keeps the least open face in it.
//   3. Each cell's PASSAGE WIDTH: the run of cells air can pass through it along x, along y and along z (each side up to
//      kOpenCells), the narrowest of the three - a doorway's width, a corridor's, the gap under a door, a room's height.
//   4. The FREE STREAM: the air the weather's wind blows through unhindered - flooded from the open sky over the level
//      through far bricks and cells whose passage is at least kOpenCells wide every way and wholly open.
//   5. Everywhere else air can reach (a best-first search out of the free stream through passable cells): the wind there
//      is T x min(1, kJetCore x w / s) - T the open shares of the screens it passed (fences, grates), w the narrowest
//      passage on its way, s how far it has come since it left the free stream. Out of an opening of width w a draught
//      keeps the free stream's speed for about kJetCore x w (a free jet's potential core - Rajaratnam 1976, Turbulent
//      Jets: the centreline velocity u / u0 = 6.2 d / x past the core of a round jet) and falls off as 1 / distance
//      after - so a room's air is stiller the farther it is from its door, a closed room's (no way in) is still: 0.
//   6. The SKY reach: the open share of every face straight above a cell multiplied together - 1 in the open, 0 under a
//      roof or a glass canopy, the fence's share under a grating (what rain and snow falling straight down reach; the
//      wind-slanted reach of falling rain is the precipitation occlusion map's - precip_occlusion.h).
// Not modelled: the wind's direction (the connectivity is the same whichever way it blows - the wake behind a wall in
// the wind isn't stiller than its windward side), recirculation in street canyons, buoyancy (a warm room's stack draught).
//
// Sampled trilinearly (a cell inside a wall takes the most open of the cells beside it, so a surface's wind is the air's
// beside it); outside the volume (beyond the level) everything is free stream and open sky. API-neutral; the GLSL twin
// samples a window of it round the camera that a backend uploads (air_window; gpu_vulkan/vulkan_air.h).

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace render::air {

using Vec3 = std::array<float, 3>;

inline constexpr float kCell = 16.0F;     // world units (about 0.4 m) a cell
inline constexpr int kBrickCells = 8;     // cells a brick side
inline constexpr int kOpenCells = 8;      // a passage this many cells wide every way (128 units, 3.25 m) is open air
inline constexpr float kJetCore = 6.2F;   // a draught keeps its speed for this many opening widths (Rajaratnam 1976)
inline constexpr float kWallShare = 0.5F; // a cell letting less than this share of air through is a wall for the widths
inline constexpr float kPad = 2.0F; // bricks of open air round the level's faces (above: the sky the wind comes from)

struct Face {
    std::array<Vec3, 3> corners{}; // world (y up)
    float air = 0.0F;              // the share of its area open to air (0: solid)
};

// What the air at a point is.
struct Sample {
    float wind = 1.0F; // the share of the free stream's wind that reaches here (0 - 1)
    float sky = 1.0F;  // the share of what falls straight down from the sky that reaches here (0 - 1)
};

struct Report {
    std::array<int, 3> bricks{}; // the brick grid (x, y, z)
    std::size_t mixed = 0;       // bricks keeping their cells
    std::size_t faces = 0;
    std::size_t screens = 0;    // faces letting some air through (fences, gratings)
    std::size_t free_cells = 0; // mixed bricks' cells in the free stream
    std::size_t sheltered = 0;  // ... reached out of it
    std::size_t stilled = 0;    // ... passable but never reached (closed rooms, under the ground)
    std::size_t walls = 0;      // ... too closed for air
    double milliseconds = 0.0;
    std::array<double, 5>
        phases{}; // (ms since the start at the end of: the faces, the widths, the sky, the free stream, the rest)
    std::size_t bytes = 0; // what the built volume keeps
};

class Volume {
public:
    // The volume of `faces` (an empty list: no volume - everything open).
    [[nodiscard]] static Volume build(std::span<const Face> faces, Report* report = nullptr);
    [[nodiscard]] Sample at(const Vec3& point) const;
    [[nodiscard]] bool empty() const { return slots_.empty() && far_.empty(); }
    [[nodiscard]] uint64_t serial() const { return serial_; }
    // The cell grid's origin and size in cells (x, y, z).
    [[nodiscard]] Vec3 origin() const { return origin_; }
    [[nodiscard]] std::array<int, 3> cells() const
    {
        return {bricks_[0] * kBrickCells, bricks_[1] * kBrickCells, bricks_[2] * kBrickCells};
    }
    // One cell's stored values (outside the grid: open): wind 0 - 254 (255: a wall's cell), sky 0 - 255.
    [[nodiscard]] std::array<uint8_t, 2> cell(int x, int y, int z) const;

private:
    Vec3 origin_{};
    std::array<int, 3> bricks_{};
    std::vector<int32_t> slots_;                // per brick: its mixed cells' slot, -1 a far brick
    std::vector<std::array<uint8_t, 2>> far_;   // per brick: a far brick's wind and sky
    std::vector<std::array<uint8_t, 2>> cells_; // per slot, kBrickCells^3 cells: wind and sky
    uint64_t serial_ = 0;
};

// The level's volume now (null: none - every place open: no level, Direct3D's path before a level, a level being built).
void set_volume(std::shared_ptr<const Volume> volume);
[[nodiscard]] std::shared_ptr<const Volume> volume();
// The air at a point (open where there is no volume).
[[nodiscard]] Sample sample_at(const Vec3& point);

// A window of the volume round a point, for a backend to put on the device (gpu_vulkan/vulkan_air.h - its GLSL twin):
// kWindowCells cells a side across the ground and kWindowLayers up, each kWindowCell units (the GPU's consumers want the
// air's broad shape, not its 16-unit detail), its first cell `first` (absolute, in window cells); its texels RG8 (wind,
// sky), x fastest, then z, then y (the layers one after another) - each the volume sampled at the texel's centre, so a
// linear filter between texel centres is the volume's own trilinear sampling.
inline constexpr int kWindowCells = 128;
inline constexpr int kWindowLayers = 32;
inline constexpr float kWindowCell = 2.0F * kCell;
struct Window {
    std::array<int, 3> first{};
    uint64_t serial = 0; // the volume's it was filled from
    std::vector<uint8_t> texels;
};
[[nodiscard]] Window air_window(const Volume& volume, const Vec3& centre);
// The window's first cell for a centre (the centre in its middle).
[[nodiscard]] std::array<int, 3> window_first(const Vec3& centre);

} // namespace render::air

#pragma once

// NOT ORIGINAL - paint trails ([Graphics] paint_trails, paint_trail_scale, paint_trail_colour; docs/design/vulkan.md
// "Paint trails"): every skater leaves a line of paint in its colour wherever its board touches the level - the ground,
// ramps, rails (a stripe along the rail), walls (a wallride) -, slim as the board while it just rides; once a combo is
// going the line widens and splatters with the combo's size, so the paint shows the combo as it grows. The paint stays
// for the level session - a bail leaves it - and has no score: a picture of the session's lines.
//
// API-neutral: the game hands over what each skater does (GameHooks::paint_brushes - read only: where its board is, the
// surface it is on, its combo), render_pc steps it once a simulation step, and a backend that draws paint (the Vulkan
// Gpu's, gpu_vulkan/vulkan_paint.h) reads the fields kept here. Direct3D 9 draws none: nothing is stamped.
//
// The paint is kept in the surface trails' fields (engine/gfx/surface_trail.h), three of them - the level seen along
// each axis: from above (x, z: the floors, the ramps' gentle parts, the rails' tops) and from the two sides (z, y and x,
// y: the walls and the steep ramps). A stroke goes into each field whose axis the surface faces enough (its normal's
// share kStampShare or more: a ramp's curve into two). A texel holds two words: the coat (RGBA8, the colour
// premultiplied by its coverage) and where the painted surface is along the axis (a float: a drawn surface takes the
// coat only where it is there - the ground under a rail keeps its own, a wall's back none).
//
// Purely visual. Nothing here is read by the game: the physics, the scripts, the scores and the game's random numbers
// are untouched; the splatter has its own random numbers (a PCG32 seeded the same each session). Given the same brushes,
// the same paint: everything is worked out on the processor, once a simulation step, from what the step left.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "engine/gfx/surface_trail.h"

namespace render::paint {

// --- the settings ---------------------------------------------------------------------------------------------------
inline constexpr float kLeastScale = 0.5F;
inline constexpr float kMostScale = 2.0F;
struct Settings {
    bool on = false;          // [Graphics] paint_trails (a backend that draws it set it: Direct3D 9 never does)
    float scale = 1.0F;       // paint_trail_scale: the lines' width and the splatter's reach
    uint32_t colour = 0;      // paint_trail_colour: 0xRRGGBB for this machine's first player; 0 - its slot's colour
    bool operator==(const Settings&) const = default;
};
void set_settings(const Settings& settings);
[[nodiscard]] Settings settings();

// The colour of a player slot (0xRRGGBB): eight, bright and far apart, by slot (an online game's players by their
// object id - the colour each one picked isn't sent between players).
inline constexpr std::size_t kPaletteSize = 8;
[[nodiscard]] uint32_t slot_colour(int slot);
// A colour from its ini text: "auto"/"" (0: the slot's), a palette name (pink, cyan, lime, orange, yellow, violet, red,
// white) or #RRGGBB / RRGGBB. Unknown: 0.
[[nodiscard]] uint32_t colour_from_text(const char* text);

// --- the combo --------------------------------------------------------------------------------------------------------
// The combo's size, 0 - 1, from its running score (its points times its multiplier): log-scaled, so a small combo
// shows at once and a huge one keeps growing - 1000 points 0.18, 10,000 0.41, 100,000 0.67, 1,000,000 0.92, kFullCombo 1.
inline constexpr float kComboUnit = 250.0F;
inline constexpr float kFullCombo = 2000000.0F;
[[nodiscard]] float combo_size(float score);

// --- the fields -------------------------------------------------------------------------------------------------------
inline constexpr float kTexel = 2.0F;    // world units (inches) a texel
inline constexpr int kWindowTiles = 20;  // the backend's window round the camera, tiles a side (2560 units, 65 m)
inline constexpr std::size_t kAxes = 3;  // the fields: along x (the walls facing x), y (from above), z (facing z)
inline constexpr std::size_t kCoatPlane = 0;  // RGBA8: the colour premultiplied by the coverage (alpha)
inline constexpr std::size_t kDepthPlane = 1; // float: the painted surface's place along the axis
inline constexpr std::size_t kPlanes = 2;
inline constexpr float kStampShare = 0.45F;  // a surface facing an axis this much (its normal's share) is painted in its field
inline constexpr float kDrawShare = 0.4F;    // ... and a drawn surface reads a field from this share (backend)
inline constexpr float kSameSurface = 6.0F;  // a coat this near along the axis is on the same surface (painted over)
inline constexpr float kDrawSlack = 6.0F;    // a drawn surface this near the painted place takes its coat (backend; + its slope)
// A point's place on an axis's field (u, v) and along the axis.
[[nodiscard]] surface_trail::Point plane_of(std::size_t axis, const std::array<float, 3>& point);
[[nodiscard]] float depth_of(std::size_t axis, const std::array<float, 3>& point);

// --- the brushes ------------------------------------------------------------------------------------------------------
enum class Contact : uint8_t {
    kNone,   // in the air, walking, bailing, driving: nothing painted
    kGround, // riding the ground or a ramp
    kRail,   // grinding a rail or a ledge, or a lip trick on the coping: a stripe along it
    kWall,   // a wallride
};
// A skater this step, as the game has it.
struct Brush {
    uint64_t id = 0;     // the skater (its object id): its stroke goes on from where it was the step before
    int slot = 0;        // its player slot (its colour)
    bool own = false;    // this machine's first player (paint_trail_colour's colour)
    Contact contact = Contact::kNone;
    std::array<float, 3> at{};              // where its board is (the skater's place)
    std::array<float, 3> normal{0, 1, 0};   // the surface's (the skater's up)
    std::array<float, 3> forward{0, 0, 1};  // the way it faces
    float combo = 0.0F;   // its combo's running score now (points x multiplier; 0: no combo, or not known - a remote one)
    float landed = 0.0F;  // the points of a combo it landed this step (a remote player's, as the game was sent them): a burst
};

// The strokes' rules.
inline constexpr float kBoardHalfWidth = 4.0F;   // a slim line: the board's width
inline constexpr float kComboHalfWidth = 30.0F;  // ... widened this much more by a full combo
inline constexpr float kRailHalfWidth = 1.8F;    // a rail's stripe
inline constexpr float kRailComboHalfWidth = 2.2F;
inline constexpr float kTeleport = 240.0F;       // a move this long in a step: a new line (a restart, a respawn)
inline constexpr float kDropsPerSecond = 90.0F;  // the splatter at a full combo (fewer with a smaller one; none with none)
inline constexpr float kSideReach = 44.0F;       // drops thrown this far to the sides at a full combo ...
inline constexpr float kAheadReach = 80.0F;      // ... and ahead
inline constexpr float kLeastCombo = 0.02F;      // a combo smaller than this: no splatter
inline constexpr std::size_t kMostLandingDrops = 80;

// --- the state, stepped ---------------------------------------------------------------------------------------------
// One simulation step of `seconds`: the brushes' strokes and splatter. A new `level` (render_pc's key of the loaded
// world) starts a new session: no paint, the random numbers seeded afresh. Nothing while paint is off.
void step(std::span<const Brush> brushes, double seconds, uint64_t level);
// The same with the game's brushes (GameHooks::paint_brushes; none installed - none).
void step_from_game(double seconds, uint64_t level);
// What the backend reads, under the state's lock: the three fields (x, y, z).
void read_fields(const std::function<void(std::span<const surface_trail::Field* const, kAxes> fields)>& reader);
// (the log's, the checks') since the session began
struct Counts {
    uint64_t strokes = 0;
    uint64_t drops = 0;
    uint64_t steps = 0;
    uint64_t clears = 0;
};
[[nodiscard]] Counts counts();
// The paint wiped (the script command PaintTrailsClear, the checks'; a new level does it too).
void clear();

} // namespace render::paint

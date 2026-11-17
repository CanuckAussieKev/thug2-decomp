#pragma once

// NOT ORIGINAL - (round light cards) the original's light cards that lie on a surface, as the relit bake's evidence of
// that surface's light. API-neutral: render_pc/rt_record.cpp gathers the cards in the bake, render_pc/relit_bake.cpp puts
// their light on the surfaces under them before the fit.
//
// A light card (rt_lights.h light_card: a world mesh drawn additively, its texture still) is no surface: it adds its
// texture x its colours over whatever is behind it. Most hang in the air round a lamp (a halo, a starburst) and are the
// lamp's evidence. Some are laid on a surface - Training's lower floor is lit by 520 x 1030 floor cards of cracks and
// light (material 7F51D464, blend 2) over vertex colours the bake left dark: the floor's light pool is in the card, not
// in its vertices. The relit level hides every card, so a card's light on a surface must be in that surface's light, or
// the fit reads the floor as unlit and its albedo as near black.
//
// What a surface vertex shows under such cards is (display-referred, as the original blends): its texture x its vertex
// colour (128: 1) + the cards' light there - the same as its texture x (its vertex colour + the cards' light over its
// texture). So the bake's evidence of the surface's light is its vertex colour plus the cards' light divided by its
// texture's colour (its first texture's mean, display-referred): what its vertex colour would be had the bake put that
// light into it. The card's light at a point is its texel x its corners' colours (vertex x pass) x its blend's factor -
// the fixed alpha (blend 2) or the texel's alpha x the vertex alpha (blend 1).
//
// A card lies on a surface where the surface's vertex is within kCardOnSurface of the card's plane, the two face the same
// way (|n . m| at least kCardParallel) and the vertex's own area (a disc of half its edges' mean length, sampled
// kCardSamples x kCardSamples) falls inside the card - the share inside counts. On a surface that isn't a floor (facing
// up by the world's winding at least kCardFloor) the card's luminous core - its texels whose brightest channel is at least
// rt_lights.h kTexelBright, the lamp the card is drawn round (round L) - is the lamp itself, not light on the surface, and
// is left out; on a floor no lamp lies, and the whole card is its light pool.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#include "engine/gfx/rt_relit.h"
#include "engine/gfx/texel_decode.h"

namespace render::rt {

// (the rays' own "on the surface": they start kRelitOffset off it - a card nearer than twice that is, to the analysis,
// laid on the surface, a decal lifted to keep it out of the depth test's way)
inline constexpr float kCardOnSurface = 2.0F * kRelitOffset;
inline constexpr float kCardParallel = 0.95F; // (within about 18 degrees: one plane, not a card standing on it)
inline constexpr float kCardFloor = 0.7F;     // (rt_record.cpp's floor for the textured candidates: the same rule)
inline constexpr uint32_t kCardSamples = 5;
inline constexpr float kCardNearMiss = 4.0F; // (reported) a parallel card this many times kCardOnSurface off: a near miss
inline constexpr uint32_t kNoCardImage = 0xFFFFFFFFU;

struct CardTriangle {
    std::array<Vec3, 3> corners{};
    std::array<std::array<float, 2>, 3> uvs{};
    // each corner's colour (the vertex's x the pass's, display-referred; 1: the texture as it is) x the blend's fixed
    // factor (blend 2), and its alpha (the vertex alpha, doubled: blend 1 multiplies the texel's alpha by it)
    std::array<Rgbf, 3> colours{};
    std::array<float, 3> alphas{1.0F, 1.0F, 1.0F};
    uint32_t image = kNoCardImage; // its texture among LightCards::images (none: white)
    bool texel_alpha = false;      // blend 1 (add by the source's alpha)
    MeshId mesh = 0;
};

struct LightCards {
    std::vector<CardTriangle> triangles;
    std::vector<std::shared_ptr<const texel::Image>> images;
    // each world mesh's (by its id) surface colour: its first texture's mean, display-referred (1: white) - what its
    // vertex colour multiplies; a mesh not listed: white
    std::vector<std::pair<MeshId, Rgbf>> surface_colours;
};

// (reported) A card mesh as the surfaces under it saw it.
struct CardSeen {
    MeshId mesh = 0;
    Vec3 centre{};
    float area = 0.0F;
    float up = 0.0F; // its faces' |n.y|, area-weighted
    float nearest = std::numeric_limits<float>::max(); // the nearest surface vertex under it (within kCardNearMiss x kCardOnSurface)
    std::size_t lying = 0; // its triangles lying on a surface
};

struct CardReport {
    std::size_t cards = 0;       // the card triangles looked at ...
    std::size_t on_surface = 0;  // ... of them lying on a surface (some vertex under them)
    std::size_t vertices = 0;    // the surface vertices given a card's light
    double light = 0.0;          // ... the mean light added (luma, display-referred: 1 = a byte of 128)
    std::vector<CardSeen> seen;
    std::size_t near_misses = 0; // vertices under a parallel card within kCardNearMiss x kCardOnSurface but not within it (reported)
};

// The light the cards lying on each surface vertex show there, in that vertex's colour unit (display-referred, the byte over 128 - as
// the original blends: to add to its colour word), per mesh and vertex (empty for a mesh none lies on). `cards_of`: which meshes are light cards (never
// given a card's light: no surface); `light`: the vertices' normals and which are used (relit_normals); `facing`: the
// world's winding (relit_facing).
[[nodiscard]] std::vector<std::vector<Rgbf>> card_light_on_surfaces(std::span<const RelitMesh> meshes,
                                                                    std::span<const RelitVertices> light,
                                                                    std::span<const uint8_t> cards_of,
                                                                    const LightCards& cards, float facing,
                                                                    CardReport* report = nullptr);

// (round light cards - the art's light cones) How a mesh sits round the axis from `apex` (a lamp) through its vertices'
// mean: a cone or frustum hanging from the lamp has every vertex on the far side (`from` > 0), its radius growing
// linearly along the axis (radius_near < radius_far, the line's misfit `residual` small against the mean radius) and
// its vertices all round the axis (`gap_degrees`: the widest angle round it without a vertex - 90 for a four-sided
// frustum, 180 for a flat card).
struct ConeFit {
    bool fitted = false;
    float from = 0.0F; // the vertices' least and greatest distance along the axis
    float to = 0.0F;
    float radius_near = 0.0F; // the fitted radius there
    float radius_far = 0.0F;
    float residual = 0.0F; // the radii's RMS misfit over their mean
    float gap_degrees = 360.0F; // (over its walls' centres)
    float walls = 0.0F; // the share of its area in walls (facing within kConeWallAlong of across the axis)
};
inline constexpr float kConeWallAlong = 0.7F; // (a triangle's normal along the axis at most this: a wall, not a cap)
[[nodiscard]] ConeFit cone_fit(const RelitMesh& mesh, const Vec3& apex);
// A baked colour word (A8R8G8B8, 128: 1) with `added` light (display-referred, its byte over 128) in it - each channel held at 255 (the
// bake's own range).
[[nodiscard]] uint32_t colour_with_light(uint32_t argb, const Rgbf& added);

} // namespace render::rt

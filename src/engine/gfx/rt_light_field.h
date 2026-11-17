#pragma once

// NOT ORIGINAL - the level's light where a thing is (docs/design/vulkan.md "One lighting path", "Round O: one light for
// everything"): the relit level's light store (engine/gfx/rt_relit.h - linear, in the global light unit) gathered round
// any point, so that what isn't a relit vertex - the skater, the peds, the vehicles, the lit objects, and the props drawn
// with their own baked colours, the particles - is lit by the same light as the level round it, with nothing of its own.
//
// The field holds the relit level's vertices' light, each part, in cells kLightFieldCell units across, and per cell an
// irradiance probe of first order (SH L1: c0 + c1 . n per channel) fitted by least squares over the vertices' normals -
// what a surface facing n there receives - for each of
//   - the sky's light (the sky's b K: the store's traced sky visibility), held toward the open sky's own shape where the
//     vertices don't tell (a surface under an open hemisphere of radiance S receives S (1 + n . up) / 2 - exactly first
//     order);
//   - the bounce (g B, the surroundings' radiance) and the lamps (L x the lamps on - the solved emitters' light), held
//     toward the same light from every side where the vertices don't tell;
// and the direct light it was made for (the sun's or the moon's: toward, its light per unit of N.L - the relit
// lighting's, rt_relit.h RelitLighting). A thing's light (light_at): E(n) = sun x max(n . toward, 0) x its visibility
// (the shadow pass's rays, per pixel) + sky(n) + bounce(n) + lamps(n) x the share the lamps pass doesn't light per pixel
// (rt_lights.h lamps_drawn) - a grey object and a grey wall at one place get one light.
// A point's probe is its cells' (the eight round it, trilinearly, the empty ones left out; else the nearest within
// kLightFieldReach cells; else the level's whole).
// The props drawn with their own baked colours (no store of their own: their colour is tint x the bake's light) take the
// light now over the bake's where they stand (`at` - linear; scaled_colour applies it to a display-referred colour).
// Made by render_pc's rt_relit_world each time the lighting changes (all at once: it reads the model, not the written
// colours), published here, read by every consumer; none while the level isn't relit (the original's look). API-neutral.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace render::rt {

using FieldRgb = std::array<float, 3>;
using FieldPoint = std::array<float, 3>;

inline constexpr float kLightFieldCell = 128.0F; // units a cell (about a skater's height and a half)
inline constexpr int kLightFieldReach = 3;       // cells searched round a point whose own cells are empty
inline constexpr float kLightFieldLeast = 1e-4F;
// The probes' fit: c1 held toward its prior by this x the vertices' count (a regulariser - where the vertices' normals
// don't vary the data says nothing about the other directions; the prior answers there)
inline constexpr float kProbeRidge = 0.01F;

// One vertex's light, each part (linear, in the global unit: what multiplies a surface's albedo).
struct LightParts {
    FieldRgb direct_now{};
    FieldRgb indirect_now{}; // the sky's and the bounce's
    FieldRgb direct_own{};
    FieldRgb indirect_own{};
    FieldRgb local{};   // the lamps' (L x the lamps on)
    FieldRgb sky_now{}; // (round O) the sky's part of indirect_now
    FieldPoint normal{}; // the vertex's (unit; zero: none)
};

// An irradiance probe of first order: what a surface facing n receives, c0 + c1 . n per channel (at least 0).
struct ProbeL1 {
    FieldRgb c0{};
    std::array<FieldPoint, 3> c1{}; // per channel
    [[nodiscard]] FieldRgb at(const FieldPoint& normal) const;
};
// The same as device lights (Direct3D's: an ambient and directional lights, each max(n . toward, 0)): an ambient c0, a
// light toward `toward` of colour k and one from the other side of colour -k - so c0 + k (n . toward) exactly, for the
// part of each channel's c1 along the channels' common direction (their luma's); `lost` the part across it (luma).
struct ProbeLights {
    FieldRgb ambient{};
    FieldPoint toward{0.0F, 1.0F, 0.0F};
    FieldRgb lobe{}; // k (each channel's c1 along `toward`; may be negative)
    float lost = 0.0F;
};
[[nodiscard]] ProbeLights probe_lights(const ProbeL1& probe);

// The light a thing at a point receives (light_at): linear, in the global unit.
struct PointLight {
    FieldPoint toward{0.0F, 1.0F, 0.0F}; // toward the sun or the moon
    FieldRgb sun{};                      // its light per unit of N.L (unshadowed - the shadow pass's rays take it)
    ProbeL1 sky;
    ProbeL1 bounce;
    ProbeL1 lamps;
    bool valid = false;
    // the indirect light and the lamps' share `lamps_share` (1 - rt_lights.h lamps_drawn) as one probe
    [[nodiscard]] ProbeL1 indirect(float lamps_share) const;
    // the whole light on a surface facing `normal` with the sun's visibility `seen`
    [[nodiscard]] FieldRgb on(const FieldPoint& normal, float seen, float lamps_share) const;
};

// (round O) A point's light as Direct3D's lights (render_pc/pc_lights.h OneLightSent): the key the sun (its light per
// unit of N.L), the fill the indirect probe's first-order part toward it (colour k) with its ambient c0, the back the
// same part's other side (colour -k) - directions the way each light travels. Nothing of the game's lights in it: the
// light store's alone. `lamps_share`: the lamps' part the lamps pass doesn't light per pixel (rt_lights.h lamps_drawn).
struct DeviceLights {
    FieldRgb ambient{};
    FieldRgb key{};
    FieldPoint key_travel{0.0F, -1.0F, 0.0F};
    FieldRgb fill{};
    FieldPoint fill_travel{0.0F, -1.0F, 0.0F};
    FieldRgb back{};
    FieldPoint back_travel{0.0F, 1.0F, 0.0F};
    float lost = 0.0F; // (probe_lights' lost)
    // What Direct3D's lighting makes of them on a surface facing `normal` (the key's light x `seen`): ambient + each
    // light x max(n . -travel, 0).
    [[nodiscard]] FieldRgb on(const FieldPoint& normal, float seen) const;
};
[[nodiscard]] DeviceLights device_lights(const PointLight& light, float lamps_share);

struct LightRatio {
    FieldRgb whole{1.0F, 1.0F, 1.0F};
    FieldRgb indirect{1.0F, 1.0F, 1.0F};
    bool operator==(const LightRatio&) const = default;
};

class LightField {
public:
    void add(const FieldPoint& at, const LightParts& parts);
    // The direct light the field was made for (its vertices' direct_now was lit by it).
    void set_sun(const FieldPoint& toward, const FieldRgb& light);
    // The light now over the bake's at a point (linear; the level's whole where no cell is near; 1 for an empty field).
    [[nodiscard]] LightRatio at(const FieldPoint& point) const;
    // The level's whole: every vertex's light summed (what a thing with no place - the particles - takes).
    [[nodiscard]] LightRatio whole() const;
    // The light a thing at a point receives (not valid for an empty field).
    [[nodiscard]] PointLight light_at(const FieldPoint& point) const;
    [[nodiscard]] std::size_t cells() const { return cells_.size(); }
    [[nodiscard]] std::size_t vertices() const { return vertices_; }

private:
    // A probe's fit's sums: per channel [1, n] x the light (the normals' [1, n] [1, n]^T are the cell's).
    using ProbeSums = std::array<std::array<float, 4>, 3>;
    struct Moments {
        std::array<float, 10> normals{}; // [1, n] [1, n]^T's upper triangle
        ProbeSums sky{};
        ProbeSums bounce{};
        ProbeSums lamps{};
    };
    struct Cell {
        LightParts sum;
        Moments moments;
        float count = 0.0F;
    };
    struct Weighted {
        const Cell* cell = nullptr;
        float weight = 0.0F; // (its share, over its count)
    };
    static int64_t key_of(int x, int y, int z);
    [[nodiscard]] const Cell* cell(int x, int y, int z) const;
    // The cells a point takes its light from (the eight round it, else the nearest shell within kLightFieldReach; none:
    // the level's whole).
    [[nodiscard]] std::vector<Weighted> around(const FieldPoint& point) const;
    static LightRatio ratio_of(const LightParts& sum);
    std::unordered_map<int64_t, Cell> cells_;
    Cell all_;
    std::size_t vertices_ = 0;
    FieldPoint sun_toward_{0.0F, 1.0F, 0.0F};
    FieldRgb sun_{};
};

// What the frame's light is where things are: the field made for the lighting now (null: none - the original's look).
void set_light_field(std::shared_ptr<const LightField> field);
[[nodiscard]] std::shared_ptr<const LightField> light_field();
// The ratios at a point (1 without a field).
[[nodiscard]] LightRatio light_ratio_at(const FieldPoint& point);
// The light a thing at a point receives (not valid without a field).
[[nodiscard]] PointLight point_light_at(const FieldPoint& point);
// The level's whole (1 without a field).
[[nodiscard]] LightRatio level_light_ratio();
// A colour's r, g, b bytes (0xAARRGGBB, or as the tod manager keeps them - red low) times a linear light ratio, held to a
// byte; the alpha kept. `red_low`: the red byte the lowest. (The colour is display-referred - rt_display.h: the ratio is
// applied as its display-referred factor, so the colour's light is the linear ratio's.)
[[nodiscard]] uint32_t scaled_colour(uint32_t colour, const FieldRgb& ratio, bool red_low);

} // namespace render::rt

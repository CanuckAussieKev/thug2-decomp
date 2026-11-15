#pragma once

// NOT ORIGINAL - the level's light where a thing is (docs/design/vulkan.md "One lighting path"): the relit level's
// light model (engine/gfx/rt_relit.h) evaluated round any point, so that what the relit mode doesn't relight vertex by
// vertex - the objects' models (doors, hydrants), the props drawn with their own baked colours (tables, chairs), the
// skater, the peds and the vehicles lit by the game's lights, the particles - is lit by the same light as the level
// round it: the same direct light (the sun's or the moon's, where it reaches), the same sky light, the same bounce and
// the same lamps, with nothing of its own.
//
// What a thing shows was authored for the level's own light (its baked colours, or the game's lights for its state):
// the level's own light is the bake's. So the light now over the bake's, where it stands, is what its own light is
// multiplied by - its colours de-lit by the bake's light round it and lit again by the light now. The field holds the
// relit level's vertices' light, each part, in cells kLightFieldCell units across:
//   - the direct light (a x N.L x the sun's visibility: rt_relit.h relit_parts) now and at the bake;
//   - the indirect light (the sky's b K and the bounce's g B) now and at the bake;
//   - the level's own lights (the residual over the tint: the lamps and glows the bake has - constant through the day).
// A point's light is its cells' (the eight round it, trilinearly, the empty ones left out; else the nearest within
// kLightFieldReach cells; else the level's whole), and its ratios:
//   - whole: (direct + indirect + own lights) now over the same at the bake - a baked colour's factor;
//   - indirect: (indirect + own lights) now over at the bake - the game's fill and ambient lights' factor (its key light
//     is the direct light itself: the lighting state's light_colour, its shadows the shadow pass's).
// Made by render_pc's rt_relit_world each time the lighting changes (all at once: it reads the model, not the written
// colours), published here, read by every consumer; none while the level isn't relit - every ratio 1 (the original's
// look). API-neutral.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace render::rt {

using FieldRgb = std::array<float, 3>;
using FieldPoint = std::array<float, 3>;

inline constexpr float kLightFieldCell = 128.0F; // units a cell (about a skater's height and a half)
inline constexpr int kLightFieldReach = 3;       // cells searched round a point whose own cells are empty
inline constexpr float kLightFieldMostRatio = 2.0F; // a ratio at most this (a colour byte's room: 255 of 128)
inline constexpr float kLightFieldLeast = 1e-4F;

// One vertex's light, each part (light: 1 is a colour byte's 128).
struct LightParts {
    FieldRgb direct_now{};
    FieldRgb indirect_now{};
    FieldRgb direct_own{};
    FieldRgb indirect_own{};
    FieldRgb local{}; // the level's own lights
};

struct LightRatio {
    FieldRgb whole{1.0F, 1.0F, 1.0F};
    FieldRgb indirect{1.0F, 1.0F, 1.0F};
    bool operator==(const LightRatio&) const = default;
};

class LightField {
public:
    void add(const FieldPoint& at, const LightParts& parts);
    // The ratios at a point (the level's whole where no cell is near; 1 for an empty field).
    [[nodiscard]] LightRatio at(const FieldPoint& point) const;
    // The level's whole: every vertex's light summed (what a thing with no place - the particles - takes).
    [[nodiscard]] LightRatio whole() const;
    [[nodiscard]] std::size_t cells() const { return cells_.size(); }
    [[nodiscard]] std::size_t vertices() const { return vertices_; }

private:
    struct Cell {
        LightParts sum;
        float count = 0.0F;
    };
    static int64_t key_of(int x, int y, int z);
    [[nodiscard]] const Cell* cell(int x, int y, int z) const;
    static LightRatio ratio_of(const LightParts& sum);
    std::unordered_map<int64_t, Cell> cells_;
    Cell all_;
    std::size_t vertices_ = 0;
};

// What the frame's light is where things are: the field made for the lighting now (null: none - every ratio 1).
void set_light_field(std::shared_ptr<const LightField> field);
[[nodiscard]] std::shared_ptr<const LightField> light_field();
// The ratios at a point (1 without a field).
[[nodiscard]] LightRatio light_ratio_at(const FieldPoint& point);
// The level's whole (1 without a field).
[[nodiscard]] LightRatio level_light_ratio();
// A colour's r, g, b bytes (0xAARRGGBB, or as the tod manager keeps them - red low) times a ratio, held to a byte; the
// alpha kept. `red_low`: the red byte the lowest.
[[nodiscard]] uint32_t scaled_colour(uint32_t colour, const FieldRgb& ratio, bool red_low);

} // namespace render::rt

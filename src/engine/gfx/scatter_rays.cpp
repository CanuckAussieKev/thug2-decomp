// NOT ORIGINAL - the clutter in the rays (round PG): its items as instances of a mesh per part (scatter.h).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_material_table.h"
#include "engine/gfx/scatter.h"

namespace render::scatter {

namespace {

constexpr uint32_t kByte = 0xFFU;
constexpr uint32_t kNibble = 0xFU;
constexpr float kTurns = 256.0F;    // the facing byte: turns / 256
constexpr float kVariants = 15.0F;  // the variant nibble: / 15
constexpr float kHashMost = 255.0F; // the colour hash byte: / 255
constexpr float kLabelPart = 0.5F;  // a vertex's part at least this: the second colour (a can's label)

using Colour = Vec3;

// The clutter's vertex shader's own_colour (vulkan_scatter.cpp kItemVertexBody): the items' printed and dyed colours,
// display-referred. `h` the colour hash (0 - 1); `label` a can's second part.
Colour own_colour(ItemKind kind, float h, bool label)
{
    const auto fract = [](float x) { return x - std::floor(x); };
    const auto mix = [](const Colour& a, const Colour& b, float t) {
        return Colour{core::at(a, 0) + ((core::at(b, 0) - core::at(a, 0)) * t),
                      core::at(a, 1) + ((core::at(b, 1) - core::at(a, 1)) * t),
                      core::at(a, 2) + ((core::at(b, 2) - core::at(a, 2)) * t)};
    };
    constexpr float kQuarter = 0.25F;
    constexpr float kHalf = 0.5F;
    constexpr float kThreeQuarters = 0.75F;
    switch (kind) {
    case ItemKind::kLeaf: {
        Colour a = h < kQuarter ? Colour{0.55F, 0.30F, 0.08F}
                   : h < kHalf  ? Colour{0.62F, 0.48F, 0.12F}
                   : h < kThreeQuarters ? Colour{0.36F, 0.22F, 0.10F}
                                        : Colour{0.32F, 0.36F, 0.12F};
        constexpr float kBase = 0.85F;
        constexpr float kSpread = 0.3F;
        constexpr float kVeins = 7.0F;
        const float factor = kBase + (kSpread * fract(h * kVeins));
        return {core::at(a, 0) * factor, core::at(a, 1) * factor, core::at(a, 2) * factor};
    }
    case ItemKind::kPaper:
        return mix({0.82F, 0.81F, 0.77F}, {0.72F, 0.68F, 0.58F}, fract(h * 3.0F));
    case ItemKind::kCan: {
        if (!label) {
            return {0.6F, 0.6F, 0.62F};
        }
        constexpr float kRed = 0.3F;
        constexpr float kBlue = 0.55F;
        constexpr float kGreen = 0.8F;
        return h < kRed ? Colour{0.62F, 0.08F, 0.06F}
               : h < kBlue ? Colour{0.1F, 0.2F, 0.55F}
               : h < kGreen ? Colour{0.1F, 0.4F, 0.15F}
                            : Colour{0.7F, 0.7F, 0.72F};
    }
    case ItemKind::kPebble: {
        // (the raster takes the gravel's albedo - linear - as it is: here encoded for the material's colour)
        const std::array<float, 2>& range = rt::material_values(rt::MaterialKind::kGravel).albedo;
        const float linear = core::at(range, 0) + ((core::at(range, 1) - core::at(range, 0)) * h);
        const float shown = rt::display_from_linear(linear);
        return {shown, shown, shown};
    }
    default:
        return mix({0.18F, 0.34F, 0.08F}, {0.32F, 0.42F, 0.12F}, h); // a weed
    }
}

ItemKind kind_of(const Item& item)
{
    return static_cast<ItemKind>(std::min<uint32_t>((item.packed >> kItemKindShift) & kNibble,
                                                    static_cast<uint32_t>(ItemKind::kPebble)));
}

} // namespace

std::vector<MeshVertex> ray_part_triangles(std::span<const MeshVertex> meshes, uint32_t part)
{
    std::vector<MeshVertex> out;
    const uint32_t kind = part == kRayCanLabel ? static_cast<uint32_t>(ItemKind::kCan) : part;
    const bool can = kind == static_cast<uint32_t>(ItemKind::kCan);
    const std::size_t first = std::size_t{kind} * kMeshVertices;
    if (first + kMeshVertices > meshes.size()) {
        return out;
    }
    for (std::size_t at = first; at + 3 <= first + kMeshVertices; at += 3) {
        const MeshVertex& a = core::at(meshes, at);
        const MeshVertex& b = core::at(meshes, at + 1);
        const MeshVertex& c = core::at(meshes, at + 2);
        // (a collapsed triangle - the mesh's unused vertices, all at the origin - left out)
        const std::array<float, 3> ab{core::at(b.position, 0) - core::at(a.position, 0),
                                      core::at(b.position, 1) - core::at(a.position, 1),
                                      core::at(b.position, 2) - core::at(a.position, 2)};
        const std::array<float, 3> ac{core::at(c.position, 0) - core::at(a.position, 0),
                                      core::at(c.position, 1) - core::at(a.position, 1),
                                      core::at(c.position, 2) - core::at(a.position, 2)};
        const float cx = (core::at(ab, 1) * core::at(ac, 2)) - (core::at(ab, 2) * core::at(ac, 1));
        const float cy = (core::at(ab, 2) * core::at(ac, 0)) - (core::at(ab, 0) * core::at(ac, 2));
        const float cz = (core::at(ab, 0) * core::at(ac, 1)) - (core::at(ab, 1) * core::at(ac, 0));
        if ((cx * cx) + (cy * cy) + (cz * cz) <= 0.0F) {
            continue;
        }
        const bool label = core::at(a.normal, 3) >= kLabelPart;
        if (can && label != (part == kRayCanLabel)) {
            continue;
        }
        out.push_back(a);
        out.push_back(b);
        out.push_back(c);
    }
    return out;
}

std::span<const uint32_t> ray_parts_of(ItemKind kind)
{
    static constexpr std::array<uint32_t, 2> kCanParts{static_cast<uint32_t>(ItemKind::kCan), kRayCanLabel};
    static constexpr std::array<uint32_t, kItemKinds> kBodies{0, 1, 2, 3, 4};
    if (kind == ItemKind::kCan) {
        return kCanParts;
    }
    return std::span<const uint32_t>(kBodies).subspan(std::min<std::size_t>(static_cast<std::size_t>(kind), kItemKinds - 1), 1);
}

uint32_t ray_shade(const Item& item)
{
    const uint32_t hash = (item.packed >> kItemHashShift) & kByte;
    return std::min(kRayShades - 1, hash * kRayShades / (kByte + 1));
}

Vec3 ray_part_colour(uint32_t part, uint32_t shade)
{
    const float h = (static_cast<float>(std::min(shade, kRayShades - 1)) + 0.5F) / static_cast<float>(kRayShades);
    if (part == kRayCanLabel) {
        return own_colour(ItemKind::kCan, h, true);
    }
    return own_colour(static_cast<ItemKind>(std::min<uint32_t>(part, kItemKinds - 1)), h, false);
}

std::array<std::array<float, 4>, 3> ray_item_transform(const Item& item)
{
    const ItemKind kind = kind_of(item);
    const float variant = static_cast<float>((item.packed >> kItemVariantShift) & kNibble) / kVariants;
    const float turn = static_cast<float>((item.packed >> kItemFacingShift) & kByte) / kTurns * 2.0F *
                       std::numbers::pi_v<float>;
    const float h = static_cast<float>((item.packed >> kItemHashShift) & kByte) / kHashMost;
    // a pebble's own proportions (the shader's local *= ...)
    std::array<float, 3> stretch{1.0F, 1.0F, 1.0F};
    if (kind == ItemKind::kPebble) {
        constexpr float kAcross = 0.75F;
        constexpr float kAcrossSpread = 0.5F;
        constexpr float kHigh = 0.7F;
        constexpr float kHighSpread = 0.6F;
        constexpr float kOther = 5.3F;
        const float other = (variant * kOther) - std::floor(variant * kOther);
        stretch = {kAcross + (kAcrossSpread * variant), kHigh + (kHighSpread * h), kAcross + (kAcrossSpread * other)};
    }
    const float ct = std::cos(item.tilt);
    const float st = std::sin(item.tilt);
    const float c = std::cos(turn);
    const float s = std::sin(turn);
    // turn (about y) x tilt (about x): the shader's tilt first, then the turn
    const std::array<std::array<float, 3>, 3> turned{{{c, -s * st, -s * ct}, {0.0F, ct, -st}, {s, c * st, c * ct}}};
    std::array<std::array<float, 4>, 3> rows{};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t k = 0; k < 3; ++k) {
            core::at(core::at(rows, r), k) = core::at(core::at(turned, r), k) * core::at(stretch, k) * item.size;
        }
        core::at(core::at(rows, r), 3) = core::at(item.root, r);
    }
    return rows;
}

void ray_items(const Scatter& made, const Vec3& camera, float range, std::vector<uint32_t>* out)
{
    out->clear();
    if (!(range > 0.0F)) {
        return;
    }
    const float range2 = range * range;
    for (const Tile& tile : made.tiles) {
        // (the box's nearest point to the camera beyond the range: none of its items is within it)
        float box2 = 0.0F;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const float v = core::at(camera, axis);
            const float below = core::at(tile.low, axis) - v;
            const float above = v - core::at(tile.high, axis);
            const float outside = std::max({below, above, 0.0F});
            box2 += outside * outside;
        }
        if (box2 >= range2) {
            continue;
        }
        const uint32_t end = std::min<uint32_t>(tile.first_item + tile.item_count, static_cast<uint32_t>(made.items.size()));
        for (uint32_t i = tile.first_item; i < end; ++i) {
            const Vec3& root = core::at(made.items, i).root;
            const float dx = core::at(root, 0) - core::at(camera, 0);
            const float dy = core::at(root, 1) - core::at(camera, 1);
            const float dz = core::at(root, 2) - core::at(camera, 2);
            if ((dx * dx) + (dy * dy) + (dz * dz) < range2) { // (the shader's drawn_at: d < the range)
                out->push_back(i);
            }
        }
    }
}

} // namespace render::scatter

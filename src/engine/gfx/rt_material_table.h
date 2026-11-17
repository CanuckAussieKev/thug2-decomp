#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_brdf.h"

// NOT ORIGINAL - round M: the material table (docs/design/vulkan.md "One light model: the BRDF and the material table").
// A surface's light model needs its albedo, F0 and roughness (rt_brdf.h), its normal and height; the weather rounds
// need how much light and air a sheet of it lets through. Where a texture has a surface map (tools/pbr_maps) its
// texels give the normal, the roughness and the height; the albedo is the texture's colour (the de-lit sets take the
// painted light out of it). Everything else comes from the surface's KIND - what it is made of - here: the real
// material's values with where each comes from. Nothing in it is tuned to a look.
//
// Each value's source, by its kind (the `source` text names the publication or measurement):
//   - "scan": the mean of a CC0 photogrammetry scan's roughness map, the scan the detail-material library uses for the
//     kind (Poly Haven, tools/detail_materials/materials.json - measured 2026-11-17 over its 1K roughness map; p10 - p90
//     in the comment);
//   - "index": F0 from the refractive index, ((n - 1) / (n + 1))^2;
//   - "measured": a measured value from the cited paper or standard;
//   - "geometry": derived from the object's dimensions (stated);
//   - "unmeasured": no measured source found - the surface-map generator's class value (tools/pbr_maps/classes.ts),
//     listed in round M's record for a measurement to replace.
// The relief is the height range of the surface's own relief in millimetres (parallax / displacement depth): the map's
// height 0 - 1 spans it.
namespace render::rt {

enum class MaterialKind : uint8_t {
    kPainted = 0, // painted or unclassified surfaces
    kAsphalt,
    kConcreteSmooth,
    kConcreteRough,
    kBrick,
    kStone, // rock, cobbles, stone blocks
    kMetal, // street steel and iron (galvanised, painted over or bare)
    kWood,
    kGround, // dirt, sand
    kGrass,  // grass, leaves, plants
    kTile,   // glazed ceramic tiles, polished floors
    kPlaster,
    kGravel,
    kGlass,
    kWater,
    kSkin,
    kCloth,
    kDenim,
    kLeather,
    kRubber,
    kHair,
    kChainLink, // (round W) a chain-link fence's woven wire
    kGrating,   // (round W) a bar grating (walkways, drains, vents)
    kCount,
};
inline constexpr std::size_t kMaterialKindCount = static_cast<std::size_t>(MaterialKind::kCount);

struct MaterialValues {
    std::string_view name;
    float index = 1.5F;                  // the dielectric's refractive index (0: a metal - metal_f0)
    brdf::Rgb metal_f0{};                // (metals) F0 per channel
    float roughness = 0.5F;              // perceptual, a typical sample's
    std::array<float, 2> albedo{};       // the range real samples' diffuse albedo falls in (reference, not shading input)
    brdf::Rgb scatter_mm{};              // the diffuse mean free path per channel (subsurface scattering), mm
    brdf::Rgb sheen{};                   // the sheen's colour (fibres at grazing angles)
    float sheen_roughness = 0.0F;
    float light_transmission = 0.0F;     // the share of light a sheet of it lets through (cut-outs: their texture's alpha)
    float air_transmission = 0.0F;       // the share of its area open to air (cut-outs: their texture's alpha)
    float relief_mm = 0.0F;              // the height range of its surface relief
    std::string_view source;
};

// Jensen, Marschner, Levoy, Hanrahan 2001, "A Practical Model for Subsurface Light Transport" - skin1's measured
// coefficients (mm^-1): sigma_s' (0.74, 0.88, 1.01), sigma_a (0.032, 0.17, 0.48); the diffuse mean free path
// 1 / sigma_tr, sigma_tr = sqrt(3 sigma_a (sigma_a + sigma_s')): (3.67, 1.37, 0.68) mm.
inline constexpr brdf::Rgb kSkinScatterMm{3.67F, 1.37F, 0.68F};

inline constexpr std::array<MaterialValues, kMaterialKindCount> kMaterialTable{{
    {.name = "painted",
     .index = 1.5F,
     .roughness = 0.72F,
     .albedo = {0.05F, 0.85F},
     .source = "index: binders (acrylic, alkyd) n ~1.5; roughness unmeasured (classes.ts default); albedo: paints span black to "
               "white"},
    {.name = "asphalt",
     .index = 1.6F,
     .roughness = 0.77F,
     .albedo = {0.04F, 0.18F},
     .relief_mm = 1.0F,
     .source = "scan: asphalt_02 roughness 0.770 (p10 0.69, p90 0.84); index: bitumen n ~1.6; albedo: LBNL Heat Island Group "
               "(fresh 0.04-0.05, aged 0.1-0.18); relief: ASTM E965 mean texture depth ~1 mm"},
    {.name = "concrete_smooth",
     .index = 1.5F,
     .roughness = 0.52F,
     .albedo = {0.25F, 0.45F},
     .relief_mm = 0.5F,
     .source = "scan: concrete_floor_02 roughness 0.517 (p10 0.49, p90 0.55); albedo: LBNL (Portland cement concrete "
               "0.25-0.45); relief: ASTM E965, a trowelled slab under 1 mm"},
    {.name = "concrete_rough",
     .index = 1.5F,
     .roughness = 0.75F,
     .albedo = {0.2F, 0.4F},
     .relief_mm = 1.5F,
     .source = "scan: granular_concrete roughness 0.754 (p10 0.75, p90 0.76); albedo: LBNL; relief: ASTM E965 (broomed "
               "concrete 1-2 mm)"},
    {.name = "brick",
     .index = 1.5F,
     .roughness = 0.79F,
     .albedo = {0.2F, 0.4F},
     .relief_mm = 6.0F,
     .source = "scan: brick_wall_02 roughness 0.789 (p10 0.76, p90 0.81); albedo: Oke 1987, Boundary Layer Climates (brick "
               "0.2-0.4); relief: a raked mortar joint recessed 1/4 in (Brick Industry Association Technical Note 7B)"},
    {.name = "stone",
     .index = 1.55F,
     .roughness = 0.96F,
     .albedo = {0.15F, 0.4F},
     .relief_mm = 10.0F,
     .source = "scan: rock_04 roughness 0.956 (p10 0.90, p90 1.0); index: quartz / calcite n 1.55; relief: geometry - sett "
               "and cobble joints about 10 mm deep"},
    {.name = "metal",
     .index = 0.0F,
     .metal_f0 = {0.56F, 0.57F, 0.58F},
     .roughness = 0.59F,
     .albedo = {0.0F, 0.0F},
     .relief_mm = 0.5F,
     .source = "measured: iron F0 (0.56, 0.57, 0.58) (Hoffman 2013, SIGGRAPH Physically Based Shading course; "
               "Real-Time Rendering 4th ed. table 9.2); scan: metal_plate roughness 0.589 (p10 0.49, p90 0.70)"},
    {.name = "wood",
     .index = 1.53F,
     .roughness = 0.89F,
     .albedo = {0.1F, 0.5F},
     .relief_mm = 2.0F,
     .source = "scan: fine_grained_wood roughness 0.891 (p10 0.87, p90 0.91); index: cellulose n 1.53; relief: geometry - "
               "plank gaps and grain about 2 mm"},
    {.name = "ground",
     .index = 1.5F,
     .roughness = 0.94F,
     .albedo = {0.1F, 0.4F},
     .relief_mm = 5.0F,
     .source = "scan: dirt roughness 0.944 (p10 0.93, p90 0.96); albedo: Oke 1987 (dry soil 0.2-0.35, dark soil 0.1, sand "
               "0.4); relief: geometry - clods and stones about 5 mm"},
    {.name = "grass",
     .index = 1.45F,
     .roughness = 0.94F,
     .albedo = {0.15F, 0.26F},
     .light_transmission = 0.1F,
     .air_transmission = 0.0F,
     .relief_mm = 0.0F,
     .source = "scan: sparse_grass roughness 0.943 (p10 0.92, p90 0.96); index: leaf cuticle n ~1.45; albedo: Oke 1987 "
               "(grass 0.16-0.26); light transmission: a leaf's in the visible ~0.1 (Gates et al. 1965, Applied Optics 4)"},
    {.name = "tile",
     .index = 1.5F,
     .roughness = 0.70F,
     .albedo = {0.3F, 0.8F},
     .relief_mm = 2.0F,
     .source = "scan: interior_tiles roughness 0.696 (p10 0.67, p90 0.72); index: glaze (silicate glass) n ~1.5; relief: "
               "geometry - grout joints about 2 mm below the tile"},
    {.name = "plaster",
     .index = 1.5F,
     .roughness = 0.91F,
     .albedo = {0.4F, 0.85F},
     .relief_mm = 1.0F,
     .source = "scan: plastered_wall roughness 0.912 (p10 0.91, p90 0.92); index: gypsum / lime n ~1.52"},
    {.name = "gravel",
     .index = 1.55F,
     .roughness = 0.51F,
     .albedo = {0.15F, 0.35F},
     .relief_mm = 8.0F,
     .source = "scan: gravel_floor roughness 0.506 (p10 0.46, p90 0.55); relief: geometry - half a 16 mm aggregate"},
    {.name = "glass",
     .index = 1.52F,
     .roughness = 0.0F,
     .albedo = {0.0F, 0.0F},
     .light_transmission = 0.89F,
     .air_transmission = 0.0F,
     .source = "index: soda-lime float glass n 1.52; roughness: a polished float surface (nm-scale: the numerical floor); "
               "light transmission: 4 mm clear float glass's visible transmittance ~0.89 (EN 410)"},
    {.name = "water",
     .index = 1.333F,
     .roughness = 0.0F,
     .albedo = {0.0F, 0.0F},
     .light_transmission = 1.0F,
     .air_transmission = 0.0F,
     .source = "index: water n 1.333 (IAPWS); a calm surface (the wind's waves are the water's own model)"},
    {.name = "skin",
     .index = 1.4F,
     .roughness = 0.55F,
     .albedo = {0.1F, 0.6F},
     .scatter_mm = kSkinScatterMm,
     .source = "index: skin n 1.4 (Tuchin, Tissue Optics); scatter: Jensen et al. 2001 skin1 (above); roughness "
               "unmeasured (classes.ts skin)"},
    {.name = "cloth",
     .index = 1.53F,
     .roughness = 0.88F,
     .albedo = {0.03F, 0.85F},
     .sheen = {0.04F, 0.04F, 0.04F},
     .sheen_roughness = 0.6F,
     .source = "index: cellulose / polyester n 1.53-1.57 (the fibres' reflectance, the sheen's colour (0.04)); roughness "
               "and the sheen's roughness unmeasured (classes.ts cloth)"},
    {.name = "denim",
     .index = 1.53F,
     .roughness = 0.85F,
     .albedo = {0.03F, 0.6F},
     .sheen = {0.04F, 0.04F, 0.04F},
     .sheen_roughness = 0.75F,
     .source = "index: cotton (cellulose) n 1.53 (the sheen's colour); roughness unmeasured (classes.ts denim)"},
    {.name = "leather",
     .index = 1.5F,
     .roughness = 0.45F,
     .albedo = {0.03F, 0.5F},
     .source = "index: a finished leather's lacquer n ~1.5; roughness unmeasured (classes.ts leather)"},
    {.name = "rubber",
     .index = 1.52F,
     .roughness = 0.75F,
     .albedo = {0.02F, 0.5F},
     .source = "index: natural rubber n 1.52; roughness unmeasured (classes.ts rubber)"},
    {.name = "hair",
     .index = brdf::kHairIndex,
     .roughness = 0.42F,
     .albedo = {0.02F, 0.6F},
     .source = "index: keratin n 1.55, cuticle tilt -5 to -10 degrees (Marschner et al. 2003); roughness unmeasured "
               "(classes.ts hair)"},
    // (round W - the air's transport) screens: what a sheet of them lets through is their open area
    {.name = "chain_link",
     .index = 0.0F,
     .metal_f0 = {0.56F, 0.57F, 0.58F},
     .roughness = 0.59F,
     .albedo = {0.0F, 0.0F},
     .light_transmission = 0.86F,
     .air_transmission = 0.86F,
     .source = "geometry: ASTM F668 / CLFMI standard fence fabric - 2 in (50.8 mm) mesh of 9 gauge (3.76 mm) galvanised "
               "wire: open area (1 - 3.76 / 50.8)^2 = 0.857 for light and air alike; F0 / roughness as street steel"},
    {.name = "grating",
     .index = 0.0F,
     .metal_f0 = {0.56F, 0.57F, 0.58F},
     .roughness = 0.59F,
     .albedo = {0.0F, 0.0F},
     .light_transmission = 0.79F,
     .air_transmission = 0.79F,
     .source = "geometry: NAAMM MBG 531 welded bar grating 19-W-4 - 3/16 in bars at 1-3/16 in centres, cross bars 1/4 in "
               "at 4 in: open area (1 - 4.76 / 30.2) x (1 - 6.35 / 101.6) = 0.79; F0 / roughness as street steel"},
}};

[[nodiscard]] inline const MaterialValues& material_values(MaterialKind kind)
{
    return core::at(kMaterialTable, static_cast<std::size_t>(kind));
}
// A kind's F0 per channel: a metal's measured F0, a dielectric's from its index.
[[nodiscard]] inline brdf::Rgb material_f0(MaterialKind kind)
{
    const MaterialValues& values = material_values(kind);
    if (values.index <= 0.0F) {
        return values.metal_f0;
    }
    const float f0 = brdf::f0_of_index(values.index);
    return {f0, f0, f0};
}
[[nodiscard]] inline bool material_is_metal(MaterialKind kind) { return material_values(kind).index <= 0.0F; }

// (round W) The kind a collision face's terrain says it is made of - the game's terrain names (its terrain table,
// TERRAIN_*: what the skater's wheels sound like on it - so what it is made of). A terrain not listed: painted / solid.
// The air's transport (air_volume.h) reads each face's air_transmission by it: a chain-link fence and a grating let air
// through, plexiglass (glass) and everything else none.
[[nodiscard]] inline MaterialKind kind_of_terrain(std::string_view terrain)
{
    struct Named {
        std::string_view name;
        MaterialKind kind;
    };
    static constexpr std::array<Named, 22> kNames{{
        {"TERRAIN_CONCSMOOTH", MaterialKind::kConcreteSmooth}, {"TERRAIN_CONCROUGH", MaterialKind::kConcreteRough},
        {"TERRAIN_WETCONC", MaterialKind::kConcreteSmooth},    {"TERRAIN_SIDEWALK", MaterialKind::kConcreteRough},
        {"TERRAIN_METALSMOOTH", MaterialKind::kMetal},         {"TERRAIN_METALROUGH", MaterialKind::kMetal},
        {"TERRAIN_METALCORRUGATED", MaterialKind::kMetal},     {"TERRAIN_METALTIN", MaterialKind::kMetal},
        {"TERRAIN_METALFUTURE", MaterialKind::kMetal},         {"TERRAIN_METALGRATING", MaterialKind::kGrating},
        {"TERRAIN_CHAINLINK", MaterialKind::kChainLink},       {"TERRAIN_WOOD", MaterialKind::kWood},
        {"TERRAIN_WOODPIER", MaterialKind::kWood},             {"TERRAIN_BRICK", MaterialKind::kBrick},
        {"TERRAIN_TILE", MaterialKind::kTile},                 {"TERRAIN_ASPHALT", MaterialKind::kAsphalt},
        {"TERRAIN_ROCK", MaterialKind::kStone},                {"TERRAIN_GRAVEL", MaterialKind::kGravel},
        {"TERRAIN_GRASS", MaterialKind::kGrass},               {"TERRAIN_DIRT", MaterialKind::kGround},
        {"TERRAIN_WATER", MaterialKind::kWater},               {"TERRAIN_PLEXIGLASS", MaterialKind::kGlass},
    }};
    for (const Named& named : kNames) {
        if (named.name == terrain) {
            return named.kind;
        }
    }
    return MaterialKind::kPainted;
}

// The kind of a surface map's class (formats/surface_map_format.h MaterialClass: 0 - 17) and of a detail-material kind
// (formats/detail_material_format.h Kind: 0 - 12) - the numbers as those files define them (an unknown one: painted).
[[nodiscard]] inline MaterialKind kind_of_map_class(uint8_t material_class)
{
    static constexpr std::array<MaterialKind, 18> kByClass{
        MaterialKind::kPainted, MaterialKind::kAsphalt, MaterialKind::kConcreteSmooth, MaterialKind::kStone,
        MaterialKind::kMetal,   MaterialKind::kWood,    MaterialKind::kGlass,          MaterialKind::kWater,
        MaterialKind::kGrass,   MaterialKind::kGround,  MaterialKind::kCloth,          MaterialKind::kTile,
        MaterialKind::kSkin,    MaterialKind::kCloth,   MaterialKind::kLeather,        MaterialKind::kDenim,
        MaterialKind::kRubber,  MaterialKind::kHair};
    return material_class < kByClass.size() ? core::at(kByClass, material_class) : MaterialKind::kPainted;
}
[[nodiscard]] inline MaterialKind kind_of_detail(uint8_t detail_kind)
{
    static constexpr std::array<MaterialKind, 13> kByDetail{
        MaterialKind::kPainted, MaterialKind::kAsphalt, MaterialKind::kConcreteSmooth, MaterialKind::kConcreteRough,
        MaterialKind::kBrick,   MaterialKind::kStone,   MaterialKind::kMetal,          MaterialKind::kWood,
        MaterialKind::kGround,  MaterialKind::kGrass,   MaterialKind::kTile,           MaterialKind::kPlaster,
        MaterialKind::kGravel};
    return detail_kind < kByDetail.size() ? core::at(kByDetail, detail_kind) : MaterialKind::kPainted;
}

} // namespace render::rt

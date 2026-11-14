#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// NOT ORIGINAL - surface maps: the normal, roughness and height maps made offline from the game's own textures
// (tools/pbr_maps, `unpack_game.ts --pbr-maps`) for the Vulkan renderer's per-pixel lighting ([Graphics] rt_pbr;
// docs/design/vulkan.md "Per-pixel lighting and surface maps"). One file beside each texture dictionary - the
// dictionary's name with ".pbr" added ("levels/bo/bo.tex.xbx" -> "levels/bo/bo.tex.xbx.pbr") - holding a map for some
// of its textures (the generator skips text, decals, UI pictures and atlases).
//
// The file (little-endian): u32 magic "T2SM", u32 version (1), u32 count, u32 reserved (0); then `count` entries of
// 32 bytes:
//   u32 the texture's checksum (as in the dictionary)
//   u64 the texture's content hash: content_hash() of its first level's bytes as the dictionary stores them - a map
//       is used only for the texels it was made from (a texture replaced later - an upscaled one, a mod's - keeps none)
//   u16 width, u16 height (the map's; any size - the texture's or not)
//   u8 material class (MaterialClass), u8 generator (Generator), u8 flags (kFlag*; 0 before the characters' maps),
//   u8 reserved (0)
//   f32 F0: the reflectance at normal incidence (0 - 1) the class gives
//   u32 the texels' offset from the file's start, u32 their size (width x height x 4)
// and the texels: rows top first (the texture's v growing downward, as its own rows), 4 bytes each -
//   R, G: the normal's tangent-space x (along +u) and y (along +v), 0 - 255 for -1 - 1 (z = sqrt(1 - x^2 - y^2))
//   B: roughness 0 - 1 (perceptual: GGX's alpha is its square)
//   A: height 0 - 1 (0.5: the surface; for displacement - not read by the lighting)
namespace surface_map_format {

inline constexpr uint32_t kMagic = 0x4D533254U; // "T2SM"
inline constexpr uint32_t kVersion = 1;
inline constexpr std::size_t kHeaderSize = 16;
inline constexpr std::size_t kEntrySize = 32;
inline constexpr std::size_t kTexelBytes = 4;
inline constexpr uint32_t kLargestSide = 8192;

// What a texture shows, as the generator classified it - it sets the roughness range and F0 (tools/pbr_maps
// classes.ts). The numbers are the file's; unknown values read as kDefault.
enum class MaterialClass : uint8_t {
    kDefault = 0, // painted or unclassified
    kAsphalt = 1,
    kConcrete = 2,
    kStone = 3, // brick, rock, stone tiles
    kMetal = 4,
    kWood = 5,
    kGlass = 6,
    kWater = 7,
    kFoliage = 8, // grass, leaves, plants
    kGround = 9,  // dirt, sand, gravel
    kFabric = 10, // cloth, skin, characters
    kTile = 11,   // glazed tiles, polished floors
    // (added for the characters - [Graphics] rt_characters; tools/pbr_maps/models.ts) the character models' textures
    kSkin = 12,
    kCloth = 13,
    kLeather = 14,
    kDenim = 15,
    kRubber = 16, // soles, pads
    kHair = 17,
};
inline constexpr uint8_t kClassCount = 18;

// (added for the characters) An entry's flags: made for a character model's dictionary (used only with
// [Graphics] rt_characters on); a hair texture whose strands run along u (else along v); a garment whose skin-coloured
// texels are shaded as skin (an atlas of a shirt and the arms in it).
inline constexpr uint8_t kFlagModel = 0x01;
inline constexpr uint8_t kFlagStrandsU = 0x02;
inline constexpr uint8_t kFlagSkinTexels = 0x04;

enum class Generator : uint8_t {
    kClassical = 0, // height from luminance, normals by a Sobel filter (tools/pbr_maps/classical.ts)
    kChord = 1,     // Ubisoft La Forge's CHORD model (the player's own run; research-only weights)
};

struct Entry {
    uint32_t checksum = 0;
    uint64_t content_hash = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    MaterialClass material_class = MaterialClass::kDefault;
    Generator generator = Generator::kClassical;
    uint8_t flags = 0; // (added for the characters) kFlag*
    float f0 = 0.0F;
    uint32_t offset = 0;
    uint32_t bytes = 0;
};

// 64-bit FNV-1a of `bytes` (offset basis 0xcbf29ce484222325, prime 0x100000001b3) - the content hash.
[[nodiscard]] uint64_t content_hash(std::span<const uint8_t> bytes);

// The file's entries; false (and none) if it isn't one, is of another version, or an entry's texels lie outside it or
// don't match its size. A file with no entries is valid.
[[nodiscard]] bool read_entries(std::span<const uint8_t> file, std::vector<Entry>* entries);

// The file made from entries and their texels (tests and tools; the generator writes the same bytes from TypeScript).
// `texels` holds each entry's texels in order; offsets are filled in.
[[nodiscard]] std::vector<uint8_t> write_file(std::span<const Entry> entries, std::span<const std::vector<uint8_t>> texels);

// The texture's map in `entries` (by checksum and content hash), or null.
[[nodiscard]] const Entry* find(std::span<const Entry> entries, uint32_t checksum, uint64_t content_hash);

} // namespace surface_map_format

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// NOT ORIGINAL - the detail-material library: a tiling high-resolution detail layer and a real material (roughness,
// F0, metalness) per surface kind, and which kind each of the levels' textures is, for the Vulkan renderer's detail
// layer ([Graphics] rt_detail; docs/design/vulkan.md "Detail materials and de-lighting"). Made on the player's machine
// by tools/detail_materials (`detail_materials.ts pack`) from CC0 material scans it downloads (its materials.json:
// the URLs, hashes and licences) and the player's own levels; one file in the game folder,
// thug2-decomp-materials.t2dm.
//
// The file (little-endian): u32 magic "T2DM", u32 version (1), u32 kind count, u32 assignment count, 16 bytes reserved
// (0); then `kind count` entries of 48 bytes:
//   u8 the kind (Kind), u8 reserved (0), u16 the side of its square texture (a power of two, 4 - 4096)
//   f32 the metres one tile of it covers (the scan's size; kept for the tools)
//   f32 roughness (perceptual), f32 the detail's roughness swing, f32 F0, f32 metalness (0 - 1), f32 how far the kind's
//       roughness replaces a surface map's (0 - 1)
//   u32 the texels' offset from the file's start, u32 their size (side x side x 4), 12 bytes reserved (0)
// then `assignment count` entries of 24 bytes:
//   u32 the texture's checksum, u64 its content hash (surface_map_format::content_hash of its first level as the
//   dictionary stores it), u8 its kind, u8 the evidence (Evidence), u16 reserved (0), f32 the detail's tiles per unit of
//   the texture's coordinates (0: the default), f32 the texture's mean luminance 0 - 1 (< 0: unknown)
// and the texels, rows top first, 4 bytes each: R, G the detail's high-passed tangent-space normal x (along +u), y (along
// +v) as 0 - 255 for -1 - 1; B its roughness's swing about 0.5; A its albedo's luminance over its local mean x 0.5
// (0.5: unchanged).
namespace detail_material_format {

inline constexpr uint32_t kMagic = 0x4D443254U; // "T2DM"
inline constexpr uint32_t kVersion = 1;
inline constexpr std::size_t kHeaderSize = 32;
inline constexpr std::size_t kKindEntrySize = 48;
inline constexpr std::size_t kAssignmentSize = 24;
inline constexpr std::size_t kTexelBytes = 4;
inline constexpr uint32_t kLargestSide = 4096;

// A surface kind (the numbers are the file's; tools/detail_materials/kinds.ts has the same list).
enum class Kind : uint8_t {
    kNone = 0,
    kAsphalt = 1,
    kConcreteSmooth = 2, // smooth concrete, sidewalks
    kConcreteRough = 3,
    kBrick = 4,
    kStone = 5, // rock, cobbles, stone blocks
    kMetal = 6,
    kWood = 7,
    kGround = 8, // dirt, sand
    kGrass = 9,
    kTile = 10, // glazed tiles, polished floors
    kPlaster = 11, // painted and plastered walls
    kGravel = 12,
};
inline constexpr uint8_t kKindCount = 13;

// How a texture's kind was decided.
enum class Evidence : uint8_t {
    kClass = 0,   // its surface map's class (the texture's pixels and its level's materials)
    kTerrain = 1, // the terrain types of the collision faces drawn with it
};

struct KindEntry {
    Kind kind = Kind::kNone;
    uint16_t side = 0;
    float tile_metres = 0.0F;
    float roughness = 0.0F;
    float roughness_swing = 0.0F;
    float f0 = 0.0F;
    float metalness = 0.0F;
    float roughness_weight = 0.0F;
    uint32_t offset = 0;
    uint32_t bytes = 0;
};

struct Assignment {
    uint32_t checksum = 0;
    uint64_t content_hash = 0;
    Kind kind = Kind::kNone;
    Evidence evidence = Evidence::kClass;
    float repeats = 0.0F;
    float mean_luma = -1.0F;
};

struct Library {
    std::vector<KindEntry> kinds;
    std::vector<Assignment> assignments;
};

// The file's tables; false (and none) if it isn't one, is of another version, or a kind's texels lie outside it or
// don't match its side. A file with no kinds or assignments is valid.
[[nodiscard]] bool read_library(std::span<const uint8_t> file, Library* library);

// The file made from the tables and each kind's texels in order (tests and tools; the packer writes the same bytes from
// TypeScript). Offsets are filled in.
[[nodiscard]] std::vector<uint8_t> write_file(const Library& library, std::span<const std::vector<uint8_t>> texels);

} // namespace detail_material_format

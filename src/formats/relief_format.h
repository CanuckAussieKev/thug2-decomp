#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// NOT ORIGINAL - building relief: the facade structure of a level's building-front textures (windows, frames, sills,
// floor bands as rectangles at heights off the wall), made offline from the game's own textures
// (tools/pbr_maps/building_relief.ts) for the relief geometry ([Graphics] building_relief; engine/gfx/facade_relief.h,
// docs/design/vulkan.md "Building relief"). One file beside each texture dictionary - its name with ".relief" added
// ("levels/bo/bo.tex.xbx" -> "levels/bo/bo.tex.xbx.relief") - holding an entry for those of its textures that show a
// building front.
//
// The file (little-endian): u32 magic "T2RF", u32 version (1), u32 count, u32 the detector's version, u64 the sources'
// hash (the tool's, to skip what is made); then `count` entries of 24 bytes -
//   u32 the texture's checksum, u64 its content hash (surface_map_format::content_hash of its first level as the game's
//   own dictionary stores it: an entry is used only for the texels it was made from), u16 the cell's repeats across the
//   texture, u16 down, u32 its first rectangle, u32 how many
// - and the rectangles, 24 bytes each: u8 kind (Kind), 3 bytes 0, f32 u0, v0, u1, v1 (fractions of the cell, u0 < u1,
// v0 < v1, at most a cell wide - u1 / v1 may pass 1: it wraps into the next cell), f32 height (in texture widths: a
// texture drawn 300 units across turns 0.01 into 3 units; + out of the wall, - into it). Later rectangles lie over
// earlier ones.
namespace relief_format {

inline constexpr uint32_t kMagic = 0x46523254U; // "T2RF"
inline constexpr uint32_t kVersion = 1;
inline constexpr std::size_t kHeaderSize = 24;
inline constexpr std::size_t kEntrySize = 24;
inline constexpr std::size_t kRectSize = 24;
inline constexpr uint16_t kMostRepeats = 64;
inline constexpr float kMostHeight = 1.0F; // texture widths

enum class Kind : uint8_t {
    kBand = 1,   // a floor band or cornice, standing out
    kFrame = 2,  // a window's frame, raised
    kSill = 3,   // a window's sill, standing out
    kWindow = 4, // the window, recessed
};

struct Rect {
    Kind kind = Kind::kWindow;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;
    float height = 0.0F;
};

struct Entry {
    uint32_t checksum = 0;
    uint64_t content_hash = 0;
    uint16_t repeat_u = 1;
    uint16_t repeat_v = 1;
    uint32_t first = 0; // into File::rects
    uint32_t count = 0;
};

struct File {
    uint32_t detector = 0;
    uint64_t sources = 0;
    std::vector<Entry> entries;
    std::vector<Rect> rects;
};

// The file read; false (and nothing) if it isn't one, is of another version, or an entry or rectangle is damaged (its
// rectangles outside the file, a kind unknown, a value not finite or out of range). A file with no entries is valid.
[[nodiscard]] bool read_file(std::span<const uint8_t> file, File* out);

// The file's bytes (the checks; the tool writes the same from TypeScript - tools/pbr_maps/relief_file.ts).
[[nodiscard]] std::vector<uint8_t> write_file(const File& file);

// The texture's entry (by checksum and content hash), or null.
[[nodiscard]] const Entry* find(const File& file, uint32_t checksum, uint64_t content_hash);

} // namespace relief_format

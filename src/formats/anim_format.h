#pragma once

#include <cstddef>
#include "engine/core/checked_index.h"
#include <span>
#include <cstdint>
#include <array>

#include "formats/byte_stream.h"
#include <cstring>
#include "engine/core/pointer_cell.h"

// NOT ORIGINAL - the animation format (.ska, PC: "x.ska.xbx"; also cutscenes' camera / object animations), its
// headers and custom keys read through util::ByteReader and its sections laid out and checked before the frame data
// (gfx::JointClipFrames, engine/gfx/boned_anim_frame_data*.cpp) uses them in place.
//
// An animation (little-endian; sections aligned to 4 as addresses - the file is loaded 16-byte aligned):
//   u32 version, u32 flags, u32 duration (a float's bits), then by the flags:
//   float keys (flags & kFloatKeys): u32 bone count, u32 rotation key count, u32 translation key count, u32 custom
//     key count; [kBoneNames: a name checksum per bone]; [kPartial: the partial-animation bits - i32 count then
//     ((count - 1) / 32 + 1) u32s]; each bone's {rotation keys, translation keys} counts (u8 each, i16 with
//     kWideCounts); 4-aligned: the rotation keys then the translation keys (16 bytes each with kWideKeys or
//     kBoneNames, else 8); 4-aligned: the custom keys
//   compressed (flags & kCompressed, without kFloatKeys): u32 bone count, rotation key count, translation key count,
//     custom key count, rotation keys' size, translation keys' size; each bone's rotation keys' size (u16), then each
//     bone's translation keys' size (u16); 4-aligned: [kKeyTable: a 0x600-byte table]; [kPartial: as above];
//     4-aligned: unless kWideKeys or kBoneNames, the rotation keys then the translation keys (their sizes); 4-aligned:
//     the custom keys
//   a custom key: i32 frame, u32 kind, u32 size, then the kind's data (1: a float field of view; 2 / 7: a camera's
//     position and rotation, 8 floats; 3: 32 bytes; 4 / 5 / 6: one u32; others: `size` bytes, skipped)
namespace anim_format {

constexpr int kBitsPerByte = 8;
constexpr std::size_t kCustomKeyValues = 8; // the most floats a custom key carries

constexpr uint32_t kKeyTable = 0x2000;
constexpr uint32_t kPartial = 0x80000;
constexpr uint32_t kWideCounts = 0x400000;
constexpr uint32_t kCompressed = 0x800000;
constexpr uint32_t kBoneNames = 0x1000000;
constexpr uint32_t kWideKeys = 0x8000000;
constexpr uint32_t kFloatKeys = 0x10000000;

constexpr size_t kHeaderSize = 0xC;

struct AnimHeader {
    uint32_t version = 0;
    uint32_t flags = 0;
    uint32_t duration = 0;
    // the keys' header
    uint32_t bone_count = 0;
    uint32_t rotation_key_count = 0;
    uint32_t translation_key_count = 0;
    uint32_t custom_key_count = 0;
    uint32_t rotation_size = 0;    // compressed only
    uint32_t translation_size = 0; // compressed only
};

// The sections as addresses in the animation loaded at its base (0: not there).
struct AnimLayout {
    uintptr_t keys_header = 0;
    uintptr_t rotation_sizes = 0;    // compressed
    uintptr_t translation_sizes = 0; // compressed
    uintptr_t key_table = 0;         // compressed, kKeyTable
    uintptr_t bone_names = 0;        // float keys, kBoneNames
    uintptr_t partial_bones = 0;     // kPartial
    uintptr_t key_counts = 0;        // float keys
    uintptr_t rotation_keys = 0;
    uintptr_t translation_keys = 0;
    uintptr_t custom_keys = 0;
    uintptr_t end = 0; // the animation's end
};

// The sections as the frame data holds them (in place: the loaded animation's own bytes).
inline uint16_t* rotation_size_list(const AnimLayout& layout) { return cell::at_address<uint16_t>(layout.rotation_sizes); }
inline uint16_t* translation_size_list(const AnimLayout& layout)
{
    return cell::at_address<uint16_t>(layout.translation_sizes);
}
inline uint8_t* key_table_bytes(const AnimLayout& layout) { return cell::at_address<uint8_t>(layout.key_table); }
inline uint32_t* bone_name_list(const AnimLayout& layout) { return cell::at_address<uint32_t>(layout.bone_names); }
inline uint32_t* partial_bone_bits(const AnimLayout& layout)
{
    return cell::at_address<uint32_t>(layout.partial_bones);
}
inline uint8_t* key_count_bytes(const AnimLayout& layout) { return cell::at_address<uint8_t>(layout.key_counts); }
inline uint8_t* rotation_key_bytes(const AnimLayout& layout) { return cell::at_address<uint8_t>(layout.rotation_keys); }
inline uint8_t* translation_key_bytes(const AnimLayout& layout)
{
    return cell::at_address<uint8_t>(layout.translation_keys);
}
// The custom keys up to the animation's end.
inline util::ByteReader custom_key_reader(const AnimLayout& layout)
{
    return {cell::at_address<const uint8_t>(layout.custom_keys), layout.end - layout.custom_keys};
}

// A bone's two key counts with kWideCounts: i16 each (`index`: 2 * bone, + 1 for the translations).
inline int wide_key_count(std::span<const uint8_t> counts, size_t index)
{
    return static_cast<int16_t>(core::at(counts, 2 * index) | core::at(counts, (2 * index) + 1) << kBitsPerByte);
}

// A key with floats (a quaternion's x, y, z, w or a translation's x, y, z after the time word), as the file keeps it.
struct FloatKey {
    uint16_t time;
    uint16_t pad;
    std::array<float, 4> value;
};
constexpr std::size_t kFloatKeyBytes = 20;

static_assert(sizeof(FloatKey) == kFloatKeyBytes, "file format: a float key");
// The float key at `key`, and one written back there (the frame data changes keys in place).
inline FloatKey float_key_at(const uint8_t* key)
{
    FloatKey out{};
    std::memcpy(&out, key, sizeof(out));
    return out;
}
inline void store_float_key(uint8_t* key, const FloatKey& value) { std::memcpy(key, &value, sizeof(value)); }
// A key's time word (the first 16 bits).
inline uint16_t key_time_word(const uint8_t* key)
{
    uint16_t word = 0;
    std::memcpy(&word, key, sizeof(word));
    return word;
}

// The headers read and the sections laid out as the frame data lays them out. `size`: the animation's bytes (0: not
// known - nothing checked past the headers). False when it is neither float keys nor compressed, a count is negative
// as the frame data keeps it (16 bits), or a section isn't within the animation.
bool lay_out_animation(const uint8_t* base, size_t size, AnimHeader* header, AnimLayout* layout);

// The per-bone key counts / sizes against the totals: false if any bone's keys run past them.
bool check_bone_keys(const AnimHeader& header, const AnimLayout& layout);

struct CustomKey {
    int32_t frame = 0;
    uint32_t kind = 0;
    uint32_t size = 0;
    std::array<float, kCustomKeyValues> values = {}; // 1: [0] the field of view; 2 / 7: [0..3] the position, [4..7] the rotation
    uint32_t value = 0;   // 4 / 5 / 6
};
// One custom key (the reader past it); false if it runs past the end.
bool read_custom_key(util::ByteReader& reader, CustomKey* out);

} // namespace anim_format

#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "formats/byte_stream.h"

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

constexpr uint32_t kKeyTable = 0x2000;
constexpr uint32_t kPartial = 0x80000;
constexpr uint32_t kWideCounts = 0x400000;
constexpr uint32_t kCompressed = 0x800000;
constexpr uint32_t kBoneNames = 0x1000000;
constexpr uint32_t kWideKeys = 0x8000000;
constexpr uint32_t kFloatKeys = 0x10000000;

constexpr size_t kHeaderSize = 0xC;

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
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
// NOLINTEND(readability-identifier-naming)

// The sections as addresses in the animation loaded at its base (0: not there).
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
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
// NOLINTEND(readability-identifier-naming)

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
    std::array<float, 8> values = {}; // 1: [0] the field of view; 2 / 7: [0..3] the position, [4..7] the rotation
    uint32_t value = 0;   // 4 / 5 / 6
};
// One custom key (the reader past it); false if it runs past the end.
bool read_custom_key(util::ByteReader& reader, CustomKey* out);

} // namespace anim_format

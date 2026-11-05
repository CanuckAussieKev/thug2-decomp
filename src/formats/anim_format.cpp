#include "formats/anim_format.h"
#include "base/core/ints.h"
#include "base/core/pointer_cell.h"
#include "base/core/checked_index.h"

#include <cstddef>
#include <cstdint>

namespace anim_format {

constexpr int kFlagsPerWord = 32;          // a bone's key flags are bits of 32-bit words
constexpr int kFlagTableHeaderBytes = 8;
constexpr int kFloatKeysHeaderBytes = 0x10;
constexpr int kPackedKeysHeaderBytes = 0x18;
constexpr int kKeyTableBytes = 0x600;
// The custom keys' kinds beyond those 1-4: 7 reads like 2 (position and rotation), 5 and 6 a word like 4; 3 skips 32 bytes.
constexpr int kKindPositionRotationAlt = 7;
constexpr int kKindWord5 = 5;
constexpr int kKindWord6 = 6;
constexpr int kKindSkippedBytes = 32;

namespace {

uintptr_t align4(uintptr_t at) { return (at + 3) & ~static_cast<uintptr_t>(3); }

// The partial-animation bits' size at `at` (a count, then a word per 32 bones - one too many, as the game reads it).
bool partial_bits_size(uintptr_t at, uintptr_t end, uintptr_t* size)
{
    if (at > end || end - at < 4) {
        return false;
    }
    const int32_t count = util::ByteReader(cell::pointer<const void>(at), 4).i32();
    if (count < 0) {
        return false;
    }
    *size = static_cast<uintptr_t>((((count - 1) / kFlagsPerWord) * 4) + kFlagTableHeaderBytes);
    return true;
}

bool fits(uintptr_t at, uint64_t length, uintptr_t end) { return at <= end && length <= end - at; }

} // namespace

bool lay_out_animation(const uint8_t* base, size_t size, AnimHeader* header, AnimLayout* layout)
{
    util::ByteReader reader(base, size);
    header->version = reader.u32();
    header->flags = reader.u32();
    header->duration = reader.u32();
    const bool float_keys = (header->flags & kFloatKeys) != 0;
    if (!float_keys && ((header->flags & kCompressed) == 0U)) {
        return false;
    }
    header->bone_count = reader.u32();
    header->rotation_key_count = reader.u32();
    header->translation_key_count = reader.u32();
    header->custom_key_count = reader.u32();
    if (!float_keys) {
        header->rotation_size = reader.u32();
        header->translation_size = reader.u32();
    }
    if (!reader.ok()) {
        return false;
    }
    // (the frame data keeps the key counts in 16 bits, the bone count and the custom key count as they are)
    auto rotations = static_cast<int16_t>(header->rotation_key_count);
    auto translations = static_cast<int16_t>(header->translation_key_count);
    if (static_cast<int32_t>(header->bone_count) < 0 || rotations < 0 || translations < 0 ||
        static_cast<int16_t>(header->custom_key_count) < 0) {
        return false;
    }
    const uintptr_t end = cell::address(base) + size;
    const uint32_t flags = header->flags;
    const uint64_t bones = header->bone_count;
    *layout = {};
    layout->end = end;
    layout->keys_header = cell::address(base) + kHeaderSize;
    uintptr_t offset = 0;
    if (float_keys) {
        offset = layout->keys_header + kFloatKeysHeaderBytes;
        if ((flags & kBoneNames) != 0U) {
            layout->bone_names = offset;
            if (!fits(offset, bones * 4, end)) {
                return false;
            }
            offset += static_cast<uintptr_t>(bones * 4);
        }
        if ((flags & kPartial) != 0U) {
            uintptr_t bits = 0;
            if (!partial_bits_size(offset, end, &bits)) {
                return false;
            }
            layout->partial_bones = offset;
            offset += bits;
        }
        layout->key_counts = offset;
        const uint64_t counts = bones * (((flags & kWideCounts) != 0U) ? 4 : 2);
        if (!fits(offset, counts, end)) {
            return false;
        }
        offset = align4(offset + static_cast<uintptr_t>(counts));
        const uint64_t key_size = ((flags & kWideKeys) != 0U) || ((flags & kBoneNames) != 0U) ? 16 : 8;
        layout->rotation_keys = offset;
        layout->translation_keys = offset + static_cast<uintptr_t>(rotations * key_size);
        offset = layout->translation_keys + static_cast<uintptr_t>(translations * key_size);
        if (!fits(layout->rotation_keys, (static_cast<uint64_t>(rotations) + translations) * key_size, end)) {
            return false;
        }
    } else {
        offset = layout->keys_header + kPackedKeysHeaderBytes;
        layout->rotation_sizes = offset;
        layout->translation_sizes = offset + static_cast<uintptr_t>(bones * 2);
        if (!fits(offset, bones * 4, end)) {
            return false;
        }
        offset = align4(layout->translation_sizes + static_cast<uintptr_t>(bones * 2));
        if ((flags & kKeyTable) != 0U) {
            layout->key_table = offset;
            if (!fits(offset, kKeyTableBytes, end)) {
                return false;
            }
            offset += kKeyTableBytes;
        }
        if ((flags & kPartial) != 0U) {
            uintptr_t bits = 0;
            if (!partial_bits_size(offset, end, &bits)) {
                return false;
            }
            layout->partial_bones = offset;
            offset += bits;
        }
        offset = align4(offset);
        if (((flags & kWideKeys) == 0U) && ((flags & kBoneNames) == 0U)) {
            const uint64_t keys = static_cast<uint64_t>(header->rotation_size) + header->translation_size;
            if (!fits(offset, keys, end)) {
                return false;
            }
            layout->rotation_keys = offset;
            layout->translation_keys = offset + header->rotation_size;
            offset += static_cast<uintptr_t>(keys);
        }
    }
    layout->custom_keys = align4(offset);
    return layout->custom_keys <= end;
}

bool check_bone_keys(const AnimHeader& header, const AnimLayout& layout)
{
    uint64_t rotations = 0;
    uint64_t translations = 0;
    if ((header.flags & kFloatKeys) != 0U) {
        const bool wide = (header.flags & kWideCounts) != 0;
        util::ByteReader counts(cell::pointer<const void>(layout.key_counts),
                                static_cast<size_t>(header.bone_count) * (wide ? 4 : 2));
        for (uint32_t i = 0; i < header.bone_count; ++i) {
            const int rotation = wide ? counts.i16() : counts.u8();
            const int translation = wide ? counts.i16() : counts.u8();
            if (rotation < 0 || translation < 0) {
                return false;
            }
            rotations += static_cast<uint64_t>(rotation);
            translations += static_cast<uint64_t>(translation);
        }
        return counts.ok() &&
               rotations <= ints::as_unsigned(int64_t{static_cast<int16_t>(header.rotation_key_count)}) &&
               translations <= ints::as_unsigned(int64_t{static_cast<int16_t>(header.translation_key_count)});
    }
    if (layout.rotation_keys == 0U) {
        return true;
    }
    util::ByteReader sizes(cell::pointer<const void>(layout.rotation_sizes),
                           static_cast<size_t>(header.bone_count) * 4);
    for (uint32_t i = 0; i < header.bone_count; ++i) {
        rotations += sizes.u16();
    }
    for (uint32_t i = 0; i < header.bone_count; ++i) {
        translations += sizes.u16();
    }
    return sizes.ok() && rotations <= header.rotation_size && translations <= header.translation_size;
}

bool read_custom_key(util::ByteReader& reader, CustomKey* out)
{
    out->frame = reader.i32();
    out->kind = reader.u32();
    out->size = reader.u32();
    switch (out->kind) {
    case 1:
        core::at(out->values, 0) = reader.f32();
        break;
    case 2:
    case kKindPositionRotationAlt:
        for (float& value : out->values) {
            value = reader.f32();
        }
        break;
    case 3:
        reader.skip(kKindSkippedBytes);
        break;
    case 4:
    case kKindWord5:
    case kKindWord6:
        out->value = reader.u32();
        break;
    default:
        reader.skip(out->size);
        break;
    }
    return reader.ok();
}

} // namespace anim_format

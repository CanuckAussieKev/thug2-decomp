#include "formats/anim_format.h"
#include "engine/core/ints.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/checked_index.h"

namespace anim_format {

namespace {

uintptr_t align4(uintptr_t at) { return (at + 3) & ~static_cast<uintptr_t>(3); }

// The partial-animation bits' size at `at` (a count, then a word per 32 bones - one too many, as the game reads it).
bool partial_bits_size(uintptr_t at, uintptr_t end, uintptr_t* size)
{
    if (at > end || end - at < 4)
        return false;
    const int32_t count = util::ByteReader(cell::pointer<const void>(at), 4).i32();
    if (count < 0)
        return false;
    *size = static_cast<uintptr_t>(((count - 1) / 32) * 4 + 8);
    return true;
}

bool fits(uintptr_t at, uint64_t length, uintptr_t end) { return at <= end && length <= end - at; }

} // namespace

bool lay_out_animation(const uint8_t* base, size_t size, AnimHeader* header, AnimLayout* layout)
{
    const bool known = size != 0 && size != util::ByteReader::kUnknownSize;
    util::ByteReader reader(base, known ? size : util::ByteReader::kUnknownSize);
    header->version = reader.u32();
    header->flags = reader.u32();
    header->duration = reader.u32();
    const bool float_keys = (header->flags & kFloatKeys) != 0;
    if (!float_keys && !(header->flags & kCompressed))
        return false;
    header->bone_count = reader.u32();
    header->rotation_key_count = reader.u32();
    header->translation_key_count = reader.u32();
    header->custom_key_count = reader.u32();
    if (!float_keys) {
        header->rotation_size = reader.u32();
        header->translation_size = reader.u32();
    }
    if (!reader.ok())
        return false;
    // (the frame data keeps the key counts in 16 bits, the bone count and the custom key count as they are)
    auto rotations = static_cast<int16_t>(header->rotation_key_count);
    auto translations = static_cast<int16_t>(header->translation_key_count);
    if (static_cast<int32_t>(header->bone_count) < 0 || rotations < 0 || translations < 0 ||
        static_cast<int16_t>(header->custom_key_count) < 0)
        return false;
    const uintptr_t end = known ? cell::address(base) + size : ~static_cast<uintptr_t>(0);
    const uint32_t flags = header->flags;
    const uint64_t bones = header->bone_count;
    *layout = {};
    layout->end = end;
    layout->keys_header = cell::address(base) + kHeaderSize;
    uintptr_t p = 0;
    if (float_keys) {
        p = layout->keys_header + 0x10;
        if (flags & kBoneNames) {
            layout->bone_names = p;
            if (!fits(p, bones * 4, end))
                return false;
            p += static_cast<uintptr_t>(bones * 4);
        }
        if (flags & kPartial) {
            uintptr_t bits = 0;
            if (!partial_bits_size(p, end, &bits))
                return false;
            layout->partial_bones = p;
            p += bits;
        }
        layout->key_counts = p;
        const uint64_t counts = bones * ((flags & kWideCounts) ? 4 : 2);
        if (!fits(p, counts, end))
            return false;
        p = align4(p + static_cast<uintptr_t>(counts));
        const uint64_t key_size = (flags & kWideKeys) || (flags & kBoneNames) ? 16 : 8;
        layout->rotation_keys = p;
        layout->translation_keys = p + static_cast<uintptr_t>(rotations * key_size);
        p = layout->translation_keys + static_cast<uintptr_t>(translations * key_size);
        if (!fits(layout->rotation_keys, (static_cast<uint64_t>(rotations) + translations) * key_size, end))
            return false;
    } else {
        p = layout->keys_header + 0x18;
        layout->rotation_sizes = p;
        layout->translation_sizes = p + static_cast<uintptr_t>(bones * 2);
        if (!fits(p, bones * 4, end))
            return false;
        p = align4(layout->translation_sizes + static_cast<uintptr_t>(bones * 2));
        if (flags & kKeyTable) {
            layout->key_table = p;
            if (!fits(p, 0x600, end))
                return false;
            p += 0x600;
        }
        if (flags & kPartial) {
            uintptr_t bits = 0;
            if (!partial_bits_size(p, end, &bits))
                return false;
            layout->partial_bones = p;
            p += bits;
        }
        p = align4(p);
        if (!(flags & kWideKeys) && !(flags & kBoneNames)) {
            const uint64_t keys = static_cast<uint64_t>(header->rotation_size) + header->translation_size;
            if (!fits(p, keys, end))
                return false;
            layout->rotation_keys = p;
            layout->translation_keys = p + header->rotation_size;
            p += static_cast<uintptr_t>(keys);
        }
    }
    layout->custom_keys = align4(p);
    return !known || layout->custom_keys <= end;
}

bool check_bone_keys(const AnimHeader& header, const AnimLayout& layout)
{
    uint64_t rotations = 0;
    uint64_t translations = 0;
    if (header.flags & kFloatKeys) {
        const bool wide = (header.flags & kWideCounts) != 0;
        util::ByteReader counts(cell::pointer<const void>(layout.key_counts),
                                static_cast<size_t>(header.bone_count) * (wide ? 4 : 2));
        for (uint32_t i = 0; i < header.bone_count; ++i) {
            const int rotation = wide ? counts.i16() : counts.u8();
            const int translation = wide ? counts.i16() : counts.u8();
            if (rotation < 0 || translation < 0)
                return false;
            rotations += static_cast<uint64_t>(rotation);
            translations += static_cast<uint64_t>(translation);
        }
        return counts.ok() &&
               rotations <= ints::as_unsigned(int64_t{static_cast<int16_t>(header.rotation_key_count)}) &&
               translations <= ints::as_unsigned(int64_t{static_cast<int16_t>(header.translation_key_count)});
    }
    if (!layout.rotation_keys)
        return true;
    util::ByteReader sizes(cell::pointer<const void>(layout.rotation_sizes),
                           static_cast<size_t>(header.bone_count) * 4);
    for (uint32_t i = 0; i < header.bone_count; ++i)
        rotations += sizes.u16();
    for (uint32_t i = 0; i < header.bone_count; ++i)
        translations += sizes.u16();
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
    case 7:
        for (float& value : out->values)
            value = reader.f32();
        break;
    case 3:
        reader.skip(32);
        break;
    case 4:
    case 5:
    case 6:
        out->value = reader.u32();
        break;
    default:
        reader.skip(out->size);
        break;
    }
    return reader.ok();
}

} // namespace anim_format

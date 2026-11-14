// NOT ORIGINAL - the detail-material library's file (detail_material_format.h).
#include "formats/detail_material_format.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "formats/byte_stream.h"

namespace detail_material_format {

namespace {

constexpr uint32_t kHighShift = 32;
constexpr uint32_t kReservedWords = 4;
constexpr uint32_t kKindReservedWords = 3;
constexpr uint32_t kSmallestSide = 4;

bool finite(std::span<const float> values)
{
    for (const float value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool read_library(std::span<const uint8_t> file, Library* library)
{
    library->kinds.clear();
    library->assignments.clear();
    util::ByteReader reader(file);
    const uint32_t magic = reader.u32();
    const uint32_t version = reader.u32();
    const uint32_t kinds = reader.u32();
    const uint32_t assignments = reader.u32();
    for (uint32_t word = 0; word < kReservedWords; ++word) {
        (void)reader.u32();
    }
    if (!reader.ok() || magic != kMagic || version != kVersion || kinds > kKindCount ||
        static_cast<std::size_t>(assignments) > (file.size() - kHeaderSize) / kAssignmentSize ||
        kHeaderSize + (static_cast<std::size_t>(kinds) * kKindEntrySize) +
                (static_cast<std::size_t>(assignments) * kAssignmentSize) >
            file.size()) {
        return false;
    }
    Library read;
    read.kinds.reserve(kinds);
    for (uint32_t index = 0; index < kinds; ++index) {
        KindEntry entry;
        const uint8_t kind = reader.u8();
        (void)reader.u8();
        entry.side = reader.u16();
        entry.tile_metres = reader.f32();
        entry.roughness = reader.f32();
        entry.roughness_swing = reader.f32();
        entry.f0 = reader.f32();
        entry.metalness = reader.f32();
        entry.roughness_weight = reader.f32();
        entry.offset = reader.u32();
        entry.bytes = reader.u32();
        for (uint32_t word = 0; word < kKindReservedWords; ++word) {
            (void)reader.u32();
        }
        const std::size_t expected = static_cast<std::size_t>(entry.side) * entry.side * kTexelBytes;
        const bool power_of_two = entry.side != 0 && (entry.side & (entry.side - 1U)) == 0;
        if (!reader.ok() || kind == 0 || kind >= kKindCount || !power_of_two || entry.side < kSmallestSide ||
            entry.side > kLargestSide || entry.bytes != expected || entry.offset > file.size() ||
            entry.bytes > file.size() - entry.offset ||
            !finite(std::array{entry.tile_metres, entry.roughness, entry.roughness_swing, entry.f0, entry.metalness,
                               entry.roughness_weight})) {
            return false;
        }
        entry.kind = static_cast<Kind>(kind);
        read.kinds.push_back(entry);
    }
    read.assignments.reserve(assignments);
    for (uint32_t index = 0; index < assignments; ++index) {
        Assignment entry;
        entry.checksum = reader.u32();
        const uint32_t low = reader.u32();
        const uint32_t high = reader.u32();
        entry.content_hash = (static_cast<uint64_t>(high) << kHighShift) | low;
        const uint8_t kind = reader.u8();
        entry.kind = kind < kKindCount ? static_cast<Kind>(kind) : Kind::kNone;
        entry.evidence = static_cast<Evidence>(reader.u8());
        (void)reader.u16();
        entry.repeats = reader.f32();
        entry.mean_luma = reader.f32();
        if (!reader.ok() || !finite(std::array{entry.repeats, entry.mean_luma})) {
            return false;
        }
        read.assignments.push_back(entry);
    }
    *library = std::move(read);
    return true;
}

std::vector<uint8_t> write_file(const Library& library, std::span<const std::vector<uint8_t>> texels)
{
    std::size_t size = kHeaderSize + (library.kinds.size() * kKindEntrySize) + (library.assignments.size() * kAssignmentSize);
    for (const std::vector<uint8_t>& data : texels) {
        size += data.size();
    }
    std::vector<uint8_t> file(size);
    util::ByteWriter writer(file);
    writer.u32(kMagic);
    writer.u32(kVersion);
    writer.u32(static_cast<uint32_t>(library.kinds.size()));
    writer.u32(static_cast<uint32_t>(library.assignments.size()));
    for (uint32_t word = 0; word < kReservedWords; ++word) {
        writer.u32(0);
    }
    std::size_t offset = kHeaderSize + (library.kinds.size() * kKindEntrySize) + (library.assignments.size() * kAssignmentSize);
    for (std::size_t index = 0; index < library.kinds.size(); ++index) {
        const KindEntry& entry = core::at(library.kinds, index);
        const std::size_t bytes = index < texels.size() ? core::at(texels, index).size() : 0U;
        writer.u8(static_cast<uint8_t>(entry.kind));
        writer.u8(0);
        writer.u16(entry.side);
        writer.f32(entry.tile_metres);
        writer.f32(entry.roughness);
        writer.f32(entry.roughness_swing);
        writer.f32(entry.f0);
        writer.f32(entry.metalness);
        writer.f32(entry.roughness_weight);
        writer.u32(static_cast<uint32_t>(offset));
        writer.u32(static_cast<uint32_t>(bytes));
        for (uint32_t word = 0; word < kKindReservedWords; ++word) {
            writer.u32(0);
        }
        offset += bytes;
    }
    for (const Assignment& entry : library.assignments) {
        writer.u32(entry.checksum);
        writer.u32(static_cast<uint32_t>(entry.content_hash));
        writer.u32(static_cast<uint32_t>(entry.content_hash >> kHighShift));
        writer.u8(static_cast<uint8_t>(entry.kind));
        writer.u8(static_cast<uint8_t>(entry.evidence));
        writer.u16(0);
        writer.f32(entry.repeats);
        writer.f32(entry.mean_luma);
    }
    for (std::size_t index = 0; index < library.kinds.size() && index < texels.size(); ++index) {
        const std::vector<uint8_t>& data = core::at(texels, index);
        writer.bytes(data.data(), data.size());
    }
    return file;
}

} // namespace detail_material_format

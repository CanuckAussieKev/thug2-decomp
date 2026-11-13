// NOT ORIGINAL - surface maps' file (surface_map_format.h).
#include "formats/surface_map_format.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "formats/byte_stream.h"

namespace surface_map_format {

namespace {

constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001b3ULL;
constexpr uint32_t kHighShift = 32;

} // namespace

uint64_t content_hash(std::span<const uint8_t> bytes)
{
    uint64_t hash = kFnvBasis;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

bool read_entries(std::span<const uint8_t> file, std::vector<Entry>* entries)
{
    entries->clear();
    util::ByteReader reader(file);
    const uint32_t magic = reader.u32();
    const uint32_t version = reader.u32();
    const uint32_t count = reader.u32();
    (void)reader.u32();
    if (!reader.ok() || magic != kMagic || version != kVersion ||
        static_cast<std::size_t>(count) > (file.size() - kHeaderSize) / kEntrySize) {
        return false;
    }
    std::vector<Entry> read;
    read.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        Entry entry;
        entry.checksum = reader.u32();
        const uint32_t low = reader.u32();
        const uint32_t high = reader.u32();
        entry.content_hash = (static_cast<uint64_t>(high) << kHighShift) | low;
        entry.width = reader.u16();
        entry.height = reader.u16();
        const uint8_t material = reader.u8();
        entry.material_class = material < kClassCount ? static_cast<MaterialClass>(material) : MaterialClass::kDefault;
        entry.generator = static_cast<Generator>(reader.u8());
        (void)reader.u8();
        (void)reader.u8();
        entry.f0 = reader.f32();
        entry.offset = reader.u32();
        entry.bytes = reader.u32();
        if (!reader.ok()) {
            return false;
        }
        const std::size_t expected = static_cast<std::size_t>(entry.width) * entry.height * kTexelBytes;
        if (entry.width == 0 || entry.height == 0 || entry.width > kLargestSide || entry.height > kLargestSide ||
            entry.bytes != expected || entry.offset > file.size() || entry.bytes > file.size() - entry.offset ||
            !std::isfinite(entry.f0)) {
            return false;
        }
        read.push_back(entry);
    }
    *entries = std::move(read);
    return true;
}

std::vector<uint8_t> write_file(std::span<const Entry> entries, std::span<const std::vector<uint8_t>> texels)
{
    std::size_t size = kHeaderSize + (entries.size() * kEntrySize);
    for (const std::vector<uint8_t>& data : texels) {
        size += data.size();
    }
    std::vector<uint8_t> file(size);
    util::ByteWriter writer(file);
    writer.u32(kMagic);
    writer.u32(kVersion);
    writer.u32(static_cast<uint32_t>(entries.size()));
    writer.u32(0);
    std::size_t offset = kHeaderSize + (entries.size() * kEntrySize);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const Entry& entry = core::at(entries, index);
        const std::size_t bytes = index < texels.size() ? core::at(texels, index).size() : 0U;
        writer.u32(entry.checksum);
        writer.u32(static_cast<uint32_t>(entry.content_hash));
        writer.u32(static_cast<uint32_t>(entry.content_hash >> kHighShift));
        writer.u16(entry.width);
        writer.u16(entry.height);
        writer.u8(static_cast<uint8_t>(entry.material_class));
        writer.u8(static_cast<uint8_t>(entry.generator));
        writer.u8(0);
        writer.u8(0);
        writer.f32(entry.f0);
        writer.u32(static_cast<uint32_t>(offset));
        writer.u32(static_cast<uint32_t>(bytes));
        offset += bytes;
    }
    for (std::size_t index = 0; index < entries.size() && index < texels.size(); ++index) {
        const std::vector<uint8_t>& data = core::at(texels, index);
        writer.bytes(data.data(), data.size());
    }
    return file;
}

const Entry* find(std::span<const Entry> entries, uint32_t checksum, uint64_t content_hash)
{
    for (const Entry& entry : entries) {
        if (entry.checksum == checksum && entry.content_hash == content_hash) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace surface_map_format

// NOT ORIGINAL - building relief's file (relief_format.h).
#include "formats/relief_format.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "formats/byte_stream.h"

namespace relief_format {

namespace {

constexpr uint32_t kHighShift = 32;
constexpr uint8_t kFirstKind = 1;
constexpr uint8_t kLastKind = 4;

bool sane(const Rect& rect)
{
    const auto in_cell = [](float low, float high) {
        return std::isfinite(low) && std::isfinite(high) && low >= 0.0F && low < 1.0F && high > low && high - low <= 1.0F;
    };
    return in_cell(rect.u0, rect.u1) && in_cell(rect.v0, rect.v1) && std::isfinite(rect.height) &&
           std::fabs(rect.height) <= kMostHeight;
}

} // namespace

bool read_file(std::span<const uint8_t> file, File* out)
{
    *out = File{};
    util::ByteReader reader(file);
    const uint32_t magic = reader.u32();
    const uint32_t version = reader.u32();
    const uint32_t count = reader.u32();
    const uint32_t detector = reader.u32();
    const uint32_t sources_low = reader.u32();
    const uint32_t sources_high = reader.u32();
    if (!reader.ok() || magic != kMagic || version != kVersion ||
        static_cast<std::size_t>(count) > (file.size() - kHeaderSize) / kEntrySize) {
        return false;
    }
    const std::size_t rect_bytes = file.size() - kHeaderSize - (static_cast<std::size_t>(count) * kEntrySize);
    if (rect_bytes % kRectSize != 0) {
        return false;
    }
    const std::size_t rect_count = rect_bytes / kRectSize;
    File read;
    read.detector = detector;
    read.sources = (static_cast<uint64_t>(sources_high) << kHighShift) | sources_low;
    read.entries.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        Entry entry;
        entry.checksum = reader.u32();
        const uint32_t low = reader.u32();
        const uint32_t high = reader.u32();
        entry.content_hash = (static_cast<uint64_t>(high) << kHighShift) | low;
        entry.repeat_u = reader.u16();
        entry.repeat_v = reader.u16();
        entry.first = reader.u32();
        entry.count = reader.u32();
        if (!reader.ok() || entry.repeat_u == 0 || entry.repeat_v == 0 || entry.repeat_u > kMostRepeats ||
            entry.repeat_v > kMostRepeats || entry.first > rect_count || entry.count > rect_count - entry.first) {
            return false;
        }
        read.entries.push_back(entry);
    }
    read.rects.reserve(rect_count);
    for (std::size_t index = 0; index < rect_count; ++index) {
        Rect rect;
        const uint8_t kind = reader.u8();
        (void)reader.u8();
        (void)reader.u8();
        (void)reader.u8();
        rect.u0 = reader.f32();
        rect.v0 = reader.f32();
        rect.u1 = reader.f32();
        rect.v1 = reader.f32();
        rect.height = reader.f32();
        if (!reader.ok() || kind < kFirstKind || kind > kLastKind || !sane(rect)) {
            return false;
        }
        rect.kind = static_cast<Kind>(kind);
        read.rects.push_back(rect);
    }
    *out = std::move(read);
    return true;
}

std::vector<uint8_t> write_file(const File& file)
{
    std::vector<uint8_t> bytes(kHeaderSize + (file.entries.size() * kEntrySize) + (file.rects.size() * kRectSize));
    util::ByteWriter writer(bytes);
    writer.u32(kMagic);
    writer.u32(kVersion);
    writer.u32(static_cast<uint32_t>(file.entries.size()));
    writer.u32(file.detector);
    writer.u32(static_cast<uint32_t>(file.sources));
    writer.u32(static_cast<uint32_t>(file.sources >> kHighShift));
    for (const Entry& entry : file.entries) {
        writer.u32(entry.checksum);
        writer.u32(static_cast<uint32_t>(entry.content_hash));
        writer.u32(static_cast<uint32_t>(entry.content_hash >> kHighShift));
        writer.u16(entry.repeat_u);
        writer.u16(entry.repeat_v);
        writer.u32(entry.first);
        writer.u32(entry.count);
    }
    for (const Rect& rect : file.rects) {
        writer.u8(static_cast<uint8_t>(rect.kind));
        writer.u8(0);
        writer.u8(0);
        writer.u8(0);
        writer.f32(rect.u0);
        writer.f32(rect.v0);
        writer.f32(rect.u1);
        writer.f32(rect.v1);
        writer.f32(rect.height);
    }
    return bytes;
}

const Entry* find(const File& file, uint32_t checksum, uint64_t content_hash)
{
    for (const Entry& entry : file.entries) {
        if (entry.checksum == checksum && entry.content_hash == content_hash) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace relief_format

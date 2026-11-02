// NOT ORIGINAL - the packed rails' reader: see rail_buffer_format.h.

#include "formats/rail_buffer_format.h"
#include "engine/core/checked_index.h"

#include <utility>

namespace park_editing::rail_buffer_format {

bool counts_fit(const RailBuffer& buffer)
{
    if (buffer.rail_count_ > kMaxRails)
        return false;
    int points = 0;
    for (int i = 0; std::cmp_less(i, buffer.rail_count_); ++i)
        points += core::at(buffer.point_counts_, i);
    return points <= kMaxPoints;
}

bool read_rail_buffer(util::ByteReader& reader, RailBuffer* out)
{
    out->rail_count_ = reader.u16();
    for (uint16_t& count : out->point_counts_)
        count = reader.u16();
    for (auto& record : out->records_) {
        for (uint16_t& value : record)
            value = reader.u16();
    }
    if (reader.ok() && counts_fit(*out))
        return true;
    *out = RailBuffer{};
    return false;
}

bool write_rail_buffer(util::ByteWriter& writer, const RailBuffer& buffer)
{
    writer.u16(buffer.rail_count_);
    for (const uint16_t count : buffer.point_counts_)
        writer.u16(count);
    for (const auto& record : buffer.records_) {
        for (const uint16_t value : record)
            writer.u16(value);
    }
    return writer.ok();
}

} // namespace park_editing::rail_buffer_format

// NOT ORIGINAL - the packed rails' reader: see rail_buffer_format.h.

#include "game/park_editing/rail_buffer_format.h"

#include <cstring>

namespace park_editing::rail_buffer_format {

bool counts_fit(const RailBuffer& buffer)
{
    if (buffer.rail_count_ > kMaxRails)
        return false;
    int points = 0;
    for (int i = 0; i < buffer.rail_count_; ++i)
        points += buffer.point_counts_[i];
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
    std::memset(out, 0, sizeof(*out));
    return false;
}

} // namespace park_editing::rail_buffer_format

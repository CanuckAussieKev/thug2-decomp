// NOT ORIGINAL - the sound effects' file reader: see wave_format.h.

#include "formats/wave_format.h"
#include "engine/core/checked_index.h"
#include "formats/byte_stream.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace wave_format {

bool read_wave(util::ByteReader& reader, Wave* out)
{
    *out = Wave{};
    reader.skip(12); // "RIFF", its size, "WAVE"
    while (reader.ok() && reader.remaining() >= 8) {
        const uint32_t id = reader.u32();
        const uint32_t size = reader.u32();
        if (size > reader.remaining())
            return false;
        const std::span<const uint8_t> body = reader.rest().first(size);
        reader.skip(size);
        if (id == kFormatId) {
            out->format_size = size;
            for (size_t i = 0; i < kFormatSize && i < size; ++i)
                core::at(out->format, i) = core::at(body, i);
        } else if (id == kDataId) {
            out->samples = body.data();
            out->sample_bytes = size;
            out->has_data = true;
        } else if (id == kSamplerId) {
            uint32_t loops = 0; // (the eighth word; 0 when the chunk is shorter)
            if (size >= 0x20)
                loops = core::at(body, 0x1C) | core::at(body, 0x1D) << 8 | core::at(body, 0x1E) << 16 |
                        static_cast<uint32_t>(core::at(body, 0x1F)) << 24;
            if (loops > 0)
                out->looping = true;
        }
    }
    return reader.ok() && out->format_size && out->has_data;
}

} // namespace wave_format

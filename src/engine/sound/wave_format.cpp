// NOT ORIGINAL - the sound effects' file reader: see wave_format.h.

#include "engine/sound/wave_format.h"

namespace wave_format {

bool read_wave(util::ByteReader& reader, Wave* out)
{
    *out = Wave{};
    reader.skip(12); // "RIFF", its size, "WAVE"
    while (reader.ok() && reader.remaining() >= 8) {
        uint32_t id = reader.u32();
        uint32_t size = reader.u32();
        const uint8_t* body = reader.cursor();
        if (!reader.skip(size))
            return false;
        if (id == kFormatId) {
            out->format_size = size;
            for (size_t i = 0; i < kFormatSize && i < size; ++i)
                out->format[i] = body[i];
        } else if (id == kDataId) {
            out->samples = body;
            out->sample_bytes = size;
            out->has_data = true;
        } else if (id == kSamplerId) {
            uint32_t loops = 0; // (the eighth word; 0 when the chunk is shorter)
            if (size >= 0x20)
                loops = body[0x1C] | body[0x1D] << 8 | body[0x1E] << 16 | static_cast<uint32_t>(body[0x1F]) << 24;
            if (loops > 0)
                out->looping = true;
        }
    }
    return reader.ok() && out->format_size && out->has_data;
}

} // namespace wave_format

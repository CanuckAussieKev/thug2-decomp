// NOT ORIGINAL - the sound effects' file reader: see wave_format.h.

#include "formats/wave_format.h"
#include "base/core/checked_index.h"
#include "formats/byte_stream.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace wave_format {

constexpr int kRiffHeaderBytes = 12;      // "RIFF", its size, "WAVE"
constexpr int kChunkHeaderBytes = 8;      // a chunk's id and size
constexpr int kSamplerChunkMinBytes = 0x20;
constexpr int kLoopCountByte0 = 0x1C;     // the sampler chunk's eighth word: the loop count
constexpr int kLoopCountByte1 = 0x1D;
constexpr int kLoopCountByte2 = 0x1E;
constexpr int kLoopCountByte3 = 0x1F;

bool read_wave(util::ByteReader& reader, Wave* out)
{
    *out = Wave{};
    reader.skip(kRiffHeaderBytes); // "RIFF", its size, "WAVE"
    while (reader.ok() && reader.remaining() >= kChunkHeaderBytes) {
        const uint32_t id = reader.u32();
        const uint32_t size = reader.u32();
        if (size > reader.remaining()) {
            return false;
        }
        const std::span<const uint8_t> body = reader.rest().first(size);
        reader.skip(size);
        if (id == kFormatId) {
            out->set_format_size(size);
            for (size_t i = 0; i < kFormatSize && i < size; ++i) {
                core::at(out->format(), i) = core::at(body, i);
            }
        } else if (id == kDataId) {
            out->set_samples(body.data());
            out->set_sample_bytes(size);
            out->set_has_data(true);
        } else if (id == kSamplerId) {
            uint32_t loops = 0; // (the eighth word; 0 when the chunk is shorter)
            if (size >= kSamplerChunkMinBytes) {
                loops = core::at(body, kLoopCountByte0) | core::at(body, kLoopCountByte1) << kBitsPerByte | core::at(body, kLoopCountByte2) << kBitsPerTwoBytes |
                        static_cast<uint32_t>(core::at(body, kLoopCountByte3)) << kBitsPerThreeBytes;
            }
            if (loops > 0) {
                out->set_looping(true);
            }
        }
    }
    return reader.ok() && (out->format_size() != 0U) && out->has_data();
}

} // namespace wave_format

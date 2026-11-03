#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "formats/byte_stream.h"
#include "engine/core/checked_index.h"

// NOT ORIGINAL - the sound effects' files ("data\sounds\...\x.snd": RIFF / WAVE, IMA ADPCM samples), read through
// util::ByteReader.
//
// A file (little-endian): "RIFF", u32 size, "WAVE" (none of it checked), then chunks {u32 id, u32 size, size bytes}
// up to the end: "fmt " (the WAVEFORMATEX: tag, channels, samples a second, bytes a second - the game's own use: the
// decoded size -, block align, bits a sample, extra size, 2 more), "data" (the 4-bit samples), "smpl" (its eighth
// word: the number of loops), others skipped.
//
// Read by sfx::PcWave::read_in (engine/sound/sfx_voices.cpp), which decodes "bytes a second" bytes of 16-bit samples
// from the data: each byte two samples.
namespace wave_format {

// The format chunk's fields.
constexpr std::size_t kBytesPerSecondOffset = 8;
constexpr std::size_t kBlockAlignOffset = 0xC;
constexpr std::size_t kBlockAlignHighOffset = 0xD;
constexpr int kBitsPerByte = 8;
constexpr int kBitsPerTwoBytes = 16;
constexpr int kBitsPerThreeBytes = 24;

constexpr uint32_t kFormatId = 0x20746D66;  // "fmt "
constexpr uint32_t kDataId = 0x61746164;    // "data"
constexpr uint32_t kSamplerId = 0x6C706D73; // "smpl"
constexpr size_t kFormatSize = 0x14;        // the format's bytes the game keeps (the original read the whole chunk)

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct Wave {
    // The fields other code reads and sets.
    std::array<uint8_t, kFormatSize>& format() { return format_; }
    [[nodiscard]] const std::array<uint8_t, kFormatSize>& format() const { return format_; }
    [[nodiscard]] uint32_t format_size() const { return format_size_; }
    void set_format_size(uint32_t value) { format_size_ = value; }
    [[nodiscard]] const uint8_t* samples() const { return samples_; }
    void set_samples(const uint8_t* value) { samples_ = value; }
    [[nodiscard]] uint32_t sample_bytes() const { return sample_bytes_; }
    void set_sample_bytes(uint32_t value) { sample_bytes_ = value; }
    [[nodiscard]] bool has_data() const { return has_data_; }
    void set_has_data(bool value) { has_data_ = value; }
    [[nodiscard]] bool looping() const { return looping_; }
    void set_looping(bool value) { looping_ = value; }

private:
    std::array<uint8_t, kFormatSize> format_ = {};
    uint32_t format_size_ = 0; // as stored (0: none)
    const uint8_t* samples_ = nullptr;
    uint32_t sample_bytes_ = 0;
    bool has_data_ = false;
    bool looping_ = false;

public:
    // the format's fields
    [[nodiscard]] uint32_t samples_per_second() const { return word(4); }
    [[nodiscard]] uint32_t bytes_per_second() const { return word(kBytesPerSecondOffset); }
    [[nodiscard]] uint16_t block_align() const { return static_cast<uint16_t>(core::at(format_, kBlockAlignOffset) | core::at(format_, kBlockAlignHighOffset) << kBitsPerByte); }
    [[nodiscard]] uint32_t word(size_t at) const
    {
        return core::at(format_, at) | core::at(format_, at + 1) << kBitsPerByte | core::at(format_, at + 2) << kBitsPerTwoBytes | static_cast<uint32_t>(core::at(format_, at + 3)) << kBitsPerThreeBytes;
    }
};
// NOLINTEND(readability-identifier-naming)

// False if cut short (a chunk past the end), without a format or data chunk.
bool read_wave(util::ByteReader& reader, Wave* out);

// The data bytes the decoder reads for `decoded_bytes` of 16-bit samples (two samples a byte).
inline uint64_t sample_bytes_needed(uint32_t decoded_bytes) { return (uint64_t{decoded_bytes} / 2 + 1) / 2; }

} // namespace wave_format

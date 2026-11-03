#pragma once

#include <cstddef>
#include <span>
#include <cstdint>
#include <array>
#include "engine/core/raw_view.h"
#include <cstring>
#include <type_traits>
#include "wire/message_ids.h"

namespace util {
class ByteReader;
class ByteWriter;
} // namespace util

// NOT ORIGINAL (the format is the original's) - the net layer's own messages as they are on the wire, with their
// readers and writers (the original cast the bytes onto structs). Little-endian, unpacked:
//
//   a message in a packet     id (u8), length (u16 - left out for ids flagged kMsgNoLength), the data
//   sequenced (id 7)          group (u8), sequence number (u32), the message's id (u8), its data
//   compressed (id 0x12)      method (u8: 1 run-length, 2 LZSS, THUGPRO's LZHAM), expanded size (u16), the stream;
//                             expanded, it is one message: id (u8), length (u16), data
//   receipt                   the packet's stamp (u16)
//   latency probe             the sender's time (u32), echoed back in the response
//   stream start (id 0x0E)    description (32 bytes, zero-padded), total size (i32), stream id (i32), CRC of all the
//                             data (u32), the streamed message's id (u8), the first piece (at most 256 bytes), then
//                             3 zero bytes (the original sent its struct's 3 bytes past the piece)
//   stream piece (id 0x0F)    stream id (i32), the piece (at most 256 bytes)
namespace net::wire {

// A message whose body is text (a NUL-terminated string it carries).
inline const char* text_of(const uint8_t* body) { return raw::chars(body); }
// The text starting `offset` bytes into a message's body (a second string after the first's field).
inline const char* text_at(const uint8_t* body, size_t offset)
{
    return raw::chars(std::span<const uint8_t>(body, offset + 1).last(1).data());
}
// The value of type T (a number, a flag byte) at `offset` in a message's body (little-endian, unaligned, as sent).
template <typename T>
T value_at(const uint8_t* body, size_t offset = 0)
{
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    std::memcpy(&value, std::span<const uint8_t>(body, offset + sizeof(value)).last(sizeof(value)).data(), sizeof(value));
    return value;
}


constexpr size_t kMessageHeaderSize = 3;
constexpr size_t kSequencedHeaderSize = 6;
constexpr size_t kCompressedHeaderSize = 3;
constexpr size_t kStreamPieceSize = 0x100;
constexpr size_t kStreamDescSize = 32;
constexpr size_t kStreamStartHeaderSize = 0x2D;
constexpr size_t kStreamStartTrailer = 3;
constexpr size_t kStreamStartMaxSize = kStreamStartHeaderSize + kStreamPieceSize + kStreamStartTrailer;
constexpr size_t kStreamPieceHeaderSize = 4;
constexpr size_t kStreamPieceMaxSize = kStreamPieceHeaderSize + kStreamPieceSize;

struct SequencedHeader {
    MessageGroup group{};
    uint32_t sequence = 0;
    MessageId id{};
};
bool read_sequenced_header(util::ByteReader& reader, SequencedHeader* header);
bool write_sequenced_header(util::ByteWriter& writer, const SequencedHeader& header);

struct CompressedHeader {
    uint8_t method = 0;
    uint16_t size = 0; // expanded
};
bool read_compressed_header(util::ByteReader& reader, CompressedHeader* header);

// A message's id and length (the compressed message's expanded data starts with one).
struct MessageHeader {
    MessageId id{};
    uint16_t length = 0;
};
bool read_message_header(util::ByteReader& reader, MessageHeader* header);

struct StreamStart {
    std::array<char, kStreamDescSize + 1> desc = {}; // (terminated here; on the wire 32 bytes, possibly without a terminator)
    int32_t total_size = 0;
    int32_t stream_id = 0;
    uint32_t checksum = 0;
    MessageId msg_id{};
};
// The start message's fields; the reader is left at the first piece.
bool read_stream_start(util::ByteReader& reader, StreamStart* start);
// The start message: the fields, the first piece (`piece_size` <= kStreamPieceSize) and the trailer.
bool write_stream_start(util::ByteWriter& writer, const StreamStart& start, const void* piece, size_t piece_size);

// A piece message: the stream id and the piece.
bool write_stream_piece(util::ByteWriter& writer, int32_t stream_id, const void* piece, size_t piece_size);

} // namespace net::wire

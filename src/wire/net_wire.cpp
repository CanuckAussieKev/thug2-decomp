// NOT ORIGINAL - the net layer's own messages' readers and writers. See net_wire.h.

#include "wire/net_wire.h"

#include "formats/byte_stream.h"

namespace net::wire {

bool read_sequenced_header(util::ByteReader& reader, SequencedHeader* header)
{
    header->group = reader.u8();
    header->sequence = reader.u32();
    header->id = reader.u8();
    return reader.ok();
}

bool write_sequenced_header(util::ByteWriter& writer, const SequencedHeader& header)
{
    writer.u8(header.group);
    writer.u32(header.sequence);
    writer.u8(header.id);
    return writer.ok();
}

bool read_compressed_header(util::ByteReader& reader, CompressedHeader* header)
{
    header->method = reader.u8();
    header->size = reader.u16();
    return reader.ok();
}

bool read_message_header(util::ByteReader& reader, MessageHeader* header)
{
    header->id = reader.u8();
    header->length = reader.u16();
    return reader.ok();
}

bool read_stream_start(util::ByteReader& reader, StreamStart* start)
{
    reader.fixed_string(start->desc, sizeof(start->desc), kStreamDescSize);
    start->total_size = reader.i32();
    start->stream_id = reader.i32();
    start->checksum = reader.u32();
    start->msg_id = reader.u8();
    return reader.ok();
}

bool write_stream_start(util::ByteWriter& writer, const StreamStart& start, const void* piece, size_t piece_size)
{
    writer.fixed_string(start.desc, kStreamDescSize);
    writer.i32(start.total_size);
    writer.i32(start.stream_id);
    writer.u32(start.checksum);
    writer.u8(start.msg_id);
    writer.bytes(piece, piece_size);
    writer.fill(0, kStreamStartTrailer);
    return writer.ok();
}

bool write_stream_piece(util::ByteWriter& writer, int32_t stream_id, const void* piece, size_t piece_size)
{
    writer.i32(stream_id);
    writer.bytes(piece, piece_size);
    return writer.ok();
}

} // namespace net::wire

// NOT ORIGINAL - the skate module's net messages' readers and writers. See skate_net_messages.h.

#include "game/skate/skate_net_messages.h"

#include "engine/util/byte_stream.h"

namespace skate_net {

bool read_scores(util::ByteReader& reader, ScoresMessage* message)
{
    message->time_left = reader.u32();
    message->cheats = reader.u32();
    message->final = reader.u8() != 0;
    message->count = int{reader.i8()};
    for (int i = 0; i < message->count; ++i) {
        message->entries[i].id = reader.i8();
        message->entries[i].score = reader.i32();
        if (!reader.ok()) {
            message->count = i;
            return false;
        }
    }
    return reader.ok();
}

bool write_scores(util::ByteWriter& writer, const ScoresMessage& message)
{
    writer.u32(message.time_left);
    writer.u32(message.cheats);
    writer.u8(message.final ? 1 : 0);
    writer.u8(static_cast<uint8_t>(message.count));
    for (int i = 0; i < message.count; ++i) {
        writer.i8(message.entries[i].id);
        writer.i32(message.entries[i].score);
    }
    return writer.ok();
}

bool read_graffiti_colors(util::ByteReader& reader, GraffitiColorsMessage* message)
{
    return reader.bytes(message->counts, sizeof(message->counts));
}

} // namespace skate_net

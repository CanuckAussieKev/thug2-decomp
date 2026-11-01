// NOT ORIGINAL - the skater's animation / model messages' readers and writers. See skater_messages.h.

#include "wire/skater_messages.h"

#include "formats/byte_stream.h"
#include "engine/core/checked_index.h"

namespace online {

bool read_play_anim(util::ByteReader& reader, PlayAnimMessage* message)
{
    message->loop_type = reader.u8();
    message->object_id = reader.u8();
    message->anim = reader.u32();
    message->from = reader.u16();
    message->to = reader.u16();
    message->blend_period = reader.u16();
    message->speed = reader.u16();
    return reader.ok();
}

bool write_play_anim(util::ByteWriter& writer, const PlayAnimMessage& message)
{
    writer.u8(message.loop_type);
    writer.u8(message.object_id);
    writer.u32(message.anim);
    writer.u16(message.from);
    writer.u16(message.to);
    writer.u16(message.blend_period);
    writer.u16(message.speed);
    return writer.ok();
}

bool read_pair(util::ByteReader& reader, PairMessage* message)
{
    message->first = reader.u8();
    message->second = reader.u8();
    return reader.ok();
}

bool write_pair(util::ByteWriter& writer, const PairMessage& message)
{
    writer.u8(message.first);
    writer.u8(message.second);
    return writer.ok();
}

bool read_hide_geom(util::ByteReader& reader, HideGeomMessage* message)
{
    message->name = reader.u32();
    message->object_id = reader.u8();
    message->hide = reader.u8() != 0;
    return reader.ok();
}

bool write_hide_geom(util::ByteWriter& writer, const HideGeomMessage& message)
{
    writer.u32(message.name);
    writer.u8(message.object_id);
    writer.u8(message.hide ? 1 : 0);
    writer.fill(0, 2);
    return writer.ok();
}

bool read_anim_speed(util::ByteReader& reader, AnimSpeedMessage* message)
{
    message->speed_bits = reader.u32();
    message->object_id = reader.u8();
    return reader.ok();
}

bool write_anim_speed(util::ByteWriter& writer, const AnimSpeedMessage& message)
{
    writer.u32(message.speed_bits);
    writer.u8(message.object_id);
    writer.fill(0, 3);
    return writer.ok();
}

bool read_spin_model(util::ByteReader& reader, SpinModelMessage* message)
{
    message->period = reader.i32();
    message->power = reader.i32();
    message->start = reader.i16();
    message->range = reader.i16();
    message->object_id = reader.u8();
    message->hold = reader.u8();
    message->axes = reader.u8();
    return reader.ok();
}

bool write_spin_model(util::ByteWriter& writer, const SpinModelMessage& message)
{
    writer.i32(message.period);
    writer.i32(message.power);
    writer.i16(message.start);
    writer.i16(message.range);
    writer.u8(message.object_id);
    writer.u8(message.hold);
    writer.u8(message.axes);
    writer.u8(0);
    return writer.ok();
}

bool read_special_item(util::ByteReader& reader, SpecialItemMessage* message)
{
    message->params = reader.u32();
    message->bone = reader.u32();
    message->object_id = reader.u8();
    message->slot = reader.i8();
    return reader.ok();
}

bool write_special_item(util::ByteWriter& writer, const SpecialItemMessage& message)
{
    writer.u32(message.params);
    writer.u32(message.bone);
    writer.u8(message.object_id);
    writer.i8(message.slot);
    writer.fill(0, 2);
    return writer.ok();
}

namespace {

// The names after a header: `count` of them, all within the message.
bool read_names(util::ByteReader& reader, TrickObjectsMessage* message)
{
    if (!reader.ok() || message->count > kMaxTrickObjects || message->count > reader.remaining() / 4) {
        message->count = 0;
        reader.skip(reader.remaining() + 1); // (fails the reader)
        return false;
    }
    for (uint32_t i = 0; i < message->count; ++i)
        core::at(message->objects, i) = reader.u32();
    return reader.ok();
}

bool write_names(util::ByteWriter& writer, const TrickObjectsMessage& message)
{
    if (message.count > kMaxTrickObjects)
        return writer.seek(writer.capacity() + 1); // (fails the writer)
    for (uint32_t i = 0; i < message.count; ++i)
        writer.u32(core::at(message.objects, i));
    return writer.ok();
}

} // namespace

bool read_trick_objects(util::ByteReader& reader, TrickObjectsMessage* message)
{
    message->one = reader.i8();
    reader.skip(9);
    message->game_id = reader.u8();
    reader.skip(1);
    message->player = reader.i32();
    message->score = reader.u32();
    message->count = reader.u32();
    return read_names(reader, message);
}

bool write_trick_objects(util::ByteWriter& writer, const TrickObjectsMessage& message)
{
    writer.i8(message.one);
    writer.fill(0, 9);
    writer.u8(message.game_id);
    writer.u8(0);
    writer.i32(message.player);
    writer.u32(message.score);
    writer.u32(message.count);
    return write_names(writer, message);
}

bool read_observed_trick_objects(util::ByteReader& reader, TrickObjectsMessage* message)
{
    message->game_id = reader.u8();
    reader.skip(3);
    message->player = reader.i32();
    message->count = reader.u32();
    return read_names(reader, message);
}

bool write_observed_trick_objects(util::ByteWriter& writer, const TrickObjectsMessage& message)
{
    writer.u8(message.game_id);
    writer.fill(0, 3);
    writer.i32(message.player);
    writer.u32(message.count);
    return write_names(writer, message);
}

bool read_tag_stolen(util::ByteReader& reader, TagStolenMessage* message)
{
    message->game_id = reader.u8();
    reader.skip(3);
    message->thief_id = reader.u32();
    message->victim_id = reader.u32();
    return reader.ok();
}

bool write_tag_stolen(util::ByteWriter& writer, const TagStolenMessage& message)
{
    writer.u8(message.game_id);
    writer.fill(0, 3);
    writer.u32(message.thief_id);
    writer.u32(message.victim_id);
    return writer.ok();
}

bool read_points(util::ByteReader& reader, PointsMessage* message)
{
    message->game_id = reader.u8();
    reader.skip(3);
    message->points = reader.i32();
    return reader.ok();
}

bool write_points(util::ByteWriter& writer, const PointsMessage& message)
{
    writer.u8(message.game_id);
    writer.fill(0, 3);
    writer.i32(message.points);
    return writer.ok();
}

} // namespace online

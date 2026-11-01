// NOT ORIGINAL - the skater's animation / model messages' readers and writers. See skater_messages.h.

#include "engine/net/skater_messages.h"

#include "engine/util/byte_stream.h"

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

} // namespace online

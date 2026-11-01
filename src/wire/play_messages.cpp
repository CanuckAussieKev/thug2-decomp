// NOT ORIGINAL - the network game's other messages' readers and writers. See play_messages.h.

#include "wire/play_messages.h"

#include "formats/byte_stream.h"

namespace online {

namespace {

void read_floats(util::ByteReader& reader, float* out, int count)
{
    for (int i = 0; i < count; ++i)
        out[i] = reader.f32();
}
void write_floats(util::ByteWriter& writer, const float* values, int count)
{
    for (int i = 0; i < count; ++i)
        writer.f32(values[i]);
}

} // namespace

bool read_hunt_placements(util::ByteReader& reader, HuntPlacements* message)
{
    message->player_id = reader.i8();
    reader.skip(3);
    message->count = reader.i32();
    return reader.ok();
}

bool read_hunt_position(util::ByteReader& reader, float position[4])
{
    read_floats(reader, position, 4);
    return reader.ok();
}

bool write_hunt_placements(util::ByteWriter& writer, const HuntPlacements& message)
{
    writer.i8(message.player_id);
    writer.fill(0, 3);
    writer.i32(message.count);
    for (const auto& position : message.positions)
        write_floats(writer, position, 4);
    return writer.ok();
}

bool write_hunt_position(util::ByteWriter& writer, const float position[4])
{
    write_floats(writer, position, 4);
    return writer.ok();
}

bool read_board_vehicle(util::ByteReader& reader, BoardVehicle* message)
{
    message->skater_id = reader.i8();
    reader.skip(3);
    message->control_type = reader.u32();
    return reader.ok();
}

bool write_board_vehicle(util::ByteWriter& writer, const BoardVehicle& message)
{
    writer.i8(message.skater_id);
    writer.fill(0, 3);
    writer.u32(message.control_type);
    return writer.ok();
}

bool read_projectile(util::ByteReader& reader, Projectile* message)
{
    message->time = reader.u32();
    read_floats(reader, message->position, 4);
    read_floats(reader, message->velocity, 4);
    message->type = reader.u32();
    message->ammo_type = reader.u32();
    message->radius = reader.i32();
    message->scale = reader.f32();
    message->owner_id = reader.i8();
    return reader.ok();
}

bool write_projectile(util::ByteWriter& writer, const Projectile& message)
{
    writer.u32(message.time);
    write_floats(writer, message.position, 4);
    write_floats(writer, message.velocity, 4);
    writer.u32(message.type);
    writer.u32(message.ammo_type);
    writer.i32(message.radius);
    writer.f32(message.scale);
    writer.i8(message.owner_id);
    writer.fill(0, 3);
    return writer.ok();
}

bool read_place_tag(util::ByteReader& reader, PlaceTag* message)
{
    message->object_id = reader.i8();
    message->sticker = reader.u8() != 0;
    reader.skip(2);
    read_floats(reader, message->start, 4);
    read_floats(reader, message->end, 4);
    message->size = reader.i32();
    return reader.ok();
}

bool write_place_tag(util::ByteWriter& writer, const PlaceTag& message)
{
    writer.i8(message.object_id);
    writer.u8(message.sticker ? 1 : 0);
    writer.fill(0, 2);
    write_floats(writer, message.start, 4);
    write_floats(writer, message.end, 4);
    writer.i32(message.size);
    return writer.ok();
}

bool read_terrain_sound(util::ByteReader& reader, TerrainSound* message)
{
    message->x = reader.i16();
    message->y = reader.i16();
    message->z = reader.i16();
    message->table = reader.u8();
    message->terrain = reader.u8();
    message->volume = reader.i8();
    message->pitch = reader.i8();
    message->value = reader.i8();
    return reader.ok();
}

bool write_terrain_sound(util::ByteWriter& writer, const TerrainSound& message)
{
    writer.i16(message.x);
    writer.i16(message.y);
    writer.i16(message.z);
    writer.u8(message.table);
    writer.u8(message.terrain);
    writer.i8(message.volume);
    writer.i8(message.pitch);
    writer.i8(message.value);
    writer.u8(0);
    return writer.ok();
}

bool read_toggle_cheat(util::ByteReader& reader, ToggleCheat* message)
{
    message->cheat = reader.u32();
    message->on = reader.u8() != 0;
    return reader.ok();
}

bool write_toggle_cheat(util::ByteWriter& writer, const ToggleCheat& message)
{
    writer.u32(message.cheat);
    writer.u8(message.on ? 1 : 0);
    writer.fill(0, 3);
    return writer.ok();
}

bool read_cheats(util::ByteReader& reader, Cheats* message)
{
    const int32_t count = reader.i32();
    if (!reader.ok() || count < 0 || count > kMaxCheats || static_cast<size_t>(count) > reader.remaining() / 4) {
        message->count = 0;
        return false;
    }
    message->count = count;
    for (int i = 0; i < count; ++i)
        message->cheats[i] = reader.u32();
    return reader.ok();
}

bool write_cheats(util::ByteWriter& writer, const Cheats& message)
{
    if (message.count < 0 || message.count > kMaxCheats)
        return writer.seek(writer.capacity() + 1); // (fails the writer)
    writer.i32(message.count);
    for (int i = 0; i < message.count; ++i)
        writer.u32(message.cheats[i]);
    return writer.ok();
}

bool read_hit(util::ByteReader& reader, Hit* message)
{
    message->object_id = reader.u8();
    message->fireball = reader.u8() != 0;
    reader.skip(2);
    message->score = reader.i32();
    return reader.ok();
}

bool write_hit(util::ByteWriter& writer, const Hit& message)
{
    writer.u8(message.object_id);
    writer.u8(message.fireball ? 1 : 0);
    writer.fill(0, 2);
    writer.i32(message.score);
    return writer.ok();
}

bool read_ran_into(util::ByteReader& reader, RanInto* message)
{
    message->object_id = reader.u8();
    reader.skip(3);
    read_floats(reader, message->push, 4);
    return reader.ok();
}

bool write_ran_into(util::ByteWriter& writer, const RanInto& message)
{
    writer.u8(message.object_id);
    writer.fill(0, 3);
    write_floats(writer, message.push, 4);
    return writer.ok();
}

bool read_run_script(util::ByteReader& reader, RunScript* message)
{
    message->name = reader.u32();
    message->object_id = reader.u32();
    return reader.ok();
}

bool write_run_script(util::ByteWriter& writer, const RunScript& message)
{
    writer.u32(message.name);
    writer.u32(message.object_id);
    return writer.ok();
}

bool read_spawn_script(util::ByteReader& reader, SpawnScript* message)
{
    message->name = reader.u32();
    message->object_id = reader.u32();
    message->node = reader.i32();
    message->permanent = reader.u8() != 0;
    return reader.ok();
}

bool write_spawn_script(util::ByteWriter& writer, const SpawnScript& message)
{
    writer.u32(message.name);
    writer.u32(message.object_id);
    writer.i32(message.node);
    writer.u8(message.permanent ? 1 : 0);
    writer.fill(0, 3);
    return writer.ok();
}

} // namespace online

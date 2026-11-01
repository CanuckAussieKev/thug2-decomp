// NOT ORIGINAL - the network manager's session messages' readers and writers. See gamenet_messages.h.

#include "engine/net/gamenet_messages.h"

#include "engine/util/byte_stream.h"

namespace online {

bool read_server_info(util::ByteReader& reader, ServerInfo* message)
{
    message->num_players = reader.i8();
    message->max_players = reader.i8();
    message->num_observers = reader.i8();
    message->max_observers = reader.i8();
    message->password = reader.u8() != 0;
    message->in_game = reader.u8() != 0;
    message->host_mode = reader.u8();
    message->logged_in = reader.u8() != 0;
    reader.fixed_string(message->name, sizeof(message->name), kServerInfoNameSize);
    reader.fixed_string(message->level, sizeof(message->level), kServerInfoTextSize);
    reader.fixed_string(message->mode, sizeof(message->mode), kServerInfoTextSize);
    message->skill_level = reader.u8();
    reader.skip(3);
    message->time = reader.u32();
    for (auto& player : message->players)
        reader.fixed_string(player, sizeof(player), kServerInfoNameSize);
    return reader.ok();
}

bool write_server_info(util::ByteWriter& writer, const ServerInfo& message)
{
    writer.i8(message.num_players);
    writer.i8(message.max_players);
    writer.i8(message.num_observers);
    writer.i8(message.max_observers);
    writer.u8(message.password ? 1 : 0);
    writer.u8(message.in_game ? 1 : 0);
    writer.u8(message.host_mode);
    writer.u8(message.logged_in ? 1 : 0);
    writer.fixed_string(message.name, kServerInfoNameSize);
    writer.fixed_string(message.level, kServerInfoTextSize);
    writer.fixed_string(message.mode, kServerInfoTextSize);
    writer.u8(message.skill_level);
    writer.fill(0, 3);
    writer.u32(message.time);
    for (const auto& player : message.players)
        writer.fixed_string(player, kServerInfoNameSize);
    return writer.ok();
}

bool read_session_brief(util::ByteReader& reader, SessionBrief* message)
{
    message->crown_start_slot = reader.u8();
    message->not_lan = reader.u8() != 0;
    message->game_id = reader.u8();
    message->pro_option_bits = reader.u8();
    message->max_players = reader.u8();
    reader.skip(3);
    message->time_left = reader.u32();
    message->game_type = reader.u32();
    message->level = reader.u32();
    message->teams = reader.u32();
    message->goals = reader.u32();
    message->team_graffiti_mode = reader.u32();
    message->time_limit = reader.u32();
    message->target_score = reader.u32();
    for (uint32_t& point : message->start_points)
        point = reader.u32();
    message->round = reader.u32();
    message->variant = reader.u8();
    reader.skip(3);
    return reader.ok();
}

bool write_session_brief(util::ByteWriter& writer, const SessionBrief& message)
{
    writer.u8(message.crown_start_slot);
    writer.u8(message.not_lan ? 1 : 0);
    writer.u8(message.game_id);
    writer.u8(message.pro_option_bits);
    writer.u8(message.max_players);
    writer.fill(0, 3);
    writer.u32(message.time_left);
    writer.u32(message.game_type);
    writer.u32(message.level);
    writer.u32(message.teams);
    writer.u32(message.goals);
    writer.u32(message.team_graffiti_mode);
    writer.u32(message.time_limit);
    writer.u32(message.target_score);
    for (uint32_t point : message.start_points)
        writer.u32(point);
    writer.u32(message.round);
    writer.u8(message.variant);
    writer.fill(0, 3);
    return writer.ok();
}

bool read_start_game(util::ByteReader& reader, StartGame* message)
{
    message->game_type = reader.u32();
    message->time_limit = reader.u32();
    message->target_score = reader.u32();
    message->fireball_level = reader.u32();
    reader.skip(4);
    message->stop_at_zero = reader.i8();
    message->reset_scores = reader.i8();
    message->game_id = reader.u8();
    reader.skip(1);
    message->teams = reader.u32();
    message->goals = reader.u32();
    message->team_graffiti_mode = reader.u32();
    message->crown_start_slot = reader.i8();
    reader.skip(3);
    for (uint32_t& point : message->start_points)
        point = reader.u32();
    return reader.ok();
}

bool write_start_game(util::ByteWriter& writer, const StartGame& message)
{
    writer.u32(message.game_type);
    writer.u32(message.time_limit);
    writer.u32(message.target_score);
    writer.u32(message.fireball_level);
    writer.u32(0);
    writer.i8(message.stop_at_zero);
    writer.i8(message.reset_scores);
    writer.u8(message.game_id);
    writer.u8(0);
    writer.u32(message.teams);
    writer.u32(message.goals);
    writer.u32(message.team_graffiti_mode);
    writer.i8(message.crown_start_slot);
    writer.fill(0, 3);
    for (uint32_t point : message.start_points)
        writer.u32(point);
    return writer.ok();
}

} // namespace online

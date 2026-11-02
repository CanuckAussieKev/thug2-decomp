// NOT ORIGINAL - the network manager's session messages' readers and writers. See gamenet_messages.h.

#include "wire/gamenet_messages.h"

#include <cstring>
#include <array>
#include <span>

#include "formats/byte_stream.h"
#include "engine/core/checked_index.h"

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
    reader.fixed_string(message->name.data(), sizeof(message->name), kServerInfoNameSize);
    reader.fixed_string(message->level.data(), sizeof(message->level), kServerInfoTextSize);
    reader.fixed_string(message->mode.data(), sizeof(message->mode), kServerInfoTextSize);
    message->skill_level = reader.u8();
    reader.skip(3);
    message->time = reader.u32();
    for (auto& player : message->players)
        reader.fixed_string(player.data(), sizeof(player), kServerInfoNameSize);
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
    writer.fixed_string(message.name.data(), kServerInfoNameSize);
    writer.fixed_string(message.level.data(), kServerInfoTextSize);
    writer.fixed_string(message.mode.data(), kServerInfoTextSize);
    writer.u8(message.skill_level);
    writer.fill(0, 3);
    writer.u32(message.time);
    for (const auto& player : message.players)
        writer.fixed_string(player.data(), kServerInfoNameSize);
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
    for (const uint32_t point : message.start_points)
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
    for (const uint32_t point : message.start_points)
        writer.u32(point);
    return writer.ok();
}

bool read_connect_info(util::ByteReader& reader, ConnectInfo* message)
{
    message->version = reader.i32();
    message->observer = reader.u8() != 0;
    message->can_queue_up_ = reader.u8() != 0;
    reader.fixed_string(message->password.data(), sizeof(message->password), kPasswordSize);
    message->broadband = reader.u8() != 0;
    message->rank = reader.i8();
    reader.fixed_string(message->cd_key_response_.data(), sizeof(message->cd_key_response_), kCdKeyResponseSize);
    return reader.ok();
}

bool write_connect_info(util::ByteWriter& writer, const ConnectInfo& message)
{
    writer.i32(message.version);
    writer.u8(message.observer ? 1 : 0);
    writer.u8(message.can_queue_up_ ? 1 : 0);
    writer.fixed_string(message.password.data(), kPasswordSize);
    writer.u8(message.broadband ? 1 : 0);
    writer.i8(message.rank);
    writer.fixed_string(message.cd_key_response_.data(), kCdKeyResponseSize);
    return writer.ok();
}

bool read_join_request(util::ByteReader& reader, JoinRequestHeader* message)
{
    read_connect_info(reader, message);
    message->profile = reader.i32();
    message->rating = reader.i32();
    message->drive_scheme_ = reader.i32();
    reader.fixed_string(message->name.data(), sizeof(message->name), kNameSize);
    return reader.ok();
}

bool write_join_request(util::ByteWriter& writer, const JoinRequestHeader& message)
{
    write_connect_info(writer, message);
    writer.i32(message.profile);
    writer.i32(message.rating);
    writer.i32(message.drive_scheme_);
    writer.fixed_string(message.name.data(), kNameSize);
    return writer.ok();
}

bool read_proceed_info(util::ByteReader& reader, ProceedInfo* message)
{
    message->public_ip_ = reader.u32();
    message->private_ip_ = reader.u32();
    message->port = reader.i32();
    reader.fixed_string(message->server_name_.data(), sizeof(message->server_name_), kServerNameSize);
    message->max_players_ = reader.i8();
    message->broadband = reader.u8() != 0;
    return reader.ok();
}

bool write_proceed_info(util::ByteWriter& writer, const ProceedInfo& message)
{
    writer.u32(message.public_ip_);
    writer.u32(message.private_ip_);
    writer.i32(message.port);
    writer.fixed_string(message.server_name_.data(), kServerNameSize);
    writer.i8(message.max_players_);
    writer.u8(message.broadband ? 1 : 0);
    writer.fill(0, 2);
    return writer.ok();
}

bool read_player_arrived(util::ByteReader& reader, PlayerArrived* message)
{
    message->profile_id = reader.i32();
    message->rating = reader.i32();
    message->flags = reader.u32();
    message->score = reader.i32();
    message->eliminated_time = reader.u32();
    message->accumulated_score = reader.u32();
    message->object_id = reader.i8();
    message->team = reader.i8();
    reader.skip(2);
    message->drive_scheme = reader.i32();
    reader.fixed_string(message->name.data(), sizeof(message->name), kNameSize);
    return reader.ok();
}

bool write_player_arrived(util::ByteWriter& writer, const PlayerArrived& message)
{
    writer.i32(message.profile_id);
    writer.i32(message.rating);
    writer.u32(message.flags);
    writer.i32(message.score);
    writer.u32(message.eliminated_time);
    writer.u32(message.accumulated_score);
    writer.i8(message.object_id);
    writer.i8(message.team);
    writer.fill(0, 2);
    writer.i32(message.drive_scheme);
    writer.fixed_string(message.name.data(), kNameSize);
    return writer.ok();
}

bool read_hud_notice(util::ByteReader& reader, HudNotice* message)
{
    message->properties = reader.u32();
    message->text = reader.u32();
    message->time = reader.i32();
    reader.fixed_string(message->string1.data(), sizeof(message->string1), kHudNoticeTextSize);
    reader.fixed_string(message->string2.data(), sizeof(message->string2), kHudNoticeTextSize);
    return reader.ok();
}

bool write_hud_notice(util::ByteWriter& writer, const HudNotice& message)
{
    writer.u32(message.properties);
    writer.u32(message.text);
    writer.i32(message.time);
    writer.fixed_string(message.string1.data(), kHudNoticeTextSize);
    writer.fixed_string(message.string2.data(), kHudNoticeTextSize);
    return writer.ok();
}

bool read_start_request(util::ByteReader& reader, StartRequest* message)
{
    message->game_type = reader.u32();
    message->time_limit = reader.u32();
    message->skill_level = reader.u32();
    message->fireball_level = reader.u32();
    message->player_collision = reader.u32();
    message->friendly_fire = reader.u32();
    reader.skip(4);
    message->target_score = reader.u32();
    message->stop_at_zero = reader.u32();
    message->score_reset = reader.u32();
    message->ctf_game_type = reader.u32();
    message->goals = reader.u32();
    message->team_graffiti_mode = reader.u32();
    return reader.ok();
}

bool write_start_request(util::ByteWriter& writer, const StartRequest& message)
{
    writer.u32(message.game_type);
    writer.u32(message.time_limit);
    writer.u32(message.skill_level);
    writer.u32(message.fireball_level);
    writer.u32(message.player_collision);
    writer.u32(message.friendly_fire);
    writer.u32(0);
    writer.u32(message.target_score);
    writer.u32(message.stop_at_zero);
    writer.u32(message.score_reset);
    writer.u32(message.ctf_game_type);
    writer.u32(message.goals);
    writer.u32(message.team_graffiti_mode);
    return writer.ok();
}

bool read_kick_request(util::ByteReader& reader, KickRequest* message)
{
    message->kick = reader.u8();
    message->index = reader.i8();
    reader.fixed_string(message->name.data(), sizeof(message->name), kNameSize);
    return reader.ok();
}

bool write_kick_request(util::ByteWriter& writer, const KickRequest& message)
{
    writer.u8(message.kick);
    writer.i8(message.index);
    writer.fixed_string(message.name.data(), kNameSize);
    return writer.ok();
}

bool read_goal_message(util::ByteReader& reader, GoalMessage* message)
{
    message->id = reader.u8();
    reader.skip(3);
    message->goal = reader.u32();
    return reader.ok();
}

bool write_goal_message(util::ByteWriter& writer, const GoalMessage& message)
{
    writer.u8(message.id);
    writer.fill(0, 3);
    writer.u32(message.goal);
    return writer.ok();
}

bool read_level_switch(util::ByteReader& reader, LevelSwitch* message)
{
    message->level = reader.u32();
    message->flag = reader.u8();
    return reader.ok();
}

bool write_level_switch(util::ByteWriter& writer, const LevelSwitch& message)
{
    writer.u32(message.level);
    writer.u8(message.flag);
    writer.fill(0, 3);
    return writer.ok();
}

namespace {

// The rest of the message up to its NUL, into `out` (`size` bytes, terminated): false when there's no NUL before the
// end (what there is still copied, cut to fit).
bool read_rest_string(util::ByteReader& reader, char* out_text, size_t size)
{
    const std::span<char> out(out_text, size);
    size_t length = 0;
    const std::span<const uint8_t> rest = reader.rest();
    const size_t remaining = rest.size();
    while (length < remaining && core::at(rest, length))
        ++length;
    const bool ended = length < remaining;
    const size_t copy = length < size - 1 ? length : size - 1;
    std::memcpy(out_text, rest.data(), copy);
    core::at(out, copy) = 0;
    reader.skip(ended ? length + 1 : length);
    return ended;
}

void write_cut_string(util::ByteWriter& writer, const char* text, size_t size)
{
    const size_t length = strnlen(text, size - 1);
    writer.bytes(text, length);
    writer.u8(0);
}

} // namespace

bool read_chat(util::ByteReader& reader, Chat* message)
{
    message->color = reader.u8();
    reader.fixed_string(message->name.data(), sizeof(message->name), kChatNameSize);
    if (!reader.ok())
        return false;
    read_rest_string(reader, message->text.data(), sizeof(message->text));
    return reader.ok();
}

bool write_chat(util::ByteWriter& writer, const Chat& message)
{
    writer.u8(message.color);
    writer.fixed_string(message.name.data(), kChatNameSize);
    write_cut_string(writer, message.text.data(), kChatTextSize);
    return writer.ok();
}

bool read_news(util::ByteReader& reader, News* news)
{
    news->checksum = reader.u32();
    news->size = reader.i32();
    if (!reader.ok() || news->size < 0 || static_cast<size_t>(news->size) > reader.remaining())
        return false;
    news->data = reader.cursor();
    util::ByteReader text(news->data, static_cast<size_t>(news->size));
    news->motd = text.c_string();
    if (!news->motd)
        return false;
    auto padded = static_cast<int32_t>((std::strlen(news->motd) + 4) & ~static_cast<size_t>(3));
    news->motd_size = padded < news->size ? padded : news->size;
    return reader.skip(static_cast<size_t>(news->size));
}

namespace thugpro {

bool read_connect_trailer(util::ByteReader& reader, ConnectTrailer* message)
{
    reader.bytes(message->magic.data(), sizeof(message->magic));
    reader.skip(2);
    message->version = reader.u32();
    message->release = reader.u32();
    message->scripts_hash_ = reader.u32();
    message->machine_id_ = reader.u32();
    message->modified_scripts_ = reader.u8() != 0;
    return reader.ok();
}

bool write_connect_trailer(util::ByteWriter& writer, const ConnectTrailer& message)
{
    writer.bytes(message.magic.data(), sizeof(message.magic));
    writer.fill(0, 2);
    writer.u32(message.version);
    writer.u32(message.release);
    writer.u32(message.scripts_hash_);
    writer.u32(message.machine_id_);
    writer.u8(message.modified_scripts_ ? 1 : 0);
    writer.fill(0, 3);
    return writer.ok();
}

bool read_chat_message(util::ByteReader& reader, ChatMessage* message)
{
    if (reader.remaining() <= kChatMessageHeaderSize || reader.remaining() > kChatMessageSize)
        return false;
    message->machine_id_ = reader.u32();
    message->obj_id_ = reader.u8();
    std::array<char, 17> name{};
    reader.fixed_string(name.data(), sizeof(name), 16);
    if (std::strlen(name.data()) >= 16)
        return false;
    std::memcpy(message->name.data(), name.data(), sizeof(message->name));
    return read_rest_string(reader, message->text.data(), sizeof(message->text)) && reader.ok();
}

bool write_chat_message(util::ByteWriter& writer, const ChatMessage& message)
{
    writer.u32(message.machine_id_);
    writer.u8(message.obj_id_);
    std::array<char, 16> name = {};
    std::memcpy(name.data(), message.name.data(), strnlen(message.name.data(), 15));
    writer.bytes(name.data(), sizeof(name));
    write_cut_string(writer, message.text.data(), kChatMessageTextSize);
    return writer.ok();
}

bool read_server_info(util::ByteReader& reader, ServerInfo* message)
{
    message->time_of_day_ = reader.u32();
    message->weather = reader.u32();
    const uint64_t low = reader.u32();
    const uint64_t high = reader.u32();
    message->flags = low | high << 32;
    return reader.ok();
}

bool write_server_info(util::ByteWriter& writer, const ServerInfo& message)
{
    writer.u32(message.time_of_day_);
    writer.u32(message.weather);
    writer.u32(static_cast<uint32_t>(message.flags));
    writer.u32(static_cast<uint32_t>(message.flags >> 32));
    return writer.ok();
}

bool write_change_level(util::ByteWriter& writer, const LevelSwitch& level, const char* name)
{
    write_level_switch(writer, level);
    std::array<char, kChangeLevelNameSize> field = {};
    if (name)
        std::memcpy(field.data(), name, strnlen(name, kChangeLevelNameSize - 1));
    writer.bytes(field.data(), sizeof(field));
    return writer.ok();
}

} // namespace thugpro

} // namespace online

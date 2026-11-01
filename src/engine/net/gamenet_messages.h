#pragma once

#include <cstddef>
#include <cstdint>

namespace util {
class ByteReader;
class ByteWriter;
} // namespace util

// NOT ORIGINAL (the formats are the original's) - session messages of the network manager as they are on the wire,
// with their readers and writers (the original built them as structs on the stack and cast received bytes onto
// offsets). Little-endian; the bytes marked (0) were padding or never written in the original - sent as zeros here,
// ignored when read.
//
//   0x10 server info (0xE0 bytes; a LAN server's answer to a search - read by on_server_reply_msg)
//     +0x00 i8 x4 players, max players, observers, max observers   +0x04 u8 password   +0x05 u8 in a game (not the
//     lobby)   +0x06 u8 the hosting role   +0x07 u8 logged in (internet play)   +0x08 char[16] the server's name
//     +0x18 char[32] the level   +0x38 char[32] the game mode   +0x58 u8 skill level, (0) x3
//     +0x5C u32 the asker's time (echoed: the ping)   +0x60 char[16] x8 the players' names (zero-padded)
//   0x33 session brief (0x50 bytes; to a player joining, read by SkateNet::on_run_start_msg)
//     +0x00 u8  the crown's start slot     +0x01 u8  not LAN   +0x02 u8  game id   +0x03 u8  the pro set option bits
//     +0x04 u8  max players, (0) x3        +0x08 u32 time left (seconds)          +0x0C u32 game type
//     +0x10 u32 level   +0x14 u32 teams    +0x18 u32 the goals preference         +0x1C u32 team graffiti mode
//     +0x20 u32 time limit                 +0x24 u32 target score                 +0x28 u32 x8 the start points
//     +0x48 u32 the round                  +0x4C u8  which of the mode's goals runs (scavenger hunt / elimiskate),
//                                          (0) x3
//   0x34 start game (0x48 bytes; to every player, read by handle_start_game)
//     +0x00 u32 game type   +0x04 u32 time limit (0: none)   +0x08 u32 target score   +0x0C u32 fireball level
//     +0x10 (0) x4          +0x14 u8 stop at zero  +0x15 u8 reset scores  +0x16 u8 game id  (0)
//     +0x18 u32 teams       +0x1C u32 the goals preference   +0x20 u32 team graffiti mode
//     +0x24 i8 the crown's start slot, (0) x3                +0x28 u32 x8 the start points
namespace online {

constexpr uint8_t kMsgSessionBrief = 0x33;
constexpr uint8_t kMsgStartGame = 0x34;
constexpr size_t kSessionBriefSize = 0x50;
constexpr size_t kStartGameSize = 0x48;
constexpr int kStartPoints = 8;
// Fields the THUGPRO guards (net_guards.cpp) check and rewrite in place, before the handlers read the message.
constexpr uint32_t kSessionBriefGameTypeAt = 0x0C;
constexpr uint32_t kSessionBriefLevelAt = 0x10;
constexpr uint32_t kStartGameTypeAt = 0x00;

constexpr uint8_t kMsgServerInfo = 0x10;
constexpr size_t kServerInfoSize = 0xE0;
constexpr int kServerInfoPlayers = 8;
constexpr size_t kServerInfoNameSize = 16;
constexpr size_t kServerInfoTextSize = 32;

struct ServerInfo {
    int8_t num_players = 0;
    int8_t max_players = 0;
    int8_t num_observers = 0;
    int8_t max_observers = 0;
    bool password = false;
    bool in_game = false;
    uint8_t host_mode = 0;
    bool logged_in = false;
    // (terminated here; on the wire their whole fields, terminated when shorter)
    char name[kServerInfoNameSize + 1] = {};
    char level[kServerInfoTextSize + 1] = {};
    char mode[kServerInfoTextSize + 1] = {};
    uint8_t skill_level = 0;
    uint32_t time = 0;
    char players[kServerInfoPlayers][kServerInfoNameSize + 1] = {};
};
bool read_server_info(util::ByteReader& reader, ServerInfo* message);
bool write_server_info(util::ByteWriter& writer, const ServerInfo& message);

struct SessionBrief {
    uint8_t crown_start_slot = 0;
    bool not_lan = false;
    uint8_t game_id = 0;
    uint8_t pro_option_bits = 0;
    uint8_t max_players = 0;
    uint32_t time_left = 0;
    uint32_t game_type = 0;
    uint32_t level = 0;
    uint32_t teams = 0;
    uint32_t goals = 0;
    uint32_t team_graffiti_mode = 0;
    uint32_t time_limit = 0;
    uint32_t target_score = 0;
    uint32_t start_points[kStartPoints] = {};
    uint32_t round = 0;
    uint8_t variant = 0;
};
bool read_session_brief(util::ByteReader& reader, SessionBrief* message);
bool write_session_brief(util::ByteWriter& writer, const SessionBrief& message);

struct StartGame {
    uint32_t game_type = 0;
    uint32_t time_limit = 0;
    uint32_t target_score = 0;
    uint32_t fireball_level = 0;
    int8_t stop_at_zero = 0; // (bools, kept as the bytes: the handler passes them on widened)
    int8_t reset_scores = 0;
    uint8_t game_id = 0;
    uint32_t teams = 0;
    uint32_t goals = 0;
    uint32_t team_graffiti_mode = 0;
    int8_t crown_start_slot = 0;
    uint32_t start_points[kStartPoints] = {};
};
bool read_start_game(util::ByteReader& reader, StartGame* message);
bool write_start_game(util::ByteWriter& writer, const StartGame& message);

// --- The join messages (THUG2's) ---
//
//   3 connection request (92; the joining client's, read by the host's on_link_established / joins_accepted)
//     +0x00 i32 the network version (kVersion)   +0x04 u8 observer   +0x05 u8 can queue up (a game in progress is no
//     refusal)   +0x06 char[10] the password   +0x10 u8 broadband   +0x11 i8 the rank (the rank limit's test)
//     +0x12 char[74] the GameSpy CD-key response (72 characters) to the host's challenge
//     (THUGPRO's clients add 32 bytes: ConnectTrailer below)
//   80 join request (0x78 + the appearance's used length): the connection request's 92 bytes, then
//     +0x5C i32 the GameSpy profile id   +0x60 i32 the rating   +0x64 i32 the drive scheme   +0x68 char[16] the
//     name   +0x78 the appearance (RiderIdentity::emit_into_buffer, up to 0x1000 bytes)
//   79 join proceed (32; the host's answer, read by the client's on_go_ahead_msg)
//     +0x00 u32 the public ip   +0x04 u32 the private ip (internet hosts)   +0x08 i32 the port   +0x0C char[16] the
//     server's name   +0x1C i8 max players   +0x1D u8 broadband (the host's)   (0) x2
constexpr int32_t kVersion = 0x0004000B; // THUG2 PC's network version (both ends must match)
constexpr int kPasswordSize = 10;        // 9 characters + NUL
constexpr int kNameSize = 16;            // 15 characters + NUL
constexpr int kServerNameSize = 16;
constexpr int kCdKeyResponseSize = 74;
constexpr uint32_t kAppearanceSize = 0x1000;
constexpr size_t kConnectInfoSize = 92;
constexpr uint32_t kJoinInfoHeaderSize = 0x78;
constexpr size_t kProceedInfoSize = 32;

struct ConnectInfo {
    int32_t version;
    bool observer;
    bool can_queue_up_;
    char password[kPasswordSize]; // (terminated: on the wire its 10 bytes)
    bool broadband;
    int8_t rank;
    char cd_key_response_[kCdKeyResponseSize];
};
bool read_connect_info(util::ByteReader& reader, ConnectInfo* message);
bool write_connect_info(util::ByteWriter& writer, const ConnectInfo& message);

// The join request's fields (the appearance follows them on the wire).
struct JoinRequestHeader : ConnectInfo {
    int32_t profile;
    int32_t rating;
    int32_t drive_scheme_;
    char name[kNameSize];
};
struct JoinRequestBlock : JoinRequestHeader {
    uint8_t appearance[kAppearanceSize]; // (RiderIdentity::emit_into_buffer)
};
bool read_join_request(util::ByteReader& reader, JoinRequestHeader* message);
bool write_join_request(util::ByteWriter& writer, const JoinRequestHeader& message);

struct ProceedInfo {
    uint32_t public_ip_;
    uint32_t private_ip_;
    int32_t port;
    char server_name_[kServerNameSize];
    int8_t max_players_;
    bool broadband; // (the host's: the client's connection to it is narrowband if not)
};
bool read_proceed_info(util::ByteReader& reader, ProceedInfo* message);
bool write_proceed_info(util::ByteWriter& writer, const ProceedInfo& message);

// --- The session's other messages ---
//
//   0x22 player arrived (0x30 + the appearance; the server's, read by on_player_added_msg)
//     +0x00 i32 the GameSpy profile id   +0x04 i32 the rating   +0x08 u32 the flags   +0x0C i32 the score
//     +0x10 u32 the eliminated time (not read)   +0x14 u32 the accumulated score   +0x18 i8 the object id   +0x19 i8 the team   (0) x2
//     +0x1C i32 the drive scheme   +0x20 char[16] the name   +0x30 the appearance (RiderIdentity::emit_into_buffer)
//   0x3F hud notice (0x2C; the server's broadcast panel message, read by on_hud_notice_msg)
//     +0x00 u32 the properties' name's CRC   +0x04 u32 the text   +0x08 i32 the time   +0x0C char[16] string 1
//     +0x1C char[16] string 2
//   0x60 start request (0x34; the first-come-first-served host to the server, read by handle_host_options)
//     u32 x13: the game type, the time limit, the skill level, the fireball level, player collision, friendly fire,
//     (0), the target score, stop at zero, score reset, the CTF game type, the goals, the team graffiti mode
//   0x61 kick request (18): kick (u8: 1, else ban), the player's index (i8), the player's name (char[16])
//   0x6E / 0x6F goal completed / started (8; a client's to the server, the server's to the players): an id
//     (u8: the game id from a client, the player's object id from the server), (0) x3, the goal's name (u32)
//   0x46 level switch / 0x6B request game data (8): the level (u32), a flag (u8: 0x6B's 'F' asks for the level
//     change too; 0x46's 0 - on_level_switch_msg acts on a non-zero one only), (0) x3
constexpr uint8_t kMsgPlayerArrived = 0x22;
constexpr uint8_t kMsgHudNotice = 0x3F;
constexpr uint8_t kMsgStartRequest = 0x60;
constexpr uint8_t kMsgKickRequest = 0x61;
constexpr uint8_t kMsgLevelSwitch = 0x46;
constexpr uint8_t kMsgRequestGameData = 0x6B;
constexpr uint8_t kMsgGoalCompleted = 0x6E;
constexpr uint8_t kMsgGoalStarted = 0x6F;
constexpr size_t kPlayerArrivedHeaderSize = 0x30;
constexpr size_t kHudNoticeSize = 0x2C;
constexpr size_t kHudNoticeTextSize = 16;
constexpr size_t kStartRequestSize = 0x34;
constexpr size_t kKickRequestSize = 2 + kNameSize;
constexpr size_t kGoalMessageSize = 8;
constexpr size_t kLevelSwitchSize = 8;
// Fields the THUGPRO guards (net_guards.cpp) check in place.
constexpr uint32_t kLevelSwitchLevelAt = 0;

struct PlayerArrived {
    int32_t profile_id = 0;
    int32_t rating = 0;
    uint32_t flags = 0;
    int32_t score = 0;
    uint32_t eliminated_time = 0;
    uint32_t accumulated_score = 0;
    int8_t object_id = 0;
    int8_t team = 0;
    int32_t drive_scheme = 0;
    char name[kNameSize + 1] = {}; // (terminated here)
};
bool read_player_arrived(util::ByteReader& reader, PlayerArrived* message);
bool write_player_arrived(util::ByteWriter& writer, const PlayerArrived& message);

struct HudNotice {
    uint32_t properties = 0;
    uint32_t text = 0;
    int32_t time = 0;
    char string1[kHudNoticeTextSize + 1] = {}; // (terminated here)
    char string2[kHudNoticeTextSize + 1] = {};
};
bool read_hud_notice(util::ByteReader& reader, HudNotice* message);
bool write_hud_notice(util::ByteWriter& writer, const HudNotice& message);

struct StartRequest {
    uint32_t game_type = 0;
    uint32_t time_limit = 0;
    uint32_t skill_level = 0;
    uint32_t fireball_level = 0;
    uint32_t player_collision = 0;
    uint32_t friendly_fire = 0;
    uint32_t target_score = 0;
    uint32_t stop_at_zero = 0;
    uint32_t score_reset = 0;
    uint32_t ctf_game_type = 0;
    uint32_t goals = 0;
    uint32_t team_graffiti_mode = 0;
};
bool read_start_request(util::ByteReader& reader, StartRequest* message);
bool write_start_request(util::ByteWriter& writer, const StartRequest& message);

struct KickRequest {
    uint8_t kick = 0;
    int8_t index = 0;
    char name[kNameSize + 1] = {}; // (terminated here)
};
bool read_kick_request(util::ByteReader& reader, KickRequest* message);
bool write_kick_request(util::ByteWriter& writer, const KickRequest& message);

struct GoalMessage {
    uint8_t id = 0;
    uint32_t goal = 0;
};
bool read_goal_message(util::ByteReader& reader, GoalMessage* message);
bool write_goal_message(util::ByteWriter& writer, const GoalMessage& message);

struct LevelSwitch {
    uint32_t level = 0;
    uint8_t flag = 0;
};
bool read_level_switch(util::ByteReader& reader, LevelSwitch* message);
bool write_level_switch(util::ByteWriter& writer, const LevelSwitch& message);

// --- Chat ---
//
//   0x4C chat (0x12 + the text's length; at most 0x94): the sender's colour (u8: its skater's object id, 8 for an
//     observer), the name (char[16]), the text and its NUL (at most 131 bytes) - the server relays it as it is
//     (THUGPRO's peers put a machine id in front: thugpro::ChatMessage below)
constexpr uint8_t kMsgChat = 0x4C;
constexpr size_t kChatNameSize = 16;
constexpr size_t kChatTextSize = 131;
constexpr size_t kChatHeaderSize = 1 + kChatNameSize;
constexpr size_t kChatSize = kChatHeaderSize + kChatTextSize; // 0x94

struct Chat {
    uint8_t color = 0;
    char name[kChatNameSize + 1] = {}; // (terminated here; on the wire its 16 bytes)
    char text[kChatTextSize] = {};     // (the text, cut to 130 characters, and its NUL)
};
// (the text: what is left of the message, up to its NUL - cut there when it has none)
bool read_chat(util::ByteReader& reader, Chat* message);
bool write_chat(util::ByteWriter& writer, const Chat& message);

// --- The news (motd.dat, downloaded by the server list's news thread; each byte ^ 0x6D) ---
//   the text's caseless CRC (u32), the size of what follows (i32), the message of the day (NUL-ended, padded to a
//   word), then the rest of the data (pending_data_)
struct News {
    uint32_t checksum = 0;
    int32_t size = 0;
    const uint8_t* data = nullptr; // (the `size` bytes after the header, in place)
    const char* motd = nullptr;    // (= data: ended inside them)
    int32_t motd_size = 0;         // (with its NUL, padded to a word; at most `size`)
};
// (fails when the size is negative or past the end, or the text has no NUL within it)
bool read_news(util::ByteReader& reader, News* news);

namespace thugpro {

// NOT ORIGINAL - THUGPRO's connection request trailer (after THUG2's 92 bytes; engine/net/thugpro_join.h, TP-R12):
//   +0x5C char[10] "_THUG_PRO_"   (0) x2   +0x68 u32 its protocol version   +0x6C u32 its release type
//   +0x70 u32 its scripts' hash   +0x74 u32 its machine id   +0x78 u8 modified scripts   (0) x3
constexpr size_t kConnectTrailerSize = 32;
constexpr size_t kConnectRequestSize = kConnectInfoSize + kConnectTrailerSize; // 124
constexpr char kMagic[10] = {'_', 'T', 'H', 'U', 'G', '_', 'P', 'R', 'O', '_'};
struct ConnectTrailer {
    char magic[10];
    uint32_t version;
    uint32_t release;
    uint32_t scripts_hash_;
    uint32_t machine_id_;
    bool modified_scripts_;
};
bool read_connect_trailer(util::ByteReader& reader, ConnectTrailer* message);
bool write_connect_trailer(util::ByteWriter& writer, const ConnectTrailer& message);

// NOT ORIGINAL - THUGPRO's messages (engine/net/thugpro_peer.h: the peer mode; register TP-R2 ... TP-R5):
//   76 chat (21 + the text's length; at most 149): the sender's machine id (u32), then THUG2's chat - the object id
//     (u8), the name (char[16], ended inside it), the text and its NUL (at most 128 bytes)
//   155 server info (16): the time of day (u32), the weather (u32), the flags (u64: bits 0-7 the level options,
//     8-15 the pro sets)
//   70 change level (40, when we host): THUG2's 8 bytes (LevelSwitch), then the level's name (char[32], NUL-ended)
//   156 / 157 / 158 / 159 updates (4): a word
constexpr size_t kChatMessageHeaderSize = 4 + 1 + 16;
constexpr size_t kChatMessageTextSize = 128;
constexpr size_t kChatMessageSize = kChatMessageHeaderSize + kChatMessageTextSize; // 149
constexpr size_t kServerInfoSize = 16;
constexpr size_t kChangeLevelNameSize = 32;
constexpr size_t kChangeLevelThugproSize = kLevelSwitchSize + kChangeLevelNameSize;

struct ChatMessage {
    uint32_t machine_id_ = 0;
    uint8_t obj_id_ = 0;
    char name[16] = {};                    // (ended inside its field)
    char text[kChatMessageTextSize] = {}; // (at most 127 characters and the NUL)
};
// (fails - as THUGPRO drops them - when the message is no longer than its header or longer than 149 bytes, or its
// name or text doesn't end inside it)
bool read_chat_message(util::ByteReader& reader, ChatMessage* message);
// (the name cut to 15 characters, the text to 127)
bool write_chat_message(util::ByteWriter& writer, const ChatMessage& message);

struct ServerInfo {
    uint32_t time_of_day_ = 0;
    uint32_t weather = 0;
    uint64_t flags = 0;
};
bool read_server_info(util::ByteReader& reader, ServerInfo* message);
bool write_server_info(util::ByteWriter& writer, const ServerInfo& message);

// (the name cut to 31 characters)
bool write_change_level(util::ByteWriter& writer, const LevelSwitch& level, const char* name);

} // namespace thugpro

} // namespace online

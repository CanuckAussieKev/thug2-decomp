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

} // namespace online

#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "wire/message_ids.h"
#include <span>

namespace util {
class ByteReader;
class ByteWriter;
} // namespace util

// NOT ORIGINAL (the formats are the original's) - the other game messages of a network game, as they are on the wire,
// with their readers and writers (the original built them as structs / byte arrays on the stack and cast received
// bytes onto offsets). Little-endian; the bytes marked (0) are padding (sent as zeros here - some of the original's
// were whatever its stack held -, not read).
//
//   0x8F hunt placements (server -> players, 0x58)  the player's object id (i8), (0) x3, the count (i32), then 5
//                              positions (4 floats each; only the first `count` meant)
//   0x8F hunt placement (a client -> the server, 16)  the position (4 floats)
//   0x90 hunt item picked up (4)  the item's id (u32)
//   0x95 hunt items left (2)   the player's object id or the team (i8), how many are left (u8: 1 or 5)
//   0x89 board vehicle (8)     the skater's object id (i8), (0) x3, the vehicle's control type (u32)
//   0x83 projectile (0x38)     the time (u32: 0 from its client; the server puts the sender's latency in), the
//                              position and the velocity (4 floats each), the type (u32: fireball...), the ammo type
//                              (u32), the radius (i32), the scale (f32), the owner's object id (i8; the server puts
//                              the sender's in), (0) x3
//   0x99 place tag (0x28)      the object id (i8), sticker (u8), (0) x2, the start and the end (4 floats each), the
//                              size (i32)
//   0x35 / 0x36 terrain sound (12)  the position x / y / z (i16 each, truncated), the sound table (u8), the terrain
//                              (u8), the volume, pitch and value percentages (i8 each, truncated), (0)
//   0x7F toggle cheat (8)      the cheat's name (u32), on (u8), (0) x3
//   0x82 cheats (4 + 4 a cheat)  the count (i32), the cheats' names (u32)
//   0x48 run script (8 + the params)  the script's name (u32), the object's id (u32; -1: none), the parameters as a
//                              script structure buffer (up to 0x400 bytes)
//   0x49 spawn script (16)     the script's name (u32), the object's id (u32; -1: none), the node (i32), permanent
//                              (u8), (0) x3
//   0x2D player gone (2)       the player's object id (i8), the reason (i8)
//   (0x46 level switch: gamenet_messages.h)
//   0x84 hit / 0x85 hit owner (8)  the other skater's object id (u8), fireball (u8), (0) x2, the score the hit takes
//                              (i32)
//   0x43 ran into (20)         the other's object id (u8; bit 7: it was driving), (0) x3, the push (4 floats);
//   0x44 ran into ack (1)      the same first byte
//   0x96 graffiti for (1)      the player's object id (u8); 0x9A graffiti parts (4): which board parts show it (u32)
namespace online {


constexpr int kHuntPlacements = 5;
constexpr size_t kHuntPlacementsSize = 0x58;
constexpr size_t kHuntPlacementSize = 16;
constexpr size_t kBoardVehicleSize = 8;
constexpr size_t kProjectileSize = 0x38;
constexpr size_t kPlaceTagSize = 0x28;
constexpr size_t kTerrainSoundSize = 12;
constexpr size_t kToggleCheatSize = 8;
constexpr int kMaxCheats = 32;
constexpr size_t kRunScriptHeaderSize = 8;
constexpr size_t kRunScriptParamsSize = 0x400;
constexpr size_t kSpawnScriptSize = 16;
constexpr size_t kHitSize = 8;
constexpr size_t kRanIntoSize = 20;
// Fields the server rewrites in a projectile it relays (in place, before passing it on).
constexpr uint32_t kProjectileTimeAt = 0x00;
constexpr uint32_t kProjectileOwnerAt = 0x34;

struct HuntPlacements {
    int8_t player_id = 0;
    int32_t count = 0; // (the positions follow: read_hunt_position for each)
    std::array<std::array<float, 4>, kHuntPlacements> positions = {};
};
bool read_hunt_placements(util::ByteReader& reader, HuntPlacements* message);
bool read_hunt_position(util::ByteReader& reader, std::span<float, 4> position);
bool write_hunt_placements(util::ByteWriter& writer, const HuntPlacements& message);
bool write_hunt_position(util::ByteWriter& writer, std::span<const float, 4> position);

struct BoardVehicle {
    int8_t skater_id = 0;
    uint32_t control_type = 0;
};
bool read_board_vehicle(util::ByteReader& reader, BoardVehicle* message);
bool write_board_vehicle(util::ByteWriter& writer, const BoardVehicle& message);

struct Projectile {
    uint32_t time = 0;
    std::array<float, 4> position = {};
    std::array<float, 4> velocity = {};
    uint32_t type = 0;
    uint32_t ammo_type = 0;
    int32_t radius = 0;
    float scale = 0.0F;
    int8_t owner_id = 0;
};
bool read_projectile(util::ByteReader& reader, Projectile* message);
bool write_projectile(util::ByteWriter& writer, const Projectile& message);

struct PlaceTag {
    int8_t object_id = 0;
    bool sticker = false;
    std::array<float, 4> start = {};
    std::array<float, 4> end = {};
    int32_t size = 0;
};
bool read_place_tag(util::ByteReader& reader, PlaceTag* message);
bool write_place_tag(util::ByteWriter& writer, const PlaceTag& message);

struct TerrainSound {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    uint8_t table = 0;
    uint8_t terrain = 0;
    int8_t volume = 0;
    int8_t pitch = 0;
    int8_t value = 0;
};
bool read_terrain_sound(util::ByteReader& reader, TerrainSound* message);
bool write_terrain_sound(util::ByteWriter& writer, const TerrainSound& message);

struct ToggleCheat {
    uint32_t cheat = 0;
    bool on = false;
};
bool read_toggle_cheat(util::ByteReader& reader, ToggleCheat* message);
bool write_toggle_cheat(util::ByteWriter& writer, const ToggleCheat& message);

struct Cheats {
    int32_t count = 0; // (0 to kMaxCheats)
    std::array<uint32_t, kMaxCheats> cheats = {};
};
// (fails when the count is negative, more than kMaxCheats or more than the message holds)
bool read_cheats(util::ByteReader& reader, Cheats* message);
bool write_cheats(util::ByteWriter& writer, const Cheats& message);

struct Hit {
    uint8_t object_id = 0;
    bool fireball = false;
    int32_t score = 0;
};
bool read_hit(util::ByteReader& reader, Hit* message);
bool write_hit(util::ByteWriter& writer, const Hit& message);

struct RanInto {
    uint8_t object_id = 0; // (with bit 7: driving)
    std::array<float, 4> push = {};
};
bool read_ran_into(util::ByteReader& reader, RanInto* message);
bool write_ran_into(util::ByteWriter& writer, const RanInto& message);

// 0x48's header (the parameters follow: script::decode_from_bytes on what is left).
struct RunScript {
    uint32_t name = 0;
    uint32_t object_id = 0;
};
bool read_run_script(util::ByteReader& reader, RunScript* message);
bool write_run_script(util::ByteWriter& writer, const RunScript& message);

struct SpawnScript {
    uint32_t name = 0;
    uint32_t object_id = 0;
    int32_t node = 0;
    bool permanent = false;
};
bool read_spawn_script(util::ByteReader& reader, SpawnScript* message);
bool write_spawn_script(util::ByteWriter& writer, const SpawnScript& message);

} // namespace online

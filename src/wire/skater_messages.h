#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

namespace util {
class ByteReader;
class ByteWriter;
} // namespace util

// NOT ORIGINAL (the formats are the original's) - the messages a skater's client sends about its animation and model
// (the blend channel, the board's flip and rotation, the model's hidden parts and spins, the special items), which
// the server relays to the other players as they are and their clients keep in the skater's state history
// (game/skate/skate_net_relay.cpp, game/rider/nonlocal_playback.cpp). The original built them as byte arrays / structs
// on the stack and read them back by offset. Little-endian; the bytes marked (0) are padding (sent as zeros here -
// the original's were whatever its stack held -, not read).
//
//   0x29 play anim (14)        the loop type (u8), the object id (u8), the animation's name (u32), then from, to,
//                              the blend period and the speed x 4096 (i16 each; read back unsigned, as the original)
//   0x2A board rotated (2)     the object id (u8), rotated (u8)
//   0x2B wobble target (2)     the object id (u8), the target x 255 (u8)
//   0x2C flip (2)              the object id (u8), flipped (u8)
//   0x3C wobble details (2)    which of the known values the five details have (u8: two bits, then one each), the
//                              object id (u8)
//   0x3D loop type (2)         the object id (u8), the loop type (u8)
//   0x3E hide geom (8)         the geom's name (u32), the object id (u8), hidden (u8), (0) x2
//   0x41 speed (8)             the speed (f32), the object id (u8), (0) x3
//   0x76 spin model (16)       the period (i32, ms), the sine power (i32), the start angle and the range (i16 each),
//                              the object id (u8), hold on the last angle (u8), the axes (u8: bit 0 x...), (0)
//   0x77 create item (12)      the item's params structure (u32, a name), the bone (u32), the object id (u8), the
//                              slot (u8), (0) x2
//   0x78 destroy item (12)     (0) x8, the object id (u8), the slot (u8), (0) x2
//   0x8C cancel spins (1)      the object id (u8)
namespace online {

constexpr uint8_t kMsgPlayAnim = 0x29;
constexpr uint8_t kMsgBoardRotated = 0x2A;
constexpr uint8_t kMsgWobbleTarget = 0x2B;
constexpr uint8_t kMsgFlip = 0x2C;
constexpr uint8_t kMsgWobbleDetails = 0x3C;
constexpr uint8_t kMsgLoopType = 0x3D;
constexpr uint8_t kMsgHideGeom = 0x3E;
constexpr uint8_t kMsgAnimSpeed = 0x41;
constexpr uint8_t kMsgSpinModel = 0x76;
constexpr uint8_t kMsgCreateSpecialItem = 0x77;
constexpr uint8_t kMsgDestroySpecialItem = 0x78;
constexpr uint8_t kMsgCancelSpins = 0x8C;

constexpr size_t kPlayAnimSize = 14;
// Fields the THUGPRO guards (net_guards.cpp) check in place.
constexpr uint32_t kPlayAnimAnimAt = 2;
constexpr uint32_t kSpecialItemParamsAt = 0;
constexpr size_t kHideGeomSize = 8;
constexpr size_t kAnimSpeedSize = 8;
constexpr size_t kSpinModelSize = 16;
constexpr size_t kSpecialItemSize = 12;

struct PlayAnimMessage {
    uint8_t loop_type = 0;
    uint8_t object_id = 0;
    uint32_t anim = 0;
    uint16_t from = 0; // (x 4096: the sender's signed shorts)
    uint16_t to = 0;
    uint16_t blend_period = 0;
    uint16_t speed = 0;
};
bool read_play_anim(util::ByteReader& reader, PlayAnimMessage* message);
bool write_play_anim(util::ByteWriter& writer, const PlayAnimMessage& message);

// The two-byte messages (0x2A, 0x2B, 0x2C, 0x3D: the object id first; 0x3C: the object id second).
struct PairMessage {
    uint8_t first = 0;
    uint8_t second = 0;
};
bool read_pair(util::ByteReader& reader, PairMessage* message);
bool write_pair(util::ByteWriter& writer, const PairMessage& message);

struct HideGeomMessage {
    uint32_t name = 0;
    uint8_t object_id = 0;
    bool hide = false;
};
bool read_hide_geom(util::ByteReader& reader, HideGeomMessage* message);
bool write_hide_geom(util::ByteWriter& writer, const HideGeomMessage& message);

struct AnimSpeedMessage {
    uint32_t speed_bits = 0; // (the float's bits: passed on unchanged)
    uint8_t object_id = 0;
};
bool read_anim_speed(util::ByteReader& reader, AnimSpeedMessage* message);
bool write_anim_speed(util::ByteWriter& writer, const AnimSpeedMessage& message);

struct SpinModelMessage {
    int32_t period = 0;
    int32_t power = 0;
    int16_t start = 0;
    int16_t range = 0;
    uint8_t object_id = 0;
    uint8_t hold = 0;
    uint8_t axes = 0;
};
bool read_spin_model(util::ByteReader& reader, SpinModelMessage* message);
bool write_spin_model(util::ByteWriter& writer, const SpinModelMessage& message);

// 0x77 and 0x78 (the latter with params and bone 0).
struct SpecialItemMessage {
    uint32_t params = 0;
    uint32_t bone = 0;
    uint8_t object_id = 0;
    int8_t slot = 0; // (read signed, as the original)
};
bool read_special_item(util::ByteReader& reader, SpecialItemMessage* message);
bool write_special_item(util::ByteWriter& writer, const SpecialItemMessage& message);

// The graffiti and score messages (the trick objects' clusters a player tagged, the points of a landed combo):
//
//   0x3B trick objects (0x18 + 4 a name)  1 (u8; read signed, other values ignored), (0) x9, the game id (u8), (0),
//                              the player's object id (i32), the score (u32), the count (u32), then that many trick
//                              object cluster names (u32) - a client to the server, the server to every player
//   0x5B observed trick objects (0xC + 4 a name)  the game id (u8), (0) x3, the player (i32; a team's: its low 16
//                              bits are the player's object id), the count (u32), the names (u32) - to observers
//   0x4B tag stolen (12)       the game id (u8), (0) x3, the thief's and the victim's object ids (u32 each)
//   0x52 landed score / 0x86 best combo (8)  the game id (u8), (0) x3, the points (i32)
constexpr uint8_t kMsgTrickObjects = 0x3B;
constexpr uint8_t kMsgObservedTrickObjects = 0x5B;
constexpr uint8_t kMsgTagStolen = 0x4B;
constexpr uint8_t kMsgLandedScore = 0x52;
constexpr uint8_t kMsgBestCombo = 0x86;
constexpr uint32_t kMaxTrickObjects = 0x200; // (the senders' buffers: the cluster log's capacity)
constexpr size_t kTrickObjectsHeaderSize = 0x18;
constexpr size_t kObservedTrickObjectsHeaderSize = 0xC;
constexpr size_t kTagStolenSize = 12;
constexpr size_t kPointsSize = 8;

struct TrickObjectsMessage {
    int8_t one = 1;
    uint8_t game_id = 0;
    int32_t player = 0;
    uint32_t score = 0;
    uint32_t count = 0; // (at most kMaxTrickObjects)
    std::array<uint32_t, kMaxTrickObjects> objects = {};
};
// (fails - nothing to take - when the count is more than the message holds or than kMaxTrickObjects)
bool read_trick_objects(util::ByteReader& reader, TrickObjectsMessage* message);
bool write_trick_objects(util::ByteWriter& writer, const TrickObjectsMessage& message);
// The observers' message: the same fields but `one` and the score.
bool read_observed_trick_objects(util::ByteReader& reader, TrickObjectsMessage* message);
bool write_observed_trick_objects(util::ByteWriter& writer, const TrickObjectsMessage& message);

struct TagStolenMessage {
    uint8_t game_id = 0;
    uint32_t thief_id = 0;
    uint32_t victim_id = 0;
};
bool read_tag_stolen(util::ByteReader& reader, TagStolenMessage* message);
bool write_tag_stolen(util::ByteWriter& writer, const TagStolenMessage& message);

struct PointsMessage {
    uint8_t game_id = 0;
    int32_t points = 0;
};
bool read_points(util::ByteReader& reader, PointsMessage* message);
bool write_points(util::ByteWriter& writer, const PointsMessage& message);

} // namespace online

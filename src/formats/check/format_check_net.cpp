// NOT ORIGINAL - thug2_format_check's checks of the game messages converted from stage 5's batch 9 on: each message
// as the new writer encodes it compared byte for byte with the struct / byte array the original's sender built
// (padding zeroed), decoded and encoded again (identical), and cut short (the reader fails).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <array>

#include "wire/gamenet_messages.h"
#include "wire/play_messages.h"
#include "wire/skater_messages.h"
#include "formats/byte_stream.h"
#include "formats/rail_buffer_format.h"
#include "platform/compiler.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/same_bits.h"
#include "engine/core/str.h"
#include "formats/check/format_check_net.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy into the test messages' fixed fields)

namespace format_check_net {

namespace {

int& failures()
{
    static int failures{0};
    return failures;
}

void check(bool condition, const std::string& what)
{
    if (!condition) {
        ++failures();
        text::print("  FAIL %s\n", what.c_str());
    }
}

// Test fixtures of the net messages: sizes and offsets of the reference structs, and the values the tests send.
constexpr std::size_t kGuardBytes = 16; // a guard past the message: nothing may be written there
constexpr int kGuardFill = 0xCD;
constexpr std::size_t kHideGeomMinBytes = 6; // the bytes the reader needs: one fewer fails
constexpr std::size_t kAnimSpeedMinBytes = 5; // the bytes the reader needs: one fewer fails
constexpr std::size_t kSpinModelMinBytes = 15; // the bytes the reader needs: one fewer fails
constexpr std::size_t kSpecialItemMinBytes = 10; // the bytes the reader needs: one fewer fails
constexpr std::size_t kProjectileMinBytes = 0x35; // the bytes the reader needs: one fewer fails
constexpr std::size_t kTerrainSoundMinBytes = 11; // the bytes the reader needs: one fewer fails
constexpr std::size_t kToggleCheatMinBytes = 5; // the bytes the reader needs: one fewer fails
constexpr std::size_t kSpawnScriptMinBytes = 13; // the bytes the reader needs: one fewer fails
constexpr std::size_t kJoinProceedMinBytes = 30; // the bytes the reader needs: one fewer fails
constexpr std::size_t kConnectTrailerMinBytes = 29; // the bytes the reader needs: one fewer fails
constexpr std::size_t kLevelSwitchMinBytes = 5; // the bytes the reader needs: one fewer fails
constexpr std::size_t kChatMinBytes = 17; // the bytes the reader needs: one fewer fails
constexpr std::size_t kTrickObjectsUnknownBytes = 9;
constexpr uint32_t kFirstTrickObject = 0x11111111;
constexpr uint32_t kSecondTrickObject = 0x22222222;
constexpr uint32_t kThirdTrickObject = 0x33333333;
constexpr std::size_t kTrickObjectsCountOffset = 0x14;
constexpr std::size_t kHuntPlacementCapacity = 5;
constexpr float kHuntPositionYSample = 5.5f;
constexpr int kHuntPositionZSample = 6;
constexpr std::size_t kHuntPositionBytes = 16; // four floats
constexpr int kProjectileVelocityYSample = 5;
constexpr int kProjectileVelocityZSample = 6;
constexpr std::size_t kVectorBytes = 16; // four floats
constexpr int kPlaceTagEndYSample = 5;
constexpr int kPlaceTagEndZSample = 6;
constexpr uint32_t kSecondCheatSample = 0xB38341C9;
constexpr uint32_t kThirdCheatSample = 0xCD09E062;
constexpr std::size_t kPasswordSize = 10;
constexpr std::size_t kCdKeyResponseSize = 74;
constexpr std::size_t kJoinNameSize = 16;
constexpr std::size_t kProceedServerNameSize = 16;
constexpr std::size_t kTrailerMagicSize = 10;
constexpr std::size_t kConnectInfoCdKeyResponseOffset = 0x12;
constexpr std::size_t kJoinHeaderNameOffset = 0x68;
constexpr std::size_t kProceedMaxPlayersOffset = 0x1C;
constexpr std::size_t kTrailerVersionPosition = 0x68; // in the connection request that carries it
constexpr std::size_t kTrailerModifiedPosition = 0x78;
constexpr std::size_t kCdKeyFillLength = 72; // less than the field (kCdKeyResponseSize)
constexpr int kProceedServerNameLength = 15; // a full field cut short of its terminator
constexpr std::size_t kPlayerArrivedNameSize = 16;
constexpr std::size_t kHudStringSize = 16;
constexpr std::size_t kStartRequestWords = 13;
constexpr std::size_t kKickNameSize = 16;
constexpr std::size_t kPlayerArrivedObjIdOffset = 0x18;
constexpr std::size_t kPlayerArrivedNameOffset = 0x20;
constexpr std::size_t kChatNameSize = 16;
constexpr std::size_t kChatTextSize = 9; // "hi there" and its NUL
constexpr std::size_t kShortChatTextSize = 6; // "hello" and its NUL
constexpr std::size_t kChangeLevelNameSize = 32;
constexpr std::size_t kChatFixedBytes = 0x12; // the message before its text
constexpr std::size_t kChatTextLength = 8;
constexpr std::size_t kThugproChatFixedBytes = 21;
constexpr std::size_t kThugproChatTextBytes = 6;
constexpr int kChatTextLastIndex = 8; // where the NUL is: replaced to fill the field
constexpr int kShortChatTextLastIndex = 5;
constexpr std::size_t kNewsHeaderBytes = 8; // the version word and the size word
constexpr std::size_t kNewsDataBytes = 12;
constexpr int kNewsSizeSample = 12;
constexpr int kNewsAfterTextIndex = 16; // the byte after the text and its padding
constexpr int kNewsGuardByte = 0xAB;
constexpr int kNewsTextBytes = 8; // "Hello!" and its NUL, padded to a word
constexpr int kNewsSizePastEnd = 13;
constexpr float kRanIntoPushXSample = 10;
constexpr float kRanIntoPushZSample = 30.5f;
constexpr int kThirdRailPointCount = 398;
constexpr int kRailRecordCount = 400;
constexpr int kRailRecordWords = 5;
constexpr int kRailFillBias = 7;
constexpr int kTooManyRails = 201;
constexpr int kThirdRailPointCountLowByte = 143; // the third rail's 398 points + 1 (0x18E -> 0x18F)
constexpr uint32_t kLevelChecksumSample = 0xB664035D; // a level's name checksum
// `message` written must be `reference`'s bytes; read back and written again the same; and a reader given fewer than
// `needed` bytes (default: all of them) fails.
template <typename Message, typename Reference>
void check_message(const std::string& what, const Message& message, const Reference& reference,
                   const std::type_identity_t<std::function<bool(util::ByteWriter&, const Message&)>>& write,
                   const std::type_identity_t<std::function<bool(util::ByteReader&, Message*)>>& read,
                   size_t needed = sizeof(Reference))
{
    constexpr size_t kSize = sizeof(Reference);
    std::vector<uint8_t> bytes(kSize + kGuardBytes, kGuardFill);
    util::ByteWriter writer(bytes.data(), bytes.size());
    write(writer, message);
    check(writer.ok() && writer.position() == kSize &&
              core::same_bytes(std::as_bytes(std::span(bytes)).first(kSize), std::as_bytes(std::span(&reference, 1)).first(kSize)),
          what + ": as the original's");
    util::ByteReader reader(bytes.data(), kSize);
    Message back{};
    const bool read_ok = read(reader, &back);
    std::vector<uint8_t> again(kSize + kGuardBytes, kGuardFill);
    util::ByteWriter again_writer(again.data(), again.size());
    write(again_writer, back);
    check(read_ok && again_writer.position() == kSize && std::memcmp(again.data(), bytes.data(), kSize) == 0,
          what + ": decoded and encoded again");
    util::ByteReader short_reader(bytes.data(), needed - 1);
    Message cut{};
    check(!read(short_reader, &cut), what + ": cut short fails");
}

#pragma pack(push, 1)
// The senders' layouts (the blend channel 0x0047E... , the model component 0x004202A0 / 0x00420730, the special items).
struct ReferencePlayAnim {
    uint8_t loop_type, object;
    uint32_t anim;
    std::array<int16_t, 4> times;
};
struct ReferenceHideGeom {
    uint32_t name;
    uint8_t object;
    uint8_t hide;
    std::array<uint8_t, 2> pad;
};
struct ReferenceAnimSpeed {
    float speed;
    uint8_t object;
    std::array<uint8_t, 3> pad;
};
struct ReferenceSpinModel {
    int32_t period, power;
    int16_t start, range;
    uint8_t object, hold, axes, pad;
};
struct ReferenceSpecialItem {
    uint32_t params, bone;
    uint8_t object;
    uint8_t slot;
    std::array<uint8_t, 2> pad;
};
#pragma pack(pop)
static_assert(sizeof(ReferencePlayAnim) == online::kPlayAnimSize && sizeof(ReferenceHideGeom) == online::kHideGeomSize &&
              sizeof(ReferenceAnimSpeed) == online::kAnimSpeedSize &&
              sizeof(ReferenceSpinModel) == online::kSpinModelSize &&
              sizeof(ReferenceSpecialItem) == online::kSpecialItemSize);

constexpr int kPlayAnimObjectIdSample = 5;
constexpr uint32_t kPlayAnimAnimSample = 0xA1B2C3D4;
constexpr int kPlayAnimFromSample = 4096; // (used negated)
constexpr int kPlayAnimToSample = 8192;
constexpr int kPlayAnimBlendPeriodSample = 1229;
constexpr int kPlayAnimSpeedSample = 4096;
constexpr int kPairSecondSample = 0xFE;
constexpr int kHideGeomNameSample = 0x12345678;
constexpr int kHideGeomObjectIdSample = 7;
constexpr float kAnimSpeedSample = 1.25f;
constexpr int kSpinModelPeriodSample = 1500;
constexpr int kSpinModelStartSample = 90; // (used negated)
constexpr int kSpinModelRangeSample = 360;
constexpr int kSpinModelAxesSample = 5;
constexpr uint32_t kSpecialItemParamsSample = 0xCAFEBABE;
constexpr int kSpecialItemBoneSample = 0x0BADF00D;
constexpr int kSpecialItemObjectIdSample = 6;
void check_skater_messages()
{
    {
        const ReferencePlayAnim ref = {.loop_type = 2, .object = 5, .anim = 0xA1B2C3D4, .times = {-4096, 8192, 1229, 4096}};
        online::PlayAnimMessage m;
        m.loop_type = 2;
        m.object_id = kPlayAnimObjectIdSample;
        m.anim = kPlayAnimAnimSample;
        m.from = static_cast<uint16_t>(-kPlayAnimFromSample);
        m.to = kPlayAnimToSample;
        m.blend_period = kPlayAnimBlendPeriodSample;
        m.speed = kPlayAnimSpeedSample;
        check_message("play anim 0x29", m, ref, online::write_play_anim, online::read_play_anim);
    }
    {
        const std::array<uint8_t, 2> ref = {{3, 0xFE}};
        online::PairMessage m;
        m.first = 3;
        m.second = kPairSecondSample;
        check_message("pair 0x2A-0x3D", m, ref, online::write_pair, online::read_pair);
    }
    {
        const ReferenceHideGeom ref = {.name = 0x12345678, .object = 7, .hide = 1, .pad = {}};
        online::HideGeomMessage m;
        m.name = kHideGeomNameSample;
        m.object_id = kHideGeomObjectIdSample;
        m.hide = true;
        check_message("hide geom 0x3E", m, ref, online::write_hide_geom, online::read_hide_geom, kHideGeomMinBytes);
    }
    {
        ReferenceAnimSpeed ref = {.speed = kAnimSpeedSample, .object = 4, .pad = {}};
        online::AnimSpeedMessage m;
        std::memcpy(&m.speed_bits, &ref.speed, 4);
        m.object_id = 4;
        check_message("anim speed 0x41", m, ref, online::write_anim_speed, online::read_anim_speed, kAnimSpeedMinBytes);
    }
    {
        const ReferenceSpinModel ref = {.period = 1500, .power = 2, .start = -90, .range = 360, .object = 1, .hold = 1, .axes = 5, .pad = 0};
        online::SpinModelMessage m;
        m.period = kSpinModelPeriodSample;
        m.power = 2;
        m.start = -kSpinModelStartSample;
        m.range = kSpinModelRangeSample;
        m.object_id = 1;
        m.hold = 1;
        m.axes = kSpinModelAxesSample;
        check_message("spin model 0x76", m, ref, online::write_spin_model, online::read_spin_model, kSpinModelMinBytes);
    }
    {
        const ReferenceSpecialItem ref = {.params = 0xCAFEBABE, .bone = 0x0BADF00D, .object = 6, .slot = 0xFF, .pad = {}};
        online::SpecialItemMessage m;
        m.params = kSpecialItemParamsSample;
        m.bone = kSpecialItemBoneSample;
        m.object_id = kSpecialItemObjectIdSample;
        m.slot = -1;
        check_message("special item 0x77 / 0x78", m, ref, online::write_special_item, online::read_special_item, kSpecialItemMinBytes);
    }
}

// The graffiti and score messages as their senders built them (score_trick_objects, PointTally, send_points).
#pragma pack(push, 1)
struct ReferenceTrickObjects {
    uint8_t one;
    std::array<uint8_t, kTrickObjectsUnknownBytes> unknown01;
    uint8_t game_id;
    uint8_t pad0b;
    int32_t player;
    uint32_t score;
    uint32_t count;
    std::array<uint32_t, 3> objects;
};
struct ReferenceObservedTrickObjects {
    uint8_t game_id;
    std::array<uint8_t, 3> pad;
    int32_t player;
    uint32_t count;
    std::array<uint32_t, 3> objects;
};
struct ReferenceTagStolen {
    uint8_t game_id;
    std::array<uint8_t, 3> pad;
    int32_t thief, victim;
};
struct ReferencePoints {
    uint8_t game_id;
    std::array<uint8_t, 3> pad;
    int32_t points;
};
#pragma pack(pop)
static_assert(offsetof(ReferenceTrickObjects, objects) == online::kTrickObjectsHeaderSize &&
              offsetof(ReferenceObservedTrickObjects, objects) == online::kObservedTrickObjectsHeaderSize &&
              sizeof(ReferenceTagStolen) == online::kTagStolenSize && sizeof(ReferencePoints) == online::kPointsSize);

constexpr int kTrickObjectsGameIdSample = 9;
constexpr int kTrickObjectsScoreSample = 12345;
constexpr int kTagStolenGameIdSample = 7;
constexpr int kTagStolenVictimIdSample = 5;
constexpr int kPointsAmountSample = 250; // (used negated)
void check_graffiti_messages()
{
    auto message = std::make_unique<online::TrickObjectsMessage>();
    message->game_id = kTrickObjectsGameIdSample;
    message->player = 3;
    message->score = kTrickObjectsScoreSample;
    message->count = 3;
    core::at(message->objects, 0) = kFirstTrickObject;
    core::at(message->objects, 1) = kSecondTrickObject;
    core::at(message->objects, 2) = kThirdTrickObject;
    ReferenceTrickObjects ref = {.one = 1, .unknown01 = {}, .game_id = kTrickObjectsGameIdSample, .pad0b = 0, .player = 3, .score = kTrickObjectsScoreSample, .count = 3, .objects = {kFirstTrickObject, kSecondTrickObject, kThirdTrickObject}};
    check_message("trick objects 0x3B", *message, ref, online::write_trick_objects, online::read_trick_objects);
    const ReferenceObservedTrickObjects observed = {.game_id = 9, .pad = {}, .player = 3, .count = 3, .objects = {0x11111111, 0x22222222, 0x33333333}};
    check_message("observed trick objects 0x5B", *message, observed, online::write_observed_trick_objects,
                  online::read_observed_trick_objects);
    // a count past the message's end, or past the senders' buffer: refused
    ref.count = 4;
    util::ByteReader reader(&ref, sizeof(ref));
    check(!online::read_trick_objects(reader, message.get()) && message->count == 0,
          "trick objects 0x3B: a count past the end refused");
    std::vector<uint8_t> big(online::kTrickObjectsHeaderSize + 4 * (online::kMaxTrickObjects + 1), 0);
    core::at(big, 0) = 1;
    uint32_t too_many = online::kMaxTrickObjects + 1;
    std::memcpy(std::span(big).subspan(kTrickObjectsCountOffset).data(), &too_many, 4);
    util::ByteReader big_reader(big.data(), big.size());
    check(!online::read_trick_objects(big_reader, message.get()), "trick objects 0x3B: more than the buffer refused");

    const ReferenceTagStolen stolen_ref = {.game_id = 7, .pad = {}, .thief = 2, .victim = 5};
    online::TagStolenMessage stolen;
    stolen.game_id = kTagStolenGameIdSample;
    stolen.thief_id = 2;
    stolen.victim_id = kTagStolenVictimIdSample;
    check_message("tag stolen 0x4B", stolen, stolen_ref, online::write_tag_stolen, online::read_tag_stolen);

    const ReferencePoints points_ref = {.game_id = 4, .pad = {}, .points = -250};
    online::PointsMessage points;
    points.game_id = 4;
    points.points = -kPointsAmountSample;
    check_message("points 0x52 / 0x86", points, points_ref, online::write_points, online::read_points);
}

// The other play messages as their senders built them (hunt_cfuncs, tagging, the terrain sounds, the cheats, the
// script manager).
#pragma pack(push, 1)
struct ReferenceHuntPlacements {
    uint8_t id;
    std::array<uint8_t, 3> pad;
    int32_t count;
    std::array<std::array<float, 4>, kHuntPlacementCapacity> positions;
};
struct ReferenceBoardVehicle {
    uint8_t skater;
    std::array<uint8_t, 3> pad;
    uint32_t control_type;
};
struct ReferenceProjectile {
    uint32_t zero;
    std::array<float, 4> pos;
    std::array<float, 4> vel;
    uint32_t type, ammo_type;
    int32_t radius;
    float scale;
    uint8_t id;
    std::array<uint8_t, 3> pad;
};
struct ReferencePlaceTag {
    uint8_t object;
    uint8_t sticker;
    std::array<uint8_t, 2> pad;
    std::array<float, 4> start;
    std::array<float, 4> end;
    int32_t size;
};
struct ReferenceTerrainSound {
    int16_t x, y, z;
    uint8_t table, terrain, volume, pitch, value, pad;
};
struct ReferenceToggleCheat {
    uint32_t cheat;
    uint8_t on;
    std::array<uint8_t, 3> pad;
};
struct ReferenceCheats {
    int32_t count;
    std::array<uint32_t, 3> cheats;
};
struct ReferenceRunScript {
    uint32_t name, object;
};
struct ReferenceSpawnScript {
    uint32_t name, object;
    int32_t node;
    uint8_t permanent;
    std::array<uint8_t, 3> pad;
};
#pragma pack(pop)
static_assert(sizeof(ReferenceHuntPlacements) == online::kHuntPlacementsSize &&
              sizeof(ReferenceBoardVehicle) == online::kBoardVehicleSize &&
              sizeof(ReferenceProjectile) == online::kProjectileSize &&
              offsetof(ReferenceProjectile, id) == online::kProjectileOwnerAt &&
              sizeof(ReferencePlaceTag) == online::kPlaceTagSize &&
              sizeof(ReferenceTerrainSound) == online::kTerrainSoundSize &&
              sizeof(ReferenceToggleCheat) == online::kToggleCheatSize &&
              sizeof(ReferenceRunScript) == online::kRunScriptHeaderSize &&
              sizeof(ReferenceSpawnScript) == online::kSpawnScriptSize);

// The placements' header and all five positions (the game reads `count` of them, one at a time).
bool read_hunt_placements_whole(util::ByteReader& reader, online::HuntPlacements* message)
{
    online::read_hunt_placements(reader, message);
    for (auto& position : message->positions)
        online::read_hunt_position(reader, position);
    return reader.ok();
}

constexpr int kBoardVehicleSkaterSample = 5;
constexpr uint32_t kBoardVehicleControlTypeSample = 0x8A6B04AD;
constexpr uint32_t kProjectileTypeSample = 0xD039432C;
constexpr int kProjectileAmmoTypeSample = 0x11223344;
constexpr int kProjectileRadiusSample = 0xF0;
constexpr float kProjectileScaleSample = 2.5f;
constexpr int kProjectileOwnerIdSample = 6;
constexpr int kPlaceTagSizeSample = 64;
constexpr int kTerrainSoundXSample = 100; // (used negated)
constexpr int kTerrainSoundYSample = 250;
constexpr int kTerrainSoundZSample = 3000;
constexpr int kTerrainSoundTerrainSample = 17;
constexpr int kTerrainSoundVolumeSample = 100;
constexpr int kTerrainSoundPitchSample = 0x9C;
constexpr int kTerrainSoundValueSample = 50;
constexpr uint32_t kToggleCheatCheatSample = 0xEEFECF1B;
constexpr int kRunScriptNameSample = 0x12345678;
constexpr uint32_t kRunScriptObjectIdSample = 0xFFFFFFFF;
constexpr uint32_t kSpawnScriptNameSample = 0x9ABCDEF0;
constexpr int kSpawnScriptNodeSample = 77;
void check_play_messages()
{
    {
        ReferenceHuntPlacements ref = {.id = 3, .pad = {}, .count = 2, .positions = {{{{1, 2, 3, 0}}, {{-4, kHuntPositionYSample, kHuntPositionZSample, 1}}, {}, {}, {}}}};
        online::HuntPlacements m;
        m.player_id = 3;
        m.count = 2;
        std::memcpy(m.positions.data(), ref.positions.data(), sizeof(ref.positions));
        check_message("hunt placements 0x8F", m, ref, online::write_hunt_placements, read_hunt_placements_whole);
        std::array<float, 4> at{};
        util::ByteReader reader(&ref, sizeof(ref));
        online::HuntPlacements back;
        online::read_hunt_placements(reader, &back);
        online::read_hunt_position(reader, at);
        online::read_hunt_position(reader, at);
        check(reader.ok() && core::at(at, 1) == kHuntPositionYSample && back.count == 2, "hunt placements 0x8F: the positions read");
        const std::array<float, 4> one = {{7, 8, 9, 1}};
        std::array<uint8_t, kHuntPositionBytes> bytes{};
        util::ByteWriter writer(bytes.data(), sizeof(bytes));
        online::write_hunt_position(writer, one);
        check(writer.ok() && core::same_bytes(std::as_bytes(std::span(bytes)), std::as_bytes(std::span(one))), "hunt placement 0x8F: as the original's vector");
    }
    {
        const ReferenceBoardVehicle ref = {.skater = 5, .pad = {}, .control_type = 0x8A6B04AD};
        online::BoardVehicle m;
        m.skater_id = kBoardVehicleSkaterSample;
        m.control_type = kBoardVehicleControlTypeSample;
        check_message("board vehicle 0x89", m, ref, online::write_board_vehicle, online::read_board_vehicle);
    }
    {
        ReferenceProjectile ref = {.zero = 0, .pos = {1, 2, 3, 0}, .vel = {4, kProjectileVelocityYSample, kProjectileVelocityZSample, 0}, .type = kProjectileTypeSample, .ammo_type = kProjectileAmmoTypeSample, .radius = kProjectileRadiusSample, .scale = kProjectileScaleSample, .id = kProjectileOwnerIdSample, .pad = {}};
        online::Projectile m;
        std::memcpy(m.position.data(), ref.pos.data(), kVectorBytes);
        std::memcpy(m.velocity.data(), ref.vel.data(), kVectorBytes);
        m.type = kProjectileTypeSample;
        m.ammo_type = kProjectileAmmoTypeSample;
        m.radius = kProjectileRadiusSample;
        m.scale = kProjectileScaleSample;
        m.owner_id = kProjectileOwnerIdSample;
        check_message("projectile 0x83", m, ref, online::write_projectile, online::read_projectile, kProjectileMinBytes);
    }
    {
        ReferencePlaceTag ref = {.object = 2, .sticker = 1, .pad = {}, .start = {1, 2, 3, 1}, .end = {4, kPlaceTagEndYSample, kPlaceTagEndZSample, 1}, .size = kPlaceTagSizeSample};
        online::PlaceTag m;
        m.object_id = 2;
        m.sticker = true;
        std::memcpy(m.start.data(), ref.start.data(), kVectorBytes);
        std::memcpy(m.end.data(), ref.end.data(), kVectorBytes);
        m.size = kPlaceTagSizeSample;
        check_message("place tag 0x99", m, ref, online::write_place_tag, online::read_place_tag);
    }
    {
        const ReferenceTerrainSound ref = {.x = -100, .y = 250, .z = 3000, .table = 2, .terrain = 17, .volume = 100, .pitch = 0x9C, .value = 50, .pad = 0};
        online::TerrainSound m;
        m.x = -kTerrainSoundXSample;
        m.y = kTerrainSoundYSample;
        m.z = kTerrainSoundZSample;
        m.table = 2;
        m.terrain = kTerrainSoundTerrainSample;
        m.volume = kTerrainSoundVolumeSample;
        m.pitch = static_cast<int8_t>(kTerrainSoundPitchSample);
        m.value = kTerrainSoundValueSample;
        check_message("terrain sound 0x35", m, ref, online::write_terrain_sound, online::read_terrain_sound, kTerrainSoundMinBytes);
    }
    {
        const ReferenceToggleCheat ref = {.cheat = 0xEEFECF1B, .on = 1, .pad = {}};
        online::ToggleCheat m;
        m.cheat = kToggleCheatCheatSample;
        m.on = true;
        check_message("toggle cheat 0x7F", m, ref, online::write_toggle_cheat, online::read_toggle_cheat, kToggleCheatMinBytes);
    }
    {
        ReferenceCheats ref = {.count = 3, .cheats = {kToggleCheatCheatSample, kSecondCheatSample, kThirdCheatSample}};
        online::Cheats m;
        m.count = 3;
        std::memcpy(m.cheats.data(), ref.cheats.data(), sizeof(ref.cheats));
        check_message("cheats 0x82", m, ref, online::write_cheats, online::read_cheats);
        ref.count = -1;
        util::ByteReader reader(&ref, sizeof(ref));
        online::Cheats back;
        check(!online::read_cheats(reader, &back) && back.count == 0, "cheats 0x82: a negative count refused");
    }
    {
        const ReferenceRunScript ref = {.name = 0x12345678, .object = 0xFFFFFFFF};
        online::RunScript m;
        m.name = kRunScriptNameSample;
        m.object_id = kRunScriptObjectIdSample;
        check_message("run script 0x48", m, ref, online::write_run_script, online::read_run_script);
        const ReferenceSpawnScript spawn_ref = {.name = 0x9ABCDEF0, .object = 3, .node = 77, .permanent = 1, .pad = {}};
        online::SpawnScript spawn;
        spawn.name = kSpawnScriptNameSample;
        spawn.object_id = 3;
        spawn.node = kSpawnScriptNodeSample;
        spawn.permanent = true;
        check_message("spawn script 0x49", spawn, spawn_ref, online::write_spawn_script, online::read_spawn_script, kSpawnScriptMinBytes);
    }
}

// The join messages as the original's structs held them (ConnectInfo, JoinRequestBlock, ProceedInfo; THUGPRO's
// trailer as its release sends it).
#pragma pack(push, 1)
struct ReferenceConnectInfo {
    int32_t version;
    uint8_t observer, can_queue_up;
    std::array<char, kPasswordSize> password;
    uint8_t broadband;
    int8_t rank;
    std::array<char, kCdKeyResponseSize> cd_key_response;
};
struct ReferenceJoinHeader {
    ReferenceConnectInfo connect;
    int32_t profile, rating, drive_scheme;
    std::array<char, kJoinNameSize> name;
};
struct ReferenceProceed {
    uint32_t public_ip, private_ip;
    int32_t port;
    std::array<char, kProceedServerNameSize> server_name;
    int8_t max_players;
    uint8_t broadband;
    std::array<uint8_t, 2> pad;
};
struct ReferenceTrailer {
    std::array<char, kTrailerMagicSize> magic;
    std::array<uint8_t, 2> pad;
    uint32_t version, release, scripts_hash, machine_id;
    uint8_t modified;
    std::array<uint8_t, 3> pad79;
};
#pragma pack(pop)
static_assert(sizeof(ReferenceConnectInfo) == online::kConnectInfoSize &&
              offsetof(ReferenceConnectInfo, cd_key_response) == kConnectInfoCdKeyResponseOffset &&
              sizeof(ReferenceJoinHeader) == online::kJoinInfoHeaderSize &&
              offsetof(ReferenceJoinHeader, name) == kJoinHeaderNameOffset && sizeof(ReferenceProceed) == online::kProceedInfoSize &&
              offsetof(ReferenceProceed, max_players) == kProceedMaxPlayersOffset &&
              sizeof(ReferenceTrailer) == online::thugpro::kConnectTrailerSize &&
              online::kConnectInfoSize + offsetof(ReferenceTrailer, version) == kTrailerVersionPosition &&
              online::kConnectInfoSize + offsetof(ReferenceTrailer, modified) == kTrailerModifiedPosition);

constexpr int kJoinRequestProfileSample = 123456;
constexpr int kJoinRequestRatingSample = 900;
constexpr int kJoinProceedPublicIpSample = 0x0100007F;
constexpr int kJoinProceedPrivateIpSample = 0x0A00000A;
constexpr int kJoinProceedPortSample = 5150;
constexpr int kJoinProceedMaxPlayersSample = 8;
constexpr int kConnectTrailerVersionSample = 0x00070000;
constexpr int kConnectTrailerReleaseSample = 0x32475554;
constexpr int kConnectTrailerScriptsHashSample = 0x09FC08B0;
constexpr uint32_t kConnectTrailerMachineIdSample = 0xA1B2C3D4;
void check_join_messages()
{
    ReferenceConnectInfo ref = {};
    ref.version = online::kVersion;
    ref.observer = 1;
    ref.can_queue_up = 1;
    text::copy_into(ref.password, "secret123");
    ref.broadband = 1;
    ref.rank = -3;
    std::memset(ref.cd_key_response.data(), 'k', kCdKeyFillLength);
    online::ConnectInfo info = {};
    info.version = online::kVersion;
    info.observer = true;
    info.can_queue_up_ = true;
    text::copy_into(info.password, "secret123");
    info.broadband = true;
    info.rank = -3;
    std::memset(info.cd_key_response_.data(), 'k', kCdKeyFillLength);
    check_message("connection request 3", info, ref, online::write_connect_info, online::read_connect_info);

    ReferenceJoinHeader join_ref = {};
    join_ref.connect = ref;
    join_ref.profile = kJoinRequestProfileSample;
    join_ref.rating = kJoinRequestRatingSample;
    join_ref.drive_scheme = 2;
    text::copy_into(join_ref.name, "fifteen_letters");
    online::JoinRequestHeader join = {};
    static_cast<online::ConnectInfo&>(join) = info;
    join.profile = kJoinRequestProfileSample;
    join.rating = kJoinRequestRatingSample;
    join.drive_scheme_ = 2;
    text::copy_into(join.name, "fifteen_letters");
    check_message("join request 80", join, join_ref, online::write_join_request, online::read_join_request);

    ReferenceProceed proceed_ref = {.public_ip = kJoinProceedPublicIpSample, .private_ip = kJoinProceedPrivateIpSample, .port = kJoinProceedPortSample, .server_name = {}, .max_players = kJoinProceedMaxPlayersSample, .broadband = 1, .pad = {}};
    text::copy_into(proceed_ref.server_name, "Skate Shop");
    online::ProceedInfo proceed = {};
    proceed.public_ip_ = kJoinProceedPublicIpSample;
    proceed.private_ip_ = kJoinProceedPrivateIpSample;
    proceed.port = kJoinProceedPortSample;
    text::copy_into(proceed.server_name_, "Skate Shop");
    proceed.max_players_ = kJoinProceedMaxPlayersSample;
    proceed.broadband = true;
    check_message("join proceed 79", proceed, proceed_ref, online::write_proceed_info, online::read_proceed_info, kJoinProceedMinBytes);
    // a server name filling its field (no terminator): cut there
    std::memset(proceed_ref.server_name.data(), 'N', kProceedServerNameSize);
    util::ByteReader reader(&proceed_ref, sizeof(proceed_ref));
    online::ProceedInfo back{};
    check(online::read_proceed_info(reader, &back) && std::strlen(back.server_name_.data()) == kProceedServerNameLength && back.max_players_ == kJoinProceedMaxPlayersSample,
          "join proceed 79: an unterminated name cut to its field");

    const ReferenceTrailer trailer_ref = {
        .magic = {'_', 'T', 'H', 'U', 'G', '_', 'P', 'R', 'O', '_'}, .pad = {}, .version = 0x00070000, .release = 0x32475554, .scripts_hash = 0x09FC08B0, .machine_id = 0xA1B2C3D4, .modified = 1, .pad79 = {}};
    online::thugpro::ConnectTrailer trailer = {};
    std::memcpy(trailer.magic.data(), online::thugpro::kMagic.data(), sizeof(trailer.magic));
    trailer.version = kConnectTrailerVersionSample;
    trailer.release = kConnectTrailerReleaseSample;
    trailer.scripts_hash_ = kConnectTrailerScriptsHashSample;
    trailer.machine_id_ = kConnectTrailerMachineIdSample;
    trailer.modified_scripts_ = true;
    check_message("THUGPRO connection trailer (TP-R12)", trailer, trailer_ref, online::thugpro::write_connect_trailer,
                  online::thugpro::read_connect_trailer, kConnectTrailerMinBytes);
}

// The session's other messages as the original's senders built them (NewPlayerMessage, PanelMessage,
// StartGameRequest, the kick request, the goal messages, the level switch).
#pragma pack(push, 1)
struct ReferencePlayerArrived {
    int32_t profile_id, rating;
    uint32_t flags;
    int32_t score;
    uint32_t value10, value14;
    int8_t obj_id, team;
    std::array<uint8_t, 2> pad;
    int32_t drive_scheme;
    std::array<char, kPlayerArrivedNameSize> name;
};
struct ReferenceHudNotice {
    uint32_t properties, text;
    int32_t time;
    std::array<char, kHudStringSize> string1;
    std::array<char, kHudStringSize> string2;
};
struct ReferenceStartRequest {
    std::array<uint32_t, kStartRequestWords> words;
};
struct ReferenceKickRequest {
    uint8_t request, index;
    std::array<char, kKickNameSize> name;
};
struct ReferenceGoalMessage {
    uint8_t id;
    std::array<uint8_t, 3> pad;
    uint32_t goal;
};
struct ReferenceLevelSwitch {
    uint32_t level;
    uint8_t flag;
    std::array<uint8_t, 3> pad;
};
#pragma pack(pop)
static_assert(sizeof(ReferencePlayerArrived) == online::kPlayerArrivedHeaderSize &&
              offsetof(ReferencePlayerArrived, obj_id) == kPlayerArrivedObjIdOffset && offsetof(ReferencePlayerArrived, name) == kPlayerArrivedNameOffset &&
              sizeof(ReferenceHudNotice) == online::kHudNoticeSize &&
              sizeof(ReferenceStartRequest) == online::kStartRequestSize &&
              sizeof(ReferenceKickRequest) == online::kKickRequestSize &&
              sizeof(ReferenceGoalMessage) == online::kGoalMessageSize &&
              sizeof(ReferenceLevelSwitch) == online::kLevelSwitchSize &&
              offsetof(ReferenceLevelSwitch, level) == online::kLevelSwitchLevelAt);

constexpr int kPlayerArrivedProfileIdSample = 4242;
constexpr int kPlayerArrivedRatingSample = 1500;
constexpr int kPlayerArrivedFlagsSample = 0x11;
constexpr int kPlayerArrivedScoreSample = 9000;
constexpr int kPlayerArrivedValue10Sample = 77;
constexpr int kPlayerArrivedValue14Sample = 31337;
constexpr int kPlayerArrivedEliminatedTimeSample = 77;
constexpr int kPlayerArrivedAccumulatedScoreSample = 31337;
constexpr int kHudNoticePropertiesSample = 0x11112222;
constexpr int kHudNoticeTextSample = 0x33334444;
constexpr int kHudNoticeTimeSample = 5000;
constexpr int kStartRequestPlayerCollisionSample = 5;
constexpr int kStartRequestFriendlyFireSample = 6;
constexpr int kStartRequestTargetScoreSample = 8;
constexpr int kStartRequestStopAtZeroSample = 9;
constexpr int kStartRequestScoreResetSample = 10;
constexpr int kStartRequestCtfGameTypeSample = 11;
constexpr int kStartRequestGoalsSample = 12;
constexpr int kStartRequestTeamGraffitiModeSample = 13;
constexpr int kGoalMessageIdSample = 7;
constexpr uint32_t kGoalMessageGoalSample = 0xC0FFEE00;
void check_session_others()
{
    {
        ReferencePlayerArrived ref = {.profile_id = kPlayerArrivedProfileIdSample, .rating = kPlayerArrivedRatingSample, .flags = kPlayerArrivedFlagsSample, .score = kPlayerArrivedScoreSample, .value10 = kPlayerArrivedValue10Sample, .value14 = kPlayerArrivedValue14Sample, .obj_id = 3, .team = 1, .pad = {}, .drive_scheme = 2, .name = {}};
        text::copy_into(ref.name, "player_one");
        online::PlayerArrived m;
        m.profile_id = kPlayerArrivedProfileIdSample;
        m.rating = kPlayerArrivedRatingSample;
        m.flags = kPlayerArrivedFlagsSample;
        m.score = kPlayerArrivedScoreSample;
        m.eliminated_time = kPlayerArrivedEliminatedTimeSample;
        m.accumulated_score = kPlayerArrivedAccumulatedScoreSample;
        m.object_id = 3;
        m.team = 1;
        m.drive_scheme = 2;
        text::copy_into(m.name, "player_one");
        check_message("player arrived 0x22", m, ref, online::write_player_arrived, online::read_player_arrived);
    }
    {
        ReferenceHudNotice ref = {.properties = kHudNoticePropertiesSample, .text = kHudNoticeTextSample, .time = kHudNoticeTimeSample, .string1 = {}, .string2 = {}};
        text::copy_into(ref.string1, "Skater 1");
        text::copy_into(ref.string2, "Team Red");
        online::HudNotice m;
        m.properties = kHudNoticePropertiesSample;
        m.text = kHudNoticeTextSample;
        m.time = kHudNoticeTimeSample;
        text::copy_into(m.string1, "Skater 1");
        text::copy_into(m.string2, "Team Red");
        check_message("hud notice 0x3F", m, ref, online::write_hud_notice, online::read_hud_notice);
        // string 1 filling its field (no terminator): cut there, string 2 still read
        std::memset(ref.string1.data(), 'x', kHudStringSize);
        util::ByteReader reader(&ref, sizeof(ref));
        online::HudNotice back;
        check(online::read_hud_notice(reader, &back) && std::strlen(back.string1.data()) == kHudStringSize &&
                  std::strcmp(back.string2.data(), "Team Red") == 0,
              "hud notice 0x3F: an unterminated string ends in its field");
    }
    {
        const ReferenceStartRequest ref = {{1, 2, 3, 4, 5, 6, 0, 8, 9, 10, 11, 12, 13}};
        online::StartRequest m;
        m.game_type = 1;
        m.time_limit = 2;
        m.skill_level = 3;
        m.fireball_level = 4;
        m.player_collision = kStartRequestPlayerCollisionSample;
        m.friendly_fire = kStartRequestFriendlyFireSample;
        m.target_score = kStartRequestTargetScoreSample;
        m.stop_at_zero = kStartRequestStopAtZeroSample;
        m.score_reset = kStartRequestScoreResetSample;
        m.ctf_game_type = kStartRequestCtfGameTypeSample;
        m.goals = kStartRequestGoalsSample;
        m.team_graffiti_mode = kStartRequestTeamGraffitiModeSample;
        check_message("start request 0x60", m, ref, online::write_start_request, online::read_start_request);
    }
    {
        ReferenceKickRequest ref = {.request = 1, .index = 2, .name = {}};
        text::copy_into(ref.name, "griefer");
        online::KickRequest m;
        m.kick = 1;
        m.index = 2;
        text::copy_into(m.name, "griefer");
        check_message("kick request 0x61", m, ref, online::write_kick_request, online::read_kick_request);
    }
    {
        const ReferenceGoalMessage ref = {.id = 7, .pad = {}, .goal = 0xC0FFEE00};
        online::GoalMessage m;
        m.id = kGoalMessageIdSample;
        m.goal = kGoalMessageGoalSample;
        check_message("goal 0x6E / 0x6F", m, ref, online::write_goal_message, online::read_goal_message);
    }
    {
        const ReferenceLevelSwitch ref = {.level = 0xB664035D, .flag = 'F', .pad = {}};
        online::LevelSwitch m;
        m.level = kLevelChecksumSample;
        m.flag = 'F';
        check_message("level switch 0x46 / 0x6B", m, ref, online::write_level_switch, online::read_level_switch, kLevelSwitchMinBytes);
    }
}

// Chat as the original's packet and THUGPRO's (its MsgChatTHUGPRO), THUGPRO's server info and change level.
#pragma pack(push, 1)
struct ReferenceChat {
    uint8_t color;
    std::array<char, kChatNameSize> name;
    std::array<char, kChatTextSize> text; // "hi there" and its NUL: the message is 0x12 + strlen(text) bytes
};
struct ReferenceThugproChat {
    uint32_t machine_id;
    uint8_t obj_id;
    std::array<char, kChatNameSize> name;
    std::array<char, kShortChatTextSize> text; // "hello" and its NUL
};
struct ReferenceThugproServerInfo {
    uint32_t time_of_day, weather;
    uint64_t flags;
};
struct ReferenceThugproChangeLevel {
    uint32_t level;
    uint8_t flag;
    std::array<uint8_t, 3> pad;
    std::array<char, kChangeLevelNameSize> name;
};
#pragma pack(pop)
static_assert(sizeof(ReferenceChat) == kChatFixedBytes + kChatTextLength && sizeof(ReferenceThugproChat) == kThugproChatFixedBytes + kThugproChatTextBytes &&
              sizeof(ReferenceThugproServerInfo) == online::thugpro::kServerInfoSize &&
              sizeof(ReferenceThugproChangeLevel) == online::thugpro::kChangeLevelThugproSize);

constexpr uint32_t kThugproChatMachineIdSample = 0xDEADBEEF;
constexpr int kThugproServerInfoTimeOfDaySample = 0x12345678;
constexpr int kThugproServerInfoWeatherSample = 0x1CA1FF20;
constexpr int kThugproServerInfoFlagsSample = 0x4281;
void check_chat_messages()
{
    {
        ReferenceChat ref = {.color = 3, .name = {}, .text = {}};
        text::copy_into(ref.name, "skater");
        text::copy_into(ref.text, "hi there");
        online::Chat m;
        m.color = 3;
        text::copy_into(m.name, "skater");
        text::copy_into(m.text, "hi there");
        check_message("chat 0x4C", m, ref, online::write_chat, online::read_chat, kChatMinBytes);
        // a text without its NUL: cut at the message's end
        ReferenceChat open = ref;
        core::at(open.text, kChatTextLastIndex) = '!';
        util::ByteReader reader(&open, sizeof(open));
        online::Chat back;
        check(online::read_chat(reader, &back) && std::strcmp(back.text.data(), "hi there!") == 0,
              "chat 0x4C: an unended text cut at the message's end");
    }
    {
        ReferenceThugproChat ref = {.machine_id = kThugproChatMachineIdSample, .obj_id = 2, .name = {}, .text = {}};
        text::copy_into(ref.name, "pro");
        text::copy_into(ref.text, "hello");
        online::thugpro::ChatMessage m;
        m.machine_id_ = kThugproChatMachineIdSample;
        m.obj_id_ = 2;
        text::copy_into(m.name, "pro");
        text::copy_into(m.text, "hello");
        check_message("THUGPRO chat 76 (TP-R2)", m, ref, online::thugpro::write_chat_message,
                      online::thugpro::read_chat_message);
        // THUGPRO's drops: a name filling its field, a text without its NUL
        ReferenceThugproChat bad = ref;
        std::memset(bad.name.data(), 'n', kChatNameSize);
        util::ByteReader name_reader(&bad, sizeof(bad));
        online::thugpro::ChatMessage back;
        check(!online::thugpro::read_chat_message(name_reader, &back), "THUGPRO chat 76: an unended name dropped");
        bad = ref;
        core::at(bad.text, kShortChatTextLastIndex) = '!';
        util::ByteReader text_reader(&bad, sizeof(bad));
        check(!online::thugpro::read_chat_message(text_reader, &back), "THUGPRO chat 76: an unended text dropped");
    }
    {
        const ReferenceThugproServerInfo ref = {.time_of_day = 0x12345678, .weather = 0x1CA1FF20, .flags = 0x0000000000004281ull};
        online::thugpro::ServerInfo m;
        m.time_of_day_ = kThugproServerInfoTimeOfDaySample;
        m.weather = kThugproServerInfoWeatherSample;
        m.flags = kThugproServerInfoFlagsSample;
        check_message("THUGPRO server info 155 (TP-R3)", m, ref, online::thugpro::write_server_info,
                      online::thugpro::read_server_info);
    }
    {
        ReferenceThugproChangeLevel ref = {.level = kLevelChecksumSample, .flag = 0, .pad = {}, .name = {}};
        text::copy_into(ref.name, "Custom Park\n");
        online::LevelSwitch level;
        level.level = kLevelChecksumSample;
        std::array<uint8_t, online::thugpro::kChangeLevelThugproSize> bytes{};
        util::ByteWriter writer(bytes.data(), sizeof(bytes));
        online::thugpro::write_change_level(writer, level, "Custom Park\n");
        check(writer.ok() && writer.position() == sizeof(ref) && std::memcmp(bytes.data(), &ref, sizeof(ref)) == 0,
              "THUGPRO change level 70 (TP-R5): as THUGPRO's");
    }
}

// The news file's header and text (motd.dat, after its ^ 0x6D).
void check_news()
{
    std::array<uint8_t, kNewsHeaderBytes + kNewsDataBytes> file = {};
    const auto text = std::to_array("Hello!"); // 7 with its NUL: 8 as a word
    int32_t size = kNewsSizeSample;
    std::memcpy(std::span(file).subspan(4).data(), &size, 4);
    std::memcpy(std::span(file).subspan(kNewsHeaderBytes).data(), text.data(), sizeof(text));
    core::at(file, kNewsAfterTextIndex) = kNewsGuardByte; // (the data after it)
    {
        util::ByteReader reader(file.data(), sizeof(file));
        online::News news;
        check(online::read_news(reader, &news) && news.size == kNewsSizeSample && std::strcmp(news.motd, "Hello!") == 0 &&
                  news.motd_size == kNewsTextBytes && core::at(std::span<const uint8_t>(news.data, sizeof(file)), news.motd_size) == kNewsGuardByte && reader.remaining() == 0,
              "news: read");
    }
    {
        size = kNewsSizePastEnd; // past the end
        std::memcpy(std::span(file).subspan(4).data(), &size, 4);
        util::ByteReader reader(file.data(), sizeof(file));
        online::News news;
        check(!online::read_news(reader, &news), "news: a size past the end refused");
    }
    {
        size = 4; // the text has no NUL within it
        std::memcpy(std::span(file).subspan(4).data(), &size, 4);
        util::ByteReader reader(file.data(), sizeof(file));
        online::News news;
        check(!online::read_news(reader, &news), "news: a text without its NUL refused");
    }
}

// The skater collision messages as their senders built them (the projectile's HitMessage, the state history's).
#pragma pack(push, 1)
struct ReferenceHit {
    uint8_t id;
    uint8_t fireball;
    std::array<uint8_t, 2> pad;
    int32_t score;
};
struct ReferenceRanInto {
    uint8_t id;
    std::array<uint8_t, 3> pad;
    std::array<float, 4> push;
};
#pragma pack(pop)
static_assert(sizeof(ReferenceHit) == online::kHitSize && sizeof(ReferenceRanInto) == online::kRanIntoSize);

constexpr int kHitObjectIdSample = 5;
constexpr int kHitScoreSample = 120;
constexpr int kRanIntoIdSample = 0x83;
constexpr int kRanIntoObjectIdSample = 0x83;
void check_collision_messages()
{
    const ReferenceHit hit_ref = {.id = 5, .fireball = 1, .pad = {}, .score = 120};
    online::Hit hit;
    hit.object_id = kHitObjectIdSample;
    hit.fireball = true;
    hit.score = kHitScoreSample;
    check_message("hit 0x84 / 0x85", hit, hit_ref, online::write_hit, online::read_hit);
    ReferenceRanInto ran_ref = {.id = kRanIntoIdSample, .pad = {}, .push = {kRanIntoPushXSample, -2, kRanIntoPushZSample, 0}};
    online::RanInto ran;
    ran.object_id = kRanIntoObjectIdSample;
    std::memcpy(ran.push.data(), ran_ref.push.data(), sizeof(ran.push));
    check_message("ran into 0x43", ran, ran_ref, online::write_ran_into, online::read_ran_into);
}

// The park's rails (0x7A, sent whole): written and read as the rail editor's buffer - the bytes the original's own
// buffer held (its in-memory record, byte for byte); cut short, more than 200 rails or 400 points refused (the
// original copied 0x1132 bytes and walked the counts as they came).
void check_rails()
{
    park_editing::RailBuffer sent{};
    sent.rail_count_ = 3;
    core::at(sent.point_counts_, 0) = 2;
    core::at(sent.point_counts_, 1) = 0;
    core::at(sent.point_counts_, 2) = kThirdRailPointCount;
    for (int i = 0; i < kRailRecordCount; ++i)
        for (int k = 0; k < kRailRecordWords; ++k)
            core::at(core::at(sent.records_, i), k) = static_cast<uint16_t>(i * kRailRecordWords + k - kRailFillBias);
    std::vector<uint8_t> bytes(park_editing::rail_buffer_format::kSize);
    util::ByteWriter writer(bytes.data(), bytes.size());
    std::vector<uint8_t> in_memory(sizeof(sent));
    std::memcpy(in_memory.data(), &sent, sizeof(sent));
    check(park_editing::rail_buffer_format::write_rail_buffer(writer, sent) && writer.remaining() == 0 &&
              bytes == in_memory,
          "rails 0x7A: written as the original's buffer");
    auto read = std::make_unique<park_editing::RailBuffer>();
    util::ByteReader reader(bytes.data(), bytes.size());
    check(park_editing::rail_buffer_format::read_rail_buffer(reader, read.get()) &&
              std::memcmp(read.get(), &sent, sizeof(sent)) == 0 && reader.remaining() == 0,
          "rails 0x7A: as the original's copy");
    util::ByteReader cut(bytes.data(), bytes.size() - 1);
    check(!park_editing::rail_buffer_format::read_rail_buffer(cut, read.get()) && read->rail_count_ == 0,
          "rails 0x7A: cut short");
    core::at(bytes, 0) = kTooManyRails;
    util::ByteReader rails(bytes.data(), bytes.size());
    check(!park_editing::rail_buffer_format::read_rail_buffer(rails, read.get()), "rails 0x7A: 201 rails");
    core::at(bytes, 0) = 3;
    core::at(bytes, 2 + 2 * 2) = kThirdRailPointCountLowByte; // the third rail's 398 points + 1 (0x18E -> 0x18F): 401 in all
    util::ByteReader points(bytes.data(), bytes.size());
    check(!park_editing::rail_buffer_format::read_rail_buffer(points, read.get()), "rails 0x7A: 401 points");
}

} // namespace

int run()
{
    text::print("game messages (batch 9 on)\n");
    check_skater_messages();
    check_graffiti_messages();
    check_play_messages();
    check_join_messages();
    check_session_others();
    check_chat_messages();
    check_news();
    check_collision_messages();
    check_rails();
    return failures();
}

} // namespace format_check_net

// NOT ORIGINAL - thug2_format_check's checks of the game messages converted from stage 5's batch 9 on: each message
// as the new writer encodes it compared byte for byte with the struct / byte array the original's sender built
// (padding zeroed), decoded and encoded again (identical), and cut short (the reader fails).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine/net/gamenet_messages.h"
#include "engine/net/play_messages.h"
#include "engine/net/skater_messages.h"
#include "engine/util/byte_stream.h"
#include "game/park_editing/rail_buffer_format.h"
#include "platform/compiler.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy into the test messages' fixed fields)

namespace format_check_net {

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what)
{
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

// `message` written must be `reference`'s bytes; read back and written again the same; and a reader given fewer than
// `needed` bytes (default: all of them) fails.
template <typename Message, typename Reference>
void check_message(const std::string& what, const Message& message, const Reference& reference,
                   bool (*write)(util::ByteWriter&, const Message&), bool (*read)(util::ByteReader&, Message*),
                   size_t needed = sizeof(Reference))
{
    constexpr size_t kSize = sizeof(Reference);
    std::vector<uint8_t> bytes(kSize + 16, 0xCD);
    util::ByteWriter writer(bytes.data(), bytes.size());
    write(writer, message);
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
    check(writer.ok() && writer.position() == kSize && std::memcmp(bytes.data(), &reference, kSize) == 0,
          what + ": as the original's");
    util::ByteReader reader(bytes.data(), kSize);
    Message back{};
    bool read_ok = read(reader, &back);
    std::vector<uint8_t> again(kSize + 16, 0xCD);
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
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferencePlayAnim {
    uint8_t loop_type, object;
    uint32_t anim;
    int16_t times[4];
};
// NOLINTEND(readability-identifier-naming)
struct ReferenceHideGeom {
    uint32_t name;
    uint8_t object, hide, pad[2];
};
struct ReferenceAnimSpeed {
    float speed;
    uint8_t object, pad[3];
};
struct ReferenceSpinModel {
    int32_t period, power;
    int16_t start, range;
    uint8_t object, hold, axes, pad;
};
struct ReferenceSpecialItem {
    uint32_t params, bone;
    uint8_t object, slot, pad[2];
};
#pragma pack(pop)
static_assert(sizeof(ReferencePlayAnim) == online::kPlayAnimSize && sizeof(ReferenceHideGeom) == online::kHideGeomSize &&
              sizeof(ReferenceAnimSpeed) == online::kAnimSpeedSize &&
              sizeof(ReferenceSpinModel) == online::kSpinModelSize &&
              sizeof(ReferenceSpecialItem) == online::kSpecialItemSize);

void check_skater_messages()
{
    {
        ReferencePlayAnim ref = {2, 5, 0xA1B2C3D4, {-4096, 8192, 1229, 4096}};
        online::PlayAnimMessage m;
        m.loop_type = 2;
        m.object_id = 5;
        m.anim = 0xA1B2C3D4;
        m.from = static_cast<uint16_t>(-4096);
        m.to = 8192;
        m.blend_period = 1229;
        m.speed = 4096;
        check_message("play anim 0x29", m, ref, online::write_play_anim, online::read_play_anim);
    }
    {
        const uint8_t ref[2] = {3, 0xFE};
        online::PairMessage m;
        m.first = 3;
        m.second = 0xFE;
        check_message("pair 0x2A-0x3D", m, ref, online::write_pair, online::read_pair);
    }
    {
        ReferenceHideGeom ref = {0x12345678, 7, 1, {}};
        online::HideGeomMessage m;
        m.name = 0x12345678;
        m.object_id = 7;
        m.hide = true;
        check_message("hide geom 0x3E", m, ref, online::write_hide_geom, online::read_hide_geom, 6);
    }
    {
        ReferenceAnimSpeed ref = {1.25f, 4, {}};
        online::AnimSpeedMessage m;
        std::memcpy(&m.speed_bits, &ref.speed, 4);
        m.object_id = 4;
        check_message("anim speed 0x41", m, ref, online::write_anim_speed, online::read_anim_speed, 5);
    }
    {
        ReferenceSpinModel ref = {1500, 2, -90, 360, 1, 1, 5, 0};
        online::SpinModelMessage m;
        m.period = 1500;
        m.power = 2;
        m.start = -90;
        m.range = 360;
        m.object_id = 1;
        m.hold = 1;
        m.axes = 5;
        check_message("spin model 0x76", m, ref, online::write_spin_model, online::read_spin_model, 15);
    }
    {
        ReferenceSpecialItem ref = {0xCAFEBABE, 0x0BADF00D, 6, 0xFF, {}};
        online::SpecialItemMessage m;
        m.params = 0xCAFEBABE;
        m.bone = 0x0BADF00D;
        m.object_id = 6;
        m.slot = -1;
        check_message("special item 0x77 / 0x78", m, ref, online::write_special_item, online::read_special_item, 10);
    }
}

// The graffiti and score messages as their senders built them (score_trick_objects, PointTally, send_points).
#pragma pack(push, 1)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceTrickObjects {
    uint8_t one, unknown01[9], game_id, pad0b;
    int32_t player;
    uint32_t score, count, objects[3];
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceObservedTrickObjects {
    uint8_t game_id, pad[3];
    int32_t player;
    uint32_t count, objects[3];
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceTagStolen {
    uint8_t game_id, pad[3];
    int32_t thief, victim;
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferencePoints {
    uint8_t game_id, pad[3];
    int32_t points;
};
// NOLINTEND(readability-identifier-naming)
#pragma pack(pop)
static_assert(offsetof(ReferenceTrickObjects, objects) == online::kTrickObjectsHeaderSize &&
              offsetof(ReferenceObservedTrickObjects, objects) == online::kObservedTrickObjectsHeaderSize &&
              sizeof(ReferenceTagStolen) == online::kTagStolenSize && sizeof(ReferencePoints) == online::kPointsSize);

void check_graffiti_messages()
{
    auto message = std::make_unique<online::TrickObjectsMessage>();
    message->game_id = 9;
    message->player = 3;
    message->score = 12345;
    message->count = 3;
    message->objects[0] = 0x11111111;
    message->objects[1] = 0x22222222;
    message->objects[2] = 0x33333333;
    ReferenceTrickObjects ref = {1, {}, 9, 0, 3, 12345, 3, {0x11111111, 0x22222222, 0x33333333}};
    check_message("trick objects 0x3B", *message, ref, online::write_trick_objects, online::read_trick_objects);
    ReferenceObservedTrickObjects observed = {9, {}, 3, 3, {0x11111111, 0x22222222, 0x33333333}};
    check_message("observed trick objects 0x5B", *message, observed, online::write_observed_trick_objects,
                  online::read_observed_trick_objects);
    // a count past the message's end, or past the senders' buffer: refused
    ref.count = 4;
    util::ByteReader reader(&ref, sizeof(ref));
    check(!online::read_trick_objects(reader, message.get()) && message->count == 0,
          "trick objects 0x3B: a count past the end refused");
    std::vector<uint8_t> big(online::kTrickObjectsHeaderSize + 4 * (online::kMaxTrickObjects + 1), 0);
    big[0] = 1;
    uint32_t too_many = online::kMaxTrickObjects + 1;
    std::memcpy(big.data() + 0x14, &too_many, 4);
    util::ByteReader big_reader(big.data(), big.size());
    check(!online::read_trick_objects(big_reader, message.get()), "trick objects 0x3B: more than the buffer refused");

    ReferenceTagStolen stolen_ref = {7, {}, 2, 5};
    online::TagStolenMessage stolen;
    stolen.game_id = 7;
    stolen.thief_id = 2;
    stolen.victim_id = 5;
    check_message("tag stolen 0x4B", stolen, stolen_ref, online::write_tag_stolen, online::read_tag_stolen);

    ReferencePoints points_ref = {4, {}, -250};
    online::PointsMessage points;
    points.game_id = 4;
    points.points = -250;
    check_message("points 0x52 / 0x86", points, points_ref, online::write_points, online::read_points);
}

// The other play messages as their senders built them (hunt_cfuncs, tagging, the terrain sounds, the cheats, the
// script manager).
#pragma pack(push, 1)
struct ReferenceHuntPlacements {
    uint8_t id, pad[3];
    int32_t count;
    float positions[5][4];
};
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceBoardVehicle {
    uint8_t skater, pad[3];
    uint32_t control_type;
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceProjectile {
    uint32_t zero;
    float pos[4], vel[4];
    uint32_t type, ammo_type;
    int32_t radius;
    float scale;
    uint8_t id, pad[3];
};
// NOLINTEND(readability-identifier-naming)
struct ReferencePlaceTag {
    uint8_t object, sticker, pad[2];
    float start[4], end[4];
    int32_t size;
};
struct ReferenceTerrainSound {
    int16_t x, y, z;
    uint8_t table, terrain, volume, pitch, value, pad;
};
struct ReferenceToggleCheat {
    uint32_t cheat;
    uint8_t on, pad[3];
};
struct ReferenceCheats {
    int32_t count;
    uint32_t cheats[3];
};
struct ReferenceRunScript {
    uint32_t name, object;
};
struct ReferenceSpawnScript {
    uint32_t name, object;
    int32_t node;
    uint8_t permanent, pad[3];
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

void check_play_messages()
{
    {
        ReferenceHuntPlacements ref = {3, {}, 2, {{1, 2, 3, 0}, {-4, 5.5f, 6, 1}, {}, {}, {}}};
        online::HuntPlacements m;
        m.player_id = 3;
        m.count = 2;
        std::memcpy(m.positions, ref.positions, sizeof(ref.positions));
        check_message("hunt placements 0x8F", m, ref, online::write_hunt_placements, read_hunt_placements_whole);
        float at[4];
        util::ByteReader reader(&ref, sizeof(ref));
        online::HuntPlacements back;
        online::read_hunt_placements(reader, &back);
        online::read_hunt_position(reader, at);
        online::read_hunt_position(reader, at);
        check(reader.ok() && at[1] == 5.5f && back.count == 2, "hunt placements 0x8F: the positions read");
        const float one[4] = {7, 8, 9, 1};
        uint8_t bytes[16];
        util::ByteWriter writer(bytes, sizeof(bytes));
        online::write_hunt_position(writer, one);
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
        check(writer.ok() && std::memcmp(bytes, one, 16) == 0, "hunt placement 0x8F: as the original's vector");
    }
    {
        ReferenceBoardVehicle ref = {5, {}, 0x8A6B04AD};
        online::BoardVehicle m;
        m.skater_id = 5;
        m.control_type = 0x8A6B04AD;
        check_message("board vehicle 0x89", m, ref, online::write_board_vehicle, online::read_board_vehicle);
    }
    {
        ReferenceProjectile ref = {0, {1, 2, 3, 0}, {4, 5, 6, 0}, 0xD039432C, 0x11223344, 0xF0, 2.5f, 6, {}};
        online::Projectile m;
        std::memcpy(m.position, ref.pos, 16);
        std::memcpy(m.velocity, ref.vel, 16);
        m.type = 0xD039432C;
        m.ammo_type = 0x11223344;
        m.radius = 0xF0;
        m.scale = 2.5f;
        m.owner_id = 6;
        check_message("projectile 0x83", m, ref, online::write_projectile, online::read_projectile, 0x35);
    }
    {
        ReferencePlaceTag ref = {2, 1, {}, {1, 2, 3, 1}, {4, 5, 6, 1}, 64};
        online::PlaceTag m;
        m.object_id = 2;
        m.sticker = true;
        std::memcpy(m.start, ref.start, 16);
        std::memcpy(m.end, ref.end, 16);
        m.size = 64;
        check_message("place tag 0x99", m, ref, online::write_place_tag, online::read_place_tag);
    }
    {
        ReferenceTerrainSound ref = {-100, 250, 3000, 2, 17, 100, 0x9C, 50, 0};
        online::TerrainSound m;
        m.x = -100;
        m.y = 250;
        m.z = 3000;
        m.table = 2;
        m.terrain = 17;
        m.volume = 100;
        m.pitch = static_cast<int8_t>(0x9C);
        m.value = 50;
        check_message("terrain sound 0x35", m, ref, online::write_terrain_sound, online::read_terrain_sound, 11);
    }
    {
        ReferenceToggleCheat ref = {0xEEFECF1B, 1, {}};
        online::ToggleCheat m;
        m.cheat = 0xEEFECF1B;
        m.on = true;
        check_message("toggle cheat 0x7F", m, ref, online::write_toggle_cheat, online::read_toggle_cheat, 5);
    }
    {
        ReferenceCheats ref = {3, {0xEEFECF1B, 0xB38341C9, 0xCD09E062}};
        online::Cheats m;
        m.count = 3;
        std::memcpy(m.cheats, ref.cheats, sizeof(ref.cheats));
        check_message("cheats 0x82", m, ref, online::write_cheats, online::read_cheats);
        ref.count = -1;
        util::ByteReader reader(&ref, sizeof(ref));
        online::Cheats back;
        check(!online::read_cheats(reader, &back) && back.count == 0, "cheats 0x82: a negative count refused");
    }
    {
        ReferenceRunScript ref = {0x12345678, 0xFFFFFFFF};
        online::RunScript m;
        m.name = 0x12345678;
        m.object_id = 0xFFFFFFFF;
        check_message("run script 0x48", m, ref, online::write_run_script, online::read_run_script);
        ReferenceSpawnScript spawn_ref = {0x9ABCDEF0, 3, 77, 1, {}};
        online::SpawnScript spawn;
        spawn.name = 0x9ABCDEF0;
        spawn.object_id = 3;
        spawn.node = 77;
        spawn.permanent = true;
        check_message("spawn script 0x49", spawn, spawn_ref, online::write_spawn_script, online::read_spawn_script, 13);
    }
}

// The join messages as the original's structs held them (ConnectInfo, JoinRequestBlock, ProceedInfo; THUGPRO's
// trailer as its release sends it).
#pragma pack(push, 1)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceConnectInfo {
    int32_t version;
    uint8_t observer, can_queue_up;
    char password[10];
    uint8_t broadband;
    int8_t rank;
    char cd_key_response[74];
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceJoinHeader {
    ReferenceConnectInfo connect;
    int32_t profile, rating, drive_scheme;
    char name[16];
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceProceed {
    uint32_t public_ip, private_ip;
    int32_t port;
    char server_name[16];
    int8_t max_players;
    uint8_t broadband, pad[2];
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceTrailer {
    char magic[10];
    uint8_t pad[2];
    uint32_t version, release, scripts_hash, machine_id;
    uint8_t modified, pad79[3];
};
// NOLINTEND(readability-identifier-naming)
#pragma pack(pop)
static_assert(sizeof(ReferenceConnectInfo) == online::kConnectInfoSize &&
              offsetof(ReferenceConnectInfo, cd_key_response) == 0x12 &&
              sizeof(ReferenceJoinHeader) == online::kJoinInfoHeaderSize &&
              offsetof(ReferenceJoinHeader, name) == 0x68 && sizeof(ReferenceProceed) == online::kProceedInfoSize &&
              offsetof(ReferenceProceed, max_players) == 0x1C &&
              sizeof(ReferenceTrailer) == online::thugpro::kConnectTrailerSize &&
              online::kConnectInfoSize + offsetof(ReferenceTrailer, version) == 0x68 &&
              online::kConnectInfoSize + offsetof(ReferenceTrailer, modified) == 0x78);

void check_join_messages()
{
    ReferenceConnectInfo ref = {};
    ref.version = online::kVersion;
    ref.observer = 1;
    ref.can_queue_up = 1;
    std::strcpy(ref.password, "secret123");
    ref.broadband = 1;
    ref.rank = -3;
    std::memset(ref.cd_key_response, 'k', 72);
    online::ConnectInfo info = {};
    info.version = online::kVersion;
    info.observer = true;
    info.can_queue_up_ = true;
    std::strcpy(info.password, "secret123");
    info.broadband = true;
    info.rank = -3;
    std::memset(info.cd_key_response_, 'k', 72);
    check_message("connection request 3", info, ref, online::write_connect_info, online::read_connect_info);

    ReferenceJoinHeader join_ref = {};
    join_ref.connect = ref;
    join_ref.profile = 123456;
    join_ref.rating = 900;
    join_ref.drive_scheme = 2;
    std::strcpy(join_ref.name, "fifteen_letters");
    online::JoinRequestHeader join = {};
    static_cast<online::ConnectInfo&>(join) = info;
    join.profile = 123456;
    join.rating = 900;
    join.drive_scheme_ = 2;
    std::strcpy(join.name, "fifteen_letters");
    check_message("join request 80", join, join_ref, online::write_join_request, online::read_join_request);

    ReferenceProceed proceed_ref = {0x0100007F, 0x0A00000A, 5150, {}, 8, 1, {}};
    std::strcpy(proceed_ref.server_name, "Skate Shop");
    online::ProceedInfo proceed = {};
    proceed.public_ip_ = 0x0100007F;
    proceed.private_ip_ = 0x0A00000A;
    proceed.port = 5150;
    std::strcpy(proceed.server_name_, "Skate Shop");
    proceed.max_players_ = 8;
    proceed.broadband = true;
    check_message("join proceed 79", proceed, proceed_ref, online::write_proceed_info, online::read_proceed_info, 30);
    // a server name filling its field (no terminator): cut there
    std::memset(proceed_ref.server_name, 'N', 16);
    util::ByteReader reader(&proceed_ref, sizeof(proceed_ref));
    online::ProceedInfo back;
    check(online::read_proceed_info(reader, &back) && std::strlen(back.server_name_) == 15 && back.max_players_ == 8,
          "join proceed 79: an unterminated name cut to its field");

    ReferenceTrailer trailer_ref = {{'_', 'T', 'H', 'U', 'G', '_', 'P', 'R', 'O', '_'}, {}, 0x00070000, 0x32475554,
                                    0x09FC08B0, 0xA1B2C3D4, 1, {}};
    online::thugpro::ConnectTrailer trailer = {};
    std::memcpy(trailer.magic, online::thugpro::kMagic, sizeof(trailer.magic));
    trailer.version = 0x00070000;
    trailer.release = 0x32475554;
    trailer.scripts_hash_ = 0x09FC08B0;
    trailer.machine_id_ = 0xA1B2C3D4;
    trailer.modified_scripts_ = true;
    check_message("THUGPRO connection trailer (TP-R12)", trailer, trailer_ref, online::thugpro::write_connect_trailer,
                  online::thugpro::read_connect_trailer, 29);
}

// The session's other messages as the original's senders built them (NewPlayerMessage, PanelMessage,
// StartGameRequest, the kick request, the goal messages, the level switch).
#pragma pack(push, 1)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferencePlayerArrived {
    int32_t profile_id, rating;
    uint32_t flags;
    int32_t score;
    uint32_t value10, value14;
    int8_t obj_id, team;
    uint8_t pad[2];
    int32_t drive_scheme;
    char name[16];
};
// NOLINTEND(readability-identifier-naming)
struct ReferenceHudNotice {
    uint32_t properties, text;
    int32_t time;
    char string1[16], string2[16];
};
struct ReferenceStartRequest {
    uint32_t words[13];
};
struct ReferenceKickRequest {
    uint8_t request, index;
    char name[16];
};
struct ReferenceGoalMessage {
    uint8_t id, pad[3];
    uint32_t goal;
};
struct ReferenceLevelSwitch {
    uint32_t level;
    uint8_t flag, pad[3];
};
#pragma pack(pop)
static_assert(sizeof(ReferencePlayerArrived) == online::kPlayerArrivedHeaderSize &&
              offsetof(ReferencePlayerArrived, obj_id) == 0x18 && offsetof(ReferencePlayerArrived, name) == 0x20 &&
              sizeof(ReferenceHudNotice) == online::kHudNoticeSize &&
              sizeof(ReferenceStartRequest) == online::kStartRequestSize &&
              sizeof(ReferenceKickRequest) == online::kKickRequestSize &&
              sizeof(ReferenceGoalMessage) == online::kGoalMessageSize &&
              sizeof(ReferenceLevelSwitch) == online::kLevelSwitchSize &&
              offsetof(ReferenceLevelSwitch, level) == online::kLevelSwitchLevelAt);

void check_session_others()
{
    {
        ReferencePlayerArrived ref = {4242, 1500, 0x11, 9000, 77, 31337, 3, 1, {}, 2, {}};
        std::strcpy(ref.name, "player_one");
        online::PlayerArrived m;
        m.profile_id = 4242;
        m.rating = 1500;
        m.flags = 0x11;
        m.score = 9000;
        m.eliminated_time = 77;
        m.accumulated_score = 31337;
        m.object_id = 3;
        m.team = 1;
        m.drive_scheme = 2;
        std::strcpy(m.name, "player_one");
        check_message("player arrived 0x22", m, ref, online::write_player_arrived, online::read_player_arrived);
    }
    {
        ReferenceHudNotice ref = {0x11112222, 0x33334444, 5000, {}, {}};
        std::strcpy(ref.string1, "Skater 1");
        std::strcpy(ref.string2, "Team Red");
        online::HudNotice m;
        m.properties = 0x11112222;
        m.text = 0x33334444;
        m.time = 5000;
        std::strcpy(m.string1, "Skater 1");
        std::strcpy(m.string2, "Team Red");
        check_message("hud notice 0x3F", m, ref, online::write_hud_notice, online::read_hud_notice);
        // string 1 filling its field (no terminator): cut there, string 2 still read
        std::memset(ref.string1, 'x', 16);
        util::ByteReader reader(&ref, sizeof(ref));
        online::HudNotice back;
        check(online::read_hud_notice(reader, &back) && std::strlen(back.string1) == 16 &&
                  std::strcmp(back.string2, "Team Red") == 0,
              "hud notice 0x3F: an unterminated string ends in its field");
    }
    {
        ReferenceStartRequest ref = {{1, 2, 3, 4, 5, 6, 0, 8, 9, 10, 11, 12, 13}};
        online::StartRequest m;
        m.game_type = 1;
        m.time_limit = 2;
        m.skill_level = 3;
        m.fireball_level = 4;
        m.player_collision = 5;
        m.friendly_fire = 6;
        m.target_score = 8;
        m.stop_at_zero = 9;
        m.score_reset = 10;
        m.ctf_game_type = 11;
        m.goals = 12;
        m.team_graffiti_mode = 13;
        check_message("start request 0x60", m, ref, online::write_start_request, online::read_start_request);
    }
    {
        ReferenceKickRequest ref = {1, 2, {}};
        std::strcpy(ref.name, "griefer");
        online::KickRequest m;
        m.kick = 1;
        m.index = 2;
        std::strcpy(m.name, "griefer");
        check_message("kick request 0x61", m, ref, online::write_kick_request, online::read_kick_request);
    }
    {
        ReferenceGoalMessage ref = {7, {}, 0xC0FFEE00};
        online::GoalMessage m;
        m.id = 7;
        m.goal = 0xC0FFEE00;
        check_message("goal 0x6E / 0x6F", m, ref, online::write_goal_message, online::read_goal_message);
    }
    {
        ReferenceLevelSwitch ref = {0xB664035D, 'F', {}};
        online::LevelSwitch m;
        m.level = 0xB664035D;
        m.flag = 'F';
        check_message("level switch 0x46 / 0x6B", m, ref, online::write_level_switch, online::read_level_switch, 5);
    }
}

// Chat as the original's packet and THUGPRO's (its MsgChatTHUGPRO), THUGPRO's server info and change level.
#pragma pack(push, 1)
struct ReferenceChat {
    uint8_t color;
    char name[16];
    char text[9]; // "hi there" and its NUL: the message is 0x12 + strlen(text) bytes
};
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceThugproChat {
    uint32_t machine_id;
    uint8_t obj_id;
    char name[16];
    char text[6]; // "hello" and its NUL
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceThugproServerInfo {
    uint32_t time_of_day, weather;
    uint64_t flags;
};
// NOLINTEND(readability-identifier-naming)
struct ReferenceThugproChangeLevel {
    uint32_t level;
    uint8_t flag, pad[3];
    char name[32];
};
#pragma pack(pop)
static_assert(sizeof(ReferenceChat) == 0x12 + 8 && sizeof(ReferenceThugproChat) == 21 + 6 &&
              sizeof(ReferenceThugproServerInfo) == online::thugpro::kServerInfoSize &&
              sizeof(ReferenceThugproChangeLevel) == online::thugpro::kChangeLevelThugproSize);

void check_chat_messages()
{
    {
        ReferenceChat ref = {3, {}, {}};
        std::strcpy(ref.name, "skater");
        std::strcpy(ref.text, "hi there");
        online::Chat m;
        m.color = 3;
        std::strcpy(m.name, "skater");
        std::strcpy(m.text, "hi there");
        check_message("chat 0x4C", m, ref, online::write_chat, online::read_chat, 17);
        // a text without its NUL: cut at the message's end
        ReferenceChat open = ref;
        open.text[8] = '!';
        util::ByteReader reader(&open, sizeof(open));
        online::Chat back;
        check(online::read_chat(reader, &back) && std::strcmp(back.text, "hi there!") == 0,
              "chat 0x4C: an unended text cut at the message's end");
    }
    {
        ReferenceThugproChat ref = {0xDEADBEEF, 2, {}, {}};
        std::strcpy(ref.name, "pro");
        std::strcpy(ref.text, "hello");
        online::thugpro::ChatMessage m;
        m.machine_id_ = 0xDEADBEEF;
        m.obj_id_ = 2;
        std::strcpy(m.name, "pro");
        std::strcpy(m.text, "hello");
        check_message("THUGPRO chat 76 (TP-R2)", m, ref, online::thugpro::write_chat_message,
                      online::thugpro::read_chat_message);
        // THUGPRO's drops: a name filling its field, a text without its NUL
        ReferenceThugproChat bad = ref;
        std::memset(bad.name, 'n', 16);
        util::ByteReader name_reader(&bad, sizeof(bad));
        online::thugpro::ChatMessage back;
        check(!online::thugpro::read_chat_message(name_reader, &back), "THUGPRO chat 76: an unended name dropped");
        bad = ref;
        bad.text[5] = '!';
        util::ByteReader text_reader(&bad, sizeof(bad));
        check(!online::thugpro::read_chat_message(text_reader, &back), "THUGPRO chat 76: an unended text dropped");
    }
    {
        ReferenceThugproServerInfo ref = {0x12345678, 0x1CA1FF20, 0x0000000000004281ull};
        online::thugpro::ServerInfo m;
        m.time_of_day_ = 0x12345678;
        m.weather = 0x1CA1FF20;
        m.flags = 0x4281;
        check_message("THUGPRO server info 155 (TP-R3)", m, ref, online::thugpro::write_server_info,
                      online::thugpro::read_server_info);
    }
    {
        ReferenceThugproChangeLevel ref = {0xB664035D, 0, {}, {}};
        std::strcpy(ref.name, "Custom Park\n");
        online::LevelSwitch level;
        level.level = 0xB664035D;
        uint8_t bytes[online::thugpro::kChangeLevelThugproSize];
        util::ByteWriter writer(bytes, sizeof(bytes));
        online::thugpro::write_change_level(writer, level, "Custom Park\n");
        check(writer.ok() && writer.position() == sizeof(ref) && std::memcmp(bytes, &ref, sizeof(ref)) == 0,
              "THUGPRO change level 70 (TP-R5): as THUGPRO's");
    }
}

// The news file's header and text (motd.dat, after its ^ 0x6D).
void check_news()
{
    uint8_t file[8 + 12] = {};
    const char text[] = "Hello!"; // 7 with its NUL: 8 as a word
    int32_t size = 12;
    std::memcpy(file + 4, &size, 4);
    std::memcpy(file + 8, text, sizeof(text));
    file[16] = 0xAB; // (the data after it)
    {
        util::ByteReader reader(file, sizeof(file));
        online::News news;
        check(online::read_news(reader, &news) && news.size == 12 && std::strcmp(news.motd, "Hello!") == 0 &&
                  news.motd_size == 8 && news.data[news.motd_size] == 0xAB && reader.remaining() == 0,
              "news: read");
    }
    {
        size = 13; // past the end
        std::memcpy(file + 4, &size, 4);
        util::ByteReader reader(file, sizeof(file));
        online::News news;
        check(!online::read_news(reader, &news), "news: a size past the end refused");
    }
    {
        size = 4; // the text has no NUL within it
        std::memcpy(file + 4, &size, 4);
        util::ByteReader reader(file, sizeof(file));
        online::News news;
        check(!online::read_news(reader, &news), "news: a text without its NUL refused");
    }
}

// The skater collision messages as their senders built them (the projectile's HitMessage, the state history's).
#pragma pack(push, 1)
struct ReferenceHit {
    uint8_t id, fireball, pad[2];
    int32_t score;
};
struct ReferenceRanInto {
    uint8_t id, pad[3];
    float push[4];
};
#pragma pack(pop)
static_assert(sizeof(ReferenceHit) == online::kHitSize && sizeof(ReferenceRanInto) == online::kRanIntoSize);

void check_collision_messages()
{
    ReferenceHit hit_ref = {5, 1, {}, 120};
    online::Hit hit;
    hit.object_id = 5;
    hit.fireball = true;
    hit.score = 120;
    check_message("hit 0x84 / 0x85", hit, hit_ref, online::write_hit, online::read_hit);
    ReferenceRanInto ran_ref = {0x83, {}, {10, -2, 30.5f, 0}};
    online::RanInto ran;
    ran.object_id = 0x83;
    std::memcpy(ran.push, ran_ref.push, sizeof(ran.push));
    check_message("ran into 0x43", ran, ran_ref, online::write_ran_into, online::read_ran_into);
}

// The park's rails (0x7A, sent whole): read as the rail editor's own buffer, byte for byte; cut short, more than 200
// rails or 400 points refused (the original copied 0x1132 bytes and walked the counts as they came).
void check_rails()
{
    park_editing::RailBuffer sent{};
    sent.rail_count_ = 3;
    sent.point_counts_[0] = 2;
    sent.point_counts_[1] = 0;
    sent.point_counts_[2] = 398;
    for (int i = 0; i < 400; ++i)
        for (int k = 0; k < 5; ++k)
            sent.records_[i][k] = static_cast<uint16_t>(i * 5 + k - 7);
    std::vector<uint8_t> bytes(sizeof(sent));
    std::memcpy(bytes.data(), &sent, sizeof(sent));
    auto read = std::make_unique<park_editing::RailBuffer>();
    util::ByteReader reader(bytes.data(), bytes.size());
    check(park_editing::rail_buffer_format::read_rail_buffer(reader, read.get()) &&
              std::memcmp(read.get(), &sent, sizeof(sent)) == 0 && reader.remaining() == 0,
          "rails 0x7A: as the original's copy");
    util::ByteReader cut(bytes.data(), bytes.size() - 1);
    check(!park_editing::rail_buffer_format::read_rail_buffer(cut, read.get()) && read->rail_count_ == 0,
          "rails 0x7A: cut short");
    bytes[0] = 201;
    util::ByteReader rails(bytes.data(), bytes.size());
    check(!park_editing::rail_buffer_format::read_rail_buffer(rails, read.get()), "rails 0x7A: 201 rails");
    bytes[0] = 3;
    bytes[2 + 2 * 2] = 143; // the third rail's 398 points + 1 (0x18E -> 0x18F): 401 in all
    util::ByteReader points(bytes.data(), bytes.size());
    check(!park_editing::rail_buffer_format::read_rail_buffer(points, read.get()), "rails 0x7A: 401 points");
}

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage): format_check.cpp declares it
int run()
{
    std::printf("game messages (batch 9 on)\n");
    check_skater_messages();
    check_graffiti_messages();
    check_play_messages();
    check_join_messages();
    check_session_others();
    check_chat_messages();
    check_news();
    check_collision_messages();
    check_rails();
    return g_failures;
}

} // namespace format_check_net

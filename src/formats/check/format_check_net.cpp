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

// `message` written must be `reference`'s bytes; read back and written again the same; and a reader given fewer than
// `needed` bytes (default: all of them) fails.
template <typename Message, typename Reference>
void check_message(const std::string& what, const Message& message, const Reference& reference,
                   const std::type_identity_t<std::function<bool(util::ByteWriter&, const Message&)>>& write,
                   const std::type_identity_t<std::function<bool(util::ByteReader&, Message*)>>& read,
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
    const bool read_ok = read(reader, &back);
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
    std::array<int16_t, 4> times;
};
// NOLINTEND(readability-identifier-naming)
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

void check_skater_messages()
{
    {
        const ReferencePlayAnim ref = {.loop_type = 2, .object = 5, .anim = 0xA1B2C3D4, .times = {-4096, 8192, 1229, 4096}};
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
        const std::array<uint8_t, 2> ref = {{3, 0xFE}};
        online::PairMessage m;
        m.first = 3;
        m.second = 0xFE;
        check_message("pair 0x2A-0x3D", m, ref, online::write_pair, online::read_pair);
    }
    {
        const ReferenceHideGeom ref = {.name = 0x12345678, .object = 7, .hide = 1, .pad = {}};
        online::HideGeomMessage m;
        m.name = 0x12345678;
        m.object_id = 7;
        m.hide = true;
        check_message("hide geom 0x3E", m, ref, online::write_hide_geom, online::read_hide_geom, 6);
    }
    {
        ReferenceAnimSpeed ref = {.speed = 1.25f, .object = 4, .pad = {}};
        online::AnimSpeedMessage m;
        std::memcpy(&m.speed_bits, &ref.speed, 4);
        m.object_id = 4;
        check_message("anim speed 0x41", m, ref, online::write_anim_speed, online::read_anim_speed, 5);
    }
    {
        const ReferenceSpinModel ref = {.period = 1500, .power = 2, .start = -90, .range = 360, .object = 1, .hold = 1, .axes = 5, .pad = 0};
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
        const ReferenceSpecialItem ref = {.params = 0xCAFEBABE, .bone = 0x0BADF00D, .object = 6, .slot = 0xFF, .pad = {}};
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
    uint8_t one;
    std::array<uint8_t, 9> unknown01;
    uint8_t game_id;
    uint8_t pad0b;
    int32_t player;
    uint32_t score;
    uint32_t count;
    std::array<uint32_t, 3> objects;
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceObservedTrickObjects {
    uint8_t game_id;
    std::array<uint8_t, 3> pad;
    int32_t player;
    uint32_t count;
    std::array<uint32_t, 3> objects;
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceTagStolen {
    uint8_t game_id;
    std::array<uint8_t, 3> pad;
    int32_t thief, victim;
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferencePoints {
    uint8_t game_id;
    std::array<uint8_t, 3> pad;
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
    core::at(message->objects, 0) = 0x11111111;
    core::at(message->objects, 1) = 0x22222222;
    core::at(message->objects, 2) = 0x33333333;
    ReferenceTrickObjects ref = {.one = 1, .unknown01 = {}, .game_id = 9, .pad0b = 0, .player = 3, .score = 12345, .count = 3, .objects = {0x11111111, 0x22222222, 0x33333333}};
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
    std::memcpy(std::span(big).subspan(0x14).data(), &too_many, 4);
    util::ByteReader big_reader(big.data(), big.size());
    check(!online::read_trick_objects(big_reader, message.get()), "trick objects 0x3B: more than the buffer refused");

    const ReferenceTagStolen stolen_ref = {.game_id = 7, .pad = {}, .thief = 2, .victim = 5};
    online::TagStolenMessage stolen;
    stolen.game_id = 7;
    stolen.thief_id = 2;
    stolen.victim_id = 5;
    check_message("tag stolen 0x4B", stolen, stolen_ref, online::write_tag_stolen, online::read_tag_stolen);

    const ReferencePoints points_ref = {.game_id = 4, .pad = {}, .points = -250};
    online::PointsMessage points;
    points.game_id = 4;
    points.points = -250;
    check_message("points 0x52 / 0x86", points, points_ref, online::write_points, online::read_points);
}

// The other play messages as their senders built them (hunt_cfuncs, tagging, the terrain sounds, the cheats, the
// script manager).
#pragma pack(push, 1)
struct ReferenceHuntPlacements {
    uint8_t id;
    std::array<uint8_t, 3> pad;
    int32_t count;
    std::array<std::array<float, 4>, 5> positions;
};
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceBoardVehicle {
    uint8_t skater;
    std::array<uint8_t, 3> pad;
    uint32_t control_type;
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
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
// NOLINTEND(readability-identifier-naming)
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

void check_play_messages()
{
    {
        ReferenceHuntPlacements ref = {.id = 3, .pad = {}, .count = 2, .positions = {{{{1, 2, 3, 0}}, {{-4, 5.5f, 6, 1}}, {}, {}, {}}}};
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
        check(reader.ok() && core::at(at, 1) == 5.5f && back.count == 2, "hunt placements 0x8F: the positions read");
        const std::array<float, 4> one = {{7, 8, 9, 1}};
        std::array<uint8_t, 16> bytes{};
        util::ByteWriter writer(bytes.data(), sizeof(bytes));
        online::write_hunt_position(writer, one);
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
        check(writer.ok() && std::memcmp(bytes.data(), one.data(), 16) == 0, "hunt placement 0x8F: as the original's vector");
    }
    {
        const ReferenceBoardVehicle ref = {.skater = 5, .pad = {}, .control_type = 0x8A6B04AD};
        online::BoardVehicle m;
        m.skater_id = 5;
        m.control_type = 0x8A6B04AD;
        check_message("board vehicle 0x89", m, ref, online::write_board_vehicle, online::read_board_vehicle);
    }
    {
        ReferenceProjectile ref = {.zero = 0, .pos = {1, 2, 3, 0}, .vel = {4, 5, 6, 0}, .type = 0xD039432C, .ammo_type = 0x11223344, .radius = 0xF0, .scale = 2.5f, .id = 6, .pad = {}};
        online::Projectile m;
        std::memcpy(m.position.data(), ref.pos.data(), 16);
        std::memcpy(m.velocity.data(), ref.vel.data(), 16);
        m.type = 0xD039432C;
        m.ammo_type = 0x11223344;
        m.radius = 0xF0;
        m.scale = 2.5f;
        m.owner_id = 6;
        check_message("projectile 0x83", m, ref, online::write_projectile, online::read_projectile, 0x35);
    }
    {
        ReferencePlaceTag ref = {.object = 2, .sticker = 1, .pad = {}, .start = {1, 2, 3, 1}, .end = {4, 5, 6, 1}, .size = 64};
        online::PlaceTag m;
        m.object_id = 2;
        m.sticker = true;
        std::memcpy(m.start.data(), ref.start.data(), 16);
        std::memcpy(m.end.data(), ref.end.data(), 16);
        m.size = 64;
        check_message("place tag 0x99", m, ref, online::write_place_tag, online::read_place_tag);
    }
    {
        const ReferenceTerrainSound ref = {.x = -100, .y = 250, .z = 3000, .table = 2, .terrain = 17, .volume = 100, .pitch = 0x9C, .value = 50, .pad = 0};
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
        const ReferenceToggleCheat ref = {.cheat = 0xEEFECF1B, .on = 1, .pad = {}};
        online::ToggleCheat m;
        m.cheat = 0xEEFECF1B;
        m.on = true;
        check_message("toggle cheat 0x7F", m, ref, online::write_toggle_cheat, online::read_toggle_cheat, 5);
    }
    {
        ReferenceCheats ref = {.count = 3, .cheats = {0xEEFECF1B, 0xB38341C9, 0xCD09E062}};
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
        m.name = 0x12345678;
        m.object_id = 0xFFFFFFFF;
        check_message("run script 0x48", m, ref, online::write_run_script, online::read_run_script);
        const ReferenceSpawnScript spawn_ref = {.name = 0x9ABCDEF0, .object = 3, .node = 77, .permanent = 1, .pad = {}};
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
    std::array<char, 10> password;
    uint8_t broadband;
    int8_t rank;
    std::array<char, 74> cd_key_response;
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceJoinHeader {
    ReferenceConnectInfo connect;
    int32_t profile, rating, drive_scheme;
    std::array<char, 16> name;
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceProceed {
    uint32_t public_ip, private_ip;
    int32_t port;
    std::array<char, 16> server_name;
    int8_t max_players;
    uint8_t broadband;
    std::array<uint8_t, 2> pad;
};
// NOLINTEND(readability-identifier-naming)
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceTrailer {
    std::array<char, 10> magic;
    std::array<uint8_t, 2> pad;
    uint32_t version, release, scripts_hash, machine_id;
    uint8_t modified;
    std::array<uint8_t, 3> pad79;
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
    std::strcpy(ref.password.data(), "secret123");
    ref.broadband = 1;
    ref.rank = -3;
    std::memset(ref.cd_key_response.data(), 'k', 72);
    online::ConnectInfo info = {};
    info.version = online::kVersion;
    info.observer = true;
    info.can_queue_up_ = true;
    std::strcpy(info.password.data(), "secret123");
    info.broadband = true;
    info.rank = -3;
    std::memset(info.cd_key_response_.data(), 'k', 72);
    check_message("connection request 3", info, ref, online::write_connect_info, online::read_connect_info);

    ReferenceJoinHeader join_ref = {};
    join_ref.connect = ref;
    join_ref.profile = 123456;
    join_ref.rating = 900;
    join_ref.drive_scheme = 2;
    std::strcpy(join_ref.name.data(), "fifteen_letters");
    online::JoinRequestHeader join = {};
    static_cast<online::ConnectInfo&>(join) = info;
    join.profile = 123456;
    join.rating = 900;
    join.drive_scheme_ = 2;
    std::strcpy(join.name.data(), "fifteen_letters");
    check_message("join request 80", join, join_ref, online::write_join_request, online::read_join_request);

    ReferenceProceed proceed_ref = {.public_ip = 0x0100007F, .private_ip = 0x0A00000A, .port = 5150, .server_name = {}, .max_players = 8, .broadband = 1, .pad = {}};
    std::strcpy(proceed_ref.server_name.data(), "Skate Shop");
    online::ProceedInfo proceed = {};
    proceed.public_ip_ = 0x0100007F;
    proceed.private_ip_ = 0x0A00000A;
    proceed.port = 5150;
    std::strcpy(proceed.server_name_.data(), "Skate Shop");
    proceed.max_players_ = 8;
    proceed.broadband = true;
    check_message("join proceed 79", proceed, proceed_ref, online::write_proceed_info, online::read_proceed_info, 30);
    // a server name filling its field (no terminator): cut there
    std::memset(proceed_ref.server_name.data(), 'N', 16);
    util::ByteReader reader(&proceed_ref, sizeof(proceed_ref));
    online::ProceedInfo back{};
    check(online::read_proceed_info(reader, &back) && std::strlen(back.server_name_.data()) == 15 && back.max_players_ == 8,
          "join proceed 79: an unterminated name cut to its field");

    const ReferenceTrailer trailer_ref = {
        .magic = {'_', 'T', 'H', 'U', 'G', '_', 'P', 'R', 'O', '_'}, .pad = {}, .version = 0x00070000, .release = 0x32475554, .scripts_hash = 0x09FC08B0, .machine_id = 0xA1B2C3D4, .modified = 1, .pad79 = {}};
    online::thugpro::ConnectTrailer trailer = {};
    std::memcpy(trailer.magic.data(), online::thugpro::kMagic.data(), sizeof(trailer.magic));
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
    std::array<uint8_t, 2> pad;
    int32_t drive_scheme;
    std::array<char, 16> name;
};
// NOLINTEND(readability-identifier-naming)
struct ReferenceHudNotice {
    uint32_t properties, text;
    int32_t time;
    std::array<char, 16> string1;
    std::array<char, 16> string2;
};
struct ReferenceStartRequest {
    std::array<uint32_t, 13> words;
};
struct ReferenceKickRequest {
    uint8_t request, index;
    std::array<char, 16> name;
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
        ReferencePlayerArrived ref = {.profile_id = 4242, .rating = 1500, .flags = 0x11, .score = 9000, .value10 = 77, .value14 = 31337, .obj_id = 3, .team = 1, .pad = {}, .drive_scheme = 2, .name = {}};
        std::strcpy(ref.name.data(), "player_one");
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
        std::strcpy(m.name.data(), "player_one");
        check_message("player arrived 0x22", m, ref, online::write_player_arrived, online::read_player_arrived);
    }
    {
        ReferenceHudNotice ref = {.properties = 0x11112222, .text = 0x33334444, .time = 5000, .string1 = {}, .string2 = {}};
        std::strcpy(ref.string1.data(), "Skater 1");
        std::strcpy(ref.string2.data(), "Team Red");
        online::HudNotice m;
        m.properties = 0x11112222;
        m.text = 0x33334444;
        m.time = 5000;
        std::strcpy(m.string1.data(), "Skater 1");
        std::strcpy(m.string2.data(), "Team Red");
        check_message("hud notice 0x3F", m, ref, online::write_hud_notice, online::read_hud_notice);
        // string 1 filling its field (no terminator): cut there, string 2 still read
        std::memset(ref.string1.data(), 'x', 16);
        util::ByteReader reader(&ref, sizeof(ref));
        online::HudNotice back;
        check(online::read_hud_notice(reader, &back) && std::strlen(back.string1.data()) == 16 &&
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
        ReferenceKickRequest ref = {.request = 1, .index = 2, .name = {}};
        std::strcpy(ref.name.data(), "griefer");
        online::KickRequest m;
        m.kick = 1;
        m.index = 2;
        std::strcpy(m.name.data(), "griefer");
        check_message("kick request 0x61", m, ref, online::write_kick_request, online::read_kick_request);
    }
    {
        const ReferenceGoalMessage ref = {.id = 7, .pad = {}, .goal = 0xC0FFEE00};
        online::GoalMessage m;
        m.id = 7;
        m.goal = 0xC0FFEE00;
        check_message("goal 0x6E / 0x6F", m, ref, online::write_goal_message, online::read_goal_message);
    }
    {
        const ReferenceLevelSwitch ref = {.level = 0xB664035D, .flag = 'F', .pad = {}};
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
    std::array<char, 16> name;
    std::array<char, 9> text; // "hi there" and its NUL: the message is 0x12 + strlen(text) bytes
};
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceThugproChat {
    uint32_t machine_id;
    uint8_t obj_id;
    std::array<char, 16> name;
    std::array<char, 6> text; // "hello" and its NUL
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
    uint8_t flag;
    std::array<uint8_t, 3> pad;
    std::array<char, 32> name;
};
#pragma pack(pop)
static_assert(sizeof(ReferenceChat) == 0x12 + 8 && sizeof(ReferenceThugproChat) == 21 + 6 &&
              sizeof(ReferenceThugproServerInfo) == online::thugpro::kServerInfoSize &&
              sizeof(ReferenceThugproChangeLevel) == online::thugpro::kChangeLevelThugproSize);

void check_chat_messages()
{
    {
        ReferenceChat ref = {.color = 3, .name = {}, .text = {}};
        std::strcpy(ref.name.data(), "skater");
        std::strcpy(ref.text.data(), "hi there");
        online::Chat m;
        m.color = 3;
        std::strcpy(m.name.data(), "skater");
        std::strcpy(m.text.data(), "hi there");
        check_message("chat 0x4C", m, ref, online::write_chat, online::read_chat, 17);
        // a text without its NUL: cut at the message's end
        ReferenceChat open = ref;
        core::at(open.text, 8) = '!';
        util::ByteReader reader(&open, sizeof(open));
        online::Chat back;
        check(online::read_chat(reader, &back) && std::strcmp(back.text.data(), "hi there!") == 0,
              "chat 0x4C: an unended text cut at the message's end");
    }
    {
        ReferenceThugproChat ref = {.machine_id = 0xDEADBEEF, .obj_id = 2, .name = {}, .text = {}};
        std::strcpy(ref.name.data(), "pro");
        std::strcpy(ref.text.data(), "hello");
        online::thugpro::ChatMessage m;
        m.machine_id_ = 0xDEADBEEF;
        m.obj_id_ = 2;
        std::strcpy(m.name.data(), "pro");
        std::strcpy(m.text.data(), "hello");
        check_message("THUGPRO chat 76 (TP-R2)", m, ref, online::thugpro::write_chat_message,
                      online::thugpro::read_chat_message);
        // THUGPRO's drops: a name filling its field, a text without its NUL
        ReferenceThugproChat bad = ref;
        std::memset(bad.name.data(), 'n', 16);
        util::ByteReader name_reader(&bad, sizeof(bad));
        online::thugpro::ChatMessage back;
        check(!online::thugpro::read_chat_message(name_reader, &back), "THUGPRO chat 76: an unended name dropped");
        bad = ref;
        core::at(bad.text, 5) = '!';
        util::ByteReader text_reader(&bad, sizeof(bad));
        check(!online::thugpro::read_chat_message(text_reader, &back), "THUGPRO chat 76: an unended text dropped");
    }
    {
        const ReferenceThugproServerInfo ref = {.time_of_day = 0x12345678, .weather = 0x1CA1FF20, .flags = 0x0000000000004281ull};
        online::thugpro::ServerInfo m;
        m.time_of_day_ = 0x12345678;
        m.weather = 0x1CA1FF20;
        m.flags = 0x4281;
        check_message("THUGPRO server info 155 (TP-R3)", m, ref, online::thugpro::write_server_info,
                      online::thugpro::read_server_info);
    }
    {
        ReferenceThugproChangeLevel ref = {.level = 0xB664035D, .flag = 0, .pad = {}, .name = {}};
        std::strcpy(ref.name.data(), "Custom Park\n");
        online::LevelSwitch level;
        level.level = 0xB664035D;
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
    std::array<uint8_t, 8 + 12> file = {};
    const auto text = std::to_array("Hello!"); // 7 with its NUL: 8 as a word
    int32_t size = 12;
    std::memcpy(std::span(file).subspan(4).data(), &size, 4);
    std::memcpy(std::span(file).subspan(8).data(), text.data(), sizeof(text));
    core::at(file, 16) = 0xAB; // (the data after it)
    {
        util::ByteReader reader(file.data(), sizeof(file));
        online::News news;
        check(online::read_news(reader, &news) && news.size == 12 && std::strcmp(news.motd, "Hello!") == 0 &&
                  news.motd_size == 8 && core::at(std::span<const uint8_t>(news.data, sizeof(file)), news.motd_size) == 0xAB && reader.remaining() == 0,
              "news: read");
    }
    {
        size = 13; // past the end
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

void check_collision_messages()
{
    const ReferenceHit hit_ref = {.id = 5, .fireball = 1, .pad = {}, .score = 120};
    online::Hit hit;
    hit.object_id = 5;
    hit.fireball = true;
    hit.score = 120;
    check_message("hit 0x84 / 0x85", hit, hit_ref, online::write_hit, online::read_hit);
    ReferenceRanInto ran_ref = {.id = 0x83, .pad = {}, .push = {10, -2, 30.5f, 0}};
    online::RanInto ran;
    ran.object_id = 0x83;
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
    core::at(sent.point_counts_, 2) = 398;
    for (int i = 0; i < 400; ++i)
        for (int k = 0; k < 5; ++k)
            core::at(core::at(sent.records_, i), k) = static_cast<uint16_t>(i * 5 + k - 7);
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
    core::at(bytes, 0) = 201;
    util::ByteReader rails(bytes.data(), bytes.size());
    check(!park_editing::rail_buffer_format::read_rail_buffer(rails, read.get()), "rails 0x7A: 201 rails");
    core::at(bytes, 0) = 3;
    core::at(bytes, 2 + 2 * 2) = 143; // the third rail's 398 points + 1 (0x18E -> 0x18F): 401 in all
    util::ByteReader points(bytes.data(), bytes.size());
    check(!park_editing::rail_buffer_format::read_rail_buffer(points, read.get()), "rails 0x7A: 401 points");
}

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage): format_check.cpp declares it
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

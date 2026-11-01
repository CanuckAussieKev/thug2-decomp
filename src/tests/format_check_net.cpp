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

#include "engine/net/play_messages.h"
#include "engine/net/skater_messages.h"
#include "engine/util/byte_stream.h"

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
struct ReferencePlayAnim {
    uint8_t loop_type, object;
    uint32_t anim;
    int16_t times[4];
};
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
struct ReferenceTrickObjects {
    uint8_t one, unknown01[9], game_id, pad0b;
    int32_t player;
    uint32_t score, count, objects[3];
};
struct ReferenceObservedTrickObjects {
    uint8_t game_id, pad[3];
    int32_t player;
    uint32_t count, objects[3];
};
struct ReferenceTagStolen {
    uint8_t game_id, pad[3];
    int32_t thief, victim;
};
struct ReferencePoints {
    uint8_t game_id, pad[3];
    int32_t points;
};
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
struct ReferenceBoardVehicle {
    uint8_t skater, pad[3];
    uint32_t control_type;
};
struct ReferenceProjectile {
    uint32_t zero;
    float pos[4], vel[4];
    uint32_t type, ammo_type;
    int32_t radius;
    float scale;
    uint8_t id, pad[3];
};
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

} // namespace

int run()
{
    std::printf("game messages (batch 9 on)\n");
    check_skater_messages();
    check_graffiti_messages();
    check_play_messages();
    return g_failures;
}

} // namespace format_check_net

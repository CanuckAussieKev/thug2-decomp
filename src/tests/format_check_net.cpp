// NOT ORIGINAL - thug2_format_check's checks of the game messages converted from stage 5's batch 9 on: each message
// as the new writer encodes it compared byte for byte with the struct / byte array the original's sender built
// (padding zeroed), decoded and encoded again (identical), and cut short (the reader fails).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

} // namespace

int run()
{
    std::printf("game messages (batch 9 on)\n");
    check_skater_messages();
    return g_failures;
}

} // namespace format_check_net

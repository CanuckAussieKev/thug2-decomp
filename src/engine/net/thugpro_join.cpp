// NOT ORIGINAL - the THUGPRO peer mode's join handshake. See thugpro_join.h.

#include "engine/net/thugpro_join.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <array>

#include "engine/net/thugpro_peer.h"
#include "engine/script/param_struct.h"
#include "formats/byte_stream.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "engine/core/checked_index.h"
#include "wire/gamenet_messages.h"

namespace online::thugpro {

namespace {
constexpr int kHexBase = 16;

uint32_t& hash_override()
{
    static uint32_t hash_override{0};
    return hash_override;
}

uint32_t read_scripts_hash()
{
    constexpr size_t kTextSize = 32;
    std::array<char, kTextSize> text{};
    build_info::fetch_text("online", "thugpro_scripts_hash", "", text.data(), sizeof(text));
    auto hash = static_cast<uint32_t>(std::strtoul(text.data(), nullptr, kHexBase));
    return hash ? hash : kReleaseScriptsHash;
}

// THUGPRO's refusal texts: its names (their checksums are what its own client sets), our words.
struct ReasonText {
    uint32_t name;
    const char* name_text_;
    const char* words;
};
constexpr auto kReasons = std::to_array<ReasonText>({
    {.name = 0xD9658C73, .name_text_ = "tp_net_reason_version_low", .words = "The host runs a newer THUGPRO than this game speaks."},
    {.name = 0x3CE70459, .name_text_ = "tp_net_reason_modifications", .words = "The host doesn't allow modified scripts (THUGPRO's mods setting)."},
    {.name = 0xCAAD4ABB, .name_text_ = "tp_net_reason_release_type", .words = "The host doesn't accept this kind of release."},
    {.name = 0xD039C57D, .name_text_ = "tp_net_reason_player_name_dupe", .words = "A player with your name is already in this game."},
    {.name = 0xD32BAEE3, .name_text_ = "tp_net_reason_player_name_tooshort", .words = "Your player name is too short for this host."},
    {.name = 0xB82CAE9B, .name_text_ = "tp_net_reason_banned", .words = "You have been banned from this server."},
    {.name = 0x1F8DF1D2, .name_text_ = "tp_net_reason_test", .words = "Connection refused (THUGPRO's test refusal)."},
});

} // namespace

uint32_t scripts_hash()
{
    if (hash_override())
        return hash_override();
    static const uint32_t kScriptsHash = read_scripts_hash();
    return kScriptsHash;
}

void set_scripts_hash_override(uint32_t hash) { hash_override() = hash; }

uint16_t make_connect_request(const ConnectInfo& thug2, ConnectRequest* out)
{
    ConnectTrailer trailer = {};
    std::memcpy(trailer.magic.data(), kMagic.data(), sizeof(kMagic));
    trailer.version = kProtocolVersion;
    trailer.release = kReleaseThug2Decomp;
    trailer.scripts_hash_ = scripts_hash();
    trailer.machine_id_ = get_machine_id();
    // (THUGPRO's host checks this flag, not the hash: the two say the same - see the register, TP-R12)
    trailer.modified_scripts_ = trailer.scripts_hash_ != kReleaseScriptsHash;
    util::ByteWriter writer(out->bytes.data(), sizeof(out->bytes));
    write_connect_info(writer, thug2);
    write_connect_trailer(writer, trailer);
    return static_cast<uint16_t>(writer.position());
}

bool read_connect_trailer(const void* message, uint32_t length, ConnectTrailer* out)
{
    if (!message || length < kConnectRequestSize)
        return false;
    util::ByteReader reader(message, length);
    reader.seek(kConnectInfoSize);
    ConnectTrailer trailer{};
    if (!read_connect_trailer(reader, &trailer) || std::memcmp(trailer.magic.data(), kMagic.data(), sizeof(kMagic)) != 0)
        return false;
    *out = trailer;
    return true;
}

const char* refused_reason_words(int reason)
{
    if (reason < kRefusedVersionLow || reason > kRefusedTest)
        return nullptr;
    return core::at(kReasons, reason - kRefusedVersionLow).words;
}

uint32_t refused_reason_text(int reason)
{
    if (reason < kRefusedVersionLow || reason > kRefusedTest)
        return 0;
    const ReasonText& text = core::at(kReasons, reason - kRefusedVersionLow);
    if (!script::find_symbol(text.name)) {
        script::SymbolTableEntry* symbol = script::create_symbol(text.name);
        symbol->set_type(script::QbValueKind::kString);
        symbol->value().set_text(script::spawn_text(text.words));
    }
    get_log("THUGPRO join refused (TP-R13): reason %d - %s", reason, text.words);
    return text.name;
}

} // namespace online::thugpro

// NOT ORIGINAL - the THUGPRO peer mode's join handshake. See thugpro_join.h.

#include "engine/net/thugpro_join.h"

#include <cstdlib>
#include <cstring>
#include <array>

#include "engine/core/checksums.h"
#include "engine/net/thugpro_peer.h"
#include "engine/script/param_struct.h"
#include "formats/byte_stream.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/checked_index.h"

namespace online::thugpro {

namespace {

uint32_t g_hash_override = 0;

uint32_t read_scripts_hash()
{
    std::array<char, 32> text{};
    build_info::fetch_text("online", "thugpro_scripts_hash", "", text.data(), sizeof(text));
    auto hash = static_cast<uint32_t>(std::strtoul(text.data(), nullptr, 16));
    return hash ? hash : kReleaseScriptsHash;
}

// THUGPRO's refusal texts: its names (their checksums are what its own client sets), our words.
struct ReasonText {
    uint32_t name;
    const char* name_text_;
    const char* words;
};
constexpr auto kReasons = std::to_array<ReasonText>({
    {0xD9658C73, "tp_net_reason_version_low", "The host runs a newer THUGPRO than this game speaks."},
    {0x3CE70459, "tp_net_reason_modifications", "The host doesn't allow modified scripts (THUGPRO's mods setting)."},
    {0xCAAD4ABB, "tp_net_reason_release_type", "The host doesn't accept this kind of release."},
    {0xD039C57D, "tp_net_reason_player_name_dupe", "A player with your name is already in this game."},
    {0xD32BAEE3, "tp_net_reason_player_name_tooshort", "Your player name is too short for this host."},
    {0xB82CAE9B, "tp_net_reason_banned", "You have been banned from this server."},
    {0x1F8DF1D2, "tp_net_reason_test", "Connection refused (THUGPRO's test refusal)."},
});

} // namespace

uint32_t scripts_hash()
{
    if (g_hash_override)
        return g_hash_override;
    static const uint32_t kScriptsHash = read_scripts_hash();
    return kScriptsHash;
}

void set_scripts_hash_override(uint32_t hash) { g_hash_override = hash; }

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
        symbol->type = script::kQbvalString;
        symbol->value.set_text(script::spawn_text(text.words));
    }
    get_log("THUGPRO join refused (TP-R13): reason %d - %s", reason, text.words);
    return text.name;
}

} // namespace online::thugpro

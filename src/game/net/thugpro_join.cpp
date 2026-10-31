// NOT ORIGINAL - the THUGPRO peer mode's join handshake. See thugpro_join.h.

#include "game/net/thugpro_join.h"

#include <cstdlib>
#include <cstring>

#include "game/core/crc.h"
#include "game/net/thugpro_peer.h"
#include "game/script/struct.h"
#include "game/sys/config.h"
#include "game/sys/log.h"

namespace GameNet::Thugpro {

namespace {

uint32_t g_hashOverride = 0;

uint32_t ReadScriptsHash()
{
    char text[32];
    Config::GetString("online", "thugpro_scripts_hash", "", text, sizeof(text));
    uint32_t hash = static_cast<uint32_t>(std::strtoul(text, nullptr, 16));
    return hash ? hash : kReleaseScriptsHash;
}

// THUGPRO's refusal texts: its names (their checksums are what its own client sets), our words.
struct ReasonText {
    uint32_t name;
    const char* nameText;
    const char* words;
};
constexpr ReasonText kReasons[] = {
    {0xD9658C73, "tp_net_reason_version_low", "The host runs a newer THUGPRO than this game speaks."},
    {0x3CE70459, "tp_net_reason_modifications", "The host doesn't allow modified scripts (THUGPRO's mods setting)."},
    {0xCAAD4ABB, "tp_net_reason_release_type", "The host doesn't accept this kind of release."},
    {0xD039C57D, "tp_net_reason_player_name_dupe", "A player with your name is already in this game."},
    {0xD32BAEE3, "tp_net_reason_player_name_tooshort", "Your player name is too short for this host."},
    {0xB82CAE9B, "tp_net_reason_banned", "You have been banned from this server."},
    {0x1F8DF1D2, "tp_net_reason_test", "Connection refused (THUGPRO's test refusal)."},
};

} // namespace

uint32_t ScriptsHash()
{
    if (g_hashOverride)
        return g_hashOverride;
    static uint32_t hash = ReadScriptsHash();
    return hash;
}

void SetScriptsHashOverride(uint32_t hash) { g_hashOverride = hash; }

uint16_t MakeConnectRequest(const ConnectInfo& thug2, ConnectRequest* out)
{
    std::memset(out, 0, sizeof(*out));
    out->thug2 = thug2;
    std::memcpy(out->thugpro.magic, kMagic, sizeof(kMagic));
    out->thugpro.version = kProtocolVersion;
    out->thugpro.release = kReleaseThug2Decomp;
    out->thugpro.scriptsHash = ScriptsHash();
    out->thugpro.machineId = MachineId();
    // (THUGPRO's host checks this flag, not the hash: the two say the same - see the register, TP-R12)
    out->thugpro.modifiedScripts = out->thugpro.scriptsHash != kReleaseScriptsHash;
    return static_cast<uint16_t>(sizeof(ConnectRequest));
}

bool ReadConnectTrailer(const void* message, uint32_t length, ConnectTrailer* out)
{
    if (!message || length < sizeof(ConnectRequest))
        return false;
    const auto* request = static_cast<const ConnectRequest*>(message);
    if (std::memcmp(request->thugpro.magic, kMagic, sizeof(kMagic)) != 0)
        return false;
    *out = request->thugpro;
    return true;
}

const char* RefusedReasonWords(int reason)
{
    if (reason < kRefusedVersionLow || reason > kRefusedTest)
        return nullptr;
    return kReasons[reason - kRefusedVersionLow].words;
}

uint32_t RefusedReasonText(int reason)
{
    if (reason < kRefusedVersionLow || reason > kRefusedTest)
        return 0;
    const ReasonText& text = kReasons[reason - kRefusedVersionLow];
    if (!Script::FindSymbol(text.name)) {
        Script::CSymbolTableEntry* symbol = Script::CreateSymbol(text.name);
        symbol->type = Script::ESYMBOLTYPE_STRING;
        symbol->raw = reinterpret_cast<uint32_t>(Script::CreateString(text.words));
    }
    Log("THUGPRO join refused (TP-R13): reason %d - %s", reason, text.words);
    return text.name;
}

} // namespace GameNet::Thugpro

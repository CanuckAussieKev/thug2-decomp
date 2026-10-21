#pragma once

#include <cstddef>
#include <cstdint>

#include "game/net/gamenet_manager.h"

// NOT ORIGINAL - the join handshake of the THUGPRO peer mode (thugpro_peer.h: PeerMode()), as THUGPRO's release
// (thugpro.dll, its protocol version 0.7) speaks it. Register: tools/compat/thugpro/README.md, TP-R11 ... TP-R13.
//
// What THUGPRO's release changes in THUG2's join (read from the installed thugpro.dll, with the insight of Morten1337's
// thugpro_proxy - used with his permission, written here in this project's terms):
//   - its client sends THUG2's connection request (message 3, version 0x0004000B) with 32 bytes after it: the text
//     "_THUG_PRO_", its protocol version, its release type, its scripts' hash, its machine id and a "modified scripts"
//     flag - 124 bytes, LZHAM-compressed (method 3);
//   - its host checks those (before THUG2's own checks) for a remote client sending THUG2's version: no "_THUG_PRO_"
//     -> refused (reason 6); a version under 0.7 -> 31; "modified scripts" while the host doesn't allow mods -> 32; a
//     banned machine id -> 36. A release type of 2 (its developers') skips the last three;
//   - its join request (message 80) is THUG2's, with the machine id in place of the GameSpy profile id;
//   - its hosts skip THUG2's CD-key check (its players' keys are made up);
//   - its client shows reasons 31-37 in its own words.
// (The source snapshot of thugpro_proxy has a different, 48-byte request - an unreleased version: a release host
// takes THUG2's version from its first 4 bytes and refuses anything else, so this mode sends what the release does.)
namespace GameNet::Thugpro {

// The release's scripts hash: the CRC-32 of THUGPRO's data\pre\thugpro_qb.prx (THUGPRO's own toolchain computes it with
// calculate_qb_prx_hash.bat). The user's decision (2026-10-21): our client sends THUGPRO's release hash, hard-coded,
// updated when THUGPRO releases new scripts. To update it: the CRC-32 (zlib's, as `crc32` tools print it) of the new
// %LOCALAPPDATA%\THUG Pro\data\pre\thugpro_qb.prx -
//   node tools/compat/thugpro/scripts_hash.ts
// (or 7-Zip's "CRC-32" of the file), written here, or set in the ini without a rebuild: [online] thugpro_scripts_hash=<hex>.
// TestThugproJoinLayouts checks this value against the user's install when it's there.
constexpr uint32_t kReleaseScriptsHash = 0x09FC08B0; // THUGPRO's release scripts (thugpro_qb.prx of 2026-10-17)

// The hash we send: [online] thugpro_scripts_hash=<hex> if set, else kReleaseScriptsHash.
uint32_t ScriptsHash();
void SetScriptsHashOverride(uint32_t hash); // (tests; 0: the settings again)

// THUGPRO's connection request trailer (after THUG2's 92 bytes).
constexpr char kMagic[10] = {'_', 'T', 'H', 'U', 'G', '_', 'P', 'R', 'O', '_'};
constexpr uint32_t kProtocolVersion = 0x00070000;  // THUGPRO 0.7's (its host: byte-swapped, at least 0x700)
// The release type field: THUGPRO's are 0 (beta, what its release sends), 1 (public), 2 (its developers': checks
// skipped). Ours says what it is - not THUGPRO at all - with a value of its own (THUGPRO's host only looks for 2).
constexpr uint32_t kReleaseThug2Decomp = 0x32475554; // "TUG2" in memory
struct ConnectTrailer {
    char magic[10];          // +0x5C "_THUG_PRO_"
    uint8_t pad66[2];
    uint32_t version;        // +0x68
    uint32_t release;        // +0x6C
    uint32_t scriptsHash;    // +0x70
    uint32_t machineId;      // +0x74
    bool modifiedScripts;    // +0x78
    uint8_t pad79[3];
};
struct ConnectRequest {
    ConnectInfo thug2;
    ConnectTrailer thugpro;
};
static_assert(sizeof(ConnectRequest) == 124 && offsetof(ConnectRequest, thugpro.version) == 0x68 &&
              offsetof(ConnectRequest, thugpro.machineId) == 0x74 && offsetof(ConnectRequest, thugpro.modifiedScripts) == 0x78);

// THUG2's connection request with THUGPRO's trailer: the hash ScriptsHash(), "modified" when that isn't the release's,
// our machine id. Returns the length to send (124).
uint16_t MakeConnectRequest(const ConnectInfo& thug2, ConnectRequest* out);
// What a connection request from a THUGPRO client carries (for the host's log): false if it has no trailer.
bool ReadConnectTrailer(const void* message, uint32_t length, ConnectTrailer* out);

// THUGPRO's refusal reasons (its client's words: ours - the texts are global strings made when first needed, under
// THUGPRO's own names; CreateJoinRefusedDialog shows the one named by the manager's m_refusedReason).
enum : int8_t {
    kRefusedVersionLow = 31,
    kRefusedModifications = 32,
    kRefusedReleaseType = 33,
    kRefusedNameDupe = 34,
    kRefusedNameTooShort = 35,
    kRefusedBannedId = 36,
    kRefusedTest = 37,
};
// The text's name for THUGPRO reason `reason` (31-37; 0 for any other), the text made if it isn't yet.
uint32_t RefusedReasonText(int reason);
// The words of reason `reason` (31-37; null for any other).
const char* RefusedReasonWords(int reason);

} // namespace GameNet::Thugpro

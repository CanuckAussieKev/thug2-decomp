#pragma once

#include <cstddef>
#include <cstdint>

// NOT ORIGINAL - the THUGPRO peer mode: playing network games with THUGPRO players (THUG2 PC plus Morten1337's
// thugpro.dll and scripts). THUGPRO keeps THUG2's protocol and adds to it (docs/thugpro-online.md section 3); in
// this mode our side speaks those additions:
//   - chat (message 76) carries a 4-byte machine id in front of THUG2's layout (chat.cpp);
//   - the host's options: server info (155: time of day, weather, 8 level options, 8 pro-set flags - sent before
//     every start info), time of day (156) and weather (157) updates, level option (158: THUGPRO ignores it too),
//     mods allowed (159) - received and applied (the time of day with THUG2's own TOD manager, the level options by
//     the level's <level>_LEVEL_CHANGE_0n scripts, the pro sets by THUGPRO's node prefixes), and sent when we host;
//   - change level (70) has the level's name (32 bytes) after THUG2's 8 when we host.
// Approach and message layouts from Morten1337's thugpro_proxy (GameNet/GameMsg.h, Net/Application.cpp,
// GameNet/Manager.cpp, Modules/Skate/SkateNet.cpp) and its scripts (thugpro_server.q, tod_manager.q) - used with his
// permission, rewritten here. Register: tools/compat/thugpro/README.md, "Run time" (TP-R2 ... TP-R5).
//
// On when thug2-decomp.ini says [Miscellaneous] OnlineGameName=thugpro (THUGPRO's lobbies) or [online]
// thugpro_peer=1. Off: THUG2's own protocol, unchanged.
namespace Net {
struct App;
struct MsgDesc;
struct MsgHandlerContext;
} // namespace Net

namespace GameNet {
class Manager;
}

namespace GameNet::Thugpro {

// The mode (the settings, read once). Tests set it with SetPeerModeOverride (1 on, 0 off, -1 the settings again).
bool PeerMode();
void SetPeerModeOverride(int mode);

// The 4-byte id THUGPRO puts in front of chat (its mute list keys on it): [online] thugpro_machine_id=<hex> if set,
// else a random one made once per run (nothing identifying the machine).
uint32_t MachineId();

// --- Message ids and layouts (THUGPRO's GameMsg.h) ---

constexpr uint8_t kMsgChangeLevel = 70;          // 0x46
constexpr uint8_t kMsgStartInfo = 51;            // 0x33
constexpr uint8_t kMsgChat = 76;                 // 0x4C
constexpr uint8_t kMsgServerInfo = 155;          // S->C
constexpr uint8_t kMsgUpdateTimeOfDay = 156;     // S->C
constexpr uint8_t kMsgUpdateWeather = 157;       // S->C
constexpr uint8_t kMsgUpdateLevelOption = 158;   // S->C (no sender, a no-op handler in THUGPRO)
constexpr uint8_t kMsgModsAllowed = 159;         // S->C

constexpr int kNameSize = 16;      // vMAX_PLAYER_NAME_LEN + 1
constexpr int kChatTextSize = 128; // vMAX_CHAT_MSG_LEN + 1 (THUGPRO drops longer text)

// Chat: THUG2's layout (object id / colour, name, text) with the sender's machine id first. Sent with the text's
// length: 4 + 1 + 16 + strlen(text) + 1.
#pragma pack(push, 1)
struct ChatMessage {
    uint32_t machineId;
    uint8_t objId;   // the sender's skater's object id, 8 for an observer; shown as colour objId + 2
    char name[kNameSize];
    char text[kChatTextSize];
};
#pragma pack(pop)
static_assert(sizeof(ChatMessage) == 149, "THUGPRO's MsgChatTHUGPRO");
constexpr uint32_t kChatHeaderSize = 4 + 1 + kNameSize;

// A chat message for the wire: `text` cut to THUGPRO's 127 characters, `name` to 15. Returns the length to send.
uint32_t EncodeChat(ChatMessage* out, uint32_t machineId, uint8_t objId, const char* name, const char* text);
// A received chat message (of either layout: THUGPRO's when `thugpro`, else THUG2's - object id, name, text):
// false when it's too short, too long, or its name / text don't end inside it (THUGPRO drops those too).
struct ChatLine {
    uint32_t machineId; // 0 for THUG2's layout
    uint8_t objId;
    char name[kNameSize];
    char text[kChatTextSize + 4];
};
bool DecodeChat(const uint8_t* data, uint32_t length, bool thugpro, ChatLine* out);

// Server info (155): 16 bytes. Flags: bits 0-7 level options 1-8, bits 8-15 pro sets 1-7 and the competition's
// geometry (G_COMP); the rest unused.
struct ServerInfo {
    uint32_t timeOfDay;  // afternoon / night / morning / evening (THUG2's TOD states' checksums)
    uint32_t weather;    // "default" / clear / rain / snow
    uint64_t flags;
};
static_assert(sizeof(ServerInfo) == 16, "THUGPRO's MsgTHUGProServerInfo");
constexpr uint32_t kWeatherDefault = 0x1CA1FF20; // "default"
uint8_t LevelOptions(const ServerInfo& info);
uint8_t ProSets(const ServerInfo& info);
uint64_t MakeFlags(uint8_t levelOptions, uint8_t proSets);
bool DecodeServerInfo(const uint8_t* data, uint32_t length, ServerInfo* out);

// Change level (70): THUG2's 8 bytes (the level's load script, the show-warning byte, 3 bytes padding), then the
// level's name, 32 bytes (strncpy_s'd: cut, NUL-ended).
constexpr uint32_t kChangeLevelSize = 8;
constexpr uint32_t kChangeLevelNameSize = 32;
constexpr uint32_t kChangeLevelThugproSize = kChangeLevelSize + kChangeLevelNameSize;
// The name THUGPRO sends for `level`: its own words for the skate shop, the park editor and a created park
// (with a newline, as THUGPRO has them); else the level select's name (`listName`, null: "Unknown\n").
const char* ChangeLevelName(uint32_t level, const char* listName);
void ExtendChangeLevel(const uint8_t* message, const char* name, uint8_t out[kChangeLevelThugproSize]);

// --- What the messages change ---

// The time-of-day state is one of THUG2's four.
bool IsTimeOfDay(uint32_t state);

// What applying server info does, in order (also used by the tests on stand-in hooks): the time of day (THUG2's TOD
// manager, at once: TODManager_SetTODInstant), the weather (THUG2's rain / snow), each level option set (its
// <level>_LEVEL_CHANGE_0n script run with `on` - an option not set is left as the level's own setup made it, as
// THUGPRO only ever sets its flags), and in THPS4's levels and THUG1's New York the pro sets (THUGPRO's
// toggle_geo_nomenu for each: the set's nodes created, its NOT nodes killed - or the other way round).
struct Effects {
    virtual ~Effects() = default;
    virtual void SetTimeOfDay(uint32_t state) = 0;
    virtual void SetWeather(uint32_t weather) = 0;
    virtual bool RunScript(uint32_t script, bool on) = 0;            // false: no such script
    virtual void KillPrefix(const char* prefix) = 0;
    virtual void CreatePrefix(const char* prefix) = 0;
    virtual void KillName(uint32_t name) = 0;
};
// `levelPrefix`: the level structure's `level` ("lon", "z_dn"...); `levelLoad`: its load script's checksum (the pro
// sets only in THUGPRO's THPS4 / New York levels).
void ApplyServerInfo(const ServerInfo& info, const char* levelPrefix, uint32_t levelLoad, Effects& effects);
// One pro set (index 0-7: PROSET1..7, then G_COMP) `on` or off - THUGPRO's toggle_geo_nomenu.
void ApplyProSet(int index, bool on, uint32_t levelLoad, Effects& effects);
// Whether `levelLoad` is one of THUGPRO's THPS4 levels or New York (where its pro-set flags count).
bool ProSetLevel(uint32_t levelLoad);

// --- The game ---

// The client handlers (155-159), added by the skate module's AddClientHandlers in this mode (`skate`: its data).
void AddClientHandlers(Net::App* client, void* skate);
int __cdecl HandleServerInfo(Net::MsgHandlerContext* context);
int __cdecl HandleUpdateTimeOfDay(Net::MsgHandlerContext* context);
int __cdecl HandleUpdateWeather(Net::MsgHandlerContext* context);
int __cdecl HandleUpdateLevelOption(Net::MsgHandlerContext* context);
int __cdecl HandleModsAllowed(Net::MsgHandlerContext* context);

// Once a frame (the front end's input logic): received server info applied once the level is up (every load of the
// session, as THUGPRO keeps its flags); the session's state dropped when the network game ends.
void Update();

// For the tests: the session's received options (null before any), and whether the TOD manager's multiplayer lock
// is lifted now (this mode, a network game).
const ServerInfo* ReceivedServerInfo();
bool TimeOfDayUnlocked();
// For the tests: App::EnqueueMessage's filter (net_app.h) as installed.
bool FilterOutgoingMessage(Net::App* app, int handle, Net::MsgDesc* desc);

// What we send as host: server info from our TOD manager's state and the options set with the chat commands.
ServerInfo HostServerInfo();
// The chat commands of this mode (chat.cpp passes lines starting with '/'): /tod <state>, /weather <state>,
// /option <1-8> on|off, /proset <1-8> on|off, /thugpro (the state). True when `text` was one of them.
bool HandleChatCommand(const char* text);

} // namespace GameNet::Thugpro

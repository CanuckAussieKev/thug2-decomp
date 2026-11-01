#pragma once

#include <cstddef>
#include <cstdint>

#include "wire/gamenet_messages.h"

// NOT ORIGINAL - the THUGPRO peer mode: playing network games with THUGPRO players (THUG2 PC plus Morten1337's
// thugpro.dll and scripts). THUGPRO keeps THUG2's protocol and adds to it (docs/thugpro-online.md section 3); in
// this mode our side speaks those additions:
//   - chat (message 76) carries a 4-byte machine id in front of THUG2's layout (chat.cpp);
//   - the host's options: server info (155: time of day, weather, 8 level options, 8 pro-set flags - sent before
//     every start info), time of day (156) and weather (157) updates, level option (158: THUGPRO ignores it too),
//     mods allowed (159) - received and applied (the time of day with THUG2's own TOD manager, the level options by
//     the level's <level>_LEVEL_CHANGE_0n scripts, the pro sets by THUGPRO's node prefixes), and sent when we host;
//   - change level (70) has the level's name (32 bytes) after THUG2's 8 when we host.
// Approach and message layouts from Morten1337's thugpro_proxy (online/GameMsg.h, Net/Application.cpp,
// online/Manager.cpp, Modules/Skate/SkateNet.cpp) and its scripts (thugpro_server.q, tod_manager.q) - used with his
// permission, rewritten here. Register: thug2-thugpro-compat/thugpro/README.md, "Run time" (TP-R2 ... TP-R5).
//
// On when thug2-decomp.ini says [Miscellaneous] OnlineGameName=thugpro (THUGPRO's lobbies) or [online]
// thugpro_peer=1. Off: THUG2's own protocol, unchanged.
namespace net {
struct AppHost;
struct NetMessageInfo;
struct InboundMsgCtx;
} // namespace net

namespace online {
// NOLINTNEXTLINE(bugprone-forward-declaration-namespace): online::Hub, defined with the online code
class Hub;
}

namespace online::thugpro {

// The mode (the settings, read once). Tests set it with set_peer_mode_override (1 on, 0 off, -1 the settings again).
bool peer_mode();
void set_peer_mode_override(int mode);

// The 4-byte id THUGPRO puts in front of chat (its mute list keys on it): [online] thugpro_machine_id=<hex> if set,
// else a random one made once per run (nothing identifying the machine).
uint32_t get_machine_id();

// --- Message ids and layouts (THUGPRO's GameMsg.h) ---

constexpr uint8_t kMsgLevelSwitch = 70;        // 0x46
constexpr uint8_t kMsgLobbyDetails = 51;       // 0x33
constexpr uint8_t kMsgChat = 76;               // 0x4C
constexpr uint8_t kMsgServerInfo = 155;        // S->C
constexpr uint8_t kMsgUpdateTimeOfDay = 156;   // S->C
constexpr uint8_t kMsgUpdateWeather = 157;     // S->C
constexpr uint8_t kMsgUpdateLevelOption = 158; // S->C (no sender, a no-op handler in THUGPRO)
constexpr uint8_t kMsgModsAllowed = 159;       // S->C

constexpr int kNameSize = 16;      // vMAX_PLAYER_NAME_LEN + 1
constexpr int kChatTextSize = 128; // vMAX_CHAT_MSG_LEN + 1 (THUGPRO drops longer text)

// Chat: THUG2's layout (object id / colour, name, text) with the sender's machine id first (ChatMessage, its format
// in gamenet_messages.h). Sent with the text's length: 4 + 1 + 16 + strlen(text) + 1.
constexpr uint32_t kChatHeaderSize = static_cast<uint32_t>(kChatMessageHeaderSize);

// A chat message for the wire, into `out`: `text` cut to THUGPRO's 127 characters, `name` to 15. Returns the length
// to send.
uint32_t encode_chat(uint8_t out[kChatMessageSize], uint32_t machine_id, uint8_t obj_id, const char* name,
                     const char* text);
// A received chat message (of either layout: THUGPRO's when `thugpro`, else THUG2's - object id, name, text):
// false when it's too short, too long, or its name / text don't end inside it (THUGPRO drops those too).
struct ChatLine {
    uint32_t machine_id_; // 0 for THUG2's layout
    uint8_t obj_id_;
    char name[kNameSize];
    char text[kChatTextSize + 4];
};
bool decode_chat(const uint8_t* data, uint32_t length, bool thugpro, ChatLine* out);

// Server info (155, 16 bytes: ServerInfo in gamenet_messages.h): the time of day (afternoon / night / morning / evening:
// THUG2's TOD states' checksums), the weather ("default" / clear / rain / snow), the flags - bits 0-7 level options
// 1-8, bits 8-15 pro sets 1-7 and the competition's geometry (G_COMP); the rest unused.
using HostListing = ServerInfo;
constexpr uint32_t kWeatherDefault = 0x1CA1FF20; // "default"
uint8_t get_level_options(const HostListing& info);
uint8_t get_pro_sets(const HostListing& info);
uint64_t make_flags(uint8_t level_options, uint8_t pro_sets);
bool decode_server_info(const uint8_t* data, uint32_t length, HostListing* out);

// Change level (70): THUG2's 8 bytes (the level's load script, the show-warning byte, 3 bytes padding), then the
// level's name, 32 bytes (strncpy_s'd: cut, NUL-ended).
constexpr uint32_t kChangeLevelSize = static_cast<uint32_t>(kLevelSwitchSize);
// The name THUGPRO sends for `level`: its own words for the skate shop, the park editor and a created park
// (with a newline, as THUGPRO has them); else the level select's name (`list_name`, null: "Unknown\n").
const char* change_level_name(uint32_t level, const char* list_name);
void extend_change_level(const LevelSwitch& level, const char* name, uint8_t out[kChangeLevelThugproSize]);

// --- What the messages change ---

// The time-of-day state is one of THUG2's four.
bool is_time_of_day(uint32_t state);

// What applying server info does, in order (also used by the tests on stand-in hooks): the time of day (THUG2's TOD
// manager, at once: TODManager_SetTODInstant), the weather (THUG2's rain / snow), each level option set (its
// <level>_LEVEL_CHANGE_0n script run with `on` - an option not set is left as the level's own setup made it, as
// THUGPRO only ever sets its flags), and in THPS4's levels and THUG1's New York the pro sets (THUGPRO's
// toggle_geo_nomenu for each: the set's nodes created, its NOT nodes killed - or the other way round).
struct Effects {
    virtual ~Effects() = default;
    virtual void set_time_of_day(uint32_t state) = 0;
    virtual void set_weather(uint32_t weather) = 0;
    virtual bool launch_script(uint32_t script, bool on) = 0; // false: no such script
    virtual void kill_prefix(const char* prefix) = 0;
    virtual void create_prefix(const char* prefix) = 0;
    virtual void kill_name(uint32_t name) = 0;
};
// `level_prefix`: the level structure's `level` ("lon", "z_dn"...); `level_load`: its load script's checksum (the pro
// sets only in THUGPRO's THPS4 / New York levels).
void apply_server_info(const HostListing& info, const char* level_prefix, uint32_t level_load, Effects& effects);
// One pro set (index 0-7: PROSET1..7, then G_COMP) `on` or off - THUGPRO's toggle_geo_nomenu.
void apply_pro_set(int index, bool on, uint32_t level_load, Effects& effects);
// Whether `level_load` is one of THUGPRO's THPS4 levels or New York (where its pro-set flags count).
bool pro_set_level(uint32_t level_load);

// --- The game ---

// The client handlers (155-159), added by the skate module's add_client_handlers in this mode (`skate`: its data).
void add_client_handlers(net::AppHost* client, void* skate);
int handle_server_info(net::InboundMsgCtx* context);
int handle_update_time_of_day(net::InboundMsgCtx* context);
int handle_update_weather(net::InboundMsgCtx* context);
int handle_update_level_option(net::InboundMsgCtx* context);
int handle_mods_allowed(net::InboundMsgCtx* context);

// Once a frame (the front end's input logic): received server info applied once the level is up (every load of the
// session, as THUGPRO keeps its flags); the session's state dropped when the network game ends.
void tick_frame();

// For the tests: the session's received options (null before any), and whether the TOD manager's multiplayer lock
// is lifted now (this mode, a network game).
const HostListing* received_server_info();
bool time_of_day_unlocked();
// For the tests: AppHost::put_on_outbox's filter (net_app.h) as installed.
bool filter_outgoing_message(net::AppHost* app, int handle, net::NetMessageInfo* desc);

// What we send as host: server info from our TOD manager's state and the options set with the chat commands.
HostListing host_server_info();
// The chat commands of this mode (chat.cpp passes lines starting with '/'): /tod <state>, /weather <state>,
// /option <1-8> on|off, /proset <1-8> on|off, /thugpro (the state). True when `text` was one of them.
bool handle_chat_command(const char* text);

} // namespace online::thugpro

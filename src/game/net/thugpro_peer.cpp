// NOT ORIGINAL - the THUGPRO peer mode (see thugpro_peer.h). Approach and layouts from Morten1337's thugpro_proxy and
// thugpro scripts (used with his permission; rewritten in this project's terms, nothing copied).

#include "game/net/thugpro_peer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include "game/core/crc.h"
#include "game/gfx/screen.h"
#include "game/gfx/tod_manager.h"
#include "game/net/chat.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/net/online_identity.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/skate_net.h"
#include "game/sys/config.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/log.h"
#include "game/gfx/engine_glue.h"
#include "game/skate/skate_module.h"
#include "hook/game_code.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

namespace Script {
class CStruct;
class CScript;
} // namespace Script
namespace CFuncs {
bool __cdecl CFunc_Create(Script::CStruct* params, Script::CScript* script); // 0x005A4FF0 (node_object_cfuncs.cpp)
bool __cdecl CFunc_Kill(Script::CStruct* params, Script::CScript* script);   // 0x005A5030
} // namespace CFuncs

namespace GameNet::Thugpro {

namespace {

// Names.
constexpr uint32_t kOn = 0xF649D637;            // on
constexpr uint32_t kOff = 0xD443A2BC;           // off
constexpr uint32_t kLevel = 0x651533EC;         // level
constexpr uint32_t kPrefix = 0x6C4E7971;        // prefix
constexpr uint32_t kName = 0xA1DC81F9;          // name
constexpr uint32_t kNullScript = 0x0F7E902C;    // NullScript (checked at run time: none, no pro sets)
constexpr uint32_t kSkater = 0x5B8AB877;        // Skater (the local skater)
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;  // load_mainmenu (the skate shop)
constexpr uint32_t kLoadSk5ed = 0xE8B4B836;     // load_sk5ed (the park editor)
constexpr uint32_t kLoadSk5edGameplay = 0xB664035D; // load_sk5ed_gameplay (a created park)
constexpr uint32_t kWeatherRain = 0x48AD7552;   // rain
constexpr uint32_t kWeatherSnow = 0xC97C5A4C;   // snow (and clear 0x1A4E0EF9 - or anything else: none)

constexpr uint8_t kGroupPlayerMessages = 8;     // the sequence group of chat and THUGPRO's messages
constexpr int kHandleBroadcast = 0xFF;
constexpr int kSettleFrames = 30;               // frames with the level up before the options are applied
constexpr uint32_t kLoadGapMs = 400;            // a frame this long: a level (re)load ran in it

int g_override = -1;

bool ReadPeerMode()
{
    if (_stricmp(Net::GameName(), "thugpro") == 0)
        return true;
    return Config::GetBool("online", "thugpro_peer", false);
}

uint32_t ReadMachineId()
{
    char text[32];
    Config::GetString("online", "thugpro_machine_id", "", text, sizeof(text));
    uint32_t id = static_cast<uint32_t>(std::strtoul(text, nullptr, 16));
    std::random_device random;
    while (id == 0)
        id = static_cast<uint32_t>(random());
    return id;
}

// A string into a field: cut to fit, NUL-ended; the rest of the field left as it is (strncpy_s would fill it
// with 0xFE in a debug build - on the wire).
void CopyField(char* out, size_t size, const char* in)
{
    size_t length = in ? strnlen(in, size - 1) : 0;
    if (length)
        std::memcpy(out, in, length);
    out[length] = '\0';
}

// --- THUGPRO's level lists (register TP-R4) ---

// THUGPRO's THPS4 levels (its Levels.q `Game = THPS4`) and THUG1's New York: where its pro-set flags count
// (gameflow: LEVEL_ORIGINAL_GAME = THPS4 or LevelIs Load_NY).
constexpr const char* kProSetLevels[] = {
    "Load_practice", "Load_sch", "Load_alc", "Load_sf2", "Load_kon", "Load_lon",
    "Load_jnk",      "Load_zoo", "Load_cnv", "Load_hof", "Load_NY",
};

// THUGPRO's toggle_*_params: the pro sets' node prefixes (PROSET1..7, then the competition's geometry).
struct ProSetPrefixes {
    const char* geo;
    const char* trg;
    const char* trg2; // (G_COMP only)
    const char* geoNot;
    const char* trgNot;
};
constexpr ProSetPrefixes kProSets[8] = {
    {"Proset1", "TRG_Proset1", nullptr, "PROSET1NOT", "TRG_PROSET1NOT"},
    {"Proset2", "TRG_Proset2", nullptr, "PROSET2NOT", "TRG_PROSET2NOT"},
    {"Proset3", "TRG_Proset3", nullptr, "PROSET3NOT", "TRG_PROSET3NOT"},
    {"Proset4", "TRG_Proset4", nullptr, "PROSET4NOT", "TRG_PROSET4NOT"},
    {"Proset5", "TRG_Proset5", nullptr, "PROSET5NOT", "TRG_PROSET5NOT"},
    {"Proset6", "TRG_Proset6", nullptr, "PROSET6NOT", "TRG_PROSET6NOT"},
    {"Proset7", "TRG_Proset7", nullptr, "PROSET7NOT", "TRG_PROSET7NOT"},
    {"G_COMP", "TRG_G_COMP", "TRG_COMP", "G_COMPNOT", "TRG_G_COMPNOT"},
};

} // namespace

bool PeerMode()
{
    if (g_override >= 0)
        return g_override != 0;
    static bool mode = ReadPeerMode();
    return mode;
}

void SetPeerModeOverride(int mode) { g_override = mode; }

uint32_t MachineId()
{
    static uint32_t id = ReadMachineId();
    return id;
}

// --- Layouts ---

uint32_t EncodeChat(ChatMessage* out, uint32_t machineId, uint8_t objId, const char* name, const char* text)
{
    std::memset(out, 0, sizeof(*out));
    out->machineId = machineId;
    out->objId = objId;
    CopyField(out->name, sizeof(out->name), name);
    CopyField(out->text, sizeof(out->text), text);
    return kChatHeaderSize + static_cast<uint32_t>(std::strlen(out->text)) + 1;
}

bool DecodeChat(const uint8_t* data, uint32_t length, bool thugpro, ChatLine* out)
{
    // THUGPRO: id, object, name, text - at most its struct (so text <= 127). THUG2: object, name, text - at most our
    // 0x94-byte packet (text <= 130). The text ends inside the message (so the message is longer than its header).
    const uint32_t header = thugpro ? kChatHeaderSize : 1 + kNameSize;
    const uint32_t largest = thugpro ? static_cast<uint32_t>(sizeof(ChatMessage)) : 0x94;
    if (!data || length <= header || length > largest)
        return false;
    const uint8_t* fields = data + (thugpro ? 4 : 0);
    const char* name = reinterpret_cast<const char*>(fields + 1);
    const char* text = reinterpret_cast<const char*>(data + header);
    size_t nameLength = strnlen(name, kNameSize);
    size_t textLength = strnlen(text, length - header);
    if (nameLength >= static_cast<size_t>(kNameSize) || textLength >= length - header)
        return false;
    std::memset(out, 0, sizeof(*out));
    if (thugpro)
        std::memcpy(&out->machineId, data, 4);
    out->objId = fields[0];
    std::memcpy(out->name, name, nameLength);
    std::memcpy(out->text, text, textLength);
    return true;
}

uint8_t LevelOptions(const ServerInfo& info) { return static_cast<uint8_t>(info.flags & 0xFF); }
uint8_t ProSets(const ServerInfo& info) { return static_cast<uint8_t>((info.flags >> 8) & 0xFF); }
uint64_t MakeFlags(uint8_t levelOptions, uint8_t proSets)
{
    return static_cast<uint64_t>(levelOptions) | (static_cast<uint64_t>(proSets) << 8);
}

bool DecodeServerInfo(const uint8_t* data, uint32_t length, ServerInfo* out)
{
    if (!data || length < sizeof(ServerInfo))
        return false;
    std::memcpy(out, data, sizeof(ServerInfo));
    return true;
}

const char* ChangeLevelName(uint32_t level, const char* listName)
{
    switch (level) {
    case kLoadMainMenu:
        return "Skateshop\n";
    case kLoadSk5ed:
        return "Park Editor\n";
    case kLoadSk5edGameplay:
        return "Custom Park\n";
    default:
        return listName ? listName : "Unknown\n";
    }
}

void ExtendChangeLevel(const uint8_t* message, const char* name, uint8_t out[kChangeLevelThugproSize])
{
    std::memset(out, 0, kChangeLevelThugproSize);
    std::memcpy(out, message, kChangeLevelSize);
    CopyField(reinterpret_cast<char*>(out + kChangeLevelSize), kChangeLevelNameSize, name);
}

// --- Effects ---

bool IsTimeOfDay(uint32_t state) { return Gfx::NextTODState(state) != 0; }

bool ProSetLevel(uint32_t levelLoad)
{
    for (const char* load : kProSetLevels)
        if (Crc::GenerateCRCFromString(load) == levelLoad)
            return true;
    return false;
}

void ApplyProSet(int index, bool on, uint32_t levelLoad, Effects& effects)
{
    // THUGPRO's toggle_geo_nomenu (its KillAllTextureSplats first left out: decals only).
    const ProSetPrefixes& p = kProSets[index];
    effects.KillPrefix(p.geo);
    effects.KillPrefix(p.trg);
    if (p.trg2)
        effects.KillPrefix(p.trg2);
    effects.KillPrefix(p.geoNot);
    effects.KillPrefix(p.trgNot);
    effects.CreatePrefix(p.geoNot);
    effects.CreatePrefix(p.trgNot);
    if (!on)
        return;
    effects.CreatePrefix(p.geo);
    effects.CreatePrefix(p.trg);
    if (p.trg2)
        effects.CreatePrefix(p.trg2);
    effects.KillPrefix(p.geoNot);
    effects.KillPrefix(p.trgNot);
    effects.KillPrefix("TRG_G_COMP_Spectator");
    if (levelLoad == Crc::GenerateCRCFromString("Load_sch"))
        effects.KillName(Crc::GenerateCRCFromString("G_COMP_Ramp04"));
}

void ApplyServerInfo(const ServerInfo& info, const char* levelPrefix, uint32_t levelLoad, Effects& effects)
{
    effects.SetTimeOfDay(info.timeOfDay);
    effects.SetWeather(info.weather);
    // The level options: THUGPRO only ever sets its SERVER_LEVEL_CHANGE_0n_ON flags from this message (the level's
    // own setup sets its default ones), then runs each toggle by its flag - so an option not set here stays as the
    // level's setup made it (our ports run the defaults in their setup scripts).
    uint8_t options = LevelOptions(info);
    if (levelPrefix && *levelPrefix) {
        for (int n = 1; n <= 8; ++n) {
            if (!(options & (1 << (n - 1))))
                continue;
            char name[96];
            std::snprintf(name, sizeof(name), "%s_LEVEL_CHANGE_0%d", levelPrefix, n);
            effects.RunScript(Crc::GenerateCRCFromString(name), true);
        }
    }
    // The pro sets: in THUGPRO's THPS4 levels and New York all eight toggled by their flags (THUGPRO's
    // HandleProtrickObjects at each start).
    if (ProSetLevel(levelLoad)) {
        uint8_t sets = ProSets(info);
        for (int i = 0; i < 8; ++i)
            ApplyProSet(i, (sets >> i) & 1, levelLoad, effects);
    }
}

namespace {

// The game's side of the effects.
class GameEffects final : public Effects {
public:
    void SetTimeOfDay(uint32_t state) override
    {
        if (!IsTimeOfDay(state))
            state = Gfx::kAfternoon; // (THUGPRO's thugpro_change_time_of_day: anything else is afternoon)
        Gfx::CTODManager* manager = Gfx::TODManager();
        if (!manager || !manager->CanSetTODInstant(state)) {
            Log("thugpro: time of day %08X not set (no time-of-day states in this level)", state);
            return;
        }
        if (manager->CurrentState() == state)
            return;
        manager->SetTODInstant(state);
    }
    void SetWeather(uint32_t weather) override
    {
        // THUGPRO's set_current_weather_values: THUG2's rain / snow (the weather object 0x006FA7C8: +0x10 the
        // rain's rate, +0x40 the snow's, +0x58 rain (1) or snow (0)). "default" leaves the level's own weather
        // (THUGPRO clears it: it has its own per-level weather settings - ours are THUG2's).
        if (weather == kWeatherDefault)
            return;
        uint8_t* object = reinterpret_cast<uint8_t*>(Nx::g_weather);
        if (!object)
            return;
        float& rainRate = *reinterpret_cast<float*>(object + 0x10);
        float& snowRate = *reinterpret_cast<float*>(object + 0x40);
        bool& rain = *reinterpret_cast<bool*>(object + 0x58);
        if (weather == kWeatherRain) {
            rain = true;
            rainRate = 16.0f;
            snowRate = 0.0f;
        } else if (weather == kWeatherSnow) {
            rain = false;
            snowRate = 5.0f;
            rainRate = 0.0f;
        } else { // clear, or anything else
            rainRate = 0.0f;
            snowRate = 0.0f;
        }
    }
    bool RunScript(uint32_t script, bool on) override
    {
        if (!Script::ScriptExists(script))
            return false;
        Script::CStruct* params = NewParams();
        params->AddChecksum(0u, on ? kOn : kOff);
        Script::RunScript(script, params, nullptr, false, nullptr);
        DeleteParams(params);
        return true;
    }
    void KillPrefix(const char* prefix) override { ObjectCommand(reinterpret_cast<uint32_t>(GAME_CODE(0x005A5030, CFuncs::CFunc_Kill)), kPrefix, prefix, 0); }
    void CreatePrefix(const char* prefix) override { ObjectCommand(reinterpret_cast<uint32_t>(GAME_CODE(0x005A4FF0, CFuncs::CFunc_Create)), kPrefix, prefix, 0); }
    void KillName(uint32_t name) override { ObjectCommand(reinterpret_cast<uint32_t>(GAME_CODE(0x005A5030, CFuncs::CFunc_Kill)), kName, nullptr, name); }

private:
    static Script::CStruct* NewParams()
    {
        Script::CStruct* params = Script::NewStruct();
        return params ? params->Init() : nullptr;
    }
    static void DeleteParams(Script::CStruct* params)
    {
        if (!params)
            return;
        params->Clear();
        Script::DeleteStruct(params);
    }
    // The script commands Kill (0x005A5030) and Create (0x005A4FF0) - prefix= or name= - run from a spawned
    // NullScript (Kill reads its script's object for some classes: it needs a real script).
    static void ObjectCommand(uint32_t command, uint32_t key, const char* text, uint32_t checksum)
    {
        if (!Script::ScriptExists(kNullScript)) {
            Log("thugpro: no NullScript - pro sets not changed");
            return;
        }
        Script::CScript* script = Script::SpawnScript(kNullScript, nullptr, 0, nullptr, -1, 0, false, false, false,
                                                      false);
        if (!script)
            return;
        Script::CStruct* params = NewParams();
        if (text)
            params->AddString(key, text);
        else
            params->AddChecksum(key, checksum);
        reinterpret_cast<bool(__cdecl*)(Script::CStruct*, Script::CScript*)>(command)(params, script);
        DeleteParams(params);
        Script::KillSpawnedScript(script);
    }
};

// The level structure being played: its `level` (the skate module's +0x380: the structure's name).
const char* CurrentLevelPrefix()
{
    uint8_t* skate = reinterpret_cast<uint8_t*>(Mdl::g_skateModule);
    if (!skate)
        return nullptr;
    Script::CStruct* structure = Script::GetStructure(*reinterpret_cast<uint32_t*>(skate + 0x380));
    const char* prefix = nullptr;
    if (!structure || !structure->GetString(kLevel, &prefix, 0))
        return nullptr;
    return prefix;
}

// The session's received options (a client of a THUGPRO host).
struct Session {
    bool have = false;      // server info (or an update) received
    bool applied = false;   // applied since the level's last load
    ServerInfo info = {Gfx::kAfternoon, kWeatherDefault, 0};
    uint32_t level = 0;     // the network level (load script) and the skater it was applied with
    void* skater = nullptr;
    int settle = 0;         // frames the level has been up
    uint32_t lastTime = 0;
};
Session g_session;

// The host's options (the chat commands): sent in server info.
struct HostOptions {
    uint32_t weather = kWeatherDefault;
    uint8_t levelOptions = 0;
    uint8_t proSets = 0;
};
HostOptions g_host;

Manager* NetManager() { return GetManager(); }

bool IsClientOfNetGame(Manager* manager) { return manager && manager->InNetGame() && !manager->OnServer(); }
bool IsHostOfNetGame(Manager* manager) { return manager && manager->InNetGame() && manager->OnServer(); }

bool LevelUp() { return g_session.settle >= kSettleFrames; }

void ApplySession()
{
    GameEffects effects;
    const char* prefix = CurrentLevelPrefix();
    const ServerInfo& info = g_session.info;
    Log("thugpro: host options applied in %s: time of day %08X, weather %08X, level options %02X, pro sets %02X",
        prefix ? prefix : "(no level structure)", info.timeOfDay, info.weather, LevelOptions(info), ProSets(info));
    ApplyServerInfo(info, prefix, NetManager()->m_level, effects);
    g_session.applied = true;
}

uint32_t Word(Net::MsgHandlerContext* context)
{
    uint32_t value = 0;
    if (context->m_MsgLength >= 4)
        std::memcpy(&value, context->m_Msg, 4);
    return value;
}

// Server info for `app`'s connection `handle`, before THUG2's start info.
void SendServerInfo(Net::App* app, int handle)
{
    ServerInfo info = HostServerInfo();
    Net::MsgDesc desc;
    desc.Construct();
    desc.m_id = kMsgServerInfo;
    desc.m_length = sizeof(info);
    desc.m_data = &info;
    desc.m_priority = 0xFF;
    desc.m_queue = Net::MsgDesc::kQueueImportant;
    desc.m_groupId = kGroupPlayerMessages;
    app->EnqueueMessage(handle, &desc);
}

// A 4-byte update to every player (the host's own client ignores it).
void Broadcast(uint8_t id, uint32_t value)
{
    Manager* manager = NetManager();
    if (!IsHostOfNetGame(manager) || !manager->m_server)
        return;
    Net::MsgDesc desc;
    desc.Construct();
    desc.m_id = id;
    desc.m_length = 4;
    desc.m_data = &value;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupPlayerMessages;
    manager->m_server->EnqueueMessage(kHandleBroadcast, &desc);
}

// NOT ORIGINAL - App::EnqueueMessage's filter (net_app.h): what THUGPRO's EnqueueMessage hook adds when we host.
bool FilterOutgoing(Net::App* app, int handle, Net::MsgDesc* desc)
{
    if (!PeerMode())
        return false;
    Manager* manager = NetManager();
    if (!IsHostOfNetGame(manager) || app != manager->m_server)
        return false;
    if (desc->m_id == kMsgStartInfo) {
        SendServerInfo(app, handle);
        return false;
    }
    if (desc->m_id == kMsgChangeLevel && desc->m_length == kChangeLevelSize && desc->m_data) {
        uint8_t message[kChangeLevelThugproSize];
        const auto* data = static_cast<const uint8_t*>(desc->m_data);
        uint32_t level;
        std::memcpy(&level, data, 4);
        ExtendChangeLevel(data, ChangeLevelName(level, Manager::LevelListName(level)), message);
        Net::MsgDesc extended = *desc;
        extended.m_data = message;
        extended.m_length = kChangeLevelThugproSize;
        app->EnqueueMessage(handle, &extended);
        return true;
    }
    return false;
}

// NOT ORIGINAL - the TOD manager's multiplayer unlock (tod_manager.h): THUGPRO network games.
bool UnlockTimeOfDay()
{
    if (!PeerMode())
        return false;
    Manager* manager = NetManager();
    return manager && manager->InNetGame();
}

struct Install {
    Install()
    {
        Net::SetEnqueueFilter(FilterOutgoing);
        Gfx::SetMultiplayerUnlock(UnlockTimeOfDay);
    }
} g_install;

} // namespace

const ServerInfo* ReceivedServerInfo() { return g_session.have ? &g_session.info : nullptr; }
bool TimeOfDayUnlocked() { return UnlockTimeOfDay(); }
bool FilterOutgoingMessage(Net::App* app, int handle, Net::MsgDesc* desc) { return FilterOutgoing(app, handle, desc); }

// --- The handlers ---

void AddClientHandlers(Net::App* client, void* skate)
{
    Net::Dispatcher& dispatcher = client->m_dispatcher;
    dispatcher.AddHandler(kMsgServerInfo, HandleServerInfo, 0, skate, 0x80);
    dispatcher.AddHandler(kMsgModsAllowed, HandleModsAllowed, 0, skate, 0x80);
    dispatcher.AddHandler(kMsgUpdateTimeOfDay, HandleUpdateTimeOfDay, Net::kPacketLate, skate, 0x80);
    dispatcher.AddHandler(kMsgUpdateWeather, HandleUpdateWeather, Net::kPacketLate, skate, 0x80);
    dispatcher.AddHandler(kMsgUpdateLevelOption, HandleUpdateLevelOption, Net::kPacketLate, skate, 0x80);
}

int __cdecl HandleServerInfo(Net::MsgHandlerContext* context)
{
    ServerInfo info;
    if (!PeerMode() || !IsClientOfNetGame(NetManager()) ||
        !DecodeServerInfo(reinterpret_cast<const uint8_t*>(context->m_Msg), context->m_MsgLength, &info))
        return 1;
    g_session.have = true;
    g_session.applied = false;
    g_session.info = info;
    Log("thugpro: server info: time of day %08X, weather %08X, level options %02X, pro sets %02X", info.timeOfDay,
        info.weather, LevelOptions(info), ProSets(info));
    return 1;
}

int __cdecl HandleUpdateTimeOfDay(Net::MsgHandlerContext* context)
{
    if (!PeerMode() || !IsClientOfNetGame(NetManager()) || context->m_MsgLength < 4)
        return 1;
    g_session.have = true;
    g_session.info.timeOfDay = Word(context);
    if (LevelUp()) {
        GameEffects effects;
        effects.SetTimeOfDay(g_session.info.timeOfDay);
    }
    return 1;
}

int __cdecl HandleUpdateWeather(Net::MsgHandlerContext* context)
{
    if (!PeerMode() || !IsClientOfNetGame(NetManager()) || context->m_MsgLength < 4)
        return 1;
    g_session.have = true;
    g_session.info.weather = Word(context);
    if (LevelUp()) {
        GameEffects effects;
        effects.SetWeather(g_session.info.weather);
    }
    return 1;
}

// (THUGPRO registers it and does nothing; no THUGPRO code sends it)
int __cdecl HandleUpdateLevelOption(Net::MsgHandlerContext* /*context*/) { return 1; }

int __cdecl HandleModsAllowed(Net::MsgHandlerContext* context)
{
    // THUGPRO's client leaves when the host allows no modified scripts and its own are modified. Ours aren't
    // THUGPRO's at all - so a host that allows none is left, as the server removing us (THUG2's dialog).
    if (!PeerMode() || !IsClientOfNetGame(NetManager()) || context->m_MsgLength < 4 || Word(context) == 1)
        return 1;
    Log("thugpro: the host allows no modified scripts (message 159) - leaving");
    Net::MsgHandlerContext removed = *context;
    removed.m_MsgId = 'Q';
    return Mdl::SkateNet::HandleServerRemovedYou(&removed);
}

void Update()
{
    Manager* manager = NetManager();
    if (!PeerMode() || !IsClientOfNetGame(manager)) {
        if (g_session.have)
            Log("thugpro: network game over - the host's options dropped");
        g_session = Session();
        return;
    }
    uint32_t now = Tmr::GetTime();
    bool loaded = g_session.lastTime != 0 && now - g_session.lastTime > kLoadGapMs;
    g_session.lastTime = now;
    void* skater = Obj::FindObject(kSkater);
    if (loaded || !skater || manager->m_level == kLoadMainMenu || manager->m_level != g_session.level ||
        skater != g_session.skater) {
        if (loaded || manager->m_level != g_session.level || skater != g_session.skater)
            g_session.applied = false;
        g_session.level = manager->m_level;
        g_session.skater = skater;
        g_session.settle = 0;
        if (!skater || manager->m_level == kLoadMainMenu)
            return;
    }
    if (g_session.settle < kSettleFrames) {
        ++g_session.settle;
        return;
    }
    if (g_session.have && !g_session.applied)
        ApplySession();
}

// --- Hosting ---

ServerInfo HostServerInfo()
{
    ServerInfo info = {Gfx::kAfternoon, g_host.weather, MakeFlags(g_host.levelOptions, g_host.proSets)};
    if (Gfx::CTODManager* manager = Gfx::TODManager()) {
        uint32_t state = manager->CurrentState();
        if (IsTimeOfDay(state))
            info.timeOfDay = state;
    }
    return info;
}

namespace {

// The time-of-day / weather names of the chat commands.
uint32_t StateNamed(const char* word, const char* const* names, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (_stricmp(word, names[i]) == 0)
            return Crc::GenerateCRCFromString(names[i]);
    return 0;
}

bool OnOff(const char* word, bool* on)
{
    if (_stricmp(word, "on") == 0)
        *on = true;
    else if (_stricmp(word, "off") == 0)
        *on = false;
    else
        return false;
    return true;
}

void Say(const char* line) { ShowLocalChatLine(line); }

} // namespace

bool HandleChatCommand(const char* text)
{
    if (!PeerMode() || text[0] != '/')
        return false;
    char command[16] = {}, first[24] = {}, second[8] = {};
    int words = sscanf_s(text, "/%15s %23s %7s", command, static_cast<unsigned>(sizeof(command)), first,
                         static_cast<unsigned>(sizeof(first)), second, static_cast<unsigned>(sizeof(second)));
    if (words < 1)
        return false;
    Manager* manager = NetManager();
    bool client = IsClientOfNetGame(manager);
    char line[160];
    if (_stricmp(command, "thugpro") == 0) {
        const ServerInfo& info = g_session.info;
        std::snprintf(line, sizeof(line),
                      "\\c4thugpro:\\c0 machine id %08X; %s; time of day %08X, weather %08X, options %02X, pro sets "
                      "%02X",
                      MachineId(), client ? (g_session.have ? "the host's" : "no host options yet") : "ours",
                      client ? info.timeOfDay : HostServerInfo().timeOfDay, client ? info.weather : g_host.weather,
                      client ? LevelOptions(info) : g_host.levelOptions, client ? ProSets(info) : g_host.proSets);
        Say(line);
        return true;
    }
    bool tod = _stricmp(command, "tod") == 0;
    bool weather = _stricmp(command, "weather") == 0;
    bool option = _stricmp(command, "option") == 0;
    bool proSet = _stricmp(command, "proset") == 0;
    if (!tod && !weather && !option && !proSet)
        return false;
    if (client) {
        Say("\\c4thugpro:\\c0 only the host sets the time of day, weather and options");
        return true;
    }
    GameEffects effects;
    if (tod || weather) {
        static const char* const kTimes[] = {"morning", "afternoon", "evening", "night"};
        static const char* const kWeathers[] = {"default", "clear", "rain", "snow"};
        uint32_t state = tod ? StateNamed(first, kTimes, 4) : StateNamed(first, kWeathers, 4);
        if (words < 2 || !state) {
            Say(tod ? "\\c4thugpro:\\c0 /tod morning|afternoon|evening|night"
                    : "\\c4thugpro:\\c0 /weather default|clear|rain|snow");
            return true;
        }
        if (tod) {
            effects.SetTimeOfDay(state);
            Broadcast(kMsgUpdateTimeOfDay, state);
        } else {
            g_host.weather = state;
            effects.SetWeather(state);
            Broadcast(kMsgUpdateWeather, state);
        }
        std::snprintf(line, sizeof(line), "\\c4thugpro:\\c0 %s %s%s", command, first,
                      IsHostOfNetGame(manager) ? " (sent to the players)" : "");
        Say(line);
        return true;
    }
    // /option n on|off, /proset n on|off: here now, to the players with the next game's server info (THUGPRO
    // changes them in a game through its own flag broadcast, which THUG2's scripts don't have).
    int n = std::atoi(first);
    bool on = false;
    if (words < 3 || n < 1 || n > 8 || !OnOff(second, &on)) {
        Say(option ? "\\c4thugpro:\\c0 /option <1-8> on|off" : "\\c4thugpro:\\c0 /proset <1-8> on|off (8: G_COMP)");
        return true;
    }
    uint8_t bit = static_cast<uint8_t>(1 << (n - 1));
    uint32_t load = manager ? manager->m_level : 0;
    if (option) {
        g_host.levelOptions = static_cast<uint8_t>(on ? g_host.levelOptions | bit : g_host.levelOptions & ~bit);
        const char* prefix = CurrentLevelPrefix();
        char name[96];
        std::snprintf(name, sizeof(name), "%s_LEVEL_CHANGE_0%d", prefix ? prefix : "", n);
        bool ran = prefix && effects.RunScript(Crc::GenerateCRCFromString(name), on);
        std::snprintf(line, sizeof(line), "\\c4thugpro:\\c0 level option %d %s%s", n, on ? "on" : "off",
                      ran ? "" : " (this level has no such option)");
    } else {
        g_host.proSets = static_cast<uint8_t>(on ? g_host.proSets | bit : g_host.proSets & ~bit);
        bool here = ProSetLevel(load);
        if (here)
            ApplyProSet(n - 1, on, load, effects);
        std::snprintf(line, sizeof(line), "\\c4thugpro:\\c0 pro set %d %s%s", n, on ? "on" : "off",
                      here ? "" : " (only THPS4's levels and New York have them)");
    }
    Say(line);
    return true;
}

} // namespace GameNet::Thugpro

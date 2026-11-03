// NOT ORIGINAL - the THUGPRO peer mode (see thugpro_peer.h). Approach and layouts from Morten1337's thugpro_proxy and
// thugpro scripts (used with his permission; rewritten in this project's terms, nothing copied).

#include "engine/net/thugpro_peer.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <array>
#include <span>

#include "engine/core/checksums.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/tod_manager.h"
#include "engine/net/chat.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/online_identity.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_net.h"
#include "platform/platform_config.h"
#include "platform/stopwatch.h"
#include "platform/run_log.h"
#include "engine/gfx/engine_glue.h"
#include "game/skate/skate_module.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include "engine/gfx/weather.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/parse_number.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/field_reader.h"
#include "engine/core/no_destroy.h"
#include "engine/net/net_game_state.h"
#include "wire/message_ids.h"

namespace script {
class Struct;
class Script;
} // namespace script
namespace commands {
bool cfunc_create(script::Struct* params, script::Script* script); // 0x005A4FF0 (node_object_cfuncs.cpp)
bool cfunc_kill(script::Struct* params, script::Script* script);   // 0x005A5030
} // namespace commands

namespace online::thugpro {

namespace {
constexpr int kHexBase = 16;
constexpr unsigned kFlagsByteMask = 0xFF;
constexpr unsigned kFlagsByteShift = 8;
constexpr int kLevelOptionCount = 8;
constexpr int kProSetCount = 8;
constexpr float kRainRate = 16.0f;
constexpr float kSnowRate = 5.0f;

// Names.
constexpr uint32_t kOn = 0xF649D637;                // on
constexpr uint32_t kOff = 0xD443A2BC;               // off
constexpr uint32_t kLevel = 0x651533EC;             // level
constexpr uint32_t kPrefix = 0x6C4E7971;            // prefix
constexpr uint32_t kName = 0xA1DC81F9;              // name
constexpr uint32_t kNullScript = 0x0F7E902C;        // NullScript (checked at run time: none, no pro sets)
constexpr uint32_t kSkater = 0x5B8AB877;            // Skater (the local skater)
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;      // load_mainmenu (the skate shop)
constexpr uint32_t kLoadSk5ed = 0xE8B4B836;         // load_sk5ed (the park editor)
constexpr uint32_t kLoadSk5edGameplay = 0xB664035D; // load_sk5ed_gameplay (a created park)
constexpr uint32_t kWeatherRain = 0x48AD7552;       // rain
constexpr uint32_t kWeatherSnow = 0xC97C5A4C;       // snow (and clear 0x1A4E0EF9 - or anything else: none)

constexpr int kHandleBroadcast = 0xFF;
constexpr int kSettleFrames = 30;    // frames with the level up before the options are applied
constexpr uint32_t kLoadGapMs = 400; // a frame this long: a level (re)load ran in it

int& override()
{
    static int override{-1};
    return override;
}

bool read_peer_mode()
{
    if (_stricmp(net::get_game_name(), "thugpro") == 0)
        return true;
    return build_info::fetch_flag("online", "thugpro_peer", false);
}

uint32_t read_machine_id()
{
    constexpr size_t kTextSize = 32;
    std::array<char, kTextSize> text{};
    build_info::fetch_text("online", "thugpro_machine_id", "", text.data(), sizeof(text));
    auto id = static_cast<uint32_t>(std::strtoul(text.data(), nullptr, kHexBase));
    std::random_device random;
    while (id == 0)
        id = static_cast<uint32_t>(random());
    return id;
}

// A string into a field: cut to fit, NUL-ended; the rest of the field left as it is (strncpy_s would fill it
// with 0xFE in a debug build - on the wire).
void copy_field(char* out, size_t size, const char* in)
{
    const size_t length = in ? strnlen(in, size - 1) : 0;
    if (length)
        std::memcpy(out, in, length);
    core::at(std::span<char>(out, size), length) = '\0';
}

// --- THUGPRO's level lists (register TP-R4) ---

// THUGPRO's THPS4 levels (its Levels.q `Game = THPS4`) and THUG1's New York: where its pro-set flags count
// (gameflow: LEVEL_ORIGINAL_GAME = THPS4 or LevelIs Load_NY).
constexpr auto kProSetLevels = std::to_array<const char*>({
    "Load_practice", "Load_sch", "Load_alc", "Load_sf2", "Load_kon", "Load_lon",
    "Load_jnk",      "Load_zoo", "Load_cnv", "Load_hof", "Load_NY",
});

// THUGPRO's toggle_*_params: the pro sets' node prefixes (PROSET1..7, then the competition's geometry).
struct ProSetPrefixes {
    const char* geo;
    const char* trg;
    const char* trg2; // (G_COMP only)
    const char* geo_not_;
    const char* trg_not_;
};
constexpr std::array<ProSetPrefixes, 8> kProSets = {{
    {.geo = "Proset1", .trg = "TRG_Proset1", .trg2 = nullptr, .geo_not_ = "PROSET1NOT", .trg_not_ = "TRG_PROSET1NOT"},
    {.geo = "Proset2", .trg = "TRG_Proset2", .trg2 = nullptr, .geo_not_ = "PROSET2NOT", .trg_not_ = "TRG_PROSET2NOT"},
    {.geo = "Proset3", .trg = "TRG_Proset3", .trg2 = nullptr, .geo_not_ = "PROSET3NOT", .trg_not_ = "TRG_PROSET3NOT"},
    {.geo = "Proset4", .trg = "TRG_Proset4", .trg2 = nullptr, .geo_not_ = "PROSET4NOT", .trg_not_ = "TRG_PROSET4NOT"},
    {.geo = "Proset5", .trg = "TRG_Proset5", .trg2 = nullptr, .geo_not_ = "PROSET5NOT", .trg_not_ = "TRG_PROSET5NOT"},
    {.geo = "Proset6", .trg = "TRG_Proset6", .trg2 = nullptr, .geo_not_ = "PROSET6NOT", .trg_not_ = "TRG_PROSET6NOT"},
    {.geo = "Proset7", .trg = "TRG_Proset7", .trg2 = nullptr, .geo_not_ = "PROSET7NOT", .trg_not_ = "TRG_PROSET7NOT"},
    {.geo = "G_COMP", .trg = "TRG_G_COMP", .trg2 = "TRG_COMP", .geo_not_ = "G_COMPNOT", .trg_not_ = "TRG_G_COMPNOT"},
}};

} // namespace

bool peer_mode()
{
    if (override() >= 0)
        return override() != 0;
    static const bool kPeerMode = read_peer_mode();
    return kPeerMode;
}

void set_peer_mode_override(int mode) { override() = mode; }

uint32_t get_machine_id()
{
    static const uint32_t kMachineId = read_machine_id();
    return kMachineId;
}

// --- Layouts ---

uint32_t encode_chat(std::span<uint8_t, kChatMessageSize> out, ChatSender sender, const char* name,
                     const char* text)
{
    const auto machine_id = sender.machine_id_;
    const auto obj_id = sender.obj_id_;
    ChatMessage message;
    message.machine_id_ = machine_id;
    message.obj_id_ = obj_id;
    copy_field(message.name.data(), sizeof(message.name), name);
    copy_field(message.text.data(), sizeof(message.text), text);
    util::ByteWriter writer(out);
    write_chat_message(writer, message);
    return static_cast<uint32_t>(writer.position());
}

bool decode_chat(const uint8_t* data, uint32_t length, bool thugpro, ChatLine* out)
{
    // THUGPRO: id, object, name, text - at most its 149 bytes (so text <= 127). THUG2: object, name, text - at most
    // its 0x94 bytes (text <= 130). The text ends inside the message (so the message is longer than its header), the
    // name inside its field.
    if (!data)
        return false;
    util::ByteReader reader(data, length);
    std::memset(out, 0, sizeof(*out));
    if (thugpro) {
        ChatMessage message;
        if (!read_chat_message(reader, &message))
            return false;
        out->machine_id_ = message.machine_id_;
        out->obj_id_ = message.obj_id_;
        std::memcpy(out->name.data(), message.name.data(), sizeof(out->name));
        std::memcpy(out->text.data(), message.text.data(), sizeof(message.text));
        return true;
    }
    if (length <= online::kChatHeaderSize || length > online::kChatSize)
        return false;
    Chat message;
    if (!read_chat(reader, &message) || std::strlen(message.name.data()) >= static_cast<size_t>(kNameSize) ||
        (reader.remaining() == 0 && core::at(std::span<const uint8_t>(data, length), length - 1) != 0))
        return false;
    out->obj_id_ = message.color;
    std::memcpy(out->name.data(), message.name.data(), sizeof(out->name));
    std::memcpy(out->text.data(), message.text.data(), sizeof(out->text) - 1);
    return true;
}

uint8_t get_level_options(const HostListing& info) { return static_cast<uint8_t>(info.flags & kFlagsByteMask); }
uint8_t get_pro_sets(const HostListing& info) { return static_cast<uint8_t>((info.flags >> kFlagsByteShift) & kFlagsByteMask); }
uint64_t make_flags(uint8_t level_options, uint8_t pro_sets)
{
    return static_cast<uint64_t>(level_options) | (static_cast<uint64_t>(pro_sets) << kFlagsByteShift);
}

bool decode_server_info(const uint8_t* data, uint32_t length, HostListing* out)
{
    if (!data)
        return false;
    util::ByteReader reader(data, length);
    return read_server_info(reader, out);
}

const char* change_level_name(uint32_t level, const char* list_name)
{
    switch (level) {
    case kLoadMainMenu:
        return "Skateshop\n";
    case kLoadSk5ed:
        return "Park Editor\n";
    case kLoadSk5edGameplay:
        return "Custom Park\n";
    default:
        return list_name ? list_name : "Unknown\n";
    }
}

void extend_change_level(const LevelSwitch& level, const char* name, std::span<uint8_t, kChangeLevelThugproSize> out)
{
    util::ByteWriter writer(out);
    write_change_level(writer, level, name);
}

// --- Effects ---

bool is_time_of_day(uint32_t state) { return gfx::next_tod_state(state) != 0; }

bool pro_set_level(uint32_t level_load)
{
    for (const char* load : kProSetLevels)
        if (checksums::hash_from_text(load) == level_load)
            return true;
    return false;
}

void apply_pro_set(int index, bool on, uint32_t level_load, Effects& effects)
{
    // THUGPRO's toggle_geo_nomenu (its KillAllTextureSplats first left out: decals only).
    const ProSetPrefixes& p = core::at(kProSets, index);
    effects.kill_prefix(p.geo);
    effects.kill_prefix(p.trg);
    if (p.trg2)
        effects.kill_prefix(p.trg2);
    effects.kill_prefix(p.geo_not_);
    effects.kill_prefix(p.trg_not_);
    effects.create_prefix(p.geo_not_);
    effects.create_prefix(p.trg_not_);
    if (!on)
        return;
    effects.create_prefix(p.geo);
    effects.create_prefix(p.trg);
    if (p.trg2)
        effects.create_prefix(p.trg2);
    effects.kill_prefix(p.geo_not_);
    effects.kill_prefix(p.trg_not_);
    effects.kill_prefix("TRG_G_COMP_Spectator");
    if (level_load == checksums::hash_from_text("Load_sch"))
        effects.kill_name(checksums::hash_from_text("G_COMP_Ramp04"));
}

void apply_server_info(const HostListing& info, const char* level_prefix, uint32_t level_load, Effects& effects)
{
    effects.set_time_of_day(info.time_of_day_);
    effects.set_weather(info.weather);
    // The level options: THUGPRO only ever sets its SERVER_LEVEL_CHANGE_0n_ON flags from this message (the level's
    // own setup sets its default ones), then runs each toggle by its flag - so an option not set here stays as the
    // level's setup made it (our ports run the defaults in their setup scripts).
    const uint8_t options = get_level_options(info);
    if (level_prefix && *level_prefix) {
        for (int n = 1; n <= kLevelOptionCount; ++n) {
            if (!(options & (1 << (n - 1))))
                continue;
            constexpr size_t kNameSize = 96;
            std::array<char, kNameSize> name{};
            text::format_to(name, "%s_LEVEL_CHANGE_0%d", level_prefix, n);
            effects.launch_script(checksums::hash_from_text(name.data()), true);
        }
    }
    // The pro sets: in THUGPRO's THPS4 levels and New York all eight toggled by their flags (THUGPRO's
    // HandleProtrickObjects at each start).
    if (pro_set_level(level_load)) {
        const uint8_t sets = get_pro_sets(info);
        for (int i = 0; i < kProSetCount; ++i)
            apply_pro_set(i, (sets >> i) & 1, level_load, effects);
    }
}

namespace {

// The game's side of the effects.
class GameEffects final : public Effects {
public:
    void set_time_of_day(uint32_t state) override
    {
        if (!is_time_of_day(state))
            state = gfx::kAfternoon; // (THUGPRO's thugpro_change_time_of_day: anything else is afternoon)
        gfx::CtodManager* manager = gfx::tod_manager();
        if (!manager || !manager->can_set_tod_instant(state)) {
            get_log("thugpro: time of day %08X not set (no time-of-day states in this level)", state);
            return;
        }
        if (manager->current_state() == state)
            return;
        manager->set_tod_instant(state);
    }
    void set_weather(uint32_t weather) override
    {
        // THUGPRO's set_current_weather_values: THUG2's rain / snow (the weather object 0x006FA7C8: +0x10 the
        // rain's rate, +0x40 the snow's, +0x58 rain (1) or snow (0)). "default" leaves the level's own weather
        // (THUGPRO clears it: it has its own per-level weather settings - ours are THUG2's).
        if (weather == kWeatherDefault)
            return;
        render::WeatherSystem* object = render::weather();
        if (!object)
            return;
        float& rain_rate = object->rain_rate();
        float& snow_rate = object->snow_rate();
        bool& rain = object->rain_on();
        if (weather == kWeatherRain) {
            rain = true;
            rain_rate = kRainRate;
            snow_rate = 0.0f;
        } else if (weather == kWeatherSnow) {
            rain = false;
            snow_rate = kSnowRate;
            rain_rate = 0.0f;
        } else { // clear, or anything else
            rain_rate = 0.0f;
            snow_rate = 0.0f;
        }
    }
    bool launch_script(uint32_t script, bool on) override
    {
        if (!script::script_exists(script))
            return false;
        gsl::owner<script::Struct*> params = new_params();
        params->put_name_key(checksums::Name{0u}, on ? kOn : kOff);
        script::launch_script(script, params, nullptr, false, nullptr);
        delete_params(params);
        return true;
    }
    void kill_prefix(const char* prefix) override
    {
        object_command(&commands::cfunc_kill, kPrefix, prefix, 0);
    }
    void create_prefix(const char* prefix) override
    {
        object_command(&commands::cfunc_create, kPrefix, prefix, 0);
    }
    void kill_name(uint32_t name) override
    {
        object_command(&commands::cfunc_kill, kName, nullptr, name);
    }

private:
    static gsl::owner<script::Struct*> new_params() { return script::new_struct(); }
    static void delete_params(script::Struct* params)
    {
        if (!params)
            return;
        params->get_clear();
        script::delete_struct(params);
    }
    // The script commands Kill (0x005A5030) and Create (0x005A4FF0) - prefix= or name= - run from a spawned
    // NullScript (Kill reads its script's object for some classes: it needs a real script).
    static void object_command(bool (*command)(script::Struct*, script::Script*), uint32_t key, const char* text,
                               uint32_t checksum)
    {
        if (!script::script_exists(kNullScript)) {
            get_log("thugpro: no NullScript - pro sets not changed");
            return;
        }
        script::Script* script =
            script::spawn_script(kNullScript, nullptr, {.node = -1});
        if (!script)
            return;
        gsl::owner<script::Struct*> params = new_params();
        if (text)
            params->append_word(key, text);
        else
            params->put_name_key(checksums::Name{key}, checksum);
        command(params, script);
        delete_params(params);
        script::kill_spawned_script(script);
    }
};

// The level structure being played: its `level` (the skate module's +0x380: the structure's name).
const char* current_level_prefix()
{
    auto* skate = modules::Skate::sole();
    if (!skate)
        return nullptr;
    script::Struct* structure = script::read_sub_struct(modules::Skate::sole()->level_params());
    const char* prefix = nullptr;
    if (!structure || !structure->fetch_text(kLevel, &prefix, 0))
        return nullptr;
    return prefix;
}

// The session's received options (a client of a THUGPRO host).
struct Session {
    bool have = false;    // server info (or an update) received
    bool applied = false; // applied since the level's last load
    HostListing info = {.time_of_day_ = gfx::kAfternoon, .weather = kWeatherDefault, .flags = 0};
    uint32_t level = 0; // the network level (load script) and the skater it was applied with
    void* skater = nullptr;
    int settle = 0; // frames the level has been up
    uint32_t last_time_ = 0;
};
Session& session()
{
    static core::NoDestroy<Session> session;
    return session.get();
}

// The host's options (the chat commands): sent in server info.
struct HostOptions {
    uint32_t weather = kWeatherDefault;
    uint8_t level_options_ = 0;
    uint8_t pro_sets_ = 0;
};
HostOptions& host()
{
    static core::NoDestroy<HostOptions> host;
    return host.get();
}



bool level_up() { return session().settle >= kSettleFrames; }

void apply_session()
{
    GameEffects effects;
    const char* prefix = current_level_prefix();
    const HostListing& info = session().info;
    get_log("thugpro: host options applied in %s: time of day %08X, weather %08X, level options %02X, pro sets %02X",
            prefix ? prefix : "(no level structure)", info.time_of_day_, info.weather, get_level_options(info),
            get_pro_sets(info));
    apply_server_info(info, prefix, online::online_hub()->level(), effects);
    session().applied = true;
}

uint32_t get_word(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body(), context->body_size());
    const uint32_t value = reader.u32();
    return reader.ok() ? value : 0;
}

// Server info for `app`'s connection `handle`, before THUG2's start info.
void send_server_info(net::AppHost* app, int handle)
{
    const HostListing info = host_server_info();
    std::array<uint8_t, kServerInfoSize> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    write_server_info(writer, info);
    net::NetMessageInfo desc;
    desc.set_ident(kThugproServerInfo);
    desc.set_byte_count(sizeof(bytes));
    desc.set_content(bytes.data());
    desc.set_rank(net::NetMessageInfo::kHighestRank);
    desc.set_queue(net::NetMessageInfo::kLanePriority);
    desc.set_group_id(online::MessageGroup::kGame);
    app->put_on_outbox(handle, &desc);
}

// A broadcast message's id and its value.
struct Broadcast {
    MessageId id{};
    uint32_t value{};
};

// A 4-byte update to every player (the host's own client ignores it).
void get_broadcast(Broadcast broadcast)
{
    const auto id = broadcast.id;
    const auto value = broadcast.value;
    Hub* manager = online::online_hub();
    if (!(manager && manager->in_net_game() && manager->get_on_server()) || !manager->host())
        return;
    net::NetMessageInfo desc;
    desc.set_ident(id);
    std::array<uint8_t, 4> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    writer.u32(value);
    desc.set_byte_count(sizeof(bytes));
    desc.set_content(bytes.data());
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(online::MessageGroup::kGame);
    manager->host()->put_on_outbox(kHandleBroadcast, &desc);
}

// NOT ORIGINAL - AppHost::put_on_outbox's filter (net_app.h): what THUGPRO's put_on_outbox hook adds when we host.
bool filter_outgoing(net::AppHost* app, int handle, net::NetMessageInfo* desc)
{
    if (!peer_mode())
        return false;
    Hub* manager = online::online_hub();
    if (!(manager && manager->in_net_game() && manager->get_on_server()) || app != manager->host())
        return false;
    if (desc->ident() == MessageId::kSessionBrief) {
        send_server_info(app, handle);
        return false;
    }
    if (desc->ident() == MessageId::kLevelSwitch && desc->byte_count() == kChangeLevelSize && desc->content()) {
        std::array<uint8_t, kChangeLevelThugproSize> message{};
        util::ByteReader reader(desc->content(), desc->byte_count());
        LevelSwitch change;
        read_level_switch(reader, &change);
        extend_change_level(change, change_level_name(change.level, Hub::level_list_name(change.level)), message);
        net::NetMessageInfo extended = *desc;
        extended.set_content(message.data());
        extended.set_byte_count(kChangeLevelThugproSize);
        app->put_on_outbox(handle, &extended);
        return true;
    }
    return false;
}

// NOT ORIGINAL - the TOD manager's multiplayer unlock (tod_manager.h): THUGPRO network games.
bool unlock_time_of_day()
{
    if (!peer_mode())
        return false;
    Hub* manager = online::online_hub();
    return manager && manager->in_net_game();
}

struct Install {
    Install() noexcept
    {
        net::set_enqueue_filter(filter_outgoing);
        gfx::set_multiplayer_unlock(unlock_time_of_day);
    }
};

Install& install()
{
    static core::NoDestroy<Install> state;
    return state.get();
}

} // namespace

const HostListing* received_server_info() { return session().have ? &session().info : nullptr; }
bool time_of_day_unlocked() { return unlock_time_of_day(); }
bool filter_outgoing_message(net::AppHost* app, int handle, net::NetMessageInfo* desc)
{
    return filter_outgoing(app, handle, desc);
}

// --- The handlers ---

void add_client_handlers(net::AppHost* client, void* skate)
{
    net::Router& dispatcher = client->dispatcher();
    dispatcher.append_listener(kThugproServerInfo, handle_server_info, 0, skate, net::kListenerPriority);
    dispatcher.append_listener(kThugproModsAllowed, handle_mods_allowed, 0, skate, net::kListenerPriority);
    dispatcher.append_listener(kThugproUpdateTimeOfDay, handle_update_time_of_day, net::kPacketLate, skate, net::kListenerPriority);
    dispatcher.append_listener(kThugproUpdateWeather, handle_update_weather, net::kPacketLate, skate, net::kListenerPriority);
    dispatcher.append_listener(kThugproUpdateLevelOption, handle_update_level_option, net::kPacketLate, skate, net::kListenerPriority);
}

int handle_server_info(net::InboundMsgCtx* context)
{
    HostListing info;
    if (!peer_mode() || !(online::online_hub() && online::online_hub()->in_net_game() && !online::online_hub()->get_on_server()) ||
        !decode_server_info(context->body(), context->body_size(), &info))
        return 1;
    session().have = true;
    session().applied = false;
    session().info = info;
    get_log("thugpro: server info: time of day %08X, weather %08X, level options %02X, pro sets %02X",
            info.time_of_day_, info.weather, get_level_options(info), get_pro_sets(info));
    return 1;
}

int handle_update_time_of_day(net::InboundMsgCtx* context)
{
    if (!peer_mode() || !(online::online_hub() && online::online_hub()->in_net_game() && !online::online_hub()->get_on_server()) || context->body_size() < 4)
        return 1;
    session().have = true;
    session().info.time_of_day_ = get_word(context);
    if (level_up()) {
        GameEffects effects;
        effects.set_time_of_day(session().info.time_of_day_);
    }
    return 1;
}

int handle_update_weather(net::InboundMsgCtx* context)
{
    if (!peer_mode() || !(online::online_hub() && online::online_hub()->in_net_game() && !online::online_hub()->get_on_server()) || context->body_size() < 4)
        return 1;
    session().have = true;
    session().info.weather = get_word(context);
    if (level_up()) {
        GameEffects effects;
        effects.set_weather(session().info.weather);
    }
    return 1;
}

// (THUGPRO registers it and does nothing; no THUGPRO code sends it)
int handle_update_level_option(net::InboundMsgCtx* /*context*/) { return 1; }

int handle_mods_allowed(net::InboundMsgCtx* context)
{
    // THUGPRO's client leaves when the host allows no modified scripts and its own are modified. Ours aren't
    // THUGPRO's at all - so a host that allows none is left, as the server removing us (THUG2's dialog).
    if (!peer_mode() || !(online::online_hub() && online::online_hub()->in_net_game() && !online::online_hub()->get_on_server()) || context->body_size() < 4 || get_word(context) == 1)
        return 1;
    get_log("thugpro: the host allows no modified scripts (message 159) - leaving");
    net::InboundMsgCtx removed = *context;
    removed.set_type_code(MessageId::kRemovedYou);
    return modules::SkateNet::handle_server_removed_you(&removed);
}

void tick_frame()
{
    Hub* manager = online::online_hub();
    if (!peer_mode() || !(manager && manager->in_net_game() && !manager->get_on_server())) {
        if (session().have)
            get_log("thugpro: network game over - the host's options dropped");
        session() = Session();
        return;
    }
    const uint32_t now = frame_clock::get_time();
    const bool loaded = session().last_time_ != 0 && now - session().last_time_ > kLoadGapMs;
    session().last_time_ = now;
    void* skater = objects::find_object(kSkater);
    if (loaded || !skater || manager->level() == kLoadMainMenu || manager->level() != session().level ||
        skater != session().skater) {
        if (loaded || manager->level() != session().level || skater != session().skater)
            session().applied = false;
        session().level = manager->level();
        session().skater = skater;
        session().settle = 0;
        if (!skater || manager->level() == kLoadMainMenu)
            return;
    }
    if (session().settle < kSettleFrames) {
        ++session().settle;
        return;
    }
    if (session().have && !session().applied)
        apply_session();
}

// --- Hosting ---

HostListing host_server_info()
{
    HostListing info = {.time_of_day_ = gfx::kAfternoon, .weather = host().weather, .flags = make_flags(host().level_options_, host().pro_sets_)};
    if (const gfx::CtodManager* manager = gfx::tod_manager()) {
        const uint32_t state = manager->current_state();
        if (is_time_of_day(state))
            info.time_of_day_ = state;
    }
    return info;
}

namespace {

// The time-of-day / weather names of the chat commands.
uint32_t state_named(const char* word, const char* const* names, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (_stricmp(word, core::at(std::span<const char* const>(names, count), i)) == 0)
            return checksums::hash_from_text(core::at(std::span<const char* const>(names, count), i));
    return 0;
}

bool on_off(const char* word, bool* on)
{
    if (_stricmp(word, "on") == 0)
        *on = true;
    else if (_stricmp(word, "off") == 0)
        *on = false;
    else
        return false;
    return true;
}

void get_say(const char* line) { show_local_chat_line(line); }

} // namespace

bool handle_chat_command(const char* text)
{
    if (!peer_mode() || *text != '/')
        return false;
    constexpr size_t kCommandSize = 16;
    std::array<char, kCommandSize> command = {};
    constexpr size_t kFirstSize = 24;
    std::array<char, kFirstSize> first = {};
    constexpr size_t kSecondSize = 8;
    std::array<char, kSecondSize> second = {};
    text::FieldReader fields(text); // "/%15s %23s %7s"
    (void)(fields.match('/') && fields.read_word(command) && fields.read_word(first) && fields.read_word(second));
    const int words = fields.scanned();
    if (words < 1)
        return false;
    Hub* manager = online::online_hub();
    const bool client = manager && manager->in_net_game() && !manager->get_on_server();
    constexpr size_t kLineSize = 160;
    std::array<char, kLineSize> line{};
    if (_stricmp(command.data(), "thugpro") == 0) {
        const HostListing& info = session().info;
        text::format_to(
            line,
            "\\c4thugpro:\\c0 machine id %08X; %s; time of day %08X, weather %08X, options %02X, pro sets "
            "%02X",
            get_machine_id(), client ? (session().have ? "the host's" : "no host options yet") : "ours",
            client ? info.time_of_day_ : host_server_info().time_of_day_, client ? info.weather : host().weather,
            client ? get_level_options(info) : host().level_options_, client ? get_pro_sets(info) : host().pro_sets_);
        get_say(line.data());
        return true;
    }
    const bool tod = _stricmp(command.data(), "tod") == 0;
    const bool weather = _stricmp(command.data(), "weather") == 0;
    const bool option = _stricmp(command.data(), "option") == 0;
    const bool pro_set = _stricmp(command.data(), "proset") == 0;
    if (!tod && !weather && !option && !pro_set)
        return false;
    if (client) {
        get_say("\\c4thugpro:\\c0 only the host sets the time of day, weather and options");
        return true;
    }
    GameEffects effects;
    if (tod || weather) {
        static const auto kTimes = std::to_array<const char*>({"morning", "afternoon", "evening", "night"});
        static const auto kWeathers = std::to_array<const char*>({"default", "clear", "rain", "snow"});
        const uint32_t state = tod ? state_named(first.data(), kTimes.data(), 4) : state_named(first.data(), kWeathers.data(), 4);
        if (words < 2 || !state) {
            get_say(tod ? "\\c4thugpro:\\c0 /tod morning|afternoon|evening|night"
                        : "\\c4thugpro:\\c0 /weather default|clear|rain|snow");
            return true;
        }
        if (tod) {
            effects.set_time_of_day(state);
            get_broadcast({.id = kThugproUpdateTimeOfDay, .value = state});
        } else {
            host().weather = state;
            effects.set_weather(state);
            get_broadcast({.id = kThugproUpdateWeather, .value = state});
        }
        text::format_to(line, "\\c4thugpro:\\c0 %s %s%s", command.data(), first.data(),
                        (manager && manager->in_net_game() && manager->get_on_server()) ? " (sent to the players)" : "");
        get_say(line.data());
        return true;
    }
    // /option n on|off, /proset n on|off: here now, to the players with the next game's server info (THUGPRO
    // changes them in a game through its own flag broadcast, which THUG2's scripts don't have).
    const int n = text::to_int(first.data());
    bool on = false;
    if (words < 3 || n < 1 || n > kLevelOptionCount || !on_off(second.data(), &on)) {
        get_say(option ? "\\c4thugpro:\\c0 /option <1-8> on|off" : "\\c4thugpro:\\c0 /proset <1-8> on|off (8: G_COMP)");
        return true;
    }
    auto bit = static_cast<uint8_t>(1 << (n - 1));
    const uint32_t load = manager ? manager->level() : 0;
    if (option) {
        host().level_options_ = static_cast<uint8_t>(on ? host().level_options_ | bit : host().level_options_ & ~bit);
        const char* prefix = current_level_prefix();
        constexpr size_t kNameSize = 96;
        std::array<char, kNameSize> name{};
        text::format_to(name, "%s_LEVEL_CHANGE_0%d", prefix ? prefix : "", n);
        const bool ran = prefix && effects.launch_script(checksums::hash_from_text(name.data()), on);
        text::format_to(line, "\\c4thugpro:\\c0 level option %d %s%s", n, on ? "on" : "off",
                        ran ? "" : " (this level has no such option)");
    } else {
        host().pro_sets_ = static_cast<uint8_t>(on ? host().pro_sets_ | bit : host().pro_sets_ & ~bit);
        const bool here = pro_set_level(load);
        if (here)
            apply_pro_set(n - 1, on, load, effects);
        text::format_to(line, "\\c4thugpro:\\c0 pro set %d %s%s", n, on ? "on" : "off",
                        here ? "" : " (only THPS4's levels and New York have them)");
    }
    get_say(line.data());
    return true;
}

} // namespace online::thugpro

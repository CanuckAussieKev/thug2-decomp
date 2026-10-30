// Self-tests for the THUGPRO peer mode (game/net/thugpro_peer.h) and the chat handler (0x00532050): the messages'
// byte layouts both ways against THUGPRO's structs (built here independently: its bool bit fields, its packing),
// malformed input, what server info does to a stand-in game (the effects in order), the handlers on a stand-in
// network manager, the host's outgoing filter on a stand-in EnqueueMessage, the TOD manager's multiplayer unlock,
// and the chat handler: ours against the original on THUG2's layout (the script calls and C runtime stood in), and
// THUGPRO's layout in the mode.

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/core/pool.h"
#include "game/gfx/tod_manager.h"
#include "game/net/chat.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_dispatcher.h"
#include "game/net/thugpro_peer.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/skate_net.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

#pragma warning(disable : 4996) // (strcpy into the test's own fixed buffers)
#pragma warning(disable : 4201) // (THUGPRO's flags: a nameless struct of bit fields in a union, as it declares them)

namespace {

namespace TP = GameNet::Thugpro;
using TestStandIns::AddressOf;
using TestStandIns::Branch;
using TestStandIns::Patch;

uint32_t Crc(const char* s) { return Crc::GenerateCRCFromString(s); }

std::string Hex(const void* data, size_t size)
{
    std::string out;
    char b[4];
    for (size_t i = 0; i < size; ++i) {
        std::snprintf(b, sizeof(b), "%02X", static_cast<const uint8_t*>(data)[i]);
        out += b;
    }
    return out;
}

// THUGPRO's own structs, as its GameMsg.h declares them (MSVC lays out the bool bit fields).
union TpFlags {
    uint64_t raw;
    struct {
        bool o1 : 1, o2 : 1, o3 : 1, o4 : 1, o5 : 1, o6 : 1, o7 : 1, o8 : 1;
        bool p1 : 1, p2 : 1, p3 : 1, p4 : 1, p5 : 1, p6 : 1, p7 : 1, p8 : 1;
    };
};
struct TpServerInfo {
    uint32_t tod;
    uint32_t weather;
    TpFlags flags;
};
class TpChangeLevel {
public:
    int level;
    char showWarning;
};
class TpChangeLevelThugpro : public TpChangeLevel {
public:
    char name[32];
};
struct TpChat {
    uint32_t id;
    char objId;
    char name[16];
    char text[128];
};

// --- Layouts ---------------------------------------------------------------------------------------------------

void TestThugproLayouts()
{
    // Server info: THUGPRO's struct bit for bit.
    static_assert(sizeof(TpServerInfo) == sizeof(TP::ServerInfo), "THUGPRO's server info size");
    TpServerInfo theirs = {};
    theirs.tod = Crc("night");
    theirs.weather = Crc("rain");
    theirs.flags.o1 = theirs.flags.o3 = theirs.flags.o8 = true;
    theirs.flags.p2 = theirs.flags.p8 = true;
    TP::ServerInfo ours = {Crc("night"), Crc("rain"), TP::MakeFlags(0x85, 0x82)};
    Check(std::memcmp(&theirs, &ours, sizeof(ours)) == 0, "thugpro: server info bytes = THUGPRO's struct");
    const uint8_t expected[16] = {0x68, 0xF0, 0x25, 0xEE, 0x52, 0x75, 0xAD, 0x48, 0x85, 0x82, 0, 0, 0, 0, 0, 0};
    Check(std::memcmp(&ours, expected, 16) == 0, "thugpro: server info bytes (night, rain, options 1 3 8, sets 2 8)");
    TP::ServerInfo decoded = {};
    Check(TP::DecodeServerInfo(expected, 16, &decoded) && decoded.timeOfDay == Crc("night") &&
              decoded.weather == Crc("rain") && TP::LevelOptions(decoded) == 0x85 && TP::ProSets(decoded) == 0x82,
          "thugpro: server info decoded");
    uint8_t longer[20] = {};
    std::memcpy(longer, expected, 16);
    Check(TP::DecodeServerInfo(longer, 20, &decoded) && TP::LevelOptions(decoded) == 0x85,
          "thugpro: server info longer than 16 bytes read");
    decoded = {};
    Check(!TP::DecodeServerInfo(expected, 15, &decoded) && decoded.timeOfDay == 0 &&
              !TP::DecodeServerInfo(nullptr, 16, &decoded),
          "thugpro: short server info refused (nothing read)");
    TP::ServerInfo high = {0, 0, 0xFFFFFFFFFFFF0000ull};
    Check(TP::LevelOptions(high) == 0 && TP::ProSets(high) == 0, "thugpro: flags past bit 15 ignored");

    // Chat: THUGPRO's struct, and the length it sends.
    TpChat tp = {};
    tp.id = 0xA1B2C3D4;
    tp.objId = 3;
    std::strcpy(tp.name, "Skater");
    std::strcpy(tp.text, "hello there");
    TP::ChatMessage message;
    uint32_t length = TP::EncodeChat(&message, 0xA1B2C3D4, 3, "Skater", "hello there");
    Check(length == 4 + 1 + 16 + 11 + 1 && std::memcmp(&message, &tp, length) == 0,
          "thugpro: chat bytes = THUGPRO's struct (length id + object + name + text + NUL)");
    Check(reinterpret_cast<uint8_t*>(&message)[0] == 0xD4 && reinterpret_cast<uint8_t*>(&message)[4] == 3 &&
              reinterpret_cast<char*>(&message)[5] == 'S' && reinterpret_cast<char*>(&message)[21] == 'h',
          "thugpro: chat offsets (id 0, object 4, name 5, text 21)");
    std::string longText(200, 'x');
    length = TP::EncodeChat(&message, 1, 8, "AVeryLongPlayerName", longText.c_str());
    Check(length == 21 + 127 + 1 && std::strlen(message.text) == 127 && std::strcmp(message.name, "AVeryLongPlaye") != 0 &&
              std::strlen(message.name) == 15,
          "thugpro: chat text cut to 127, name to 15");
    length = TP::EncodeChat(&message, 1, 0, "n", "");
    Check(length == 22 && message.text[0] == '\0', "thugpro: empty chat text: 22 bytes");

    // Decoding THUGPRO's layout.
    TP::ChatLine line;
    length = TP::EncodeChat(&message, 0x01020304, 5, "Kev", "hi all");
    Check(TP::DecodeChat(reinterpret_cast<uint8_t*>(&message), length, true, &line) && line.machineId == 0x01020304 &&
              line.objId == 5 && std::strcmp(line.name, "Kev") == 0 && std::strcmp(line.text, "hi all") == 0,
          "thugpro: chat decoded (THUGPRO's layout)");
    auto* bytes = reinterpret_cast<uint8_t*>(&message);
    Check(!TP::DecodeChat(bytes, 21, true, &line), "thugpro: chat without text refused");
    Check(!TP::DecodeChat(bytes, sizeof(TP::ChatMessage) + 1, true, &line), "thugpro: chat longer than THUGPRO's struct refused");
    Check(!TP::DecodeChat(nullptr, length, true, &line), "thugpro: no chat data refused");
    TP::ChatMessage bad = message;
    std::memset(bad.name, 'A', sizeof(bad.name));
    Check(!TP::DecodeChat(reinterpret_cast<uint8_t*>(&bad), length, true, &line), "thugpro: chat name without its NUL refused");
    bad = message;
    Check(!TP::DecodeChat(reinterpret_cast<uint8_t*>(&bad), length - 1, true, &line),
          "thugpro: chat text not ended inside the message refused");
    TP::ChatMessage full = {};
    std::memset(full.text, 'y', 128);
    Check(!TP::DecodeChat(reinterpret_cast<uint8_t*>(&full), sizeof(full), true, &line),
          "thugpro: chat text of 128 without NUL refused");
    full.text[127] = '\0';
    Check(TP::DecodeChat(reinterpret_cast<uint8_t*>(&full), sizeof(full), true, &line) && std::strlen(line.text) == 127,
          "thugpro: chat text of 127 taken");
    // THUG2's layout (object, name, text) through the same decoder.
    uint8_t thug2[0x94] = {};
    thug2[0] = 2;
    std::strcpy(reinterpret_cast<char*>(thug2 + 1), "Pro");
    std::strcpy(reinterpret_cast<char*>(thug2 + 17), "yo");
    Check(TP::DecodeChat(thug2, 17 + 3, false, &line) && line.objId == 2 && std::strcmp(line.name, "Pro") == 0 &&
              std::strcmp(line.text, "yo") == 0 && line.machineId == 0,
          "thugpro: THUG2 chat layout decoded");
    Check(!TP::DecodeChat(thug2, 0x95, false, &line) && !TP::DecodeChat(thug2, 17, false, &line),
          "thugpro: THUG2 chat too long / too short refused");

    // Change level: THUGPRO's derived class (the name after the base's 8 bytes).
    static_assert(sizeof(TpChangeLevelThugpro) == TP::kChangeLevelThugproSize, "THUGPRO's change level size");
    TpChangeLevelThugpro change = {};
    change.level = static_cast<int>(Crc("load_lon"));
    change.showWarning = 1;
    std::strcpy(change.name, "THPS4 LONDON");
    uint8_t base[8] = {};
    uint32_t level = Crc("load_lon");
    std::memcpy(base, &level, 4);
    base[4] = 1;
    base[5] = 0xEE; // (the original's padding is sent as it is)
    reinterpret_cast<uint8_t*>(&change)[5] = 0xEE;
    uint8_t extended[TP::kChangeLevelThugproSize];
    TP::ExtendChangeLevel(base, "THPS4 LONDON", extended);
    Check(std::memcmp(extended, &change, sizeof(extended)) == 0, "thugpro: change level bytes = THUGPRO's class");
    TP::ExtendChangeLevel(base, std::string(40, 'Z').c_str(), extended);
    Check(extended[8] == 'Z' && extended[8 + 30] == 'Z' && extended[8 + 31] == 0, "thugpro: change level name cut to 31");
    Check(std::strcmp(TP::ChangeLevelName(0xE92ECAFE, "x"), "Skateshop\n") == 0 &&
              std::strcmp(TP::ChangeLevelName(0xE8B4B836, "x"), "Park Editor\n") == 0 &&
              std::strcmp(TP::ChangeLevelName(0xB664035D, "x"), "Custom Park\n") == 0 &&
              std::strcmp(TP::ChangeLevelName(level, "London"), "London") == 0 &&
              std::strcmp(TP::ChangeLevelName(level, nullptr), "Unknown\n") == 0,
          "thugpro: change level names (THUGPRO's words, the list's, Unknown)");

    Check(TP::IsTimeOfDay(Crc("morning")) && TP::IsTimeOfDay(Crc("afternoon")) && TP::IsTimeOfDay(Crc("evening")) &&
              TP::IsTimeOfDay(Crc("night")) && !TP::IsTimeOfDay(Crc("default")) && !TP::IsTimeOfDay(0),
          "thugpro: time-of-day states");
    Check(TP::ProSetLevel(Crc("Load_lon")) && TP::ProSetLevel(Crc("load_practice")) && TP::ProSetLevel(Crc("Load_NY")) &&
              TP::ProSetLevel(Crc("Load_hof")) && !TP::ProSetLevel(Crc("Load_TR")) && !TP::ProSetLevel(Crc("Load_NJ")) &&
              !TP::ProSetLevel(0),
          "thugpro: THUGPRO's pro-set levels (THPS4's, New York)");
}

// --- Effects ---------------------------------------------------------------------------------------------------

struct RecordingEffects : TP::Effects {
    std::vector<std::string> log;
    std::vector<uint32_t> scripts; // the ones that "exist"
    void SetTimeOfDay(uint32_t state) override { Add("tod", state); }
    void SetWeather(uint32_t weather) override { Add("weather", weather); }
    bool RunScript(uint32_t script, bool on) override
    {
        Add(on ? "run on" : "run off", script);
        for (uint32_t s : scripts)
            if (s == script)
                return true;
        return false;
    }
    void KillPrefix(const char* prefix) override { log.push_back(std::string("kill ") + prefix); }
    void CreatePrefix(const char* prefix) override { log.push_back(std::string("create ") + prefix); }
    void KillName(uint32_t name) override { Add("kill name", name); }
    void Add(const char* what, uint32_t value)
    {
        char line[64];
        std::snprintf(line, sizeof(line), "%s %08X", what, value);
        log.push_back(line);
    }
};

std::vector<std::string> ExpectedProSet(int n, bool on, bool sch)
{
    char geo[16], trg[24], not1[24], not2[24];
    if (n == 8) {
        std::strcpy(geo, "G_COMP");
        std::strcpy(trg, "TRG_G_COMP");
        std::strcpy(not1, "G_COMPNOT");
        std::strcpy(not2, "TRG_G_COMPNOT");
    } else {
        std::snprintf(geo, sizeof(geo), "Proset%d", n);
        std::snprintf(trg, sizeof(trg), "TRG_Proset%d", n);
        std::snprintf(not1, sizeof(not1), "PROSET%dNOT", n);
        std::snprintf(not2, sizeof(not2), "TRG_PROSET%dNOT", n);
    }
    std::vector<std::string> out = {std::string("kill ") + geo, std::string("kill ") + trg};
    if (n == 8)
        out.push_back("kill TRG_COMP");
    out.insert(out.end(), {std::string("kill ") + not1, std::string("kill ") + not2, std::string("create ") + not1,
                           std::string("create ") + not2});
    if (!on)
        return out;
    out.insert(out.end(), {std::string("create ") + geo, std::string("create ") + trg});
    if (n == 8)
        out.push_back("create TRG_COMP");
    out.insert(out.end(), {std::string("kill ") + not1, std::string("kill ") + not2, "kill TRG_G_COMP_Spectator"});
    if (sch) {
        char line[64];
        std::snprintf(line, sizeof(line), "kill name %08X", Crc("G_COMP_Ramp04"));
        out.push_back(line);
    }
    return out;
}

void TestThugproEffects()
{
    auto hex = [](const char* what, uint32_t v) {
        char line[64];
        std::snprintf(line, sizeof(line), "%s %08X", what, v);
        return std::string(line);
    };
    // London (THPS4): options 1 and 3, pro sets 2 and G_COMP.
    {
        RecordingEffects effects;
        TP::ServerInfo info = {Crc("evening"), Crc("snow"), TP::MakeFlags(0x05, 0x82)};
        TP::ApplyServerInfo(info, "lon", Crc("Load_lon"), effects);
        std::vector<std::string> expected = {hex("tod", Crc("evening")), hex("weather", Crc("snow")),
                                             hex("run on", Crc("lon_LEVEL_CHANGE_01")),
                                             hex("run on", Crc("lon_LEVEL_CHANGE_03"))};
        for (int n = 1; n <= 8; ++n) {
            auto set = ExpectedProSet(n, n == 2 || n == 8, false);
            expected.insert(expected.end(), set.begin(), set.end());
        }
        Check(effects.log == expected, "thugpro: server info in London (time, weather, options 1 3, all 8 pro sets)");
        if (effects.log != expected)
            for (size_t i = 0; i < effects.log.size() && i < 40; ++i)
                Log("  effect %u: %s", static_cast<unsigned>(i), effects.log[i].c_str());
    }
    // A THUG2 level: no pro sets, options 2 and 8.
    {
        RecordingEffects effects;
        TP::ServerInfo info = {Crc("afternoon"), Crc("default"), TP::MakeFlags(0x82, 0xFF)};
        TP::ApplyServerInfo(info, "TR", Crc("Load_TR"), effects);
        std::vector<std::string> expected = {hex("tod", Crc("afternoon")), hex("weather", Crc("default")),
                                             hex("run on", Crc("TR_LEVEL_CHANGE_02")),
                                             hex("run on", Crc("TR_LEVEL_CHANGE_08"))};
        Check(effects.log == expected, "thugpro: server info in a THUG2 level (no pro sets; options 2 8)");
    }
    // No level structure: time and weather only. School (THPS4): set 1 on - its G_COMP ramp killed.
    {
        RecordingEffects effects;
        TP::ServerInfo info = {Crc("night"), Crc("clear"), TP::MakeFlags(0xFF, 0x01)};
        TP::ApplyServerInfo(info, nullptr, Crc("Load_TR"), effects);
        Check(effects.log.size() == 2, "thugpro: no level prefix: no options run");
        RecordingEffects school;
        TP::ApplyServerInfo(info, "", Crc("Load_sch"), school);
        std::vector<std::string> expected = {hex("tod", Crc("night")), hex("weather", Crc("clear"))};
        for (int n = 1; n <= 8; ++n) {
            auto set = ExpectedProSet(n, n == 1, true);
            expected.insert(expected.end(), set.begin(), set.end());
        }
        Check(school.log == expected, "thugpro: School: pro set 1 on (and its G_COMP ramp killed), the rest off");
    }
    // One pro set at a time (the host's /proset).
    {
        RecordingEffects effects;
        TP::ApplyProSet(7, true, Crc("Load_zoo"), effects);
        Check(effects.log == ExpectedProSet(8, true, false), "thugpro: G_COMP on alone");
        effects.log.clear();
        TP::ApplyProSet(0, false, Crc("Load_zoo"), effects);
        Check(effects.log == ExpectedProSet(1, false, false), "thugpro: pro set 1 off alone");
    }
}

// --- The handlers and the host's filter ------------------------------------------------------------------------

alignas(16) uint8_t g_tp_manager[0x40];
alignas(16) uint8_t g_tp_client[sizeof(Net::App)];
alignas(16) uint8_t g_tp_server[sizeof(Net::App)];
GameNet::Manager* g_tp_savedManager;
Script::CSymbolTableEntry** g_tp_savedTable;
Gfx::CTODManager* g_tp_savedTod;

struct Enqueued {
    Net::App* app;
    int handle;
    uint8_t id;
    int queue;
    int priority;
    uint8_t group;
    std::string data;
};
std::vector<Enqueued> g_tp_enqueued;
void __fastcall StandInEnqueue(Net::App* app, void*, int handle, Net::MsgDesc* desc)
{
    g_tp_enqueued.push_back({app, handle, desc->m_id, desc->m_queue, desc->m_priority, desc->m_groupId,
                             Hex(desc->m_data, desc->m_length)});
}
std::vector<uint8_t> g_tp_removed;
int __cdecl StandInRemovedYou(Net::MsgHandlerContext* context)
{
    g_tp_removed.push_back(context->m_MsgId);
    return 3;
}

void RestoreThugpro()
{
    TP::SetPeerModeOverride(-1);
    GameVariable<GameNet::Manager*>(0x007CCE14) = g_tp_savedManager;
    if (g_tp_savedTable)
        GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = g_tp_savedTable;
    Gfx::TODManager() = g_tp_savedTod;
}

GameNet::Manager* SetManager(uint8_t flags, uint32_t level)
{
    std::memset(g_tp_manager, 0, sizeof(g_tp_manager));
    auto* manager = reinterpret_cast<GameNet::Manager*>(g_tp_manager);
    manager->m_flags = flags;
    manager->m_client = reinterpret_cast<Net::App*>(g_tp_client);
    manager->m_server = (flags & 1) ? reinterpret_cast<Net::App*>(g_tp_server) : nullptr;
    manager->m_level = level;
    return manager;
}

Net::MsgHandlerContext Context(uint8_t id, const void* data, uint32_t length)
{
    Net::MsgHandlerContext context;
    std::memset(&context, 0, sizeof(context));
    context.m_Msg = static_cast<char*>(const_cast<void*>(data));
    context.m_MsgId = id;
    context.m_MsgLength = length;
    return context;
}

void TestThugproHandlers()
{
    g_tp_savedManager = GameVariable<GameNet::Manager*>(0x007CCE14);
    g_tp_savedTable = nullptr;
    g_tp_savedTod = Gfx::TODManager();
    Gfx::TODManager() = nullptr;
    std::memset(g_tp_client, 0, sizeof(g_tp_client));
    std::memset(g_tp_server, 0, sizeof(g_tp_server));
    GameNet::Manager* manager = SetManager(0x0C, Crc("load_lon"));
    GameVariable<GameNet::Manager*>(0x007CCE14) = manager;

    // The mode off: the session dropped, nothing taken.
    TP::SetPeerModeOverride(0);
    TP::Update();
    TP::ServerInfo info = {Crc("night"), Crc("rain"), TP::MakeFlags(3, 4)};
    auto context = Context(155, &info, sizeof(info));
    Check(TP::HandleServerInfo(&context) == 1 && !TP::ReceivedServerInfo(), "thugpro: server info ignored, mode off");
    Check(!TP::TimeOfDayUnlocked(), "thugpro: time of day locked, mode off");

    TP::SetPeerModeOverride(1);
    Check(TP::TimeOfDayUnlocked(), "thugpro: time of day unlocked in a network game");
    context = Context(155, &info, 15);
    Check(TP::HandleServerInfo(&context) == 1 && !TP::ReceivedServerInfo(), "thugpro: short server info ignored");
    SetManager(0x0D, Crc("load_lon"));
    context = Context(155, &info, sizeof(info));
    Check(TP::HandleServerInfo(&context) == 1 && !TP::ReceivedServerInfo(), "thugpro: server info ignored by the host");
    SetManager(0x00, Crc("load_lon"));
    Check(TP::HandleServerInfo(&context) == 1 && !TP::ReceivedServerInfo() && !TP::TimeOfDayUnlocked(),
          "thugpro: server info ignored outside a network game; time of day locked");
    reinterpret_cast<Net::App*>(g_tp_client)->m_flags = Net::App::kLocal;
    SetManager(0x0C, Crc("load_lon"));
    Check(!TP::TimeOfDayUnlocked(), "thugpro: a local client isn't a network game");
    reinterpret_cast<Net::App*>(g_tp_client)->m_flags = 0;
    Check(TP::HandleServerInfo(&context) == 1 && TP::ReceivedServerInfo() &&
              std::memcmp(TP::ReceivedServerInfo(), &info, sizeof(info)) == 0,
          "thugpro: server info taken by a client");
    // Updates: time of day and weather kept (the level isn't up: nothing applied yet); short ones ignored.
    uint32_t morning = Crc("morning");
    context = Context(156, &morning, 3);
    TP::HandleUpdateTimeOfDay(&context);
    Check(TP::ReceivedServerInfo()->timeOfDay == Crc("night"), "thugpro: short time-of-day update ignored");
    context = Context(156, &morning, 4);
    Check(TP::HandleUpdateTimeOfDay(&context) == 1 && TP::ReceivedServerInfo()->timeOfDay == morning,
          "thugpro: time-of-day update kept");
    uint32_t snow = Crc("snow");
    context = Context(157, &snow, 4);
    Check(TP::HandleUpdateWeather(&context) == 1 && TP::ReceivedServerInfo()->weather == snow &&
              TP::LevelOptions(*TP::ReceivedServerInfo()) == 3,
          "thugpro: weather update kept (the options too)");
    context = Context(158, &snow, 4);
    Check(TP::HandleUpdateLevelOption(&context) == 1, "thugpro: level option update: nothing (as THUGPRO)");

    // Mods allowed: 1 stays; anything else leaves as removed ('Q'); short ignored.
    {
        Patch removed(AddressOf(&Mdl::SkateNet::HandleServerRemovedYou),
                      Branch(0xE9, AddressOf(&Mdl::SkateNet::HandleServerRemovedYou), &StandInRemovedYou));
        g_tp_removed.clear();
        uint32_t one = 1, zero = 0, two = 2;
        context = Context(159, &one, 4);
        int stays = TP::HandleModsAllowed(&context);
        context = Context(159, &zero, 3);
        int shortOne = TP::HandleModsAllowed(&context);
        context = Context(159, &zero, 4);
        int leaves = TP::HandleModsAllowed(&context);
        context = Context(159, &two, 4);
        int leaves2 = TP::HandleModsAllowed(&context);
        Check(stays == 1 && shortOne == 1 && leaves == 3 && leaves2 == 3 && g_tp_removed.size() == 2 &&
                  g_tp_removed[0] == 'Q' && context.m_MsgId == 159,
              "thugpro: mods allowed: 1 stays, else removed (the context's id left alone), short ignored");
        SetManager(0x0D, Crc("load_lon"));
        g_tp_removed.clear();
        context = Context(159, &zero, 4);
        Check(TP::HandleModsAllowed(&context) == 1 && g_tp_removed.empty(), "thugpro: mods allowed ignored by the host");
        SetManager(0x0C, Crc("load_lon"));
    }

    // The network game over (Update): the session dropped.
    SetManager(0x00, Crc("load_lon"));
    TP::Update();
    Check(!TP::ReceivedServerInfo(), "thugpro: session dropped when the network game ends");

    // The host's filter: server info before start info, the name after change level.
    g_tp_savedTable = GameVariable<Script::CSymbolTableEntry**>(0x006F3624);
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;
    {
        Patch enqueue(AddressOf(&Net::App::EnqueueMessage),
                      Branch(0xE9, AddressOf(&Net::App::EnqueueMessage), &StandInEnqueue));
        auto* server = reinterpret_cast<Net::App*>(g_tp_server);
        auto* client = reinterpret_cast<Net::App*>(g_tp_client);
        uint8_t start[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        uint8_t change[8] = {0xFE, 0xCA, 0x2E, 0xE9, 1, 0, 0, 0}; // load_mainmenu, show warning
        auto desc = [](uint8_t id, void* data, uint16_t length) {
            Net::MsgDesc d;
            std::memset(&d, 0, sizeof(d));
            d.m_id = id;
            d.m_data = data;
            d.m_length = length;
            d.m_queue = Net::MsgDesc::kQueueSequenced;
            d.m_groupId = 8;
            d.m_priority = 0x80;
            return d;
        };
        std::vector<int> results;
        auto run = [&](Net::App* app, int handle, Net::MsgDesc d) {
            results.push_back(TP::FilterOutgoingMessage(app, handle, &d));
        };
        g_tp_enqueued.clear();
        SetManager(0x0D, Crc("load_mainmenu"));
        run(server, 3, desc(51, start, sizeof(start)));
        run(server, 0xFF, desc(70, change, 8));
        run(server, 4, desc(70, change, 7));      // not THUG2's change level: as it is
        run(client, 3, desc(51, start, sizeof(start))); // not the server app
        run(server, 3, desc(52, start, sizeof(start))); // another message
        SetManager(0x0C, Crc("load_mainmenu"));
        run(server, 3, desc(51, start, sizeof(start))); // not hosting
        TP::SetPeerModeOverride(0);
        SetManager(0x0D, Crc("load_mainmenu"));
        run(server, 3, desc(70, change, 8)); // the mode off
        TP::SetPeerModeOverride(1);
        TP::ServerInfo host = TP::HostServerInfo();
        uint8_t extended[40] = {0xFE, 0xCA, 0x2E, 0xE9, 1, 0, 0, 0, 'S', 'k', 'a', 't', 'e', 's', 'h', 'o', 'p', '\n'};
        Check(results == std::vector<int>{0, 1, 0, 0, 0, 0, 0}, "thugpro: host filter results (change level taken over)");
        Check(g_tp_enqueued.size() == 2 && g_tp_enqueued[0].app == server && g_tp_enqueued[0].handle == 3 &&
                  g_tp_enqueued[0].id == 155 && g_tp_enqueued[0].queue == Net::MsgDesc::kQueueImportant &&
                  g_tp_enqueued[0].priority == 0xFF && g_tp_enqueued[0].group == 8 &&
                  g_tp_enqueued[0].data == Hex(&host, sizeof(host)),
              "thugpro: host sends server info (important, highest, group 8) before start info");
        Check(g_tp_enqueued.size() == 2 && g_tp_enqueued[1].handle == 0xFF && g_tp_enqueued[1].id == 70 &&
                  g_tp_enqueued[1].queue == Net::MsgDesc::kQueueSequenced && g_tp_enqueued[1].group == 8 &&
                  g_tp_enqueued[1].data == Hex(extended, sizeof(extended)),
              "thugpro: host's change level with the level's name (40 bytes)");
        Check(host.timeOfDay == Crc("afternoon") && host.weather == Crc("default") && host.flags == 0,
              "thugpro: host's server info without a TOD manager: afternoon, default, no options");
    }

    // The TOD manager's multiplayer lock: the original's test, then the unlock.
    {
        static bool multiplayer;
        struct Stub {
            static bool __cdecl IsMultiplayer() { return multiplayer; }
            static bool Yes() { return true; }
            static bool No() { return false; }
        };
        Patch isMultiplayer(0x005651F0, Branch(0xE9, 0x005651F0, &Stub::IsMultiplayer));
        const uint32_t ourIsMultiplayer = reinterpret_cast<uint32_t>(CodeAddress(&Mdl::Skate::IsMultiplayer));
        Patch isMultiplayerOurs(ourIsMultiplayer, Branch(0xE9, ourIsMultiplayer, &Stub::IsMultiplayer));
        Gfx::MultiplayerUnlock saved = Gfx::GetMultiplayerUnlock();
        std::vector<int> locked;
        for (Gfx::MultiplayerUnlock unlock : {static_cast<Gfx::MultiplayerUnlock>(nullptr), &Stub::Yes, &Stub::No}) {
            Gfx::SetMultiplayerUnlock(unlock);
            for (bool m : {false, true}) {
                multiplayer = m;
                locked.push_back(Gfx::TODLightsLocked());
            }
        }
        Gfx::SetMultiplayerUnlock(saved);
        Check(locked == std::vector<int>{0, 1, 0, 0, 0, 1}, "thugpro: TOD lock (multiplayer, unless unlocked)");
        Check(saved != nullptr && saved() == TP::TimeOfDayUnlocked(), "thugpro: the TOD unlock installed");
    }

    // The TOD manager's walk check and state.
    {
        alignas(16) static uint8_t managerBytes[sizeof(Gfx::CTODManager)];
        std::memset(managerBytes, 0, sizeof(managerBytes));
        auto* tod = reinterpret_cast<Gfx::CTODManager*>(managerBytes);
        // parameters: night and morning structures only
        alignas(16) static uint8_t structs[3][8];
        alignas(16) static Script::CComponent components[2];
        std::memset(structs, 0, sizeof(structs));
        std::memset(components, 0, sizeof(components));
        uint32_t names[2] = {Crc("night"), Crc("morning")};
        for (int i = 0; i < 2; ++i) {
            components[i].typeAndFlag = static_cast<uint8_t>(Script::ESYMBOLTYPE_STRUCTURE << 1);
            components[i].name = names[i];
            components[i].structure = reinterpret_cast<Script::CStruct*>(structs[i + 1]);
            if (i)
                components[i - 1].next = &components[i];
        }
        reinterpret_cast<Script::CComponent**>(structs[0])[1] = &components[0];
        tod->m_systems[0].sky = true;
        tod->m_systems[1].state = Crc("evening");
        tod->m_systems[1].params = reinterpret_cast<Script::CStruct*>(structs[0]);
        std::vector<int> can;
        for (const char* target : {"morning", "night", "afternoon", "evening", "default"})
            can.push_back(tod->CanSetTODInstant(Crc(target)));
        uint32_t state = tod->CurrentState();
        tod->m_systems[1].state = 0x12345678;
        can.push_back(tod->CanSetTODInstant(Crc("night")));
        tod->m_systems[1].state = Crc("evening");
        tod->m_systems[1].params = nullptr;
        can.push_back(tod->CanSetTODInstant(Crc("night")));
        tod->m_systems[1].sky = true;
        can.push_back(tod->CanSetTODInstant(Crc("night")));
        uint32_t noState = tod->CurrentState();
        Check(can == std::vector<int>{1, 1, 1, 0, 0, 0, 0, 0} && state == Crc("evening") && noState == 0,
              "thugpro: TOD walk check (states on the way, unknown states, no parameters, sky only)");
        // All four states there: an unknown target or current state is still refused (the walk would never end).
        {
            alignas(16) static uint8_t all[5][8];
            alignas(16) static Script::CComponent four[4];
            std::memset(all, 0, sizeof(all));
            std::memset(four, 0, sizeof(four));
            const char* stateNames[4] = {"morning", "afternoon", "evening", "night"};
            for (int i = 0; i < 4; ++i) {
                four[i].typeAndFlag = static_cast<uint8_t>(Script::ESYMBOLTYPE_STRUCTURE << 1);
                four[i].name = Crc(stateNames[i]);
                four[i].structure = reinterpret_cast<Script::CStruct*>(all[i + 1]);
                if (i)
                    four[i - 1].next = &four[i];
            }
            reinterpret_cast<Script::CComponent**>(all[0])[1] = &four[0];
            tod->m_systems[1].sky = false;
            tod->m_systems[1].params = reinterpret_cast<Script::CStruct*>(all[0]);
            tod->m_systems[1].state = Crc("evening");
            bool evening = tod->CanSetTODInstant(Crc("evening"));
            bool unknownTarget = tod->CanSetTODInstant(Crc("default"));
            tod->m_systems[1].state = Crc("default");
            bool unknownCurrent = tod->CanSetTODInstant(Crc("night"));
            tod->m_systems[1].sky = true;
            Check(evening && !unknownTarget && !unknownCurrent,
                  "thugpro: TOD walk check with every state: unknown target / current state refused");
        }
        // The host's server info from it.
        tod->m_systems[1].sky = false;
        tod->m_systems[1].state = Crc("night");
        Gfx::TODManager() = tod;
        TP::ServerInfo host = TP::HostServerInfo();
        tod->m_systems[1].state = 0x12345678;
        TP::ServerInfo odd = TP::HostServerInfo();
        Gfx::TODManager() = nullptr;
        Check(host.timeOfDay == Crc("night") && odd.timeOfDay == Crc("afternoon"),
              "thugpro: host's time of day from the TOD manager (afternoon when it's none of the four)");
    }
    RestoreThugpro();
}

// --- The chat handler ------------------------------------------------------------------------------------------


std::vector<std::string> g_tp_sent;
void __fastcall StandInSendToServer(void* client, void*, Net::MsgDesc* desc)
{
    char tail[64];
    std::snprintf(tail, sizeof(tail), " %d %d %d %s", desc->m_id, desc->m_queue, desc->m_groupId,
                  client ? "client" : "none");
    g_tp_sent.push_back(Hex(desc->m_data, desc->m_length) + tail);
}
std::vector<std::string> g_tp_handlers;
Net::MsgHandler* __fastcall StandInAddHandler(Net::Dispatcher*, void*, uint8_t id, Net::MsgHandlerCode code,
                                              uint32_t flags, void* data, int priority)
{
    char line[96];
    std::snprintf(line, sizeof(line), "%d %p %u %s %d", id, reinterpret_cast<void*>(code), flags,
                  data ? "skate" : "none", priority);
    g_tp_handlers.push_back(line);
    return nullptr;
}


std::vector<std::string> g_tp_chat;
alignas(16) uint8_t g_tp_struct[16];
void* __fastcall StandInAllocate(void*, void*)
{
    g_tp_chat.push_back("alloc");
    return g_tp_struct;
}
void* __fastcall StandInConstruct(void* self, void*)
{
    g_tp_chat.push_back(self == g_tp_struct ? "ctor" : "ctor elsewhere");
    return self;
}
void __fastcall StandInAddString(void* self, void*, uint32_t name, const char* value)
{
    char line[300];
    std::snprintf(line, sizeof(line), "add %d %08X %s", self == g_tp_struct, name, value);
    g_tp_chat.push_back(line);
}
void __fastcall StandInDestruct(void* self, void*) { g_tp_chat.push_back(self == g_tp_struct ? "dtor" : "dtor ?"); }
void __cdecl StandInFree(void* p) { g_tp_chat.push_back(p == g_tp_struct ? "free" : "free ?"); }
void __cdecl StandInRunScript(uint32_t name, void* params, void* object, int net, const char*)
{
    char line[64];
    std::snprintf(line, sizeof(line), "run %08X %d %p %d", name, params == g_tp_struct, object, net & 0xFF);
    g_tp_chat.push_back(line);
}
int __cdecl StandInChatSprintf(char* out, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int n = std::vsnprintf(out, 256, format, args);
    va_end(args);
    return n;
}

void TestThugproChatHandler()
{
    int savedIndex = GameVariable<int>(0x006F3614);
    void* savedPools[2] = {GameVariable<void* [2]>(0x006F361C)[0], GameVariable<void* [2]>(0x006F361C)[1]};
    GameVariable<int>(0x006F3614) = 0;
    static uint8_t fakePool[64];
    GameVariable<void* [2]>(0x006F361C)[0] = fakePool;
    std::vector<Patch*> patches;
    auto standIn = [&](uint32_t address, const void* to) { patches.push_back(new Patch(address, Branch(0xE9, address, to))); };
    standIn(0x005BA3D0, reinterpret_cast<const void*>(&StandInAllocate));
    standIn(AddressOf(&Mem::Pool::Allocate), reinterpret_cast<const void*>(&StandInAllocate));
    standIn(0x00476410, reinterpret_cast<const void*>(&StandInConstruct));
    standIn(0x004779D0, reinterpret_cast<const void*>(&StandInAddString));
    standIn(0x00477130, reinterpret_cast<const void*>(&StandInDestruct));
    standIn(0x00409070, reinterpret_cast<const void*>(&StandInFree));
    // (and ours: the chat code calls them directly)
    standIn(AddressOf(&Script::CStruct::Init), reinterpret_cast<const void*>(&StandInConstruct));
    standIn(AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, const char*)>(&Script::CStruct::AddString)),
            reinterpret_cast<const void*>(&StandInAddString));
    standIn(AddressOf(&Script::CStruct::Clear), reinterpret_cast<const void*>(&StandInDestruct));
    standIn(AddressOf(&Script::DeleteStruct), reinterpret_cast<const void*>(&StandInFree));
    standIn(0x00475790, reinterpret_cast<const void*>(&StandInRunScript));
    standIn(AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                &Script::RunScript)),
            reinterpret_cast<const void*>(&StandInRunScript));
    standIn(0x006234FE, reinterpret_cast<const void*>(&StandInChatSprintf));

    // THUG2's layout: colours (signed: 0xFF is 1), names and texts up to their sizes; the mode off.
    TP::SetPeerModeOverride(0);
    struct Case {
        uint8_t color;
        std::string name;
        std::string text;
    };
    std::vector<Case> cases = {{0, "Kevin", "hello"},
                               {0xFF, std::string(15, 'N'), std::string(130, 't')},
                               {8, "Observer", ""},
                               {5, "", "no name"},
                               {0x7F, "x", "\\c3coloured"}};
    std::vector<std::string> traces[2];
    for (int run = 0; run < 2; ++run) {
        g_tp_chat.clear();
        for (const Case& c : cases) {
            uint8_t message[0x94] = {};
            message[0] = c.color;
            std::memcpy(message + 1, c.name.data(), c.name.size());
            std::memcpy(message + 17, c.text.data(), c.text.size());
            auto context = Context(76, message, static_cast<uint32_t>(c.text.size() + 18));
            int result = run == 0 ? Original<int(__cdecl*)(Net::MsgHandlerContext*)>(0x00532050)(&context)
                                  : GameNet::HandleChat(&context);
            g_tp_chat.push_back(result == 1 ? "1" : "not 1");
        }
        traces[run] = g_tp_chat;
    }
    Check(traces[0] == traces[1] && !traces[0].empty(), "chat handler 0x00532050: ours = the original (THUG2's layout)");
    if (traces[0] != traces[1])
        for (size_t i = 0; i < traces[0].size() || i < traces[1].size(); ++i)
            Log("  chat %u: original [%s] ours [%s]", static_cast<unsigned>(i),
                i < traces[0].size() ? traces[0][i].c_str() : "-", i < traces[1].size() ? traces[1][i].c_str() : "-");
    // A THUG2 message whose text runs to the message's end without its NUL: ours stops there.
    {
        uint8_t message[0x30];
        std::memset(message, 'q', sizeof(message));
        message[0] = 1;
        message[16] = '\0';
        g_tp_chat.clear();
        auto context = Context(76, message, 17 + 5);
        GameNet::HandleChat(&context);
        char expected[80];
        std::snprintf(expected, sizeof(expected), "add 1 %08X \\c3%s\\c0 : qqqqq", 0xC4745838u, std::string(15, 'q').c_str());
        Check(g_tp_chat.size() > 2 && g_tp_chat[2] == expected, "chat handler: an unended text stops at the message's end");
    }

    // THUGPRO's layout in the mode; malformed ones dropped (nothing made, nothing run).
    TP::SetPeerModeOverride(1);
    {
        TP::ChatMessage message;
        uint32_t length = TP::EncodeChat(&message, 0xCAFEF00D, 2, "Morten", "gg");
        g_tp_chat.clear();
        auto context = Context(76, &message, length);
        int result = GameNet::HandleChat(&context);
        char add[80], run[64];
        std::snprintf(add, sizeof(add), "add 1 %08X \\c4Morten\\c0 : gg", 0xC4745838u);
        std::snprintf(run, sizeof(run), "run %08X 1 %p 0", 0xCCB19938u, static_cast<void*>(nullptr));
        Check(result == 1 && g_tp_chat == std::vector<std::string>{"alloc", "ctor", add, run, "dtor", "free"},
              "chat handler: THUGPRO's layout shown (colour object + 2, name, text)");
        g_tp_chat.clear();
        context = Context(76, &message, 21);
        int shortResult = GameNet::HandleChat(&context);
        context = Context(76, &message, sizeof(message) + 1);
        int longResult = GameNet::HandleChat(&context);
        Check(shortResult == 1 && longResult == 1 && g_tp_chat.empty(), "chat handler: malformed THUGPRO chat dropped");
    }
    // Sending (a named line: no player info needed): THUGPRO's layout in the mode (machine id, text cut to 127),
    // THUG2's otherwise; the line shown here is what was sent.
    {
        Patch send(0x00459E50, Branch(0xE9, 0x00459E50, &StandInSendToServer));
        Patch sendOurs(AddressOf(&Net::Client::EnqueueMessageToServer),
                       Branch(0xE9, AddressOf(&Net::Client::EnqueueMessageToServer), &StandInSendToServer));
        g_tp_sent.clear();
        g_tp_chat.clear();
        alignas(16) static uint8_t manager[0x40];
        std::memset(manager, 0, sizeof(manager));
        static uint8_t client[16];
        *reinterpret_cast<void**>(manager + 8) = client;
        std::string text(200, 'w');
        GameNet::SendNamedChatMessage(reinterpret_cast<GameNet::Manager*>(manager), "Server", text.c_str());
        GameNet::SendNamedChatMessage(reinterpret_cast<GameNet::Manager*>(manager), "Server", "");
        TP::ChatMessage expected;
        uint32_t length = TP::EncodeChat(&expected, TP::MachineId(), 7, "Server", text.c_str());
        char add[300];
        std::snprintf(add, sizeof(add), "add 1 %08X Server : %s", 0xC4745838u, std::string(127, 'w').c_str());
        Check(g_tp_sent.size() == 1 && g_tp_sent[0] == Hex(&expected, length) + " 76 2 8 client" && length == 149 &&
                  g_tp_chat.size() > 2 && g_tp_chat[2] == add,
              "chat: THUGPRO's layout sent in the mode (machine id, colour, name, text cut to 127; shown as sent)");
        TP::SetPeerModeOverride(0);
        g_tp_sent.clear();
        GameNet::SendNamedChatMessage(reinterpret_cast<GameNet::Manager*>(manager), "Server", "abc");
        uint8_t thug2[0x15] = {7, 'S', 'e', 'r', 'v', 'e', 'r'};
        std::memcpy(thug2 + 17, "abc", 4);
        Check(g_tp_sent.size() == 1 && g_tp_sent[0] == Hex(thug2, sizeof(thug2)) + " 76 2 8 client",
              "chat: THUG2's layout sent with the mode off");
        TP::SetPeerModeOverride(1);
    }
    // The client handlers of the mode (ids, packet flags, data).
    {
        Patch add(AddressOf(&Net::Dispatcher::AddHandler),
                  Branch(0xE9, AddressOf(&Net::Dispatcher::AddHandler), &StandInAddHandler));
        g_tp_handlers.clear();
        static uint8_t skate[4];
        TP::AddClientHandlers(reinterpret_cast<Net::App*>(g_tp_client), skate);
        auto entry = [](uint8_t id, Net::MsgHandlerCode code, uint32_t flags) {
            char line[96];
            std::snprintf(line, sizeof(line), "%d %p %u skate 128", id, reinterpret_cast<void*>(code), flags);
            return std::string(line);
        };
        std::vector<std::string> expected = {entry(155, TP::HandleServerInfo, 0), entry(159, TP::HandleModsAllowed, 0),
                                             entry(156, TP::HandleUpdateTimeOfDay, 2),
                                             entry(157, TP::HandleUpdateWeather, 2),
                                             entry(158, TP::HandleUpdateLevelOption, 2)};
        Check(g_tp_handlers == expected, "thugpro: client handlers 155-159 (flags as THUGPRO's, the skate's data)");
    }
    TP::SetPeerModeOverride(-1);
    for (auto it = patches.rbegin(); it != patches.rend(); ++it)
        delete *it;
    GameVariable<int>(0x006F3614) = savedIndex;
    GameVariable<void* [2]>(0x006F361C)[0] = savedPools[0];
    GameVariable<void* [2]>(0x006F361C)[1] = savedPools[1];
}

void RestoreThugproChat() { TP::SetPeerModeOverride(-1); }

} // namespace

SELFTEST(Early, TestThugproLayouts);
SELFTEST(Early, TestThugproEffects);
SELFTEST(Early, TestThugproHandlers, RestoreThugpro);
SELFTEST(Early, TestThugproChatHandler, RestoreThugproChat);


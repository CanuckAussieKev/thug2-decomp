// Self-tests for the test-automation channel (src/game/front/automation.cpp - our own addition, no original to
// compare with): the command parser, the buttons' bits (as Sio's keyboard sets them), the deny-list (scripts, ids,
// parameters, stand-in menu items with event handlers) and the status file's text, against worked-out values.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "game/core/crc.h"
#include "game/front/automation.h"
#include "game/front/screen_element.h"
#include "game/front/test_view.h"
#include "game/obj/event.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/selftest.h"

namespace {

using namespace Front::Automation;

uint32_t Crc(const char* name) { return Crc::GenerateCRCFromString(name); }

// A line parsed and described again: "" for an error, "-" for an empty line.
std::string Round(const char* line)
{
    Command command;
    std::string error;
    switch (ParseCommand(line, &command, &error)) {
    case ParseResult::Command: return Describe(command);
    case ParseResult::Empty: return "-";
    default: return "";
    }
}

void TestAutomationParse()
{
    struct Case {
        const char* line;
        const char* described;
    };
    const Case cases[] = {
        {"run launch_level_select_menu", "run launch_level_select_menu"},
        {"  RUN  cas100_apply  ", "run cas100_apply"},
        {"run foo a=12 b=-3 c=1.5 d=\"two words\" e=bar flag", "run foo a=12 b=-3 c=1.5 d=\"two words\" e=bar flag"},
        {"run foo e=0x1234ABCD # a comment", "run foo e=0x1234ABCD"},
        {"choose menu_skate_now", "choose menu_skate_now"},
        {"choose 0x1E3E253B 20", "choose 0x1E3E253B"},
        {"Focus menu_options", "focus menu_options"},
        {"press enter", "press x 3"},
        {"press ESC 7", "press circle 7"},
        {"press pause", "press start 3"},
        {"press y", "press triangle 3"},
        {"wait 0", "wait 0"},
        {"wait 120", "wait 120"},
        {"waitfor ready", "waitfor ready"},
        {"waitfor ready 30", "waitfor ready"},
        {"waitfor frontend", "waitfor frontend"},
        {"waitfor ingame 200", "waitfor ingame"},
        {"waitfor level BO", "waitfor level BO"},
        {"waitfor menu level_select_anchor 15", "waitfor menu level_select_anchor"},
        {"waitfor focus 0x0000BEEF", "waitfor focus 0x0000BEEF"},
        {"level thps4_zoo", "level thps4_zoo"},
        {"echo step 2 # done", "echo step 2 # done"},
        {"echo", "echo "},
        {"quit", "quit"},
        {"teleport -4087.3 -390 2386.6", "teleport -4087.3 -390 2386.6"},
        {"Teleport 1 2 3 -177.3", "teleport 1 2 3 -177.3"},
        {"camera -4080.9 -293.7 2523.3 -0.044 -0.311 -0.949", "camera -4080.9 -293.7 2523.3 -0.044 -0.311 -0.949"},
        {"camera OFF", "camera off"},
        {"camera 1 2 3 0 0 -1 0.05 0 -0.5", "camera 1 2 3 0 0 -1 0.05 0 -0.5"},
        {"capture 90", "capture 90"},
        {"capture 600 foun_ramp", "capture 600 foun_ramp"},
        {"capture 2 \"two words\"", "capture 2 two words"},
        {"", "-"},
        {"   ", "-"},
        {"# only a comment", "-"},
        // errors
        {"jump", ""},
        {"run", ""},
        {"run bad-name", ""},
        {"run foo a=", ""},
        {"run foo =3", ""},
        {"run foo a=\"open", ""},
        {"run foo a=b\"c", ""},
        {"run foo 12", ""},
        {"choose", ""},
        {"choose a b", ""},
        {"choose a 0", ""},
        {"choose 0xZZ", ""},
        {"press", ""},
        {"press jump", ""},
        {"press x 0", ""},
        {"press x 601", ""},
        {"wait", ""},
        {"wait -1", ""},
        {"wait 36001", ""},
        {"wait x", ""},
        {"waitfor", ""},
        {"waitfor sunrise", ""},
        {"waitfor level", ""},
        {"waitfor menu", ""},
        {"waitfor ready 0", ""},
        {"waitfor ready 5 6", ""},
        {"waitfor level 0x12", ""},
        {"level", ""},
        {"level a b", ""},
        {"quit now", ""},
        {"teleport", ""},
        {"teleport 1 2", ""},
        {"teleport 1 2 3 4 5", ""},
        {"teleport 1 x 3", ""},
        {"camera 1 2 3 4 5", ""},
        {"camera 1 2 3 4 5 6 7", ""},
        {"camera 1 2 3 4 5 6 7 8 9 10", ""},
        {"camera 1 2 3 0 0 0", ""},
        {"camera on", ""},
        {"capture", ""},
        {"capture 0", ""},
        {"capture 601", ""},
        {"capture 5 ../out", ""},
        {"capture 5 a b", ""},
    };
    for (const Case& c : cases) {
        std::string got = Round(c.line);
        Check(got == c.described, (std::string("automation parse: '") + c.line + "' -> '" + got + "'").c_str());
    }

    // The values and the numbers.
    Command command;
    std::string error;
    bool parsed = ParseCommand("run s i=-7 f=2.25 t=\"a b\" n=name0 flag1", &command, &error) == ParseResult::Command;
    Check(parsed && command.op == Op::Run && command.target == "s" && command.params.size() == 5,
          "automation parse: run's parameters");
    if (parsed && command.params.size() == 5) {
        const Param* p = command.params.data();
        Check(p[0].kind == Param::Integer && p[0].name == "i" && p[0].integer == -7, "automation parse: integer");
        Check(p[1].kind == Param::Float && p[1].name == "f" && p[1].real == 2.25f, "automation parse: float");
        Check(p[2].kind == Param::String && p[2].name == "t" && p[2].text == "a b", "automation parse: string");
        Check(p[3].kind == Param::Name && p[3].name == "n" && p[3].text == "name0", "automation parse: name");
        Check(p[4].kind == Param::Flag && p[4].name.empty() && p[4].text == "flag1", "automation parse: flag");
    }
    Check(ParseCommand("choose menu_net 25", &command, &error) == ParseResult::Command && command.op == Op::Choose &&
              command.seconds == 25 && command.target == "menu_net",
          "automation parse: choose's timeout");
    Check(ParseCommand("focus x", &command, &error) == ParseResult::Command && command.op == Op::Focus &&
              command.seconds == 10,
          "automation parse: focus's default timeout");
    Check(ParseCommand("press down 9", &command, &error) == ParseResult::Command && command.frames == 9 &&
              command.target == "down",
          "automation parse: press's frames");
    Check(ParseCommand("press up", &command, &error) == ParseResult::Command && command.frames == 3,
          "automation parse: press's default frames");
    Check(ParseCommand("waitfor level TR 300", &command, &error) == ParseResult::Command &&
              command.condition == Condition::Level && command.seconds == 300 && command.target == "TR",
          "automation parse: waitfor level's timeout");
    Check(ParseCommand("waitfor ingame", &command, &error) == ParseResult::Command &&
              command.condition == Condition::InGame && command.seconds == 90,
          "automation parse: waitfor's default timeout");
    Check(ParseCommand("wait 45", &command, &error) == ParseResult::Command && command.op == Op::Wait &&
              command.frames == 45,
          "automation parse: wait's frames");
    Check(ParseCommand("teleport 1.5 -2 3 90", &command, &error) == ParseResult::Command && command.op == Op::Teleport &&
              command.numbers.size() == 4 && command.numbers[0] == 1.5f && command.numbers[1] == -2.0f &&
              command.numbers[3] == 90.0f,
          "automation parse: teleport's numbers");
    Check(ParseCommand("capture 120 x", &command, &error) == ParseResult::Command && command.op == Op::Capture &&
              command.frames == 120 && command.target == "x",
          "automation parse: capture's frames and folder");
    Check(ParseCommand("trace 600", &command, &error) == ParseResult::Command && command.op == Op::Trace &&
              command.frames == 600 && Round("trace 600") == "trace 600" &&
              ParseCommand("trace", &command, &error) == ParseResult::Error &&
              ParseCommand("trace 0", &command, &error) == ParseResult::Error,
          "automation parse: trace's frames");
    Check(ParseCommand("logtricks", &command, &error) == ParseResult::Command && command.op == Op::LogTricks &&
              command.frames == 0 && ParseCommand("logtricks 300", &command, &error) == ParseResult::Command &&
              command.frames == 300 && ParseCommand("logtricks 0", &command, &error) == ParseResult::Error,
          "automation parse: logtricks' count");
    Check(ParseCommand("crash Overrun", &command, &error) == ParseResult::Command && command.op == Op::Crash &&
              command.target == "overrun" && Round("crash hang") == "crash hang" &&
              ParseCommand("crash", &command, &error) == ParseResult::Error &&
              ParseCommand("crash nothing", &command, &error) == ParseResult::Error,
          "automation parse: crash's kinds");
    Check(ParseCommand("frobnicate", &command, &error) == ParseResult::Error && !error.empty(),
          "automation parse: an error says why");
}
SELFTEST(Early, TestAutomationParse);

void TestAutomationButtons()
{
    // As Sio's keyboard / controller set them (sdl_input.cpp): byte 2 select L3 R3 start up right down left,
    // byte 3 L2 R2 L1 R1 triangle circle cross square; the pressures 8-19.
    struct Expected {
        const char* name;
        uint8_t byte, bit, pressure;
        bool chooses;
    };
    const Expected expected[] = {
        {"up", 2, 4, 10, false},    {"right", 2, 5, 8, false},    {"down", 2, 6, 11, false},
        {"left", 2, 7, 9, false},   {"x", 3, 6, 14, true},        {"circle", 3, 5, 13, true},
        {"square", 3, 7, 15, true}, {"triangle", 3, 4, 12, true}, {"start", 2, 3, 0, true},
        {"select", 2, 0, 0, true},  {"l1", 3, 2, 16, true},       {"r1", 3, 3, 17, true},
        {"l2", 3, 0, 18, true},     {"r2", 3, 1, 19, true},       {"l3", 2, 1, 0, true},
        {"r3", 2, 2, 0, true},
    };
    for (const Expected& e : expected) {
        const Button* button = FindButton(e.name);
        bool ok = button && std::strcmp(button->name, e.name) == 0 && button->byte == e.byte && button->bit == e.bit &&
                  button->pressure == e.pressure && ButtonChooses(*button) == e.chooses;
        Check(ok, (std::string("automation button ") + e.name).c_str());
    }
    const char* const aliases[][2] = {{"enter", "x"},    {"CROSS", "x"},    {"choose", "x"},  {"a", "x"},
                                      {"esc", "circle"}, {"back", "circle"}, {"b", "circle"}, {"Y", "triangle"},
                                      {"pause", "start"}, {"Up", "up"}};
    for (const auto& alias : aliases) {
        const Button* button = FindButton(alias[0]);
        Check(button && std::strcmp(button->name, alias[1]) == 0,
              (std::string("automation button alias ") + alias[0]).c_str());
    }
    Check(!FindButton("jump") && !FindButton("") && !FindButton("l4"), "automation: no such buttons");
}
SELFTEST(Early, TestAutomationButtons);

void TestAutomationDenyList()
{
    // Scripts: the network files' (and the gateways), online-looking names - and the offline menus' scripts not.
    const char* const denied[] = {"select_network_play_multi", "select_network_play", "change_gamemode_net",
                                  "select_internet_play",     "select_lan_play",     "chose_internet",
                                  "profile_connect",          "create_lobby_list_menu", "launch_profile_menu",
                                  "chosen_host_game",         "spawned_chosen_host_game", "select_xbox_multiplayer",
                                  "create_network_select_games_menu", "start_internet_game", "log_in_profile",
                                  "join_chosen",              "create_online_stats_menu", "launch_homie_list",
                                  "my_online_test",           "GoInternetNow",       "SERVER_start",
                                  "x_net",                    "net",                 "please_join_x",
                                  "gamespyconnect",           "my_lan_game"};
    for (const char* name : denied)
        Check(ScriptDenied(name), (std::string("automation: deny ") + name).c_str());
    const char* const allowed[] = {"launch_level_select_menu", "main_menu_choose",   "launch_select_skater_menu",
                                   "ss_menu_exit",             "level_select_change_level", "main_menu_skate_now",
                                   "create_main_menu",         "change_gamemode_singlesession", "cas100_apply",
                                   "mark_first_input_received", "create_setup_options_menu", "DrawInstructionLine",
                                   "planet_x",                 "cabinet_open",       "nets",
                                   "dialog_box_exit",          "generic_menu_pad_back", "EnableSun"};
    for (const char* name : allowed)
        Check(!ScriptDenied(name), (std::string("automation: allow ") + name).c_str());

    // Ids, by checksum (with or without the name).
    Check(IdDenied(Crc("menu_net"), "") && IdDenied(Crc("mm_net_play"), "") && IdDenied(Crc("mm_multi_play"), "") &&
              IdDenied(Crc("menu_network_options"), ""),
          "automation: denied ids");
    Check(IdDenied(Crc("whatever"), "my_lobby_item"), "automation: an online-looking id name");
    Check(!IdDenied(Crc("menu_skate_now"), "menu_skate_now") && !IdDenied(Crc("menu_singlesess"), "menu_singlesess") &&
              !IdDenied(Crc("main_menu"), "main_menu") && !IdDenied(Crc("current_menu"), "current_menu") &&
              !IdDenied(Crc("ss_play_level"), "ss_play_level") && !IdDenied(Crc("menu_options"), ""),
          "automation: allowed ids");
    Check(ChecksumDenied(Crc("select_internet_play")) && ChecksumDenied(Crc("mm_net_play")) &&
              !ChecksumDenied(Crc("launch_level_select_menu")),
          "automation: denied checksums");

    // Whole commands.
    struct Case {
        const char* line;
        bool refused;
    };
    const Case cases[] = {
        {"run launch_level_select_menu", false},
        {"run select_network_play_multi", true},
        {"run gamemode_options_menu_exit new_menu_script=select_internet_play", true},
        {"run gamemode_options_menu_exit new_menu_script=create_gamemode_options_menu", false},
        {"run foo change_gamemode=change_gamemode_net", true},
        {"run foo online", true},
        {"run foo name=\"select_lan_play\"", true},
        {"run foo internet_mode=1", true},
        // (named after a denied script, without an online word: by the table only)
        {"run foo select_xbox_multiplayer", true},
        {"run foo log_in_profile=1", true},
        {"run foo text=\"hello\" count=2", false},
        {"choose menu_net", true},
        {"choose mm_net_play", true},
        {"focus mm_multi_play", true},
        {"choose menu_singlesess", false},
        {"choose ss_play_level", false},
        {"press x", false},
        {"level BO", false},
        {"waitfor menu menu_net", false},
    };
    for (const Case& c : cases) {
        Command command;
        std::string error;
        bool parsed = ParseCommand(c.line, &command, &error) == ParseResult::Command;
        Check(parsed && RefusalByName(command).empty() != c.refused, (std::string("automation refusal: ") + c.line).c_str());
    }
    char hex[24];
    std::snprintf(hex, sizeof(hex), "choose 0x%08X", Crc("menu_net"));
    Command command;
    std::string error;
    Check(ParseCommand(hex, &command, &error) == ParseResult::Command && !RefusalByName(command).empty(),
          "automation refusal: menu_net by its checksum");

    // Names for the status.
    Check(NameOf(Crc("menu_skate_now")) == "menu_skate_now" && NameOf(Crc("freeskate")) == "freeskate" &&
              NameOf(Crc("Load_BO")) == "Load_BO" && NameOf(0x00000001u) == "0x00000001",
          "automation: names");
}
SELFTEST(Early, TestAutomationDenyList);

// --- parameters and menu items with event handlers (stand-ins: no game memory) -------------------------------------

Script::CComponent MakeComponent(Script::ESymbolType type, uint32_t name, uint32_t value, Script::CComponent* next)
{
    Script::CComponent component = {};
    component.typeAndFlag = static_cast<uint8_t>(type << 1);
    component.name = name;
    component.raw = value;
    component.next = next;
    return component;
}

void TestAutomationParams()
{
    using namespace Script;
    // {new_menu_script=<x> change_gamemode=<y> hold_camera}
    CComponent hold = MakeComponent(ESYMBOLTYPE_NAME, 0, Crc("hold_camera"), nullptr);
    CComponent mode = MakeComponent(ESYMBOLTYPE_NAME, Crc("change_gamemode"), Crc("change_gamemode_singlesession"), &hold);
    CComponent menu = MakeComponent(ESYMBOLTYPE_NAME, Crc("new_menu_script"), Crc("launch_select_skater_menu"), &mode);
    CStruct params = {};
    params.m_head = &menu;
    Check(!ParamsDenied(&params), "automation params: free skate's");
    Check(!ParamsDenied(nullptr), "automation params: none");
    mode.raw = Crc("change_gamemode_net");
    Check(ParamsDenied(&params), "automation params: change_gamemode_net");
    mode.raw = Crc("change_gamemode_singlesession");
    hold.raw = Crc("select_lan_play");
    Check(ParamsDenied(&params), "automation params: a flag");
    hold.raw = Crc("hold_camera");
    hold.name = Crc("join_chosen");
    hold.typeAndFlag = static_cast<uint8_t>(ESYMBOLTYPE_INTEGER << 1);
    Check(ParamsDenied(&params), "automation params: a component's name");
    hold.name = 0;
    // An integer whose value happens to be a denied checksum isn't a name.
    hold.raw = Crc("select_lan_play");
    Check(!ParamsDenied(&params), "automation params: an integer isn't a name");

    // Nested: {pad_choose_params={new_menu_script=select_internet_play}}
    CComponent inner = MakeComponent(ESYMBOLTYPE_NAME, Crc("new_menu_script"), Crc("select_internet_play"), nullptr);
    CStruct innerStruct = {};
    innerStruct.m_head = &inner;
    CComponent outer = MakeComponent(ESYMBOLTYPE_STRUCTURE, Crc("pad_choose_params"),
                                     reinterpret_cast<uint32_t>(&innerStruct), nullptr);
    CStruct outerStruct = {};
    outerStruct.m_head = &outer;
    Check(ParamsDenied(&outerStruct), "automation params: in a structure");
    inner.raw = Crc("launch_level_select_menu");
    Check(!ParamsDenied(&outerStruct), "automation params: in a structure, allowed");

    // An array of names: [launch_level_select_menu chose_internet]
    uint32_t elements[2] = {Crc("launch_level_select_menu"), Crc("chose_internet")};
    CArray array = {};
    array.m_type = ESYMBOLTYPE_NAME;
    array.m_size = 2;
    array.m_elements = elements;
    CComponent list = MakeComponent(ESYMBOLTYPE_ARRAY, Crc("scripts"), reinterpret_cast<uint32_t>(&array), nullptr);
    CStruct listStruct = {};
    listStruct.m_head = &list;
    Check(ParamsDenied(&listStruct), "automation params: in an array");
    elements[1] = Crc("ss_menu_exit");
    Check(!ParamsDenied(&listStruct), "automation params: in an array, allowed");
}
SELFTEST(Early, TestAutomationParams);

// A stand-in screen element (zeroed) with a script and its event handler table.
struct StandIn {
    uint8_t element[sizeof(Front::CScreenElement)];
    uint8_t script[sizeof(Script::CScript)];
    Obj::CEventHandlerTable table;
    Obj::CEventHandlerTable::Entry entries[3];

    Front::CScreenElement* Element() { return reinterpret_cast<Front::CScreenElement*>(element); }
    void Init(uint32_t id)
    {
        std::memset(this, 0, sizeof(*this));
        Element()->m_id = id;
        Element()->m_script = reinterpret_cast<Script::CScript*>(script);
        reinterpret_cast<Script::CScript*>(script)->m_eventHandlers = &table;
        table.m_entries = entries;
    }
    void Handler(uint32_t type, uint32_t name, Script::CStruct* params)
    {
        Obj::CEventHandlerTable::Entry& entry = entries[table.m_count++];
        entry.type = type;
        entry.script = name;
        entry.params = params;
    }
};

void TestAutomationElements()
{
    using namespace Script;
    static StandIn item, menu, anchor;
    const uint32_t padChoose = Crc("pad_choose"), padBack = Crc("pad_back"), focus = Crc("focus");

    // MULTIPLAYER: pad_choose main_menu_choose {new_menu_script=select_network_play_multi ...}
    CComponent mode = MakeComponent(ESYMBOLTYPE_NAME, Crc("change_gamemode"), Crc("change_gamemode_net"), nullptr);
    CComponent next = MakeComponent(ESYMBOLTYPE_NAME, Crc("new_menu_script"), Crc("select_network_play_multi"), &mode);
    CStruct params = {};
    params.m_head = &next;
    item.Init(Crc("some_item"));
    item.Handler(focus, Crc("main_menu_focus"), nullptr);
    item.Handler(padChoose, Crc("main_menu_choose"), &params);
    Check(!ElementRefusal(item.Element()).empty(), "automation element: MULTIPLAYER's handler");
    next.raw = Crc("create_gamemode_options_menu");
    mode.raw = Crc("change_gamemode_career");
    Check(ElementRefusal(item.Element()).empty(), "automation element: STORY MODE's handler");
    // The handler script itself.
    item.entries[1].script = Crc("chose_internet");
    Check(!ElementRefusal(item.Element()).empty(), "automation element: an online handler script");
    item.entries[1].script = Obj::kNullScript; // (a removed entry isn't looked at)
    next.raw = Crc("select_internet_play");
    Check(ElementRefusal(item.Element()).empty(), "automation element: a removed handler");
    next.raw = Crc("create_gamemode_options_menu");
    item.entries[1].script = Crc("main_menu_choose");
    // Any handler of the item counts (a focus handler naming a network script too).
    item.entries[0].script = Crc("create_xbox_online_menu");
    Check(!ElementRefusal(item.Element()).empty(), "automation element: any handler of the item");
    item.entries[0].script = Crc("main_menu_focus");
    // Its id.
    item.Element()->m_id = Crc("mm_net_play");
    Check(!ElementRefusal(item.Element()).empty(), "automation element: a denied id");
    item.Element()->m_id = Crc("some_item");

    // Its menus: only their pad_choose / pad_start handlers.
    menu.Init(Crc("some_menu"));
    anchor.Init(Crc("some_anchor"));
    item.Element()->mp_parent.mp_object = menu.Element();
    menu.Element()->mp_parent.mp_object = anchor.Element();
    anchor.Handler(padBack, Crc("back_from_internet_menu"), nullptr);
    Check(ElementRefusal(item.Element()).empty(), "automation element: a menu's pad_back handler doesn't count");
    anchor.Handler(padChoose, Crc("join_chosen"), nullptr);
    Check(!ElementRefusal(item.Element()).empty(), "automation element: a menu's pad_choose handler");
    anchor.entries[1].type = Crc("pad_start");
    Check(!ElementRefusal(item.Element()).empty(), "automation element: a menu's pad_start handler");
    anchor.entries[1].script = Crc("ss_menu_exit");
    Check(ElementRefusal(item.Element()).empty(), "automation element: a menu's allowed handler");
    item.Element()->mp_parent.mp_object = nullptr;
    menu.Element()->mp_parent.mp_object = nullptr;
}
SELFTEST(Early, TestAutomationElements);

void TestAutomationStatus()
{
    Status status = {};
    status.frames = 1234;
    status.ready = true;
    status.mainMenu = false;
    status.paused = true;
    status.loading = false;
    status.where = "level";
    status.level = "BO";
    status.gameMode = "freeskate";
    status.focus = {"main_vmenu", "0x0000BEEF"};
    status.selected = {};
    status.linesRead = 5;
    status.linesDone = 4;
    status.queued = 1;
    status.current = "waitfor menu x";
    status.last = "echo say \"hi\"\\";
    status.result = std::string("a\tb") + char(1);
    status.errors = 2;
    status.refusals = 3;
    const char* expected = "{\n"
                           "  \"frames\": 1234,\n"
                           "  \"ready\": true,\n"
                           "  \"main_menu\": false,\n"
                           "  \"where\": \"level\",\n"
                           "  \"paused\": true,\n"
                           "  \"loading\": false,\n"
                           "  \"level\": \"BO\",\n"
                           "  \"game_mode\": \"freeskate\",\n"
                           "  \"focus\": [\"main_vmenu\", \"0x0000BEEF\"],\n"
                           "  \"selected\": [],\n"
                           "  \"lines_read\": 5,\n"
                           "  \"lines_done\": 4,\n"
                           "  \"queued\": 1,\n"
                           "  \"current\": \"waitfor menu x\",\n"
                           "  \"last\": \"echo say \\\"hi\\\"\\\\\",\n"
                           "  \"result\": \"a\\u0009b\\u0001\",\n"
                           "  \"errors\": 2,\n"
                           "  \"refusals\": 3\n"
                           "}\n";
    std::string got = StatusJson(status);
    Check(got == expected, ("automation status: " + got).c_str());
}
SELFTEST(Early, TestAutomationStatus);

// The test views' maths (game/front/test_view.h): a camera's rows from a view direction, a standing skater's from a
// heading - worked out by hand.
void TestTestViewRows()
{
    using Front::TestView::Vec3;
    auto near = [](Vec3 a, Vec3 b) {
        return std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.y - b.y) < 1e-5f && std::fabs(a.z - b.z) < 1e-5f;
    };
    Vec3 right, up, back;
    // Looking along +z: back -z, right up x back = (0,1,0) x (0,0,-1) = (-1, 0, 0), up (0, 1, 0).
    Check(Front::TestView::CameraRows({0.0f, 0.0f, 2.0f}, &right, &up, &back) && near(back, {0, 0, -1}) &&
              near(right, {-1, 0, 0}) && near(up, {0, 1, 0}),
          "test view: camera along +z");
    // Along +x: back -x, right (0,1,0) x (-1,0,0) = (0, 0, 1).
    Check(Front::TestView::CameraRows({3.0f, 0.0f, 0.0f}, &right, &up, &back) && near(back, {-1, 0, 0}) &&
              near(right, {0, 0, 1}) && near(up, {0, 1, 0}),
          "test view: camera along +x");
    // Down 45 degrees along -z: back (0, 0.7071, 0.7071), right (1, 0, 0), up back x right = (0, 0.7071, -0.7071).
    const float h = 0.70710678f;
    Check(Front::TestView::CameraRows({0.0f, -1.0f, -1.0f}, &right, &up, &back) && near(back, {0, h, h}) &&
              near(right, {1, 0, 0}) && near(up, {0, h, -h}),
          "test view: camera down along -z");
    Check(!Front::TestView::CameraRows({0.0f, 0.0f, 0.0f}, &right, &up, &back), "test view: a zero direction");
    Check(!Front::TestView::CameraRows({0.0f, -5.0f, 0.0f}, &right, &up, &back), "test view: straight down");
    Vec3 at;
    Front::TestView::StandingRows(0.0f, &right, &up, &at);
    Check(near(right, {1, 0, 0}) && near(up, {0, 1, 0}) && near(at, {0, 0, 1}), "test view: heading 0 (identity)");
    Front::TestView::StandingRows(90.0f, &right, &up, &at);
    Check(near(right, {0, 0, -1}) && near(at, {1, 0, 0}), "test view: heading 90 (+x)");
    Front::TestView::StandingRows(-180.0f, &right, &up, &at);
    Check(near(right, {-1, 0, 0}) && near(at, {0, 0, -1}), "test view: heading -180 (-z)");
}
SELFTEST(Early, TestTestViewRows);

} // namespace

// The test-automation channel (game/front/automation.h). NOT ORIGINAL.

#include "game/front/automation.h"

#include <share.h>
#include <windows.h>

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <unordered_map>
#include <vector>

#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/front/mouse_menus.h"
#include "game/front/new_frontend.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/start_level.h"
#include "game/front/test_view.h"
#include "game/gfx/device.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/loading_bar_cfuncs.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_manager.h"
#include "game/obj/event.h"
#include "game/lst/hashtable.h"
#include "game/obj/object.h"
#include "game/obj/trick_objects.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/sys/mlp.h"
#include "game/sys/screenshot.h"
#include "game/sys/crash.h"
#include "game/sys/log.h"

namespace Front::Automation {

namespace {

#include "game/front/automation_tables.inc"

constexpr uint32_t kSystem = 0x36B2EE74;       // "system" (the menus' pad events' target and source)
constexpr uint32_t kFocusEvent = 0x9D3FB516;   // "focus"
constexpr uint32_t kPadChoose = Mdl::FrontEnd::kPadChoose;
constexpr uint32_t kPadStart = Mdl::FrontEnd::kPadStart;
constexpr uint32_t kSkater = 0x5B8AB877;       // Skater (the local skater's alias)
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu (the skate module's level in the front end)

constexpr int kDefaultPressFrames = 3;
constexpr int kReleaseFrames = 3;
constexpr int kPressWaitSeconds = 5; // press: at most this long for the menus to take pad events
constexpr int kDefaultChooseSeconds = 10;
constexpr int kDefaultWaitSeconds = 90;
constexpr int kSettleFrames = 60;      // waitfor level / ingame: how long the condition must hold
constexpr int kPartialLineFrames = 30;
constexpr int kMaxCaptureFrames = Sys::Screenshot::kMaxSeriesFrames; // a last line without its end of line, once the file has been still this long
constexpr int kMaxTraceFrames = 36000;   // trace: ten minutes at 60 frames a second
constexpr uint32_t kSkaterStateComponent = 0x43686585; // SkaterState
constexpr uint32_t kSkaterScoreComponent = 0xD262802F; // SkaterScore
constexpr uint32_t kSkaterCorePhysicsComponent = 0x5BD63B29; // SkaterCorePhysics

uint32_t Checksum(const std::string& name) { return Crc::GenerateCRCFromString(name.c_str()); }

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool IsIdentifier(const std::string& text)
{
    if (text.empty() || std::isdigit(static_cast<unsigned char>(text[0])))
        return false;
    for (char c : text)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    return true;
}

bool IsHex(const std::string& text)
{
    if (text.size() < 3 || text.size() > 10 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X'))
        return false;
    for (size_t i = 2; i < text.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(text[i])))
            return false;
    return true;
}

bool IsInteger(const std::string& text)
{
    size_t start = text[0] == '-' ? 1 : 0;
    if (start >= text.size())
        return false;
    for (size_t i = start; i < text.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(text[i])))
            return false;
    return true;
}

bool IsFloat(const std::string& text)
{
    size_t start = text[0] == '-' ? 1 : 0;
    bool digits = false, point = false;
    for (size_t i = start; i < text.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(text[i])))
            digits = true;
        else if (text[i] == '.' && !point)
            point = true;
        else
            return false;
    }
    return digits && point;
}

// An element id: a name or 0x<hex>.
bool IsId(const std::string& text) { return IsHex(text) || IsIdentifier(text); }

// A positive number that fits (false for anything else).
bool ReadCount(const std::string& text, int low, int high, int* out)
{
    if (!IsInteger(text) || text.size() > 9)
        return false;
    int value = std::atoi(text.c_str());
    if (value < low || value > high)
        return false;
    *out = value;
    return true;
}

// tokens[first...] as numbers (integers or decimals, signed); false if any isn't one.
bool ReadNumbers(const std::vector<std::string>& tokens, size_t first, std::vector<float>* out)
{
    out->clear();
    for (size_t i = first; i < tokens.size(); ++i) {
        if (!IsInteger(tokens[i]) && !IsFloat(tokens[i]))
            return false;
        out->push_back(static_cast<float>(std::atof(tokens[i].c_str())));
    }
    return true;
}

// The words of a line: split at spaces, a quoted part kept whole (quotes included); `#` outside quotes ends it.
bool Tokenize(const std::string& line, std::vector<std::string>* tokens, std::string* error)
{
    std::string token;
    bool quoted = false, any = false;
    for (char c : line) {
        if (quoted) {
            token += c;
            if (c == '"')
                quoted = false;
            continue;
        }
        if (c == '#')
            break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (any)
                tokens->push_back(token);
            token.clear();
            any = false;
            continue;
        }
        token += c;
        any = true;
        if (c == '"')
            quoted = true;
    }
    if (quoted) {
        *error = "a quote isn't closed";
        return false;
    }
    if (any)
        tokens->push_back(token);
    return true;
}

// The text after the first word (echo's), comments included.
std::string Rest(const std::string& line)
{
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos)
        return {};
    size_t gap = line.find_first_of(" \t", start);
    if (gap == std::string::npos)
        return {};
    size_t text = line.find_first_not_of(" \t", gap);
    if (text == std::string::npos)
        return {};
    size_t end = line.find_last_not_of(" \t\r\n");
    return line.substr(text, end + 1 - text);
}

bool ParseParam(const std::string& token, Param* out, std::string* error)
{
    size_t equals = token.find('=');
    if (equals == std::string::npos) {
        if (!IsIdentifier(token)) {
            *error = "not a flag: " + token;
            return false;
        }
        *out = {Param::Flag, "", token, 0, 0.0f};
        return true;
    }
    std::string name = token.substr(0, equals), value = token.substr(equals + 1);
    if (!IsIdentifier(name) || value.empty()) {
        *error = "not a parameter: " + token;
        return false;
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        *out = {Param::String, name, value.substr(1, value.size() - 2), 0, 0.0f};
    else if (value.find('"') != std::string::npos)
        return *error = "not a parameter: " + token, false;
    else if (IsInteger(value) && value.size() <= 10)
        *out = {Param::Integer, name, value, std::atoi(value.c_str()), 0.0f};
    else if (IsFloat(value))
        *out = {Param::Float, name, value, 0, static_cast<float>(std::atof(value.c_str()))};
    else if (IsId(value))
        *out = {Param::Name, name, value, 0, 0.0f};
    else
        return *error = "not a value: " + value, false;
    return true;
}

const Button kButtons[] = {
    {"up", 2, 4, 10},       {"right", 2, 5, 8},     {"down", 2, 6, 11},      {"left", 2, 7, 9},
    {"x", 3, 6, 14},        {"circle", 3, 5, 13},   {"square", 3, 7, 15},    {"triangle", 3, 4, 12},
    {"start", 2, 3, 0},     {"select", 2, 0, 0},    {"l1", 3, 2, 16},        {"r1", 3, 3, 17},
    {"l2", 3, 0, 18},       {"r2", 3, 1, 19},       {"l3", 2, 1, 0},         {"r3", 2, 2, 0},
};
const struct {
    const char* alias;
    const char* name;
} kButtonAliases[] = {
    {"cross", "x"}, {"choose", "x"}, {"enter", "x"}, {"a", "x"},   {"back", "circle"}, {"esc", "circle"},
    {"b", "circle"}, {"y", "triangle"}, {"pause", "start"},
};

// --- names ------------------------------------------------------------------------------------------------------

std::unordered_map<uint32_t, std::string>& Names()
{
    static std::unordered_map<uint32_t, std::string>* names = [] {
        auto* table = new std::unordered_map<uint32_t, std::string>;
        auto add = [table](const char* name) { table->emplace(Crc::GenerateCRCFromString(name), name); };
        for (const char* name : kKnownNames)
            add(name);
        for (const char* name : kDeniedScripts)
            add(name);
        for (const char* name : {"Load_MainMenu", "mainmenu", "main_vmenu"})
            add(name);
        return table;
    }();
    return *names;
}

void Remember(const std::string& name)
{
    if (IsIdentifier(name))
        Names().emplace(Checksum(name), name);
}

uint32_t IdChecksum(const std::string& id)
{
    if (IsHex(id))
        return static_cast<uint32_t>(std::strtoul(id.c_str() + 2, nullptr, 16));
    return Checksum(id);
}

const std::unordered_map<uint32_t, bool>& DeniedChecksums()
{
    static std::unordered_map<uint32_t, bool>* denied = [] {
        auto* table = new std::unordered_map<uint32_t, bool>;
        for (const char* name : kDeniedScripts)
            (*table)[Crc::GenerateCRCFromString(name)] = true;
        for (const char* name : kDeniedIds)
            (*table)[Crc::GenerateCRCFromString(name)] = true;
        return table;
    }();
    return *denied;
}

std::string JsonString(const std::string& text)
{
    std::string out = "\"";
    for (char c : text) {
        unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u < 0x20 || u >= 0x7F) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", u);
            out += escaped;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

std::string JsonList(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i)
        out += (i ? ", " : "") + JsonString(items[i]);
    return out + "]";
}

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

ParseResult ParseCommand(const std::string& line, Command* out, std::string* error)
{
    std::vector<std::string> tokens;
    if (!Tokenize(line, &tokens, error))
        return ParseResult::Error;
    if (tokens.empty())
        return ParseResult::Empty;
    Command command = {Op::Echo, "", {}, Condition::None, 0, 0};
    std::string keyword = Lower(tokens[0]);
    size_t count = tokens.size();
    auto fail = [error](const std::string& why) {
        *error = why;
        return ParseResult::Error;
    };
    if (keyword == "run") {
        if (count < 2 || !IsIdentifier(tokens[1]))
            return fail("run <script> [name=value | flag]...");
        command.op = Op::Run;
        command.target = tokens[1];
        for (size_t i = 2; i < count; ++i) {
            Param param;
            if (!ParseParam(tokens[i], &param, error))
                return ParseResult::Error;
            command.params.push_back(param);
        }
    } else if (keyword == "choose" || keyword == "focus") {
        command.op = keyword == "choose" ? Op::Choose : Op::Focus;
        command.seconds = kDefaultChooseSeconds;
        if (count < 2 || count > 3 || !IsId(tokens[1]) || (count == 3 && !ReadCount(tokens[2], 1, 3600, &command.seconds)))
            return fail(keyword + " <id> [seconds]");
        command.target = tokens[1];
    } else if (keyword == "hold") {
        // (test tooling: press's button held without waiting - the next commands run while it's down)
        command.op = Op::Hold;
        command.frames = kDefaultPressFrames;
        if (count < 2 || count > 3 || (count == 3 && !ReadCount(tokens[2], 1, 3600, &command.frames)))
            return fail("hold <button> [frames]");
        const Button* button = FindButton(tokens[1]);
        if (!button)
            return fail("no such button: " + tokens[1]);
        command.target = button->name;
    } else if (keyword == "press") {
        command.op = Op::Press;
        command.frames = kDefaultPressFrames;
        if (count < 2 || count > 3 || (count == 3 && !ReadCount(tokens[2], 1, 600, &command.frames)))
            return fail("press <button> [frames]");
        const Button* button = FindButton(tokens[1]);
        if (!button)
            return fail("no such button: " + tokens[1]);
        command.target = button->name;
    } else if (keyword == "wait") {
        command.op = Op::Wait;
        if (count != 2 || !ReadCount(tokens[1], 0, 36000, &command.frames))
            return fail("wait <frames>");
    } else if (keyword == "waitfor") {
        command.op = Op::WaitFor;
        command.seconds = kDefaultWaitSeconds;
        if (count < 2)
            return fail("waitfor ready | frontend | ingame | level <name> | menu <id> | focus <id> [seconds]");
        std::string what = Lower(tokens[1]);
        size_t next = 2;
        if (what == "fe") {
            command.condition = Condition::NewFrontend;
            if (count < 3 || !IsIdentifier(tokens[2]))
                return fail("waitfor fe <screen> [seconds]");
            command.target = Lower(tokens[2]);
            next = 3;
        } else if (what == "ready")
            command.condition = Condition::Ready;
        else if (what == "frontend")
            command.condition = Condition::FrontEnd;
        else if (what == "ingame")
            command.condition = Condition::InGame;
        else if (what == "level" || what == "menu" || what == "focus") {
            command.condition = what == "level" ? Condition::Level : what == "menu" ? Condition::Menu : Condition::Focus;
            if (count < 3 || !(command.condition == Condition::Level ? IsIdentifier(tokens[2]) : IsId(tokens[2])))
                return fail("waitfor " + what + " <" + (what == "level" ? "name" : "id") + "> [seconds]");
            command.target = tokens[2];
            next = 3;
        } else {
            return fail("no such condition: " + tokens[1]);
        }
        if (count > next + 1 || (count == next + 1 && !ReadCount(tokens[next], 1, 3600, &command.seconds)))
            return fail("waitfor: a timeout is whole seconds (1-3600)");
    } else if (keyword == "level") {
        command.op = Op::Level;
        if (count != 2 || !IsIdentifier(tokens[1]))
            return fail("level <name>");
        command.target = tokens[1];
    } else if (keyword == "teleport") {
        command.op = Op::Teleport;
        // (a last word `board`: the skater put on its board instead of standing - TestView::Teleport's onBoard)
        std::vector<std::string> words = tokens;
        if (words.size() >= 5 && Lower(words.back()) == "board") {
            command.target = "board";
            words.pop_back();
        }
        if ((words.size() != 4 && words.size() != 5) || !ReadNumbers(words, 1, &command.numbers))
            return fail("teleport <x> <y> <z> [heading] [board]");
    } else if (keyword == "camera") {
        command.op = Op::Camera;
        if (count == 2 && Lower(tokens[1]) == "off")
            command.target = "off";
        else if ((count != 7 && count != 10) || !ReadNumbers(tokens, 1, &command.numbers))
            return fail("camera <x> <y> <z> <dx> <dy> <dz> [<drift x> <drift y> <drift z>] | camera off");
        else if (command.numbers[3] == 0.0f && command.numbers[4] == 0.0f && command.numbers[5] == 0.0f)
            return fail("camera: the direction is zero");
    } else if (keyword == "capture") {
        command.op = Op::Capture;
        if (count < 2 || count > 3 || !ReadCount(tokens[1], 1, kMaxCaptureFrames, &command.frames))
            return fail("capture <frames 1-" + std::to_string(kMaxCaptureFrames) + "> [folder]");
        if (count == 3) {
            std::string folder = tokens[2];
            if (folder.size() >= 2 && folder.front() == '"' && folder.back() == '"')
                folder = folder.substr(1, folder.size() - 2);
            if (folder.empty() || folder.find("..") != std::string::npos)
                return fail("capture: a folder name (no ..)");
            command.target = folder;
        }
    } else if (keyword == "trace") {
        command.op = Op::Trace;
        if (count != 2 || !ReadCount(tokens[1], 1, kMaxTraceFrames, &command.frames))
            return fail("trace <frames 1-" + std::to_string(kMaxTraceFrames) + ">");
    } else if (keyword == "logtricks") {
        command.op = Op::LogTricks;
        command.frames = 0; // (every trick object)
        if (count > 2 || (count == 2 && !ReadCount(tokens[1], 1, 100000, &command.frames)))
            return fail("logtricks [count]");
    } else if (keyword == "fe") {
        command.op = Op::Frontend;
        command.seconds = 10;
        command.target = Rest(line);
        if (count < 2)
            return fail("fe select <screen>/<item> | fe focus <screen>/<item> | fe back | fe press <input>");
        // (select / focus: a last number is the timeout - the front end ignores it)
        if (count == 4 && (Lower(tokens[1]) == "select" || Lower(tokens[1]) == "focus") &&
            !ReadCount(tokens[3], 1, 3600, &command.seconds))
            return fail("fe " + Lower(tokens[1]) + ": a timeout is whole seconds (1-3600)");
    } else if (keyword == "echo") {
        command.op = Op::Echo;
        command.target = Rest(line);
    } else if (keyword == "crash") {
        command.op = Op::Crash;
        TestFault fault;
        if (count != 2 || !ParseTestFault(tokens[1], &fault))
            return fail(std::string("crash <kind>: ") + TestFaultNames());
        command.target = TestFaultName(fault);
    } else if (keyword == "quit") {
        command.op = Op::Quit;
        if (count != 1)
            return fail("quit takes nothing");
    } else {
        return fail("no such command: " + tokens[0]);
    }
    *out = command;
    return ParseResult::Command;
}

const Button* FindButton(const std::string& name)
{
    std::string low = Lower(name);
    for (const auto& alias : kButtonAliases)
        if (low == alias.alias)
            low = alias.name;
    for (const Button& button : kButtons)
        if (low == button.name)
            return &button;
    return nullptr;
}

bool ButtonChooses(const Button& button) { return !(button.byte == 2 && button.bit >= 4); }

bool NameLooksOnline(const std::string& name)
{
    static const char* const kWords[] = {"net",     "online",   "internet", "lobby",  "lobbies", "login",
                                         "logon",   "buddy",    "buddies",  "homie",  "homies",  "gamespy",
                                         "network", "server",   "servers",  "join",   "host",    "lan"};
    static const char* const kParts[] = {"internet", "lobby", "gamespy", "network", "login"};
    std::string low = Lower(name);
    for (const char* part : kParts)
        if (low.find(part) != std::string::npos)
            return true;
    size_t start = 0;
    while (start <= low.size()) {
        size_t end = low.find('_', start);
        if (end == std::string::npos)
            end = low.size();
        std::string word = low.substr(start, end - start);
        for (const char* online : kWords)
            if (word == online)
                return true;
        start = end + 1;
    }
    return false;
}

bool ScriptDenied(const std::string& name) { return NameLooksOnline(name) || ChecksumDenied(Checksum(name)); }

bool ChecksumDenied(uint32_t checksum) { return DeniedChecksums().count(checksum) != 0; }

bool IdDenied(uint32_t id, const std::string& name)
{
    return ChecksumDenied(id) || (!name.empty() && NameLooksOnline(name));
}

std::string RefusalByName(const Command& command)
{
    switch (command.op) {
    case Op::Run:
        if (ScriptDenied(command.target))
            return "the script " + command.target + " is online / network";
        for (const Param& param : command.params) {
            const std::string& name = param.kind == Param::Flag ? param.text : param.name;
            if (NameLooksOnline(name) || ChecksumDenied(Checksum(name)))
                return "the parameter " + name + " is online / network";
            if ((param.kind == Param::Name || param.kind == Param::String) && ScriptDenied(param.text))
                return "the value " + param.text + " is online / network";
        }
        return {};
    case Op::Choose:
    case Op::Focus:
        if (IdDenied(IdChecksum(command.target), IsHex(command.target) ? "" : command.target))
            return "the element " + command.target + " is online / network";
        return {};
    default:
        return {};
    }
}

std::string NameOf(uint32_t checksum)
{
    auto found = Names().find(checksum);
    if (found != Names().end())
        return found->second;
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", checksum);
    return text;
}

std::string Describe(const Command& command)
{
    switch (command.op) {
    case Op::Run: {
        std::string text = "run " + command.target;
        for (const Param& param : command.params) {
            text += " ";
            if (param.kind == Param::Flag)
                text += param.text;
            else
                text += param.name + "=" + (param.kind == Param::String ? "\"" + param.text + "\"" : param.text);
        }
        return text;
    }
    case Op::Choose: return "choose " + command.target;
    case Op::Focus: return "focus " + command.target;
    case Op::Press: return "press " + command.target + " " + std::to_string(command.frames);
    case Op::Hold: return "hold " + command.target + " " + std::to_string(command.frames);
    case Op::Wait: return "wait " + std::to_string(command.frames);
    case Op::WaitFor: {
        static const char* const kConditions[] = {"?", "ready", "frontend", "ingame", "level", "menu", "focus", "fe"};
        std::string text = std::string("waitfor ") + kConditions[static_cast<int>(command.condition)];
        if (!command.target.empty())
            text += " " + command.target;
        return text;
    }
    case Op::Level: return "level " + command.target;
    case Op::Echo: return "echo " + command.target;
    case Op::Frontend: return "fe " + command.target;
    case Op::Quit: return "quit";
    case Op::Crash: return "crash " + command.target;
    case Op::Teleport:
    case Op::Camera: {
        if (command.target == "off")
            return "camera off";
        std::string text = command.op == Op::Teleport ? "teleport" : "camera";
        for (float number : command.numbers) {
            char value[32];
            std::snprintf(value, sizeof(value), " %g", static_cast<double>(number));
            text += value;
        }
        if (command.op == Op::Teleport && command.target == "board")
            text += " board";
        return text;
    }
    case Op::Capture: return "capture " + std::to_string(command.frames) + (command.target.empty() ? "" : " " + command.target);
    case Op::LogTricks: return "logtricks" + (command.frames ? " " + std::to_string(command.frames) : std::string());
    case Op::Trace: return "trace " + std::to_string(command.frames);
    }
    return "?";
}

std::string StatusJson(const Status& status)
{
    std::string out = "{\n";
    auto field = [&out](const char* name, const std::string& value, bool last = false) {
        out += std::string("  \"") + name + "\": " + value + (last ? "\n" : ",\n");
    };
    auto boolean = [](bool value) { return std::string(value ? "true" : "false"); };
    field("frames", std::to_string(status.frames));
    field("ready", boolean(status.ready));
    field("main_menu", boolean(status.mainMenu));
    field("where", JsonString(status.where));
    field("paused", boolean(status.paused));
    field("loading", boolean(status.loading));
    field("level", JsonString(status.level));
    field("game_mode", JsonString(status.gameMode));
    field("focus", JsonList(status.focus));
    field("selected", JsonList(status.selected));
    field("lines_read", std::to_string(status.linesRead));
    field("lines_done", std::to_string(status.linesDone));
    field("queued", std::to_string(status.queued));
    field("current", JsonString(status.current));
    field("last", JsonString(status.last));
    field("result", JsonString(status.result));
    field("errors", std::to_string(status.errors));
    if (!status.frontend.empty())
        field("frontend", JsonString(status.frontend));
    field("refusals", std::to_string(status.refusals), true);
    return out + "}\n";
}

// --- the game ---------------------------------------------------------------------------------------------------


namespace {

using MouseMenus::ChildCount;
using MouseMenus::IsMenu;
using MouseMenus::SelectedIndex;

uint8_t* SkateModule() { return reinterpret_cast<uint8_t*>(Mdl::g_skateModule); }
// The level loaded (its load script's checksum, the skate module's +0x378; load_mainmenu in the front end).
uint32_t LevelLoad() { return SkateModule() ? *reinterpret_cast<uint32_t*>(SkateModule() + 0x378) : 0; }
bool InFrontEnd() { return LevelLoad() == kLoadMainMenu; }

CScreenElement* FirstChild(const CScreenElement* element) { return element->mp_firstChild.Get(); }
CScreenElement* NextSibling(const CScreenElement* element) { return element->mp_next.Get(); }
CScreenElement* Parent(const CScreenElement* element) { return element->mp_parent.Get(); }
uint32_t Id(const CScreenElement* element) { return element->m_id; }

CScreenElement* ChildAt(const CScreenElement* menu, int index)
{
    if (index < 0)
        return nullptr;
    CScreenElement* child = FirstChild(menu);
    for (; child && index > 0; --index)
        child = NextSibling(child);
    return child;
}

int IndexOf(const CScreenElement* menu, const CScreenElement* item)
{
    int index = 0;
    for (CScreenElement* child = FirstChild(menu); child; child = NextSibling(child), ++index)
        if (child == item)
            return index;
    return -1;
}

bool HiddenInChain(const CScreenElement* element)
{
    for (; element; element = Parent(element))
        if (element->m_objectFlags & CScreenElement::kHidden)
            return true;
    return false;
}

// The element `id` if it exists (null if not); `holder` keeps it alive.
CScreenElement* FindElement(uint32_t id, ElementPtr* holder)
{
    holder->Release();
    if (!CScreenElementManager::Instance())
        return nullptr;
    GetElement(holder, id);
    return holder->Get();
}

// Whether a script's parameters name a denied script or id (structures and arrays too).
bool StructDenied(Script::CStruct* params, int depth);

bool ValueDenied(uint32_t type, uint32_t value, int depth)
{
    switch (type) {
    case Script::ESYMBOLTYPE_NAME:
        return ChecksumDenied(value);
    case Script::ESYMBOLTYPE_STRUCTURE:
    case Script::ESYMBOLTYPE_STRUCTUREPOINTER:
        return StructDenied(reinterpret_cast<Script::CStruct*>(value), depth + 1);
    case Script::ESYMBOLTYPE_ARRAY:
    case Script::ESYMBOLTYPE_ARRAYPOINTER: {
        auto* array = reinterpret_cast<Script::CArray*>(value);
        if (!array || depth > 8)
            return false;
        uint32_t* elements = array->GetArrayPointer();
        for (uint32_t i = 0; elements && i < array->m_size; ++i)
            if (ValueDenied(array->m_type, elements[i], depth + 1))
                return true;
        return false;
    }
    default:
        return false;
    }
}

bool StructDenied(Script::CStruct* params, int depth)
{
    if (!params || depth > 8)
        return false;
    for (Script::CComponent* component = params->GetNextComponent(nullptr); component;
         component = params->GetNextComponent(component)) {
        if (ChecksumDenied(component->name) || ValueDenied(component->Type(), component->raw, depth))
            return true;
    }
    return false;
}

// Whether an element's event handlers (all, or only pad_choose / pad_start ones) run or name a denied script.
bool HandlersDenied(const CScreenElement* element, bool chooseOnly)
{
    Script::CScript* script = element->m_script;
    if (!script || !script->m_eventHandlers)
        return false;
    const Obj::CEventHandlerTable* table = script->m_eventHandlers;
    for (int i = 0; i < table->m_count; ++i) {
        const Obj::CEventHandlerTable::Entry& entry = table->m_entries[i];
        if (entry.script == Obj::kNullScript)
            continue;
        if (chooseOnly && entry.type != kPadChoose && entry.type != kPadStart)
            continue;
        if (ChecksumDenied(entry.script) || StructDenied(entry.params, 0))
            return true;
    }
    return false;
}

// Why choosing this element would be refused ("" if it wouldn't): its id, its own handlers, its parents' choose
// handlers.
std::string RefusalOfElement(const CScreenElement* element)
{
    if (ChecksumDenied(Id(element)))
        return "the element " + NameOf(Id(element)) + " is online / network";
    if (HandlersDenied(element, false))
        return "the element " + NameOf(Id(element)) + "'s handlers lead online / to the network";
    for (const CScreenElement* parent = Parent(element); parent; parent = Parent(parent))
        if (HandlersDenied(parent, true))
            return "the menu " + NameOf(Id(parent)) + "'s choose handlers lead online / to the network";
    return {};
}

// The focused elements (controller 0's focus list) and the selected items of the focused menus.
void FocusedElements(std::vector<CScreenElement*>* focused, std::vector<CScreenElement*>* selected)
{
    CScreenElementManager* manager = CScreenElementManager::Instance();
    if (!manager)
        return;
    int guard = 0;
    for (CScreenElementManager::FocusEntry* entry = manager->mp_focusLists[0]; entry && guard < 64;
         entry = entry->next, ++guard) {
        CScreenElement* element = entry->element.Get();
        if (!element)
            continue;
        focused->push_back(element);
        if (IsMenu(element))
            if (CScreenElement* item = ChildAt(element, SelectedIndex(element)))
                selected->push_back(item);
    }
}

// Why the game state refuses actions ("" if it doesn't): online, a network game, a network game mode.
std::string NetRefusal()
{
    if (Net::Manager* net = Net::Manager::Instance())
        if (net->IsOnline())
            return "the game is online";
    if (void* gameNet = GameNet::g_gameNetManager)
        if (static_cast<GameNet::Manager*>(gameNet)->InNetGame())
            return "a network game runs";
    if (void* mode = reinterpret_cast<void*>(Game::g_gameMode)) {
        std::string name = NameOf(static_cast<Game::CGameMode*>(mode)->GetNameChecksum());
        if (name.rfind("net", 0) == 0)
            return "the game mode is a network one (" + name + ")";
    }
    return {};
}

std::string GameModeName()
{
    void* mode = reinterpret_cast<void*>(Game::g_gameMode);
    return mode ? NameOf(static_cast<Game::CGameMode*>(mode)->GetNameChecksum()) : "";
}

// The main menu: its menu up and taking pad events, in the front end.
bool MainMenuUp()
{
    static const uint32_t kMainVMenu = Crc::GenerateCRCFromString("main_vmenu");
    if (!InFrontEnd())
        return false;
    ElementPtr holder;
    CScreenElement* menu = FindElement(kMainVMenu, &holder);
    return menu && IsMenu(menu) && MouseMenus::MenuGetsPadEvents(menu);
}

bool SkaterUp() { return Obj::FindObject(kSkater) != nullptr; }

// --- trace: the skater frame by frame ---------------------------------------------------------------------------

struct Trace {
    FILE* file = nullptr;
    int left = 0;
};
Trace& TraceState()
{
    static Trace trace;
    return trace;
}

void StopTrace()
{
    Trace& trace = TraceState();
    if (trace.file)
        std::fclose(trace.file);
    trace.file = nullptr;
    trace.left = 0;
}

std::string StartTrace(int frames)
{
    StopTrace();
    std::string path = GameFolderPath("thug2-decomp-trace.txt");
    FILE* file = _fsopen(path.c_str(), "w", _SH_DENYWR);
    if (!file)
        return "can't write " + path;
    std::fprintf(file, "# frame x y z vx vy vz up_y state physics bailing trick pot multiplier landed ground_flags ground_object\n");
    std::fflush(file);
    TraceState().file = file;
    TraceState().left = frames;
    return {};
}

// Once a frame: the skater's line (nothing while there's none - a level loading).
void TraceFrame()
{
    Trace& trace = TraceState();
    if (!trace.file)
        return;
    auto* skater = static_cast<Obj::CCompositeObject*>(Obj::FindObject(kSkater));
    if (skater) {
        auto* state = reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterStateComponent));
        int where = state ? *reinterpret_cast<int*>(state + 0x18) : -1;
        int physics = state ? *reinterpret_cast<int*>(state + 0x1C) : -1;
        int bailing = state ? state[0x70] : -1;
        int trick = state ? state[0xE0] : -1;
        // the score: the combo so far (+0x14) and its multiplier (+0x60), the last score landed (+0x20)
        auto* scoreComponent = reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterScoreComponent));
        uint8_t* score = scoreComponent ? *reinterpret_cast<uint8_t**>(scoreComponent + 0x18) : nullptr;
        int pot = score ? *reinterpret_cast<int*>(score + 0x14) : -1;
        float multiplier = score ? *reinterpret_cast<float*>(score + 0x60) : 0.0f;
        int landed = score ? *reinterpret_cast<int*>(score + 0x20) : -1;
        // the core physics' last ground hit (its feeler copy at +0x158): the face's flags and the object's checksum
        auto* core = reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterCorePhysicsComponent));
        unsigned groundFlags = core ? *reinterpret_cast<uint16_t*>(core + 0x158 + 0x50) : 0;
        unsigned groundObject = core ? *reinterpret_cast<uint32_t*>(core + 0x158 + 0x58) : 0;
        std::fprintf(trace.file, "%u %.2f %.2f %.2f %.2f %.2f %.2f %.3f %d %d %d %d %d %.2f %d 0x%04X 0x%08X\n",
                     Mlp::FramesRun(), static_cast<double>(skater->m_pos.x), static_cast<double>(skater->m_pos.y),
                     static_cast<double>(skater->m_pos.z), static_cast<double>(skater->m_vel.x),
                     static_cast<double>(skater->m_vel.y), static_cast<double>(skater->m_vel.z),
                     static_cast<double>(skater->m_matrix.row[1].y), where, physics, bailing, trick, pot,
                     static_cast<double>(multiplier), landed, groundFlags, groundObject);
        std::fflush(trace.file);
    }
    if (--trace.left <= 0)
        StopTrace();
}

// --- the command file -------------------------------------------------------------------------------------------

struct Line {
    std::string text;
    int number;
};

struct Runner {
    // the file
    bool looked = false;
    uint64_t size = 0, time = 0, offset = 0;
    std::string partial;
    int stillFrames = 0;
    int linesRead = 0, linesDone = 0;
    std::deque<Line> queue;
    // the running command
    bool active = false;
    Command command;
    std::string text;
    DWORD started = 0;
    int counter = 0;
    int phase = 0;
    MouseMenus::Target target = {};
    bool targetSet = false;
    // the button held (port 0)
    const Button* held = nullptr;
    // `hold`'s button (port 0), held for `holdFrames` more frames while later commands run
    const Button* holdButton = nullptr;
    int holdFrames = 0;
    // the status
    std::string last, result = "ok";
    int errors = 0, refusals = 0;
    bool ready = false;
    std::string written;
    uint32_t writtenFrames = 0;
    // -run
    bool runDone = false;
    int readyFrames = 0;
    // the main menu: frames in a row it has been up, and whether that's a second
    int menuFrames = 0;
    bool menuSettled = false;
};

Runner& State()
{
    static Runner* runner = new Runner; // (never freed: nothing runs on the game's memory at exit)
    return *runner;
}

// The menu the running choose / focus is moving (cleared if it dies).
ElementPtr& TargetMenu()
{
    static ElementPtr* menu = new ElementPtr;
    return *menu;
}

uint64_t FileTimeValue(const FILETIME& time) { return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime; }

uint64_t ProcessStart()
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0;
    return FileTimeValue(created);
}

void AddLine(Runner& runner, const std::string& text)
{
    ++runner.linesRead;
    // (a UTF-8 byte order mark - PowerShell's Out-File writes one - isn't part of the command)
    bool bom = text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0;
    runner.queue.push_back({bom ? text.substr(3) : text, runner.linesRead});
}

void PollFile(Runner& runner)
{
    std::string path = GameFolderPath("thug2-decomp-commands.txt");
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &info)) {
        runner.looked = true;
        return;
    }
    uint64_t size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    uint64_t time = FileTimeValue(info.ftLastWriteTime);
    if (!runner.looked) {
        runner.looked = true;
        // (a file from before this run - written more than 10 s before the game started - isn't run)
        uint64_t start = ProcessStart();
        if (start && time + 10ull * 10000000ull < start) {
            Log("automation: %s is older than this run - not run", path.c_str());
            runner.size = runner.offset = size;
            runner.time = time;
            return;
        }
    }
    if (size == runner.size && time == runner.time) {
        // A last line without an end of line, once the file has been still a moment.
        if (!runner.partial.empty() && ++runner.stillFrames >= kPartialLineFrames) {
            AddLine(runner, runner.partial);
            runner.partial.clear();
        }
        return;
    }
    runner.stillFrames = 0;
    if (size < runner.offset) {
        Log("automation: the command file got shorter - read from its start again");
        runner.offset = 0;
        runner.partial.clear();
        runner.queue.clear();
        runner.linesRead = runner.linesDone = 0;
    }
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return; // (try again next frame)
    LARGE_INTEGER at;
    at.QuadPart = static_cast<LONGLONG>(runner.offset);
    std::string data;
    if (SetFilePointerEx(file, at, nullptr, FILE_BEGIN) && size > runner.offset && size - runner.offset < (1u << 20)) {
        data.resize(static_cast<size_t>(size - runner.offset));
        DWORD read = 0;
        if (!ReadFile(file, &data[0], static_cast<DWORD>(data.size()), &read, nullptr))
            read = 0;
        data.resize(read);
    }
    CloseHandle(file);
    runner.size = size;
    runner.time = time;
    runner.offset += data.size();
    runner.partial += data;
    size_t end;
    while ((end = runner.partial.find('\n')) != std::string::npos) {
        std::string line = runner.partial.substr(0, end);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        AddLine(runner, line);
        runner.partial.erase(0, end + 1);
    }
}

// --- running commands -------------------------------------------------------------------------------------------

void Finish(Runner& runner, const std::string& result)
{
    runner.active = false;
    runner.last = runner.text;
    runner.result = result;
    ++runner.linesDone;
    Log("automation: %s -> %s", runner.text.c_str(), result.c_str());
    TargetMenu().Release();
    runner.targetSet = false;
}

// A failure: the rest of the batch is skipped.
void Fail(Runner& runner, const std::string& result, bool refused)
{
    if (refused) {
        ++runner.refusals;
        Log("automation: REFUSED %s: %s", runner.text.c_str(), result.c_str());
    } else {
        ++runner.errors;
    }
    Finish(runner, (refused ? "refused: " : "error: ") + result);
    if (!runner.queue.empty())
        Log("automation: %d command line(s) after it skipped", static_cast<int>(runner.queue.size()));
    runner.linesDone += static_cast<int>(runner.queue.size());
    runner.queue.clear();
}

bool TimedOut(const Runner& runner, int seconds)
{
    return GetTickCount() - runner.started > static_cast<DWORD>(seconds) * 1000u;
}

Script::CStruct* MakeParams(const std::vector<Param>& params)
{
    Script::CStruct* out = Script::NewStruct()->Init();
    for (const Param& param : params) {
        switch (param.kind) {
        case Param::Flag: out->AddChecksum(0u, Checksum(param.text)); break;
        case Param::Integer: out->AddInteger(Checksum(param.name), param.integer); break;
        case Param::Float: out->AddFloat(Checksum(param.name), param.real); break;
        case Param::String: out->AddString(Checksum(param.name), param.text.c_str()); break;
        case Param::Name: out->AddChecksum(Checksum(param.name), IdChecksum(param.text)); break;
        }
    }
    return out;
}

bool ScriptExists(uint32_t name)
{
    Script::CSymbolTableEntry* symbol = Script::LookUpSymbol(name);
    return symbol && (symbol->type == Script::ESYMBOLTYPE_QSCRIPT || symbol->type == Script::ESYMBOLTYPE_CFUNCTION);
}

// Spawn a script (run's and -run's); "" if it ran, else why not.
std::string SpawnNamed(const std::string& name, const std::vector<Param>& params)
{
    uint32_t checksum = Checksum(name);
    if (!ScriptExists(checksum))
        return "no script " + name;
    Script::CStruct* structure = MakeParams(params);
    Script::SpawnScript(checksum, structure, 0, nullptr, 0, 0, false, false, false, false);
    structure->Clear();
    Script::DeleteStruct(structure);
    return {};
}

int ReadTarget(void* /*context*/) { return TargetMenu() ? SelectedIndex(TargetMenu().Get()) : -1; }

void SendStep(void* context, int direction)
{
    auto* frontEnd = static_cast<Mdl::FrontEnd*>(context);
    bool vertical = TargetMenu() && MouseMenus::IsVerticalMenu(TargetMenu().Get());
    uint32_t event = vertical ? (direction < 0 ? Mdl::FrontEnd::kPadUp : Mdl::FrontEnd::kPadDown)
                              : (direction < 0 ? Mdl::FrontEnd::kPadLeft : Mdl::FrontEnd::kPadRight);
    MouseMenus::SendMenuPadEvent(frontEnd, event);
}

// Whether the front end drops menu pad events now (as InputLogic / SendMenuPadEvent check): the first controller's
// mapping inactive, menu input blocked, no input device.
bool MenuInputBlocked(Mdl::FrontEnd* frontEnd)
{
    if (!frontEnd || frontEnd->m_padMappings[0].m_mapping == 0 || frontEnd->m_blockMenuInput)
        return true;
    Mdl::FrontEndInputHandler* handler = frontEnd->m_inputHandlers[0];
    return !handler || !handler->m_device;
}

// Whether a pad press would be lost on the menus now: in the front end or the pause menu, menu input blocked, or a
// menu has the focus but none of the focused menus takes pad events (a menu still coming in).
bool MenusIgnorePad(Mdl::FrontEnd* frontEnd)
{
    if (!InFrontEnd() && !(frontEnd && frontEnd->m_paused))
        return false;
    if (MenuInputBlocked(frontEnd))
        return true;
    std::vector<CScreenElement*> focused, selected;
    FocusedElements(&focused, &selected);
    bool anyMenu = false;
    for (CScreenElement* element : focused) {
        if (!IsMenu(element))
            continue;
        anyMenu = true;
        if (MouseMenus::MenuGetsPadEvents(element))
            return false;
    }
    return anyMenu;
}

// Why pressing the button now is refused ("" if it isn't): a button that can choose, with a focused element (or a
// focused menu's selected item) that leads online.
std::string PressRefusal(const Button& button)
{
    if (!ButtonChooses(button))
        return {};
    std::vector<CScreenElement*> focused, selected;
    FocusedElements(&focused, &selected);
    for (auto* list : {&selected, &focused}) {
        for (CScreenElement* element : *list) {
            std::string refusal = RefusalOfElement(element);
            if (!refusal.empty())
                return refusal;
        }
    }
    return {};
}

// Whether a loading screen is up: the script's DisplayLoadingScreen flag (0x006FC49C: set by 0x00490890, cleared by
// 0x004908C0), or the PC loading screen's drawing thread (its handle 0x00786D08, set at 0x004B7445; its running flag
// 0x00786D04, set at 0x004B7ABB; both cleared by 0x004B6F10 - also the start-up screen, shown without the script
// flag). Menus made under one are usually still being set up by their scripts (choosing one away under the loading
// logo left the logo up), so choose / press wait for it - at most kPressWaitSeconds.
bool LoadingScreenUp()
{
    return Nx::g_loadingScreenShown != 0 || Nx::g_loadingBarTimer != 0 ||
           static_cast<uint8_t>(Nx::g_loadingScreenUp) != 0;
}

// choose / focus, once a frame until done.
void StepChoose(Runner& runner, Mdl::FrontEnd* frontEnd)
{
    bool choose = runner.command.op == Op::Choose;
    uint32_t id = IdChecksum(runner.command.target);
    ElementPtr holder;
    CScreenElement* item = FindElement(id, &holder);
    if (!item || HiddenInChain(item)) {
        if (TimedOut(runner, runner.command.seconds))
            Fail(runner, item ? "the element is hidden" : "no element " + runner.command.target, false);
        return;
    }
    // A menu given: its selected item (choose) - or the focus event to the menu (focus).
    CScreenElement* menu = nullptr;
    CScreenElement* chosen = item;
    if (IsMenu(item)) {
        if (!choose) {
            std::string why = RefusalOfElement(item);
            if (!why.empty())
                return Fail(runner, why, true);
            Obj::Tracker()->LaunchEvent(kFocusEvent, id, kSystem, nullptr, false, -1.0f);
            return Finish(runner, "ok (focus sent to the menu)");
        }
        menu = item;
        chosen = ChildAt(menu, SelectedIndex(menu));
        if (!chosen) {
            if (TimedOut(runner, runner.command.seconds))
                Fail(runner, "the menu " + NameOf(id) + " has no item selected", false);
            return;
        }
    } else {
        menu = Parent(item);
    }
    std::string why = RefusalOfElement(chosen);
    if (!why.empty())
        return Fail(runner, why, true);
    if (!menu || !IsMenu(menu)) {
        // Not a menu's item: the event goes to it.
        Obj::Tracker()->LaunchEvent(choose ? kPadChoose : kFocusEvent, id, kSystem, nullptr, false, -1.0f);
        return Finish(runner, choose ? "ok (pad_choose sent to the element)" : "ok (focus sent to the element)");
    }
    int index = IndexOf(menu, chosen);
    CScreenElement* current = ChildAt(menu, SelectedIndex(menu));
    // (nothing sent while the menu doesn't take pad events, the front end blocks menu input - SetButtonEventMappings
    // block_menu_input, TemporarilyDisableInput - or the menu is on an item it passes over by itself)
    bool wait = !MouseMenus::MenuGetsPadEvents(menu) || MenuInputBlocked(frontEnd) ||
                (current && MouseMenus::ItemPassedOver(current)) ||
                (LoadingScreenUp() && !TimedOut(runner, kPressWaitSeconds));
    if (wait && TimedOut(runner, runner.command.seconds))
        return Fail(runner, "its menu " + NameOf(Id(menu)) + " didn't take pad events", false);
    if (!runner.targetSet || TargetMenu().Get() != menu) {
        if (wait)
            return;
        TargetMenu().Set(menu);
        runner.target = MouseMenus::StartTarget(index, ChildCount(menu), MouseMenus::MenuWraps(menu));
        runner.targetSet = true;
    }
    runner.target.count = ChildCount(menu);
    MouseMenus::TargetState state = MouseMenus::AdvanceTarget(runner.target, wait, ReadTarget, SendStep, frontEnd);
    if (state == MouseMenus::TargetState::Moving)
        return;
    if (state == MouseMenus::TargetState::GaveUp)
        return Fail(runner, "its menu's selection didn't get to it", false);
    if (wait)
        return; // (there - the choose waits for the menu to take it)
    if (choose)
        MouseMenus::SendMenuPadEvent(frontEnd, kPadChoose);
    Finish(runner, "ok");
}

bool ConditionHolds(const Command& command)
{
    switch (command.condition) {
    case Condition::Ready: return State().menuSettled;
    case Condition::FrontEnd: return InFrontEnd();
    case Condition::NewFrontend: return Front::NewFrontend::AutomationScreenUp(command.target);
    case Condition::InGame: {
        auto* frontEnd = Mdl::g_frontEndInstance;
        return LevelLoad() && !InFrontEnd() && SkaterUp() && !(frontEnd && frontEnd->m_paused);
    }
    case Condition::Level: {
        std::string name = command.target;
        if (_strnicmp(name.c_str(), "load_", 5) != 0)
            name = "Load_" + name;
        return LevelLoad() == Checksum(name) && SkaterUp();
    }
    case Condition::Menu: {
        ElementPtr holder;
        return FindElement(IdChecksum(command.target), &holder) != nullptr;
    }
    case Condition::Focus: {
        uint32_t id = IdChecksum(command.target);
        std::vector<CScreenElement*> focused, selected;
        FocusedElements(&focused, &selected);
        for (CScreenElement* element : focused)
            if (Id(element) == id)
                return true;
        for (CScreenElement* element : selected)
            if (Id(element) == id)
                return true;
        return false;
    }
    default: return false;
    }
}

// The condition has held long enough: a level / in game for a second in a row (the level's first frames - its intro
// camera, the skater set down - pass first), the others at once.
bool ConditionMet(Runner& runner)
{
    if (!ConditionHolds(runner.command)) {
        runner.counter = 0;
        return false;
    }
    Condition condition = runner.command.condition;
    int frames = condition == Condition::Level || condition == Condition::InGame ? kSettleFrames : 1;
    return ++runner.counter >= frames;
}

// `logtricks [count]` (a test of the graffiti change mask, TP-R7): one trick by
// skater 0 over the level's trick objects (every one, or the first `count` in the table's order), through the trick
// object manager's LogTrickObjects as a skater's trick goes - more than THUG2's own mask held crashed there.
std::string LogTricks(int count, std::string* failed)
{
    auto* manager = Obj::g_trickObjectManager;
    if (!manager) {
        *failed = "no trick object manager";
        return "";
    }
    std::vector<uint32_t> objects;
    std::vector<void*> clusters;
    const Lst::HashTable& table = manager->m_objectClusters;
    for (int bucket = 0; bucket < (1 << table.m_bits); ++bucket) {
        for (const Lst::HashTable::Item* item = &table.m_buckets[bucket]; item; item = item->next) {
            if (!item->value)
                continue;
            objects.push_back(item->key);
            if (std::find(clusters.begin(), clusters.end(), item->value) == clusters.end())
                clusters.push_back(item->value);
        }
    }
    if (count > 0 && objects.size() > static_cast<size_t>(count))
        objects.resize(static_cast<size_t>(count));
    size_t asked = objects.size();
    objects.push_back(0xEEEEEEEE); // (past the list: must be left alone)
    uint8_t stolen[0x100] = {};
    uint32_t changed = manager->LogTrickObjects(static_cast<uint32_t>(asked), objects.data(), stolen, 0, 1000000);
    if (objects[asked] != 0xEEEEEEEE) {
        *failed = "the word past the list was changed";
        return "";
    }
    char result[160];
    std::snprintf(result, sizeof(result), "ok (%u trick objects of %u clusters in the level logged, %u changed hands)",
                  static_cast<unsigned>(asked), static_cast<unsigned>(clusters.size()), changed);
    return result;
}

// Start the next command; true if it's done already (the next can start this frame).
void WriteStatus(Runner& runner);

bool Start(Runner& runner, Mdl::FrontEnd* frontEnd)
{
    Line line = runner.queue.front();
    runner.queue.pop_front();
    std::string error;
    Command command;
    runner.text = line.text;
    runner.started = GetTickCount();
    runner.counter = 0;
    runner.phase = 0;
    runner.targetSet = false;
    switch (ParseCommand(line.text, &command, &error)) {
    case ParseResult::Empty:
        ++runner.linesDone;
        return true;
    case ParseResult::Error:
        Log("automation: line %d: %s", line.number, line.text.c_str());
        Fail(runner, "line " + std::to_string(line.number) + ": " + error, false);
        return true;
    case ParseResult::Command:
        break;
    }
    runner.active = true;
    runner.command = command;
    runner.text = Describe(command);
    Remember(command.target);
    if (command.op == Op::Level || command.condition == Condition::Level)
        Remember(_strnicmp(command.target.c_str(), "load_", 5) == 0 ? command.target : "Load_" + command.target);
    Log("automation: %s", runner.text.c_str());
    std::string why = RefusalByName(command);
    if (why.empty() && command.op != Op::Wait && command.op != Op::Echo && command.op != Op::Quit &&
        command.op != Op::WaitFor)
        why = NetRefusal();
    if (!why.empty()) {
        Fail(runner, why, true);
        return true;
    }
    switch (command.op) {
    case Op::Run: {
        std::string failed = SpawnNamed(command.target, command.params);
        if (failed.empty())
            Finish(runner, "ok");
        else
            Fail(runner, failed, false);
        return true;
    }
    case Op::Level: {
        std::string failed;
        if (StartLevel::Load(command.target.c_str(), &failed))
            Finish(runner, "ok");
        else
            Fail(runner, failed, false);
        return true;
    }
    case Op::Echo:
        Finish(runner, command.target);
        return true;
    case Op::Frontend: {
        bool done = true;
        std::string result = Front::NewFrontend::AutomationCommand(command.target, &done);
        if (!done)
            return false; // (Continue: once its screen is up)
        if (result.rfind("refused", 0) == 0)
            Fail(runner, result, true);
        else if (result.rfind("error", 0) == 0)
            Fail(runner, result, false);
        else
            Finish(runner, result);
        return true;
    }
    case Op::Teleport: {
        if (!LevelLoad() || InFrontEnd() || !SkaterUp()) {
            Fail(runner, "not in a level with the skater", false);
            return true;
        }
        const std::vector<float>& n = command.numbers;
        Front::TestView::Vec3 at = {};
        std::string failed = Front::TestView::Teleport({n[0], n[1], n[2]}, n.size() > 3, n.size() > 3 ? n[3] : 0.0f, &at,
                                                       command.target == "board");
        if (!failed.empty()) {
            Fail(runner, failed, false);
            return true;
        }
        char result[96];
        std::snprintf(result, sizeof(result), "ok (standing at %.1f %.1f %.1f)", at.x, at.y, at.z);
        if (FILE* traceFile = TraceState().file) { // (a trace marks each teleport: the rides it holds)
            std::fprintf(traceFile, "# %s -> %s\n", runner.text.c_str(), result);
            std::fflush(traceFile);
        }
        Finish(runner, result);
        return true;
    }
    case Op::Camera:
        if (command.target == "off")
            Front::TestView::UnlockCamera();
        else if (!Front::TestView::LockCamera(
                     {command.numbers[0], command.numbers[1], command.numbers[2]},
                     {command.numbers[3], command.numbers[4], command.numbers[5]},
                     command.numbers.size() > 6
                         ? Front::TestView::Vec3{command.numbers[6], command.numbers[7], command.numbers[8]}
                         : Front::TestView::Vec3{0.0f, 0.0f, 0.0f})) {
            Fail(runner, "the direction has no horizon (straight up / down)", false);
            return true;
        }
        Finish(runner, "ok");
        return true;
    case Op::Capture: {
        std::string failed = Sys::Screenshot::StartSeries(command.frames, command.target);
        if (!failed.empty()) {
            Fail(runner, failed, false);
            return true;
        }
        return false; // (Continue: done once written)
    }
    case Op::Trace: {
        std::string failed = StartTrace(command.frames);
        if (failed.empty())
            Finish(runner, "ok");
        else
            Fail(runner, failed, false);
        return true;
    }
    case Op::LogTricks: {
        if (!LevelLoad() || InFrontEnd() || !SkaterUp()) {
            Fail(runner, "not in a level with the skater", false);
            return true;
        }
        std::string failed;
        std::string result = LogTricks(command.frames, &failed);
        if (failed.empty())
            Finish(runner, result);
        else
            Fail(runner, failed, false);
        return true;
    }
    case Op::Crash: {
        TestFault fault = TestFault::Access;
        ParseTestFault(command.target, &fault);
        WriteStatus(runner); // (its "current" says what ended the game)
        if (!CauseTestFault(fault))
            Fail(runner, "crash " + command.target + ": not in this build (a Debug build's check)", false);
        else
            Fail(runner, "crash " + command.target + ": the game still runs after it", false);
        return true;
    }
    case Op::Quit: {
        Finish(runner, "ok");
        SDL_Event event = {};
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
        return false;
    }
    case Op::Press: {
        std::string refusal = PressRefusal(*FindButton(command.target));
        if (!refusal.empty()) {
            Fail(runner, refusal, true);
            return true;
        }
        runner.phase = 0; // (Continue: held once the menus take it - looked at again then)
        return false;
    }
    case Op::Hold: {
        std::string refusal = PressRefusal(*FindButton(command.target));
        if (refusal.empty())
            refusal = NetRefusal();
        if (!refusal.empty()) {
            Fail(runner, refusal, true);
            return true;
        }
        runner.holdButton = FindButton(command.target);
        runner.holdFrames = command.frames;
        Finish(runner, "ok");
        return true;
    }
    case Op::Choose:
    case Op::Focus:
        StepChoose(runner, frontEnd);
        return !runner.active;
    case Op::Wait:
        if (command.frames == 0) {
            Finish(runner, "ok");
            return true;
        }
        return false;
    case Op::WaitFor:
        if (ConditionMet(runner)) {
            Finish(runner, "ok");
            return true;
        }
        return false;
    }
    return false;
}

// The running command's frame.
void Continue(Runner& runner, Mdl::FrontEnd* frontEnd)
{
    switch (runner.command.op) {
    case Op::Press:
        if (runner.phase == 0) {
            // In the menus, not while they'd drop it (at most kPressWaitSeconds).
            if ((MenusIgnorePad(frontEnd) || LoadingScreenUp()) && !TimedOut(runner, kPressWaitSeconds))
                return;
            std::string refusal = PressRefusal(*FindButton(runner.command.target));
            if (refusal.empty())
                refusal = NetRefusal();
            if (!refusal.empty())
                return Fail(runner, refusal, true);
            runner.held = FindButton(runner.command.target);
            runner.phase = 1;
            runner.counter = 0;
        }
        ++runner.counter;
        if (runner.phase == 1 && runner.counter >= runner.command.frames) {
            runner.held = nullptr;
            runner.phase = 2;
            runner.counter = 0;
        } else if (runner.phase == 2 && runner.counter >= kReleaseFrames) {
            Finish(runner, "ok");
        }
        return;
    case Op::Wait:
        if (++runner.counter >= runner.command.frames)
            Finish(runner, "ok");
        return;
    case Op::Frontend: {
        std::string result = Front::NewFrontend::AutomationResult();
        if (result.empty()) {
            if (TimedOut(runner, runner.command.seconds)) {
                Front::NewFrontend::AutomationCancel();
                Fail(runner, "fe " + runner.command.target + ": its screen wasn't up in " +
                                 std::to_string(runner.command.seconds) + " s (" + Front::NewFrontend::AutomationStatus() + ")",
                     false);
            }
            return;
        }
        if (result.rfind("error", 0) == 0)
            Fail(runner, result, false);
        else
            Finish(runner, result);
        return;
    }
    case Op::WaitFor:
        if (ConditionMet(runner))
            Finish(runner, "ok");
        else if (TimedOut(runner, runner.command.seconds))
            Fail(runner, "timed out after " + std::to_string(runner.command.seconds) + " s", false);
        return;
    case Op::Choose:
    case Op::Focus:
        if (!NetRefusal().empty())
            return Fail(runner, NetRefusal(), true);
        StepChoose(runner, frontEnd);
        return;
    case Op::Capture:
        if (Sys::Screenshot::SeriesRunning())
            return;
        if (Sys::Screenshot::SeriesResult().rfind("ok", 0) == 0)
            Finish(runner, Sys::Screenshot::SeriesResult());
        else
            Fail(runner, Sys::Screenshot::SeriesResult(), false);
        return;
    default:
        Finish(runner, "ok");
        return;
    }
}

void WriteStatus(Runner& runner)
{
    Status status = {};
    status.frames = Mlp::FramesRun();
    status.ready = runner.ready;
    status.mainMenu = runner.menuSettled;
    auto* frontEnd = Mdl::g_frontEndInstance;
    status.paused = frontEnd && frontEnd->m_paused;
    status.loading = LoadingScreenUp();
    uint32_t load = LevelLoad();
    status.where = !load ? "starting" : load == kLoadMainMenu ? "front_end" : "level";
    status.level = load ? NameOf(load) : "";
    if (_strnicmp(status.level.c_str(), "load_", 5) == 0)
        status.level.erase(0, 5);
    status.gameMode = GameModeName();
    std::vector<CScreenElement*> focused, selected;
    FocusedElements(&focused, &selected);
    for (CScreenElement* element : focused)
        status.focus.push_back(NameOf(Id(element)));
    for (CScreenElement* element : selected)
        status.selected.push_back(NameOf(Id(element)));
    status.linesRead = runner.linesRead;
    status.linesDone = runner.linesDone;
    status.queued = static_cast<int>(runner.queue.size());
    status.current = runner.active ? runner.text : "";
    status.last = runner.last;
    status.result = runner.result;
    status.errors = runner.errors;
    status.refusals = runner.refusals;
    status.frontend = Front::NewFrontend::AutomationStatus();
    // Written when anything but the frame count changed, or every half second.
    status.frames = 0;
    std::string key = StatusJson(status);
    if (key == runner.written && Mlp::FramesRun() - runner.writtenFrames < 30)
        return;
    runner.written = key;
    runner.writtenFrames = Mlp::FramesRun();
    status.frames = Mlp::FramesRun();
    std::string text = StatusJson(status);
    std::string path = GameFolderPath("thug2-decomp-status.json");
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

// The command line as the DLL found it, read while the DLL loads: the game's own parsing cuts the process's command
// line buffer into words later (a 0 after the first word: "... -run").
const std::string g_commandLine = GetCommandLineA();

// -run <script>, --run <script> or --run=<script> ("" if none).
const std::string& RequestedRun()
{
    static const std::string script = [] {
        const std::string& line = g_commandLine;
        for (const char* option : {"--run=", "--run ", "-run "}) {
            size_t at = line.find(option);
            if (at == std::string::npos)
                continue;
            size_t start = line.find_first_not_of(' ', at + std::strlen(option));
            if (start == std::string::npos)
                return std::string();
            size_t end = line.find_first_of(" \"", start);
            return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }
        return std::string();
    }();
    return script;
}

void UpdateRun(Runner& runner)
{
    if (runner.runDone || RequestedRun().empty() || !runner.ready)
        return;
    if (++runner.readyFrames < 30) // (a moment for the menu to settle, as -level)
        return;
    runner.runDone = true;
    const std::string& name = RequestedRun();
    std::string why;
    if (!IsIdentifier(name))
        why = "not a script name";
    else if (ScriptDenied(name))
        why = "the script is online / network";
    else
        why = NetRefusal();
    if (!why.empty()) {
        Log("automation: REFUSED -run %s: %s", name.c_str(), why.c_str());
        return;
    }
    std::string failed = SpawnNamed(name, {});
    Log("automation: -run %s -> %s", name.c_str(), failed.empty() ? "ok" : failed.c_str());
}

} // namespace

bool& Enabled()
{
    static bool on = ConfigInt("features", "automation", 0) != 0;
    return on;
}

void Update(Mdl::FrontEnd* frontEnd)
{
    // (not before the game's first frame: the early self-tests run the front end's input logic on stand-in state)
    if (Mlp::FramesRun() == 0)
        return;
    if (!Enabled() && RequestedRun().empty())
        return;
    Runner& runner = State();
    static bool s_named = false;
    if (!s_named) { // (-level's level by name in the status)
        s_named = true;
        std::string level = StartLevel::Requested();
        if (!level.empty())
            Remember(_strnicmp(level.c_str(), "load_", 5) == 0 ? level : "Load_" + level);
    }
    // (the main menu counts once it has taken pad events for a second: chosen away at once, while its start-up
    // scripts still ran, the next menu came up under a loading logo that stayed)
    runner.menuFrames = MainMenuUp() ? runner.menuFrames + 1 : 0;
    runner.menuSettled = runner.menuFrames >= kSettleFrames;
    if (!runner.ready && runner.menuSettled) {
        runner.ready = true;
        Log("automation: the main menu is up (frame %u)", Mlp::FramesRun());
    }
    UpdateRun(runner);
    if (!Enabled())
        return;
    TraceFrame();
    if (runner.holdFrames > 0 && --runner.holdFrames == 0)
        runner.holdButton = nullptr;
    PollFile(runner);
    if (runner.active)
        Continue(runner, frontEnd);
    for (int i = 0; i < 32 && !runner.active && !runner.queue.empty(); ++i)
        if (!Start(runner, frontEnd))
            break;
    WriteStatus(runner);
}

bool ParamsDenied(Script::CStruct* params) { return StructDenied(params, 0); }

std::string ElementRefusal(const CScreenElement* element) { return RefusalOfElement(element); }

void AddPadButtons(int port, uint8_t* data)
{
    if (port != 0 || !Enabled())
        return;
    for (const Button* button : {State().held, State().holdButton}) {
        if (!button)
            continue;
        data[button->byte] |= static_cast<uint8_t>(1u << button->bit);
        if (button->pressure)
            data[button->pressure] = 0xFF;
    }
}

} // namespace Front::Automation

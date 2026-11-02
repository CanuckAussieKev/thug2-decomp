// The test-automation channel (frontend/automation.h). NOT ORIGINAL.

#include "frontend/automation.h"

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
#include <array>

#include "engine/core/checksums.h"
#include "platform/winmain.h"
#include "engine/core/parse_number.h"
#include "frontend/front_end.h"
#include "frontend/mouse_menus.h"
#include "frontend/new_frontend.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "frontend/start_level.h"
#include "frontend/test_view.h"
#include "engine/gfx/device.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_manager.h"
#include "engine/objects/signal.h"
#include "engine/lists/key_map.h"
#include "engine/objects/entity.h"
#include "engine/objects/trick_objects.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/sys/mlp.h"
#include "platform/screenshot.h"
#include "platform/crash.h"
#include "platform/run_log.h"
#include "game/skate/point_tally.h"
#include "game/rider/skater_score.h"
#include "engine/util/unique_handle.h"

namespace frontend::automation {

namespace {

#include "frontend/automation_tables.inc"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/mem.h"

constexpr uint32_t kSystem = 0x36B2EE74;     // "system" (the menus' pad events' target and source)
constexpr uint32_t kFocusEvent = 0x9D3FB516; // "focus"
constexpr uint32_t kPadChoose = modules::FrontEnd::kPadChoose;
constexpr uint32_t kPadStart = modules::FrontEnd::kPadStart;
constexpr uint32_t kSkater = 0x5B8AB877;       // Skater (the local skater's alias)
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu (the skate module's level in the front end)

constexpr int kDefaultPressFrames = 3;
constexpr int kReleaseFrames = 3;
constexpr int kPressWaitSeconds = 5; // press: at most this long for the menus to take pad events
constexpr int kDefaultChooseSeconds = 10;
constexpr int kDefaultWaitSeconds = 90;
constexpr int kSettleFrames = 60; // waitfor level / ingame: how long the condition must hold
constexpr int kPartialLineFrames = 30;
constexpr int kMaxCaptureFrames =
    sys::screenshot::kMaxSeriesFrames; // a last line without its end of line, once the file has been still this long
constexpr int kMaxTraceFrames = 36000; // trace: ten minutes at 60 frames a second
constexpr uint32_t kStateModuleKey = 0x43686585;             // SkaterState
constexpr uint32_t kScoreModuleKey = 0xD262802F;             // SkaterScore
constexpr uint32_t kSkaterCorePhysicsComponent = 0x5BD63B29; // SkaterCorePhysics

uint32_t name_hash(const std::string& name) { return checksums::hash_from_text(name.c_str()); }

std::string get_lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool is_identifier(const std::string& text)
{
    if (text.empty() || std::isdigit(static_cast<unsigned char>(::core::at(text, 0))))
        return false;
    for (const char c : text)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    return true;
}

bool is_hex(const std::string& text)
{
    if (text.size() < 3 || text.size() > 10 || ::core::at(text, 0) != '0' || (::core::at(text, 1) != 'x' && ::core::at(text, 1) != 'X'))
        return false;
    for (size_t i = 2; i < text.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(::core::at(text, i))))
            return false;
    return true;
}

bool is_integer(const std::string& text)
{
    const size_t start = ::core::at(text, 0) == '-' ? 1 : 0;
    if (start >= text.size())
        return false;
    for (size_t i = start; i < text.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(::core::at(text, i))))
            return false;
    return true;
}

bool is_float(const std::string& text)
{
    const size_t start = ::core::at(text, 0) == '-' ? 1 : 0;
    bool digits = false, point = false;
    for (size_t i = start; i < text.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(::core::at(text, i))))
            digits = true;
        else if (::core::at(text, i) == '.' && !point)
            point = true;
        else
            return false;
    }
    return digits && point;
}

// An element id: a name or 0x<hex>.
bool is_id(const std::string& text) { return is_hex(text) || is_identifier(text); }

// A positive number that fits (false for anything else).
bool read_count(const std::string& text, int low, int high, int* out)
{
    if (!is_integer(text) || text.size() > 9)
        return false;
    const int value = text::to_int(text.c_str());
    if (value < low || value > high)
        return false;
    *out = value;
    return true;
}

// tokens[first...] as numbers (integers or decimals, signed); false if any isn't one.
bool read_numbers(const std::vector<std::string>& tokens, size_t first, std::vector<float>* out)
{
    out->clear();
    for (size_t i = first; i < tokens.size(); ++i) {
        if (!is_integer(::core::at(tokens, i)) && !is_float(::core::at(tokens, i)))
            return false;
        out->push_back(static_cast<float>(text::to_double(::core::at(tokens, i).c_str())));
    }
    return true;
}

// The words of a line: split at spaces, a quoted part kept whole (quotes included); `#` outside quotes ends it.
bool tokenize(const std::string& line, std::vector<std::string>* tokens, std::string* error)
{
    std::string token;
    bool quoted = false, any = false;
    for (const char c : line) {
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
std::string get_rest(const std::string& line)
{
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos)
        return {};
    const size_t gap = line.find_first_of(" \t", start);
    if (gap == std::string::npos)
        return {};
    const size_t text = line.find_first_not_of(" \t", gap);
    if (text == std::string::npos)
        return {};
    const size_t end = line.find_last_not_of(" \t\r\n");
    return line.substr(text, end + 1 - text);
}

bool parse_param(const std::string& token, Param* out, std::string* error)
{
    const size_t equals = token.find('=');
    if (equals == std::string::npos) {
        if (!is_identifier(token)) {
            *error = "not a flag: " + token;
            return false;
        }
        *out = {Param::Kind::kBitMark, "", token, 0, 0.0f};
        return true;
    }
    const std::string name = token.substr(0, equals);
    std::string value = token.substr(equals + 1);
    if (!is_identifier(name) || value.empty()) {
        *error = "not a parameter: " + token;
        return false;
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        *out = {Param::Kind::kChars, name, value.substr(1, value.size() - 2), 0, 0.0f};
    else if (value.find('"') != std::string::npos)
        return *error = "not a parameter: " + token, false;
    else if (is_integer(value) && value.size() <= 10)
        *out = {Param::Kind::kInteger, name, value, text::to_int(value.c_str()), 0.0f};
    else if (is_float(value))
        *out = {Param::Kind::kFloat, name, value, 0, static_cast<float>(text::to_double(value.c_str()))};
    else if (is_id(value))
        *out = {Param::Kind::kName, name, value, 0, 0.0f};
    else
        return *error = "not a value: " + value, false;
    return true;
}

constexpr auto kButtons = std::to_array<Button>({
    {"up", 2, 4, 10},   {"right", 2, 5, 8},   {"down", 2, 6, 11},   {"left", 2, 7, 9},
    {"x", 3, 6, 14},    {"circle", 3, 5, 13}, {"square", 3, 7, 15}, {"triangle", 3, 4, 12},
    {"start", 2, 3, 0}, {"select", 2, 0, 0},  {"l1", 3, 2, 16},     {"r1", 3, 3, 17},
    {"l2", 3, 0, 18},   {"r2", 3, 1, 19},     {"l3", 2, 1, 0},      {"r3", 2, 2, 0},
});
struct ButtonAlias {
    const char* alias;
    const char* name;
};
constexpr auto kButtonAliases = std::to_array<ButtonAlias>({
    {"cross", "x"},    {"choose", "x"}, {"enter", "x"},    {"a", "x"},         {"back", "circle"},
    {"esc", "circle"}, {"b", "circle"}, {"y", "triangle"}, {"pause", "start"},
});

// --- names ------------------------------------------------------------------------------------------------------

std::unordered_map<uint32_t, std::string>& get_names()
{
    static std::unordered_map<uint32_t, std::string>* names = [] {
        auto table = memory::make_owned<std::unordered_map<uint32_t, std::string>>();
        auto add = [table](const char* name) { table->emplace(checksums::hash_from_text(name), name); };
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

void remember(const std::string& name)
{
    if (is_identifier(name))
        get_names().emplace(name_hash(name), name);
}

uint32_t id_checksum(const std::string& id)
{
    if (is_hex(id))
        return static_cast<uint32_t>(std::strtoul(id.c_str() + 2, nullptr, 16));
    return name_hash(id);
}

const std::unordered_map<uint32_t, bool>& denied_checksums()
{
    static const std::unordered_map<uint32_t, bool>* denied = [] {
        auto table = memory::make_owned<std::unordered_map<uint32_t, bool>>();
        for (const char* name : kDeniedScripts)
            (*table)[checksums::hash_from_text(name)] = true;
        for (const char* name : kDeniedIds)
            (*table)[checksums::hash_from_text(name)] = true;
        return table;
    }();
    return *denied;
}

std::string json_string(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text) {
        auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u < 0x20 || u >= 0x7F) {
            std::array<char, 8> escaped{};
            std::snprintf(escaped.data(), sizeof(escaped), "\\u%04x", u);
            out += escaped.data();
        } else {
            out += c;
        }
    }
    return out + "\"";
}

std::string json_list(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i)
        out += (i ? ", " : "") + json_string(::core::at(items, i));
    return out + "]";
}

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

CommandLineOutcome parse_command(const std::string& line, OrderCode* out, std::string* error)
{
    std::vector<std::string> tokens;
    if (!tokenize(line, &tokens, error))
        return CommandLineOutcome::kError;
    if (tokens.empty())
        return CommandLineOutcome::kVacant;
    OrderCode command = {Op::kEcho, "", {}, Condition::kNone, 0, 0};
    const std::string keyword = get_lower(::core::at(tokens, 0));
    const size_t count = tokens.size();
    auto fail = [error](const std::string& why) {
        *error = why;
        return CommandLineOutcome::kError;
    };
    if (keyword == "run") {
        if (count < 2 || !is_identifier(::core::at(tokens, 1)))
            return fail("run <script> [name=value | flag]...");
        command.op = Op::kRun;
        command.target = ::core::at(tokens, 1);
        for (size_t i = 2; i < count; ++i) {
            Param param;
            if (!parse_param(::core::at(tokens, i), &param, error))
                return CommandLineOutcome::kError;
            command.params.push_back(param);
        }
    } else if (keyword == "choose" || keyword == "focus") {
        command.op = keyword == "choose" ? Op::kChoose : Op::kFocus;
        command.seconds = kDefaultChooseSeconds;
        if (count < 2 || count > 3 || !is_id(::core::at(tokens, 1)) ||
            (count == 3 && !read_count(::core::at(tokens, 2), 1, 3600, &command.seconds)))
            return fail(keyword + " <id> [seconds]");
        command.target = ::core::at(tokens, 1);
    } else if (keyword == "hold") {
        // (test tooling: press's button held without waiting - the next commands run while it's down)
        command.op = Op::kHold;
        command.frames = kDefaultPressFrames;
        if (count < 2 || count > 3 || (count == 3 && !read_count(::core::at(tokens, 2), 1, 3600, &command.frames)))
            return fail("hold <button> [frames]");
        const Button* button = find_button(::core::at(tokens, 1));
        if (!button)
            return fail("no such button: " + ::core::at(tokens, 1));
        command.target = button->name;
    } else if (keyword == "press") {
        command.op = Op::kPress;
        command.frames = kDefaultPressFrames;
        if (count < 2 || count > 3 || (count == 3 && !read_count(::core::at(tokens, 2), 1, 600, &command.frames)))
            return fail("press <button> [frames]");
        const Button* button = find_button(::core::at(tokens, 1));
        if (!button)
            return fail("no such button: " + ::core::at(tokens, 1));
        command.target = button->name;
    } else if (keyword == "wait") {
        command.op = Op::kStall;
        if (count != 2 || !read_count(::core::at(tokens, 1), 0, 36000, &command.frames))
            return fail("wait <frames>");
    } else if (keyword == "waitfor") {
        command.op = Op::kWaitFor;
        command.seconds = kDefaultWaitSeconds;
        if (count < 2)
            return fail("waitfor ready | frontend | ingame | level <name> | menu <id> | focus <id> [seconds]");
        const std::string what = get_lower(::core::at(tokens, 1));
        size_t next = 2;
        if (what == "fe") {
            command.condition = Condition::kNewFrontend;
            if (count < 3 || !is_identifier(::core::at(tokens, 2)))
                return fail("waitfor fe <screen> [seconds]");
            command.target = get_lower(::core::at(tokens, 2));
            next = 3;
        } else if (what == "ready")
            command.condition = Condition::kReady;
        else if (what == "frontend")
            command.condition = Condition::kFrontEnd;
        else if (what == "ingame")
            command.condition = Condition::kInGame;
        else if (what == "level" || what == "menu" || what == "focus") {
            command.condition = what == "level"  ? Condition::kStageMap
                                : what == "menu" ? Condition::kMenu
                                                 : Condition::kFocus;
            if (count < 3 || !(command.condition == Condition::kStageMap ? is_identifier(::core::at(tokens, 2)) : is_id(::core::at(tokens, 2))))
                return fail("waitfor " + what + " <" + (what == "level" ? "name" : "id") + "> [seconds]");
            command.target = ::core::at(tokens, 2);
            next = 3;
        } else {
            return fail("no such condition: " + ::core::at(tokens, 1));
        }
        if (count > next + 1 || (count == next + 1 && !read_count(::core::at(tokens, next), 1, 3600, &command.seconds)))
            return fail("waitfor: a timeout is whole seconds (1-3600)");
    } else if (keyword == "level") {
        command.op = Op::kStageMap;
        if (count != 2 || !is_identifier(::core::at(tokens, 1)))
            return fail("level <name>");
        command.target = ::core::at(tokens, 1);
    } else if (keyword == "teleport") {
        command.op = Op::kJumpTo;
        // (a last word `board`: the skater put on its board instead of standing - test_view::JumpTo's on_board)
        std::vector<std::string> words = tokens;
        if (words.size() >= 5 && get_lower(words.back()) == "board") {
            command.target = "board";
            words.pop_back();
        }
        if ((words.size() != 4 && words.size() != 5) || !read_numbers(words, 1, &command.numbers))
            return fail("teleport <x> <y> <z> [heading] [board]");
    } else if (keyword == "camera") {
        command.op = Op::kViewCam;
        if (count == 2 && get_lower(::core::at(tokens, 1)) == "off")
            command.target = "off";
        else if ((count != 7 && count != 10) || !read_numbers(tokens, 1, &command.numbers))
            return fail("camera <x> <y> <z> <dx> <dy> <dz> [<drift x> <drift y> <drift z>] | camera off");
        else if (::core::at(command.numbers, 3) == 0.0f && ::core::at(command.numbers, 4) == 0.0f && ::core::at(command.numbers, 5) == 0.0f)
            return fail("camera: the direction is zero");
    } else if (keyword == "capture") {
        command.op = Op::kCapture;
        if (count < 2 || count > 3 || !read_count(::core::at(tokens, 1), 1, kMaxCaptureFrames, &command.frames))
            return fail("capture <frames 1-" + std::to_string(kMaxCaptureFrames) + "> [folder]");
        if (count == 3) {
            std::string folder = ::core::at(tokens, 2);
            if (folder.size() >= 2 && folder.front() == '"' && folder.back() == '"')
                folder = folder.substr(1, folder.size() - 2);
            if (folder.empty() || folder.find("..") != std::string::npos)
                return fail("capture: a folder name (no ..)");
            command.target = folder;
        }
    } else if (keyword == "trace") {
        command.op = Op::kTrace;
        if (count != 2 || !read_count(::core::at(tokens, 1), 1, kMaxTraceFrames, &command.frames))
            return fail("trace <frames 1-" + std::to_string(kMaxTraceFrames) + ">");
    } else if (keyword == "logtricks") {
        command.op = Op::kLogTricks;
        command.frames = 0; // (every trick object)
        if (count > 2 || (count == 2 && !read_count(::core::at(tokens, 1), 1, 100000, &command.frames)))
            return fail("logtricks [count]");
    } else if (keyword == "fe") {
        command.op = Op::kFrontend;
        command.seconds = 10;
        command.target = get_rest(line);
        if (count < 2)
            return fail("fe select <screen>/<item> | fe focus <screen>/<item> | fe back | fe press <input>");
        // (select / focus: a last number is the timeout - the front end ignores it)
        if (count == 4 && (get_lower(::core::at(tokens, 1)) == "select" || get_lower(::core::at(tokens, 1)) == "focus") &&
            !read_count(::core::at(tokens, 3), 1, 3600, &command.seconds))
            return fail("fe " + get_lower(::core::at(tokens, 1)) + ": a timeout is whole seconds (1-3600)");
    } else if (keyword == "echo") {
        command.op = Op::kEcho;
        command.target = get_rest(line);
    } else if (keyword == "crash") {
        command.op = Op::kCrash;
        TestFault fault{};
        if (count != 2 || !parse_test_fault(::core::at(tokens, 1), &fault))
            return fail(std::string("crash <kind>: ") + test_fault_names());
        command.target = test_fault_name(fault);
    } else if (keyword == "quit") {
        command.op = Op::kQuit;
        if (count != 1)
            return fail("quit takes nothing");
    } else {
        return fail("no such command: " + ::core::at(tokens, 0));
    }
    *out = command;
    return CommandLineOutcome::kOrderCode;
}

const Button* find_button(const std::string& name)
{
    std::string low = get_lower(name);
    for (const auto& alias : kButtonAliases)
        if (low == alias.alias)
            low = alias.name;
    for (const Button& button : kButtons)
        if (low == button.name)
            return &button;
    return nullptr;
}

bool button_chooses(const Button& button) { return !(button.byte == 2 && button.bit >= 4); }

bool name_looks_online(const std::string& name)
{
    static const auto kWords = std::to_array<const char*>({"net",     "online", "internet", "lobby", "lobbies", "login",
                                         "logon",   "buddy",  "buddies",  "homie", "homies",  "gamespy",
                                         "network", "server", "servers",  "join",  "host",    "lan"});
    static const auto kParts = std::to_array<const char*>({"internet", "lobby", "gamespy", "network", "login"});
    const std::string low = get_lower(name);
    for (const char* part : kParts)
        if (low.find(part) != std::string::npos)
            return true;
    size_t start = 0;
    while (start <= low.size()) {
        size_t end = low.find('_', start);
        if (end == std::string::npos)
            end = low.size();
        const std::string word = low.substr(start, end - start);
        for (const char* online : kWords)
            if (word == online)
                return true;
        start = end + 1;
    }
    return false;
}

bool script_denied(const std::string& name) { return name_looks_online(name) || checksum_denied(name_hash(name)); }

bool checksum_denied(uint32_t checksum) { return denied_checksums().count(checksum) != 0; }

bool id_denied(uint32_t id, const std::string& name)
{
    return checksum_denied(id) || (!name.empty() && name_looks_online(name));
}

std::string refusal_by_name(const OrderCode& command)
{
    switch (command.op) {
    case Op::kRun:
        if (script_denied(command.target))
            return "the script " + command.target + " is online / network";
        for (const Param& param : command.params) {
            const std::string& name = param.kind == Param::Kind::kBitMark ? param.text : param.name;
            if (name_looks_online(name) || checksum_denied(name_hash(name)))
                return "the parameter " + name + " is online / network";
            if ((param.kind == Param::Kind::kName || param.kind == Param::Kind::kChars) && script_denied(param.text))
                return "the value " + param.text + " is online / network";
        }
        return {};
    case Op::kChoose:
    case Op::kFocus:
        if (id_denied(id_checksum(command.target), is_hex(command.target) ? "" : command.target))
            return "the element " + command.target + " is online / network";
        return {};
    default:
        return {};
    }
}

std::string get_name_of(uint32_t checksum)
{
    auto found = get_names().find(checksum);
    if (found != get_names().end())
        return found->second;
    std::array<char, 16> text{};
    std::snprintf(text.data(), sizeof(text), "0x%08X", checksum);
    return text.data();
}

std::string describe(const OrderCode& command)
{
    switch (command.op) {
    case Op::kRun: {
        std::string text = "run " + command.target;
        for (const Param& param : command.params) {
            text += " ";
            if (param.kind == Param::Kind::kBitMark)
                text += param.text;
            else
                text += param.name + "=" + (param.kind == Param::Kind::kChars ? "\"" + param.text + "\"" : param.text);
        }
        return text;
    }
    case Op::kChoose:
        return "choose " + command.target;
    case Op::kFocus:
        return "focus " + command.target;
    case Op::kPress:
        return "press " + command.target + " " + std::to_string(command.frames);
    case Op::kHold:
        return "hold " + command.target + " " + std::to_string(command.frames);
    case Op::kStall:
        return "wait " + std::to_string(command.frames);
    case Op::kWaitFor: {
        static const auto kConditions = std::to_array<const char*>({"?", "ready", "frontend", "ingame", "level", "menu", "focus", "fe"});
        std::string text = std::string("waitfor ") + ::core::at(kConditions, static_cast<int>(command.condition));
        if (!command.target.empty())
            text += " " + command.target;
        return text;
    }
    case Op::kStageMap:
        return "level " + command.target;
    case Op::kEcho:
        return "echo " + command.target;
    case Op::kFrontend:
        return "fe " + command.target;
    case Op::kQuit:
        return "quit";
    case Op::kCrash:
        return "crash " + command.target;
    case Op::kJumpTo:
    case Op::kViewCam: {
        if (command.target == "off")
            return "camera off";
        std::string text = command.op == Op::kJumpTo ? "teleport" : "camera";
        for (const float number : command.numbers) {
            std::array<char, 32> value{};
            std::snprintf(value.data(), sizeof(value), " %g", static_cast<double>(number));
            text += value.data();
        }
        if (command.op == Op::kJumpTo && command.target == "board")
            text += " board";
        return text;
    }
    case Op::kCapture:
        return "capture " + std::to_string(command.frames) + (command.target.empty() ? "" : " " + command.target);
    case Op::kLogTricks:
        return "logtricks" + (command.frames ? " " + std::to_string(command.frames) : std::string());
    case Op::kTrace:
        return "trace " + std::to_string(command.frames);
    }
    return "?";
}

std::string status_json(const Standing& status)
{
    std::string out = "{\n";
    auto field = [&out](const char* name, const std::string& value, bool last = false) {
        out += std::string("  \"") + name + "\": " + value + (last ? "\n" : ",\n");
    };
    auto boolean = [](bool value) { return std::string(value ? "true" : "false"); };
    field("frames", std::to_string(status.frames));
    field("ready", boolean(status.ready));
    field("main_menu", boolean(status.main_menu_));
    field("where", json_string(status.where));
    field("paused", boolean(status.paused));
    field("loading", boolean(status.loading));
    field("level", json_string(status.level));
    field("game_mode", json_string(status.game_mode_));
    field("focus", json_list(status.focus));
    field("selected", json_list(status.selected));
    field("lines_read", std::to_string(status.lines_read_));
    field("lines_done", std::to_string(status.lines_done_));
    field("queued", std::to_string(status.queued));
    field("current", json_string(status.current));
    field("last", json_string(status.last));
    field("result", json_string(status.result));
    field("errors", std::to_string(status.errors));
    if (!status.frontend.empty())
        field("frontend", json_string(status.frontend));
    field("refusals", std::to_string(status.refusals), true);
    return out + "}\n";
}

// --- the game ---------------------------------------------------------------------------------------------------

namespace {

using mouse_menus::child_count;
using mouse_menus::is_menu;
using mouse_menus::selected_index;

modules::Skate* skate_module() { return modules::g_skate_module.get(); }
// The level loaded (its load script's checksum, the skate module's +0x378; load_mainmenu in the front end).
uint32_t current_load_state() { return skate_module() ? modules::g_skate_module->level_ : 0; }
bool in_front_end() { return current_load_state() == kLoadMainMenu; }

UiWidget* first_child(const UiWidget* element) { return element->eldest_.get_pull(); }
UiWidget* next_sibling(const UiWidget* element) { return element->following_.get_pull(); }
UiWidget* current_parent(const UiWidget* element) { return element->container_.get_pull(); }
uint32_t ident_no(const UiWidget* element) { return element->ident_; }

UiWidget* child_at(const UiWidget* menu, int index)
{
    if (index < 0)
        return nullptr;
    UiWidget* child = first_child(menu);
    for (; child && index > 0; --index)
        child = next_sibling(child);
    return child;
}

int get_index_of(const UiWidget* menu, const UiWidget* item)
{
    int index = 0;
    for (const UiWidget* child = first_child(menu); child; child = next_sibling(child), ++index)
        if (child == item)
            return index;
    return -1;
}

bool hidden_in_chain(const UiWidget* element)
{
    for (; element; element = current_parent(element))
        if (element->object_flags_ & UiWidget::kHidden)
            return true;
    return false;
}

// The element `id` if it exists (null if not); `holder` keeps it alive.
UiWidget* find_element(uint32_t id, ElementPtr* holder)
{
    holder->get_release();
    if (!UiTree::sole())
        return nullptr;
    fetch_part(holder, id);
    return holder->get_pull();
}

// Whether a script's parameters name a denied script or id (structures and arrays too).
bool struct_denied(script::Struct* params, int depth);

bool value_denied(uint32_t type, script::Cell value, int depth)
{
    switch (type) {
    case script::kQbvalName:
        return checksum_denied(value.word());
    case script::kQbvalStruct:
    case script::kQbvalStructRef:
        return struct_denied(value.structure(), depth + 1);
    case script::kQbvalArray:
    case script::kEsymboltypeArraypointer: {
        script::Array* array = value.array();
        if (!array || depth > 8)
            return false;
        const script::Cell* elements = array->find_array_value();
        for (uint32_t i = 0; elements && i < array->extent_; ++i)
            if (value_denied(array->kind_tag_, elements[i], depth + 1))
                return true;
        return false;
    }
    default:
        return false;
    }
}

bool struct_denied(script::Struct* params, int depth)
{
    if (!params || depth > 8)
        return false;
    for (script::Component* component = params->step_to_entry(nullptr); component;
         component = params->step_to_entry(component)) {
        if (checksum_denied(component->name) || value_denied(component->kind_tag(), component->value, depth))
            return true;
    }
    return false;
}

// Whether an element's event handlers (all, or only pad_choose / pad_start ones) run or name a denied script.
bool handlers_denied(const UiWidget* element, bool choose_only)
{
    const script::Script* script = element->own_script_;
    if (!script || !script->signal_routes_)
        return false;
    const objects::SignalRouteTable* table = script->signal_routes_;
    for (int i = 0; i < table->num_; ++i) {
        const objects::SignalRouteTable::TableRow& entry = table->entries_[i];
        if (entry.script == objects::kNullScript)
            continue;
        if (choose_only && entry.type != kPadChoose && entry.type != kPadStart)
            continue;
        if (checksum_denied(entry.script) || struct_denied(entry.params, 0))
            return true;
    }
    return false;
}

// Why choosing this element would be refused ("" if it wouldn't): its id, its own handlers, its parents' choose
// handlers.
std::string refusal_of_element(const UiWidget* element)
{
    if (checksum_denied(ident_no(element)))
        return "the element " + get_name_of(ident_no(element)) + " is online / network";
    if (handlers_denied(element, false))
        return "the element " + get_name_of(ident_no(element)) + "'s handlers lead online / to the network";
    for (const UiWidget* parent = current_parent(element); parent; parent = current_parent(parent))
        if (handlers_denied(parent, true))
            return "the menu " + get_name_of(ident_no(parent)) + "'s choose handlers lead online / to the network";
    return {};
}

// The focused elements (controller 0's focus list) and the selected items of the focused menus.
void focused_elements(std::vector<UiWidget*>* focused, std::vector<UiWidget*>* selected)
{
    const UiTree* manager = UiTree::sole();
    if (!manager)
        return;
    int guard = 0;
    for (const UiTree::FocusEntry* entry = ::core::at(manager->focus_lists_, 0); entry && guard < 64;
         entry = entry->next, ++guard) {
        UiWidget* element = entry->element.get_pull();
        if (!element)
            continue;
        focused->emplace_back(element);
        if (is_menu(element))
            if (UiWidget* item = child_at(element, selected_index(element)))
                selected->emplace_back(item);
    }
}

// Why the game state refuses actions ("" if it doesn't): online, a network game, a network game mode.
std::string net_refusal()
{
    if (net::Hub* net = net::Hub::sole())
        if (net->is_online())
            return "the game is online";
    if (void* game_net = online::g_online_hub)
        if (static_cast<online::Hub*>(game_net)->in_net_game())
            return "a network game runs";
    if (goals::MatchRules* mode = goals::g_game_mode.get()) {
        const std::string name = get_name_of(mode->get_name_key());
        if (name.starts_with("net"))
            return "the game mode is a network one (" + name + ")";
    }
    return {};
}

std::string active_ruleset_label()
{
    goals::MatchRules* mode = goals::g_game_mode.get();
    return mode ? get_name_of(mode->get_name_key()) : "";
}

// The main menu: its menu up and taking pad events, in the front end.
bool main_menu_up()
{
    static const uint32_t kMainVMenu = checksums::hash_from_text("main_vmenu");
    if (!in_front_end())
        return false;
    ElementPtr holder;
    UiWidget* menu = find_element(kMainVMenu, &holder);
    return menu && is_menu(menu) && mouse_menus::menu_gets_pad_events(menu);
}

bool skater_present() { return objects::find_object(kSkater) != nullptr; }

// --- trace: the skater frame by frame ---------------------------------------------------------------------------

struct Trace {
    util::UniqueFile file;
    int left = 0;
};
Trace& trace_state()
{
    static Trace trace;
    return trace;
}

void stop_trace()
{
    Trace& trace = trace_state();
    trace.file.reset();
    trace.left = 0;
}

std::string start_trace(int frames)
{
    stop_trace();
    const std::string path = game_folder_path("thug2-decomp-trace.txt");
    util::UniqueFile file(_fsopen(path.c_str(), "w", _SH_DENYWR));
    if (!file)
        return "can't write " + path;
    std::fprintf(
        file.get(),
        "# frame x y z vx vy vz up_y state physics bailing trick pot multiplier landed ground_flags ground_object\n");
    std::fflush(file.get());
    trace_state().file = std::move(file);
    trace_state().left = frames;
    return {};
}

// Once a frame: the skater's line (nothing while there's none - a level loading).
void trace_frame()
{
    Trace& trace = trace_state();
    if (!trace.file)
        return;
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (skater) {
        auto* state = core::down_cast<objects::RiderPoseState>(skater->fetch_module(kStateModuleKey));
        const int where = state ? state->cur_phase_ : -1;
        const int physics = state ? state->physics_mode_ : -1;
        const int bailing = state ? state->is_on(objects::PoseFlag::kBailing) : -1;
        const int trick = state ? state->in_trick_move_ : -1;
        // the score: the combo so far (+0x14) and its multiplier (+0x60), the last score landed (+0x20)
        const auto* score_component = core::down_cast<objects::RiderPointKeeper>(skater->fetch_module(kScoreModuleKey));
        const modules::PointTally* score = score_component ? score_component->points_held_ : nullptr;
        const int pot = score ? score->pot_ : -1;
        const float multiplier = score ? score->multiplier_ : 0.0f;
        const int landed = score ? score->pot_score_ : -1;
        // the core physics' last ground hit (its feeler copy at +0x158): the face's flags and the object's checksum
        auto* core = core::down_cast<skater::SkaterPhysicsModule>(skater->fetch_module(kSkaterCorePhysicsComponent));
        const unsigned ground_flags = core ? core->floor_feeler_.hit_.face_flags_ : 0;
        const unsigned ground_object = core ? core->floor_feeler_.hit_.object_checksum_ : 0;
        std::fprintf(trace.file.get(), "%u %.2f %.2f %.2f %.2f %.2f %.2f %.3f %d %d %d %d %d %.2f %d 0x%04X 0x%08X\n",
                     frame_loop::frames_run(), static_cast<double>(skater->at_point_.x),
                     static_cast<double>(skater->at_point_.y), static_cast<double>(skater->at_point_.z),
                     static_cast<double>(skater->vel_.x), static_cast<double>(skater->vel_.y),
                     static_cast<double>(skater->vel_.z), static_cast<double>(::core::at(skater->basis_frame_.row, 1).y), where,
                     physics, bailing, trick, pot, static_cast<double>(multiplier), landed, ground_flags,
                     ground_object);
        std::fflush(trace.file.get());
    }
    if (--trace.left <= 0)
        stop_trace();
}

// --- the command file -------------------------------------------------------------------------------------------

struct Seg {
    std::string text;
    int number;
};

struct Runner {
    // the file
    bool looked = false;
    uint64_t size = 0, time = 0, offset = 0;
    std::string partial;
    int still_frames_ = 0;
    int lines_read_ = 0, lines_done_ = 0;
    std::deque<Seg> queue;
    // the running command
    bool active = false;
    OrderCode command;
    std::string text;
    DWORD started = 0;
    int counter = 0;
    int phase = 0;
    mouse_menus::Target target = {};
    bool target_set_ = false;
    // the button held (port 0)
    const Button* held = nullptr;
    // `hold`'s button (port 0), held for `hold_frames_` more frames while later commands run
    const Button* hold_button_ = nullptr;
    int hold_frames_ = 0;
    // the status
    std::string last, result = "ok";
    int errors = 0, refusals = 0;
    bool ready = false;
    std::string written;
    uint32_t written_frames_ = 0;
    // -run
    bool run_done_ = false;
    int ready_frames_ = 0;
    // the main menu: frames in a row it has been up, and whether that's a second
    int menu_frames_ = 0;
    bool menu_settled_ = false;
};

Runner& get_state()
{
    static auto runner = memory::make_owned<Runner>(); // (never freed: nothing runs on the game's memory at exit)
    return *runner;
}

// The menu the running choose / focus is moving (cleared if it dies).
ElementPtr& target_menu()
{
    static auto menu = memory::make_owned<ElementPtr>();
    return *menu;
}

uint64_t file_time_value(const FILETIME& time)
{
    return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

uint64_t process_start()
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0;
    return file_time_value(created);
}

void add_line(Runner& runner, const std::string& text)
{
    ++runner.lines_read_;
    // (a UTF-8 byte order mark - PowerShell's Out-File writes one - isn't part of the command)
    const bool bom = text.size() >= 3 && text.starts_with("\xEF\xBB\xBF");
    runner.queue.push_back({bom ? text.substr(3) : text, runner.lines_read_});
}

void poll_file(Runner& runner)
{
    const std::string path = game_folder_path("thug2-decomp-commands.txt");
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &info)) {
        runner.looked = true;
        return;
    }
    const uint64_t size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    const uint64_t time = file_time_value(info.ftLastWriteTime);
    if (!runner.looked) {
        runner.looked = true;
        // (a file from before this run - written more than 10 s before the game started - isn't run)
        const uint64_t start = process_start();
        if (start && time + 10ull * 10000000ull < start) {
            get_log("automation: %s is older than this run - not run", path.c_str());
            runner.size = runner.offset = size;
            runner.time = time;
            return;
        }
    }
    if (size == runner.size && time == runner.time) {
        // A last line without an end of line, once the file has been still a moment.
        if (!runner.partial.empty() && ++runner.still_frames_ >= kPartialLineFrames) {
            add_line(runner, runner.partial);
            runner.partial.clear();
        }
        return;
    }
    runner.still_frames_ = 0;
    if (size < runner.offset) {
        get_log("automation: the command file got shorter - read from its start again");
        runner.offset = 0;
        runner.partial.clear();
        runner.queue.clear();
        runner.lines_read_ = runner.lines_done_ = 0;
    }
    util::UniqueHandle file(CreateFileA(path.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return; // (try again next frame)
    LARGE_INTEGER at;
    at.QuadPart = static_cast<LONGLONG>(runner.offset);
    std::string data;
    if (SetFilePointerEx(file.get(), at, nullptr, FILE_BEGIN) && size > runner.offset &&
        size - runner.offset < (1u << 20)) {
        data.resize(static_cast<size_t>(size - runner.offset));
        DWORD read = 0;
        if (!ReadFile(file.get(), &::core::at(data, 0), static_cast<DWORD>(data.size()), &read, nullptr))
            read = 0;
        data.resize(read);
    }
    file.reset();
    runner.size = size;
    runner.time = time;
    runner.offset += data.size();
    runner.partial += data;
    size_t end = 0;
    while ((end = runner.partial.find('\n')) != std::string::npos) {
        std::string line = runner.partial.substr(0, end);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        add_line(runner, line);
        runner.partial.erase(0, end + 1);
    }
}

// --- running commands -------------------------------------------------------------------------------------------

void get_finish(Runner& runner, const std::string& result)
{
    runner.active = false;
    runner.last = runner.text;
    runner.result = result;
    ++runner.lines_done_;
    get_log("automation: %s -> %s", runner.text.c_str(), result.c_str());
    target_menu().get_release();
    runner.target_set_ = false;
}

// A failure: the rest of the batch is skipped.
void get_fail(Runner& runner, const std::string& result, bool refused)
{
    if (refused) {
        ++runner.refusals;
        get_log("automation: REFUSED %s: %s", runner.text.c_str(), result.c_str());
    } else {
        ++runner.errors;
    }
    get_finish(runner, (refused ? "refused: " : "error: ") + result);
    if (!runner.queue.empty())
        get_log("automation: %d command line(s) after it skipped", static_cast<int>(runner.queue.size()));
    runner.lines_done_ += static_cast<int>(runner.queue.size());
    runner.queue.clear();
}

bool get_timed_out(const Runner& runner, int seconds)
{
    return GetTickCount() - runner.started > static_cast<DWORD>(seconds) * 1000u;
}

gsl::owner<script::Struct*> make_params(const std::vector<Param>& params)
{
    gsl::owner<script::Struct*> out = script::new_struct();
    for (const Param& param : params) {
        switch (param.kind) {
        case Param::Kind::kBitMark:
            out->put_name_key(0u, name_hash(param.text));
            break;
        case Param::Kind::kInteger:
            out->put_int(name_hash(param.name), param.integer);
            break;
        case Param::Kind::kFloat:
            out->push_float(name_hash(param.name), param.real);
            break;
        case Param::Kind::kChars:
            out->append_word(name_hash(param.name), param.text.c_str());
            break;
        case Param::Kind::kName:
            out->put_name_key(name_hash(param.name), id_checksum(param.text));
            break;
        }
    }
    return out;
}

bool script_exists(uint32_t name)
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name);
    return symbol && (symbol->type == script::kQbvalScriptBody || symbol->type == script::kQbvalNativeFunc);
}

// Spawn a script (run's and -run's); "" if it ran, else why not.
std::string spawn_named(const std::string& name, const std::vector<Param>& params)
{
    const uint32_t checksum = name_hash(name);
    if (!script_exists(checksum))
        return "no script " + name;
    gsl::owner<script::Struct*> structure = make_params(params);
    script::spawn_script(checksum, structure, 0, nullptr, 0, 0, false, false, false, false);
    structure->get_clear();
    script::delete_struct(structure);
    return {};
}

int read_target(void* /*context*/) { return target_menu() ? selected_index(target_menu().get_pull()) : -1; }

void send_step(void* context, int direction)
{
    auto* front_end = static_cast<modules::FrontEnd*>(context);
    const bool vertical = target_menu() && mouse_menus::menu_runs_top_to_bottom(target_menu().get_pull());
    const uint32_t event = vertical ? (direction < 0 ? modules::FrontEnd::kPadUp : modules::FrontEnd::kPadDown)
                                    : (direction < 0 ? modules::FrontEnd::kPadLeft : modules::FrontEnd::kPadRight);
    mouse_menus::send_menu_pad_event(front_end, event);
}

// Whether the front end drops menu pad events now (as input_logic / send_menu_pad_event check): the first controller's
// mapping inactive, menu input blocked, no input device.
bool menu_input_blocked(modules::FrontEnd* front_end)
{
    if (!front_end || ::core::at(front_end->pad_mappings_, 0).mapping_ == 0 || front_end->block_menu_input_)
        return true;
    const modules::FrontEndInputHandler* handler = ::core::at(front_end->pad_listeners_, 0).get();
    return !handler || !handler->dev_;
}

// Whether a pad press would be lost on the menus now: in the front end or the pause menu, menu input blocked, or a
// menu has the focus but none of the focused menus takes pad events (a menu still coming in).
bool menus_ignore_pad(modules::FrontEnd* front_end)
{
    if (!in_front_end() && !(front_end && front_end->is_halted_))
        return false;
    if (menu_input_blocked(front_end))
        return true;
    std::vector<UiWidget*> focused, selected;
    focused_elements(&focused, &selected);
    bool any_menu = false;
    for (UiWidget* element : focused) {
        if (!is_menu(element))
            continue;
        any_menu = true;
        if (mouse_menus::menu_gets_pad_events(element))
            return false;
    }
    return any_menu;
}

// Why pressing the button now is refused ("" if it isn't): a button that can choose, with a focused element (or a
// focused menu's selected item) that leads online.
std::string press_refusal(const Button& button)
{
    if (!button_chooses(button))
        return {};
    std::vector<UiWidget*> focused, selected;
    focused_elements(&focused, &selected);
    for (auto* list : {&selected, &focused}) {
        for (const UiWidget* element : *list) {
            std::string refusal = refusal_of_element(element);
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
bool loading_screen_up()
{
    return render::g_loading_screen_shown != 0 || render::g_loading_bar_timer != 0 ||
           static_cast<uint8_t>(render::g_loading_screen_up) != 0;
}

// choose / focus, once a frame until done.
void step_choose(Runner& runner, modules::FrontEnd* front_end)
{
    const bool choose = runner.command.op == Op::kChoose;
    const uint32_t id = id_checksum(runner.command.target);
    ElementPtr holder;
    UiWidget* item = find_element(id, &holder);
    if (!item || hidden_in_chain(item)) {
        if (get_timed_out(runner, runner.command.seconds))
            get_fail(runner, item ? "the element is hidden" : "no element " + runner.command.target, false);
        return;
    }
    // A menu given: its selected item (choose) - or the focus event to the menu (focus).
    UiWidget* menu = nullptr;
    const UiWidget* chosen = item;
    if (is_menu(item)) {
        if (!choose) {
            const std::string why = refusal_of_element(item);
            if (!why.empty())
                return get_fail(runner, why, true);
            objects::get_tracker()->launch_event(kFocusEvent, id, kSystem, nullptr, false, -1.0f);
            return get_finish(runner, "ok (focus sent to the menu)");
        }
        menu = item;
        chosen = child_at(menu, selected_index(menu));
        if (!chosen) {
            if (get_timed_out(runner, runner.command.seconds))
                get_fail(runner, "the menu " + get_name_of(id) + " has no item selected", false);
            return;
        }
    } else {
        menu = current_parent(item);
    }
    const std::string why = refusal_of_element(chosen);
    if (!why.empty())
        return get_fail(runner, why, true);
    if (!menu || !is_menu(menu)) {
        // Not a menu's item: the event goes to it.
        objects::get_tracker()->launch_event(choose ? kPadChoose : kFocusEvent, id, kSystem, nullptr, false, -1.0f);
        return get_finish(runner, choose ? "ok (pad_choose sent to the element)" : "ok (focus sent to the element)");
    }
    const int index = get_index_of(menu, chosen);
    const UiWidget* current = child_at(menu, selected_index(menu));
    // (nothing sent while the menu doesn't take pad events, the front end blocks menu input - SetButtonEventMappings
    // block_menu_input, TemporarilyDisableInput - or the menu is on an item it passes over by itself)
    const bool wait = !mouse_menus::menu_gets_pad_events(menu) || menu_input_blocked(front_end) ||
                      (current && mouse_menus::item_passed_over(current)) ||
                      (loading_screen_up() && !get_timed_out(runner, kPressWaitSeconds));
    if (wait && get_timed_out(runner, runner.command.seconds))
        return get_fail(runner, "its menu " + get_name_of(ident_no(menu)) + " didn't take pad events", false);
    if (!runner.target_set_ || target_menu().get_pull() != menu) {
        if (wait)
            return;
        target_menu().assign_value(menu);
        runner.target = mouse_menus::start_target(index, child_count(menu), mouse_menus::menu_wraps(menu));
        runner.target_set_ = true;
    }
    runner.target.count = child_count(menu);
    const mouse_menus::TargetState state =
        mouse_menus::advance_target(runner.target, wait, read_target, send_step, front_end);
    if (state == mouse_menus::TargetState::kMoving)
        return;
    if (state == mouse_menus::TargetState::kGaveUp)
        return get_fail(runner, "its menu's selection didn't get to it", false);
    if (wait)
        return; // (there - the choose waits for the menu to take it)
    if (choose)
        mouse_menus::send_menu_pad_event(front_end, kPadChoose);
    get_finish(runner, "ok");
}

bool condition_holds(const OrderCode& command)
{
    switch (command.condition) {
    case Condition::kReady:
        return get_state().menu_settled_;
    case Condition::kFrontEnd:
        return in_front_end();
    case Condition::kNewFrontend:
        return frontend::new_frontend::automation_screen_up(command.target);
    case Condition::kInGame: {
        auto* front_end = modules::g_front_end_instance.get();
        return current_load_state() && !in_front_end() && skater_present() && !(front_end && front_end->is_halted_);
    }
    case Condition::kStageMap: {
        std::string name = command.target;
        if (_strnicmp(name.c_str(), "load_", 5) != 0)
            name = "Load_" + name;
        return current_load_state() == name_hash(name) && skater_present();
    }
    case Condition::kMenu: {
        ElementPtr holder;
        return find_element(id_checksum(command.target), &holder) != nullptr;
    }
    case Condition::kFocus: {
        const uint32_t id = id_checksum(command.target);
        std::vector<UiWidget*> focused, selected;
        focused_elements(&focused, &selected);
        for (const UiWidget* element : focused)
            if (ident_no(element) == id)
                return true;
        for (const UiWidget* element : selected)
            if (ident_no(element) == id)
                return true;
        return false;
    }
    default:
        return false;
    }
}

// The condition has held long enough: a level / in game for a second in a row (the level's first frames - its intro
// camera, the skater set down - pass first), the others at once.
bool condition_met(Runner& runner)
{
    if (!condition_holds(runner.command)) {
        runner.counter = 0;
        return false;
    }
    const Condition condition = runner.command.condition;
    const int frames = condition == Condition::kStageMap || condition == Condition::kInGame ? kSettleFrames : 1;
    return ++runner.counter >= frames;
}

// `logtricks [count]` (a test of the graffiti change mask, TP-R7): one trick by
// skater 0 over the level's trick objects (every one, or the first `count` in the table's order), through the trick
// object manager's log_trick_objects as a skater's trick goes - more than THUG2's own mask held crashed there.
std::string log_tricks(int count, std::string* failed)
{
    auto* manager = objects::g_trick_spot_hub.get();
    if (!manager) {
        *failed = "no trick object manager";
        return "";
    }
    std::vector<uint32_t> objects;
    std::vector<void*> clusters;
    const lists::KeyMap& table = manager->object_clusters_;
    for (int bucket = 0; bucket < (1 << table.bits_); ++bucket) {
        for (const lists::KeyMap::Item* item = &table.buckets_[bucket]; item; item = item->next) {
            if (!item->value)
                continue;
            objects.push_back(item->key);
            if (std::ranges::find(clusters, item->value) == clusters.end())
                clusters.push_back(item->value);
        }
    }
    if (count > 0 && objects.size() > static_cast<size_t>(count))
        objects.resize(static_cast<size_t>(count));
    const size_t asked = objects.size();
    objects.push_back(0xEEEEEEEE); // (past the list: must be left alone)
    std::array<uint8_t, 0x100> stolen = {};
    const uint32_t changed =
        manager->log_trick_objects(static_cast<uint32_t>(asked), objects.data(), stolen.data(), 0, 1000000);
    if (::core::at(objects, asked) != 0xEEEEEEEE) {
        *failed = "the word past the list was changed";
        return "";
    }
    std::array<char, 160> result{};
    std::snprintf(result.data(), sizeof(result), "ok (%u trick objects of %u clusters in the level logged, %u changed hands)",
                  static_cast<unsigned>(asked), static_cast<unsigned>(clusters.size()), changed);
    return result.data();
}

// Start the next command; true if it's done already (the next can start this frame).
void write_status(Runner& runner);

bool kickoff(Runner& runner, modules::FrontEnd* front_end)
{
    const Seg line = runner.queue.front();
    runner.queue.pop_front();
    std::string error;
    OrderCode command;
    runner.text = line.text;
    runner.started = GetTickCount();
    runner.counter = 0;
    runner.phase = 0;
    runner.target_set_ = false;
    switch (parse_command(line.text, &command, &error)) {
    case CommandLineOutcome::kVacant:
        ++runner.lines_done_;
        return true;
    case CommandLineOutcome::kError:
        get_log("automation: line %d: %s", line.number, line.text.c_str());
        get_fail(runner, "line " + std::to_string(line.number) + ": " + error, false);
        return true;
    case CommandLineOutcome::kOrderCode:
        break;
    }
    runner.active = true;
    runner.command = command;
    runner.text = describe(command);
    remember(command.target);
    if (command.op == Op::kStageMap || command.condition == Condition::kStageMap)
        remember(_strnicmp(command.target.c_str(), "load_", 5) == 0 ? command.target : "Load_" + command.target);
    get_log("automation: %s", runner.text.c_str());
    std::string why = refusal_by_name(command);
    if (why.empty() && command.op != Op::kStall && command.op != Op::kEcho && command.op != Op::kQuit &&
        command.op != Op::kWaitFor)
        why = net_refusal();
    if (!why.empty()) {
        get_fail(runner, why, true);
        return true;
    }
    switch (command.op) {
    case Op::kRun: {
        const std::string failed = spawn_named(command.target, command.params);
        if (failed.empty())
            get_finish(runner, "ok");
        else
            get_fail(runner, failed, false);
        return true;
    }
    case Op::kStageMap: {
        std::string failed;
        if (begin_level::read_in(command.target.c_str(), &failed))
            get_finish(runner, "ok");
        else
            get_fail(runner, failed, false);
        return true;
    }
    case Op::kEcho:
        get_finish(runner, command.target);
        return true;
    case Op::kFrontend: {
        bool done = true;
        const std::string result = frontend::new_frontend::automation_command(command.target, &done);
        if (!done)
            return false; // (Continue: once its screen is up)
        if (result.starts_with("refused"))
            get_fail(runner, result, true);
        else if (result.starts_with("error"))
            get_fail(runner, result, false);
        else
            get_finish(runner, result);
        return true;
    }
    case Op::kJumpTo: {
        if (!current_load_state() || in_front_end() || !skater_present()) {
            get_fail(runner, "not in a level with the skater", false);
            return true;
        }
        const std::vector<float>& n = command.numbers;
        frontend::test_view::Vec3 at = {};
        const std::string failed = frontend::test_view::jump_to(
            {::core::at(n, 0), ::core::at(n, 1), ::core::at(n, 2)}, n.size() > 3, n.size() > 3 ? ::core::at(n, 3) : 0.0f, &at, command.target == "board");
        if (!failed.empty()) {
            get_fail(runner, failed, false);
            return true;
        }
        std::array<char, 96> result{};
        std::snprintf(result.data(), sizeof(result), "ok (standing at %.1f %.1f %.1f)", at.x, at.y, at.z);
        if (FILE* trace_file = trace_state().file.get()) { // (a trace marks each teleport: the rides it holds)
            std::fprintf(trace_file, "# %s -> %s\n", runner.text.c_str(), result.data());
            std::fflush(trace_file);
        }
        get_finish(runner, result.data());
        return true;
    }
    case Op::kViewCam:
        if (command.target == "off")
            frontend::test_view::unlock_camera();
        else if (!frontend::test_view::lock_camera(
                     {::core::at(command.numbers, 0), ::core::at(command.numbers, 1), ::core::at(command.numbers, 2)},
                     {::core::at(command.numbers, 3), ::core::at(command.numbers, 4), ::core::at(command.numbers, 5)},
                     command.numbers.size() > 6
                         ? frontend::test_view::Vec3{::core::at(command.numbers, 6), ::core::at(command.numbers, 7), ::core::at(command.numbers, 8)}
                         : frontend::test_view::Vec3{0.0f, 0.0f, 0.0f})) {
            get_fail(runner, "the direction has no horizon (straight up / down)", false);
            return true;
        }
        get_finish(runner, "ok");
        return true;
    case Op::kCapture: {
        const std::string failed = sys::screenshot::start_series(command.frames, command.target);
        if (!failed.empty()) {
            get_fail(runner, failed, false);
            return true;
        }
        return false; // (Continue: done once written)
    }
    case Op::kTrace: {
        const std::string failed = start_trace(command.frames);
        if (failed.empty())
            get_finish(runner, "ok");
        else
            get_fail(runner, failed, false);
        return true;
    }
    case Op::kLogTricks: {
        if (!current_load_state() || in_front_end() || !skater_present()) {
            get_fail(runner, "not in a level with the skater", false);
            return true;
        }
        std::string failed;
        const std::string result = log_tricks(command.frames, &failed);
        if (failed.empty())
            get_finish(runner, result);
        else
            get_fail(runner, failed, false);
        return true;
    }
    case Op::kCrash: {
        TestFault fault = TestFault::kAccess;
        parse_test_fault(command.target, &fault);
        write_status(runner); // (its "current" says what ended the game)
        if (!cause_test_fault(fault))
            get_fail(runner, "crash " + command.target + ": not in this build (a Debug build's check)", false);
        else
            get_fail(runner, "crash " + command.target + ": the game still runs after it", false);
        return true;
    }
    case Op::kQuit: {
        get_finish(runner, "ok");
        SDL_Event event = {};
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
        return false;
    }
    case Op::kPress: {
        const std::string refusal = press_refusal(*find_button(command.target));
        if (!refusal.empty()) {
            get_fail(runner, refusal, true);
            return true;
        }
        runner.phase = 0; // (Continue: held once the menus take it - looked at again then)
        return false;
    }
    case Op::kHold: {
        std::string refusal = press_refusal(*find_button(command.target));
        if (refusal.empty())
            refusal = net_refusal();
        if (!refusal.empty()) {
            get_fail(runner, refusal, true);
            return true;
        }
        runner.hold_button_ = find_button(command.target);
        runner.hold_frames_ = command.frames;
        get_finish(runner, "ok");
        return true;
    }
    case Op::kChoose:
    case Op::kFocus:
        step_choose(runner, front_end);
        return !runner.active;
    case Op::kStall:
        if (command.frames == 0) {
            get_finish(runner, "ok");
            return true;
        }
        return false;
    case Op::kWaitFor:
        if (condition_met(runner)) {
            get_finish(runner, "ok");
            return true;
        }
        return false;
    }
    return false;
}

// The running command's frame.
void continue_command(Runner& runner, modules::FrontEnd* front_end)
{
    switch (runner.command.op) {
    case Op::kPress:
        if (runner.phase == 0) {
            // In the menus, not while they'd drop it (at most kPressWaitSeconds).
            if ((menus_ignore_pad(front_end) || loading_screen_up()) && !get_timed_out(runner, kPressWaitSeconds))
                return;
            std::string refusal = press_refusal(*find_button(runner.command.target));
            if (refusal.empty())
                refusal = net_refusal();
            if (!refusal.empty())
                return get_fail(runner, refusal, true);
            runner.held = find_button(runner.command.target);
            runner.phase = 1;
            runner.counter = 0;
        }
        ++runner.counter;
        if (runner.phase == 1 && runner.counter >= runner.command.frames) {
            runner.held = nullptr;
            runner.phase = 2;
            runner.counter = 0;
        } else if (runner.phase == 2 && runner.counter >= kReleaseFrames) {
            get_finish(runner, "ok");
        }
        return;
    case Op::kStall:
        if (++runner.counter >= runner.command.frames)
            get_finish(runner, "ok");
        return;
    case Op::kFrontend: {
        const std::string result = frontend::new_frontend::automation_result();
        if (result.empty()) {
            if (get_timed_out(runner, runner.command.seconds)) {
                frontend::new_frontend::automation_cancel();
                get_fail(runner,
                         "fe " + runner.command.target + ": its screen wasn't up in " +
                             std::to_string(runner.command.seconds) + " s (" +
                             frontend::new_frontend::automation_status() + ")",
                         false);
            }
            return;
        }
        if (result.starts_with("error"))
            get_fail(runner, result, false);
        else
            get_finish(runner, result);
        return;
    }
    case Op::kWaitFor:
        if (condition_met(runner))
            get_finish(runner, "ok");
        else if (get_timed_out(runner, runner.command.seconds))
            get_fail(runner, "timed out after " + std::to_string(runner.command.seconds) + " s", false);
        return;
    case Op::kChoose:
    case Op::kFocus:
        if (!net_refusal().empty())
            return get_fail(runner, net_refusal(), true);
        step_choose(runner, front_end);
        return;
    case Op::kCapture:
        if (sys::screenshot::series_running())
            return;
        if (sys::screenshot::series_result().starts_with("ok"))
            get_finish(runner, sys::screenshot::series_result());
        else
            get_fail(runner, sys::screenshot::series_result(), false);
        return;
    default:
        get_finish(runner, "ok");
        return;
    }
}

void write_status(Runner& runner)
{
    Standing status = {};
    status.frames = frame_loop::frames_run();
    status.ready = runner.ready;
    status.main_menu_ = runner.menu_settled_;
    auto* front_end = modules::g_front_end_instance.get();
    status.paused = front_end && front_end->is_halted_;
    status.loading = loading_screen_up();
    const uint32_t load = current_load_state();
    status.where = !load ? "starting" : load == kLoadMainMenu ? "front_end" : "level";
    status.level = load ? get_name_of(load) : "";
    if (_strnicmp(status.level.c_str(), "load_", 5) == 0)
        status.level.erase(0, 5);
    status.game_mode_ = active_ruleset_label();
    std::vector<UiWidget*> focused, selected;
    focused_elements(&focused, &selected);
    for (const UiWidget* element : focused)
        status.focus.push_back(get_name_of(ident_no(element)));
    for (const UiWidget* element : selected)
        status.selected.push_back(get_name_of(ident_no(element)));
    status.lines_read_ = runner.lines_read_;
    status.lines_done_ = runner.lines_done_;
    status.queued = static_cast<int>(runner.queue.size());
    status.current = runner.active ? runner.text : "";
    status.last = runner.last;
    status.result = runner.result;
    status.errors = runner.errors;
    status.refusals = runner.refusals;
    status.frontend = frontend::new_frontend::automation_status();
    // Written when anything but the frame count changed, or every half second.
    status.frames = 0;
    const std::string key = status_json(status);
    if (key == runner.written && frame_loop::frames_run() - runner.written_frames_ < 30)
        return;
    runner.written = key;
    runner.written_frames_ = frame_loop::frames_run();
    status.frames = frame_loop::frames_run();
    std::string text = status_json(status);
    const std::string path = game_folder_path("thug2-decomp-status.json");
    const util::UniqueHandle file(CreateFileA(path.c_str(), GENERIC_WRITE,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return;
    DWORD written = 0;
    WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

// The command line as the process got it (the game's own parsing cuts the process's buffer into words later: a 0
// after the first word - platform/winmain.h).
const std::string& command_line() { return sys::startup_command_line(); }

// -run <script>, --run <script> or --run=<script> ("" if none).
const std::string& requested_run()
{
    static const std::string kScript = [] {
        const std::string& line = command_line();
        for (const char* option : {"--run=", "--run ", "-run "}) {
            const size_t at = line.find(option);
            if (at == std::string::npos)
                continue;
            const size_t start = line.find_first_not_of(' ', at + std::strlen(option));
            if (start == std::string::npos)
                return std::string();
            const size_t end = line.find_first_of(" \"", start);
            return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }
        return std::string();
    }();
    return kScript;
}

void update_run(Runner& runner)
{
    if (runner.run_done_ || requested_run().empty() || !runner.ready)
        return;
    if (++runner.ready_frames_ < 30) // (a moment for the menu to settle, as -level)
        return;
    runner.run_done_ = true;
    const std::string& name = requested_run();
    std::string why;
    if (!is_identifier(name))
        why = "not a script name";
    else if (script_denied(name))
        why = "the script is online / network";
    else
        why = net_refusal();
    if (!why.empty()) {
        get_log("automation: REFUSED -run %s: %s", name.c_str(), why.c_str());
        return;
    }
    const std::string failed = spawn_named(name, {});
    get_log("automation: -run %s -> %s", name.c_str(), failed.empty() ? "ok" : failed.c_str());
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "automation", 0) != 0;
    return on;
}

void tick_frame(modules::FrontEnd* front_end)
{
    // (not before the game's first frame: the early self-tests run the front end's input logic on stand-in state)
    if (frame_loop::frames_run() == 0)
        return;
    if (!is_switched_on() && requested_run().empty())
        return;
    Runner& runner = get_state();
    static bool s_named = false;
    if (!s_named) { // (-level's level by name in the status)
        s_named = true;
        const std::string level = begin_level::get_requested();
        if (!level.empty())
            remember(_strnicmp(level.c_str(), "load_", 5) == 0 ? level : "Load_" + level);
    }
    // (the main menu counts once it has taken pad events for a second: chosen away at once, while its start-up
    // scripts still ran, the next menu came up under a loading logo that stayed)
    runner.menu_frames_ = main_menu_up() ? runner.menu_frames_ + 1 : 0;
    runner.menu_settled_ = runner.menu_frames_ >= kSettleFrames;
    if (!runner.ready && runner.menu_settled_) {
        runner.ready = true;
        get_log("automation: the main menu is up (frame %u)", frame_loop::frames_run());
    }
    update_run(runner);
    if (!is_switched_on())
        return;
    trace_frame();
    // NOLINTNEXTLINE(bugprone-inc-dec-in-conditions): counted down and tested in one step, on purpose
    if (runner.hold_frames_ > 0 && --runner.hold_frames_ == 0)
        runner.hold_button_ = nullptr;
    poll_file(runner);
    if (runner.active)
        continue_command(runner, front_end);
    for (int i = 0; i < 32 && !runner.active && !runner.queue.empty(); ++i)
        if (!kickoff(runner, front_end))
            break;
    write_status(runner);
}

bool params_denied(script::Struct* params) { return struct_denied(params, 0); }

std::string element_refusal(const UiWidget* element) { return refusal_of_element(element); }

void add_pad_buttons(int port, uint8_t* data)
{
    if (port != 0 || !is_switched_on())
        return;
    for (const Button* button : {get_state().held, get_state().hold_button_}) {
        if (!button)
            continue;
        data[button->byte] |= static_cast<uint8_t>(1u << button->bit);
        if (button->pressure)
            data[button->pressure] = 0xFF;
    }
}

} // namespace frontend::automation

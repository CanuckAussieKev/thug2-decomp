// The test-automation channel's command parser (devtools/automation.h): a line of the command file into an order.
// NOT ORIGINAL.

#include "devtools/automation.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include "base/core/checksums.h"
#include "base/core/parse_number.h"
#include "devtools/screenshot.h"
#include "platform/crash.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "devtools/automation_internal.h"

namespace frontend::automation {
// What the source file's anonymous namespace held that its pieces share (automation_internal.h): visible as it was. (In
// each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::automation

namespace frontend::automation {
namespace {

constexpr std::size_t kMaxHexTextLength = 10;       // "0x" and eight digits
constexpr std::size_t kMaxIntegerDigits = 9;
constexpr std::size_t kMaxNumberTextLength = 10;

constexpr int kMaxWaitSeconds = 3600;               // the longest timeout a command takes, in seconds
constexpr int kMaxHoldFrames = 3600;
constexpr int kMaxPressFrames = 600;
constexpr int kMaxStallFrames = 36000;
constexpr std::size_t kMinTeleportWordsWithBoard = 5; // teleport's words when the last is `board`
constexpr std::size_t kTeleportWordsWithHeading = 5;
constexpr std::size_t kCameraWordsFixed = 7;        // camera: x y z and the direction
constexpr std::size_t kCameraWordsDrift = 10;       // ... and the drift

constexpr int kMaxLoggedTrickCount = 100000;
constexpr int kDefaultFrontendSeconds = 10;

constexpr int kDefaultPressFrames = 3;

constexpr int kDefaultChooseSeconds = 10;
constexpr int kDefaultWaitSeconds = 90;

constexpr int kMaxCaptureFrames =
    sys::screenshot::kMaxSeriesFrames; // a last line without its end of line, once the file has been still this long
constexpr int kMaxTraceFrames = 36000; // trace: ten minutes at 60 frames a second

} // namespace
namespace internal {

uint32_t name_hash(const std::string& name) { return checksums::hash_from_text(name.c_str()); }

std::string get_lower(std::string text)
{
    for (char& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

bool is_identifier(const std::string& text)
{
    if (text.empty() || (std::isdigit(static_cast<unsigned char>(::core::at(text, 0))) != 0)) {
        return false;
    }
    return std::ranges::all_of(text, [](const char character) {
        return (std::isalnum(static_cast<unsigned char>(character)) != 0) || character == '_';
    });
}

bool is_hex(const std::string& text)
{
    if (text.size() < 3 || text.size() > kMaxHexTextLength || ::core::at(text, 0) != '0' ||
        (::core::at(text, 1) != 'x' && ::core::at(text, 1) != 'X')) {
        return false;
    }
    for (size_t i = 2; i < text.size(); ++i) {
        if (std::isxdigit(static_cast<unsigned char>(::core::at(text, i))) == 0) {
            return false;
        }
    }
    return true;
}

} // namespace internal
namespace {

bool is_integer(const std::string& text)
{
    const size_t start = ::core::at(text, 0) == '-' ? 1 : 0;
    if (start >= text.size()) {
        return false;
    }
    for (size_t i = start; i < text.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(::core::at(text, i))) == 0) {
            return false;
        }
    }
    return true;
}

bool is_float(const std::string& text)
{
    const size_t start = ::core::at(text, 0) == '-' ? 1 : 0;
    bool digits = false;
    bool point = false;
    for (size_t i = start; i < text.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(::core::at(text, i))) != 0) {
            digits = true;
        } else if (::core::at(text, i) == '.' && !point) {
            point = true;
        } else {
            return false;
        }
    }
    return digits && point;
}

// An element id: a name or 0x<hex>.
bool is_id(const std::string& text) { return is_hex(text) || is_identifier(text); }

// A positive number that fits (false for anything else).
bool read_count(const std::string& text, int low, int high, int* out)
{
    if (!is_integer(text) || text.size() > kMaxIntegerDigits) {
        return false;
    }
    const int value = text::to_int(text.c_str());
    if (value < low || value > high) {
        return false;
    }
    *out = value;
    return true;
}

// tokens[first...] as numbers (integers or decimals, signed); false if any isn't one.
bool read_numbers(const std::vector<std::string>& tokens, size_t first, std::vector<float>* out)
{
    out->clear();
    for (size_t i = first; i < tokens.size(); ++i) {
        if (!is_integer(::core::at(tokens, i)) && !is_float(::core::at(tokens, i))) {
            return false;
        }
        out->push_back(static_cast<float>(text::to_double(::core::at(tokens, i).c_str())));
    }
    return true;
}

// The words of a line: split at spaces, a quoted part kept whole (quotes included); `#` outside quotes ends it.
bool tokenize(const std::string& line, std::vector<std::string>* tokens, std::string* error)
{
    std::string token;
    bool quoted = false;
    bool any = false;
    for (const char character : line) {
        if (quoted) {
            token += character;
            if (character == '"') {
                quoted = false;
            }
            continue;
        }
        if (character == '#') {
            break;
        }
        if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
            if (any) {
                tokens->push_back(token);
            }
            token.clear();
            any = false;
            continue;
        }
        token += character;
        any = true;
        if (character == '"') {
            quoted = true;
        }
    }
    if (quoted) {
        *error = "a quote isn't closed";
        return false;
    }
    if (any) {
        tokens->push_back(token);
    }
    return true;
}

// The text after the first word (echo's), comments included.
std::string get_rest(const std::string& line)
{
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return {};
    }
    const size_t gap = line.find_first_of(" \t", start);
    if (gap == std::string::npos) {
        return {};
    }
    const size_t text = line.find_first_not_of(" \t", gap);
    if (text == std::string::npos) {
        return {};
    }
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
        *out = {.kind = Param::Kind::kBitMark, .name = "", .text = token, .integer = 0, .real = 0.0F};
        return true;
    }
    const std::string name = token.substr(0, equals);
    std::string value = token.substr(equals + 1);
    if (!is_identifier(name) || value.empty()) {
        *error = "not a parameter: " + token;
        return false;
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        *out = {.kind = Param::Kind::kChars,
                .name = name,
                .text = value.substr(1, value.size() - 2),
                .integer = 0,
                .real = 0.0F};
    } else if (value.find('"') != std::string::npos) {
        return *error = "not a parameter: " + token, false;
    } else if (is_integer(value) && value.size() <= kMaxNumberTextLength) {
        *out = {.kind = Param::Kind::kInteger,
                .name = name,
                .text = value,
                .integer = text::to_int(value.c_str()),
                .real = 0.0F};
    } else if (is_float(value)) {
        *out = {.kind = Param::Kind::kFloat, .name = name, .text = value, .integer = 0, .real = static_cast<float>(text::to_double(value.c_str()))};
    } else if (is_id(value)) {
        *out = {.kind = Param::Kind::kName, .name = name, .text = value, .integer = 0, .real = 0.0F};
    } else {
        return *error = "not a value: " + value, false;
    }
    return true;
}

// A command line's words, and the keyword (lower case) its first.
struct CommandWords {
    const std::vector<std::string>* tokens{};
    const std::string* line{};
    std::string keyword;
};

// The error for a command line that is wrong.
bool fail_with(std::string* error, const std::string& why)
{
    *error = why;
    return false;
}

bool parse_run(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    if (count < 2 || !is_identifier(::core::at(tokens, 1))) {
        return fail_with(error, "run <script> [name=value | flag]...");
    }
    command.op = Op::kRun;
    command.target = ::core::at(tokens, 1);
    for (size_t i = 2; i < count; ++i) {
        Param param;
        if (!parse_param(::core::at(tokens, i), &param, error)) {
            return false;
        }
        command.params.push_back(param);
    }
    return true;
}

bool parse_choose(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    const std::string& keyword = words.keyword;
    command.op = keyword == "choose" ? Op::kChoose : Op::kFocus;
    command.seconds = kDefaultChooseSeconds;
    if (count < 2 || count > 3 || !is_id(::core::at(tokens, 1)) ||
        (count == 3 && !read_count(::core::at(tokens, 2), 1, kMaxWaitSeconds, &command.seconds))) {
        return fail_with(error, keyword + " <id> [seconds]");
    }
    command.target = ::core::at(tokens, 1);
    return true;
}

bool parse_hold(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    // (test tooling: press's button held without waiting - the next commands run while it's down)
    command.op = Op::kHold;
    command.frames = kDefaultPressFrames;
    if (count < 2 || count > 3 ||
        (count == 3 && !read_count(::core::at(tokens, 2), 1, kMaxHoldFrames, &command.frames))) {
        return fail_with(error, "hold <button> [frames]");
    }
    const Button* button = find_button(::core::at(tokens, 1));
    if (!button) {
        return fail_with(error, "no such button: " + ::core::at(tokens, 1));
    }
    command.target = button->name;
    return true;
}

bool parse_press(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    command.op = Op::kPress;
    command.frames = kDefaultPressFrames;
    if (count < 2 || count > 3 ||
        (count == 3 && !read_count(::core::at(tokens, 2), 1, kMaxPressFrames, &command.frames))) {
        return fail_with(error, "press <button> [frames]");
    }
    const Button* button = find_button(::core::at(tokens, 1));
    if (!button) {
        return fail_with(error, "no such button: " + ::core::at(tokens, 1));
    }
    command.target = button->name;
    return true;
}

bool parse_wait(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    command.op = Op::kStall;
    if (count != 2 || !read_count(::core::at(tokens, 1), 0, kMaxStallFrames, &command.frames)) {
        return fail_with(error, "wait <frames>");
    }
    return true;
}

bool parse_waitfor(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    command.op = Op::kWaitFor;
    command.seconds = kDefaultWaitSeconds;
    if (count < 2) {
        return fail_with(error, "waitfor ready | frontend | ingame | level <name> | menu <id> | focus <id> [seconds]");
    }
    const std::string what = get_lower(::core::at(tokens, 1));
    size_t next = 2;
    if (what == "fe") {
        command.condition = Condition::kNewFrontend;
        if (count < 3 || !is_identifier(::core::at(tokens, 2))) {
            return fail_with(error, "waitfor fe <screen> [seconds]");
        }
        command.target = get_lower(::core::at(tokens, 2));
        next = 3;
    } else if (what == "ready") {
        command.condition = Condition::kReady;
    } else if (what == "frontend") {
        command.condition = Condition::kFrontEnd;
    } else if (what == "ingame") {
        command.condition = Condition::kInGame;
    } else if (what == "level" || what == "menu" || what == "focus") {
        if (what == "level") {
            command.condition = Condition::kStageMap;
        } else if (what == "menu") {
            command.condition = Condition::kMenu;
        } else {
            command.condition = Condition::kFocus;
        }
        if (count < 3 || !(command.condition == Condition::kStageMap ? is_identifier(::core::at(tokens, 2))
                                                                     : is_id(::core::at(tokens, 2)))) {
            return fail_with(error, "waitfor " + what + " <" + (what == "level" ? "name" : "id") + "> [seconds]");
        }
        command.target = ::core::at(tokens, 2);
        next = 3;
    } else {
        return fail_with(error, "no such condition: " + ::core::at(tokens, 1));
    }
    if (count > next + 1 ||
        (count == next + 1 && !read_count(::core::at(tokens, next), 1, kMaxWaitSeconds, &command.seconds))) {
        return fail_with(error, "waitfor: a timeout is whole seconds (1-3600)");
    }
    return true;
}

bool parse_level(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    command.op = Op::kStageMap;
    if (count != 2 || !is_identifier(::core::at(tokens, 1))) {
        return fail_with(error, "level <name>");
    }
    command.target = ::core::at(tokens, 1);
    return true;
}

bool parse_teleport(const CommandWords& command_words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *command_words.tokens;
    command.op = Op::kJumpTo;
    // (a last word `board`: the skater put on its board instead of standing - test_view::JumpTo's on_board)
    std::vector<std::string> words = tokens;
    if (words.size() >= kMinTeleportWordsWithBoard && get_lower(words.back()) == "board") {
        command.target = "board";
        words.pop_back();
    }
    if ((words.size() != 4 && words.size() != kTeleportWordsWithHeading) || !read_numbers(words, 1, &command.numbers)) {
        return fail_with(error, "teleport <x> <y> <z> [heading] [board]");
    }
    return true;
}

bool parse_camera(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    command.op = Op::kViewCam;
    if (count == 2 && get_lower(::core::at(tokens, 1)) == "off") {
        command.target = "off";
    } else if ((count != kCameraWordsFixed && count != kCameraWordsDrift) ||
               !read_numbers(tokens, 1, &command.numbers)) {
        return fail_with(error, "camera <x> <y> <z> <dx> <dy> <dz> [<drift x> <drift y> <drift z>] | camera off");
    } else if (::core::at(command.numbers, 3) == 0.0F && ::core::at(command.numbers, 4) == 0.0F &&
               ::core::at(command.numbers, kDirectionZIndex) == 0.0F) {
        return fail_with(error, "camera: the direction is zero");
    }
    return true;
}

bool parse_capture(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    command.op = Op::kCapture;
    if (count < 2 || count > 3 || !read_count(::core::at(tokens, 1), 1, kMaxCaptureFrames, &command.frames)) {
        return fail_with(error, "capture <frames 1-" + std::to_string(kMaxCaptureFrames) + "> [folder]");
    }
    if (count == 3) {
        std::string folder = ::core::at(tokens, 2);
        if (folder.size() >= 2 && folder.front() == '"' && folder.back() == '"') {
            folder = folder.substr(1, folder.size() - 2);
        }
        if (folder.empty() || folder.find("..") != std::string::npos) {
            return fail_with(error, "capture: a folder name (no ..)");
        }
        command.target = folder;
    }
    return true;
}

bool parse_trace(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    command.op = Op::kTrace;
    if (count < 2 || count > 3 || !read_count(::core::at(tokens, 1), 1, kMaxTraceFrames, &command.frames) ||
        (count == 3 && ::core::at(tokens, 2) != "objects")) {
        return fail_with(error, "trace <frames 1-" + std::to_string(kMaxTraceFrames) + "> [objects]");
    }
    if (count == 3) {
        command.target = "objects";
    }
    return true;
}

bool parse_logtricks(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    command.op = Op::kLogTricks;
    command.frames = 0; // (every trick object)
    if (count > 2 || (count == 2 && !read_count(::core::at(tokens, 1), 1, kMaxLoggedTrickCount, &command.frames))) {
        return fail_with(error, "logtricks [count]");
    }
    return true;
}

bool parse_fe(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    const std::string& line = *words.line;
    command.op = Op::kFrontend;
    command.seconds = kDefaultFrontendSeconds;
    command.target = get_rest(line);
    if (count < 2) {
        return fail_with(error, "fe select <screen>/<item> | fe focus <screen>/<item> | fe back | fe press <input>");
    }
    // (select / focus: a last number is the timeout - the front end ignores it)
    if (count == 4 && (get_lower(::core::at(tokens, 1)) == "select" || get_lower(::core::at(tokens, 1)) == "focus") &&
        !read_count(::core::at(tokens, 3), 1, kMaxWaitSeconds, &command.seconds)) {
        return fail_with(error, "fe " + get_lower(::core::at(tokens, 1)) + ": a timeout is whole seconds (1-3600)");
    }
    // (the level editor's `fe editor ...`: a last number is the timeout too - a level takes a while to open; the editor
    // ignores it)
    if (count >= 3 && get_lower(::core::at(tokens, 1)) == "editor" && is_integer(tokens.back())) {
        read_count(tokens.back(), 1, kMaxWaitSeconds, &command.seconds);
    }
    return true;
}

bool parse_echo(const CommandWords& words, OrderCode& command, std::string* /*error*/)
{
    const std::string& line = *words.line;
    command.op = Op::kEcho;
    command.target = get_rest(line);
    return true;
}

bool parse_crash(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    command.op = Op::kCrash;
    TestFault fault{};
    if (count != 2 || !parse_test_fault(::core::at(tokens, 1), &fault)) {
        return fail_with(error, std::string("crash <kind>: ") + test_fault_names());
    }
    command.target = test_fault_name(fault);
    return true;
}

bool parse_quit(const CommandWords& words, OrderCode& command, std::string* error)
{
    const std::vector<std::string>& tokens = *words.tokens;
    const size_t count = tokens.size();
    command.op = Op::kQuit;
    if (count != 1) {
        return fail_with(error, "quit takes nothing");
    }
    return true;
}

struct CommandParser {
    const char* keyword;
    bool (*parse)(const CommandWords& words, OrderCode& command, std::string* error);
};
// The commands, in the order their keywords are tested.
constexpr auto kCommandParsers = std::to_array<CommandParser>({
    {.keyword = "run", .parse = parse_run},
    {.keyword = "choose", .parse = parse_choose},
    {.keyword = "focus", .parse = parse_choose},
    {.keyword = "hold", .parse = parse_hold},
    {.keyword = "press", .parse = parse_press},
    {.keyword = "wait", .parse = parse_wait},
    {.keyword = "waitfor", .parse = parse_waitfor},
    {.keyword = "level", .parse = parse_level},
    {.keyword = "teleport", .parse = parse_teleport},
    {.keyword = "camera", .parse = parse_camera},
    {.keyword = "capture", .parse = parse_capture},
    {.keyword = "trace", .parse = parse_trace},
    {.keyword = "logtricks", .parse = parse_logtricks},
    {.keyword = "fe", .parse = parse_fe},
    {.keyword = "echo", .parse = parse_echo},
    {.keyword = "crash", .parse = parse_crash},
    {.keyword = "quit", .parse = parse_quit},
});

} // namespace

CommandLineOutcome parse_command(const std::string& line, OrderCode* out, std::string* error)
{
    std::vector<std::string> tokens;
    if (!tokenize(line, &tokens, error)) {
        return CommandLineOutcome::kError;
    }
    if (tokens.empty()) {
        return CommandLineOutcome::kVacant;
    }
    OrderCode command = {.op = Op::kEcho, .target = "", .params = {}, .condition = Condition::kNone, .frames = 0, .seconds = 0};
    const std::string keyword = get_lower(::core::at(tokens, 0));
    auto fail = [error](const std::string& why) {
        *error = why;
        return CommandLineOutcome::kError;
    };
    const auto parser = std::ranges::find_if(
        kCommandParsers, [&](const CommandParser& command_parser) { return keyword == command_parser.keyword; });
    if (parser == kCommandParsers.end()) {
        return fail("no such command: " + ::core::at(tokens, 0));
    }
    if (!parser->parse({.tokens = &tokens, .line = &line, .keyword = keyword}, command, error)) {
        return CommandLineOutcome::kError;
    }
    *out = command;
    return CommandLineOutcome::kOrderCode;
}

} // namespace frontend::automation

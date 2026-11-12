// The test-automation channel's reports (devtools/automation.h): a command described, the status as JSON. NOT
// ORIGINAL.

#include "devtools/automation.h"
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <span>
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"

namespace frontend::automation {
namespace {

constexpr char32_t kFirstPrintableChar = 0x20;
constexpr char32_t kDeleteChar = 0x7F;
constexpr std::size_t kEscapeTextSize = 8;

constexpr std::size_t kNumberTextSize = 32;

std::string json_string(const std::string& text)
{
    std::string out = "\"";
    for (const char character : text) {
        auto u = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            out += '\\';
            out += character;
        } else if (u < kFirstPrintableChar || u >= kDeleteChar) {
            std::array<char, kEscapeTextSize> escaped{};
            text::format_to(escaped, "\\u%04x", u);
            out += escaped.data();
        } else {
            out += character;
        }
    }
    return out + "\"";
}

std::string json_list(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        out += ((i != 0U) ? ", " : "") + json_string(::core::at(items, i));
    }
    return out + "]";
}

} // namespace

std::string describe(const OrderCode& command)
{
    switch (command.op) {
    case Op::kRun: {
        std::string text = "run " + command.target;
        for (const Param& param : command.params) {
            text += " ";
            if (param.kind == Param::Kind::kBitMark) {
                text += param.text;
            } else {
                text += param.name + "=" + (param.kind == Param::Kind::kChars ? "\"" + param.text + "\"" : param.text);
            }
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
        static const auto kConditions =
            std::to_array<const char*>({"?", "ready", "frontend", "ingame", "level", "menu", "focus", "fe"});
        std::string text = std::string("waitfor ") + ::core::at(kConditions, static_cast<int>(command.condition));
        if (!command.target.empty()) {
            text += " " + command.target;
        }
        return text;
    }
    case Op::kStageMap:
        return "level " + command.target;
    case Op::kEcho:
        return "echo " + command.target;
    case Op::kConsole:
        return "console " + command.target;
    case Op::kFrontend:
        return "fe " + command.target;
    case Op::kQuit:
        return "quit";
    case Op::kCrash:
        return "crash " + command.target;
    case Op::kJumpTo:
    case Op::kViewCam: {
        if (command.target == "off") {
            return "camera off";
        }
        std::string text = command.op == Op::kJumpTo ? "teleport" : "camera";
        for (const float number : command.numbers) {
            std::array<char, kNumberTextSize> value{};
            text::format_to(value, " %g", static_cast<double>(number));
            text += value.data();
        }
        if (command.op == Op::kJumpTo && command.target == "board") {
            text += " board";
        }
        return text;
    }
    case Op::kCapture:
        return "capture " + std::to_string(command.frames) + (command.target.empty() ? "" : " " + command.target);
    case Op::kRayProbe:
    case Op::kCollisionProbe: {
        std::string text = command.op == Op::kRayProbe ? "rtprobe" : "colprobe";
        for (const float number : command.numbers) {
            std::array<char, kNumberTextSize> value{};
            text::format_to(value, " %g", static_cast<double>(number));
            text += value.data();
        }
        return text;
    }
    case Op::kLogTricks:
        return "logtricks" + ((command.frames != 0) ? " " + std::to_string(command.frames) : std::string());
    case Op::kTrace:
        return "trace " + std::to_string(command.frames) + (command.target.empty() ? "" : " " + command.target);
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
    if (!status.frontend.empty()) {
        field("frontend", json_string(status.frontend));
    }
    field("refusals", std::to_string(status.refusals), true);
    return out + "}\n";
}

} // namespace frontend::automation

// The test-automation channel's commands run (devtools/automation.h): the conditions waited for, the front end, jump
// and camera commands and the start of each order. NOT ORIGINAL.

#include "devtools/automation.h"
#include "devtools/console.h"
#include <windows.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include <array>
#include <span>
#include "game/front_end/front_end.h"
#include "engine/ui/screen_element.h"
#include "engine/ext/extension.h"
#include "game/front_end/start_level.h"
#include "game/rider/test_view.h"
#include "engine/lists/key_map.h"
#include "game/world/trick_objects.h"
#include "devtools/screenshot.h"
#include "platform/crash.h"
#include "platform/run_log.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "devtools/automation_internal.h"

namespace frontend::automation {
// What the source file's anonymous namespace held that its pieces share (automation_internal.h): visible as it was. (In
// each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::automation

namespace frontend::automation {
namespace {

constexpr std::size_t kDriftXIndex = 6;
constexpr std::size_t kDriftYIndex = 7;
constexpr std::size_t kDriftZIndex = 8;

constexpr std::uint32_t kGuardWord = 0xEEEEEEEEU; // put past the trick object list to see that it's left alone
constexpr std::size_t kStolenSize = 0x100;
constexpr std::size_t kTrickResultSize = 160;
constexpr std::size_t kStatusResultSize = 96;

constexpr int kReleaseFrames = 3;

bool condition_holds(const OrderCode& command)
{
    switch (command.condition) {
    case Condition::kReady:
        return get_state().menu_settled_;
    case Condition::kFrontEnd:
        return in_front_end();
    case Condition::kNewFrontend:
        return ext::screen_up(command.target);
    case Condition::kInGame: {
        auto* front_end = modules::front_end_instance().get();
        return (current_load_state() != 0U) && !in_front_end() && skater_present() &&
               !((front_end != nullptr) && front_end->is_halted());
    }
    case Condition::kStageMap: {
        std::string name = command.target;
        if (_strnicmp(name.c_str(), "load_", begin_level::kLoadPrefixLength) != 0) {
            name = "Load_" + name;
        }
        return current_load_state() == name_hash(name) && skater_present();
    }
    case Condition::kMenu: {
        ElementPtr holder;
        return find_element(id_checksum(command.target), &holder) != nullptr;
    }
    case Condition::kFocus: {
        const uint32_t id = id_checksum(command.target);
        std::vector<UiWidget*> focused;
        std::vector<UiWidget*> selected;
        focused_elements(&focused, &selected);
        for (const UiWidget* element : focused) {
            if (ident_no(element) == id) {
                return true;
            }
        }
        return std::ranges::any_of(selected, [&](const UiWidget* element) { return ident_no(element) == id; });
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
    auto* manager = objects::trick_spot_hub();
    if (!manager) {
        *failed = "no trick object manager";
        return "";
    }
    std::vector<uint32_t> objects;
    std::vector<void*> clusters;
    const lists::KeyMap& table = manager->object_clusters();
    for (int bucket = 0; bucket < (1 << table.bits()); ++bucket) {
        for (const lists::KeyMap::Item* item = &::core::at(table.bucket_run(), bucket); item; item = item->next) {
            if (!item->value) {
                continue;
            }
            objects.push_back(item->key);
            if (std::ranges::find(clusters, item->value) == clusters.end()) {
                clusters.push_back(item->value);
            }
        }
    }
    if (count > 0 && objects.size() > static_cast<size_t>(count)) {
        objects.resize(static_cast<size_t>(count));
    }
    const size_t asked = objects.size();
    objects.push_back(kGuardWord); // (past the list: must be left alone)
    std::array<uint8_t, kStolenSize> stolen = {};
    const uint32_t changed = manager->log_trick_objects(std::span<uint32_t>(objects).first(asked), stolen, 0, 1000000);
    if (::core::at(objects, asked) != kGuardWord) {
        *failed = "the word past the list was changed";
        return "";
    }
    std::array<char, kTrickResultSize> result{};
    text::format_to(result, "ok (%u trick objects of %u clusters in the level logged, %u changed hands)",
                    static_cast<unsigned>(asked), static_cast<unsigned>(clusters.size()), changed);
    return result.data();
}

// frontend: the new front end's command - refused, an error, or its result (false: not done yet).
bool run_frontend_command(Runner& runner, const OrderCode& command)
{
    bool done = true;
    const std::string result = ext::screen_command(command.target, &done);
    if (!done) {
        return false; // (Continue: once its screen is up)
    }
    if (result.starts_with("refused")) {
        get_fail(runner, result, true);
    } else if (result.starts_with("error")) {
        get_fail(runner, result, false);
    } else {
        get_finish(runner, result);
    }
    return true;
}

// jumpto: the skater put standing there (in a level with the skater), marked in a trace.
bool run_jump_to(Runner& runner, const OrderCode& command)
{
    if ((current_load_state() == 0U) || in_front_end() || !skater_present()) {
        get_fail(runner, "not in a level with the skater", false);
        return true;
    }
    const std::vector<float>& numbers = command.numbers;
    frontend::test_view::Vec3 at = {};
    const std::string failed = frontend::test_view::jump_to(
        {.x = ::core::at(numbers, 0), .y = ::core::at(numbers, 1), .z = ::core::at(numbers, 2)}, numbers.size() > 3,
        numbers.size() > 3 ? ::core::at(numbers, 3) : 0.0F, &at, command.target == "board");
    if (!failed.empty()) {
        get_fail(runner, failed, false);
        return true;
    }
    std::array<char, kStatusResultSize> result{};
    text::format_to(result, "ok (standing at %.1f %.1f %.1f)", at.x, at.y, at.z);
    if (fs::DiskStream* trace_file = trace_state().file.get()) { // (a trace marks each teleport: the rides it holds)
        fs::print_to(trace_file, "# %s -> %s\n", runner.text.c_str(), result.data());
        fs::disk().stream_flush(trace_file);
    }
    get_finish(runner, result.data());
    return true;
}

// viewcam: the camera locked (its position, direction and drift) or let go.
bool run_view_cam(Runner& runner, const OrderCode& command)
{
    if (command.target == "off") {
        frontend::test_view::unlock_camera();
    } else if (!frontend::test_view::lock_camera(
                   {.position = {.x = ::core::at(command.numbers, 0),
                                 .y = ::core::at(command.numbers, 1),
                                 .z = ::core::at(command.numbers, 2)},
                    .direction = {.x = ::core::at(command.numbers, 3),
                                  .y = ::core::at(command.numbers, 4),
                                  .z = ::core::at(command.numbers, kDirectionZIndex)},
                    .drift = command.numbers.size() > kDriftXIndex
                                 ? frontend::test_view::Vec3{.x = ::core::at(command.numbers, kDriftXIndex),
                                                             .y = ::core::at(command.numbers, kDriftYIndex),
                                                             .z = ::core::at(command.numbers, kDriftZIndex)}
                                 : frontend::test_view::Vec3{.x = 0.0F, .y = 0.0F, .z = 0.0F}})) {
        get_fail(runner, "the direction has no horizon (straight up / down)", false);
        return true;
    }
    get_finish(runner, "ok");
    return true;
}

} // namespace
namespace internal {

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
    if (command.op == Op::kStageMap || command.condition == Condition::kStageMap) {
        remember(_strnicmp(command.target.c_str(), "load_", begin_level::kLoadPrefixLength) == 0
                     ? command.target
                     : "Load_" + command.target);
    }
    get_log("automation: %s", runner.text.c_str());
    std::string why = refusal_by_name(command);
    if (why.empty() && command.op != Op::kStall && command.op != Op::kEcho && command.op != Op::kQuit &&
        command.op != Op::kWaitFor) {
        why = net_refusal();
    }
    if (!why.empty()) {
        get_fail(runner, why, true);
        return true;
    }
    switch (command.op) {
    case Op::kRun: {
        const std::string failed = spawn_named(command.target, command.params);
        if (failed.empty()) {
            get_finish(runner, "ok");
        } else {
            get_fail(runner, failed, false);
        }
        return true;
    }
    case Op::kStageMap: {
        std::string failed;
        if (begin_level::read_in(command.target.c_str(), &failed)) {
            get_finish(runner, "ok");
        } else {
            get_fail(runner, failed, false);
        }
        return true;
    }
    case Op::kEcho:
        get_finish(runner, command.target);
        return true;
    case Op::kConsole: {
        const std::string output = devtools::console::submit(command.target);
        get_log("automation: console %s:\n%s", command.target.c_str(), output.c_str());
        get_finish(runner, output);
        return true;
    }
    case Op::kFrontend: {
        return run_frontend_command(runner, command);
    }
    case Op::kJumpTo: {
        return run_jump_to(runner, command);
    }
    case Op::kViewCam:
        return run_view_cam(runner, command);
    case Op::kCapture: {
        const std::string failed = sys::screenshot::start_series(command.frames, command.target);
        if (!failed.empty()) {
            get_fail(runner, failed, false);
            return true;
        }
        return false; // (Continue: done once written)
    }
    case Op::kTrace: {
        const std::string failed = start_trace(command.frames, command.target == "objects");
        if (failed.empty()) {
            get_finish(runner, "ok");
        } else {
            get_fail(runner, failed, false);
        }
        return true;
    }
    case Op::kLogTricks: {
        if ((current_load_state() == 0U) || in_front_end() || !skater_present()) {
            get_fail(runner, "not in a level with the skater", false);
            return true;
        }
        std::string failed;
        const std::string result = log_tricks(command.frames, &failed);
        if (failed.empty()) {
            get_finish(runner, result);
        } else {
            get_fail(runner, failed, false);
        }
        return true;
    }
    case Op::kCrash: {
        TestFault fault = TestFault::kAccess;
        parse_test_fault(command.target, &fault);
        write_status(runner); // (its "current" says what ended the game)
        if (!cause_test_fault(fault)) {
            get_fail(runner, "crash " + command.target + ": not in this build (a Debug build's check)", false);
        } else {
            get_fail(runner, "crash " + command.target + ": the game still runs after it", false);
        }
        return true;
    }
    case Op::kQuit: {
        get_finish(runner, "ok");
        SDL_Event event = {};
        event.type = SDL_EVENT_QUIT;
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
        if (refusal.empty()) {
            refusal = net_refusal();
        }
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
            if ((menus_ignore_pad(front_end) || loading_screen_up()) && !get_timed_out(runner, kPressWaitSeconds)) {
                return;
            }
            std::string refusal = press_refusal(*find_button(runner.command.target));
            if (refusal.empty()) {
                refusal = net_refusal();
            }
            if (!refusal.empty()) {
                get_fail(runner, refusal, true);
                return;
            }
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
        if (++runner.counter >= runner.command.frames) {
            get_finish(runner, "ok");
        }
        return;
    case Op::kFrontend: {
        const std::string result = ext::screen_result();
        if (result.empty()) {
            if (get_timed_out(runner, runner.command.seconds)) {
                ext::screen_cancel();
                get_fail(runner,
                         "fe " + runner.command.target + ": its screen wasn't up in " +
                             std::to_string(runner.command.seconds) + " s (" + ext::screen_status() + ")",
                         false);
            }
            return;
        }
        if (result.starts_with("error")) {
            get_fail(runner, result, false);
        } else {
            get_finish(runner, result);
        }
        return;
    }
    case Op::kWaitFor:
        if (condition_met(runner)) {
            get_finish(runner, "ok");
        } else if (get_timed_out(runner, runner.command.seconds)) {
            get_fail(runner, "timed out after " + std::to_string(runner.command.seconds) + " s", false);
        }
        return;
    case Op::kChoose:
    case Op::kFocus:
        if (!net_refusal().empty()) {
            get_fail(runner, net_refusal(), true);
            return;
        }
        step_choose(runner, front_end);
        return;
    case Op::kCapture:
        if (sys::screenshot::series_running()) {
            return;
        }
        if (sys::screenshot::series_result().starts_with("ok")) {
            get_finish(runner, sys::screenshot::series_result());
        } else {
            get_fail(runner, sys::screenshot::series_result(), false);
        }
        return;
    default:
        get_finish(runner, "ok");
        return;
    }
}

} // namespace internal
} // namespace frontend::automation

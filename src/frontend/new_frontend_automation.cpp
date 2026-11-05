// NOT ORIGINAL - the new front end's side of the automation channel (ours): its commands, results and status.
// See new_frontend.h.

#include "frontend/new_frontend.h"
#include <windows.h>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <array>
#include "frontend/new_frontend_draw.h"
#include "frontend/new_frontend_game.h"
#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_model.h"
#include "imgui.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "frontend/new_frontend_internal.h"

namespace frontend::new_frontend {
// What the source file's anonymous namespace held that its pieces share (new_frontend_internal.h): visible as it was.
// (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::new_frontend

namespace frontend::new_frontend {

std::string automation_command(const std::string& line, bool* done)
{
    *done = true;
    if (!is_switched_on()) {
        return "error: the new front end is off ([features] new_frontend=0)";
    }
    // fe select <screen>/<item> [seconds] | fe focus <screen>/<item> [seconds] | fe back | fe press <input>
    std::vector<std::string> words;
    for (size_t at = 0; at < line.size();) {
        const size_t start = line.find_first_not_of(" \t", at);
        if (start == std::string::npos) {
            break;
        }
        const size_t end = line.find_first_of(" \t", start);
        words.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        at = end == std::string::npos ? line.size() : end;
    }
    if (words.empty()) {
        return "error: fe select <screen>/<item> | fe focus <screen>/<item> | fe back | fe press <input>";
    }
    std::string verb = core::at(words, 0);
    for (char& ch : verb) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (verb == "select" || verb == "focus") {
        if (words.size() < 2 || core::at(words, 1).find('/') == std::string::npos) {
            return "error: fe " + verb + " <screen>/<item> [seconds]";
        }
        const std::string screen_name = core::at(words, 1).substr(0, core::at(words, 1).find('/'));
        const std::string item = core::at(words, 1).substr(core::at(words, 1).find('/') + 1);
        Screen screen{};
        if (!screen_from_name(screen_name, &screen)) {
            return "error: no screen " + screen_name;
        }
        if (get_refused(screen, item)) {
            return "refused: online (" + core::at(words, 1) + ")";
        }
        stored_pending() = {};
        stored_pending().active = true;
        stored_pending().confirm = verb == "select";
        stored_pending().screen = screen;
        stored_pending().item = item;
        *done = false;
        return "";
    }
    if (verb == "back") {
        if (new_frontend_state().state_ != State::kShown || menu_state().occupied()) {
            return "error: the new front end isn't up";
        }
        run_action(the_front_end(), menu_state().get_press(Intake::kBack));
        return "ok";
    }
    if (verb == "press") {
        static const auto kInputs = std::to_array<std::pair<const char*, Intake>>({
            {"up", Intake::kUp},           {"down", Intake::kDown},       {"left", Intake::kLeft},
            {"right", Intake::kRight},     {"confirm", Intake::kConfirm}, {"back", Intake::kBack},
            {"prevtab", Intake::kPrevTab}, {"nexttab", Intake::kNextTab}});
        if (words.size() < 2) {
            return "error: fe press <up|down|left|right|confirm|back|resume|prevtab|nexttab>";
        }
        for (const auto& [name, input] : kInputs) {
            if (_stricmp(core::at(words, 1).c_str(), name) == 0) {
                if (new_frontend_state().state_ != State::kShown || menu_state().occupied()) {
                    return "error: the new front end isn't up";
                }
                if (input == Intake::kConfirm && get_refused(menu_state().get_shown(), menu_state().chosen_item_id())) {
                    return "refused: online";
                }
                run_action(the_front_end(), menu_state().get_press(input));
                return "ok";
            }
        }
        return "error: no input " + core::at(words, 1);
    }
    return "error: fe " + verb + "? (select, focus, back, press)";
}

std::string automation_result()
{
    if (stored_pending().active) {
        return {};
    }
    return stored_pending().result.empty() ? std::string("ok") : stored_pending().result;
}

void automation_cancel() { stored_pending().active = false; }

bool automation_screen_up(const std::string& name)
{
    // (the restyled original screens: dialog, skaters, list)
    if (is_switched_on() && !showing() && menu_mirror().mirror_.get_up() &&
        _stricmp(name.c_str(), mirror::kind_name(menu_mirror().mirror_.get_shown().kind)) == 0) {
        return true;
    }
    Screen screen{};
    return is_switched_on() && screen_from_name(name, &screen) && shown_and_idle(screen);
}

std::string automation_status()
{
    if (!is_switched_on()) {
        return {};
    }
    std::string text = std::string(get_state_name(new_frontend_state().state_));
    if (new_frontend_state().state_ == State::kShown || new_frontend_state().state_ == State::kLaunching) {
        text += std::string(" ") + get_screen_name(menu_state().get_shown());
        const std::string item = menu_state().chosen_item_id();
        if (!item.empty()) {
            text += "/" + item;
        }
    }
    if (new_frontend_state().anchor_ != Anchor::kNone && new_frontend_state().state_ == State::kShown) {
        text += std::string(" over the ") + game_screens::anchor_name(new_frontend_state().anchor_);
    }
    if (!showing() && menu_mirror().mirror_.get_up()) {
        const mirror::View& v = menu_mirror().mirror_.get_shown();
        text += std::string(", restyled ") + mirror::kind_name(v.kind) + " \"" + v.title + "\"";
        if (v.selected >= 0 && std::cmp_less(v.selected, v.rows.size())) {
            text += " on \"" + core::at(v.rows, v.selected).label + "\"";
        }
    }
    return text;
}

} // namespace frontend::new_frontend

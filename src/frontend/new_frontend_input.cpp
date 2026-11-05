// NOT ORIGINAL - the new front end's input (ours): keys, pad buttons, sticks and queued events into the original
// menus, and the mirror's clicks and steps. See new_frontend.h.

#include "frontend/new_frontend.h"
#include "engine/pad_input/input_source.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <SDL2/SDL.h>
#include <utility>
#include "game/front_end/front_end.h"
#include "frontend/mouse_menus.h"
#include "frontend/new_frontend_draw.h"
#include "frontend/new_frontend_game.h"
#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_model.h"
#include "engine/ui/screen_element.h"
#include "game/rider/thaw_bmx.h"
#include "engine/pad_input/input_event.h"
#include "platform/sdl_window.h"
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
namespace {

constexpr int kChoicesPerRow = 16;              // a choice's index is its row * 16 + choice
constexpr int kKeysPerRow = 64;                 // a keyboard key's index is its row * 64 + column
constexpr float kEdgeFraction = 0.3F;           // the part of an adjustable row at each end that steps left or right
constexpr std::size_t kMaxMirrorEvents = 64;
constexpr std::size_t kMaxQueuedEvents = 256;

// The mouse over the restyled screen: the original menu's selection stepped to the row (its own pad events), a click
// chooses it, the right button / Back is pad_back, the wheel steps.
struct StepContext {
    modules::FrontEnd* front_end_;
    bool horizontal;
};
int read_selection(void* /*unused*/)
{
    const UiWidget* menu = active_menu();
    return menu ? mouse_menus::selected_index(menu) : -1;
}
void send_step(void* context, int direction)
{
    auto* step = static_cast<StepContext*>(context);
    const uint32_t sideways_event = direction < 0 ? modules::FrontEnd::kPadLeft : modules::FrontEnd::kPadRight;
    const uint32_t vertical_event = direction < 0 ? modules::FrontEnd::kPadUp : modules::FrontEnd::kPadDown;
    const uint32_t event = step->horizontal ? sideways_event : vertical_event;
    mouse_menus::send_menu_pad_event(step->front_end_, event);
}
bool step_to(modules::FrontEnd* front_end, int row)
{
    const mirror::View& v = menu_mirror().mirror_.get_shown();
    const UiWidget* menu = active_menu();
    if (!menu || row < 0 || std::cmp_greater_equal(row, v.rows.size())) {
        return false;
    }
    const int target = core::at(v.rows, row).index;
    if (target < 0) {
        return false; // (a row of a menu not taking the pad: the story's other stats column)
    }
    StepContext context{.front_end_ = front_end, .horizontal = v.horizontal};
    mouse_menus::step_selection(target, mouse_menus::child_count(menu), mouse_menus::menu_wraps(menu), read_selection,
                                send_step, &context);
    return read_selection(nullptr) == target;
}

// A key of the on-screen keyboard: its row and its column.
struct KeyPlace {
    int row;
    int col;
};

// The keyboard's key `key` selected with the game's own pad events (each step read back), then chosen.
void choose_key(modules::FrontEnd* front_end, KeyPlace key)
{
    const int row = key.row;
    const int col = key.col;
    for (int guard = 0; guard < kMaxParentDepth; ++guard) {
        UiWidget* active = active_menu();
        const UiWidget* keys = keyboard_of(active);
        if (!keys) {
            return;
        }
        const mirror::MenuRead now = read_keyboard(active);
        if (now.key_row_ != row) {
            mouse_menus::send_menu_pad_event(front_end, now.key_row_ < row ? modules::FrontEnd::kPadDown
                                                                           : modules::FrontEnd::kPadUp);
            continue;
        }
        if (now.key_col_ != col) {
            mouse_menus::send_menu_pad_event(front_end, now.key_col_ < col ? modules::FrontEnd::kPadRight
                                                                           : modules::FrontEnd::kPadLeft);
            continue;
        }
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadChoose);
        return;
    }
}

// The stats screen's row `row` selected with the game's own pad events: in the menu taking the pad, stepped there; in the
// story's other column, Left / Right toward it first (its column change keeps the row's place - the next steps reach
// the row). Nothing while a stat is being changed. Returns whether the row is selected now.
bool step_to_stat(modules::FrontEnd* front_end, int row)
{
    const mirror::View& v = menu_mirror().mirror_.get_shown();
    if (row < 0 || std::cmp_greater_equal(row, v.rows.size())) {
        return false;
    }
    if (row == v.selected) {
        return true;
    }
    if (v.stats_editing_) {
        return false;
    }
    const mirror::Row& row_ref = core::at(v.rows, row);
    if (row_ref.index >= 0) {
        return step_to(front_end, row);
    }
    if (v.selected >= 0) {
        mouse_menus::send_menu_pad_event(front_end, row_ref.stat_column_ < core::at(v.rows, v.selected).stat_column_
                                                        ? modules::FrontEnd::kPadLeft
                                                        : modules::FrontEnd::kPadRight);
    }
    return false;
}

// A click on the stats screen: a row's name selects it (the story's selected row: chosen - a stat to change, or done);
// its blocks select it and step its value - under the value Left, past it Right (the story's row is chosen first: its
// Left / Right change the stat only while it is being changed).
void stats_click(modules::FrontEnd* front_end, const paint::Hit& hit)
{
    const mirror::View& v = menu_mirror().mirror_.get_shown();
    const bool story = v.stat_columns_ > 1;
    if (hit.kind == paint::Hit::Kind::kItem) {
        const bool was_selected = hit.index == v.selected;
        if (step_to_stat(front_end, hit.index) && was_selected && story) {
            mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadChoose);
        }
        return;
    }
    if (hit.kind != paint::Hit::Kind::kChoice || !step_to_stat(front_end, hit.index / kChoicesPerRow)) {
        return;
    }
    if (story && !v.stats_editing_) {
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadChoose);
    }
    mouse_menus::send_menu_pad_event(front_end, hit.index % kChoicesPerRow == 0 ? modules::FrontEnd::kPadLeft
                                                                    : modules::FrontEnd::kPadRight);
}

// The restyled screen's mouse: the front end, the frame the screen was fitted in, whether it is the skater select or
// a stats screen.
struct MouseMirror {
    modules::FrontEnd* front_end{};
    const TickCycle* frame{};
    bool skaters{};
    bool stats{};
};

// The hit area under the window point (nullptr: none).
const paint::Hit* hit_at(const TickCycle& frame, float wx, float wy)
{
    const float x = frame.design_x(wx);
    const float y = frame.design_y(wy);
    for (const paint::Hit& hit : menu_mirror().mirror_hits_) {
        if (get_holds(hit, x, y)) {
            return &hit;
        }
    }
    return nullptr;
}

// The mouse moved: a stats screen's row, or (not the skater select: each step loads a skater - only a click selects
// there) the item under it, stepped to.
void mirror_motion(const MouseMirror& mouse, const input::Event& widget)
{
    modules::FrontEnd* front_end = mouse.front_end;
    if (mouse.stats) {
        if (const paint::Hit* hit =
                hit_at(*mouse.frame, static_cast<float>(widget.x), static_cast<float>(widget.y))) {
            const int choice_row = hit->kind == paint::Hit::Kind::kChoice ? hit->index / 16 : -1;
            const int row = hit->kind == paint::Hit::Kind::kItem ? hit->index : choice_row;
            if (row >= 0 && row != menu_mirror().mirror_.get_shown().selected) {
                step_to_stat(front_end, row);
            }
        }
        return;
    }
    // (the skater select: each step loads a skater - only a click selects there)
    if (!mouse.skaters) {
        if (const paint::Hit* hit =
                hit_at(*mouse.frame, static_cast<float>(widget.x), static_cast<float>(widget.y))) {
            if (hit->kind == paint::Hit::Kind::kItem && hit->index != menu_mirror().mirror_.get_shown().selected) {
                step_to(front_end, hit->index);
            }
        }
    }
}

// A click on an item: stepped to, then (unless the skater select's first click) chosen - or, on a row whose value
// Left / Right change - a slider, an option: its sides do that, as the game's own mouse control sends them -
// mouse_menus.cpp option_side; the middle chooses.
void click_item(const MouseMirror& mouse, const paint::Hit* hit, const input::Event& widget)
{
    modules::FrontEnd* front_end = mouse.front_end;
    const bool was_selected = hit->index == menu_mirror().mirror_.get_shown().selected;
    if (step_to(front_end, hit->index) && (!mouse.skaters || was_selected)) {
        const mirror::View& shown = menu_mirror().mirror_.get_shown();
        const float click_x = mouse.frame->design_x(static_cast<float>(widget.x));
        const float width = hit->x1 - hit->x0;
        int side = 0;
        if (shown.kind == mirror::Kind::kLinkChain && hit->index >= 0 && std::cmp_less(hit->index, shown.rows.size()) &&
            core::at(shown.rows, hit->index).adjustable) {
            if (click_x < hit->x0 + (width * kEdgeFraction)) {
                side = -1;
            } else if (click_x >= hit->x1 - (width * kEdgeFraction)) {
                side = 1;
            }
        }
        uint32_t event = modules::FrontEnd::kPadChoose; // (an edge of a setting: its arrows)
        if (side < 0) {
            event = modules::FrontEnd::kPadLeft;
        } else if (side > 0) {
            event = modules::FrontEnd::kPadRight;
        }
        mouse_menus::send_menu_pad_event(front_end, event);
    }
}

// A mouse button: the right one backs out; the left one on a hit: back, a stats click, a key, Continue or an item.
// Whether the screen may be another now (the events left unread).
bool mirror_click(const MouseMirror& mouse, const input::Event& widget)
{
    modules::FrontEnd* front_end = mouse.front_end;
    if (widget.button == input::kMouseRight) {
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadBack);
        return false;
    }
    if (widget.button != input::kMouseLeft) {
        return false;
    }
    const paint::Hit* hit =
        hit_at(*mouse.frame, static_cast<float>(widget.x), static_cast<float>(widget.y));
    if (!hit) {
        return false;
    }
    if (hit->kind == paint::Hit::Kind::kBack) {
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadBack);
    } else if (mouse.stats) {
        stats_click(front_end, *hit);
    } else if (hit->kind == paint::Hit::Kind::kStageMap &&
               menu_mirror().mirror_.get_shown().kind == mirror::Kind::kKeyboard) {
        choose_key(front_end, {.row = hit->index / kKeysPerRow, .col = hit->index % kKeysPerRow});
    } else if (hit->kind == paint::Hit::Kind::kContinue) {
        continue_last_level(front_end);
    } else if (hit->kind == paint::Hit::Kind::kItem) {
        click_item(mouse, hit, widget);
    }
    return true; // (the screen may be another now)
}

} // namespace
namespace internal {

void mirror_mouse(modules::FrontEnd* front_end)
{
    std::vector<input::Event> events;
    events.swap(menu_mirror().mirror_events_);
    if (!menu_mirror().mirror_.get_up()) {
        return;
    }
    int w = 0;
    int height = 0;
    if (SDL_Window* window = sys::sdl_window()) {
        SDL_GetWindowSize(window, &w, &height);
    }
    const TickCycle frame = fit_frame(static_cast<float>(w), static_cast<float>(height));
    const bool skaters = menu_mirror().mirror_.get_shown().kind == mirror::Kind::kSkaters;
    const bool stats = menu_mirror().mirror_.get_shown().kind == mirror::Kind::kStats;
    const MouseMirror mouse{.front_end = front_end, .frame = &frame, .skaters = skaters, .stats = stats};
    for (const input::Event& widget : events) {
        switch (widget.type) {
        case input::EventType::kMouseMotion:
            mirror_motion(mouse, widget);
            break;
        case input::EventType::kMouseButtonDown:
            if (mirror_click(mouse, widget)) {
                return;
            }
            break;
        case input::EventType::kMouseWheel:
            if (widget.wheel != 0) {
                StepContext context{.front_end_ = front_end, .horizontal = menu_mirror().mirror_.get_shown().horizontal};
                send_step(&context, widget.wheel > 0 ? -1 : 1);
            }
            break;
        // (the main menu's Continue - ours: the game's menu has no such item; every other key the game's menu takes)
        case input::EventType::kKeyDown:
            if (menu_mirror().mirror_.get_shown().main_menu_ && !widget.repeat &&
                (widget.key == SDL_SCANCODE_X || widget.key == SDL_SCANCODE_R)) {
                continue_last_level(front_end);
                return;
            }
            break;
        case input::EventType::kPadButtonDown:
            if (menu_mirror().mirror_.get_shown().main_menu_ && widget.pad_button == SDL_CONTROLLER_BUTTON_X) {
                continue_last_level(front_end);
                return;
            }
            break;
        default:
            break;
        }
    }
}

// --- input ---

bool get_key_down(input::KeyState keys, SDL_Scancode key) { return core::at(keys, key) != 0; }

// Whether the mouse's left button is down.
bool left_button_down()
{
    input::MouseState mouse;
    return input::mouse(&mouse) && ((mouse.buttons & input::kMouseLeftMask) != 0U);
}

void press_direction(modules::FrontEnd* front_end, Intake input)
{
    run_action(front_end, menu_state().get_press(input));
}

} // namespace internal
namespace {

// A key pressed on the front end's menus: the arrows / WASD, choose and back, the tabs (Q / E the level art on the
// level select).
void handle_key_down(modules::FrontEnd* front_end, const input::Event& widget)
{
    const bool repeat = widget.repeat;
    const bool shift = (widget.modifiers & input::kModifierShift) != 0;
    switch (widget.key) {
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_W:
        press_direction(front_end, Intake::kUp);
        break;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_S:
        press_direction(front_end, Intake::kDown);
        break;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A:
        press_direction(front_end, Intake::kLeft);
        break;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D:
        press_direction(front_end, Intake::kRight);
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_SPACE:
        if (!repeat) {
            run_action(front_end, menu_state().get_press(Intake::kConfirm));
        }
        break;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_BACKSPACE:
        if (!repeat) {
            run_action(front_end, menu_state().get_press(Intake::kBack));
        }
        break;
    case SDL_SCANCODE_Q:
        // (on the level select Q / E change the picture; the tabs keep Page Up / Down, 1 / 2, Tab and the shoulders)
        if (menu_state().get_shown() == Screen::kLevels) {
            cycle_level_art(-1);
        } else {
            run_action(front_end, menu_state().get_press(Intake::kPrevTab));
        }
        break;
    case SDL_SCANCODE_PAGEUP:
    case SDL_SCANCODE_1:
        run_action(front_end, menu_state().get_press(Intake::kPrevTab));
        break;
    case SDL_SCANCODE_E:
        if (menu_state().get_shown() == Screen::kLevels) {
            cycle_level_art(1);
        } else {
            run_action(front_end, menu_state().get_press(Intake::kNextTab));
        }
        break;
    case SDL_SCANCODE_PAGEDOWN:
    case SDL_SCANCODE_2:
        run_action(front_end, menu_state().get_press(Intake::kNextTab));
        break;
    case SDL_SCANCODE_TAB:
        run_action(front_end, menu_state().get_press(shift ? Intake::kPrevTab : Intake::kNextTab));
        break;
    default:
        break;
    }
}

// A controller button pressed: the d-pad (repeating while held), choose, Start, back, the shoulders' tabs.
void handle_button_down(modules::FrontEnd* front_end, const input::Event& widget)
{
    switch (widget.pad_button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        press_direction(front_end, Intake::kUp);
        stick_repeat().repeat_ = {.direction = static_cast<int>(Intake::kUp), .wait = kRepeatDelay};
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        press_direction(front_end, Intake::kDown);
        stick_repeat().repeat_ = {.direction = static_cast<int>(Intake::kDown), .wait = kRepeatDelay};
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        press_direction(front_end, Intake::kLeft);
        stick_repeat().repeat_ = {.direction = static_cast<int>(Intake::kLeft), .wait = kRepeatDelay};
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        press_direction(front_end, Intake::kRight);
        stick_repeat().repeat_ = {.direction = static_cast<int>(Intake::kRight), .wait = kRepeatDelay};
        break;
    case SDL_CONTROLLER_BUTTON_A:
        run_action(front_end, menu_state().get_press(Intake::kConfirm));
        break;
    case SDL_CONTROLLER_BUTTON_START:
        // (Start on the pause menu resumes, as the original's; elsewhere it chooses)
        run_action(front_end,
                   menu_state().get_press(menu_state().get_shown() == Screen::kFreeze && menu_state().deepness() <= 1
                                              ? Intake::kBack
                                              : Intake::kConfirm));
        break;
    case SDL_CONTROLLER_BUTTON_B:
        run_action(front_end, menu_state().get_press(Intake::kBack));
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        run_action(front_end, menu_state().get_press(Intake::kPrevTab));
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        run_action(front_end, menu_state().get_press(Intake::kNextTab));
        break;
    default:
        break;
    }
}

// The left stick as a d-pad: past half way (back inside a third: let go).
void handle_stick(modules::FrontEnd* front_end, const input::Event& widget)
{
    // The left stick as a d-pad: past half way (back inside a third: let go).
    int* axis = nullptr;
    if (widget.pad_axis == SDL_CONTROLLER_AXIS_LEFTX) {
        axis = &stick_repeat().stick_x_;
    } else if (widget.pad_axis == SDL_CONTROLLER_AXIS_LEFTY) {
        axis = &stick_repeat().stick_y_;
    }
    if (!axis) {
        return;
    }
    const int value = widget.pad_value;
    const int settled = std::abs(value) < 11000 ? 0 : *axis; // (between the two: as it was)
    const int pushed_back = value < -16000 ? -1 : settled;
    const int now = value > 16000 ? 1 : pushed_back;
    if (now == *axis) {
        return;
    }
    *axis = now;
    if (now == 0) {
        stick_repeat().repeat_ = {};
        return;
    }
    const Intake sideways = now > 0 ? Intake::kRight : Intake::kLeft;
    const Intake vertical = now > 0 ? Intake::kDown : Intake::kUp;
    const Intake input = axis == &stick_repeat().stick_x_ ? sideways : vertical;
    press_direction(front_end, input);
    stick_repeat().repeat_ = {.direction = static_cast<int>(input), .wait = kRepeatDelay};
}

} // namespace
namespace internal {

void handle_queued(modules::FrontEnd* front_end, const input::Event& widget)
{
    int w = 0;
    int height = 0;
    if (SDL_Window* window = sys::sdl_window()) {
        SDL_GetWindowSize(window, &w, &height);
    }
    TickCycle frame = fit_frame(static_cast<float>(w), static_cast<float>(height));
    auto hit_at = [&frame](float wx, float wy) -> const paint::Hit* {
        const float x = frame.design_x(wx);
        const float y = frame.design_y(wy);
        for (const paint::Hit& hit : new_frontend_state().hits_) {
            if (get_holds(hit, x, y)) {
                return &hit;
            }
        }
        return nullptr;
    };
    switch (widget.type) {
    case input::EventType::kKeyDown: {
        handle_key_down(front_end, widget);
        return;
    }
    case input::EventType::kPadButtonDown:
        handle_button_down(front_end, widget);
        return;
    case input::EventType::kPadButtonUp:
        if (widget.pad_button >= SDL_CONTROLLER_BUTTON_DPAD_UP &&
            widget.pad_button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            stick_repeat().repeat_ = {};
        }
        return;
    case input::EventType::kPadAxis: {
        handle_stick(front_end, widget);
        return;
    }
    case input::EventType::kMouseMotion:
        if (const paint::Hit* hit = hit_at(static_cast<float>(widget.x), static_cast<float>(widget.y))) {
            if (hit->kind == paint::Hit::Kind::kItem) {
                run_action(front_end, menu_state().hover_item(hit->index));
            } else if (hit->kind == paint::Hit::Kind::kStageMap) {
                run_action(front_end, menu_state().hover_level(hit->index));
            }
        }
        return;
    case input::EventType::kMouseButtonDown:
        if (widget.button == input::kMouseRight) {
            run_action(front_end, menu_state().get_press(Intake::kBack));
            return;
        }
        if (widget.button != input::kMouseLeft) {
            return;
        }
        if (const paint::Hit* hit = hit_at(static_cast<float>(widget.x), static_cast<float>(widget.y))) {
            const paint::Hit clicked = *hit;
            switch (clicked.kind) {
            case paint::Hit::Kind::kItem:
                run_action(front_end, menu_state().click_item(clicked.index));
                break;
            case paint::Hit::Kind::kStageMap:
                run_action(front_end, menu_state().click_level(clicked.index));
                break;
            case paint::Hit::Kind::kTab:
                run_action(front_end, menu_state().click_tab(clicked.index));
                break;
            case paint::Hit::Kind::kBack:
                run_action(front_end, menu_state().click_back());
                break;
            case paint::Hit::Kind::kSkate:
                run_action(front_end, menu_state().get_shown() == Screen::kOptions
                                          ? menu_state().get_press(Intake::kConfirm)
                                          : menu_state().click_skate());
                break;
            case paint::Hit::Kind::kContinue:
                break; // (the restyled main menu's - mirror_mouse)
            case paint::Hit::Kind::kChoice:
                run_action(front_end, menu_state().click_choice({.row = clicked.index / kChoicesPerRow, .choice = clicked.index % kChoicesPerRow}));
                break;
            }
        }
        return;
    case input::EventType::kMouseWheel:
        if (widget.wheel != 0) {
            run_action(front_end, menu_state().get_wheel(widget.wheel));
        }
        return;
    default:
        return;
    }
}

void handle_event(const input::Event& event)
{
    if (!is_switched_on()) {
        return;
    }
    switch (event.type) {
    case input::EventType::kPadButtonDown:
        ++new_frontend_state().buttons_down_;
        break;
    case input::EventType::kPadButtonUp:
        new_frontend_state().buttons_down_ = std::max(0, new_frontend_state().buttons_down_ - 1);
        break;
    default:
        break;
    }
    if (new_frontend_state().state_ != State::kShown) {
        // (a restyled original screen: the mouse is ours - its keys and buttons go on to the original menu)
        if (menu_mirror().mirror_.get_up() && menu_mirror().mirror_events_.size() < kMaxMirrorEvents &&
            (event.type == input::EventType::kMouseMotion || event.type == input::EventType::kMouseButtonDown || event.type == input::EventType::kMouseWheel ||
             event.type == input::EventType::kKeyDown || event.type == input::EventType::kPadButtonDown)) {
            menu_mirror().mirror_events_.push_back(event.kept());
        }
        return;
    }
    switch (event.type) {
    case input::EventType::kKeyDown:
    case input::EventType::kPadButtonDown:
    case input::EventType::kPadButtonUp:
    case input::EventType::kPadAxis:
    case input::EventType::kMouseMotion:
    case input::EventType::kMouseButtonDown:
    case input::EventType::kMouseWheel:
        if (new_frontend_state().events_.size() < kMaxQueuedEvents) {
            new_frontend_state().events_.push_back(event.kept());
        }
        break;
    default:
        break;
    }
}

} // namespace internal
} // namespace frontend::new_frontend

// NOT ORIGINAL - the level editor's navigation and hints: the game side (ours). See level_editor_ux8.h.

#include "editor/level_editor_ux8.h"
#include "engine/pad_input/input_source.h"
#include "base/maths/vec2.h"
#include "base/core/span_copy.h"

#include <SDL2/SDL.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <array>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_nav.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux8_pie.h"
#include "platform/run_log.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"

namespace frontend::level_editor::ux8 {

namespace {

constexpr double kDefaultSaySeconds = 3.0; // how long a message stays when no time is given
constexpr float kTriggerDeadzone = 0.1F;   // the shoulder triggers' dead zone (up / down)
constexpr int kControllerButtonBits = 32; // buttons kept in the 32-bit pressed mask
constexpr double kWalkHintSeconds = 5.0; // how long the Walk Mode keys hint stays
constexpr double kGlideSeconds = 0.35; // how long the camera glide to a bookmark takes

// The registry's indices of this module's actions (Init).
struct Ids {
    int pie_context_ = -1, pie_view_ = -1, walk = -1, follow = -1, teleport = -1, pad = -1;
    std::array<int, nav::kQuickSets> store = {};
    std::array<int, nav::kQuickSets> recall = {};
};

Ids& ids()
{
    static core::NoDestroy<Ids> state;
    return state.get();
}

// NOT ORIGINAL grouping - level_editor_ux8.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_ux8_state()).
struct LevelEditorUx8State {
    bool pad_mode_{};
    bool pad_owns_{}; // (the crosshair is the cursor)
    bool following_{};
    nav::Follower follower_{};
    bool walking_{};
    nav::Walker walker_{};
    uint32_t last_empty_click_{};
    float last_empty_x_{};
    float last_empty_y_{};
    bool have_empty_click_{};
};

LevelEditorUx8State& level_editor_ux8_state()
{
    static core::NoDestroy<LevelEditorUx8State> state;
    return state.get();
}

struct GlideState {
    bool on = false;
    V3 from = {}, to = {};
    float t = 0.0F;
};

GlideState& glide_state()
{
    static core::NoDestroy<GlideState> state;
    return state.get();
}

// NOT ORIGINAL grouping - the pie menu and the view's centre (were globals). Made on first use, never destroyed
// (pie_menu()).
struct PieMenu {
    pie::State pie_{};
    float view_x_{};
    float view_y_{};
    bool have_view_centre_{};
};

PieMenu& pie_menu()
{
    static core::NoDestroy<PieMenu> state;
    return state.get();
}

std::string ini_path() { return game_folder_path("thug2-decomp.ini"); }

void get_say(const std::string& text, double seconds = kDefaultSaySeconds) { show_message(text, seconds); }

// A simulation (level_editor_playtest.h) stops before the controller or a pie edits anything: its key handler only
// sees keys typed.
void stop_simulation()
{
    if (play_test::get_simulating()) {
        play_test::get_request(play_test::Ask::kToggleSimulate);
    }
}

actions::Chord get_k(int scancode, uint8_t mods = 0) { return {.scancode = scancode, .mods = mods}; }

int append_one(const char* id, const char* name, const char* category, actions::Kind kind,
               const std::vector<actions::Chord>& keys, const char* tip, std::function<void()> run = nullptr,
               std::function<int()> checked = nullptr)
{
    actions::ActStep act_step;
    act_step.id = id;
    act_step.name = name;
    act_step.category = category;
    act_step.kind = kind;
    act_step.defaults = keys;
    act_step.keys = keys;
    act_step.tip = tip;
    act_step.run = std::move(run);
    act_step.checked = std::move(checked);
    return actions::enrol(act_step);
}

void add_hint(const char* id, const char* name, const char* category, const char* keys, const char* tip)
{
    actions::ActStep act_step;
    act_step.id = id;
    act_step.name = name;
    act_step.category = category;
    act_step.kind = actions::Kind::kDetailsOf;
    act_step.rebindable = false;
    act_step.keys_text_ = keys;
    act_step.tip = tip;
    actions::enrol(act_step);
}

// The selection as group members (UX3's groups).
std::vector<organize::Member> selection_members()
{
    const SelectionIds selection_ids = current_selection();
    std::vector<organize::Member> members;
    members.reserve(selection_ids.objects.size());
    for (const uint32_t id : selection_ids.objects) {
        members.push_back({.kind = 'o', .id = id});
    }
    for (const uint32_t id : selection_ids.sectors) {
        members.push_back({.kind = 's', .id = id});
    }
    if (selection_ids.node != 0U) {
        members.push_back({.kind = 'n', .id = selection_ids.node});
    }
    return members;
}

} // namespace

// --- quick sets (also the toolbar's Sets menu - level_editor_ux8_ui.cpp) ---

void store_set(const std::string& name)
{
    organize::State next = organize::at_hand();
    const std::vector<organize::Member> members = selection_members();
    if (!nav::store_group(&next, name, members)) {
        get_say("Nothing selected");
        return;
    }
    organize::get_change(next, "Store selection set");
    get_say("Stored " + name + " (" + std::to_string(members.size()) + ")");
}

void recall_set(const std::string& name)
{
    const int at = nav::find_group(organize::at_hand(), name);
    if (at < 0) {
        get_say(name + " is empty");
        return;
    }
    organize::select_group(at);
}

void add_to_set(int index)
{
    if (selection_members().empty()) {
        get_say("Nothing selected");
        return;
    }
    organize::add_to_group(index);
}

void take_from_set(int index)
{
    organize::State next = organize::at_hand();
    if (!nav::remove_from_group(&next, index, selection_members())) {
        get_say("Selection is not in the set");
        return;
    }
    organize::get_change(next, "Take from selection set");
}

// --- the actions ---

void register_actions()
{
    using actions::kAlt;
    using actions::kCtrl;
    using actions::Kind;
    using actions::kShift;
    ids().pie_context_ =
        append_one("pie_context", "Pie Menu", "Panels", Kind::kPress, {get_k(SDL_SCANCODE_Q), get_k(SDL_SCANCODE_TAB)},
                   "Hold for common actions; release over one to run it");
    ids().pie_view_ =
        append_one("pie_view", "View Pie Menu", "Panels", Kind::kPress,
                   {get_k(SDL_SCANCODE_Q, kAlt), get_k(SDL_SCANCODE_TAB, kShift)}, "Hold for view modes and overlays");
    ids().walk = append_one("walk_mode", "Walk Mode", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_GRAVE, kShift)},
                            "Walk at skater height with gravity");
    ids().follow =
        append_one("follow_selection", "Follow Selection", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_F, kShift)},
                   "Keep the camera on the selection as it moves", nullptr, [] { return level_editor_ux8_state().following_ ? 1 : 0; });
    ids().teleport = append_one("teleport", "Go to Cursor", "Camera", Kind::kPress, {},
                                "Move the camera to the surface under the cursor");
    ids().pad = append_one(
        "controller_mode", "Controller Mode", "Settings", Kind::kFlipOver, {}, "Use a game controller in the editor",
        [] { set_pad_mode(!level_editor_ux8_state().pad_mode_); }, [] { return level_editor_ux8_state().pad_mode_ ? 1 : 0; });
    for (int i = 0; i < nav::kQuickSets; ++i) {
        const std::string number = std::to_string(i + 1);
        const std::string store_id = "quick_set_store_" + number;
        const std::string recall_id = "quick_set_" + number;
        const std::string store_name = "Store Set " + number;
        const std::string recall_name = "Select Set " + number;
        core::at(ids().store, i) = append_one(store_id.c_str(), store_name.c_str(), "Select", Kind::kPress,
                                    {get_k(SDL_SCANCODE_1 + i, kCtrl | kAlt)}, "Store the selection as Set n");
        core::at(ids().recall, i) = append_one(recall_id.c_str(), recall_name.c_str(), "Select", Kind::kPress,
                                     {get_k(SDL_SCANCODE_1 + i, kAlt | kShift)}, "Select Set n");
        for (const int index : {core::at(ids().store, i), core::at(ids().recall, i)}) {
            if (actions::ActStep* act_step = actions::get_pull(index)) {
                const size_t at = act_step->tip.find("Set n");
                if (at != std::string::npos) {
                    act_step->tip.replace(at + 4, 1, number);
                }
            }
        }
    }
    add_hint("hint_drill", "Cycle Under Cursor", "Select", "Alt+wheel", "Step through items under the cursor");
    add_hint("hint_teleport", "Go to Surface", "Camera", "Double-click", "Move the camera to the clicked surface");
    add_hint("hint_pad", "Controller", "Camera", "A game controller", "Enable Controller Mode in Preferences");
}

void prepare_all()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    level_editor_ux8_state().pad_mode_ = GetPrivateProfileIntA("editor", "pad", 0, ini_path().c_str()) != 0;
    register_actions();
}

void kickoff()
{
    pie_menu().pie_ = pie::State{};
    level_editor_ux8_state().walking_ = false;
    level_editor_ux8_state().following_ = false;
    level_editor_ux8_state().follower_ = {};
    glide_state() = {};
    level_editor_ux8_state().have_empty_click_ = false;
    level_editor_ux8_state().pad_owns_ = false;
}

void halt() { kickoff(); }

// --- the controller ---

bool pad_mode() { return level_editor_ux8_state().pad_mode_; }

void set_pad_mode(bool on)
{
    level_editor_ux8_state().pad_mode_ = on;
    WritePrivateProfileStringA("editor", "pad", on ? "1" : "0", ini_path().c_str());
    get_say(on ? "Controller mode on" : "Controller mode off");
}

namespace {

constexpr float kDeadzone = 0.22F;
uint32_t& pad_was()
{
    static uint32_t pad_was{}; // (the buttons down last frame: bit per SDL_GameControllerButton)
    return pad_was;
}

} // namespace

Pad read_pad()
{
    Pad pad;
    // (the controllers the game's input opened - the first attached: input::controller)
    input::ControllerState controller;
    if (!level_editor_ux8_state().pad_mode_ || !input::controller(&controller)) {
        pad_was() = 0;
        return pad;
    }
    pad.active = true;
    auto axis = [&](SDL_GameControllerAxis a) {
        return nav::get_stick({.raw = core::at(controller.axes, a), .deadzone = kDeadzone});
    };
    pad.move_x_ = axis(SDL_CONTROLLER_AXIS_LEFTX);
    pad.move_y_ = -axis(SDL_CONTROLLER_AXIS_LEFTY);
    pad.look_x_ = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    pad.look_y_ = -axis(SDL_CONTROLLER_AXIS_RIGHTY);
    pad.up_down_ = nav::get_stick({.raw = core::at(controller.axes, SDL_CONTROLLER_AXIS_TRIGGERRIGHT),
                                   .deadzone = kTriggerDeadzone}) -
                   nav::get_stick({.raw = core::at(controller.axes, SDL_CONTROLLER_AXIS_TRIGGERLEFT),
                                   .deadzone = kTriggerDeadzone});
    uint32_t now = 0;
    for (int button = 0; button < std::min<int>(SDL_CONTROLLER_BUTTON_MAX, kControllerButtonBits); ++button) {
        if ((controller.buttons & (1U << button)) != 0U) {
            now |= 1U << button;
        }
    }
    uint32_t pressed = now & ~pad_was();
    uint32_t const released = pad_was() & ~now;
    pad_was() = now;
    auto down = [&](SDL_GameControllerButton b) { return (now >> b & 1U) != 0; };
    auto press = [&](SDL_GameControllerButton b) { return (pressed >> b & 1U) != 0; };
    pad.a_down_ = down(SDL_CONTROLLER_BUTTON_A);
    pad.a_presses_ = press(SDL_CONTROLLER_BUTTON_A) ? 1 : 0;
    pad.a_releases_ = ((released >> SDL_CONTROLLER_BUTTON_A & 1U) != 0U) ? 1 : 0;
    pad.b = press(SDL_CONTROLLER_BUTTON_B);
    pad.x = press(SDL_CONTROLLER_BUTTON_X);
    pad.y = press(SDL_CONTROLLER_BUTTON_Y);
    pad.x_down_ = down(SDL_CONTROLLER_BUTTON_X);
    pad.y_down_ = down(SDL_CONTROLLER_BUTTON_Y);
    pad.back = press(SDL_CONTROLLER_BUTTON_BACK);
    pad.r3 = press(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    pad.lb = press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    pad.rb = press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    pad.fast = down(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    pad.up = down(SDL_CONTROLLER_BUTTON_DPAD_UP);
    pad.down = down(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    pad.left = down(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    pad.right = down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    // (Start isn't the editor's: the game's pause key leaves it - level_editor_exit)
    const uint32_t mine = now & ~(1U << SDL_CONTROLLER_BUTTON_START) & ~(1U << SDL_CONTROLLER_BUTTON_GUIDE);
    pad.used =
        mine != 0 || pad.move_x_ != 0 || pad.move_y_ != 0 || pad.look_x_ != 0 || pad.look_y_ != 0 || pad.up_down_ != 0;
    // (anything that edits stops a simulation first)
    if ((pad.a_presses_ != 0) || pad.back || pad.lb || pad.rb || pad.up || pad.down || pad.left || pad.right) {
        stop_simulation();
    }
    return pad;
}

input::KeyState pad_keys(input::KeyState keys, const Pad& pad)
{
    static std::array<uint8_t, SDL_NUM_SCANCODES> merged;
    if (!pad.active) {
        return keys;
    }
    core::copy_into(keys.first(merged.size()), std::span(merged));
    auto hold = [&](int action) {
        const actions::ActStep* act_step = actions::get_pull(action);
        if (!act_step) {
            return;
        }
        for (const actions::Chord& key : act_step->keys) {
            if (key.scancode > 0 && key.scancode < SDL_NUM_SCANCODES && key.mods == 0) {
                core::at(merged, key.scancode) = 1;
                return;
            }
        }
    };
    if (pad.up) {
        hold(actions::kNudgeForward);
    }
    if (pad.down) {
        hold(actions::kNudgeBack);
    }
    if (pad.left) {
        hold(actions::kNudgeLeft);
    }
    if (pad.right) {
        hold(actions::kNudgeRight);
    }
    if (pad.fast) {
        core::at(merged, SDL_SCANCODE_LSHIFT) = 1;
    }
    return merged;
}

bool get_pad_cursor(const Pad& pad, bool mouse_moved, CursorOut cursor)
{
    auto* const x = cursor.x;
    auto* const y = cursor.y;
    if (!pad.active) {
        level_editor_ux8_state().pad_owns_ = false;
        return false;
    }
    if (mouse_moved) {
        level_editor_ux8_state().pad_owns_ = false;
    }
    if (pad.used) {
        level_editor_ux8_state().pad_owns_ = true;
    }
    if (!level_editor_ux8_state().pad_owns_ || !pie_menu().have_view_centre_) {
        return false;
    }
    *x = pie_menu().view_x_;
    *y = pie_menu().view_y_;
    return true;
}

bool pad_cancel(const Pad& pad)
{
    if (!pad.active) {
        return false;
    }
    if (pie_menu().pie_.open) {
        return false; // (the pie takes B)
    }
    if (pad.back) {
        actions::fire_off(actions::kUndo);
    }
    if (pad.lb) {
        actions::fire_off(actions::kTurnLeft);
    }
    if (pad.rb) {
        actions::fire_off(actions::kTurnRight);
    }
    if (pad.r3) {
        actions::fire_off(actions::kFrameSelection);
    }
    return pad.b && !level_editor_ux8_state().walking_;
}

// --- once a frame ---

namespace {

void open_pie(pie::Kind kind, bool have_selection, maths::Vec2 at, int key, bool pad_button)
{
    const auto x = at.x;
    const auto y = at.y;
    pie_menu().pie_ = pie::State{};
    pie_menu().pie_.open = true;
    pie_menu().pie_.kind = kind;
    pie_menu().pie_.items = kind == pie::Kind::kView ? pie::view_items() : pie::context_items(have_selection);
    pie_menu().pie_.x = x;
    pie_menu().pie_.y = y;
    pie_menu().pie_.key = key;
    pie_menu().pie_.pad_button_ = pad_button;
    pie_menu().pie_.opened_at_ = SDL_GetTicks();
}

void run_slice()
{
    const int at = pie_menu().pie_.hovered;
    std::vector<pie::Item> items = pie_menu().pie_.items;
    pie_menu().pie_.open = false;
    if (at < 0 || std::cmp_greater_equal(at, items.size()) || !core::at(items, at).enabled()) {
        return;
    }
    if (core::at(items, at).edits) {
        stop_simulation();
    }
    core::at(items, at).run();
}

// Quick sets: Ctrl+Alt+n stores, Alt+Shift+n selects.
void quick_set_keys()
{
    using namespace actions;
    for (int i = 0; i < nav::kQuickSets; ++i) {
        if (squeezed(core::at(ids().store, i))) {
            store_set(nav::quick_set_name(i + 1));
        }
        if (squeezed(core::at(ids().recall, i))) {
            recall_set(nav::quick_set_name(i + 1));
        }
    }
}

// The follow camera toggled: on, it frames the selection (F) and keeps with it.
void toggle_follow(const TickCycle& tick_cycle)
{
    using namespace actions;
    if (level_editor_ux8_state().following_) {
        level_editor_ux8_state().following_ = false;
        get_say("Follow off");
    } else if (!tick_cycle.have_selection_) {
        get_say("Nothing selected");
    } else {
        level_editor_ux8_state().following_ = true;
        level_editor_ux8_state().follower_ = {};
        fire_off(kFrameSelection);
        get_say("Following selection");
    }
}

// A pie opened by its key or the pad (not while flying with a pie key that's also a fly key).
void open_pie_if_asked(const TickCycle& tick_cycle)
{
    using namespace actions;
    const bool pad_open = tick_cycle.pad.active && (tick_cycle.pad.x || tick_cycle.pad.y) && !pie_menu().pie_.open;
    const bool key_context = squeezed(ids().pie_context_);
    const bool key_view = squeezed(ids().pie_view_);
    if ((key_context || key_view || pad_open) && !pie_menu().pie_.open && !tick_cycle.right && !tick_cycle.dragging &&
        !tick_cycle.browser && !level_editor_ux8_state().walking_ &&
        ui::underway()) { // (drawn by the UI: the old HUD has no pies)
        // ([editor] wasd_flies=1: a pie key that's also a fly key held flies)
        const bool flying = xform::fly_always() && !pad_open &&
                            (get_held(tick_cycle.keys, kFlyDown) || get_held(tick_cycle.keys, kFlyUp) ||
                             get_held(tick_cycle.keys, kForward) || get_held(tick_cycle.keys, kBack) ||
                             get_held(tick_cycle.keys, kLeft) || get_held(tick_cycle.keys, kRight));
        if (!flying) {
            float x = tick_cycle.mouse_x_;
            float y = tick_cycle.mouse_y_;
            if (pad_open && pie_menu().have_view_centre_) {
                x = pie_menu().view_x_;
                y = pie_menu().view_y_;
            }
            if (pad_open) {
                open_pie(tick_cycle.pad.y ? pie::Kind::kView : pie::Kind::kCtxInfo, tick_cycle.have_selection_,
                         {.x = x, .y = y}, -1, true);
            } else {
                open_pie(key_view ? pie::Kind::kView : pie::Kind::kCtxInfo, tick_cycle.have_selection_,
                         {.x = x, .y = y}, key_view ? ids().pie_view_ : ids().pie_context_, false);
            }
        }
    }
}

// Letting go of the pie's key / button: a hold runs what's pointed at; a tap keeps it up for a click.
void let_go_of_pie(const TickCycle& tick_cycle)
{
    using namespace actions;
    bool held = false; // (the pie's button or key still down)
    if (pie_menu().pie_.pad_button_) {
        held = pie_menu().pie_.kind == pie::Kind::kView ? tick_cycle.pad.y_down_ : tick_cycle.pad.x_down_;
    } else {
        held = pie_menu().pie_.key >= 0 && get_held(tick_cycle.keys, pie_menu().pie_.key);
    }
    if (!held && !pie_menu().pie_.sticky) {
        if (SDL_GetTicks() - pie_menu().pie_.opened_at_ < nav::kPieTapMs && pie_menu().pie_.hovered < 0) {
            pie_menu().pie_.sticky = true;
        } else {
            run_slice();
        }
    }
}

} // namespace

void tick_frame(TickCycle& tick_cycle)
{
    using namespace actions;
    // Quick sets: Ctrl+Alt+n stores, Alt+Shift+n selects (not while dragging: digits are typed values then).
    if (!tick_cycle.dragging) {
        quick_set_keys();
    }
    // The follow camera: on frames the selection (F) and keeps with it.
    if (squeezed(ids().follow) && !tick_cycle.dragging) {
        toggle_follow(tick_cycle);
    }
    // The pies.
    open_pie_if_asked(tick_cycle);
    if (!pie_menu().pie_.open) {
        return;
    }
    // Pointing: the mouse (or the left stick) from the centre.
    float dx = tick_cycle.mouse_x_ - pie_menu().pie_.x;
    float dy = tick_cycle.mouse_y_ - pie_menu().pie_.y;
    if (pie_menu().pie_.pad_button_ ||
        (tick_cycle.pad.active && (tick_cycle.pad.move_x_ != 0 || tick_cycle.pad.move_y_ != 0))) {
        dx = tick_cycle.pad.move_x_ * 100.0F;
        dy = -tick_cycle.pad.move_y_ * 100.0F;
    }
    pie_menu().pie_.hovered = nav::slice_at(dx, dy, static_cast<int>(pie_menu().pie_.items.size()), pie::kDeadzone);
    const bool clicked = (tick_cycle.left_presses_ != nullptr) && *tick_cycle.left_presses_ > 0;
    if (tick_cycle.left_presses_) {
        *tick_cycle.left_presses_ = 0; // (the view doesn't see clicks while a pie is up)
    }
    if (tick_cycle.left_releases_) {
        *tick_cycle.left_releases_ = 0;
    }
    if (tick_cycle.right || (tick_cycle.pad.active && tick_cycle.pad.b)) {
        pie_menu().pie_.open = false;
        return;
    }
    if (clicked || (tick_cycle.pad.active && (tick_cycle.pad.a_presses_ != 0))) {
        run_slice();
        return;
    }
    // Letting go of its key / button: a hold runs what's pointed at; a tap keeps it up for a click.
    let_go_of_pie(tick_cycle);
}

bool get_cancel()
{
    if (pie_menu().pie_.open) {
        pie_menu().pie_.open = false;
        return true;
    }
    if (level_editor_ux8_state().walking_) {
        level_editor_ux8_state().walking_ = false;
        get_say("Walk mode off");
        return true;
    }
    return false;
}

bool wants_escape() { return pie_menu().pie_.open || level_editor_ux8_state().walking_; }

bool get_following() { return level_editor_ux8_state().following_; }
nav::Follower& get_follower() { return level_editor_ux8_state().follower_; }

bool on_foot() { return level_editor_ux8_state().walking_; }
void set_walking(bool on, V3 camera)
{
    level_editor_ux8_state().walking_ = on;
    pie_menu().pie_.open = false;
    if (on) {
        level_editor_ux8_state().walker_ = nav::walker_at(camera);
        glide_state().on = false;
        get_say("Walk Mode: WASD walk, Space jump, Shift run, Esc exit", kWalkHintSeconds);
    } else {
        get_say("Walk mode off");
    }
}
nav::Walker& walker_state() { return level_editor_ux8_state().walker_; }

bool take_walk_toggle() { return actions::squeezed(ids().walk); }
bool take_teleport() { return actions::squeezed(ids().teleport); }

bool note_empty_click(uint32_t now, float x, float y)
{
    const bool twice =
        level_editor_ux8_state().have_empty_click_ && nav::is_double_click({.ms = level_editor_ux8_state().last_empty_click_, .x = level_editor_ux8_state().last_empty_x_,
                                                                             .y = level_editor_ux8_state().last_empty_y_},
                                                                            {.ms = now, .x = x, .y = y});
    level_editor_ux8_state().have_empty_click_ = !twice;
    level_editor_ux8_state().last_empty_click_ = now;
    level_editor_ux8_state().last_empty_x_ = x;
    level_editor_ux8_state().last_empty_y_ = y;
    return twice;
}

void start_glide(V3 from, V3 to)
{
    glide_state().on = true;
    glide_state().from = from;
    glide_state().to = to;
    glide_state().t = 0.0F;
}

bool glide(V3* pos, double seconds)
{
    if (!glide_state().on) {
        return false;
    }
    glide_state().t = std::min(1.0F, glide_state().t + static_cast<float>(seconds / kGlideSeconds));
    const float eased = view_cam::get_ease(glide_state().t);
    *pos = glide_state().from + (glide_state().to - glide_state().from) * eased;
    if (glide_state().t >= 1.0F) {
        glide_state().on = false;
    }
    return true;
}

void stop_glide() { glide_state().on = false; }

bool pie_open() { return pie_menu().pie_.open; }
bool pie_sticky() { return pie_menu().pie_.sticky; }

// (for the UI file)
const pie::State& pie_now() { return pie_menu().pie_; }
void note_view_centre(maths::Vec2 centre)
{
    pie_menu().view_x_ = centre.x;
    pie_menu().view_y_ = centre.y;
    pie_menu().have_view_centre_ = true;
}
bool pad_owns_cursor() { return level_editor_ux8_state().pad_mode_ && level_editor_ux8_state().pad_owns_; }

} // namespace frontend::level_editor::ux8

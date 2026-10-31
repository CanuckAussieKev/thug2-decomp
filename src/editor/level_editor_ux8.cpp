// NOT ORIGINAL - the level editor's navigation and hints: the game side (ours). See level_editor_ux8.h.

#include "editor/level_editor_ux8.h"

#include <SDL2/SDL.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <functional>

#include "editor/level_editor_actions.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux8_pie.h"
#include "platform/run_log.h"

namespace frontend::level_editor::ux8 {

namespace {

// The registry's indices of this module's actions (Init).
struct Ids {
    int pie_context_ = -1, pie_view_ = -1, walk = -1, follow = -1, teleport = -1, pad = -1;
    int store[nav::kQuickSets] = {}, recall[nav::kQuickSets] = {};
} g_ids;

bool g_pad_mode;
bool g_pad_owns; // (the crosshair is the cursor)
bool g_following;
nav::Follower g_follower;
bool g_walking;
nav::Walker g_walker;
uint32_t g_last_empty_click;
float g_last_empty_x, g_last_empty_y;
bool g_have_empty_click;
struct GlideState {
    bool on = false;
    V3 from = {}, to = {};
    float t = 0.0f;
} g_glide;

pie::State g_pie;
float g_view_x, g_view_y;
bool g_have_view_centre;

std::string ini_path() { return game_folder_path("thug2-decomp.ini"); }

void get_say(const std::string& text, double seconds = 3.0) { show_message(text, seconds); }

// A simulation (level_editor_playtest.h) stops before the controller or a pie edits anything: its key handler only
// sees keys typed.
void stop_simulation()
{
    if (play_test::get_simulating())
        play_test::get_request(play_test::Ask::kToggleSimulate);
}

actions::Chord get_k(int scancode, uint8_t mods = 0) { return {scancode, mods}; }

int append_one(const char* id, const char* name, const char* category, actions::Kind kind, std::vector<actions::Chord> keys,
        const char* tip, std::function<void()> run = nullptr, std::function<int()> checked = nullptr)
{
    actions::ActStep a;
    a.id = id;
    a.name = name;
    a.category = category;
    a.kind = kind;
    a.defaults = keys;
    a.keys = keys;
    a.tip = tip;
    a.run = std::move(run);
    a.checked = std::move(checked);
    return actions::enrol(a);
}

void add_hint(const char* id, const char* name, const char* category, const char* keys, const char* tip)
{
    actions::ActStep a;
    a.id = id;
    a.name = name;
    a.category = category;
    a.kind = actions::Kind::kDetailsOf;
    a.rebindable = false;
    a.keys_text_ = keys;
    a.tip = tip;
    actions::enrol(a);
}

// The selection as group members (UX3's groups).
std::vector<organize::Member> selection_members()
{
    SelectionIds s = current_selection();
    std::vector<organize::Member> members;
    for (uint32_t id : s.objects)
        members.push_back({'o', id});
    for (uint32_t id : s.sectors)
        members.push_back({'s', id});
    if (s.node)
        members.push_back({'n', s.node});
    return members;
}

} // namespace

// --- quick sets (also the toolbar's Sets menu - level_editor_ux8_ui.cpp) ---

void store_set(const std::string& name)
{
    organize::State next = organize::at_hand();
    std::vector<organize::Member> members = selection_members();
    if (!nav::store_group(&next, name, members)) {
        get_say("Nothing selected");
        return;
    }
    organize::get_change(next, "Store selection set");
    get_say("Stored " + name + " (" + std::to_string(members.size()) + ")");
}

void recall_set(const std::string& name)
{
    int at = nav::find_group(organize::at_hand(), name);
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
    using actions::kShift;
    using actions::Kind;
    g_ids.pie_context_ = append_one("pie_context", "Pie Menu", "Panels", Kind::kPress,
                           {get_k(SDL_SCANCODE_Q), get_k(SDL_SCANCODE_TAB)},
                           "Hold for common actions; release over one to run it");
    g_ids.pie_view_ = append_one("pie_view", "View Pie Menu", "Panels", Kind::kPress,
                        {get_k(SDL_SCANCODE_Q, kAlt), get_k(SDL_SCANCODE_TAB, kShift)},
                        "Hold for view modes and overlays");
    g_ids.walk = append_one("walk_mode", "Walk Mode", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_GRAVE, kShift)},
                     "Walk at skater height with gravity");
    g_ids.follow = append_one("follow_selection", "Follow Selection", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_F, kShift)},
                       "Keep the camera on the selection as it moves", nullptr, [] { return g_following ? 1 : 0; });
    g_ids.teleport = append_one("teleport", "Go to Cursor", "Camera", Kind::kPress, {},
                         "Move the camera to the surface under the cursor");
    g_ids.pad = append_one("controller_mode", "Controller Mode", "Settings", Kind::kFlipOver, {},
                    "Use a game controller in the editor",
                    [] { set_pad_mode(!g_pad_mode); }, [] { return g_pad_mode ? 1 : 0; });
    for (int i = 0; i < nav::kQuickSets; ++i) {
        std::string n = std::to_string(i + 1);
        std::string store_id = "quick_set_store_" + n, recall_id = "quick_set_" + n;
        std::string store_name = "Store Set " + n, recall_name = "Select Set " + n;
        g_ids.store[i] = append_one(store_id.c_str(), store_name.c_str(), "Select", Kind::kPress,
                             {get_k(SDL_SCANCODE_1 + i, kCtrl | kAlt)},
                             "Store the selection as Set n");
        g_ids.recall[i] = append_one(recall_id.c_str(), recall_name.c_str(), "Select", Kind::kPress,
                              {get_k(SDL_SCANCODE_1 + i, kAlt | kShift)}, "Select Set n");
        for (int index : {g_ids.store[i], g_ids.recall[i]}) {
            if (actions::ActStep* a = actions::get_pull(index)) {
                size_t at = a->tip.find("Set n");
                if (at != std::string::npos)
                    a->tip.replace(at + 4, 1, n);
            }
        }
    }
    add_hint("hint_drill", "Cycle Under Cursor", "Select", "Alt+wheel", "Step through items under the cursor");
    add_hint("hint_teleport", "Go to Surface", "Camera", "Double-click", "Move the camera to the clicked surface");
    add_hint("hint_pad", "Controller", "Camera", "A game controller",
            "Enable Controller Mode in Preferences");
}

void prepare_all()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    g_pad_mode = GetPrivateProfileIntA("editor", "pad", 0, ini_path().c_str()) != 0;
    register_actions();
}

void kickoff()
{
    g_pie = pie::State{};
    g_walking = false;
    g_following = false;
    g_follower = {};
    g_glide = {};
    g_have_empty_click = false;
    g_pad_owns = false;
}

void halt() { kickoff(); }

// --- the controller ---

bool pad_mode() { return g_pad_mode; }

void set_pad_mode(bool on)
{
    g_pad_mode = on;
    WritePrivateProfileStringA("editor", "pad", on ? "1" : "0", ini_path().c_str());
    get_say(on ? "Controller mode on" : "Controller mode off");
}

namespace {

SDL_GameController* find_controller()
{
    // (the controllers the game's input opened - sio/sdl_input.cpp: the first attached)
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i))
            continue;
        SDL_GameController* c = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (c && SDL_GameControllerGetAttached(c))
            return c;
    }
    return nullptr;
}

constexpr float kDeadzone = 0.22f;
uint32_t g_pad_was; // (the buttons down last frame: bit per SDL_GameControllerButton)

} // namespace

Pad read_pad()
{
    Pad p;
    SDL_GameController* c = g_pad_mode ? find_controller() : nullptr;
    if (!c) {
        g_pad_was = 0;
        return p;
    }
    p.active = true;
    auto axis = [&](SDL_GameControllerAxis a) { return nav::get_stick(SDL_GameControllerGetAxis(c, a), kDeadzone); };
    p.move_x_ = axis(SDL_CONTROLLER_AXIS_LEFTX);
    p.move_y_ = -axis(SDL_CONTROLLER_AXIS_LEFTY);
    p.look_x_ = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    p.look_y_ = -axis(SDL_CONTROLLER_AXIS_RIGHTY);
    p.up_down_ = nav::get_stick(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), 0.1f) -
               nav::get_stick(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT), 0.1f);
    uint32_t now = 0;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX && b < 32; ++b) {
        if (SDL_GameControllerGetButton(c, static_cast<SDL_GameControllerButton>(b)))
            now |= 1u << b;
    }
    uint32_t pressed = now & ~g_pad_was, released = g_pad_was & ~now;
    g_pad_was = now;
    auto down = [&](SDL_GameControllerButton b) { return (now >> b & 1u) != 0; };
    auto press = [&](SDL_GameControllerButton b) { return (pressed >> b & 1u) != 0; };
    p.a_down_ = down(SDL_CONTROLLER_BUTTON_A);
    p.a_presses_ = press(SDL_CONTROLLER_BUTTON_A) ? 1 : 0;
    p.a_releases_ = (released >> SDL_CONTROLLER_BUTTON_A & 1u) ? 1 : 0;
    p.b = press(SDL_CONTROLLER_BUTTON_B);
    p.x = press(SDL_CONTROLLER_BUTTON_X);
    p.y = press(SDL_CONTROLLER_BUTTON_Y);
    p.x_down_ = down(SDL_CONTROLLER_BUTTON_X);
    p.y_down_ = down(SDL_CONTROLLER_BUTTON_Y);
    p.back = press(SDL_CONTROLLER_BUTTON_BACK);
    p.r3 = press(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    p.lb = press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    p.rb = press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    p.fast = down(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    p.up = down(SDL_CONTROLLER_BUTTON_DPAD_UP);
    p.down = down(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    p.left = down(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    p.right = down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    // (Start isn't the editor's: the game's pause key leaves it - level_editor_exit)
    uint32_t mine = now & ~(1u << SDL_CONTROLLER_BUTTON_START) & ~(1u << SDL_CONTROLLER_BUTTON_GUIDE);
    p.used = mine != 0 || p.move_x_ != 0 || p.move_y_ != 0 || p.look_x_ != 0 || p.look_y_ != 0 || p.up_down_ != 0;
    // (anything that edits stops a simulation first)
    if (p.a_presses_ || p.back || p.lb || p.rb || p.up || p.down || p.left || p.right)
        stop_simulation();
    return p;
}

const uint8_t* pad_keys(const uint8_t* keys, const Pad& pad)
{
    static uint8_t merged[SDL_NUM_SCANCODES];
    if (!pad.active)
        return keys;
    std::memcpy(merged, keys, sizeof(merged));
    auto hold = [&](int action) {
        const actions::ActStep* a = actions::get_pull(action);
        if (!a)
            return;
        for (const actions::Chord& c : a->keys) {
            if (c.scancode > 0 && c.scancode < SDL_NUM_SCANCODES && c.mods == 0) {
                merged[c.scancode] = 1;
                return;
            }
        }
    };
    if (pad.up)
        hold(actions::kNudgeForward);
    if (pad.down)
        hold(actions::kNudgeBack);
    if (pad.left)
        hold(actions::kNudgeLeft);
    if (pad.right)
        hold(actions::kNudgeRight);
    if (pad.fast)
        merged[SDL_SCANCODE_LSHIFT] = 1;
    return merged;
}

bool get_pad_cursor(const Pad& pad, bool mouse_moved, float* x, float* y)
{
    if (!pad.active) {
        g_pad_owns = false;
        return false;
    }
    if (mouse_moved)
        g_pad_owns = false;
    if (pad.used)
        g_pad_owns = true;
    if (!g_pad_owns || !g_have_view_centre)
        return false;
    *x = g_view_x;
    *y = g_view_y;
    return true;
}

bool pad_cancel(const Pad& pad)
{
    if (!pad.active)
        return false;
    if (g_pie.open)
        return false; // (the pie takes B)
    if (pad.back)
        actions::fire_off(actions::kUndo);
    if (pad.lb)
        actions::fire_off(actions::kTurnLeft);
    if (pad.rb)
        actions::fire_off(actions::kTurnRight);
    if (pad.r3)
        actions::fire_off(actions::kFrameSelection);
    return pad.b && !g_walking;
}

// --- once a frame ---

namespace {

void open_pie(pie::Kind kind, bool have_selection, float x, float y, int key, bool pad_button)
{
    g_pie = pie::State{};
    g_pie.open = true;
    g_pie.kind = kind;
    g_pie.items = kind == pie::Kind::kView ? pie::view_items() : pie::context_items(have_selection);
    g_pie.x = x;
    g_pie.y = y;
    g_pie.key = key;
    g_pie.pad_button_ = pad_button;
    g_pie.opened_at_ = SDL_GetTicks();
}

void run_slice()
{
    int at = g_pie.hovered;
    std::vector<pie::Item> items = g_pie.items;
    g_pie.open = false;
    if (at < 0 || at >= static_cast<int>(items.size()) || !items[at].enabled())
        return;
    if (items[at].edits)
        stop_simulation();
    items[at].run();
}

} // namespace

void tick_frame(TickCycle& f)
{
    using namespace actions;
    // Quick sets: Ctrl+Alt+n stores, Alt+Shift+n selects (not while dragging: digits are typed values then).
    if (!f.dragging) {
        for (int i = 0; i < nav::kQuickSets; ++i) {
            if (squeezed(g_ids.store[i]))
                store_set(nav::quick_set_name(i + 1));
            if (squeezed(g_ids.recall[i]))
                recall_set(nav::quick_set_name(i + 1));
        }
    }
    // The follow camera: on frames the selection (F) and keeps with it.
    if (squeezed(g_ids.follow) && !f.dragging) {
        if (g_following) {
            g_following = false;
            get_say("Follow off");
        } else if (!f.have_selection_) {
            get_say("Nothing selected");
        } else {
            g_following = true;
            g_follower = {};
            fire_off(kFrameSelection);
            get_say("Following selection");
        }
    }
    // The pies.
    bool pad_open = f.pad.active && (f.pad.x || f.pad.y) && !g_pie.open;
    bool key_context = squeezed(g_ids.pie_context_), key_view = squeezed(g_ids.pie_view_);
    if ((key_context || key_view || pad_open) && !g_pie.open && !f.right && !f.dragging && !f.browser && !g_walking &&
        ui::underway()) { // (drawn by the UI: the old HUD has no pies)
        // ([editor] wasd_flies=1: a pie key that's also a fly key held flies)
        bool flying = xform::fly_always() && !pad_open &&
                      (get_held(f.keys, kFlyDown) || get_held(f.keys, kFlyUp) || get_held(f.keys, kForward) ||
                       get_held(f.keys, kBack) || get_held(f.keys, kLeft) || get_held(f.keys, kRight));
        if (!flying) {
            float x = f.mouse_x_, y = f.mouse_y_;
            if (pad_open && g_have_view_centre) {
                x = g_view_x;
                y = g_view_y;
            }
            if (pad_open)
                open_pie(f.pad.y ? pie::Kind::kView : pie::Kind::kCtxInfo, f.have_selection_, x, y, -1, true);
            else
                open_pie(key_view ? pie::Kind::kView : pie::Kind::kCtxInfo, f.have_selection_, x, y,
                        key_view ? g_ids.pie_view_ : g_ids.pie_context_, false);
        }
    }
    if (!g_pie.open)
        return;
    // Pointing: the mouse (or the left stick) from the centre.
    float dx = f.mouse_x_ - g_pie.x, dy = f.mouse_y_ - g_pie.y;
    if (g_pie.pad_button_ || (f.pad.active && (f.pad.move_x_ != 0 || f.pad.move_y_ != 0))) {
        dx = f.pad.move_x_ * 100.0f;
        dy = -f.pad.move_y_ * 100.0f;
    }
    g_pie.hovered = nav::slice_at(dx, dy, static_cast<int>(g_pie.items.size()), pie::kDeadzone);
    bool clicked = f.left_presses_ && *f.left_presses_ > 0;
    if (f.left_presses_)
        *f.left_presses_ = 0; // (the view doesn't see clicks while a pie is up)
    if (f.left_releases_)
        *f.left_releases_ = 0;
    if (f.right || (f.pad.active && f.pad.b)) {
        g_pie.open = false;
        return;
    }
    if (clicked || (f.pad.active && f.pad.a_presses_)) {
        run_slice();
        return;
    }
    // Letting go of its key / button: a hold runs what's pointed at; a tap keeps it up for a click.
    bool held = g_pie.pad_button_ ? (g_pie.kind == pie::Kind::kView ? f.pad.y_down_ : f.pad.x_down_)
                                : g_pie.key >= 0 && get_held(f.keys, g_pie.key);
    if (!held && !g_pie.sticky) {
        if (SDL_GetTicks() - g_pie.opened_at_ < nav::kPieTapMs && g_pie.hovered < 0)
            g_pie.sticky = true;
        else
            run_slice();
    }
}

bool get_cancel()
{
    if (g_pie.open) {
        g_pie.open = false;
        return true;
    }
    if (g_walking) {
        g_walking = false;
        get_say("Walk mode off");
        return true;
    }
    return false;
}

bool wants_escape() { return g_pie.open || g_walking; }

bool get_following() { return g_following; }
nav::Follower& get_follower() { return g_follower; }

bool on_foot() { return g_walking; }
void set_walking(bool on, V3 camera)
{
    g_walking = on;
    g_pie.open = false;
    if (on) {
        g_walker = nav::walker_at(camera);
        g_glide.on = false;
        get_say("Walk Mode: WASD walk, Space jump, Shift run, Esc exit", 5.0);
    } else {
        get_say("Walk mode off");
    }
}
nav::Walker& walker_state() { return g_walker; }

bool take_walk_toggle() { return actions::squeezed(g_ids.walk); }
bool take_teleport() { return actions::squeezed(g_ids.teleport); }

bool note_empty_click(uint32_t now, float x, float y)
{
    bool twice = g_have_empty_click && nav::is_double_click(g_last_empty_click, g_last_empty_x, g_last_empty_y, now, x, y);
    g_have_empty_click = !twice;
    g_last_empty_click = now;
    g_last_empty_x = x;
    g_last_empty_y = y;
    return twice;
}

void start_glide(V3 from, V3 to)
{
    g_glide.on = true;
    g_glide.from = from;
    g_glide.to = to;
    g_glide.t = 0.0f;
}

bool glide(V3* pos, double seconds)
{
    if (!g_glide.on)
        return false;
    g_glide.t = std::min(1.0f, g_glide.t + static_cast<float>(seconds / 0.35));
    float e = view_cam::get_ease(g_glide.t);
    *pos = g_glide.from + (g_glide.to - g_glide.from) * e;
    if (g_glide.t >= 1.0f)
        g_glide.on = false;
    return true;
}

void stop_glide() { g_glide.on = false; }

bool pie_open() { return g_pie.open; }
bool pie_sticky() { return g_pie.sticky; }

// (for the UI file)
const pie::State& pie_now() { return g_pie; }
void note_view_centre(float x, float y)
{
    g_view_x = x;
    g_view_y = y;
    g_have_view_centre = true;
}
bool pad_owns_cursor() { return g_pad_mode && g_pad_owns; }

} // namespace MenuSystem::level_editor::ux8

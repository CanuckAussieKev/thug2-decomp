// NOT ORIGINAL - the level editor's navigation and hints: the game side (ours). See level_editor_ux8.h.

#include "game/front/level_editor_ux8.h"

#include <SDL2/SDL.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <functional>

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_camera.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_organize.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_playtest.h"
#include "game/front/level_editor_transform.h"
#include "game/front/level_editor_ux8_pie.h"
#include "hook/log.h"

namespace Front::LevelEditor::Ux8 {

namespace {

// The registry's indices of this module's actions (Init).
struct Ids {
    int pieContext = -1, pieView = -1, walk = -1, follow = -1, teleport = -1, pad = -1;
    int store[Nav::kQuickSets] = {}, recall[Nav::kQuickSets] = {};
} g_ids;

bool g_padMode;
bool g_padOwns; // (the crosshair is the cursor)
bool g_following;
Nav::Follower g_follower;
bool g_walking;
Nav::Walker g_walker;
uint32_t g_lastEmptyClick;
float g_lastEmptyX, g_lastEmptyY;
bool g_haveEmptyClick;
struct GlideState {
    bool on = false;
    V3 from = {}, to = {};
    float t = 0.0f;
} g_glide;

Pie::State g_pie;
float g_viewX, g_viewY;
bool g_haveViewCentre;

std::string IniPath() { return GameFolderPath("thug2-decomp.ini"); }

void Say(const std::string& text, double seconds = 3.0) { ShowMessage(text, seconds); }

// A simulation (level_editor_playtest.h) stops before the controller or a pie edits anything: its key handler only
// sees keys typed.
void StopSimulation()
{
    if (PlayTest::Simulating())
        PlayTest::Request(PlayTest::Ask::ToggleSimulate);
}

Actions::Chord K(int scancode, uint8_t mods = 0) { return {scancode, mods}; }

int Add(const char* id, const char* name, const char* category, Actions::Kind kind, std::vector<Actions::Chord> keys,
        const char* tip, std::function<void()> run = nullptr, std::function<int()> checked = nullptr)
{
    Actions::Action a;
    a.id = id;
    a.name = name;
    a.category = category;
    a.kind = kind;
    a.defaults = keys;
    a.keys = keys;
    a.tip = tip;
    a.run = std::move(run);
    a.checked = std::move(checked);
    return Actions::Register(a);
}

void AddHint(const char* id, const char* name, const char* category, const char* keys, const char* tip)
{
    Actions::Action a;
    a.id = id;
    a.name = name;
    a.category = category;
    a.kind = Actions::Kind::Info;
    a.rebindable = false;
    a.keysText = keys;
    a.tip = tip;
    Actions::Register(a);
}

// The selection as group members (UX3's groups).
std::vector<Organize::Member> SelectionMembers()
{
    SelectionIds s = CurrentSelection();
    std::vector<Organize::Member> members;
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

void StoreSet(const std::string& name)
{
    Organize::State next = Organize::Current();
    std::vector<Organize::Member> members = SelectionMembers();
    if (!Nav::StoreGroup(&next, name, members)) {
        Say("Select something first - \"" + name + "\" is made of the selection");
        return;
    }
    Organize::Change(next, "Store selection set");
    Say("\"" + name + "\" stored: " + std::to_string(members.size()) + " (the Outliner's Groups, the toolbar's Sets)");
}

void RecallSet(const std::string& name)
{
    int at = Nav::FindGroup(Organize::Current(), name);
    if (at < 0) {
        Say("No \"" + name + "\" yet - Ctrl+Alt+digit stores the selection as a set");
        return;
    }
    Organize::SelectGroup(at);
}

void AddToSet(int index)
{
    if (SelectionMembers().empty()) {
        Say("Select something to add first");
        return;
    }
    Organize::AddToGroup(index);
}

void TakeFromSet(int index)
{
    Organize::State next = Organize::Current();
    if (!Nav::RemoveFromGroup(&next, index, SelectionMembers())) {
        Say("None of the selection is in that set");
        return;
    }
    Organize::Change(next, "Take from selection set");
}

// --- the actions ---

void RegisterActions()
{
    using Actions::kAlt;
    using Actions::kCtrl;
    using Actions::kShift;
    using Actions::Kind;
    g_ids.pieContext = Add("pie_context", "Pie menu: actions", "Panels", Kind::Press,
                           {K(SDL_SCANCODE_Q), K(SDL_SCANCODE_TAB)},
                           "Hold for a pie of the common actions for the selection (or, nothing selected, adding and "
                           "tools); point and let go - or tap it and click (not while the right button flies)");
    g_ids.pieView = Add("pie_view", "Pie menu: view", "Panels", Kind::Press,
                        {K(SDL_SCANCODE_Q, kAlt), K(SDL_SCANCODE_TAB, kShift)},
                        "Hold for a pie of the view modes, collision, trigger volumes, jump arcs and views");
    g_ids.walk = Add("walk_mode", "Walk mode", "Camera", Kind::Press, {K(SDL_SCANCODE_GRAVE, kShift)},
                     "The camera at a skater's eye height with gravity: W A S D walk, Space jumps, Shift runs, the mouse "
                     "looks; again or Esc stops");
    g_ids.follow = Add("follow_selection", "Follow the selection", "Camera", Kind::Press, {K(SDL_SCANCODE_F, kShift)},
                       "Frames the selection and keeps the camera with it as it moves (even after flying off it); "
                       "again: stops", nullptr, [] { return g_following ? 1 : 0; });
    g_ids.teleport = Add("teleport", "Teleport to the cursor", "Camera", Kind::Press, {},
                         "The camera to the surface under the cursor (as a double click on it does)");
    g_ids.pad = Add("controller_mode", "Controller mode", "Settings", Kind::Toggle, {},
                    "A game controller drives the editor: sticks fly / look, triggers down / up, A selects, B backs "
                    "out, X / Y pies, D-pad nudges ([editor] pad=1)",
                    [] { SetPadMode(!g_padMode); }, [] { return g_padMode ? 1 : 0; });
    for (int i = 0; i < Nav::kQuickSets; ++i) {
        std::string n = std::to_string(i + 1);
        std::string storeId = "quick_set_store_" + n, recallId = "quick_set_" + n;
        std::string storeName = "Store selection set " + n, recallName = "Select set " + n;
        g_ids.store[i] = Add(storeId.c_str(), storeName.c_str(), "Select", Kind::Press,
                             {K(SDL_SCANCODE_1 + i, kCtrl | kAlt)},
                             "The selection kept as \"Set n\" (a group: saved in the layout, in the Outliner's Groups)");
        g_ids.recall[i] = Add(recallId.c_str(), recallName.c_str(), "Select", Kind::Press,
                              {K(SDL_SCANCODE_1 + i, kAlt | kShift)}, "\"Set n\" selected again");
        for (int index : {g_ids.store[i], g_ids.recall[i]}) {
            if (Actions::Action* a = Actions::Get(index)) {
                size_t at = a->tip.find("Set n");
                if (at != std::string::npos)
                    a->tip.replace(at + 4, 1, n);
            }
        }
    }
    AddHint("hint_drill", "Drill through", "Select", "Alt+wheel",
            "Steps what a click would take through everything under the cursor, front to back (then click)");
    AddHint("hint_teleport", "Go there", "Camera", "Double-click",
            "On a surface (nothing a click selects): the camera glides there");
    AddHint("hint_pad", "Controller", "Camera", "A game controller",
            "Controller mode (Preferences): sticks fly / look, LT / RT down / up, A select, B back, X / Y pies, D-pad "
            "nudge, LB / RB turn, R3 frame, Back undo");
}

void Init()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    g_padMode = GetPrivateProfileIntA("editor", "pad", 0, IniPath().c_str()) != 0;
    RegisterActions();
}

void Start()
{
    g_pie = Pie::State{};
    g_walking = false;
    g_following = false;
    g_follower = {};
    g_glide = {};
    g_haveEmptyClick = false;
    g_padOwns = false;
}

void Stop() { Start(); }

// --- the controller ---

bool PadMode() { return g_padMode; }

void SetPadMode(bool on)
{
    g_padMode = on;
    WritePrivateProfileStringA("editor", "pad", on ? "1" : "0", IniPath().c_str());
    Say(on ? "Controller mode: the first game controller drives the editor (A selects at the crosshair)"
           : "Controller mode off");
}

namespace {

SDL_GameController* FindController()
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
uint32_t g_padWas; // (the buttons down last frame: bit per SDL_GameControllerButton)

} // namespace

Pad ReadPad()
{
    Pad p;
    SDL_GameController* c = g_padMode ? FindController() : nullptr;
    if (!c) {
        g_padWas = 0;
        return p;
    }
    p.active = true;
    auto axis = [&](SDL_GameControllerAxis a) { return Nav::Stick(SDL_GameControllerGetAxis(c, a), kDeadzone); };
    p.moveX = axis(SDL_CONTROLLER_AXIS_LEFTX);
    p.moveY = -axis(SDL_CONTROLLER_AXIS_LEFTY);
    p.lookX = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    p.lookY = -axis(SDL_CONTROLLER_AXIS_RIGHTY);
    p.upDown = Nav::Stick(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), 0.1f) -
               Nav::Stick(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT), 0.1f);
    uint32_t now = 0;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX && b < 32; ++b) {
        if (SDL_GameControllerGetButton(c, static_cast<SDL_GameControllerButton>(b)))
            now |= 1u << b;
    }
    uint32_t pressed = now & ~g_padWas, released = g_padWas & ~now;
    g_padWas = now;
    auto down = [&](SDL_GameControllerButton b) { return (now >> b & 1u) != 0; };
    auto press = [&](SDL_GameControllerButton b) { return (pressed >> b & 1u) != 0; };
    p.aDown = down(SDL_CONTROLLER_BUTTON_A);
    p.aPresses = press(SDL_CONTROLLER_BUTTON_A) ? 1 : 0;
    p.aReleases = (released >> SDL_CONTROLLER_BUTTON_A & 1u) ? 1 : 0;
    p.b = press(SDL_CONTROLLER_BUTTON_B);
    p.x = press(SDL_CONTROLLER_BUTTON_X);
    p.y = press(SDL_CONTROLLER_BUTTON_Y);
    p.xDown = down(SDL_CONTROLLER_BUTTON_X);
    p.yDown = down(SDL_CONTROLLER_BUTTON_Y);
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
    p.used = mine != 0 || p.moveX != 0 || p.moveY != 0 || p.lookX != 0 || p.lookY != 0 || p.upDown != 0;
    // (anything that edits stops a simulation first)
    if (p.aPresses || p.back || p.lb || p.rb || p.up || p.down || p.left || p.right)
        StopSimulation();
    return p;
}

const uint8_t* PadKeys(const uint8_t* keys, const Pad& pad)
{
    static uint8_t merged[SDL_NUM_SCANCODES];
    if (!pad.active)
        return keys;
    std::memcpy(merged, keys, sizeof(merged));
    auto hold = [&](int action) {
        const Actions::Action* a = Actions::Get(action);
        if (!a)
            return;
        for (const Actions::Chord& c : a->keys) {
            if (c.scancode > 0 && c.scancode < SDL_NUM_SCANCODES && c.mods == 0) {
                merged[c.scancode] = 1;
                return;
            }
        }
    };
    if (pad.up)
        hold(Actions::kNudgeForward);
    if (pad.down)
        hold(Actions::kNudgeBack);
    if (pad.left)
        hold(Actions::kNudgeLeft);
    if (pad.right)
        hold(Actions::kNudgeRight);
    if (pad.fast)
        merged[SDL_SCANCODE_LSHIFT] = 1;
    return merged;
}

bool PadCursor(const Pad& pad, bool mouseMoved, float* x, float* y)
{
    if (!pad.active) {
        g_padOwns = false;
        return false;
    }
    if (mouseMoved)
        g_padOwns = false;
    if (pad.used)
        g_padOwns = true;
    if (!g_padOwns || !g_haveViewCentre)
        return false;
    *x = g_viewX;
    *y = g_viewY;
    return true;
}

bool PadCancel(const Pad& pad)
{
    if (!pad.active)
        return false;
    if (g_pie.open)
        return false; // (the pie takes B)
    if (pad.back)
        Actions::Trigger(Actions::kUndo);
    if (pad.lb)
        Actions::Trigger(Actions::kTurnLeft);
    if (pad.rb)
        Actions::Trigger(Actions::kTurnRight);
    if (pad.r3)
        Actions::Trigger(Actions::kFrameSelection);
    return pad.b && !g_walking;
}

// --- once a frame ---

namespace {

void OpenPie(Pie::Kind kind, bool haveSelection, float x, float y, int key, bool padButton)
{
    g_pie = Pie::State{};
    g_pie.open = true;
    g_pie.kind = kind;
    g_pie.items = kind == Pie::Kind::View ? Pie::ViewItems() : Pie::ContextItems(haveSelection);
    g_pie.x = x;
    g_pie.y = y;
    g_pie.key = key;
    g_pie.padButton = padButton;
    g_pie.openedAt = SDL_GetTicks();
}

void RunSlice()
{
    int at = g_pie.hovered;
    std::vector<Pie::Item> items = g_pie.items;
    g_pie.open = false;
    if (at < 0 || at >= static_cast<int>(items.size()) || !items[at].enabled())
        return;
    if (items[at].edits)
        StopSimulation();
    items[at].run();
}

} // namespace

void Update(Frame& f)
{
    using namespace Actions;
    // Quick sets: Ctrl+Alt+n stores, Alt+Shift+n selects (not while dragging: digits are typed values then).
    if (!f.dragging) {
        for (int i = 0; i < Nav::kQuickSets; ++i) {
            if (Pressed(g_ids.store[i]))
                StoreSet(Nav::QuickSetName(i + 1));
            if (Pressed(g_ids.recall[i]))
                RecallSet(Nav::QuickSetName(i + 1));
        }
    }
    // The follow camera: on frames the selection (F) and keeps with it.
    if (Pressed(g_ids.follow) && !f.dragging) {
        if (g_following) {
            g_following = false;
            Say("Follow off (Shift+F)");
        } else if (!f.haveSelection) {
            Say("Select something to follow first (Shift+F)");
        } else {
            g_following = true;
            g_follower = {};
            Trigger(kFrameSelection);
            Say("Following the selection - the camera keeps with it as it moves (Shift+F stops)");
        }
    }
    // The pies.
    bool padOpen = f.pad.active && (f.pad.x || f.pad.y) && !g_pie.open;
    bool keyContext = Pressed(g_ids.pieContext), keyView = Pressed(g_ids.pieView);
    if ((keyContext || keyView || padOpen) && !g_pie.open && !f.right && !f.dragging && !f.browser && !g_walking) {
        // ([editor] wasd_flies=1: a pie key that's also a fly key held flies)
        bool flying = Transform::FlyAlways() && !padOpen &&
                      (Held(f.keys, kFlyDown) || Held(f.keys, kFlyUp) || Held(f.keys, kForward) ||
                       Held(f.keys, kBack) || Held(f.keys, kLeft) || Held(f.keys, kRight));
        if (!flying) {
            float x = f.mouseX, y = f.mouseY;
            if (padOpen && g_haveViewCentre) {
                x = g_viewX;
                y = g_viewY;
            }
            if (padOpen)
                OpenPie(f.pad.y ? Pie::Kind::View : Pie::Kind::Context, f.haveSelection, x, y, -1, true);
            else
                OpenPie(keyView ? Pie::Kind::View : Pie::Kind::Context, f.haveSelection, x, y,
                        keyView ? g_ids.pieView : g_ids.pieContext, false);
        }
    }
    if (!g_pie.open)
        return;
    // Pointing: the mouse (or the left stick) from the centre.
    float dx = f.mouseX - g_pie.x, dy = f.mouseY - g_pie.y;
    if (g_pie.padButton || (f.pad.active && (f.pad.moveX != 0 || f.pad.moveY != 0))) {
        dx = f.pad.moveX * 100.0f;
        dy = -f.pad.moveY * 100.0f;
    }
    g_pie.hovered = Nav::SliceAt(dx, dy, static_cast<int>(g_pie.items.size()), Pie::kDeadzone);
    bool clicked = f.leftPresses && *f.leftPresses > 0;
    if (f.leftPresses)
        *f.leftPresses = 0; // (the view doesn't see clicks while a pie is up)
    if (f.leftReleases)
        *f.leftReleases = 0;
    if (f.right || (f.pad.active && f.pad.b)) {
        g_pie.open = false;
        return;
    }
    if (clicked || (f.pad.active && f.pad.aPresses)) {
        RunSlice();
        return;
    }
    // Letting go of its key / button: a hold runs what's pointed at; a tap keeps it up for a click.
    bool held = g_pie.padButton ? (g_pie.kind == Pie::Kind::View ? f.pad.yDown : f.pad.xDown)
                                : g_pie.key >= 0 && Held(f.keys, g_pie.key);
    if (!held && !g_pie.sticky) {
        if (SDL_GetTicks() - g_pie.openedAt < Nav::kPieTapMs && g_pie.hovered < 0)
            g_pie.sticky = true;
        else
            RunSlice();
    }
}

bool Cancel()
{
    if (g_pie.open) {
        g_pie.open = false;
        return true;
    }
    if (g_walking) {
        g_walking = false;
        Say("Walk mode off");
        return true;
    }
    return false;
}

bool WantsEscape() { return g_pie.open || g_walking; }

bool Following() { return g_following; }
Nav::Follower& Follower() { return g_follower; }

bool Walking() { return g_walking; }
void SetWalking(bool on, V3 camera)
{
    g_walking = on;
    g_pie.open = false;
    if (on) {
        g_walker = Nav::WalkerAt(camera);
        g_glide.on = false;
        Say("Walk mode: W A S D walk, Space jumps, Shift runs, the mouse looks - Shift+` or Esc stops", 5.0);
    } else {
        Say("Walk mode off");
    }
}
Nav::Walker& WalkerState() { return g_walker; }

bool TakeWalkToggle() { return Actions::Pressed(g_ids.walk); }
bool TakeTeleport() { return Actions::Pressed(g_ids.teleport); }

bool NoteEmptyClick(uint32_t now, float x, float y)
{
    bool twice = g_haveEmptyClick && Nav::IsDoubleClick(g_lastEmptyClick, g_lastEmptyX, g_lastEmptyY, now, x, y);
    g_haveEmptyClick = !twice;
    g_lastEmptyClick = now;
    g_lastEmptyX = x;
    g_lastEmptyY = y;
    return twice;
}

void StartGlide(V3 from, V3 to)
{
    g_glide.on = true;
    g_glide.from = from;
    g_glide.to = to;
    g_glide.t = 0.0f;
}

bool Glide(V3* pos, double seconds)
{
    if (!g_glide.on)
        return false;
    g_glide.t = std::min(1.0f, g_glide.t + static_cast<float>(seconds / 0.35));
    float e = Camera::Ease(g_glide.t);
    *pos = g_glide.from + (g_glide.to - g_glide.from) * e;
    if (g_glide.t >= 1.0f)
        g_glide.on = false;
    return true;
}

void StopGlide() { g_glide.on = false; }

bool PieOpen() { return g_pie.open; }
bool PieSticky() { return g_pie.sticky; }

// (for the UI file)
const Pie::State& PieNow() { return g_pie; }
void NoteViewCentre(float x, float y)
{
    g_viewX = x;
    g_viewY = y;
    g_haveViewCentre = true;
}
bool PadOwnsCursor() { return g_padMode && g_padOwns; }

} // namespace Front::LevelEditor::Ux8

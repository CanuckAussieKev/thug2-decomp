#pragma once

#include <cstdint>

#include "platform/keyboard.h"
#include <string_view>
#include <functional>
#include <string>
#include <vector>

// NOT ORIGINAL - the level editor's actions and keymap (ours; see level_editor.h): every action the editor offers - a
// key press, a key held, a setting to switch, or a hint (a mouse gesture, a key of a mode) - in one registry, with its
// name, its category, its keys and, for the command search (Ctrl+Space / F3 - level_editor_ux3_ui.cpp), what running
// it does and whether it can run now. The editor's key handling reads its keys from here (Pressed / Held), so the keys
// can be rebound: the Keys section (a window of its own, or hosted by the preferences window - Ui::keys_section) keeps
// them in thug2-decomp.ini [editor_keys] as `<id>=<keys>` ("Ctrl+Z", "Ctrl+Y, Ctrl+Shift+Z", "none"); only keys that
// differ from the defaults are written. F1's help lists from the registry too.
//
// A key press resolves to actions (Resolve): those bound to that key whose Ctrl matches exactly (bound with Ctrl: held;
// without: not held) and whose Shift / Alt, when bound, are held; of those the most specific (the most modifiers)
// fire. So Z turns and Ctrl+Z undoes; Shift+G (Select similar) wins over G (drop) when Shift is held while Shift+Z
// still turns (nothing bound to it: Shift is a qualifier there - [ / ] fine with Shift, stretch with Alt); H hides and
// Alt+H shows all; Delete deletes and Ctrl+Shift+Delete resets the level. A key held (Hold: flying, nudging) counts
// while its key is down without Ctrl unless bound with it.
//
// Running an action from the command search, a toolbar or a menu: its `run`, else Trigger - as if its key had been
// pressed (the next frame's Pressed is true), so the editor's own key code does it under its own conditions.
//
// Other agents' and later work's keys: appended to the built-in table (kBuiltins) and to Id below; the order is the
// registry's and the help's.
namespace frontend::level_editor::actions {

// --- the pure parts --------------------------------------------------------------------------------------------------

constexpr uint8_t kCtrl = 1;
constexpr uint8_t kShift = 2;
constexpr uint8_t kAlt = 4;
// A key and its modifiers (an SDL scan code; 0: none).
struct Chord {
    int scancode = 0;
    uint8_t mods = 0;
};
inline bool operator==(Chord a, Chord b) { return a.scancode == b.scancode && a.mods == b.mods; }
// The key's name as the keymap writes it ("Z", "F1", "Space", "Delete", "[", "PageUp"); "" for a key it doesn't name.
const char* get_key_name(int scancode);
// A key's scan code from its name (any case); 0 if none.
int key_from_name(const std::string& name);
// "Ctrl+Shift+Z" and back (modifiers in any order, any case; "Control" and "Option" too). False: no key, an unknown key,
// a modifier alone.
std::string format_chord(Chord chord);
bool parse_chord(const std::string& text, Chord* chord);
// A list: "Ctrl+Y, Ctrl+Shift+Z" / "none" (the empty list) and back (false: any chord unreadable).
std::string format_keys(const std::vector<Chord>& keys);
bool parse_keys(const std::string& text, std::vector<Chord>* keys);

// A key's scan code and the modifiers held.
struct KeyState {
    int scancode{};
    uint8_t held{};
};

// Whether a chord matches a key pressed with `held` modifiers (see the top), and how specific it is.
bool chord_matches(Chord chord, KeyState key_state);
int get_specificity(Chord chord);
// The actions a key press fires: of `bound` (each action's keys), the indices of those matching that are most specific
// (several when several are bound alike - a conflict).
std::vector<int> settle_up(const std::vector<std::vector<Chord>>& bound, int scancode, uint8_t held);
// A key held counts for a chord: the key down, Ctrl as bound (held only when bound with it), Shift / Alt held when bound.
bool hold_matches(Chord chord, bool key_down, uint8_t held);
// Whether two chords can fire on the same press (the same key, Ctrl alike, and neither needs a Shift / Alt the other's
// press would lack - a press of the more specific fires only it): the Keys section's conflicts are exact duplicates.
bool same_binding(Chord a, Chord b);

// The command search's fuzzy match: 0 when `query`'s characters (spaces ignored, any case) don't appear in `text` in
// order; else a score, higher for a match at the start, at the start of words, and for runs of consecutive characters.
// An empty query scores 1.
int fuzzy_score(const std::string& query, std::string_view text);

// --- the registry ------------------------------------------------------------------------------------------------

enum class Kind : uint8_t {
    kPress,     // a key press does it
    kHold,      // while its key is held (flying, nudging)
    kFlipOver,  // a setting switched (checked: on / off)
    kDetailsOf, // a hint: a mouse gesture, a mode's own key - listed, not rebindable
};

struct ActStep {
    std::string id;       // the ini key ("undo")
    std::string name;     // "Undo"
    std::string category; // "Edit"
    std::string tip;      // what it does, more fully (the search's second line, the help)
    Kind kind = Kind::kPress;
    std::vector<Chord> defaults, keys;
    std::string keys_text_; // Info: what to show for its keys ("Right mouse", "Shift while dragging")
    bool rebindable = true;
    std::function<void()> run;     // the search runs it (none: Trigger)
    std::function<bool()> enabled; // can it run now (none: yes)
    std::function<int()> checked;  // Toggle: 1 on, 0 off (none: -1)
};

// The editor's own actions, in the built-in table's order (an action's index is its Id).
constexpr int kHelp = 0;
constexpr int kPalette = 1;
constexpr int kBrowser = 2;
constexpr int kPlace = 3;
constexpr int kHistory = 4;
constexpr int kChanges = 5;
constexpr int kKeys = 6;
constexpr int kSnapshots = 7;
constexpr int kUndo = 8;
constexpr int kRedo = 9;
constexpr int kSave = 10;
constexpr int kDuplicate = 11;
constexpr int kRestoreAutosave = 12;
constexpr int kDiscardAutosave = 13;
constexpr int kDelete = 14;
constexpr int kHome = 15;
constexpr int kResetLevel = 16;
constexpr int kDeselect = 17;
constexpr int kLock = 18;
constexpr int kCopy = 19;
constexpr int kPaste = 20;
constexpr int kCopyProps = 21;
constexpr int kPasteTransform = 22;
constexpr int kPasteFields = 23;
constexpr int kBoxSelect = 24;
constexpr int kSelectSimilar = 25;
constexpr int kSelectFamily = 26;
constexpr int kInvertSelection = 27;
constexpr int kSelectAll = 28;
constexpr int kMakeGroup = 29;
constexpr int kTurnLeft = 30;
constexpr int kTurnRight = 31;
constexpr int kScaleDown = 32;
constexpr int kScaleUp = 33;
constexpr int kDrop = 34;
constexpr int kForward = 35;
constexpr int kBack = 36;
constexpr int kLeft = 37;
constexpr int kRight = 38;
constexpr int kFlyUp = 39;
constexpr int kFlyDown = 40;
constexpr int kNudgeUp = 41;
constexpr int kNudgeDown = 42;
constexpr int kFaster = 43;
constexpr int kSlower = 44;
constexpr int kCollision = 45;
constexpr int kFaces = 46;
constexpr int kTriggers = 47;
constexpr int kOcclusion = 48;
constexpr int kHide = 49;
constexpr int kIsolate = 50;
constexpr int kShowAll = 51;
constexpr int kCameraNode = 52;
constexpr int kLogCamera = 53;
constexpr int kChainInsert = 54;
constexpr int kChainExtend = 55;
constexpr int kChainLink = 56;
constexpr int kChainUnlink = 57;
constexpr int kChainFinish = 58;
// (UX1: play from here, the camera's tools, the preferences - level_editor_play.h, level_editor_camera.h,
// level_editor_prefs.h)
constexpr int kPlay = 59;
constexpr int kPreferences = 60;
constexpr int kFrameSelection = 61;
constexpr int kBookmark1 = 62;
constexpr int kBookmarkStore1 = kBookmark1 + 9;
constexpr int kViewTop = kBookmarkStore1 + 9;
constexpr int kViewBottom = kViewTop + 1;
constexpr int kCamFrontView = kViewBottom + 1;
constexpr int kViewBack = kCamFrontView + 1;
constexpr int kPanRightAction = kViewBack + 1;
constexpr int kPanLeftAction = kPanRightAction + 1;
constexpr int kViewOrtho = kPanLeftAction + 1;
// (UX2: the transform tools - level_editor_transform.h; the arrows nudge with wasd_flies=0)
constexpr int kToolMove = kViewOrtho + 1;
constexpr int kToolRotate = kToolMove + 1;
constexpr int kToolScale = kToolRotate + 1;
constexpr int kToolCycle = kToolScale + 1;
constexpr int kToolSpace = kToolCycle + 1;
constexpr int kModalTransform = kToolSpace + 1;
constexpr int kRepeatDuplicate = kModalTransform + 1;
constexpr int kNudgeForward = kRepeatDuplicate + 1;
constexpr int kNudgeBack = kNudgeForward + 1;
constexpr int kNudgeLeft = kNudgeBack + 1;
constexpr int kNudgeRight = kNudgeLeft + 1;
// (UX4 / UX5: the view modes, jump arcs, the ruler - level_editor_overlays.h, level_editor_arcs.h,
// level_editor_ruler.h; the Problems window - level_editor_checks.h)
constexpr int kViewModeLit = kNudgeRight + 1;
constexpr int kViewModeUnlit = kViewModeLit + 1;
constexpr int kViewModeWireframe = kViewModeUnlit + 1;
constexpr int kViewModeCollision = kViewModeWireframe + 1;
constexpr int kJumpArcs = kViewModeCollision + 1;
constexpr int kRuler = kJumpArcs + 1;
constexpr int kRulerClear = kRuler + 1;
constexpr int kProblems = kRulerClear + 1;
// (UX6: Simulate and the Trail window - level_editor_playtest.h)
constexpr int kSimulate = kProblems + 1;
constexpr int kTrailWindow = kSimulate + 1;
constexpr int kBuiltinCount = kTrailWindow + 1;

// The registry, built-ins first (made on first use; their keys read from the ini once - load_keys).
const std::vector<ActStep>& get_all();
ActStep* get_pull(int index);
int get_find(const std::string& id);
// An action added (or, its id already there, its name / tip / callbacks replaced - its keys kept). Its index.
int enrol(const ActStep& action);
// Callbacks set on a built-in (or any) action.
void set_run(int index, std::function<void()> run);
void set_enabled(int index, std::function<bool()> enabled);
void set_checked(int index, std::function<int()> checked);
bool is_switched_on(int index);
int get_checked(int index);
// Its keys as text ("Ctrl+Z", "Ctrl+Y / Ctrl+Shift+Z", an Info's keysText; "" none).
std::string get_keys_text(int index);
// The other actions bound to the same chord as one of `index`'s keys (the Keys section's warning).
std::vector<int> conflicts(int index);

// --- the keymap, a frame at a time (the editor's Update) ------------------------------------------------------------

// The keyboard this frame (SDL_GetKeyboardState; all up while the UI types): which actions' keys were pressed.
// `suspended`: none fire (a key being captured for rebinding).
void begin_frame(platform::KeyState keys);
// The editor starting: the keys down now count as already down (the key that opened it doesn't act at once).
void seed_keys(platform::KeyState keys);
bool squeezed(int index);
bool get_held(platform::KeyState keys, int index);
// As if its key had been pressed: Pressed is true next frame (the command search, menus).
void fire_off(int index);
// The search / a menu: `run`, else Trigger (nothing when not Enabled).
void get_run(int index);

// Rebinding: the keys set (written to the ini - the defaults' entry taken out), put back to the defaults, or read
// (all, once - All() does it).
void set_keys(int index, const std::vector<Chord>& keys);
void reset_keys(int index);
void load_keys();
// An action and which of its key slots is captured.
struct CaptureSlot {
    int index{};
    int slot{};
};

// The Keys section: the next key pressed (with its modifiers) becomes the action's key `slot` (0: the first; past the
// end: added); Escape cancels. While capturing no action fires (and Escape doesn't leave the editor).
void begin_capture(CaptureSlot capture_slot);
void cancel_capture();
bool get_capturing();
int capturing_index();

} // namespace frontend::level_editor::actions

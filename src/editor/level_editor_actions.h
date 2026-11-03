#pragma once

#include <cstdint>

#include "platform/keyboard.h"
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
int fuzzy_score(const std::string& query, const std::string& text);

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
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): indexes into tables and arrays
enum IdentNo : uint8_t {
    kHelp,
    kPalette,
    kBrowser,
    kPlace,
    kHistory,
    kChanges,
    kKeys,
    kSnapshots,
    kUndo,
    kRedo,
    kSave,
    kDuplicate,
    kRestoreAutosave,
    kDiscardAutosave,
    kDelete,
    kHome,
    kResetLevel,
    kDeselect,
    kLock,
    kCopy,
    kPaste,
    kCopyProps,
    kPasteTransform,
    kPasteFields,
    kBoxSelect,
    kSelectSimilar,
    kSelectFamily,
    kInvertSelection,
    kSelectAll,
    kMakeGroup,
    kTurnLeft,
    kTurnRight,
    kScaleDown,
    kScaleUp,
    kDrop,
    kForward,
    kBack,
    kLeft,
    kRight,
    kFlyUp,
    kFlyDown,
    kNudgeUp,
    kNudgeDown,
    kFaster,
    kSlower,
    kCollision,
    kFaces,
    kTriggers,
    kOcclusion,
    kHide,
    kIsolate,
    kShowAll,
    kCameraNode,
    kLogCamera,
    kChainInsert,
    kChainExtend,
    kChainLink,
    kChainUnlink,
    kChainFinish,
    // (UX1: play from here, the camera's tools, the preferences - level_editor_play.h, level_editor_camera.h,
    // level_editor_prefs.h)
    kPlay,
    kPreferences,
    kFrameSelection,
    kBookmark1,
    kBookmarkStore1 = kBookmark1 + 9,
    kViewTop = kBookmarkStore1 + 9,
    kViewBottom,
    kCamFrontView,
    kViewBack,
    kPanRightAction,
    kPanLeftAction,
    kViewOrtho,
    // (UX2: the transform tools - level_editor_transform.h; the arrows nudge with wasd_flies=0)
    kToolMove,
    kToolRotate,
    kToolScale,
    kToolCycle,
    kToolSpace,
    kModalTransform,
    kRepeatDuplicate,
    kNudgeForward,
    kNudgeBack,
    kNudgeLeft,
    kNudgeRight,
    // (UX4 / UX5: the view modes, jump arcs, the ruler - level_editor_overlays.h, level_editor_arcs.h,
    // level_editor_ruler.h; the Problems window - level_editor_checks.h)
    kViewModeLit,
    kViewModeUnlit,
    kViewModeWireframe,
    kViewModeCollision,
    kJumpArcs,
    kRuler,
    kRulerClear,
    kProblems,
    // (UX6: Simulate and the Trail window - level_editor_playtest.h)
    kSimulate,
    kTrailWindow,
    kBuiltinCount
};

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

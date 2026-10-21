#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// NOT ORIGINAL - the level editor's actions and keymap (ours; see level_editor.h): every action the editor offers - a
// key press, a key held, a setting to switch, or a hint (a mouse gesture, a key of a mode) - in one registry, with its
// name, its category, its keys and, for the command search (Ctrl+Space / F3 - level_editor_ux3_ui.cpp), what running
// it does and whether it can run now. The editor's key handling reads its keys from here (Pressed / Held), so the keys
// can be rebound: the Keys section (a window of its own, or hosted by the preferences window - Ui::KeysSection) keeps
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
namespace Front::LevelEditor::Actions {

// --- the pure parts (self-tested: src/hook/selftests_editor_ux3.cpp) -------------------------------------------------

enum : uint8_t { kCtrl = 1, kShift = 2, kAlt = 4 };
// A key and its modifiers (an SDL scan code; 0: none).
struct Chord {
    int scancode = 0;
    uint8_t mods = 0;
};
inline bool operator==(Chord a, Chord b) { return a.scancode == b.scancode && a.mods == b.mods; }
// The key's name as the keymap writes it ("Z", "F1", "Space", "Delete", "[", "PageUp"); "" for a key it doesn't name.
const char* KeyName(int scancode);
// A key's scan code from its name (any case); 0 if none.
int KeyFromName(const std::string& name);
// "Ctrl+Shift+Z" and back (modifiers in any order, any case; "Control" and "Option" too). False: no key, an unknown key,
// a modifier alone.
std::string FormatChord(Chord chord);
bool ParseChord(const std::string& text, Chord* chord);
// A list: "Ctrl+Y, Ctrl+Shift+Z" / "none" (the empty list) and back (false: any chord unreadable).
std::string FormatKeys(const std::vector<Chord>& keys);
bool ParseKeys(const std::string& text, std::vector<Chord>* keys);

// Whether a chord matches a key pressed with `held` modifiers (see the top), and how specific it is.
bool ChordMatches(Chord chord, int scancode, uint8_t held);
int Specificity(Chord chord);
// The actions a key press fires: of `bound` (each action's keys), the indices of those matching that are most specific
// (several when several are bound alike - a conflict).
std::vector<int> Resolve(const std::vector<std::vector<Chord>>& bound, int scancode, uint8_t held);
// A key held counts for a chord: the key down, Ctrl as bound (held only when bound with it), Shift / Alt held when bound.
bool HoldMatches(Chord chord, bool keyDown, uint8_t held);
// Whether two chords can fire on the same press (the same key, Ctrl alike, and neither needs a Shift / Alt the other's
// press would lack - a press of the more specific fires only it): the Keys section's conflicts are exact duplicates.
bool SameBinding(Chord a, Chord b);

// The command search's fuzzy match: 0 when `query`'s characters (spaces ignored, any case) don't appear in `text` in
// order; else a score, higher for a match at the start, at the start of words, and for runs of consecutive characters.
// An empty query scores 1.
int FuzzyScore(const std::string& query, const std::string& text);

// --- the registry ------------------------------------------------------------------------------------------------

enum class Kind : uint8_t {
    Press,  // a key press does it
    Hold,   // while its key is held (flying, nudging)
    Toggle, // a setting switched (checked: on / off)
    Info,   // a hint: a mouse gesture, a mode's own key - listed, not rebindable
};

struct Action {
    std::string id;       // the ini key ("undo")
    std::string name;     // "Undo"
    std::string category; // "Edit"
    std::string tip;      // what it does, more fully (the search's second line, the help)
    Kind kind = Kind::Press;
    std::vector<Chord> defaults, keys;
    std::string keysText; // Info: what to show for its keys ("Right mouse", "Shift while dragging")
    bool rebindable = true;
    std::function<void()> run;     // the search runs it (none: Trigger)
    std::function<bool()> enabled; // can it run now (none: yes)
    std::function<int()> checked;  // Toggle: 1 on, 0 off (none: -1)
};

// The editor's own actions, in the built-in table's order (an action's index is its Id).
enum Id : int {
    kHelp, kPalette, kBrowser, kPlace, kHistory, kChanges, kKeys, kSnapshots,
    kUndo, kRedo, kSave, kDuplicate, kRestoreAutosave, kDiscardAutosave, kDelete, kHome, kResetLevel, kDeselect,
    kLock, kCopy, kPaste, kCopyProps, kPasteTransform, kPasteFields,
    kBoxSelect, kSelectSimilar, kSelectFamily, kInvertSelection, kSelectAll, kMakeGroup,
    kTurnLeft, kTurnRight, kScaleDown, kScaleUp, kDrop,
    kForward, kBack, kLeft, kRight, kFlyUp, kFlyDown, kNudgeUp, kNudgeDown, kFaster, kSlower,
    kCollision, kFaces, kTriggers, kOcclusion, kHide, kIsolate, kShowAll, kCameraNode, kLogCamera,
    kChainInsert, kChainExtend, kChainLink, kChainUnlink, kChainFinish,
    // (UX1: play from here, the camera's tools, the preferences - level_editor_play.h, level_editor_camera.h,
    // level_editor_prefs.h)
    kPlay, kPreferences, kFrameSelection,
    kBookmark1, kBookmarkStore1 = kBookmark1 + 9,
    kViewTop = kBookmarkStore1 + 9, kViewBottom, kViewFront, kViewBack, kViewRight, kViewLeft, kViewOrtho,
    kBuiltinCount
};

// The registry, built-ins first (made on first use; their keys read from the ini once - LoadKeys).
const std::vector<Action>& All();
Action* Get(int index);
int Find(const std::string& id);
// An action added (or, its id already there, its name / tip / callbacks replaced - its keys kept). Its index.
int Register(const Action& action);
// Callbacks set on a built-in (or any) action.
void SetRun(int index, std::function<void()> run);
void SetEnabled(int index, std::function<bool()> enabled);
void SetChecked(int index, std::function<int()> checked);
bool Enabled(int index);
int Checked(int index);
// Its keys as text ("Ctrl+Z", "Ctrl+Y / Ctrl+Shift+Z", an Info's keysText; "" none).
std::string KeysText(int index);
// The other actions bound to the same chord as one of `index`'s keys (the Keys section's warning).
std::vector<int> Conflicts(int index);

// --- the keymap, a frame at a time (the editor's Update) ------------------------------------------------------------

// The keyboard this frame (SDL_GetKeyboardState; all up while the UI types): which actions' keys were pressed.
// `suspended`: none fire (a key being captured for rebinding).
void BeginFrame(const uint8_t* keys);
// The editor starting: the keys down now count as already down (the key that opened it doesn't act at once).
void SeedKeys(const uint8_t* keys);
bool Pressed(int index);
bool Held(const uint8_t* keys, int index);
// As if its key had been pressed: Pressed is true next frame (the command search, menus).
void Trigger(int index);
// The search / a menu: `run`, else Trigger (nothing when not Enabled).
void Run(int index);

// Rebinding: the keys set (written to the ini - the defaults' entry taken out), put back to the defaults, or read
// (all, once - All() does it).
void SetKeys(int index, const std::vector<Chord>& keys);
void ResetKeys(int index);
void LoadKeys();
// The Keys section: the next key pressed (with its modifiers) becomes the action's key `slot` (0: the first; past the
// end: added); Escape cancels. While capturing no action fires (and Escape doesn't leave the editor).
void BeginCapture(int index, int slot);
void CancelCapture();
bool Capturing();
int CapturingIndex();

} // namespace Front::LevelEditor::Actions

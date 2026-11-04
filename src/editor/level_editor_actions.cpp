// NOT ORIGINAL - the level editor's actions and keymap (ours). See level_editor_actions.h.

#include "editor/level_editor_actions.h"
#include "platform/keyboard.h"
#include "base/core/span_copy.h"

#include <SDL2/SDL_scancode.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <array>
#include <vector>

#include "platform/run_log.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include <string_view>

namespace frontend::level_editor::actions {

namespace {

constexpr int kWordStartBonus = 8;    // search score: a matched letter that starts a word
constexpr int kFirstLetterBonus = 6;  // search score: a matched first letter of the text
constexpr int kPrefixBonus = 20;      // search score: the text starts with the query
constexpr int kSubstringBonus = 10;   // search score: the text contains the query
constexpr int kBookmarkCount = 9;     // bookmarks 1..9
constexpr std::size_t kBookmarkWordLength = 9; // "bookmark " (the tip's placeholder n follows)
constexpr std::size_t kIniValueSize = 160; // a key binding's text read from the ini

} // namespace

// --- the pure parts --------------------------------------------------------------------------------------------------

namespace {

struct KeyEntry {
    int scancode;
    const char* name;
};
// The keys the keymap names (SDL scan codes - the positions on a US layout, as the editor reads them).
constexpr auto kKeyNames = std::to_array<KeyEntry>({
    {.scancode = SDL_SCANCODE_A, .name = "A"},
    {.scancode = SDL_SCANCODE_B, .name = "B"},
    {.scancode = SDL_SCANCODE_C, .name = "C"},
    {.scancode = SDL_SCANCODE_D, .name = "D"},
    {.scancode = SDL_SCANCODE_E, .name = "E"},
    {.scancode = SDL_SCANCODE_F, .name = "F"},
    {.scancode = SDL_SCANCODE_G, .name = "G"},
    {.scancode = SDL_SCANCODE_H, .name = "H"},
    {.scancode = SDL_SCANCODE_I, .name = "I"},
    {.scancode = SDL_SCANCODE_J, .name = "J"},
    {.scancode = SDL_SCANCODE_K, .name = "K"},
    {.scancode = SDL_SCANCODE_L, .name = "L"},
    {.scancode = SDL_SCANCODE_M, .name = "M"},
    {.scancode = SDL_SCANCODE_N, .name = "N"},
    {.scancode = SDL_SCANCODE_O, .name = "O"},
    {.scancode = SDL_SCANCODE_P, .name = "P"},
    {.scancode = SDL_SCANCODE_Q, .name = "Q"},
    {.scancode = SDL_SCANCODE_R, .name = "R"},
    {.scancode = SDL_SCANCODE_S, .name = "S"},
    {.scancode = SDL_SCANCODE_T, .name = "T"},
    {.scancode = SDL_SCANCODE_U, .name = "U"},
    {.scancode = SDL_SCANCODE_V, .name = "V"},
    {.scancode = SDL_SCANCODE_W, .name = "W"},
    {.scancode = SDL_SCANCODE_X, .name = "X"},
    {.scancode = SDL_SCANCODE_Y, .name = "Y"},
    {.scancode = SDL_SCANCODE_Z, .name = "Z"},
    {.scancode = SDL_SCANCODE_1, .name = "1"},
    {.scancode = SDL_SCANCODE_2, .name = "2"},
    {.scancode = SDL_SCANCODE_3, .name = "3"},
    {.scancode = SDL_SCANCODE_4, .name = "4"},
    {.scancode = SDL_SCANCODE_5, .name = "5"},
    {.scancode = SDL_SCANCODE_6, .name = "6"},
    {.scancode = SDL_SCANCODE_7, .name = "7"},
    {.scancode = SDL_SCANCODE_8, .name = "8"},
    {.scancode = SDL_SCANCODE_9, .name = "9"},
    {.scancode = SDL_SCANCODE_0, .name = "0"},
    {.scancode = SDL_SCANCODE_RETURN, .name = "Enter"},
    {.scancode = SDL_SCANCODE_ESCAPE, .name = "Escape"},
    {.scancode = SDL_SCANCODE_BACKSPACE, .name = "Backspace"},
    {.scancode = SDL_SCANCODE_TAB, .name = "Tab"},
    {.scancode = SDL_SCANCODE_SPACE, .name = "Space"},
    {.scancode = SDL_SCANCODE_MINUS, .name = "-"},
    {.scancode = SDL_SCANCODE_EQUALS, .name = "="},
    {.scancode = SDL_SCANCODE_LEFTBRACKET, .name = "["},
    {.scancode = SDL_SCANCODE_RIGHTBRACKET, .name = "]"},
    {.scancode = SDL_SCANCODE_BACKSLASH, .name = "\\"},
    {.scancode = SDL_SCANCODE_SEMICOLON, .name = ";"},
    {.scancode = SDL_SCANCODE_APOSTROPHE, .name = "'"},
    {.scancode = SDL_SCANCODE_GRAVE, .name = "`"},
    {.scancode = SDL_SCANCODE_COMMA, .name = ","},
    {.scancode = SDL_SCANCODE_PERIOD, .name = "."},
    {.scancode = SDL_SCANCODE_SLASH, .name = "/"},
    {.scancode = SDL_SCANCODE_F1, .name = "F1"},
    {.scancode = SDL_SCANCODE_F2, .name = "F2"},
    {.scancode = SDL_SCANCODE_F3, .name = "F3"},
    {.scancode = SDL_SCANCODE_F4, .name = "F4"},
    {.scancode = SDL_SCANCODE_F5, .name = "F5"},
    {.scancode = SDL_SCANCODE_F6, .name = "F6"},
    {.scancode = SDL_SCANCODE_F7, .name = "F7"},
    {.scancode = SDL_SCANCODE_F8, .name = "F8"},
    {.scancode = SDL_SCANCODE_F9, .name = "F9"},
    {.scancode = SDL_SCANCODE_F10, .name = "F10"},
    {.scancode = SDL_SCANCODE_F11, .name = "F11"},
    {.scancode = SDL_SCANCODE_F12, .name = "F12"},
    {.scancode = SDL_SCANCODE_INSERT, .name = "Insert"},
    {.scancode = SDL_SCANCODE_HOME, .name = "Home"},
    {.scancode = SDL_SCANCODE_PAGEUP, .name = "PageUp"},
    {.scancode = SDL_SCANCODE_DELETE, .name = "Delete"},
    {.scancode = SDL_SCANCODE_END, .name = "End"},
    {.scancode = SDL_SCANCODE_PAGEDOWN, .name = "PageDown"},
    {.scancode = SDL_SCANCODE_RIGHT, .name = "Right"},
    {.scancode = SDL_SCANCODE_LEFT, .name = "Left"},
    {.scancode = SDL_SCANCODE_DOWN, .name = "Down"},
    {.scancode = SDL_SCANCODE_UP, .name = "Up"},
    {.scancode = SDL_SCANCODE_KP_ENTER, .name = "KeypadEnter"},
    {.scancode = SDL_SCANCODE_KP_PLUS, .name = "Keypad+"},
    {.scancode = SDL_SCANCODE_KP_MINUS, .name = "Keypad-"},
    {.scancode = SDL_SCANCODE_KP_0, .name = "Keypad0"},
    {.scancode = SDL_SCANCODE_KP_1, .name = "Keypad1"},
    {.scancode = SDL_SCANCODE_KP_2, .name = "Keypad2"},
    {.scancode = SDL_SCANCODE_KP_3, .name = "Keypad3"},
    {.scancode = SDL_SCANCODE_KP_4, .name = "Keypad4"},
    {.scancode = SDL_SCANCODE_KP_5, .name = "Keypad5"},
    {.scancode = SDL_SCANCODE_KP_6, .name = "Keypad6"},
    {.scancode = SDL_SCANCODE_KP_7, .name = "Keypad7"},
    {.scancode = SDL_SCANCODE_KP_8, .name = "Keypad8"},
    {.scancode = SDL_SCANCODE_KP_9, .name = "Keypad9"},
});

std::string get_lower(std::string text)
{
    for (char& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string get_trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

} // namespace

const char* get_key_name(int scancode)
{
    for (const KeyEntry& key_entry : kKeyNames) {
        if (key_entry.scancode == scancode) {
            return key_entry.name;
        }
    }
    return "";
}

int key_from_name(const std::string& name)
{
    std::string wanted = get_lower(get_trim(name));
    if (wanted.empty()) {
        return 0;
    }
    // (a few other spellings)
    if (wanted == "return") {
        wanted = "enter";
    } else if (wanted == "esc") {
        wanted = "escape";
    } else if (wanted == "del") {
        wanted = "delete";
    } else if (wanted == "ins") {
        wanted = "insert";
    } else if (wanted == "pgup" || wanted == "page up") {
        wanted = "pageup";
    } else if (wanted == "pgdn" || wanted == "page down") {
        wanted = "pagedown";
    }
    for (const KeyEntry& key_entry : kKeyNames) {
        if (get_lower(key_entry.name) == wanted) {
            return key_entry.scancode;
        }
    }
    return 0;
}

std::string format_chord(Chord chord)
{
    const char* key = get_key_name(chord.scancode);
    if (*key == '\0') {
        return "";
    }
    std::string text;
    if ((chord.mods & kCtrl) != 0) {
        text += "Ctrl+";
    }
    if ((chord.mods & kShift) != 0) {
        text += "Shift+";
    }
    if ((chord.mods & kAlt) != 0) {
        text += "Alt+";
    }
    return text + key;
}

bool parse_chord(const std::string& text, Chord* chord)
{
    std::string rest = get_trim(text);
    Chord parsed_chord;
    // Modifiers: words before a '+' (the key itself may be "+"-less punctuation; "Keypad+" ends in one).
    for (;;) {
        const size_t plus = rest.find('+');
        if (plus == std::string::npos || plus == 0 || plus + 1 >= rest.size()) {
            break;
        }
        const std::string word = get_lower(get_trim(rest.substr(0, plus)));
        uint8_t bit = 0;
        if (word == "ctrl" || word == "control") {
            bit = kCtrl;
        } else if (word == "shift") {
            bit = kShift;
        } else if (word == "alt" || word == "option") {
            bit = kAlt;
        }
        if (bit == 0U) {
            break;
        }
        parsed_chord.mods |= bit;
        rest = get_trim(rest.substr(plus + 1));
    }
    parsed_chord.scancode = key_from_name(rest);
    if (parsed_chord.scancode == 0) {
        return false;
    }
    *chord = parsed_chord;
    return true;
}

std::string format_keys(const std::vector<Chord>& keys)
{
    std::string text;
    for (const Chord& chord : keys) {
        const std::string one = format_chord(chord);
        if (one.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += ", ";
        }
        text += one;
    }
    return text.empty() ? "none" : text;
}

bool parse_keys(const std::string& text, std::vector<Chord>* keys)
{
    std::vector<Chord> out;
    std::string all = get_trim(text);
    if (get_lower(all) == "none" || all.empty()) {
        keys->clear();
        return true;
    }
    size_t start = 0;
    while (start <= all.size()) {
        size_t comma = all.find(',', start);
        // ("Ctrl+," would be a chord ending in a comma: a comma right after '+' belongs to the chord)
        while (comma != std::string::npos && comma > 0 && core::at(all, comma - 1) == '+') {
            comma = all.find(',', comma + 1);
        }
        const std::string part = all.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        Chord chord;
        if (!parse_chord(part, &chord)) {
            return false;
        }
        if (std::ranges::find(out, chord) == out.end()) {
            out.push_back(chord);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    *keys = out;
    return true;
}

bool chord_matches(Chord chord, KeyState key_state)
{
    const auto scancode = key_state.scancode;
    const auto held = key_state.held;
    if ((chord.scancode == 0) || chord.scancode != scancode) {
        return false;
    }
    if (((chord.mods & kCtrl) != 0) != ((held & kCtrl) != 0)) {
        return false;
    }
    return (held & chord.mods) == chord.mods;
}

int get_specificity(Chord chord)
{
    int count = 0;
    for (const uint8_t bit : {kCtrl, kShift, kAlt}) {
        count += ((chord.mods & bit) != 0) ? 1 : 0;
    }
    return count;
}

std::vector<int> settle_up(const std::vector<std::vector<Chord>>& bound, int scancode, uint8_t held)
{
    std::vector<int> best;
    int best_specificity = -1;
    for (int i = 0; std::cmp_less(i, bound.size()); ++i) {
        int specificity = -1;
        for (const Chord& chord : core::at(bound, i)) {
            if (chord_matches(chord, {.scancode = scancode, .held = held})) {
                specificity = std::max(specificity, get_specificity(chord));
            }
        }
        if (specificity < 0) {
            continue;
        }
        if (specificity > best_specificity) {
            best.clear();
            best_specificity = specificity;
        }
        if (specificity == best_specificity) {
            best.push_back(i);
        }
    }
    return best;
}

bool hold_matches(Chord chord, bool key_down, uint8_t held)
{
    if ((chord.scancode == 0) || !key_down) {
        return false;
    }
    if (((chord.mods & kCtrl) != 0) != ((held & kCtrl) != 0)) {
        return false;
    }
    return (held & chord.mods) == chord.mods;
}

bool same_binding(Chord a, Chord b) { return (a.scancode != 0) && a == b; }

int fuzzy_score(const std::string& query, std::string_view text)
{
    std::string lowered;
    for (const char character : query) {
        if (character != ' ') {
            lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
    }
    if (lowered.empty()) {
        return 1;
    }
    std::string lowercase = get_lower(std::string(text));
    int score = 0;
    size_t qi = 0;
    int run = 0;
    for (size_t ti = 0; ti < lowercase.size() && qi < lowered.size(); ++ti) {
        if (core::at(lowercase, ti) != core::at(lowered, qi)) {
            run = 0;
            continue;
        }
        const bool word_start = ti == 0 || (std::isalnum(static_cast<unsigned char>(core::at(lowercase, ti - 1))) == 0);
        score += 1 + (word_start ? kWordStartBonus : 0) + (run * 4) + (ti == 0 ? kFirstLetterBonus : 0);
        ++run;
        ++qi;
    }
    if (qi < lowered.size()) {
        return 0;
    }
    // (a whole-word or prefix match of the query ranks above scattered letters)
    if (lowercase.starts_with(lowered)) {
        score += kPrefixBonus;
    } else if (lowercase.find(lowered) != std::string::npos) {
        score += kSubstringBonus;
    }
    return score + 1;
}

// --- the registry ----------------------------------------------------------------------------------------------------

namespace {

constexpr int kA = SDL_SCANCODE_A; // (letters: A + n)
Chord get_k(int scancode, uint8_t mods = 0) { return {.scancode = scancode, .mods = mods}; }
Chord get_l(char letter, uint8_t mods = 0) { return {.scancode = kA + (letter - 'A'), .mods = mods}; }

ActStep get_make(const char* id, const char* name, const char* category, Kind kind, const std::vector<Chord>& keys,
                 const char* tip)
{
    ActStep act_step;
    act_step.id = id;
    act_step.name = name;
    act_step.category = category;
    act_step.kind = kind;
    act_step.defaults = keys;
    act_step.keys = keys;
    act_step.tip = tip;
    return act_step;
}

// The built-ins, in Id order.
std::vector<ActStep> builtins()
{
    std::vector<ActStep> steps = {
        get_make("help", "Keyboard Shortcuts", "Panels", Kind::kPress, {get_k(SDL_SCANCODE_F1)},
                 "Show all keys and mouse gestures"),
        get_make("command_search", "Command Search", "Panels", Kind::kPress,
                 {get_k(SDL_SCANCODE_SPACE, kCtrl), get_k(SDL_SCANCODE_F3)}, "Find and run any command by name"),
        get_make("browser", "Add Piece", "Panels", Kind::kPress, {get_l('B')},
                 "Browse the level's scenery and objects to place copies"),
        get_make("place", "Place Node", "Panels", Kind::kPress, {get_l('N')},
                 "Add spawns, crowns, flags, lights, sounds, rails and paths"),
        get_make("history", "Undo History", "Panels", Kind::kPress, {get_l('H', kCtrl)},
                 "List of undo steps; click one to jump to it"),
        get_make("changes", "Changes", "Panels", Kind::kPress, {get_l('H', kCtrl | kShift)},
                 "All changes from the original level"),
        get_make("keys", "Keymap", "Panels", Kind::kPress, {}, "Rebind editor keys"),
        get_make("snapshots", "Snapshots", "Panels", Kind::kPress, {}, "Save and restore named versions of the layout"),

        get_make("undo", "Undo", "Edit", Kind::kPress, {get_l('Z', kCtrl)}, "Undo the last change"),
        get_make("redo", "Redo", "Edit", Kind::kPress, {get_l('Y', kCtrl), get_l('Z', kCtrl | kShift)},
                 "Redo the last undone change"),
        get_make("save", "Save Layout", "Edit", Kind::kPress, {get_l('S', kCtrl)}, "Save the edited level's layout"),
        get_make("duplicate", "Duplicate", "Edit", Kind::kPress, {get_l('D', kCtrl)}, "Duplicate the selection"),
        get_make("restore_autosave", "Restore Autosave", "Edit", Kind::kPress, {get_l('R', kCtrl)},
                 "Restore unsaved changes from a previous session"),
        get_make("discard_autosave", "Discard Autosave", "Edit", Kind::kPress, {get_l('R', kCtrl | kShift)},
                 "Delete unsaved changes from a previous session"),
        get_make("delete", "Delete", "Edit", Kind::kPress, {get_k(SDL_SCANCODE_DELETE)},
                 "Delete added items; hide original ones"),
        get_make("home", "Revert to Original", "Edit", Kind::kPress, {get_k(SDL_SCANCODE_HOME)},
                 "Restore the selection's original state"),
        get_make("reset_level", "Reset Level...", "Edit", Kind::kPress, {get_k(SDL_SCANCODE_DELETE, kCtrl | kShift)},
                 "Revert all changes and delete the saved layout"),
        get_make("deselect", "Deselect", "Select", Kind::kPress, {get_k(SDL_SCANCODE_BACKSPACE)},
                 "Clear the selection"),
        get_make("lock", "Lock / Unlock", "Select", Kind::kPress, {get_l('L')},
                 "Make the selection unselectable in the view"),
        get_make("copy", "Copy", "Edit", Kind::kPress, {get_l('C', kCtrl)}, "Copy the selection to the clipboard"),
        get_make("paste", "Paste", "Edit", Kind::kPress, {get_l('V', kCtrl)},
                 "Paste from the clipboard at the original position"),
        get_make("copy_props", "Copy Transform and Fields", "Inspector", Kind::kPress, {get_l('C', kCtrl | kShift)},
                 "Copy position, heading, scale and node fields"),
        get_make("paste_transform", "Paste Transform", "Inspector", Kind::kPress, {get_l('V', kCtrl | kShift)},
                 "Apply the copied position, heading and scale"),
        get_make("paste_fields", "Paste Fields", "Inspector", Kind::kPress, {},
                 "Apply the copied fields to the selected nodes"),

        get_make("box_select", "Box Select", "Select", Kind::kPress, {get_l('B', kCtrl)}, "Drag a selection box"),
        get_make("select_similar", "Select Similar...", "Select", Kind::kPress, {get_l('G', kShift)},
                 "Select items matching the selection"),
        get_make("select_family", "Select Family", "Select", Kind::kPress, {},
                 "Select all items of the selection's family"),
        get_make("invert_selection", "Invert Selection", "Select", Kind::kPress, {get_l('I', kCtrl)},
                 "Select everything of the same kind that isn't selected"),
        get_make("select_all", "Select All", "Select", Kind::kPress, {get_l('A', kCtrl)},
                 "Select all objects, or all scenery when scenery is selected"),
        get_make("make_group", "Group Selection", "Select", Kind::kPress, {get_l('G', kCtrl)},
                 "Create a named group in the Outliner"),

        get_make("turn_left", "Turn Left", "Transform", Kind::kPress, {get_l('Z')}, "Rotate left by the rotation snap"),
        get_make("turn_right", "Turn Right", "Transform", Kind::kPress, {get_l('X')},
                 "Rotate right by the rotation snap"),
        get_make("scale_down", "Scale Down", "Transform", Kind::kPress, {get_k(SDL_SCANCODE_LEFTBRACKET)},
                 "Scale down 10% (Shift: 1%)"),
        get_make("scale_up", "Scale Up", "Transform", Kind::kPress, {get_k(SDL_SCANCODE_RIGHTBRACKET)},
                 "Scale up 10% (Shift: 1%)"),
        get_make("drop", "Drop to Ground", "Transform", Kind::kPress, {get_l('G')},
                 "Move the selection down onto the ground"),

        get_make("forward", "Fly Forward", "Camera", Kind::kHold, {get_l('W')}, "Hold with the right mouse button"),
        get_make("back", "Fly Back", "Camera", Kind::kHold, {get_l('S')}, "Hold with the right mouse button"),
        get_make("left", "Fly Left", "Camera", Kind::kHold, {get_l('A')}, "Hold with the right mouse button"),
        get_make("right", "Fly Right", "Camera", Kind::kHold, {get_l('D')}, "Hold with the right mouse button"),
        get_make("fly_up", "Fly Up", "Camera", Kind::kHold, {get_l('E'), get_k(SDL_SCANCODE_SPACE)},
                 "Move the camera up"),
        get_make("fly_down", "Fly Down", "Camera", Kind::kHold, {get_l('Q')}, "Move the camera down"),
        get_make("nudge_up", "Nudge Up", "Transform", Kind::kHold, {get_k(SDL_SCANCODE_PAGEUP)},
                 "Move the selection up"),
        get_make("nudge_down", "Nudge Down", "Transform", Kind::kHold, {get_k(SDL_SCANCODE_PAGEDOWN)},
                 "Move the selection down"),
        get_make("faster", "Increase Fly Speed", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_EQUALS)},
                 "Fly 1.5x faster"),
        get_make("slower", "Decrease Fly Speed", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_MINUS)},
                 "Fly 1.5x slower"),

        get_make("collision", "Show Collision", "View", Kind::kPress, {get_l('C')},
                 "Show collision near the camera, coloured by surface"),
        get_make("faces", "Face Select Mode", "View", Kind::kPress, {get_l('K')},
                 "Select collision faces to edit their flags"),
        get_make("triggers", "Show Trigger Volumes", "View", Kind::kPress, {get_l('T')},
                 "Show trigger volumes and the Triggers window"),
        get_make("occlusion", "Occlusion Culling", "View", Kind::kPress, {get_l('O')},
                 "Cull hidden geometry as in play"),
        get_make("hide", "Hide Selection", "View", Kind::kPress, {get_l('H')}, "Hide in the editor only; not saved"),
        get_make("isolate", "Isolate Selection", "View", Kind::kPress, {get_l('I'), get_k(SDL_SCANCODE_SLASH)},
                 "Hide everything else in the editor"),
        get_make("show_all", "Show All", "View", Kind::kPress, {get_l('H', kAlt)},
                 "Show everything hidden in the editor"),
        get_make("camera_node", "Camera Node from View", "View", Kind::kPress, {get_l('V')},
                 "Add a camera node at the current view"),
        get_make("log_camera", "Log Camera Position", "View", Kind::kPress, {get_l('P')},
                 "Write the camera position to the log"),

        get_make("chain_insert", "Insert Point", "Chains", Kind::kPress, {get_k(SDL_SCANCODE_INSERT)},
                 "Insert a point halfway to the next"),
        get_make("chain_extend", "Extend from End", "Chains", Kind::kPress, {get_k(SDL_SCANCODE_END)},
                 "Continue drawing from an end point"),
        get_make("chain_link", "Link to Point", "Chains", Kind::kPress, {get_l('J')},
                 "Link this end to the next end clicked"),
        get_make("chain_unlink", "Unlink Point", "Chains", Kind::kPress, {get_l('U')},
                 "Unlink this point from the next"),
        get_make("chain_finish", "Finish Drawing", "Chains", Kind::kPress, {get_k(SDL_SCANCODE_RETURN)},
                 "Finish drawing the rail or path"),

        // (UX1 - level_editor_play.h, level_editor_prefs.h, level_editor_camera.h)
        get_make("play", "Play from Here", "Play", Kind::kPress, {get_k(SDL_SCANCODE_F5), get_l('P', kCtrl)},
                 "Skate from the camera position; press again to return"),
        get_make("preferences", "Preferences...", "Panels", Kind::kPress, {get_k(SDL_SCANCODE_COMMA, kCtrl)},
                 "Editor preferences and keymap"),
        get_make("frame_selection", "Frame Selection", "Camera", Kind::kPress, {get_l('F')},
                 "Fit the selection in the view"),
    };
    for (int i = 1; i <= kBookmarkCount; ++i) {
        const std::string number = std::to_string(i);
        const ActStep act_step =
            get_make(("bookmark_" + number).c_str(), ("Go to Bookmark " + number).c_str(), "Camera", Kind::kPress,
                     {get_k(SDL_SCANCODE_1 + i - 1)}, "Move the camera to bookmark n");
        steps.push_back(act_step);
    }
    for (int i = 1; i <= kBookmarkCount; ++i) {
        const std::string number = std::to_string(i);
        const ActStep act_step =
            get_make(("bookmark_store_" + number).c_str(), ("Set Bookmark " + number).c_str(), "Camera", Kind::kPress,
                     {get_k(SDL_SCANCODE_1 + i - 1, kCtrl)}, "Store the camera as bookmark n");
        steps.push_back(act_step);
    }
    for (ActStep& act_step : steps) {
        // (the bookmarks' tips name their own number)
        const size_t at = act_step.tip.find("bookmark n");
        if (at != std::string::npos) {
            act_step.tip.replace(at + kBookmarkWordLength, 1, act_step.id.substr(act_step.id.size() - 1));
        }
    }
    std::vector<ActStep> views = {
        get_make("view_top", "Top View", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_KP_7)},
                 "Orthographic view from above"),
        get_make("view_bottom", "Bottom View", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_KP_7, kCtrl)},
                 "Orthographic view from below"),
        get_make("view_front", "Front View", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_KP_1)},
                 "Orthographic view from the front"),
        get_make("view_back", "Back View", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_KP_1, kCtrl)},
                 "Orthographic view from the back"),
        get_make("view_right", "Right View", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_KP_3)},
                 "Orthographic view from the right"),
        get_make("view_left", "Left View", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_KP_3, kCtrl)},
                 "Orthographic view from the left"),
        get_make("view_ortho", "Toggle Perspective / Ortho", "Camera", Kind::kPress, {get_k(SDL_SCANCODE_KP_5)},
                 "Switch between perspective and orthographic"),
    };
    steps.insert(steps.end(), views.begin(), views.end());
    // (UX2 - level_editor_transform.h)
    std::vector<ActStep> tools = {
        get_make("tool_move", "Move Tool", "Transform", Kind::kPress, {get_l('W')},
                 "Move the selection with the gizmo"),
        get_make("tool_rotate", "Rotate Tool", "Transform", Kind::kPress, {get_l('E')},
                 "Rotate the selection with the gizmo"),
        get_make("tool_scale", "Scale Tool", "Transform", Kind::kPress, {get_l('R')},
                 "Scale the selection with the gizmo"),
        get_make("tool_cycle", "Next Tool", "Transform", Kind::kPress, {get_k(SDL_SCANCODE_SPACE)},
                 "Cycle Move, Rotate and Scale"),
        get_make("tool_space", "Toggle World / Local", "Transform", Kind::kPress, {get_k(SDL_SCANCODE_GRAVE)},
                 "Switch the gizmo between world and local axes"),
        get_make("modal_transform", "Modal Transform", "Transform", Kind::kPress, {get_l('G', kAlt)},
                 "Transform with the mouse released; X/Y/Z constrain, click or Enter applies"),
        get_make("repeat_duplicate", "Repeat Duplicate", "Edit", Kind::kPress, {get_l('D', kCtrl | kShift)},
                 "Duplicate again with the same offset"),
        get_make("nudge_forward", "Nudge Forward", "Transform", Kind::kHold, {get_k(SDL_SCANCODE_UP)},
                 "Move the selection forward"),
        get_make("nudge_back", "Nudge Back", "Transform", Kind::kHold, {get_k(SDL_SCANCODE_DOWN)},
                 "Move the selection back"),
        get_make("nudge_left", "Nudge Left", "Transform", Kind::kHold, {get_k(SDL_SCANCODE_LEFT)},
                 "Move the selection left"),
        get_make("nudge_right", "Nudge Right", "Transform", Kind::kHold, {get_k(SDL_SCANCODE_RIGHT)},
                 "Move the selection right"),
    };
    steps.insert(steps.end(), tools.begin(), tools.end());
    // (UX4 / UX5)
    std::vector<ActStep> views2 = {
        get_make("view_mode_lit", "Lit", "View", Kind::kPress, {get_k(SDL_SCANCODE_1, kAlt)}, "View mode: as in play"),
        get_make("view_mode_unlit", "Unlit", "View", Kind::kPress, {get_k(SDL_SCANCODE_2, kAlt)},
                 "View mode: textures without lighting"),
        get_make("view_mode_wireframe", "Wireframe", "View", Kind::kPress, {get_k(SDL_SCANCODE_3, kAlt)},
                 "View mode: edges only"),
        get_make("view_mode_collision", "Collision Only", "View", Kind::kPress, {get_k(SDL_SCANCODE_4, kAlt)},
                 "View mode: collision only"),
        get_make("jump_arcs", "Jump Arcs", "View", Kind::kPress, {get_l('Y')},
                 "Show launch arcs from the selected ramp"),
        get_make("ruler", "Ruler", "View", Kind::kPress, {get_l('M')}, "Measure between two points"),
        get_make("ruler_clear", "Clear Ruler", "View", Kind::kPress, {get_l('M', kShift)}, "Clear the measurement"),
        get_make("problems", "Problems", "Panels", Kind::kPress, {get_l('P', kAlt)}, "Show level problems and budget"),
        // (UX6 - level_editor_playtest.h)
        get_make("simulate", "Simulate", "Play", Kind::kPress, {get_k(SDL_SCANCODE_F6)},
                 "Run the world with the skater frozen; press again to stop"),
        get_make("trail_window", "Trails", "Panels", Kind::kPress, {get_k(SDL_SCANCODE_F7)},
                 "Show trails and gaps from test runs"),
    };
    steps.insert(steps.end(), views2.begin(), views2.end());
    // The hints (not rebindable): what the mouse and the modes' own keys do.
    struct Hint {
        const char *id, *name, *category, *keys, *tip;
    };
    static const auto kHints = std::to_array<Hint>({
        {.id = "hint_look", .name = "Look / Orbit", .category = "Camera", .keys = "Right mouse", .tip = "Orbit the selection, or look around"},
        {.id = "hint_wheel", .name = "Dolly", .category = "Camera", .keys = "Wheel", .tip = "Move forward or back; while dragging, push or pull"},
        {.id = "hint_fast", .name = "Fast Move", .category = "Camera", .keys = "Shift (held)", .tip = "Fly and nudge 4x faster"},
        {.id = "hint_click", .name = "Select", .category = "Select", .keys = "Click", .tip = "Select an item; click again to cycle through overlapping items"},
        {.id = "hint_shift_click", .name = "Add / Remove", .category = "Select", .keys = "Shift+click", .tip = "Toggle an item in the selection"},
        {.id = "hint_alt_click", .name = "Select Single Piece", .category = "Select", .keys = "Alt+click, middle click",
         .tip = "Select one scenery piece instead of its group"},
        {.id = "hint_ctrl_click", .name = "Remove Part", .category = "Select", .keys = "Ctrl+click", .tip = "Remove a piece from the scenery selection"},
        {.id = "hint_box", .name = "Box Select", .category = "Select", .keys = "Drag from empty space",
         .tip = "Drag right: inside; drag left: crossing. Shift adds, Ctrl removes"},
        {.id = "hint_drag", .name = "Move", .category = "Transform", .keys = "Drag", .tip = "Shift: vertical; Ctrl: snap to grid; Esc cancels"},
        {.id = "hint_arrow", .name = "Move Along Axis", .category = "Transform", .keys = "Drag an arrow", .tip = "Constrain to that axis"},
        {.id = "hint_carry", .name = "Don't Carry", .category = "Transform", .keys = "Alt while moving", .tip = "Leave attached objects in place"},
        {.id = "hint_escape", .name = "Cancel", .category = "Panels", .keys = "Esc", .tip = "Cancel a drag, close a window, deselect, then exit"},
        {.id = "hint_orbit", .name = "Orbit", .category = "Camera", .keys = "Alt+drag", .tip = "Orbit around the point under the cursor"},
        {.id = "hint_speed", .name = "Fly Speed", .category = "Camera", .keys = "Right mouse + wheel", .tip = "Change fly speed while looking"},
        {.id = "hint_fly", .name = "Fly", .category = "Camera", .keys = "Right mouse + W A S D Q E", .tip = "Fly while holding the right mouse button"},
        {.id = "hint_snaps", .name = "Snap While Dragging", .category = "Transform", .keys = "Ctrl / Ctrl+Shift / Alt+Shift / Ctrl+Alt",
         .tip = "Grid, world grid, surface, vertex"},
        {.id = "hint_typed", .name = "Numeric Input", .category = "Transform", .keys = "X / Y / Z, digits (dragging)",
         .tip = "Type an axis and amount while dragging; Enter applies"},
        {.id = "hint_browser", .name = "Add Piece Keys", .category = "Panels", .keys = "Up / Down, Left / Right, F, Enter",
         .tip = "Choose, switch tab, focus, place"},
        {.id = "hint_outliner", .name = "Outliner", .category = "Panels", .keys = "Click, double-click", .tip = "Select; double-click to focus"},
    });
    for (const Hint& hint : kHints) {
        ActStep act_step = get_make(hint.id, hint.name, hint.category, Kind::kDetailsOf, {}, hint.tip);
        act_step.keys_text_ = hint.keys;
        act_step.rebindable = false;
        steps.push_back(act_step);
    }
    return steps;
}

std::vector<ActStep>& get_registry()
{
    static std::vector<ActStep> registry = builtins();
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        load_keys();
    }
    return registry;
}

// NOT ORIGINAL grouping - level_editor_actions.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_actions_state()).
struct LevelEditorActionsState {
    // The keymap's frame: the keys down last frame, the actions fired this frame, those triggered for the next.
    std::array<uint8_t, SDL_NUM_SCANCODES> was_{};
    std::vector<bool> fired_;
    std::vector<bool> triggered_;
    int capture_ = -1;
    int capture_slot_ = 0;
};

LevelEditorActionsState& level_editor_actions_state()
{
    static core::NoDestroy<LevelEditorActionsState> state;
    return state.get();
}

std::string ini_path() { return game_folder_path("thug2-decomp.ini"); }

uint8_t held_mods(platform::KeyState keys)
{
    uint8_t held = 0;
    if ((core::at(keys, SDL_SCANCODE_LCTRL) != 0U) || (core::at(keys, SDL_SCANCODE_RCTRL) != 0U)) {
        held |= kCtrl;
    }
    if ((core::at(keys, SDL_SCANCODE_LSHIFT) != 0U) || (core::at(keys, SDL_SCANCODE_RSHIFT) != 0U)) {
        held |= kShift;
    }
    if ((core::at(keys, SDL_SCANCODE_LALT) != 0U) || (core::at(keys, SDL_SCANCODE_RALT) != 0U)) {
        held |= kAlt;
    }
    return held;
}

bool is_modifier(int scancode)
{
    return scancode == SDL_SCANCODE_LCTRL || scancode == SDL_SCANCODE_RCTRL || scancode == SDL_SCANCODE_LSHIFT ||
           scancode == SDL_SCANCODE_RSHIFT || scancode == SDL_SCANCODE_LALT || scancode == SDL_SCANCODE_RALT ||
           scancode == SDL_SCANCODE_LGUI || scancode == SDL_SCANCODE_RGUI;
}

void save_keys(const ActStep& a)
{
    const bool as_default = a.keys == a.defaults;
    WritePrivateProfileStringA("editor_keys", a.id.c_str(), as_default ? nullptr : format_keys(a.keys).c_str(),
                               ini_path().c_str());
}

} // namespace

const std::vector<ActStep>& get_all() { return get_registry(); }

ActStep* get_pull(int index)
{
    std::vector<ActStep>& registry = get_registry();
    return index >= 0 && std::cmp_less(index, registry.size()) ? &core::at(registry, index) : nullptr;
}

int get_find(const std::string& id)
{
    const std::vector<ActStep>& registry = get_registry();
    for (int i = 0; std::cmp_less(i, registry.size()); ++i) {
        if (core::at(registry, i).id == id) {
            return i;
        }
    }
    return -1;
}

int enrol(const ActStep& action)
{
    std::vector<ActStep>& registry = get_registry();
    const int at = get_find(action.id);
    if (at >= 0) {
        const std::vector<Chord> keys = core::at(registry, at).keys;
        core::at(registry, at) = action;
        core::at(registry, at).keys = keys;
        return at;
    }
    registry.push_back(action);
    // (its keys from the ini, as the built-ins')
    std::array<char, kIniValueSize> text = {""};
    GetPrivateProfileStringA("editor_keys", action.id.c_str(), "", text.data(), sizeof(text), ini_path().c_str());
    std::vector<Chord> keys;
    if ((core::at(text, 0) != '\0') && parse_keys(text.data(), &keys)) {
        registry.back().keys = keys;
    }
    return static_cast<int>(registry.size()) - 1;
}

void set_run(int index, std::function<void()> run)
{
    if (ActStep* act_step = get_pull(index)) {
        act_step->run = std::move(run);
    }
}
void set_enabled(int index, std::function<bool()> enabled)
{
    if (ActStep* act_step = get_pull(index)) {
        act_step->enabled = std::move(enabled);
    }
}
void set_checked(int index, std::function<int()> checked)
{
    if (ActStep* act_step = get_pull(index)) {
        act_step->checked = std::move(checked);
    }
}

bool is_switched_on(int index)
{
    const ActStep* act_step = get_pull(index);
    return (act_step != nullptr) && act_step->kind != Kind::kDetailsOf && (!act_step->enabled || act_step->enabled());
}

int get_checked(int index)
{
    const ActStep* act_step = get_pull(index);
    return act_step && act_step->checked ? act_step->checked() : -1;
}

std::string get_keys_text(int index)
{
    const ActStep* act_step = get_pull(index);
    if (!act_step) {
        return "";
    }
    if (act_step->kind == Kind::kDetailsOf) {
        return act_step->keys_text_;
    }
    std::string text;
    for (const Chord& chord : act_step->keys) {
        const std::string one = format_chord(chord);
        if (one.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += " / ";
        }
        text += one;
    }
    return text;
}

std::vector<int> conflicts(int index)
{
    std::vector<int> out;
    const ActStep* act_step = get_pull(index);
    if (!act_step) {
        return out;
    }
    const std::vector<ActStep>& registry = get_registry();
    for (int i = 0; std::cmp_less(i, registry.size()); ++i) {
        if (i == index || core::at(registry, i).kind == Kind::kDetailsOf) {
            continue;
        }
        // (a key held and a key pressed may share a key: flying's E and nudging's E are one key in two modes)
        if ((core::at(registry, i).kind == Kind::kHold) != (act_step->kind == Kind::kHold)) {
            continue;
        }
        bool clash = false;
        for (const Chord& x : act_step->keys) {
            for (const Chord& y : core::at(registry, i).keys) {
                clash = clash || same_binding(x, y);
            }
        }
        if (clash) {
            out.push_back(i);
        }
    }
    return out;
}

void begin_frame(platform::KeyState keys)
{
    std::vector<ActStep>& registry = get_registry();
    level_editor_actions_state().fired_.assign(registry.size(), false);
    level_editor_actions_state().triggered_.resize(registry.size(), false);
    for (size_t i = 0; i < registry.size(); ++i) {
        if (core::at(level_editor_actions_state().triggered_, i)) {
            core::at(level_editor_actions_state().fired_, i) = true;
            core::at(level_editor_actions_state().triggered_, i) = false;
        }
    }
    const uint8_t held = held_mods(keys);
    std::vector<std::vector<Chord>> bound(registry.size());
    bool have_bound = false;
    for (int scancode = 0; scancode < SDL_NUM_SCANCODES; ++scancode) {
        const bool pressed =
            (core::at(keys, scancode) != 0U) && (core::at(level_editor_actions_state().was_, scancode) == 0U);
        core::at(level_editor_actions_state().was_, scancode) = core::at(keys, scancode);
        if (!pressed || is_modifier(scancode)) {
            continue;
        }
        if (level_editor_actions_state().capture_ >= 0) {
            // (the key being captured: Escape cancels)
            if (scancode != SDL_SCANCODE_ESCAPE) {
                if (const ActStep* act_step = get_pull(level_editor_actions_state().capture_)) {
                    std::vector<Chord> next = act_step->keys;
                    const Chord chord = {.scancode = scancode, .mods = held};
                    if (*get_key_name(scancode) == '\0') {
                        get_log("level editor: key %d can't be bound (not one the keymap names)", scancode);
                    } else {
                        if (std::cmp_less(level_editor_actions_state().capture_slot_, next.size())) {
                            core::at(next, level_editor_actions_state().capture_slot_) = chord;
                        } else {
                            next.push_back(chord);
                        }
                        set_keys(level_editor_actions_state().capture_, next);
                    }
                }
            }
            level_editor_actions_state().capture_ = -1;
            continue;
        }
        if (!have_bound) {
            for (size_t i = 0; i < registry.size(); ++i) {
                if (core::at(registry, i).kind == Kind::kPress || core::at(registry, i).kind == Kind::kFlipOver) {
                    core::at(bound, i) = core::at(registry, i).keys;
                }
            }
            have_bound = true;
        }
        for (const int index : settle_up(bound, scancode, held)) {
            core::at(level_editor_actions_state().fired_, index) = true;
        }
    }
}

void seed_keys(platform::KeyState keys)
{
    core::copy_into(keys.first(level_editor_actions_state().was_.size()), std::span(level_editor_actions_state().was_));
    level_editor_actions_state().triggered_.assign(level_editor_actions_state().triggered_.size(), false);
}

bool squeezed(int index) { return index >= 0 && std::cmp_less(index, level_editor_actions_state().fired_.size()) && core::at(level_editor_actions_state().fired_, index); }

bool get_held(platform::KeyState keys, int index)
{
    const ActStep* act_step = get_pull(index);
    if (!act_step || level_editor_actions_state().capture_ >= 0) {
        return false;
    }
    const uint8_t held = held_mods(keys);
    return std::ranges::any_of(act_step->keys, [&](const Chord& chord) {
        return chord.scancode > 0 && chord.scancode < SDL_NUM_SCANCODES &&
               hold_matches(chord, core::at(keys, chord.scancode) != 0, held);
    });
}

void fire_off(int index)
{
    const std::vector<ActStep>& registry = get_registry();
    if (index < 0 || std::cmp_greater_equal(index, registry.size())) {
        return;
    }
    level_editor_actions_state().triggered_.resize(registry.size(), false);
    core::at(level_editor_actions_state().triggered_, index) = true;
}

void get_run(int index)
{
    const ActStep* act_step = get_pull(index);
    if (!act_step || !is_switched_on(index)) {
        return;
    }
    if (act_step->run) {
        act_step->run();
    } else {
        fire_off(index);
    }
}

void set_keys(int index, const std::vector<Chord>& keys)
{
    ActStep* act_step = get_pull(index);
    if (!act_step || !act_step->rebindable) {
        return;
    }
    act_step->keys = keys;
    save_keys(*act_step);
    get_log("level editor: %s bound to %s", act_step->id.c_str(), format_keys(keys).c_str());
}

void reset_keys(int index)
{
    ActStep* act_step = get_pull(index);
    if (!act_step) {
        return;
    }
    act_step->keys = act_step->defaults;
    save_keys(*act_step);
}

void load_keys()
{
    std::vector<ActStep>& registry = get_registry();
    const std::string path = ini_path();
    for (ActStep& act_step : registry) {
        if (!act_step.rebindable) {
            continue;
        }
        std::array<char, kIniValueSize> text = {""};
        GetPrivateProfileStringA("editor_keys", act_step.id.c_str(), "", text.data(), sizeof(text), path.c_str());
        if (core::at(text, 0) == '\0') {
            continue;
        }
        std::vector<Chord> keys;
        if (parse_keys(text.data(), &keys)) {
            act_step.keys = keys;
        } else {
            get_log("level editor: [editor_keys] %s=%s isn't a key list - the default kept", act_step.id.c_str(),
                    text.data());
        }
    }
}

void begin_capture(CaptureSlot capture_slot)
{
    const auto index = capture_slot.index;
    const auto slot = capture_slot.slot;
    level_editor_actions_state().capture_ = get_pull(index) && get_pull(index)->rebindable ? index : -1;
    level_editor_actions_state().capture_slot_ = slot;
}
void cancel_capture() { level_editor_actions_state().capture_ = -1; }
bool get_capturing() { return level_editor_actions_state().capture_ >= 0; }
int capturing_index() { return level_editor_actions_state().capture_; }

} // namespace frontend::level_editor::actions

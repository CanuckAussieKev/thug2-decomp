// NOT ORIGINAL - the level editor's actions and keymap (ours). See level_editor_actions.h.

#include "editor/level_editor_actions.h"

#include <SDL2/SDL_scancode.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#include "platform/run_log.h"

namespace frontend::level_editor::actions {

// --- the pure parts --------------------------------------------------------------------------------------------------

namespace {

struct KeyEntry {
    int scancode;
    const char* name;
};
// The keys the keymap names (SDL scan codes - the positions on a US layout, as the editor reads them).
const KeyEntry kKeyNames[] = {
    {SDL_SCANCODE_A, "A"},
    {SDL_SCANCODE_B, "B"},
    {SDL_SCANCODE_C, "C"},
    {SDL_SCANCODE_D, "D"},
    {SDL_SCANCODE_E, "E"},
    {SDL_SCANCODE_F, "F"},
    {SDL_SCANCODE_G, "G"},
    {SDL_SCANCODE_H, "H"},
    {SDL_SCANCODE_I, "I"},
    {SDL_SCANCODE_J, "J"},
    {SDL_SCANCODE_K, "K"},
    {SDL_SCANCODE_L, "L"},
    {SDL_SCANCODE_M, "M"},
    {SDL_SCANCODE_N, "N"},
    {SDL_SCANCODE_O, "O"},
    {SDL_SCANCODE_P, "P"},
    {SDL_SCANCODE_Q, "Q"},
    {SDL_SCANCODE_R, "R"},
    {SDL_SCANCODE_S, "S"},
    {SDL_SCANCODE_T, "T"},
    {SDL_SCANCODE_U, "U"},
    {SDL_SCANCODE_V, "V"},
    {SDL_SCANCODE_W, "W"},
    {SDL_SCANCODE_X, "X"},
    {SDL_SCANCODE_Y, "Y"},
    {SDL_SCANCODE_Z, "Z"},
    {SDL_SCANCODE_1, "1"},
    {SDL_SCANCODE_2, "2"},
    {SDL_SCANCODE_3, "3"},
    {SDL_SCANCODE_4, "4"},
    {SDL_SCANCODE_5, "5"},
    {SDL_SCANCODE_6, "6"},
    {SDL_SCANCODE_7, "7"},
    {SDL_SCANCODE_8, "8"},
    {SDL_SCANCODE_9, "9"},
    {SDL_SCANCODE_0, "0"},
    {SDL_SCANCODE_RETURN, "Enter"},
    {SDL_SCANCODE_ESCAPE, "Escape"},
    {SDL_SCANCODE_BACKSPACE, "Backspace"},
    {SDL_SCANCODE_TAB, "Tab"},
    {SDL_SCANCODE_SPACE, "Space"},
    {SDL_SCANCODE_MINUS, "-"},
    {SDL_SCANCODE_EQUALS, "="},
    {SDL_SCANCODE_LEFTBRACKET, "["},
    {SDL_SCANCODE_RIGHTBRACKET, "]"},
    {SDL_SCANCODE_BACKSLASH, "\\"},
    {SDL_SCANCODE_SEMICOLON, ";"},
    {SDL_SCANCODE_APOSTROPHE, "'"},
    {SDL_SCANCODE_GRAVE, "`"},
    {SDL_SCANCODE_COMMA, ","},
    {SDL_SCANCODE_PERIOD, "."},
    {SDL_SCANCODE_SLASH, "/"},
    {SDL_SCANCODE_F1, "F1"},
    {SDL_SCANCODE_F2, "F2"},
    {SDL_SCANCODE_F3, "F3"},
    {SDL_SCANCODE_F4, "F4"},
    {SDL_SCANCODE_F5, "F5"},
    {SDL_SCANCODE_F6, "F6"},
    {SDL_SCANCODE_F7, "F7"},
    {SDL_SCANCODE_F8, "F8"},
    {SDL_SCANCODE_F9, "F9"},
    {SDL_SCANCODE_F10, "F10"},
    {SDL_SCANCODE_F11, "F11"},
    {SDL_SCANCODE_F12, "F12"},
    {SDL_SCANCODE_INSERT, "Insert"},
    {SDL_SCANCODE_HOME, "Home"},
    {SDL_SCANCODE_PAGEUP, "PageUp"},
    {SDL_SCANCODE_DELETE, "Delete"},
    {SDL_SCANCODE_END, "End"},
    {SDL_SCANCODE_PAGEDOWN, "PageDown"},
    {SDL_SCANCODE_RIGHT, "Right"},
    {SDL_SCANCODE_LEFT, "Left"},
    {SDL_SCANCODE_DOWN, "Down"},
    {SDL_SCANCODE_UP, "Up"},
    {SDL_SCANCODE_KP_ENTER, "KeypadEnter"},
    {SDL_SCANCODE_KP_PLUS, "Keypad+"},
    {SDL_SCANCODE_KP_MINUS, "Keypad-"},
    {SDL_SCANCODE_KP_0, "Keypad0"},
    {SDL_SCANCODE_KP_1, "Keypad1"},
    {SDL_SCANCODE_KP_2, "Keypad2"},
    {SDL_SCANCODE_KP_3, "Keypad3"},
    {SDL_SCANCODE_KP_4, "Keypad4"},
    {SDL_SCANCODE_KP_5, "Keypad5"},
    {SDL_SCANCODE_KP_6, "Keypad6"},
    {SDL_SCANCODE_KP_7, "Keypad7"},
    {SDL_SCANCODE_KP_8, "Keypad8"},
    {SDL_SCANCODE_KP_9, "Keypad9"},
};

std::string get_lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string get_trim(const std::string& text)
{
    const size_t a = text.find_first_not_of(" \t");
    if (a == std::string::npos)
        return "";
    const size_t b = text.find_last_not_of(" \t");
    return text.substr(a, b - a + 1);
}

} // namespace

const char* get_key_name(int scancode)
{
    for (const KeyEntry& k : kKeyNames) {
        if (k.scancode == scancode)
            return k.name;
    }
    return "";
}

int key_from_name(const std::string& name)
{
    std::string wanted = get_lower(get_trim(name));
    if (wanted.empty())
        return 0;
    // (a few other spellings)
    if (wanted == "return")
        wanted = "enter";
    else if (wanted == "esc")
        wanted = "escape";
    else if (wanted == "del")
        wanted = "delete";
    else if (wanted == "ins")
        wanted = "insert";
    else if (wanted == "pgup" || wanted == "page up")
        wanted = "pageup";
    else if (wanted == "pgdn" || wanted == "page down")
        wanted = "pagedown";
    for (const KeyEntry& k : kKeyNames) {
        if (get_lower(k.name) == wanted)
            return k.scancode;
    }
    return 0;
}

std::string format_chord(Chord chord)
{
    const char* key = get_key_name(chord.scancode);
    if (!*key)
        return "";
    std::string text;
    if (chord.mods & kCtrl)
        text += "Ctrl+";
    if (chord.mods & kShift)
        text += "Shift+";
    if (chord.mods & kAlt)
        text += "Alt+";
    return text + key;
}

bool parse_chord(const std::string& text, Chord* chord)
{
    std::string rest = get_trim(text);
    Chord c;
    // Modifiers: words before a '+' (the key itself may be "+"-less punctuation; "Keypad+" ends in one).
    for (;;) {
        const size_t plus = rest.find('+');
        if (plus == std::string::npos || plus == 0 || plus + 1 >= rest.size())
            break;
        const std::string word = get_lower(get_trim(rest.substr(0, plus)));
        const uint8_t bit = word == "ctrl" || word == "control" ? kCtrl
                            : word == "shift"                   ? kShift
                            : word == "alt" || word == "option" ? kAlt
                                                                : 0;
        if (!bit)
            break;
        c.mods |= bit;
        rest = get_trim(rest.substr(plus + 1));
    }
    c.scancode = key_from_name(rest);
    if (!c.scancode)
        return false;
    *chord = c;
    return true;
}

std::string format_keys(const std::vector<Chord>& keys)
{
    std::string text;
    for (const Chord& c : keys) {
        const std::string one = format_chord(c);
        if (one.empty())
            continue;
        if (!text.empty())
            text += ", ";
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
        while (comma != std::string::npos && comma > 0 && all[comma - 1] == '+')
            comma = all.find(',', comma + 1);
        const std::string part = all.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        Chord c;
        if (!parse_chord(part, &c))
            return false;
        if (std::find(out.begin(), out.end(), c) == out.end())
            out.push_back(c);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    *keys = out;
    return true;
}

bool chord_matches(Chord chord, int scancode, uint8_t held)
{
    if (!chord.scancode || chord.scancode != scancode)
        return false;
    if (((chord.mods & kCtrl) != 0) != ((held & kCtrl) != 0))
        return false;
    return (held & chord.mods) == chord.mods;
}

int get_specificity(Chord chord)
{
    int n = 0;
    for (const uint8_t bit : {kCtrl, kShift, kAlt})
        n += (chord.mods & bit) ? 1 : 0;
    return n;
}

std::vector<int> settle_up(const std::vector<std::vector<Chord>>& bound, int scancode, uint8_t held)
{
    std::vector<int> best;
    int best_specificity = -1;
    for (int i = 0; std::cmp_less(i, bound.size()); ++i) {
        int specificity = -1;
        for (const Chord& c : bound[i]) {
            if (chord_matches(c, scancode, held))
                specificity = std::max(specificity, get_specificity(c));
        }
        if (specificity < 0)
            continue;
        if (specificity > best_specificity) {
            best.clear();
            best_specificity = specificity;
        }
        if (specificity == best_specificity)
            best.push_back(i);
    }
    return best;
}

bool hold_matches(Chord chord, bool key_down, uint8_t held)
{
    if (!chord.scancode || !key_down)
        return false;
    if (((chord.mods & kCtrl) != 0) != ((held & kCtrl) != 0))
        return false;
    return (held & chord.mods) == chord.mods;
}

bool same_binding(Chord a, Chord b) { return a.scancode && a == b; }

int fuzzy_score(const std::string& query, const std::string& text)
{
    std::string q;
    for (const char c : query) {
        if (c != ' ')
            q += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (q.empty())
        return 1;
    std::string t = get_lower(text);
    int score = 0;
    size_t qi = 0;
    int run = 0;
    for (size_t ti = 0; ti < t.size() && qi < q.size(); ++ti) {
        if (t[ti] != q[qi]) {
            run = 0;
            continue;
        }
        const bool word_start = ti == 0 || !std::isalnum(static_cast<unsigned char>(t[ti - 1]));
        score += 1 + (word_start ? 8 : 0) + run * 4 + (ti == 0 ? 6 : 0);
        ++run;
        ++qi;
    }
    if (qi < q.size())
        return 0;
    // (a whole-word or prefix match of the query ranks above scattered letters)
    if (t.starts_with(q))
        score += 20;
    else if (t.find(q) != std::string::npos)
        score += 10;
    return score + 1;
}

// --- the registry ----------------------------------------------------------------------------------------------------

namespace {

constexpr int kA = SDL_SCANCODE_A; // (letters: A + n)
Chord get_k(int scancode, uint8_t mods = 0) { return {scancode, mods}; }
Chord get_l(char letter, uint8_t mods = 0) { return {kA + (letter - 'A'), mods}; }

ActStep get_make(const char* id, const char* name, const char* category, Kind kind, const std::vector<Chord>& keys,
                 const char* tip)
{
    ActStep a;
    a.id = id;
    a.name = name;
    a.category = category;
    a.kind = kind;
    a.defaults = keys;
    a.keys = keys;
    a.tip = tip;
    return a;
}

// The built-ins, in Id order.
std::vector<ActStep> builtins()
{
    std::vector<ActStep> b = {
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
    for (int i = 1; i <= 9; ++i) {
        const std::string n = std::to_string(i);
        const ActStep a = get_make(("bookmark_" + n).c_str(), ("Go to Bookmark " + n).c_str(), "Camera", Kind::kPress,
                                   {get_k(SDL_SCANCODE_1 + i - 1)}, "Move the camera to bookmark n");
        b.push_back(a);
    }
    for (int i = 1; i <= 9; ++i) {
        const std::string n = std::to_string(i);
        const ActStep a =
            get_make(("bookmark_store_" + n).c_str(), ("Set Bookmark " + n).c_str(), "Camera", Kind::kPress,
                     {get_k(SDL_SCANCODE_1 + i - 1, kCtrl)}, "Store the camera as bookmark n");
        b.push_back(a);
    }
    for (ActStep& a : b) {
        // (the bookmarks' tips name their own number)
        const size_t at = a.tip.find("bookmark n");
        if (at != std::string::npos)
            a.tip.replace(at + 9, 1, a.id.substr(a.id.size() - 1));
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
    b.insert(b.end(), views.begin(), views.end());
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
    b.insert(b.end(), tools.begin(), tools.end());
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
    b.insert(b.end(), views2.begin(), views2.end());
    // The hints (not rebindable): what the mouse and the modes' own keys do.
    struct Hint {
        const char *id, *name, *category, *keys, *tip;
    };
    static const Hint kHints[] = {
        {"hint_look", "Look / Orbit", "Camera", "Right mouse", "Orbit the selection, or look around"},
        {"hint_wheel", "Dolly", "Camera", "Wheel", "Move forward or back; while dragging, push or pull"},
        {"hint_fast", "Fast Move", "Camera", "Shift (held)", "Fly and nudge 4x faster"},
        {"hint_click", "Select", "Select", "Click", "Select an item; click again to cycle through overlapping items"},
        {"hint_shift_click", "Add / Remove", "Select", "Shift+click", "Toggle an item in the selection"},
        {"hint_alt_click", "Select Single Piece", "Select", "Alt+click, middle click",
         "Select one scenery piece instead of its group"},
        {"hint_ctrl_click", "Remove Part", "Select", "Ctrl+click", "Remove a piece from the scenery selection"},
        {"hint_box", "Box Select", "Select", "Drag from empty space",
         "Drag right: inside; drag left: crossing. Shift adds, Ctrl removes"},
        {"hint_drag", "Move", "Transform", "Drag", "Shift: vertical; Ctrl: snap to grid; Esc cancels"},
        {"hint_arrow", "Move Along Axis", "Transform", "Drag an arrow", "Constrain to that axis"},
        {"hint_carry", "Don't Carry", "Transform", "Alt while moving", "Leave attached objects in place"},
        {"hint_escape", "Cancel", "Panels", "Esc", "Cancel a drag, close a window, deselect, then exit"},
        {"hint_orbit", "Orbit", "Camera", "Alt+drag", "Orbit around the point under the cursor"},
        {"hint_speed", "Fly Speed", "Camera", "Right mouse + wheel", "Change fly speed while looking"},
        {"hint_fly", "Fly", "Camera", "Right mouse + W A S D Q E", "Fly while holding the right mouse button"},
        {"hint_snaps", "Snap While Dragging", "Transform", "Ctrl / Ctrl+Shift / Alt+Shift / Ctrl+Alt",
         "Grid, world grid, surface, vertex"},
        {"hint_typed", "Numeric Input", "Transform", "X / Y / Z, digits (dragging)",
         "Type an axis and amount while dragging; Enter applies"},
        {"hint_browser", "Add Piece Keys", "Panels", "Up / Down, Left / Right, F, Enter",
         "Choose, switch tab, focus, place"},
        {"hint_outliner", "Outliner", "Panels", "Click, double-click", "Select; double-click to focus"},
    };
    for (const Hint& h : kHints) {
        ActStep a = get_make(h.id, h.name, h.category, Kind::kDetailsOf, {}, h.tip);
        a.keys_text_ = h.keys;
        a.rebindable = false;
        b.push_back(a);
    }
    return b;
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

// The keymap's frame: the keys down last frame, the actions fired this frame, those triggered for the next.
uint8_t g_was[SDL_NUM_SCANCODES];
std::vector<bool> g_fired, g_triggered;
int g_capture = -1, g_capture_slot = 0;

std::string ini_path() { return game_folder_path("thug2-decomp.ini"); }

uint8_t held_mods(const uint8_t* keys)
{
    uint8_t held = 0;
    if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])
        held |= kCtrl;
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
        held |= kShift;
    if (keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT])
        held |= kAlt;
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
    std::vector<ActStep>& r = get_registry();
    return index >= 0 && std::cmp_less(index, r.size()) ? &r[index] : nullptr;
}

int get_find(const std::string& id)
{
    const std::vector<ActStep>& r = get_registry();
    for (int i = 0; std::cmp_less(i, r.size()); ++i) {
        if (r[i].id == id)
            return i;
    }
    return -1;
}

int enrol(const ActStep& action)
{
    std::vector<ActStep>& r = get_registry();
    const int at = get_find(action.id);
    if (at >= 0) {
        const std::vector<Chord> keys = r[at].keys;
        r[at] = action;
        r[at].keys = keys;
        return at;
    }
    r.push_back(action);
    // (its keys from the ini, as the built-ins')
    char text[160] = "";
    GetPrivateProfileStringA("editor_keys", action.id.c_str(), "", text, sizeof(text), ini_path().c_str());
    std::vector<Chord> keys;
    if (text[0] && parse_keys(text, &keys))
        r.back().keys = keys;
    return static_cast<int>(r.size()) - 1;
}

void set_run(int index, std::function<void()> run)
{
    if (ActStep* a = get_pull(index))
        a->run = std::move(run);
}
void set_enabled(int index, std::function<bool()> enabled)
{
    if (ActStep* a = get_pull(index))
        a->enabled = std::move(enabled);
}
void set_checked(int index, std::function<int()> checked)
{
    if (ActStep* a = get_pull(index))
        a->checked = std::move(checked);
}

bool is_switched_on(int index)
{
    const ActStep* a = get_pull(index);
    return a && a->kind != Kind::kDetailsOf && (!a->enabled || a->enabled());
}

int get_checked(int index)
{
    const ActStep* a = get_pull(index);
    return a && a->checked ? a->checked() : -1;
}

std::string get_keys_text(int index)
{
    const ActStep* a = get_pull(index);
    if (!a)
        return "";
    if (a->kind == Kind::kDetailsOf)
        return a->keys_text_;
    std::string text;
    for (const Chord& c : a->keys) {
        const std::string one = format_chord(c);
        if (one.empty())
            continue;
        if (!text.empty())
            text += " / ";
        text += one;
    }
    return text;
}

std::vector<int> conflicts(int index)
{
    std::vector<int> out;
    const ActStep* a = get_pull(index);
    if (!a)
        return out;
    const std::vector<ActStep>& r = get_registry();
    for (int i = 0; std::cmp_less(i, r.size()); ++i) {
        if (i == index || r[i].kind == Kind::kDetailsOf)
            continue;
        // (a key held and a key pressed may share a key: flying's E and nudging's E are one key in two modes)
        if ((r[i].kind == Kind::kHold) != (a->kind == Kind::kHold))
            continue;
        bool clash = false;
        for (const Chord& x : a->keys) {
            for (const Chord& y : r[i].keys)
                clash = clash || same_binding(x, y);
        }
        if (clash)
            out.push_back(i);
    }
    return out;
}

void begin_frame(const uint8_t* keys)
{
    std::vector<ActStep>& r = get_registry();
    g_fired.assign(r.size(), false);
    g_triggered.resize(r.size(), false);
    for (size_t i = 0; i < r.size(); ++i) {
        if (g_triggered[i]) {
            g_fired[i] = true;
            g_triggered[i] = false;
        }
    }
    const uint8_t held = held_mods(keys);
    std::vector<std::vector<Chord>> bound(r.size());
    bool have_bound = false;
    for (int s = 0; s < SDL_NUM_SCANCODES; ++s) {
        const bool pressed = keys[s] && !g_was[s];
        g_was[s] = keys[s];
        if (!pressed || is_modifier(s))
            continue;
        if (g_capture >= 0) {
            // (the key being captured: Escape cancels)
            if (s != SDL_SCANCODE_ESCAPE) {
                if (const ActStep* a = get_pull(g_capture)) {
                    std::vector<Chord> next = a->keys;
                    const Chord chord = {s, held};
                    if (!*get_key_name(s)) {
                        get_log("level editor: key %d can't be bound (not one the keymap names)", s);
                    } else {
                        if (std::cmp_less(g_capture_slot, next.size()))
                            next[g_capture_slot] = chord;
                        else
                            next.push_back(chord);
                        set_keys(g_capture, next);
                    }
                }
            }
            g_capture = -1;
            continue;
        }
        if (!have_bound) {
            for (size_t i = 0; i < r.size(); ++i) {
                if (r[i].kind == Kind::kPress || r[i].kind == Kind::kFlipOver)
                    bound[i] = r[i].keys;
            }
            have_bound = true;
        }
        for (const int i : settle_up(bound, s, held))
            g_fired[i] = true;
    }
}

void seed_keys(const uint8_t* keys)
{
    std::memcpy(g_was, keys, sizeof(g_was));
    g_triggered.assign(g_triggered.size(), false);
}

bool squeezed(int index) { return index >= 0 && std::cmp_less(index, g_fired.size()) && g_fired[index]; }

bool get_held(const uint8_t* keys, int index)
{
    const ActStep* a = get_pull(index);
    if (!a || g_capture >= 0)
        return false;
    const uint8_t held = held_mods(keys);
    for (const Chord& c : a->keys) {
        if (c.scancode > 0 && c.scancode < SDL_NUM_SCANCODES && hold_matches(c, keys[c.scancode] != 0, held))
            return true;
    }
    return false;
}

void fire_off(int index)
{
    const std::vector<ActStep>& r = get_registry();
    if (index < 0 || std::cmp_greater_equal(index, r.size()))
        return;
    g_triggered.resize(r.size(), false);
    g_triggered[index] = true;
}

void get_run(int index)
{
    const ActStep* a = get_pull(index);
    if (!a || !is_switched_on(index))
        return;
    if (a->run)
        a->run();
    else
        fire_off(index);
}

void set_keys(int index, const std::vector<Chord>& keys)
{
    ActStep* a = get_pull(index);
    if (!a || !a->rebindable)
        return;
    a->keys = keys;
    save_keys(*a);
    get_log("level editor: %s bound to %s", a->id.c_str(), format_keys(keys).c_str());
}

void reset_keys(int index)
{
    ActStep* a = get_pull(index);
    if (!a)
        return;
    a->keys = a->defaults;
    save_keys(*a);
}

void load_keys()
{
    std::vector<ActStep>& r = get_registry();
    const std::string path = ini_path();
    for (ActStep& a : r) {
        if (!a.rebindable)
            continue;
        char text[160] = "";
        GetPrivateProfileStringA("editor_keys", a.id.c_str(), "", text, sizeof(text), path.c_str());
        if (!text[0])
            continue;
        std::vector<Chord> keys;
        if (parse_keys(text, &keys))
            a.keys = keys;
        else
            get_log("level editor: [editor_keys] %s=%s isn't a key list - the default kept", a.id.c_str(), text);
    }
}

void begin_capture(int index, int slot)
{
    g_capture = get_pull(index) && get_pull(index)->rebindable ? index : -1;
    g_capture_slot = slot;
}
void cancel_capture() { g_capture = -1; }
bool get_capturing() { return g_capture >= 0; }
int capturing_index() { return g_capture; }

} // namespace frontend::level_editor::actions

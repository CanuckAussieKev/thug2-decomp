// NOT ORIGINAL - the level editor's actions and keymap (ours). See level_editor_actions.h.

#include "game/front/level_editor_actions.h"

#include <SDL2/SDL_scancode.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "hook/log.h"

namespace Front::LevelEditor::Actions {

// --- the pure parts --------------------------------------------------------------------------------------------------

namespace {

struct KeyEntry {
    int scancode;
    const char* name;
};
// The keys the keymap names (SDL scan codes - the positions on a US layout, as the editor reads them).
const KeyEntry kKeyNames[] = {
    {SDL_SCANCODE_A, "A"}, {SDL_SCANCODE_B, "B"}, {SDL_SCANCODE_C, "C"}, {SDL_SCANCODE_D, "D"}, {SDL_SCANCODE_E, "E"},
    {SDL_SCANCODE_F, "F"}, {SDL_SCANCODE_G, "G"}, {SDL_SCANCODE_H, "H"}, {SDL_SCANCODE_I, "I"}, {SDL_SCANCODE_J, "J"},
    {SDL_SCANCODE_K, "K"}, {SDL_SCANCODE_L, "L"}, {SDL_SCANCODE_M, "M"}, {SDL_SCANCODE_N, "N"}, {SDL_SCANCODE_O, "O"},
    {SDL_SCANCODE_P, "P"}, {SDL_SCANCODE_Q, "Q"}, {SDL_SCANCODE_R, "R"}, {SDL_SCANCODE_S, "S"}, {SDL_SCANCODE_T, "T"},
    {SDL_SCANCODE_U, "U"}, {SDL_SCANCODE_V, "V"}, {SDL_SCANCODE_W, "W"}, {SDL_SCANCODE_X, "X"}, {SDL_SCANCODE_Y, "Y"},
    {SDL_SCANCODE_Z, "Z"}, {SDL_SCANCODE_1, "1"}, {SDL_SCANCODE_2, "2"}, {SDL_SCANCODE_3, "3"}, {SDL_SCANCODE_4, "4"},
    {SDL_SCANCODE_5, "5"}, {SDL_SCANCODE_6, "6"}, {SDL_SCANCODE_7, "7"}, {SDL_SCANCODE_8, "8"}, {SDL_SCANCODE_9, "9"},
    {SDL_SCANCODE_0, "0"}, {SDL_SCANCODE_RETURN, "Enter"}, {SDL_SCANCODE_ESCAPE, "Escape"},
    {SDL_SCANCODE_BACKSPACE, "Backspace"}, {SDL_SCANCODE_TAB, "Tab"}, {SDL_SCANCODE_SPACE, "Space"},
    {SDL_SCANCODE_MINUS, "-"}, {SDL_SCANCODE_EQUALS, "="}, {SDL_SCANCODE_LEFTBRACKET, "["},
    {SDL_SCANCODE_RIGHTBRACKET, "]"}, {SDL_SCANCODE_BACKSLASH, "\\"}, {SDL_SCANCODE_SEMICOLON, ";"},
    {SDL_SCANCODE_APOSTROPHE, "'"}, {SDL_SCANCODE_GRAVE, "`"}, {SDL_SCANCODE_COMMA, ","}, {SDL_SCANCODE_PERIOD, "."},
    {SDL_SCANCODE_SLASH, "/"}, {SDL_SCANCODE_F1, "F1"}, {SDL_SCANCODE_F2, "F2"}, {SDL_SCANCODE_F3, "F3"},
    {SDL_SCANCODE_F4, "F4"}, {SDL_SCANCODE_F5, "F5"}, {SDL_SCANCODE_F6, "F6"}, {SDL_SCANCODE_F7, "F7"},
    {SDL_SCANCODE_F8, "F8"}, {SDL_SCANCODE_F9, "F9"}, {SDL_SCANCODE_F10, "F10"}, {SDL_SCANCODE_F11, "F11"},
    {SDL_SCANCODE_F12, "F12"}, {SDL_SCANCODE_INSERT, "Insert"}, {SDL_SCANCODE_HOME, "Home"},
    {SDL_SCANCODE_PAGEUP, "PageUp"}, {SDL_SCANCODE_DELETE, "Delete"}, {SDL_SCANCODE_END, "End"},
    {SDL_SCANCODE_PAGEDOWN, "PageDown"}, {SDL_SCANCODE_RIGHT, "Right"}, {SDL_SCANCODE_LEFT, "Left"},
    {SDL_SCANCODE_DOWN, "Down"}, {SDL_SCANCODE_UP, "Up"}, {SDL_SCANCODE_KP_ENTER, "KeypadEnter"},
    {SDL_SCANCODE_KP_PLUS, "Keypad+"}, {SDL_SCANCODE_KP_MINUS, "Keypad-"}, {SDL_SCANCODE_KP_0, "Keypad0"},
    {SDL_SCANCODE_KP_1, "Keypad1"}, {SDL_SCANCODE_KP_2, "Keypad2"}, {SDL_SCANCODE_KP_3, "Keypad3"},
    {SDL_SCANCODE_KP_4, "Keypad4"}, {SDL_SCANCODE_KP_5, "Keypad5"}, {SDL_SCANCODE_KP_6, "Keypad6"},
    {SDL_SCANCODE_KP_7, "Keypad7"}, {SDL_SCANCODE_KP_8, "Keypad8"}, {SDL_SCANCODE_KP_9, "Keypad9"},
};

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string Trim(const std::string& text)
{
    size_t a = text.find_first_not_of(" \t");
    if (a == std::string::npos)
        return "";
    size_t b = text.find_last_not_of(" \t");
    return text.substr(a, b - a + 1);
}

} // namespace

const char* KeyName(int scancode)
{
    for (const KeyEntry& k : kKeyNames) {
        if (k.scancode == scancode)
            return k.name;
    }
    return "";
}

int KeyFromName(const std::string& name)
{
    std::string wanted = Lower(Trim(name));
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
        if (Lower(k.name) == wanted)
            return k.scancode;
    }
    return 0;
}

std::string FormatChord(Chord chord)
{
    const char* key = KeyName(chord.scancode);
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

bool ParseChord(const std::string& text, Chord* chord)
{
    std::string rest = Trim(text);
    Chord c;
    // Modifiers: words before a '+' (the key itself may be "+"-less punctuation; "Keypad+" ends in one).
    for (;;) {
        size_t plus = rest.find('+');
        if (plus == std::string::npos || plus == 0 || plus + 1 >= rest.size())
            break;
        std::string word = Lower(Trim(rest.substr(0, plus)));
        uint8_t bit = word == "ctrl" || word == "control" ? kCtrl
                      : word == "shift"                   ? kShift
                      : word == "alt" || word == "option" ? kAlt
                                                          : 0;
        if (!bit)
            break;
        c.mods |= bit;
        rest = Trim(rest.substr(plus + 1));
    }
    c.scancode = KeyFromName(rest);
    if (!c.scancode)
        return false;
    *chord = c;
    return true;
}

std::string FormatKeys(const std::vector<Chord>& keys)
{
    std::string text;
    for (const Chord& c : keys) {
        std::string one = FormatChord(c);
        if (one.empty())
            continue;
        if (!text.empty())
            text += ", ";
        text += one;
    }
    return text.empty() ? "none" : text;
}

bool ParseKeys(const std::string& text, std::vector<Chord>* keys)
{
    std::vector<Chord> out;
    std::string all = Trim(text);
    if (Lower(all) == "none" || all.empty()) {
        keys->clear();
        return true;
    }
    size_t start = 0;
    while (start <= all.size()) {
        size_t comma = all.find(',', start);
        // ("Ctrl+," would be a chord ending in a comma: a comma right after '+' belongs to the chord)
        while (comma != std::string::npos && comma > 0 && all[comma - 1] == '+')
            comma = all.find(',', comma + 1);
        std::string part = all.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        Chord c;
        if (!ParseChord(part, &c))
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

bool ChordMatches(Chord chord, int scancode, uint8_t held)
{
    if (!chord.scancode || chord.scancode != scancode)
        return false;
    if (((chord.mods & kCtrl) != 0) != ((held & kCtrl) != 0))
        return false;
    return (held & chord.mods) == chord.mods;
}

int Specificity(Chord chord)
{
    int n = 0;
    for (uint8_t bit : {kCtrl, kShift, kAlt})
        n += (chord.mods & bit) ? 1 : 0;
    return n;
}

std::vector<int> Resolve(const std::vector<std::vector<Chord>>& bound, int scancode, uint8_t held)
{
    std::vector<int> best;
    int bestSpecificity = -1;
    for (int i = 0; i < static_cast<int>(bound.size()); ++i) {
        int specificity = -1;
        for (const Chord& c : bound[i]) {
            if (ChordMatches(c, scancode, held))
                specificity = std::max(specificity, Specificity(c));
        }
        if (specificity < 0)
            continue;
        if (specificity > bestSpecificity) {
            best.clear();
            bestSpecificity = specificity;
        }
        if (specificity == bestSpecificity)
            best.push_back(i);
    }
    return best;
}

bool HoldMatches(Chord chord, bool keyDown, uint8_t held)
{
    if (!chord.scancode || !keyDown)
        return false;
    if (((chord.mods & kCtrl) != 0) != ((held & kCtrl) != 0))
        return false;
    return (held & chord.mods) == chord.mods;
}

bool SameBinding(Chord a, Chord b) { return a.scancode && a == b; }

int FuzzyScore(const std::string& query, const std::string& text)
{
    std::string q;
    for (char c : query) {
        if (c != ' ')
            q += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (q.empty())
        return 1;
    std::string t = Lower(text);
    int score = 0;
    size_t qi = 0;
    int run = 0;
    for (size_t ti = 0; ti < t.size() && qi < q.size(); ++ti) {
        if (t[ti] != q[qi]) {
            run = 0;
            continue;
        }
        bool wordStart = ti == 0 || !std::isalnum(static_cast<unsigned char>(t[ti - 1]));
        score += 1 + (wordStart ? 8 : 0) + run * 4 + (ti == 0 ? 6 : 0);
        ++run;
        ++qi;
    }
    if (qi < q.size())
        return 0;
    // (a whole-word or prefix match of the query ranks above scattered letters)
    if (t.compare(0, q.size(), q) == 0)
        score += 20;
    else if (t.find(q) != std::string::npos)
        score += 10;
    return score + 1;
}

// --- the registry ----------------------------------------------------------------------------------------------------

namespace {

constexpr int A = SDL_SCANCODE_A; // (letters: A + n)
Chord K(int scancode, uint8_t mods = 0) { return {scancode, mods}; }
Chord L(char letter, uint8_t mods = 0) { return {A + (letter - 'A'), mods}; }

Action Make(const char* id, const char* name, const char* category, Kind kind, std::vector<Chord> keys, const char* tip)
{
    Action a;
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
std::vector<Action> Builtins()
{
    std::vector<Action> b = {
        Make("help", "Keyboard Shortcuts", "Panels", Kind::Press, {K(SDL_SCANCODE_F1)}, "Show all keys and mouse gestures"),
        Make("command_search", "Command Search", "Panels", Kind::Press, {K(SDL_SCANCODE_SPACE, kCtrl), K(SDL_SCANCODE_F3)},
             "Find and run any command by name"),
        Make("browser", "Add Piece", "Panels", Kind::Press, {L('B')}, "Browse the level's scenery and objects to place copies"),
        Make("place", "Place Node", "Panels", Kind::Press, {L('N')}, "Add spawns, crowns, flags, lights, sounds, rails and paths"),
        Make("history", "Undo History", "Panels", Kind::Press, {L('H', kCtrl)}, "List of undo steps; click one to jump to it"),
        Make("changes", "Changes", "Panels", Kind::Press, {L('H', kCtrl | kShift)},
             "All changes from the original level"),
        Make("keys", "Keymap", "Panels", Kind::Press, {}, "Rebind editor keys"),
        Make("snapshots", "Snapshots", "Panels", Kind::Press, {}, "Save and restore named versions of the layout"),

        Make("undo", "Undo", "Edit", Kind::Press, {L('Z', kCtrl)}, "Undo the last change"),
        Make("redo", "Redo", "Edit", Kind::Press, {L('Y', kCtrl), L('Z', kCtrl | kShift)}, "Redo the last undone change"),
        Make("save", "Save Layout", "Edit", Kind::Press, {L('S', kCtrl)}, "Save the edited level's layout"),
        Make("duplicate", "Duplicate", "Edit", Kind::Press, {L('D', kCtrl)}, "Duplicate the selection"),
        Make("restore_autosave", "Restore Autosave", "Edit", Kind::Press, {L('R', kCtrl)}, "Restore unsaved changes from a previous session"),
        Make("discard_autosave", "Discard Autosave", "Edit", Kind::Press, {L('R', kCtrl | kShift)}, "Delete unsaved changes from a previous session"),
        Make("delete", "Delete", "Edit", Kind::Press, {K(SDL_SCANCODE_DELETE)},
             "Delete added items; hide original ones"),
        Make("home", "Revert to Original", "Edit", Kind::Press, {K(SDL_SCANCODE_HOME)}, "Restore the selection's original state"),
        Make("reset_level", "Reset Level...", "Edit", Kind::Press, {K(SDL_SCANCODE_DELETE, kCtrl | kShift)},
             "Revert all changes and delete the saved layout"),
        Make("deselect", "Deselect", "Select", Kind::Press, {K(SDL_SCANCODE_BACKSPACE)}, "Clear the selection"),
        Make("lock", "Lock / Unlock", "Select", Kind::Press, {L('L')},
             "Make the selection unselectable in the view"),
        Make("copy", "Copy", "Edit", Kind::Press, {L('C', kCtrl)},
             "Copy the selection to the clipboard"),
        Make("paste", "Paste", "Edit", Kind::Press, {L('V', kCtrl)},
             "Paste from the clipboard at the original position"),
        Make("copy_props", "Copy Transform and Fields", "Inspector", Kind::Press, {L('C', kCtrl | kShift)},
             "Copy position, heading, scale and node fields"),
        Make("paste_transform", "Paste Transform", "Inspector", Kind::Press, {L('V', kCtrl | kShift)},
             "Apply the copied position, heading and scale"),
        Make("paste_fields", "Paste Fields", "Inspector", Kind::Press, {}, "Apply the copied fields to the selected nodes"),

        Make("box_select", "Box Select", "Select", Kind::Press, {L('B', kCtrl)},
             "Drag a selection box"),
        Make("select_similar", "Select Similar...", "Select", Kind::Press, {L('G', kShift)},
             "Select items matching the selection"),
        Make("select_family", "Select Family", "Select", Kind::Press, {},
             "Select all items of the selection's family"),
        Make("invert_selection", "Invert Selection", "Select", Kind::Press, {L('I', kCtrl)},
             "Select everything of the same kind that isn't selected"),
        Make("select_all", "Select All", "Select", Kind::Press, {L('A', kCtrl)},
             "Select all objects, or all scenery when scenery is selected"),
        Make("make_group", "Group Selection", "Select", Kind::Press, {L('G', kCtrl)},
             "Create a named group in the Outliner"),

        Make("turn_left", "Turn Left", "Transform", Kind::Press, {L('Z')}, "Rotate left by the rotation snap"),
        Make("turn_right", "Turn Right", "Transform", Kind::Press, {L('X')}, "Rotate right by the rotation snap"),
        Make("scale_down", "Scale Down", "Transform", Kind::Press, {K(SDL_SCANCODE_LEFTBRACKET)}, "Scale down 10% (Shift: 1%)"),
        Make("scale_up", "Scale Up", "Transform", Kind::Press, {K(SDL_SCANCODE_RIGHTBRACKET)}, "Scale up 10% (Shift: 1%)"),
        Make("drop", "Drop to Ground", "Transform", Kind::Press, {L('G')}, "Move the selection down onto the ground"),

        Make("forward", "Fly Forward", "Camera", Kind::Hold, {L('W')},
             "Hold with the right mouse button"),
        Make("back", "Fly Back", "Camera", Kind::Hold, {L('S')}, "Hold with the right mouse button"),
        Make("left", "Fly Left", "Camera", Kind::Hold, {L('A')}, "Hold with the right mouse button"),
        Make("right", "Fly Right", "Camera", Kind::Hold, {L('D')}, "Hold with the right mouse button"),
        Make("fly_up", "Fly Up", "Camera", Kind::Hold, {L('E'), K(SDL_SCANCODE_SPACE)}, "Move the camera up"),
        Make("fly_down", "Fly Down", "Camera", Kind::Hold, {L('Q')}, "Move the camera down"),
        Make("nudge_up", "Nudge Up", "Transform", Kind::Hold, {K(SDL_SCANCODE_PAGEUP)}, "Move the selection up"),
        Make("nudge_down", "Nudge Down", "Transform", Kind::Hold, {K(SDL_SCANCODE_PAGEDOWN)}, "Move the selection down"),
        Make("faster", "Increase Fly Speed", "Camera", Kind::Press, {K(SDL_SCANCODE_EQUALS)}, "Fly 1.5x faster"),
        Make("slower", "Decrease Fly Speed", "Camera", Kind::Press, {K(SDL_SCANCODE_MINUS)}, "Fly 1.5x slower"),

        Make("collision", "Show Collision", "View", Kind::Press, {L('C')}, "Show collision near the camera, coloured by surface"),
        Make("faces", "Face Select Mode", "View", Kind::Press, {L('K')}, "Select collision faces to edit their flags"),
        Make("triggers", "Show Trigger Volumes", "View", Kind::Press, {L('T')}, "Show trigger volumes and the Triggers window"),
        Make("occlusion", "Occlusion Culling", "View", Kind::Press, {L('O')}, "Cull hidden geometry as in play"),
        Make("hide", "Hide Selection", "View", Kind::Press, {L('H')}, "Hide in the editor only; not saved"),
        Make("isolate", "Isolate Selection", "View", Kind::Press, {L('I'), K(SDL_SCANCODE_SLASH)}, "Hide everything else in the editor"),
        Make("show_all", "Show All", "View", Kind::Press, {L('H', kAlt)}, "Show everything hidden in the editor"),
        Make("camera_node", "Camera Node from View", "View", Kind::Press, {L('V')}, "Add a camera node at the current view"),
        Make("log_camera", "Log Camera Position", "View", Kind::Press, {L('P')}, "Write the camera position to the log"),

        Make("chain_insert", "Insert Point", "Chains", Kind::Press, {K(SDL_SCANCODE_INSERT)}, "Insert a point halfway to the next"),
        Make("chain_extend", "Extend from End", "Chains", Kind::Press, {K(SDL_SCANCODE_END)}, "Continue drawing from an end point"),
        Make("chain_link", "Link to Point", "Chains", Kind::Press, {L('J')}, "Link this end to the next end clicked"),
        Make("chain_unlink", "Unlink Point", "Chains", Kind::Press, {L('U')}, "Unlink this point from the next"),
        Make("chain_finish", "Finish Drawing", "Chains", Kind::Press, {K(SDL_SCANCODE_RETURN)}, "Finish drawing the rail or path"),

        // (UX1 - level_editor_play.h, level_editor_prefs.h, level_editor_camera.h)
        Make("play", "Play from Here", "Play", Kind::Press, {K(SDL_SCANCODE_F5), L('P', kCtrl)},
             "Skate from the camera position; press again to return"),
        Make("preferences", "Preferences...", "Panels", Kind::Press, {K(SDL_SCANCODE_COMMA, kCtrl)}, "Editor preferences and keymap"),
        Make("frame_selection", "Frame Selection", "Camera", Kind::Press, {L('F')}, "Fit the selection in the view"),
    };
    for (int i = 1; i <= 9; ++i) {
        std::string n = std::to_string(i);
        Action a = Make(("bookmark_" + n).c_str(), ("Go to Bookmark " + n).c_str(), "Camera", Kind::Press,
                        {K(SDL_SCANCODE_1 + i - 1)}, "Move the camera to bookmark n");
        b.push_back(a);
    }
    for (int i = 1; i <= 9; ++i) {
        std::string n = std::to_string(i);
        Action a = Make(("bookmark_store_" + n).c_str(), ("Set Bookmark " + n).c_str(), "Camera", Kind::Press,
                        {K(SDL_SCANCODE_1 + i - 1, kCtrl)}, "Store the camera as bookmark n");
        b.push_back(a);
    }
    for (Action& a : b) {
        // (the bookmarks' tips name their own number)
        size_t at = a.tip.find("bookmark n");
        if (at != std::string::npos)
            a.tip.replace(at + 9, 1, a.id.substr(a.id.size() - 1));
    }
    std::vector<Action> views = {
        Make("view_top", "Top View", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_7)}, "Orthographic view from above"),
        Make("view_bottom", "Bottom View", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_7, kCtrl)}, "Orthographic view from below"),
        Make("view_front", "Front View", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_1)}, "Orthographic view from the front"),
        Make("view_back", "Back View", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_1, kCtrl)}, "Orthographic view from the back"),
        Make("view_right", "Right View", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_3)}, "Orthographic view from the right"),
        Make("view_left", "Left View", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_3, kCtrl)}, "Orthographic view from the left"),
        Make("view_ortho", "Toggle Perspective / Ortho", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_5)},
             "Switch between perspective and orthographic"),
    };
    b.insert(b.end(), views.begin(), views.end());
    // (UX2 - level_editor_transform.h)
    std::vector<Action> tools = {
        Make("tool_move", "Move Tool", "Transform", Kind::Press, {L('W')}, "Move the selection with the gizmo"),
        Make("tool_rotate", "Rotate Tool", "Transform", Kind::Press, {L('E')}, "Rotate the selection with the gizmo"),
        Make("tool_scale", "Scale Tool", "Transform", Kind::Press, {L('R')}, "Scale the selection with the gizmo"),
        Make("tool_cycle", "Next Tool", "Transform", Kind::Press, {K(SDL_SCANCODE_SPACE)}, "Cycle Move, Rotate and Scale"),
        Make("tool_space", "Toggle World / Local", "Transform", Kind::Press, {K(SDL_SCANCODE_GRAVE)},
             "Switch the gizmo between world and local axes"),
        Make("modal_transform", "Modal Transform", "Transform", Kind::Press, {L('G', kAlt)},
             "Transform with the mouse released; X/Y/Z constrain, click or Enter applies"),
        Make("repeat_duplicate", "Repeat Duplicate", "Edit", Kind::Press, {L('D', kCtrl | kShift)},
             "Duplicate again with the same offset"),
        Make("nudge_forward", "Nudge Forward", "Transform", Kind::Hold, {K(SDL_SCANCODE_UP)}, "Move the selection forward"),
        Make("nudge_back", "Nudge Back", "Transform", Kind::Hold, {K(SDL_SCANCODE_DOWN)}, "Move the selection back"),
        Make("nudge_left", "Nudge Left", "Transform", Kind::Hold, {K(SDL_SCANCODE_LEFT)}, "Move the selection left"),
        Make("nudge_right", "Nudge Right", "Transform", Kind::Hold, {K(SDL_SCANCODE_RIGHT)}, "Move the selection right"),
    };
    b.insert(b.end(), tools.begin(), tools.end());
    // (UX4 / UX5)
    std::vector<Action> views2 = {
        Make("view_mode_lit", "Lit", "View", Kind::Press, {K(SDL_SCANCODE_1, kAlt)}, "View mode: as in play"),
        Make("view_mode_unlit", "Unlit", "View", Kind::Press, {K(SDL_SCANCODE_2, kAlt)}, "View mode: textures without lighting"),
        Make("view_mode_wireframe", "Wireframe", "View", Kind::Press, {K(SDL_SCANCODE_3, kAlt)}, "View mode: edges only"),
        Make("view_mode_collision", "Collision Only", "View", Kind::Press, {K(SDL_SCANCODE_4, kAlt)},
             "View mode: collision only"),
        Make("jump_arcs", "Jump Arcs", "View", Kind::Press, {L('Y')},
             "Show launch arcs from the selected ramp"),
        Make("ruler", "Ruler", "View", Kind::Press, {L('M')}, "Measure between two points"),
        Make("ruler_clear", "Clear Ruler", "View", Kind::Press, {L('M', kShift)}, "Clear the measurement"),
        Make("problems", "Problems", "Panels", Kind::Press, {L('P', kAlt)}, "Show level problems and budget"),
        // (UX6 - level_editor_playtest.h)
        Make("simulate", "Simulate", "Play", Kind::Press, {K(SDL_SCANCODE_F6)},
             "Run the world with the skater frozen; press again to stop"),
        Make("trail_window", "Trails", "Panels", Kind::Press, {K(SDL_SCANCODE_F7)},
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
        {"hint_alt_click", "Select Single Piece", "Select", "Alt+click, middle click", "Select one scenery piece instead of its group"},
        {"hint_ctrl_click", "Remove Part", "Select", "Ctrl+click", "Remove a piece from the scenery selection"},
        {"hint_box", "Box Select", "Select", "Drag from empty space", "Drag right: inside; drag left: crossing. Shift adds, Ctrl removes"},
        {"hint_drag", "Move", "Transform", "Drag", "Shift: vertical; Ctrl: snap to grid; Esc cancels"},
        {"hint_arrow", "Move Along Axis", "Transform", "Drag an arrow", "Constrain to that axis"},
        {"hint_carry", "Don't Carry", "Transform", "Alt while moving", "Leave attached objects in place"},
        {"hint_escape", "Cancel", "Panels", "Esc", "Cancel a drag, close a window, deselect, then exit"},
        {"hint_orbit", "Orbit", "Camera", "Alt+drag", "Orbit around the point under the cursor"},
        {"hint_speed", "Fly Speed", "Camera", "Right mouse + wheel", "Change fly speed while looking"},
        {"hint_fly", "Fly", "Camera", "Right mouse + W A S D Q E", "Fly while holding the right mouse button"},
        {"hint_snaps", "Snap While Dragging", "Transform", "Ctrl / Ctrl+Shift / Alt+Shift / Ctrl+Alt",
         "Grid, world grid, surface, vertex"},
        {"hint_typed", "Numeric Input", "Transform", "X / Y / Z, digits (dragging)", "Type an axis and amount while dragging; Enter applies"},
        {"hint_browser", "Add Piece Keys", "Panels", "Up / Down, Left / Right, F, Enter", "Choose, switch tab, focus, place"},
        {"hint_outliner", "Outliner", "Panels", "Click, double-click", "Select; double-click to focus"},
    };
    for (const Hint& h : kHints) {
        Action a = Make(h.id, h.name, h.category, Kind::Info, {}, h.tip);
        a.keysText = h.keys;
        a.rebindable = false;
        b.push_back(a);
    }
    return b;
}

std::vector<Action>& Registry()
{
    static std::vector<Action> registry = Builtins();
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        LoadKeys();
    }
    return registry;
}

// The keymap's frame: the keys down last frame, the actions fired this frame, those triggered for the next.
uint8_t g_was[SDL_NUM_SCANCODES];
std::vector<bool> g_fired, g_triggered;
int g_capture = -1, g_captureSlot = 0;

std::string IniPath() { return GameFolderPath("thug2-decomp.ini"); }

uint8_t HeldMods(const uint8_t* keys)
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

bool IsModifier(int scancode)
{
    return scancode == SDL_SCANCODE_LCTRL || scancode == SDL_SCANCODE_RCTRL || scancode == SDL_SCANCODE_LSHIFT ||
           scancode == SDL_SCANCODE_RSHIFT || scancode == SDL_SCANCODE_LALT || scancode == SDL_SCANCODE_RALT ||
           scancode == SDL_SCANCODE_LGUI || scancode == SDL_SCANCODE_RGUI;
}

void SaveKeys(const Action& a)
{
    bool asDefault = a.keys == a.defaults;
    WritePrivateProfileStringA("editor_keys", a.id.c_str(), asDefault ? nullptr : FormatKeys(a.keys).c_str(),
                               IniPath().c_str());
}

} // namespace

const std::vector<Action>& All() { return Registry(); }

Action* Get(int index)
{
    std::vector<Action>& r = Registry();
    return index >= 0 && index < static_cast<int>(r.size()) ? &r[index] : nullptr;
}

int Find(const std::string& id)
{
    const std::vector<Action>& r = Registry();
    for (int i = 0; i < static_cast<int>(r.size()); ++i) {
        if (r[i].id == id)
            return i;
    }
    return -1;
}

int Register(const Action& action)
{
    std::vector<Action>& r = Registry();
    int at = Find(action.id);
    if (at >= 0) {
        std::vector<Chord> keys = r[at].keys;
        r[at] = action;
        r[at].keys = keys;
        return at;
    }
    r.push_back(action);
    // (its keys from the ini, as the built-ins')
    char text[160] = "";
    GetPrivateProfileStringA("editor_keys", action.id.c_str(), "", text, sizeof(text), IniPath().c_str());
    std::vector<Chord> keys;
    if (text[0] && ParseKeys(text, &keys))
        r.back().keys = keys;
    return static_cast<int>(r.size()) - 1;
}

void SetRun(int index, std::function<void()> run)
{
    if (Action* a = Get(index))
        a->run = std::move(run);
}
void SetEnabled(int index, std::function<bool()> enabled)
{
    if (Action* a = Get(index))
        a->enabled = std::move(enabled);
}
void SetChecked(int index, std::function<int()> checked)
{
    if (Action* a = Get(index))
        a->checked = std::move(checked);
}

bool Enabled(int index)
{
    const Action* a = Get(index);
    return a && a->kind != Kind::Info && (!a->enabled || a->enabled());
}

int Checked(int index)
{
    const Action* a = Get(index);
    return a && a->checked ? a->checked() : -1;
}

std::string KeysText(int index)
{
    const Action* a = Get(index);
    if (!a)
        return "";
    if (a->kind == Kind::Info)
        return a->keysText;
    std::string text;
    for (const Chord& c : a->keys) {
        std::string one = FormatChord(c);
        if (one.empty())
            continue;
        if (!text.empty())
            text += " / ";
        text += one;
    }
    return text;
}

std::vector<int> Conflicts(int index)
{
    std::vector<int> out;
    const Action* a = Get(index);
    if (!a)
        return out;
    const std::vector<Action>& r = Registry();
    for (int i = 0; i < static_cast<int>(r.size()); ++i) {
        if (i == index || r[i].kind == Kind::Info)
            continue;
        // (a key held and a key pressed may share a key: flying's E and nudging's E are one key in two modes)
        if ((r[i].kind == Kind::Hold) != (a->kind == Kind::Hold))
            continue;
        bool clash = false;
        for (const Chord& x : a->keys) {
            for (const Chord& y : r[i].keys)
                clash = clash || SameBinding(x, y);
        }
        if (clash)
            out.push_back(i);
    }
    return out;
}

void BeginFrame(const uint8_t* keys)
{
    std::vector<Action>& r = Registry();
    g_fired.assign(r.size(), false);
    g_triggered.resize(r.size(), false);
    for (size_t i = 0; i < r.size(); ++i) {
        if (g_triggered[i]) {
            g_fired[i] = true;
            g_triggered[i] = false;
        }
    }
    uint8_t held = HeldMods(keys);
    std::vector<std::vector<Chord>> bound(r.size());
    bool haveBound = false;
    for (int s = 0; s < SDL_NUM_SCANCODES; ++s) {
        bool pressed = keys[s] && !g_was[s];
        g_was[s] = keys[s];
        if (!pressed || IsModifier(s))
            continue;
        if (g_capture >= 0) {
            // (the key being captured: Escape cancels)
            if (s != SDL_SCANCODE_ESCAPE) {
                if (Action* a = Get(g_capture)) {
                    std::vector<Chord> next = a->keys;
                    Chord chord = {s, held};
                    if (!*KeyName(s)) {
                        Log("level editor: key %d can't be bound (not one the keymap names)", s);
                    } else {
                        if (g_captureSlot < static_cast<int>(next.size()))
                            next[g_captureSlot] = chord;
                        else
                            next.push_back(chord);
                        SetKeys(g_capture, next);
                    }
                }
            }
            g_capture = -1;
            continue;
        }
        if (!haveBound) {
            for (size_t i = 0; i < r.size(); ++i) {
                if (r[i].kind == Kind::Press || r[i].kind == Kind::Toggle)
                    bound[i] = r[i].keys;
            }
            haveBound = true;
        }
        for (int i : Resolve(bound, s, held))
            g_fired[i] = true;
    }
}

void SeedKeys(const uint8_t* keys)
{
    std::memcpy(g_was, keys, sizeof(g_was));
    g_triggered.assign(g_triggered.size(), false);
}

bool Pressed(int index) { return index >= 0 && index < static_cast<int>(g_fired.size()) && g_fired[index]; }

bool Held(const uint8_t* keys, int index)
{
    const Action* a = Get(index);
    if (!a || g_capture >= 0)
        return false;
    uint8_t held = HeldMods(keys);
    for (const Chord& c : a->keys) {
        if (c.scancode > 0 && c.scancode < SDL_NUM_SCANCODES && HoldMatches(c, keys[c.scancode] != 0, held))
            return true;
    }
    return false;
}

void Trigger(int index)
{
    std::vector<Action>& r = Registry();
    if (index < 0 || index >= static_cast<int>(r.size()))
        return;
    g_triggered.resize(r.size(), false);
    g_triggered[index] = true;
}

void Run(int index)
{
    Action* a = Get(index);
    if (!a || !Enabled(index))
        return;
    if (a->run)
        a->run();
    else
        Trigger(index);
}

void SetKeys(int index, const std::vector<Chord>& keys)
{
    Action* a = Get(index);
    if (!a || !a->rebindable)
        return;
    a->keys = keys;
    SaveKeys(*a);
    Log("level editor: %s bound to %s", a->id.c_str(), FormatKeys(keys).c_str());
}

void ResetKeys(int index)
{
    Action* a = Get(index);
    if (!a)
        return;
    a->keys = a->defaults;
    SaveKeys(*a);
}

void LoadKeys()
{
    std::vector<Action>& r = Registry();
    std::string path = IniPath();
    for (Action& a : r) {
        if (!a.rebindable)
            continue;
        char text[160] = "";
        GetPrivateProfileStringA("editor_keys", a.id.c_str(), "", text, sizeof(text), path.c_str());
        if (!text[0])
            continue;
        std::vector<Chord> keys;
        if (ParseKeys(text, &keys))
            a.keys = keys;
        else
            Log("level editor: [editor_keys] %s=%s isn't a key list - the default kept", a.id.c_str(), text);
    }
}

void BeginCapture(int index, int slot)
{
    g_capture = Get(index) && Get(index)->rebindable ? index : -1;
    g_captureSlot = slot;
}
void CancelCapture() { g_capture = -1; }
bool Capturing() { return g_capture >= 0; }
int CapturingIndex() { return g_capture; }

} // namespace Front::LevelEditor::Actions

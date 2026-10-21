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
        Make("help", "Help: the keys", "Panels", Kind::Press, {K(SDL_SCANCODE_F1)}, "Every key and gesture (F1 again or Esc closes it)"),
        Make("command_search", "Command search", "Panels", Kind::Press, {K(SDL_SCANCODE_SPACE, kCtrl), K(SDL_SCANCODE_F3)},
             "Search every action and setting by name, with its key; Enter runs the highlighted one"),
        Make("browser", "Add: the piece browser", "Panels", Kind::Press, {L('B')}, "Copies of the level's scenery and objects"),
        Make("place", "Place palette", "Panels", Kind::Press, {L('N')}, "New nodes: spawns, crowns, flags, lights, sounds, chains"),
        Make("history", "Undo history", "Panels", Kind::Press, {L('H', kCtrl)}, "The undo / redo steps by name - click one to go back or forward to it"),
        Make("changes", "Changes", "Panels", Kind::Press, {L('H', kCtrl | kShift)},
             "Everything this edited copy changes from the level as shipped, by kind: Go to, Revert"),
        Make("keys", "Keys (rebind)", "Panels", Kind::Press, {}, "Rebind the editor's keys (kept in thug2-decomp.ini [editor_keys])"),
        Make("snapshots", "Layout snapshots", "Panels", Kind::Press, {}, "Named versions of this edited copy's layout: save one, switch to one"),

        Make("undo", "Undo", "Edit", Kind::Press, {L('Z', kCtrl)}, "The last change undone (64 steps, this session)"),
        Make("redo", "Redo", "Edit", Kind::Press, {L('Y', kCtrl), L('Z', kCtrl | kShift)}, "The last undone change made again"),
        Make("save", "Save layout", "Edit", Kind::Press, {L('S', kCtrl)}, "The edited copy's layout saved - the level select's (EDITED) entry loads it"),
        Make("duplicate", "Duplicate", "Edit", Kind::Press, {L('D', kCtrl)}, "The selection copied beside itself"),
        Make("restore_autosave", "Restore the autosave", "Edit", Kind::Press, {L('R', kCtrl)}, "An older session's unsaved changes put in place"),
        Make("discard_autosave", "Discard the autosave", "Edit", Kind::Press, {L('R', kCtrl | kShift)}, "An older session's unsaved changes deleted"),
        Make("delete", "Delete / hide", "Edit", Kind::Press, {K(SDL_SCANCODE_DELETE)},
             "Scenery hidden, a copy deleted, an added node deleted, a level's node hidden (asks)"),
        Make("home", "Home: as the level has it", "Edit", Kind::Press, {K(SDL_SCANCODE_HOME)}, "The selection put back where the level had it"),
        Make("reset_level", "Reset level", "Edit", Kind::Press, {K(SDL_SCANCODE_DELETE, kCtrl | kShift)},
             "Everything back as shipped, the saved layout deleted (asks; Ctrl+Z undoes)"),
        Make("deselect", "Deselect", "Select", Kind::Press, {K(SDL_SCANCODE_BACKSPACE)}, "Nothing selected"),
        Make("lock", "Lock / unlock (clicks pass it)", "Select", Kind::Press, {L('L')},
             "The selection locked: clicks pass it; with a rail / ladder / path point: its whole chain or itself"),
        Make("copy", "Copy (to another level)", "Edit", Kind::Press, {L('C', kCtrl)},
             "The selection copied to the clipboard as text: scenery copies, objects, added nodes, triggers"),
        Make("paste", "Paste (from another level)", "Edit", Kind::Press, {L('V', kCtrl)},
             "The clipboard's pieces put in this edited copy where they were (their sources must exist here)"),
        Make("copy_props", "Copy transform and fields", "Inspector", Kind::Press, {L('C', kCtrl | kShift)},
             "The selection's position, heading and scale - and the inspected node's own fields"),
        Make("paste_transform", "Paste transform", "Inspector", Kind::Press, {L('V', kCtrl | kShift)},
             "The copied position, heading and scale given to the selection (one undo step)"),
        Make("paste_fields", "Paste fields", "Inspector", Kind::Press, {}, "The copied fields given to the selected nodes (one undo step)"),

        Make("box_select", "Box select", "Select", Kind::Press, {L('B', kCtrl)},
             "The next drag in the view draws a box (a drag from empty space always does): right = inside, left = crossing; "
             "Shift adds, Ctrl removes"),
        Make("select_similar", "Select similar...", "Select", Kind::Press, {L('G', kShift)},
             "Same mesh / same class / same node family / same type as the selection"),
        Make("select_family", "Select all of this family", "Select", Kind::Press, {},
             "Every object (or scenery piece) of the selection's family"),
        Make("invert_selection", "Invert selection", "Select", Kind::Press, {L('I', kCtrl)},
             "Everything of the selection's kind (objects or scenery) not selected, selected"),
        Make("select_all", "Select all (objects or scenery)", "Select", Kind::Press, {L('A', kCtrl)},
             "Every object with a model (or, scenery selected, every movable piece)"),
        Make("make_group", "Group the selection", "Select", Kind::Press, {L('G', kCtrl)},
             "A named group of the selection in the Outliner (editor-only, saved in the layout)"),

        Make("turn_left", "Turn left", "Transform", Kind::Press, {L('Z')}, "Objects and nodes 15 degrees, scenery a quarter"),
        Make("turn_right", "Turn right", "Transform", Kind::Press, {L('X')}, "Objects and nodes 15 degrees, scenery a quarter"),
        Make("scale_down", "Scale down", "Transform", Kind::Press, {K(SDL_SCANCODE_LEFTBRACKET)}, "10% (Shift: 1%; Alt: stretch along the axis faced)"),
        Make("scale_up", "Scale up", "Transform", Kind::Press, {K(SDL_SCANCODE_RIGHTBRACKET)}, "10% (Shift: 1%; Alt: stretch along the axis faced)"),
        Make("drop", "Drop to the ground", "Transform", Kind::Press, {L('G')}, "The selection down onto the ground below it"),

        Make("forward", "Fly forward", "Camera", Kind::Hold, {L('W')},
             "With the right button held (wasd_flies=1: always - and it nudges a selection)"),
        Make("back", "Fly back", "Camera", Kind::Hold, {L('S')}, "With the right button held (wasd_flies=1: always)"),
        Make("left", "Fly left", "Camera", Kind::Hold, {L('A')}, "With the right button held (wasd_flies=1: always)"),
        Make("right", "Fly right", "Camera", Kind::Hold, {L('D')}, "With the right button held (wasd_flies=1: always)"),
        Make("fly_up", "Fly up", "Camera", Kind::Hold, {L('E'), K(SDL_SCANCODE_SPACE)}, "The camera up"),
        Make("fly_down", "Fly down", "Camera", Kind::Hold, {L('Q')}, "The camera down"),
        Make("nudge_up", "Nudge up", "Transform", Kind::Hold, {K(SDL_SCANCODE_PAGEUP)}, "The selection up"),
        Make("nudge_down", "Nudge down", "Transform", Kind::Hold, {K(SDL_SCANCODE_PAGEDOWN)}, "The selection down"),
        Make("faster", "Flying speed up", "Camera", Kind::Press, {K(SDL_SCANCODE_EQUALS)}, "1.5 times faster"),
        Make("slower", "Flying speed down", "Camera", Kind::Press, {K(SDL_SCANCODE_MINUS)}, "1.5 times slower"),

        Make("collision", "Show collision", "View", Kind::Press, {L('C')}, "The collision near the camera by surface; invisible walls magenta"),
        Make("faces", "Faces mode", "View", Kind::Press, {L('K')}, "Clicks take collision faces; the Inspector sets their flags"),
        Make("triggers", "Show trigger volumes", "View", Kind::Press, {L('T')}, "Trigger volumes and the Triggers window"),
        Make("occlusion", "Occlusion culling", "View", Kind::Press, {L('O')}, "On as in play / off while editing"),
        Make("hide", "Hide the selection (editor)", "View", Kind::Press, {L('H')}, "Hidden while editing only, never saved"),
        Make("isolate", "Isolate the selection (editor)", "View", Kind::Press, {L('I'), K(SDL_SCANCODE_SLASH)}, "Everything else hidden while editing"),
        Make("show_all", "Show everything (editor)", "View", Kind::Press, {L('H', kAlt)}, "Everything hidden in the editor shown"),
        Make("camera_node", "Camera node from the view", "View", Kind::Press, {L('V')}, "A camera node where the view is, looking its way"),
        Make("log_camera", "Write the camera to the log", "View", Kind::Press, {L('P')}, "As a menu backdrop view"),

        Make("chain_insert", "Chain: insert a point", "Chains", Kind::Press, {K(SDL_SCANCODE_INSERT)}, "A point halfway to the next"),
        Make("chain_extend", "Chain: draw on from this end", "Chains", Kind::Press, {K(SDL_SCANCODE_END)}, "Draw on from an end point"),
        Make("chain_link", "Chain: link to the next end clicked", "Chains", Kind::Press, {L('J')}, "This end linked to the next end clicked"),
        Make("chain_unlink", "Chain: unlink from the next", "Chains", Kind::Press, {L('U')}, "This point unlinked from its next"),
        Make("chain_finish", "Chain: finish drawing", "Chains", Kind::Press, {K(SDL_SCANCODE_RETURN)}, "Drawing a rail / path finished (or a right click)"),

        // (UX1 - level_editor_play.h, level_editor_prefs.h, level_editor_camera.h)
        Make("play", "Play from here", "Play", Kind::Press, {K(SDL_SCANCODE_F5), L('P', kCtrl)},
             "Free skate from the camera's place (the editor comes back where it was)"),
        Make("preferences", "Preferences", "Panels", Kind::Press, {K(SDL_SCANCODE_COMMA, kCtrl)}, "The editor's preferences (and its Keys)"),
        Make("frame_selection", "Frame the selection", "Camera", Kind::Press, {L('F')}, "The camera to fit the selection"),
    };
    for (int i = 1; i <= 9; ++i) {
        std::string n = std::to_string(i);
        Action a = Make(("bookmark_" + n).c_str(), ("Camera bookmark " + n).c_str(), "Camera", Kind::Press,
                        {K(SDL_SCANCODE_1 + i - 1)}, "The camera to bookmark n (stored with Ctrl+n; kept per level)");
        b.push_back(a);
    }
    for (int i = 1; i <= 9; ++i) {
        std::string n = std::to_string(i);
        Action a = Make(("bookmark_store_" + n).c_str(), ("Store camera bookmark " + n).c_str(), "Camera", Kind::Press,
                        {K(SDL_SCANCODE_1 + i - 1, kCtrl)}, "The camera stored as bookmark n");
        b.push_back(a);
    }
    for (Action& a : b) {
        // (the bookmarks' tips name their own number)
        size_t at = a.tip.find("bookmark n");
        if (at != std::string::npos)
            a.tip.replace(at + 9, 1, a.id.substr(a.id.size() - 1));
        at = a.tip.find("Ctrl+n");
        if (at != std::string::npos)
            a.tip.replace(at + 5, 1, a.id.substr(a.id.size() - 1));
    }
    std::vector<Action> views = {
        Make("view_top", "View: top", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_7)}, "An orthographic view from above"),
        Make("view_bottom", "View: bottom", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_7, kCtrl)}, "From below"),
        Make("view_front", "View: front", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_1)}, "From the front"),
        Make("view_back", "View: back", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_1, kCtrl)}, "From the back"),
        Make("view_right", "View: right", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_3)}, "From the right"),
        Make("view_left", "View: left", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_3, kCtrl)}, "From the left"),
        Make("view_ortho", "View: perspective / ortho", "Camera", Kind::Press, {K(SDL_SCANCODE_KP_5)},
             "Perspective and orthographic switched"),
    };
    b.insert(b.end(), views.begin(), views.end());
    // (UX2 - level_editor_transform.h)
    std::vector<Action> tools = {
        Make("tool_move", "Tool: move", "Transform", Kind::Press, {L('W')}, "The gizmo moves (without the right button held)"),
        Make("tool_rotate", "Tool: rotate", "Transform", Kind::Press, {L('E')}, "The gizmo turns"),
        Make("tool_scale", "Tool: scale", "Transform", Kind::Press, {L('R')}, "The gizmo scales"),
        Make("tool_cycle", "Tool: next", "Transform", Kind::Press, {K(SDL_SCANCODE_SPACE)}, "Move, rotate, scale in turn"),
        Make("tool_space", "World / local axes", "Transform", Kind::Press, {K(SDL_SCANCODE_GRAVE)},
             "The gizmo along the world's axes or the selection's own"),
        Make("modal_transform", "Transform without the mouse held", "Transform", Kind::Press, {L('G', kAlt)},
             "The tool's transform follows the mouse: X / Y / Z an axis, type an amount, a click or Enter applies"),
        Make("repeat_duplicate", "Repeat duplicate", "Edit", Kind::Press, {L('D', kCtrl | kShift)},
             "The last Ctrl+D again, with the copy's move and turn since"),
        Make("nudge_forward", "Nudge forward", "Transform", Kind::Hold, {K(SDL_SCANCODE_UP)}, "The selection along the view"),
        Make("nudge_back", "Nudge back", "Transform", Kind::Hold, {K(SDL_SCANCODE_DOWN)}, "The selection back"),
        Make("nudge_left", "Nudge left", "Transform", Kind::Hold, {K(SDL_SCANCODE_LEFT)}, "The selection left"),
        Make("nudge_right", "Nudge right", "Transform", Kind::Hold, {K(SDL_SCANCODE_RIGHT)}, "The selection right"),
    };
    b.insert(b.end(), tools.begin(), tools.end());
    // (UX4 / UX5)
    std::vector<Action> views2 = {
        Make("view_mode_lit", "View mode: lit", "View", Kind::Press, {K(SDL_SCANCODE_1, kAlt)}, "The level as in play"),
        Make("view_mode_unlit", "View mode: unlit", "View", Kind::Press, {K(SDL_SCANCODE_2, kAlt)}, "Textures without lighting"),
        Make("view_mode_wireframe", "View mode: wireframe", "View", Kind::Press, {K(SDL_SCANCODE_3, kAlt)}, "Edges only"),
        Make("view_mode_collision", "View mode: collision only", "View", Kind::Press, {K(SDL_SCANCODE_4, kAlt)},
             "Only the collision, by surface"),
        Make("jump_arcs", "Jump arcs", "View", Kind::Press, {L('Y')},
             "A kicker's / quarter pipe's / ramp's arcs at push, top and max speed"),
        Make("ruler", "Ruler", "View", Kind::Press, {L('M')}, "Click two points to measure (snaps to corners; Alt: no snap)"),
        Make("ruler_clear", "Ruler: clear", "View", Kind::Press, {L('M', kShift)}, "The measurement taken away"),
        Make("problems", "Problems window", "Panels", Kind::Press, {L('P', kAlt)}, "What the checker finds in the edited copy"),
        // (UX6 - level_editor_playtest.h)
        Make("simulate", "Simulate", "Play", Kind::Press, {K(SDL_SCANCODE_F6)},
             "The world runs (peds, cars, moving objects) with the editor's camera, the skater frozen; again: stops, "
             "objects put back"),
        Make("trail_window", "Trail window", "Panels", Kind::Press, {K(SDL_SCANCODE_F7)},
             "The skater's trails from test runs, the gaps landed / untested, Simulate"),
    };
    b.insert(b.end(), views2.begin(), views2.end());
    // The hints (not rebindable): what the mouse and the modes' own keys do.
    struct Hint {
        const char *id, *name, *category, *keys, *tip;
    };
    static const Hint kHints[] = {
        {"hint_look", "Look about / orbit", "Camera", "Right mouse", "Orbits the selection when there is one"},
        {"hint_wheel", "Dolly / push-pull", "Camera", "Wheel", "Forward / back; with a selection nearer / further; dragging: push / pull"},
        {"hint_fast", "Faster", "Camera", "Shift (held)", "Flying and nudging four times as fast"},
        {"hint_click", "Select", "Select", "Click", "An object, scenery (its group) or a gizmo; again on the same spot: the next under it"},
        {"hint_shift_click", "Add / remove", "Select", "Shift+click", "Adds to / removes from the selection"},
        {"hint_alt_click", "One piece", "Select", "Alt+click, middle click", "One piece of scenery, any piece"},
        {"hint_ctrl_click", "Remove a part", "Select", "Ctrl+click", "A part of the scenery selection removed"},
        {"hint_box", "Box select", "Select", "Drag from empty space", "Right: what's inside; left: what the box crosses; Shift adds, Ctrl removes"},
        {"hint_drag", "Move", "Transform", "Drag", "Shift: up / down; Ctrl or Snap 25: on a 25 grid; Esc puts it back"},
        {"hint_arrow", "Move along an axis", "Transform", "Drag an arrow", "Along that axis only"},
        {"hint_carry", "Leave what it carries", "Transform", "Alt while moving", "What stood on scenery stays (this move)"},
        {"hint_escape", "Cancel / close / leave", "Panels", "Esc", "A drag put back, a window closed, the selection dropped, then leaves"},
        {"hint_orbit", "Orbit", "Camera", "Alt+drag", "About the point looked at (UX1: level_editor_camera.h)"},
        {"hint_speed", "Flying speed", "Camera", "Right mouse + wheel", "The flying speed up / down while looking"},
        {"hint_fly", "Fly", "Camera", "Right mouse + W A S D Q E", "wasd_flies=0 (the default): the camera flies while the right button is held"},
        {"hint_snaps", "Snap while dragging", "Transform", "Ctrl / Ctrl+Shift / Alt+Shift / Ctrl+Alt",
         "The snap modifiers while a gizmo is dragged (level_editor_snap.h)"},
        {"hint_typed", "Typed values", "Transform", "X / Y / Z, digits (dragging)", "An axis and an amount typed during a drag; Enter applies"},
        {"hint_browser", "Browser keys", "Panels", "Up / Down, Left / Right, F, Enter", "Choose, scenery / objects, show me, place it"},
        {"hint_outliner", "Outliner", "Panels", "Click, double-click", "Selects; takes the camera there"},
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

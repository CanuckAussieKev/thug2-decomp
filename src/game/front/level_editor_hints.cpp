// NOT ORIGINAL - the level editor's status bar hints (ours). See level_editor_hints.h.

#include "game/front/level_editor_hints.h"

namespace Front::LevelEditor::Hints {

std::string FirstKey(const std::string& keysText)
{
    size_t at = keysText.find(" / ");
    return at == std::string::npos ? keysText : keysText.substr(0, at);
}

std::string JoinKeys(const std::vector<std::string>& keys)
{
    bool single = true;
    for (const std::string& k : keys)
        single = single && k.size() == 1;
    std::string out;
    for (const std::string& k : keys) {
        if (k.empty())
            continue;
        if (!out.empty() && !single)
            out += ' ';
        out += k;
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> ModifierHints(const std::vector<Actions::Action>& actions, uint8_t mods,
                                                               size_t limit)
{
    std::vector<std::pair<std::string, std::string>> out;
    if (!mods)
        return out;
    for (const Actions::Action& a : actions) {
        if (out.size() >= limit)
            break;
        if (a.kind != Actions::Kind::Press && a.kind != Actions::Kind::Toggle)
            continue;
        for (const Actions::Chord& c : a.keys) {
            if (c.scancode && c.mods == mods) {
                std::string text = Actions::FormatChord(c);
                if (!text.empty())
                    out.emplace_back(text, a.name);
                break;
            }
        }
    }
    return out;
}

std::vector<std::string> Tokens(const std::string& keys)
{
    std::vector<std::string> out;
    std::string current;
    for (size_t i = 0; i < keys.size(); ++i) {
        char c = keys[i];
        // ('+' separates tokens - unless it ends one with nothing after it: "Keypad+", or stands alone)
        if (c == '+' && !current.empty() && i + 1 < keys.size()) {
            out.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty())
        out.push_back(current);
    return out;
}

Glyph GlyphOf(const std::string& token)
{
    if (token == "LMB")
        return Glyph::MouseLeft;
    if (token == "RMB")
        return Glyph::MouseRight;
    if (token == "MMB")
        return Glyph::MouseMiddle;
    if (token == "Wheel")
        return Glyph::Wheel;
    if (token == "Mouse")
        return Glyph::Mouse;
    if (token == "2xLMB")
        return Glyph::DoubleClick;
    if (token == "PadA")
        return Glyph::PadA;
    if (token == "PadB")
        return Glyph::PadB;
    if (token == "PadX")
        return Glyph::PadX;
    if (token == "PadY")
        return Glyph::PadY;
    static const char* const kPad[] = {"LS", "RS", "LT", "RT", "LB", "RB", "DPad", "Back", "L3", "R3", "LT/RT", "LB/RB"};
    for (const char* p : kPad) {
        if (token == p)
            return Glyph::PadOther;
    }
    return Glyph::Key;
}

namespace {

struct Builder {
    const KeyText& keyOf;
    std::vector<Hint> out;
    void Raw(const std::string& keys, const std::string& what)
    {
        if (!keys.empty() && out.size() < kMaxHints)
            out.push_back({keys, what});
    }
    // An action's first key ("" unbound: left out).
    std::string Key(const char* id) const { return FirstKey(keyOf(id)); }
    void Act(const char* id, const std::string& what, const char* prefix = nullptr)
    {
        std::string k = Key(id);
        if (k.empty())
            return;
        Raw(prefix ? std::string(prefix) + "+" + k : k, what);
    }
    // Several actions' keys as one cap ("W", "A", "S", "D" -> "WASD").
    void Acts(std::initializer_list<const char*> ids, const std::string& what, const char* prefix = nullptr)
    {
        std::vector<std::string> keys;
        for (const char* id : ids) {
            std::string k = Key(id);
            if (!k.empty())
                keys.push_back(k);
        }
        if (keys.empty())
            return;
        std::string joined = JoinKeys(keys);
        Raw(prefix ? std::string(prefix) + "+" + joined : joined, what);
    }
    // The fly keys ("WASD" - with the right button unless wasd_flies=1).
    void Fly(const Context& c, const char* what)
    {
        Acts({"forward", "left", "back", "right"}, what, c.flyAlways || c.rmb ? nullptr : "RMB");
    }
};

void PadHints(const Context& c, Builder& b)
{
    if (c.pieOpen) {
        b.Raw("LS", "point at a slice");
        b.Raw("PadA", "run it");
        b.Raw("PadB", "close");
        return;
    }
    if (c.walking) {
        b.Raw("LS", "walk");
        b.Raw("RS", "look");
        b.Raw("PadA", "jump");
        b.Raw("L3", "run");
        b.Raw("PadB", "stop walking");
        return;
    }
    b.Raw("LS", "fly");
    b.Raw("RS", c.selection != Selection::None && !c.freeCamera ? "orbit" : "look");
    b.Raw("LT/RT", "down / up");
    b.Raw("PadA", c.drag != Drag::None ? "hold: drag" : "select");
    b.Raw("PadB", c.drag != Drag::None ? "put it back" : "back");
    b.Raw("PadX", "pie");
    b.Raw("PadY", "view pie");
    if (c.selection != Selection::None) {
        b.Raw("DPad", "nudge");
        b.Raw("LB/RB", "turn");
        b.Raw("R3", "frame");
    }
    b.Raw("Back", "undo");
}

} // namespace

std::vector<Hint> Build(const Context& c, const KeyText& keyOf)
{
    Builder b{keyOf, {}};
    if (c.pad) {
        PadHints(c, b);
        return b.out;
    }
    // A pie menu: the mouse points, a click or letting the key go runs it.
    if (c.pieOpen) {
        b.Raw("Mouse", "point at a slice");
        b.Raw("LMB", "run it");
        if (!c.pieSticky)
            b.Raw(b.Key("pie_context").empty() ? "Q" : b.Key("pie_context"), "release: run");
        b.Raw("RMB", "close");
        b.Raw("Esc", "close");
        return b.out;
    }
    // Walking (skater height, gravity).
    if (c.walking) {
        b.Acts({"forward", "left", "back", "right"}, "walk");
        b.Raw("Mouse", "look");
        b.Act("fly_up", "jump");
        b.Raw("Shift", "run");
        b.Act("walk_mode", "stop walking");
        b.Raw("Esc", "stop walking");
        return b.out;
    }
    if (c.ladderDrawing) {
        b.Raw("LMB", c.chainWaiting.empty() ? "place it" : c.chainWaiting);
        b.Act("undo", "undo last ladder");
        b.Raw("Esc", "stop");
        return b.out;
    }
    if (c.chainDrawing) {
        b.Raw("LMB", "add a point");
        b.Raw("Alt+LMB", "no edge snapping");
        b.Act("chain_finish", "finish");
        b.Raw("RMB", "finish");
        b.Act("undo", "undo last point");
        b.Raw("Esc", "stop");
        return b.out;
    }
    if (c.chainPick) {
        b.Raw("LMB", c.chainWaiting.empty() ? "pick" : c.chainWaiting);
        b.Raw("Esc", "cancel");
        return b.out;
    }
    if (c.placeArmed) {
        b.Raw("LMB", "place it");
        b.Raw("Shift+LMB", "place more");
        b.Raw("Esc", "cancel");
        b.Act("place", "close the palette");
        return b.out;
    }
    if (c.browser) {
        b.Raw("Up/Down", "choose");
        b.Raw("Left/Right", "scenery / objects");
        b.Raw("Enter", "place it");
        b.Raw("F", "show me");
        b.Act("browser", "close");
        b.Raw("Esc", "close");
        return b.out;
    }
    // Dragging.
    if (c.drag != Drag::None) {
        const char* apply = c.modal ? "LMB" : nullptr;
        if (!c.typed.empty()) {
            b.Raw("Typed", c.typed);
            b.Raw("Enter", "apply");
            b.Raw("Backspace", "erase");
            b.Raw("X/Y/Z", "axis");
            b.Raw("Esc", "put it back");
            return b.out;
        }
        if (c.drag == Drag::Rotate) {
            b.Raw("Mouse", "turns it");
            b.Raw("Ctrl", "steps");
            b.Raw("0-9", "degrees");
        } else if (c.drag == Drag::Scale) {
            b.Raw("Mouse", "scales it");
            b.Raw("Ctrl", "tenths");
            b.Raw("X/Y/Z", "axis");
            b.Raw("0-9", "a factor");
        } else if (c.drag == Drag::MoveAxis) {
            b.Raw("Mouse", "along the arrow");
            b.Raw("Ctrl", "grid");
            b.Raw("Ctrl+Shift", "world grid");
            b.Raw("0-9", "an amount");
        } else {
            b.Raw("Mouse", "move");
            b.Raw("Shift", "up / down");
            b.Raw("Wheel", "push / pull");
            b.Raw("Ctrl", "grid");
            b.Raw("Ctrl+Shift", "world grid");
            b.Raw("Alt+Shift", "surface");
            b.Raw("Ctrl+Alt", "vertex");
            b.Raw("X/Y/Z", "axis");
        }
        if (c.carried > 0)
            b.Raw("Alt", "leave the " + std::to_string(c.carried) + " carried");
        if (apply)
            b.Raw(apply, "apply");
        else
            b.Raw("LMB", "let go: done");
        b.Raw("Esc", "put it back");
        return b.out;
    }
    // Looking / flying with the right button.
    if (c.rmb) {
        b.Fly(c, "fly");
        b.Acts({"fly_down", "fly_up"}, "down / up");
        b.Raw("Shift", "faster");
        b.Raw("Mouse", c.selection != Selection::None && !c.freeCamera ? "orbit" : "look");
        b.Raw("Wheel", "fly speed");
        return b.out;
    }
    // Armed tools.
    if (c.rulerArmed) {
        b.Raw("LMB", "put an end");
        b.Raw("Alt+LMB", "no corner snap");
        b.Act("ruler_clear", "clear");
        b.Act("ruler", "ruler off");
        b.Raw("Esc", "deselect");
        return b.out;
    }
    if (c.ux9Tool == 1) {
        b.Raw("LMB", "pick gap end");
        b.Act("gap_wizard", "wizard off");
        b.Raw("Esc", "cancel");
        return b.out;
    }
    if (c.ux9Tool == 2) {
        b.Raw("LMB", "place corner");
        b.Act("ground_tool", "tool off");
        b.Raw("Esc", "cancel");
        return b.out;
    }
    if (c.facesMode) {
        b.Raw("LMB", "take a face");
        b.Raw("Shift+LMB", "add / drop a face");
        b.Act("faces", "faces mode off");
        return b.out;
    }
    if (c.boxArmed) {
        b.Raw("LMB", "drag a box");
        b.Raw("Shift", "add");
        b.Raw("Ctrl", "remove");
        return b.out;
    }
    if (c.simulating)
        b.Act("simulate", "stop simulating");
    // A modifier held: what it changes, then what it's bound to (the registry).
    if (c.mods) {
        bool selected = c.selection != Selection::None;
        if (c.mods == Actions::kAlt) {
            b.Raw("Alt+LMB", "one piece (any)");
            b.Raw("Alt+LMB", "drag: orbit");
            b.Raw("Alt+Wheel", "cycle under cursor");
        } else if (c.mods == Actions::kShift) {
            b.Raw("Shift+LMB", "add / remove");
            if (selected)
                b.Raw("Shift+LMB", "drag: up / down");
        } else if (c.mods == Actions::kCtrl) {
            if (c.selection == Selection::Scenery)
                b.Raw("Ctrl+LMB", "remove a part");
            b.Raw("Ctrl+LMB", "drag: remove box");
        }
        for (const auto& m : c.modded)
            b.Raw(m.first, m.second);
        return b.out;
    }
    switch (c.selection) {
    case Selection::None:
        b.Fly(c, "fly");
        b.Raw("LMB", "select");
        b.Raw("2xLMB", "go there");
        b.Raw("Wheel", "forward / back");
        b.Raw("Alt+Wheel", "drill through");
        b.Act("pie_context", "pie");
        b.Act("browser", "add");
        b.Act("place", "place");
        b.Act("walk_mode", "walk");
        b.Act("simulate", "simulate");
        b.Act("adjust_last", "adjust last");
        b.Act("play", "play");
        b.Act("command_search", "search");
        b.Act("help", "help");
        break;
    case Selection::OtherNode:
        b.Raw("LMB", "select another");
        b.Fly(c, "fly");
        b.Act("frame_selection", "frame");
        b.Act("follow_selection", c.following ? "stop following" : "follow");
        b.Act("pie_context", "pie");
        b.Raw("Esc", "deselect");
        b.Act("help", "help");
        break;
    default: {
        b.Raw("LMB", "drag: move");
        if (c.selection == Selection::Wall) {
            b.Act("delete", "collision off");
            b.Act("home", "put back");
            b.Raw("Esc", "deselect");
            break;
        }
        if (!c.flyAlways)
            b.Acts({"tool_move", "tool_rotate", "tool_scale"}, c.tool == 1 ? "tool (rotate)" : c.tool == 2 ? "tool (scale)" : "tool (move)");
        else
            b.Act("tool_cycle", "next tool");
        b.Fly(c, c.flyAlways ? "nudge" : "fly");
        b.Acts({"nudge_forward", "nudge_back", "nudge_left", "nudge_right"}, "nudge");
        if (c.chainPoint) {
            b.Act("lock", "whole chain");
            b.Act("chain_insert", "insert a point");
            b.Act("chain_extend", "draw on");
        } else {
            b.Acts({"turn_left", "turn_right"}, "turn");
        }
        b.Act("drop", "drop");
        b.Act("duplicate", "copy");
        b.Act("linked_duplicate", "linked copy");
        if (c.selection == Selection::Scenery)
            b.Act("edge_rail", "rail along an edge");
        b.Act("arrange_window", "arrange");
        b.Act("frame_selection", c.freeCamera ? "frame" : "frame");
        b.Act("follow_selection", c.following ? "stop following" : "follow");
        b.Act("pie_context", "pie");
        b.Act("delete", "delete");
        b.Raw("Esc", "deselect");
        break;
    }
    }
    return b.out;
}

} // namespace Front::LevelEditor::Hints

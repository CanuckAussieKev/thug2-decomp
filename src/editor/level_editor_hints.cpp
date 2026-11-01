// NOT ORIGINAL - the level editor's status bar hints (ours). See level_editor_hints.h.

#include "editor/level_editor_hints.h"
#include <array>

namespace frontend::level_editor::hints {

std::string leading_key_name(const std::string& keys_text)
{
    const size_t at = keys_text.find(" / ");
    return at == std::string::npos ? keys_text : keys_text.substr(0, at);
}

std::string join_keys(const std::vector<std::string>& keys)
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

std::vector<std::pair<std::string, std::string>> modifier_hints(const std::vector<actions::ActStep>& actions,
                                                                uint8_t mods, size_t limit)
{
    std::vector<std::pair<std::string, std::string>> out;
    if (!mods)
        return out;
    for (const actions::ActStep& a : actions) {
        if (out.size() >= limit)
            break;
        if (a.kind != actions::Kind::kPress && a.kind != actions::Kind::kFlipOver)
            continue;
        for (const actions::Chord& c : a.keys) {
            if (c.scancode && c.mods == mods) {
                const std::string text = actions::format_chord(c);
                if (!text.empty())
                    out.emplace_back(text, a.name);
                break;
            }
        }
    }
    return out;
}

std::vector<std::string> get_tokens(const std::string& keys)
{
    std::vector<std::string> out;
    std::string current;
    for (size_t i = 0; i < keys.size(); ++i) {
        const char c = keys[i];
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

Glyph glyph_of(const std::string& token)
{
    if (token == "LMB")
        return Glyph::kMouseLeft;
    if (token == "RMB")
        return Glyph::kMouseRight;
    if (token == "MMB")
        return Glyph::kMouseMiddle;
    if (token == "Wheel")
        return Glyph::kWheel;
    if (token == "Mouse")
        return Glyph::kMouse;
    if (token == "2xLMB")
        return Glyph::kDoubleClick;
    if (token == "PadA")
        return Glyph::kPadA;
    if (token == "PadB")
        return Glyph::kPadB;
    if (token == "PadX")
        return Glyph::kPadX;
    if (token == "PadY")
        return Glyph::kPadY;
    static const auto kPad = std::to_array<const char*>({"LS",   "RS",   "LT", "RT", "LB",    "RB",
                                       "DPad", "Back", "L3", "R3", "LT/RT", "LB/RB"});
    for (const char* p : kPad) {
        if (token == p)
            return Glyph::kPadOther;
    }
    return Glyph::kKey;
}

namespace {

struct Builder {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): a short-lived view of the caller's data
    const KeyText& key_of_;
    std::vector<Hint> out;
    void get_raw(const std::string& keys, const std::string& what)
    {
        if (!keys.empty() && out.size() < kMaxHints)
            out.push_back({keys, what});
    }
    // An action's first key ("" unbound: left out).
    std::string get_key(const char* id) const { return leading_key_name(key_of_(id)); }
    void act(const char* id, const std::string& what, const char* prefix = nullptr)
    {
        const std::string k = get_key(id);
        if (k.empty())
            return;
        get_raw(prefix ? std::string(prefix) + "+" + k : k, what);
    }
    // Several actions' keys as one cap ("W", "A", "S", "D" -> "WASD").
    void acts(std::initializer_list<const char*> ids, const std::string& what, const char* prefix = nullptr)
    {
        std::vector<std::string> keys;
        for (const char* id : ids) {
            const std::string k = get_key(id);
            if (!k.empty())
                keys.push_back(k);
        }
        if (keys.empty())
            return;
        const std::string joined = join_keys(keys);
        get_raw(prefix ? std::string(prefix) + "+" + joined : joined, what);
    }
    // The fly keys ("WASD" - with the right button unless wasd_flies=1).
    void get_fly(const CtxInfo& c, const char* what)
    {
        acts({"forward", "left", "back", "right"}, what, c.fly_always_ || c.rmb ? nullptr : "RMB");
    }
};

void pad_hints(const CtxInfo& c, Builder& b)
{
    if (c.pie_open_) {
        b.get_raw("LS", "point at a slice");
        b.get_raw("PadA", "run it");
        b.get_raw("PadB", "close");
        return;
    }
    if (c.walking) {
        b.get_raw("LS", "walk");
        b.get_raw("RS", "look");
        b.get_raw("PadA", "jump");
        b.get_raw("L3", "run");
        b.get_raw("PadB", "stop walking");
        return;
    }
    b.get_raw("LS", "fly");
    b.get_raw("RS", c.selection != Selection::kNone && !c.free_camera_ ? "orbit" : "look");
    b.get_raw("LT/RT", "down / up");
    b.get_raw("PadA", c.drag != Drag::kNone ? "hold: drag" : "select");
    b.get_raw("PadB", c.drag != Drag::kNone ? "put it back" : "back");
    b.get_raw("PadX", "pie");
    b.get_raw("PadY", "view pie");
    if (c.selection != Selection::kNone) {
        b.get_raw("DPad", "nudge");
        b.get_raw("LB/RB", "turn");
        b.get_raw("R3", "frame");
    }
    b.get_raw("Back", "undo");
}

} // namespace

std::vector<Hint> get_build(const CtxInfo& c, const KeyText& key_of)
{
    Builder b{key_of, {}};
    if (c.pad) {
        pad_hints(c, b);
        return b.out;
    }
    // A pie menu: the mouse points, a click or letting the key go runs it.
    if (c.pie_open_) {
        b.get_raw("Mouse", "point at a slice");
        b.get_raw("LMB", "run it");
        if (!c.pie_sticky_)
            b.get_raw(b.get_key("pie_context").empty() ? "Q" : b.get_key("pie_context"), "release: run");
        b.get_raw("RMB", "close");
        b.get_raw("Esc", "close");
        return b.out;
    }
    // Walking (skater height, gravity).
    if (c.walking) {
        b.acts({"forward", "left", "back", "right"}, "walk");
        b.get_raw("Mouse", "look");
        b.act("fly_up", "jump");
        b.get_raw("Shift", "run");
        b.act("walk_mode", "stop walking");
        b.get_raw("Esc", "stop walking");
        return b.out;
    }
    if (c.ladder_drawing_) {
        b.get_raw("LMB", c.chain_waiting_.empty() ? "place it" : c.chain_waiting_);
        b.act("undo", "undo last ladder");
        b.get_raw("Esc", "stop");
        return b.out;
    }
    if (c.chain_drawing_) {
        b.get_raw("LMB", "add a point");
        b.get_raw("Alt+LMB", "no edge snapping");
        b.act("chain_finish", "finish");
        b.get_raw("RMB", "finish");
        b.act("undo", "undo last point");
        b.get_raw("Esc", "stop");
        return b.out;
    }
    if (c.chain_pick_) {
        b.get_raw("LMB", c.chain_waiting_.empty() ? "pick" : c.chain_waiting_);
        b.get_raw("Esc", "cancel");
        return b.out;
    }
    if (c.place_armed_) {
        b.get_raw("LMB", "place it");
        b.get_raw("Shift+LMB", "place more");
        b.get_raw("Esc", "cancel");
        b.act("place", "close the palette");
        return b.out;
    }
    if (c.browser) {
        b.get_raw("Up/Down", "choose");
        b.get_raw("Left/Right", "scenery / objects");
        b.get_raw("Enter", "place it");
        b.get_raw("F", "show me");
        b.act("browser", "close");
        b.get_raw("Esc", "close");
        return b.out;
    }
    // Dragging.
    if (c.drag != Drag::kNone) {
        const char* apply = c.modal ? "LMB" : nullptr;
        if (!c.typed.empty()) {
            b.get_raw("Typed", c.typed);
            b.get_raw("Enter", "apply");
            b.get_raw("Backspace", "erase");
            b.get_raw("X/Y/Z", "axis");
            b.get_raw("Esc", "put it back");
            return b.out;
        }
        if (c.drag == Drag::kSpinBy) {
            b.get_raw("Mouse", "turns it");
            b.get_raw("Ctrl", "steps");
            b.get_raw("0-9", "degrees");
        } else if (c.drag == Drag::kScale) {
            b.get_raw("Mouse", "scales it");
            b.get_raw("Ctrl", "tenths");
            b.get_raw("X/Y/Z", "axis");
            b.get_raw("0-9", "a factor");
        } else if (c.drag == Drag::kMoveAxis) {
            b.get_raw("Mouse", "along the arrow");
            b.get_raw("Ctrl", "grid");
            b.get_raw("Ctrl+Shift", "world grid");
            b.get_raw("0-9", "an amount");
        } else {
            b.get_raw("Mouse", "move");
            b.get_raw("Shift", "up / down");
            b.get_raw("Wheel", "push / pull");
            b.get_raw("Ctrl", "grid");
            b.get_raw("Ctrl+Shift", "world grid");
            b.get_raw("Alt+Shift", "surface");
            b.get_raw("Ctrl+Alt", "vertex");
            b.get_raw("X/Y/Z", "axis");
        }
        if (c.carried > 0)
            b.get_raw("Alt", "leave the " + std::to_string(c.carried) + " carried");
        if (apply)
            b.get_raw(apply, "apply");
        else
            b.get_raw("LMB", "let go: done");
        b.get_raw("Esc", "put it back");
        return b.out;
    }
    // Looking / flying with the right button.
    if (c.rmb) {
        b.get_fly(c, "fly");
        b.acts({"fly_down", "fly_up"}, "down / up");
        b.get_raw("Shift", "faster");
        b.get_raw("Mouse", c.selection != Selection::kNone && !c.free_camera_ ? "orbit" : "look");
        b.get_raw("Wheel", "fly speed");
        return b.out;
    }
    // Armed tools.
    if (c.ruler_armed_) {
        b.get_raw("LMB", "put an end");
        b.get_raw("Alt+LMB", "no corner snap");
        b.act("ruler_clear", "clear");
        b.act("ruler", "ruler off");
        b.get_raw("Esc", "deselect");
        return b.out;
    }
    if (c.ux9_tool_ == 1) {
        b.get_raw("LMB", "pick gap end");
        b.act("gap_wizard", "wizard off");
        b.get_raw("Esc", "cancel");
        return b.out;
    }
    if (c.ux9_tool_ == 2) {
        b.get_raw("LMB", "place corner");
        b.act("ground_tool", "tool off");
        b.get_raw("Esc", "cancel");
        return b.out;
    }
    if (c.faces_mode_) {
        b.get_raw("LMB", "take a face");
        b.get_raw("Shift+LMB", "add / drop a face");
        b.act("faces", "faces mode off");
        return b.out;
    }
    if (c.box_armed_) {
        b.get_raw("LMB", "drag a box");
        b.get_raw("Shift", "add");
        b.get_raw("Ctrl", "remove");
        return b.out;
    }
    if (c.simulating)
        b.act("simulate", "stop simulating");
    // A modifier held: what it changes, then what it's bound to (the registry).
    if (c.mods) {
        const bool selected = c.selection != Selection::kNone;
        if (c.mods == actions::kAlt) {
            b.get_raw("Alt+LMB", "one piece (any)");
            b.get_raw("Alt+LMB", "drag: orbit");
            b.get_raw("Alt+Wheel", "cycle under cursor");
        } else if (c.mods == actions::kShift) {
            b.get_raw("Shift+LMB", "add / remove");
            if (selected)
                b.get_raw("Shift+LMB", "drag: up / down");
        } else if (c.mods == actions::kCtrl) {
            if (c.selection == Selection::kScenery)
                b.get_raw("Ctrl+LMB", "remove a part");
            b.get_raw("Ctrl+LMB", "drag: remove box");
        }
        for (const auto& m : c.modded)
            b.get_raw(m.first, m.second);
        return b.out;
    }
    switch (c.selection) {
    case Selection::kNone:
        b.get_fly(c, "fly");
        b.get_raw("LMB", "select");
        b.get_raw("2xLMB", "go there");
        b.get_raw("Wheel", "forward / back");
        b.get_raw("Alt+Wheel", "drill through");
        b.act("pie_context", "pie");
        b.act("browser", "add");
        b.act("place", "place");
        b.act("walk_mode", "walk");
        b.act("simulate", "simulate");
        b.act("adjust_last", "adjust last");
        b.act("play", "play");
        b.act("command_search", "search");
        b.act("help", "help");
        break;
    case Selection::kOtherNode:
        b.get_raw("LMB", "select another");
        b.get_fly(c, "fly");
        b.act("frame_selection", "frame");
        b.act("follow_selection", c.following ? "stop following" : "follow");
        b.act("pie_context", "pie");
        b.get_raw("Esc", "deselect");
        b.act("help", "help");
        break;
    default: {
        b.get_raw("LMB", "drag: move");
        if (c.selection == Selection::kWall) {
            b.act("delete", "collision off");
            b.act("home", "put back");
            b.get_raw("Esc", "deselect");
            break;
        }
        if (!c.fly_always_)
            b.acts({"tool_move", "tool_rotate", "tool_scale"}, c.tool == 1   ? "tool (rotate)"
                                                               : c.tool == 2 ? "tool (scale)"
                                                                             : "tool (move)");
        else
            b.act("tool_cycle", "next tool");
        b.get_fly(c, c.fly_always_ ? "nudge" : "fly");
        b.acts({"nudge_forward", "nudge_back", "nudge_left", "nudge_right"}, "nudge");
        if (c.chain_point_) {
            b.act("lock", "whole chain");
            b.act("chain_insert", "insert a point");
            b.act("chain_extend", "draw on");
        } else {
            b.acts({"turn_left", "turn_right"}, "turn");
        }
        b.act("drop", "drop");
        b.act("duplicate", "copy");
        b.act("linked_duplicate", "linked copy");
        if (c.selection == Selection::kScenery)
            b.act("edge_rail", "rail along an edge");
        b.act("arrange_window", "arrange");
        b.act("frame_selection", "frame");
        b.act("follow_selection", c.following ? "stop following" : "follow");
        b.act("pie_context", "pie");
        b.act("delete", "delete");
        b.get_raw("Esc", "deselect");
        break;
    }
    }
    return b.out;
}

} // namespace frontend::level_editor::hints

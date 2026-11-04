// NOT ORIGINAL - the level editor's status bar hints (ours). See level_editor_hints.h.

#include "editor/level_editor_hints.h"
#include "editor/level_editor_actions.h"
#include "base/core/checked_index.h"
#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace frontend::level_editor::hints {

std::string leading_key_name(const std::string& keys_text)
{
    const size_t at = keys_text.find(" / ");
    return at == std::string::npos ? keys_text : keys_text.substr(0, at);
}

std::string join_keys(const std::vector<std::string>& keys)
{
    bool single = true;
    for (const std::string& key : keys) {
        single = single && key.size() == 1;
    }
    std::string out;
    for (const std::string& key : keys) {
        if (key.empty()) {
            continue;
        }
        if (!out.empty() && !single) {
            out += ' ';
        }
        out += key;
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> modifier_hints(const std::vector<actions::ActStep>& actions, HintLimit hint_limit)
{
    const auto mods = hint_limit.mods;
    const auto limit = hint_limit.limit;
    std::vector<std::pair<std::string, std::string>> out;
    if (mods == 0U) {
        return out;
    }
    for (const actions::ActStep& action : actions) {
        if (out.size() >= limit) {
            break;
        }
        if (action.kind != actions::Kind::kPress && action.kind != actions::Kind::kFlipOver) {
            continue;
        }
        for (const actions::Chord& key : action.keys) {
            if ((key.scancode != 0) && key.mods == mods) {
                const std::string text = actions::format_chord(key);
                if (!text.empty()) {
                    out.emplace_back(text, action.name);
                }
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
        const char character = core::at(keys, i);
        // ('+' separates tokens - unless it ends one with nothing after it: "Keypad+", or stands alone)
        if (character == '+' && !current.empty() && i + 1 < keys.size()) {
            out.push_back(current);
            current.clear();
            continue;
        }
        current += character;
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

Glyph glyph_of(const std::string& token)
{
    if (token == "LMB") {
        return Glyph::kMouseLeft;
    }
    if (token == "RMB") {
        return Glyph::kMouseRight;
    }
    if (token == "MMB") {
        return Glyph::kMouseMiddle;
    }
    if (token == "Wheel") {
        return Glyph::kWheel;
    }
    if (token == "Mouse") {
        return Glyph::kMouse;
    }
    if (token == "2xLMB") {
        return Glyph::kDoubleClick;
    }
    if (token == "PadA") {
        return Glyph::kPadA;
    }
    if (token == "PadB") {
        return Glyph::kPadB;
    }
    if (token == "PadX") {
        return Glyph::kPadX;
    }
    if (token == "PadY") {
        return Glyph::kPadY;
    }
    static const auto kPad = std::to_array<const char*>({"LS",   "RS",   "LT", "RT", "LB",    "RB",
                                       "DPad", "Back", "L3", "R3", "LT/RT", "LB/RB"});
    for (const char* line : kPad) {
        if (token == line) {
            return Glyph::kPadOther;
        }
    }
    return Glyph::kKey;
}

namespace {

// The hints of one context as they are added (at most kMaxHints).
class Builder {
public:
    explicit Builder(const KeyText& key_of) : key_of_(&key_of) {}
    [[nodiscard]] const std::vector<Hint>& hints() const { return out_; }
    void get_raw(const std::string& keys, const std::string& what)
    {
        if (!keys.empty() && out_.size() < kMaxHints) {
            out_.push_back({.keys = keys, .what = what});
        }
    }
    // An action's first key ("" unbound: left out).
    std::string get_key(const char* id) const { return leading_key_name((*key_of_)(id)); }
    void act(const char* id, const std::string& what, const char* prefix = nullptr)
    {
        const std::string key_text = get_key(id);
        if (key_text.empty()) {
            return;
        }
        get_raw(prefix ? std::string(prefix) + "+" + key_text : key_text, what);
    }
    // Several actions' keys as one cap ("W", "A", "S", "D" -> "WASD").
    void acts(std::initializer_list<const char*> ids, const std::string& what, const char* prefix = nullptr)
    {
        std::vector<std::string> keys;
        for (const char* id : ids) {
            const std::string key_text = get_key(id);
            if (!key_text.empty()) {
                keys.push_back(key_text);
            }
        }
        if (keys.empty()) {
            return;
        }
        const std::string joined = join_keys(keys);
        get_raw(prefix ? std::string(prefix) + "+" + joined : joined, what);
    }
    // The fly keys ("WASD" - with the right button unless wasd_flies=1).
    void get_fly(const CtxInfo& context, const char* what)
    {
        acts({"forward", "left", "back", "right"}, what, context.fly_always_ || context.rmb ? nullptr : "RMB");
    }

private:
    const KeyText* key_of_; // the caller's, for the build
    std::vector<Hint> out_;
};

void pad_hints(const CtxInfo& context, Builder& b)
{
    if (context.pie_open_) {
        b.get_raw("LS", "point at a slice");
        b.get_raw("PadA", "run it");
        b.get_raw("PadB", "close");
        return;
    }
    if (context.walking) {
        b.get_raw("LS", "walk");
        b.get_raw("RS", "look");
        b.get_raw("PadA", "jump");
        b.get_raw("L3", "run");
        b.get_raw("PadB", "stop walking");
        return;
    }
    b.get_raw("LS", "fly");
    b.get_raw("RS", context.selection != Selection::kNone && !context.free_camera_ ? "orbit" : "look");
    b.get_raw("LT/RT", "down / up");
    b.get_raw("PadA", context.drag != Drag::kNone ? "hold: drag" : "select");
    b.get_raw("PadB", context.drag != Drag::kNone ? "put it back" : "back");
    b.get_raw("PadX", "pie");
    b.get_raw("PadY", "view pie");
    if (context.selection != Selection::kNone) {
        b.get_raw("DPad", "nudge");
        b.get_raw("LB/RB", "turn");
        b.get_raw("R3", "frame");
    }
    b.get_raw("Back", "undo");
}

// Dragging: a typed amount's keys, or the tool's modifiers; what lets it go.
void drag_hints(const CtxInfo& context, Builder& builder)
{
    const char* apply = context.modal ? "LMB" : nullptr;
    if (!context.typed.empty()) {
        builder.get_raw("Typed", context.typed);
        builder.get_raw("Enter", "apply");
        builder.get_raw("Backspace", "erase");
        builder.get_raw("X/Y/Z", "axis");
        builder.get_raw("Esc", "put it back");
        return;
    }
    if (context.drag == Drag::kSpinBy) {
        builder.get_raw("Mouse", "turns it");
        builder.get_raw("Ctrl", "steps");
        builder.get_raw("0-9", "degrees");
    } else if (context.drag == Drag::kScale) {
        builder.get_raw("Mouse", "scales it");
        builder.get_raw("Ctrl", "tenths");
        builder.get_raw("X/Y/Z", "axis");
        builder.get_raw("0-9", "a factor");
    } else if (context.drag == Drag::kMoveAxis) {
        builder.get_raw("Mouse", "along the arrow");
        builder.get_raw("Ctrl", "grid");
        builder.get_raw("Ctrl+Shift", "world grid");
        builder.get_raw("0-9", "an amount");
    } else {
        builder.get_raw("Mouse", "move");
        builder.get_raw("Shift", "up / down");
        builder.get_raw("Wheel", "push / pull");
        builder.get_raw("Ctrl", "grid");
        builder.get_raw("Ctrl+Shift", "world grid");
        builder.get_raw("Alt+Shift", "surface");
        builder.get_raw("Ctrl+Alt", "vertex");
        builder.get_raw("X/Y/Z", "axis");
    }
    if (context.carried > 0) {
        builder.get_raw("Alt", "leave the " + std::to_string(context.carried) + " carried");
    }
    if (apply) {
        builder.get_raw(apply, "apply");
    } else {
        builder.get_raw("LMB", "let go: done");
    }
    builder.get_raw("Esc", "put it back");
}

// A modifier held: what it changes, then what it's bound to (the registry).
void modifier_hints(const CtxInfo& context, Builder& builder)
{
    const bool selected = context.selection != Selection::kNone;
    if (context.mods == actions::kAlt) {
        builder.get_raw("Alt+LMB", "one piece (any)");
        builder.get_raw("Alt+LMB", "drag: orbit");
        builder.get_raw("Alt+Wheel", "cycle under cursor");
    } else if (context.mods == actions::kShift) {
        builder.get_raw("Shift+LMB", "add / remove");
        if (selected) {
            builder.get_raw("Shift+LMB", "drag: up / down");
        }
    } else if (context.mods == actions::kCtrl) {
        if (context.selection == Selection::kScenery) {
            builder.get_raw("Ctrl+LMB", "remove a part");
        }
        builder.get_raw("Ctrl+LMB", "drag: remove box");
    }
    for (const auto& mod : context.modded) {
        builder.get_raw(mod.first, mod.second);
    }
}

// Something selected: the tool, nudging and turning (a chain point's own), copying, framing, deleting.
void selection_hints(const CtxInfo& context, Builder& builder)
{
    builder.get_raw("LMB", "drag: move");
    if (context.selection == Selection::kWall) {
        builder.act("delete", "collision off");
        builder.act("home", "put back");
        builder.get_raw("Esc", "deselect");
        return;
    }
    if (!context.fly_always_) {
        const char* tool = "tool (move)";
        if (context.tool == 1) {
            tool = "tool (rotate)";
        } else if (context.tool == 2) {
            tool = "tool (scale)";
        }
        builder.acts({"tool_move", "tool_rotate", "tool_scale"}, tool);
    } else {
        builder.act("tool_cycle", "next tool");
    }
    builder.get_fly(context, context.fly_always_ ? "nudge" : "fly");
    builder.acts({"nudge_forward", "nudge_back", "nudge_left", "nudge_right"}, "nudge");
    if (context.chain_point_) {
        builder.act("lock", "whole chain");
        builder.act("chain_insert", "insert a point");
        builder.act("chain_extend", "draw on");
    } else {
        builder.acts({"turn_left", "turn_right"}, "turn");
    }
    builder.act("drop", "drop");
    builder.act("duplicate", "copy");
    builder.act("linked_duplicate", "linked copy");
    if (context.selection == Selection::kScenery) {
        builder.act("edge_rail", "rail along an edge");
    }
    builder.act("arrange_window", "arrange");
    builder.act("frame_selection", "frame");
    builder.act("follow_selection", context.following ? "stop following" : "follow");
    builder.act("pie_context", "pie");
    builder.act("delete", "delete");
    builder.get_raw("Esc", "deselect");
}

} // namespace

std::vector<Hint> get_build(const CtxInfo& context, const KeyText& key_of)
{
    Builder builder(key_of);
    if (context.pad) {
        pad_hints(context, builder);
        return builder.hints();
    }
    // A pie menu: the mouse points, a click or letting the key go runs it.
    if (context.pie_open_) {
        builder.get_raw("Mouse", "point at a slice");
        builder.get_raw("LMB", "run it");
        if (!context.pie_sticky_) {
            builder.get_raw(builder.get_key("pie_context").empty() ? "Q" : builder.get_key("pie_context"),
                            "release: run");
        }
        builder.get_raw("RMB", "close");
        builder.get_raw("Esc", "close");
        return builder.hints();
    }
    // Walking (skater height, gravity).
    if (context.walking) {
        builder.acts({"forward", "left", "back", "right"}, "walk");
        builder.get_raw("Mouse", "look");
        builder.act("fly_up", "jump");
        builder.get_raw("Shift", "run");
        builder.act("walk_mode", "stop walking");
        builder.get_raw("Esc", "stop walking");
        return builder.hints();
    }
    if (context.ladder_drawing_) {
        builder.get_raw("LMB", context.chain_waiting_.empty() ? "place it" : context.chain_waiting_);
        builder.act("undo", "undo last ladder");
        builder.get_raw("Esc", "stop");
        return builder.hints();
    }
    if (context.chain_drawing_) {
        builder.get_raw("LMB", "add a point");
        builder.get_raw("Alt+LMB", "no edge snapping");
        builder.act("chain_finish", "finish");
        builder.get_raw("RMB", "finish");
        builder.act("undo", "undo last point");
        builder.get_raw("Esc", "stop");
        return builder.hints();
    }
    if (context.chain_pick_) {
        builder.get_raw("LMB", context.chain_waiting_.empty() ? "pick" : context.chain_waiting_);
        builder.get_raw("Esc", "cancel");
        return builder.hints();
    }
    if (context.place_armed_) {
        builder.get_raw("LMB", "place it");
        builder.get_raw("Shift+LMB", "place more");
        builder.get_raw("Esc", "cancel");
        builder.act("place", "close the palette");
        return builder.hints();
    }
    if (context.browser) {
        builder.get_raw("Up/Down", "choose");
        builder.get_raw("Left/Right", "scenery / objects");
        builder.get_raw("Enter", "place it");
        builder.get_raw("F", "show me");
        builder.act("browser", "close");
        builder.get_raw("Esc", "close");
        return builder.hints();
    }
    // Dragging.
    if (context.drag != Drag::kNone) {
        drag_hints(context, builder);
        return builder.hints();
    }
    // Looking / flying with the right button.
    if (context.rmb) {
        builder.get_fly(context, "fly");
        builder.acts({"fly_down", "fly_up"}, "down / up");
        builder.get_raw("Shift", "faster");
        builder.get_raw("Mouse", context.selection != Selection::kNone && !context.free_camera_ ? "orbit" : "look");
        builder.get_raw("Wheel", "fly speed");
        return builder.hints();
    }
    // Armed tools.
    if (context.ruler_armed_) {
        builder.get_raw("LMB", "put an end");
        builder.get_raw("Alt+LMB", "no corner snap");
        builder.act("ruler_clear", "clear");
        builder.act("ruler", "ruler off");
        builder.get_raw("Esc", "deselect");
        return builder.hints();
    }
    if (context.ux9_tool_ == 1) {
        builder.get_raw("LMB", "pick gap end");
        builder.act("gap_wizard", "wizard off");
        builder.get_raw("Esc", "cancel");
        return builder.hints();
    }
    if (context.ux9_tool_ == 2) {
        builder.get_raw("LMB", "place corner");
        builder.act("ground_tool", "tool off");
        builder.get_raw("Esc", "cancel");
        return builder.hints();
    }
    if (context.faces_mode_) {
        builder.get_raw("LMB", "take a face");
        builder.get_raw("Shift+LMB", "add / drop a face");
        builder.act("faces", "faces mode off");
        return builder.hints();
    }
    if (context.box_armed_) {
        builder.get_raw("LMB", "drag a box");
        builder.get_raw("Shift", "add");
        builder.get_raw("Ctrl", "remove");
        return builder.hints();
    }
    if (context.simulating) {
        builder.act("simulate", "stop simulating");
    }
    // A modifier held: what it changes, then what it's bound to (the registry).
    if (context.mods != 0U) {
        modifier_hints(context, builder);
        return builder.hints();
    }
    switch (context.selection) {
    case Selection::kNone:
        builder.get_fly(context, "fly");
        builder.get_raw("LMB", "select");
        builder.get_raw("2xLMB", "go there");
        builder.get_raw("Wheel", "forward / back");
        builder.get_raw("Alt+Wheel", "drill through");
        builder.act("pie_context", "pie");
        builder.act("browser", "add");
        builder.act("place", "place");
        builder.act("walk_mode", "walk");
        builder.act("simulate", "simulate");
        builder.act("adjust_last", "adjust last");
        builder.act("play", "play");
        builder.act("command_search", "search");
        builder.act("help", "help");
        break;
    case Selection::kOtherNode:
        builder.get_raw("LMB", "select another");
        builder.get_fly(context, "fly");
        builder.act("frame_selection", "frame");
        builder.act("follow_selection", context.following ? "stop following" : "follow");
        builder.act("pie_context", "pie");
        builder.get_raw("Esc", "deselect");
        builder.act("help", "help");
        break;
    default: {
        selection_hints(context, builder);
        break;
    }
    }
    return builder.hints();
}

} // namespace frontend::level_editor::hints

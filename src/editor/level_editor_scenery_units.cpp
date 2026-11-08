// NOT ORIGINAL - the level editor's scenery units: what a click, a hover and a box select take (a piece, or the groups
// it is in), the pick mode, the drill-in, and the selection commands beside them (ours). See level_editor_scenery.h.

#include "editor/level_editor_scenery.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_nav.h"
#include "editor/level_editor_organize.h"
#include "platform/run_log.h"
#include "platform/sdl_time.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor_scenery_internal.h"

namespace frontend::level_editor::scenery {
// What the source file's anonymous namespace held that its pieces share (level_editor_scenery_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::scenery

namespace frontend::level_editor::scenery {
namespace {

constexpr uint32_t kAutoGroupsRefreshMs = 1000; // the automatic groups are worked out again at most this often
constexpr uint64_t kKeyMix = 1000003U;          // (a prime: the hover's key mixes its parts with it)

// NOT ORIGINAL grouping - the units' state: the pick mode, an armed drill-in, the automatic groups as last worked out,
// the actions. Made on first use, never destroyed (units_state()).
struct UnitsState {
    bool mode_read_ = false;
    bool pieces_ = false;
    bool drill_armed_ = false;
    uint32_t drill_ = 0;
    std::vector<AutoGroup> autos_;
    bool autos_have_ = false;
    uint32_t autos_at_ = 0;
    uint64_t autos_key_ = 0;
    int act_touching_ = -1;
    int act_cluster_ = -1;
    int act_ungroup_ = -1;
    int act_pick_mode_ = -1;
};

UnitsState& units_state()
{
    static core::NoDestroy<UnitsState> state;
    return state.get();
}

// Whether every piece of `part` that isn't a shadow is in `whole` (and there is one): a shadow follows whichever unit
// took it, so units are compared by their casters - a part of shadows only (a shadow clicked by itself) by those.
bool inside(std::span<const uint32_t> part, const std::vector<uint32_t>& whole)
{
    const bool casters = std::ranges::any_of(part, [](uint32_t member) { return !is_shadow(member); });
    bool any = false;
    for (const uint32_t member : part) {
        if (casters && is_shadow(member)) {
            continue;
        }
        if (std::ranges::find(whole, member) == whole.end()) {
            return false;
        }
        any = true;
    }
    return any;
}

bool same_unit(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) { return inside(a, b) && inside(b, a); }

// The Outliner's groups (quick sets left out) that hold `checksum`: their scenery members there now, largest first.
std::vector<Unit> outliner_units(uint32_t checksum)
{
    std::vector<Unit> found;
    for (const organize::GroupSet& group : organize::at_hand().groups) {
        if (nav::is_quick_set(group.name)) { // (UX8's quick selection sets: stored selections, not units)
            continue;
        }
        Unit unit{.sectors = {}, .name = group.name, .kind = "group"};
        bool holds = false;
        for (const organize::Member& member : group.members) {
            OrientedBox box{};
            if (member.kind == 's' && cuboid(member.id, &box) && why_not_movable(member.id) == nullptr &&
                std::ranges::find(unit.sectors, member.id) == unit.sectors.end()) {
                unit.sectors.push_back(member.id);
                holds = holds || member.id == checksum;
            }
        }
        if (holds) {
            found.push_back(std::move(unit));
        }
    }
    std::ranges::stable_sort(found, [](const Unit& a, const Unit& b) { return a.sectors.size() > b.sectors.size(); });
    return found;
}

// The outermost unit of a piece, without its shadows (box select).
std::vector<uint32_t> outer_unit(uint32_t checksum)
{
    std::vector<Unit> groups = outliner_units(checksum);
    if (!groups.empty()) {
        return std::move(groups.front().sectors);
    }
    std::string name;
    std::vector<uint32_t> unit = pack_unit(checksum, &name);
    if (unit.size() < 2) {
        unit = family_unit(checksum, &name);
    }
    if (unit.size() < 2) {
        unit = {checksum};
    }
    return unit;
}

// A number that moves when what a click takes may have changed without the hovered piece or the selection changing:
// the pick mode, the Outliner's groups, the undo history (moves, copies, placements).
uint64_t units_key()
{
    uint64_t key = pick_pieces() ? 1U : 0U;
    for (const organize::GroupSet& group : organize::at_hand().groups) {
        key = (key * kKeyMix) + group.members.size() + group.name.size();
    }
    key = (key * kKeyMix) + edit::top_serial();
    return (key * kKeyMix) + get_clones().size();
}

} // namespace

bool pick_pieces()
{
    UnitsState& state = units_state();
    if (!state.mode_read_) {
        state.mode_read_ = true;
        state.pieces_ = config_int("editor", "pick_pieces", 0) != 0;
    }
    return state.pieces_;
}

void set_pick_pieces(bool pieces)
{
    pick_pieces();
    UnitsState& state = units_state();
    if (state.pieces_ == pieces) {
        return;
    }
    state.pieces_ = pieces;
    config_set_int("editor", "pick_pieces", pieces ? 1 : 0);
    scenery_hover().hover_group_of_ = 0;
    show_message(pieces ? "Pick: Pieces - a click selects one piece" : "Pick: Groups - a click selects the group; click again for one piece");
}

std::vector<Unit> units_of(uint32_t checksum)
{
    std::vector<Unit> chain;
    if (checksum == 0U || why_not_movable(checksum)) {
        return chain;
    }
    for (Unit& unit : outliner_units(checksum)) {
        unit.sectors = with_shadows(unit.sectors);
        chain.push_back(std::move(unit));
    }
    Unit pack{.sectors = {}, .name = {}, .kind = "prop pack"};
    pack.sectors = pack_unit(checksum, &pack.name);
    if (pack.sectors.size() > 1) {
        pack.sectors = with_shadows(pack.sectors);
        chain.push_back(std::move(pack));
    }
    Unit family{.sectors = {}, .name = {}, .kind = "family"};
    family.sectors = family_unit(checksum, &family.name);
    if (family.sectors.size() > 1) {
        family.sectors = with_shadows(family.sectors);
        chain.push_back(std::move(family));
    }
    chain.push_back({.sectors = with_shadows({checksum}), .name = caption(checksum), .kind = "piece"});
    // (each nests in the one before - a unit that doesn't, or adds nothing, is left out; the piece always ends it)
    std::vector<Unit> nested;
    for (size_t i = 0; i < chain.size(); ++i) {
        Unit& unit = core::at(chain, static_cast<int>(i));
        const bool last = i + 1 == chain.size();
        if (nested.empty() ||
            (inside(unit.sectors, nested.back().sectors) && !same_unit(unit.sectors, nested.back().sectors))) {
            nested.push_back(std::move(unit));
        } else if (last) {
            nested.back() = std::move(unit); // (a group of this one piece: the piece)
        }
    }
    return nested;
}

namespace internal {

std::vector<uint32_t> click_takes(uint32_t checksum, bool say)
{
    std::vector<Unit> chain = units_of(checksum);
    if (chain.empty()) {
        return {};
    }
    if (pick_pieces()) {
        return std::move(chain.back().sectors);
    }
    // (Unity's drill-in: the selection inside one of the piece's units - the next one in; else the outermost)
    const std::vector<uint32_t>& selection = get_selection();
    for (size_t k = chain.size(); k-- > 0;) {
        if (inside(selection, core::at(chain, static_cast<int>(k)).sectors)) {
            const size_t next = std::min(k + 1, chain.size() - 1);
            Unit& unit = core::at(chain, static_cast<int>(next));
            if (say && std::string_view(unit.kind) != "piece") {
                show_message(std::string(unit.kind) + " " + unit.name + " (" + std::to_string(unit.sectors.size()) +
                             ") - click again for one piece");
            }
            return std::move(unit.sectors);
        }
    }
    Unit& outer = chain.front();
    if (say && chain.size() > 1) {
        show_message(std::string(outer.kind) + " " + outer.name + " (" + std::to_string(outer.sectors.size()) +
                     ") - click again for one piece, Alt+click for one");
    }
    if (say && debug_enabled()) {
        for (const Unit& unit : chain) {
            get_log("level editor:   unit %s %s: %u sectors", unit.kind, unit.name.c_str(),
                    static_cast<unsigned>(unit.sectors.size()));
        }
    }
    return std::move(outer.sectors);
}

} // namespace internal

bool would_drill(uint32_t checksum)
{
    // (what a click would take: hover_group's - the same units, kept)
    return !pick_pieces() && get_selected(checksum) && !same_unit(hover_group(checksum), get_selection());
}

bool arm_drill(uint32_t checksum)
{
    UnitsState& state = units_state();
    state.drill_armed_ = false;
    if (!would_drill(checksum)) {
        return false;
    }
    state.drill_armed_ = true;
    state.drill_ = checksum;
    return true;
}

void end_press(bool dragged)
{
    UnitsState& state = units_state();
    if (!state.drill_armed_) {
        return;
    }
    state.drill_armed_ = false;
    if (!dragged && get_selected(state.drill_)) {
        select_group(state.drill_);
    }
}

const std::vector<uint32_t>& hover_group(uint32_t checksum)
{
    SceneryHover& hover = scenery_hover();
    const uint64_t key = units_key();
    if (checksum != hover.hover_group_of_ || key != hover.hover_key_ || hover.hover_selection_ != get_selection()) {
        hover.hover_group_of_ = checksum;
        hover.hover_key_ = key;
        hover.hover_selection_ = get_selection();
        hover.hover_group_ = (checksum != 0U) ? click_takes(checksum) : std::vector<uint32_t>{};
    }
    return hover.hover_group_;
}

void add_group(uint32_t checksum)
{
    std::vector<uint32_t> next = get_selection();
    for (const uint32_t member : click_takes(checksum)) {
        if (std::ranges::find(next, member) == next.end()) {
            next.push_back(member);
        }
    }
    set_selection(next);
}

std::vector<uint32_t> box_units(const std::vector<uint32_t>& found, bool crossing)
{
    if (pick_pieces()) {
        return with_shadows(found);
    }
    std::vector<uint32_t> taken;
    std::vector<uint32_t> covered;
    for (const uint32_t checksum : found) {
        if (std::ranges::find(covered, checksum) != covered.end()) {
            continue;
        }
        const std::vector<uint32_t> unit = outer_unit(checksum);
        const bool whole =
            std::ranges::all_of(unit, [&](uint32_t member) { return std::ranges::find(found, member) != found.end(); });
        for (const uint32_t member : unit) {
            covered.push_back(member);
            if ((crossing || whole) && std::ranges::find(taken, member) == taken.end()) {
                taken.push_back(member);
            }
        }
    }
    return with_shadows(taken);
}

const std::vector<AutoGroup>& auto_groups()
{
    UnitsState& state = units_state();
    const uint32_t now = platform::ticks_ms();
    const uint64_t key = (edit::top_serial() * kKeyMix) + get_clones().size();
    if (!state.autos_have_ || key != state.autos_key_ || now - state.autos_at_ > kAutoGroupsRefreshMs) {
        state.autos_have_ = true;
        state.autos_key_ = key;
        state.autos_at_ = now;
        state.autos_ = find_auto_groups();
    }
    return state.autos_;
}

void select_touching()
{
    const size_t before = get_selection().size();
    if (before == 0) {
        show_message("Select scenery first");
        return;
    }
    SelectionIds ids;
    ids.sectors = touching_of(get_selection());
    level_editor::set_selection(ids);
    show_message("Select Touching: " + std::to_string(get_selection().size()) + " pieces (" +
                 std::to_string(get_selection().size() - std::min(get_selection().size(), before)) + " more)");
}

void select_cluster()
{
    const std::vector<uint32_t>& before = get_selection();
    if (before.empty()) {
        show_message("Select scenery first");
        return;
    }
    if (std::ranges::none_of(before, [](uint32_t checksum) { return cluster_of(checksum) != 0U; })) {
        show_message("Not part of a trick cluster");
        return;
    }
    SelectionIds ids;
    ids.sectors = cluster_members_of(before);
    level_editor::set_selection(ids);
    show_message("Select Cluster: " + std::to_string(get_selection().size()) + " pieces");
}

std::string describe_units(uint32_t checksum)
{
    std::string text = "units:";
    for (const Unit& unit : units_of(checksum)) {
        text += " " + std::string(unit.kind) + " " + unit.name + " (" + std::to_string(unit.sectors.size()) + ");";
    }
    text += " a click takes " + std::to_string(hover_group(checksum).size());
    // (the piece's own rails: it selected for a moment, the selection put back)
    const std::vector<uint32_t> before = get_selection();
    set_selection(with_shadows({checksum}));
    text += "; the piece's rails " + std::to_string(selected_rail_names().size());
    set_selection(before);
    text += "; the old inference (cluster, then touching) " +
            std::to_string(touching_of(cluster_members_of({checksum})).size());
    return text;
}

namespace {

actions::ActStep make_action(const char* id, const char* name, const char* category, std::vector<actions::Chord> keys,
                             const char* tip)
{
    actions::ActStep act_step;
    act_step.id = id;
    act_step.name = name;
    act_step.category = category;
    act_step.kind = actions::Kind::kPress;
    act_step.defaults = keys;
    act_step.keys = std::move(keys);
    act_step.tip = tip;
    return act_step;
}

} // namespace

void prepare_units()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    UnitsState& state = units_state();
    state.act_touching_ = actions::enrol(make_action("select_touching", "Select Touching", "Select", {},
                                                     "Add the pieces touching the selected scenery"));
    state.act_cluster_ = actions::enrol(make_action("select_cluster", "Select Cluster", "Select", {},
                                                    "Add the rest of the selected scenery's trick clusters"));
    // (Ungroup: Ctrl+Shift+G, as most editors have it; Create Prefab moved to Ctrl+Alt+G - level_editor_prefabs.cpp)
    state.act_ungroup_ = actions::enrol(make_action("ungroup", "Ungroup", "Select",
                                                    {{.scancode = SDL_SCANCODE_G, .mods = actions::kCtrl | actions::kShift}},
                                                    "Remove the groups the selection belongs to"));
    actions::ActStep pick_mode = make_action("pick_units", "Pick Single Pieces", "Select", {},
                                             "A click selects one piece, not its group (the toolbar's Groups / Pieces)");
    pick_mode.checked = [] { return pick_pieces() ? 1 : 0; };
    state.act_pick_mode_ = actions::enrol(pick_mode);
    // (the hint row for clicks says what a click takes now)
    actions::ActStep hint = make_action("hint_drill", "Select Piece in Group", "Select", {},
                                        "Click a selected group again to select the piece under the cursor");
    hint.kind = actions::Kind::kDetailsOf;
    hint.keys_text_ = "Click again";
    hint.rebindable = false;
    actions::enrol(hint);
}

void tick_units()
{
    const UnitsState& state = units_state();
    const bool busy = change_under_way();
    if (actions::squeezed(state.act_touching_) && !busy) {
        select_touching();
    }
    if (actions::squeezed(state.act_cluster_) && !busy) {
        select_cluster();
    }
    if (actions::squeezed(state.act_ungroup_) && !busy) {
        organize::ungroup_selection();
    }
    if (actions::squeezed(state.act_pick_mode_)) {
        set_pick_pieces(!pick_pieces());
    }
}

} // namespace frontend::level_editor::scenery

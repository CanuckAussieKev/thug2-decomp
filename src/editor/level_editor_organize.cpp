// NOT ORIGINAL - the level editor's organising: the Outliner's per-row toggles, groups and labels, type filters, box
// select, select similar, layout snapshots (ours). See level_editor_organize.h.

#include "editor/level_editor_organize.h"

#include <cstddef>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <array>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_view.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/maths/vec2.h"
#include <span>

namespace frontend::level_editor::organize {

// --- the pure parts --------------------------------------------------------------------------------------------------

namespace {

constexpr std::size_t kHexTextSize = 16; // an 8-digit hex id
constexpr std::size_t kHexDigits = 8;
constexpr std::size_t kMemberTextLength = 10; // "<o|s|n>:<8 hex digits>"
constexpr std::size_t kMaxGroupNameLength = 40; // the longest group name accepted
constexpr float kSizeTolerance = 0.5F;          // two boxes this close in every axis count as the same size
constexpr std::size_t kMaxSetNameLength = 32; // the longest saved selection name
constexpr std::size_t kBoxStatusSize = 160; // the box select's status line
constexpr float kFarthest = 1e30F;          // the nearest-node search starts from this distance
constexpr float kBoxDragThreshold = 6.0F;   // pixels the mouse moves before a click becomes a box drag

std::string get_lower(std::string text)
{
    for (char& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string hex_text(uint32_t value)
{
    std::array<char, kHexTextSize> text{};
    text::format_to(text, "%08X", value);
    return text.data();
}

bool parse_hex8(const std::string& text, uint32_t* value)
{
    if (text.size() != kHexDigits) {
        return false;
    }
    for (const char character : text) {
        if (std::isxdigit(static_cast<unsigned char>(character)) == 0) {
            return false;
        }
    }
    *value = static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, kHexBase));
    return true;
}

bool same_groups(const std::vector<GroupSet>& a, const std::vector<GroupSet>& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (core::at(a, i).name != core::at(b, i).name || core::at(a, i).members != core::at(b, i).members) {
            return false;
        }
    }
    return true;
}

} // namespace

bool same_state(const State& a, const State& b)
{
    return same_groups(a.groups, b.groups) && a.labels == b.labels && a.locked == b.locked &&
           a.unpickable == b.unpickable && a.snapshot == b.snapshot;
}

std::string write_records(const State& state)
{
    std::string out;
    for (const GroupSet& group : state.groups) {
        out += "group " + fields::escape(group.name) + " ";
        if (group.members.empty()) {
            out += "-";
        }
        for (size_t i = 0; i < group.members.size(); ++i) {
            out += (i != 0U) ? "," : "";
            out += core::at(group.members, i).kind;
            out += ":" + hex_text(core::at(group.members, i).id);
        }
        out += "\n";
    }
    for (const auto& label : state.labels) {
        out += "label " + hex_text(label.first) + " " + fields::escape(label.second) + "\n";
    }
    for (const uint32_t id : state.locked) {
        out += "edlock " + hex_text(id) + "\n";
    }
    for (const uint32_t id : state.unpickable) {
        out += "nopick " + hex_text(id) + "\n";
    }
    if (!state.snapshot.empty()) {
        out += "snapshot " + state.snapshot + "\n";
    }
    return out;
}

bool is_record(const std::string& keyword)
{
    return keyword == "group" || keyword == "label" || keyword == "edlock" || keyword == "nopick" ||
           keyword == "snapshot";
}

bool read_record(const std::string& line, State* state)
{
    std::istringstream in(line);
    std::string keyword;
    std::string first;
    std::string second;
    if (!(in >> keyword) || !is_record(keyword) || !(in >> first)) {
        return false;
    }
    if (keyword == "group") {
        GroupSet group_set;
        std::string name;
        if (!(in >> second) || !fields::unescape(first, &name) || !clean_name(name, &group_set.name)) {
            return false;
        }
        if (second != "-") {
            std::istringstream list(second);
            std::string item;
            while (std::getline(list, item, ',')) {
                Member member;
                if (item.size() != kMemberTextLength || core::at(item, 1) != ':' ||
                    (core::at(item, 0) != 'o' && core::at(item, 0) != 's' && core::at(item, 0) != 'n') ||
                    !parse_hex8(item.substr(2), &member.id)) {
                    return false;
                }
                member.kind = core::at(item, 0);
                if (std::ranges::find(group_set.members, member) == group_set.members.end()) {
                    group_set.members.push_back(member);
                }
            }
        }
        for (const GroupSet& other : state->groups) {
            if (other.name == group_set.name) {
                return false;
            }
        }
        state->groups.push_back(group_set);
        return true;
    }
    if (keyword == "snapshot") {
        if (!valid_snapshot_name(first)) {
            return false;
        }
        state->snapshot = first;
        return true;
    }
    uint32_t id = 0;
    if (!parse_hex8(first, &id)) {
        return false;
    }
    if (keyword == "label") {
        std::string text;
        std::string clean;
        if (!(in >> second) || !fields::unescape(second, &text) || !clean_name(text, &clean)) {
            return false;
        }
        set_label(state, id, clean);
        return true;
    }
    set_flag(keyword == "edlock" ? &state->locked : &state->unpickable, id, true);
    return true;
}

bool clean_name(const std::string& text, std::string* out)
{
    const size_t first = text.find_first_not_of(" \t");
    const size_t last = text.find_last_not_of(" \t");
    if (first == std::string::npos) {
        return false;
    }
    const std::string name = text.substr(first, last - first + 1);
    if (name.size() > kMaxGroupNameLength) {
        return false;
    }
    for (const char character : name) {
        if (static_cast<unsigned char>(character) < kFirstPrintableChar || character == kDeleteChar) {
            return false;
        }
    }
    *out = name;
    return true;
}

std::string get_label_of(const State& state, uint32_t id)
{
    for (const auto& label : state.labels) {
        if (label.first == id) {
            return label.second;
        }
    }
    return "";
}

void set_label(State* state, uint32_t id, const std::string& text)
{
    auto it = std::ranges::find_if(state->labels, [&](const auto& label) { return label.first == id; });
    if (text.empty()) {
        if (it != state->labels.end()) {
            state->labels.erase(it);
        }
        return;
    }
    if (it != state->labels.end()) {
        it->second = text;
    } else {
        state->labels.emplace_back(id, text);
    }
}

bool get_has(std::span<const uint32_t> list, uint32_t id) { return std::ranges::find(list, id) != list.end(); }

void set_flag(std::vector<uint32_t>* list, uint32_t id, bool on)
{
    auto it = std::ranges::find(*list, id);
    if (on && it == list->end()) {
        list->push_back(id);
    } else if (!on && it != list->end()) {
        list->erase(it);
    }
}

bool family_of_word(const std::string& word, nodes::Family* family)
{
    std::string w = get_lower(word);
    if (w.empty()) {
        return false;
    }
    // (a few words of our own beside the families' names)
    if (w == "restart" || w == "restarts") {
        w = "spawns";
    } else if (w == "ped" || w == "peds" || w == "car" || w == "cars") {
        w = "objects";
    } else if (w == "proxim" || w == "proximity") {
        w = "triggers";
    } else if (w == "piece" || w == "pieces") {
        w = "scenery";
    }
    for (int family_index = 0; family_index < nodes::kFamilies; ++family_index) {
        std::string name = get_lower(nodes::family_name(static_cast<nodes::Family>(family_index)));
        const std::string singular = !name.empty() && name.back() == 's' ? name.substr(0, name.size() - 1) : name;
        if (w == name || w == singular) {
            *family = static_cast<nodes::Family>(family_index);
            return true;
        }
    }
    return false;
}

Filter parse_filter(const std::string& search)
{
    Filter filter;
    std::istringstream in(search);
    std::string word;
    std::string rest;
    while (in >> word) {
        const size_t colon = word.find(':');
        if (colon != std::string::npos && colon > 0) {
            const std::string key = get_lower(word.substr(0, colon));
            const std::string value = word.substr(colon + 1);
            nodes::Family family{};
            if (key == "group") {
                filter.group = value;
                // ("group:Name With Spaces" - the rest of the words up to the next filter belong to it)
                continue;
            }
            if (key == "label" || key == "labeled" || key == "labelled") {
                filter.labeled = true;
                if (!value.empty()) {
                    rest += (rest.empty() ? "" : " ") + value;
                }
                continue;
            }
            if (key == "locked") {
                filter.locked = true;
                continue;
            }
            if (key == "hidden") {
                filter.hidden = true;
                continue;
            }
            if (family_of_word(key, &family)) {
                filter.families |= nodes::get_bit(family);
                if (!value.empty()) {
                    rest += (rest.empty() ? "" : " ") + value;
                }
                continue;
            }
        }
        rest += (rest.empty() ? "" : " ") + word;
    }
    filter.text = rest;
    return filter;
}

bool row_passes(const Filter& filter, nodes::Family family, const std::string& name, const std::string& label,
                bool in_group, bool locked, bool hidden)
{
    if ((filter.families != 0U) && ((filter.families & nodes::get_bit(family)) == 0U)) {
        return false;
    }
    if (!filter.group.empty() && !in_group) {
        return false;
    }
    if (filter.labeled && label.empty()) {
        return false;
    }
    if (filter.locked && !locked) {
        return false;
    }
    if (filter.hidden && !hidden) {
        return false;
    }
    if (filter.text.empty()) {
        return true;
    }
    const std::string haystack = get_lower(name + " " + label);
    std::istringstream words(get_lower(filter.text));
    for (std::string word; words >> word;) {
        if (haystack.find(word) == std::string::npos) {
            return false;
        }
    }
    return true;
}

Rect2 rect_of(float ax, float ay, float bx, float by)
{
    return {.x0 = std::min(ax, bx), .y0 = std::min(ay, by), .x1 = std::max(ax, bx), .y1 = std::max(ay, by)};
}

bool crossing_drag(float origin_coord_x, float last_column) { return last_column < origin_coord_x; }

bool box_takes(const Rect2& box, const Rect2& thing, bool crossing)
{
    if (crossing) {
        return thing.x0 <= box.x1 && thing.x1 >= box.x0 && thing.y0 <= box.y1 && thing.y1 >= box.y0;
    }
    return thing.x0 >= box.x0 && thing.x1 <= box.x1 && thing.y0 >= box.y0 && thing.y1 <= box.y1;
}

std::vector<uint32_t> combine(const std::vector<uint32_t>& current, std::span<const uint32_t> found, int op)
{
    std::vector<uint32_t> out;
    auto add = [&](uint32_t id) {
        if (!get_has(out, id)) {
            out.push_back(id);
        }
    };
    if (op == 0) {
        for (const uint32_t id : found) {
            add(id);
        }
    } else if (op == 1) {
        for (const uint32_t id : current) {
            add(id);
        }
        for (const uint32_t id : found) {
            add(id);
        }
    } else {
        for (const uint32_t id : current) {
            if (!get_has(found, id)) {
                add(id);
            }
        }
    }
    return out;
}

bool alike(const Thing& a, const Thing& b, Similar by)
{
    switch (by) {
    case Similar::kMesh:
        if (a.family == nodes::Family::kScenery || b.family == nodes::Family::kScenery) {
            if (a.family != b.family) {
                return false;
            }
            if ((a.source != 0U) && a.source == b.source) {
                return true;
            }
            return std::fabs(a.size.x - b.size.x) <= kSizeTolerance && std::fabs(a.size.y - b.size.y) <= kSizeTolerance &&
                   std::fabs(a.size.z - b.size.z) <= kSizeTolerance;
        }
        return !a.model.empty() && get_lower(a.model) == get_lower(b.model);
    case Similar::kClass:
        return a.cls != 0 && a.cls == b.cls;
    case Similar::kFamily:
        return a.family == b.family;
    case Similar::kKindTag:
        return a.type != 0 && a.type == b.type;
    }
    return false;
}

bool valid_snapshot_name(const std::string& name)
{
    if (name.empty() || name.size() > kMaxSetNameLength || get_lower(name) == "autosave") {
        return false;
    }
    for (const char character : name) {
        if ((std::isalnum(static_cast<unsigned char>(character)) == 0) && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

std::string snapshot_file_name(uint32_t scene, const std::string& name)
{
    return hex_text(scene) + "." + name + ".txt";
}

bool snapshot_of(const std::string& file_name, uint32_t scene, std::string* name)
{
    const std::string prefix = hex_text(scene) + ".";
    if (file_name.size() <= prefix.size() + 4 || get_lower(file_name.substr(0, prefix.size())) != get_lower(prefix) ||
        get_lower(file_name.substr(file_name.size() - 4)) != ".txt") {
        return false;
    }
    const std::string middle = file_name.substr(prefix.size(), file_name.size() - prefix.size() - 4);
    if (!valid_snapshot_name(middle)) {
        return false;
    }
    *name = middle;
    return true;
}

// --- the game side ---------------------------------------------------------------------------------------------------

namespace {

// NOT ORIGINAL grouping - level_editor_organize.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_organize_state()).
struct LevelEditorOrganizeState {
    State state_{};
    std::vector<uint32_t> hidden_gizmos_; // (the eye on a node: its gizmo hidden while editing - never saved)
};

LevelEditorOrganizeState& level_editor_organize_state()
{
    static core::NoDestroy<LevelEditorOrganizeState> state;
    return state.get();
}

struct BoxDrag {
    bool armed = false, pressed = false, active = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint8_t mods = 0;
};

BoxDrag& box_drag()
{
    static core::NoDestroy<BoxDrag> state;
    return state.get();
}

struct OrganizeStep : ModuleStep {
private:
    State state;

public:
    explicit OrganizeStep(State saved_state) : state(std::move(saved_state)) {}
    void get_restore() override { level_editor_organize_state().state_ = state; }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        return std::make_shared<OrganizeStep>(level_editor_organize_state().state_);
    }
};

std::string layouts_folder() { return game_folder_path("thug2-decomp-layouts"); }

uint32_t scene_now()
{
    render::WorldScene* scene = scenery::main_scene();
    return scene ? scenery::world_scene_key(scene) : 0;
}

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && ((object->object_flags() & objects::kObjectFlagDead) == 0U) ? object : nullptr;
}

// Every object with a model the editor can select (the level's object nodes and the copies), once each.
std::vector<uint32_t> selectable_objects()
{
    std::vector<uint32_t> ids;
    auto add = [&](uint32_t id) {
        objects::GameEntity* object = live_object(id);
        OrientedBox box{};
        if (object && !get_has(ids, id) && !view::hidden_object(id) && !picking::latched(picking::kObjects, id) &&
            object_edit::model_box(object, &box)) {
            ids.push_back(id);
        }
    };
    for (const nodes::Elem& elem : nodes::get_all()) {
        if (elem.family == nodes::Family::kObject && (elem.name != 0U)) {
            add(elem.name);
        }
    }
    for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
        add(copy.name);
    }
    return ids;
}

// Every scenery piece that moves (the level's movable pieces and the copies), not hidden, not locked.
std::vector<uint32_t> selectable_sectors()
{
    std::vector<uint32_t> ids;
    auto add = [&](uint32_t checksum) {
        if (!get_has(ids, checksum) && !scenery::why_not_movable(checksum) && !scenery::get_hidden(checksum) &&
            !view::hidden_sector(checksum) && !picking::latched(picking::kScenery, checksum)) {
            ids.push_back(checksum);
        }
    };
    for (const uint32_t checksum : scenery::movable_pieces()) {
        add(checksum);
    }
    for (const scenery::Replicate& clone : scenery::get_clones()) {
        add(clone.checksum);
    }
    return ids;
}

// A thing's screen box from a placed box's corners (false: a corner behind the camera when `whole`, none in front).
bool screen_box(const OrientedBox& box, bool whole, Rect2* rect)
{
    std::array<V3, kBoxCornerCount> corners{};
    box_corners(box.origin, box.axes, box.min, box.max, corners);
    bool any = false;
    Rect2 screen_rect = {.x0 = 0, .y0 = 0, .x1 = 0, .y1 = 0};
    for (const V3& corner : corners) {
        float x = 0.0F;
        float y = 0.0F;
        if (!project_to_back(corner, &x, &y)) {
            if (whole) {
                return false;
            }
            continue;
        }
        if (!any) {
            screen_rect = {.x0 = x, .y0 = y, .x1 = x, .y1 = y};
        }
        screen_rect = {.x0 = std::min(screen_rect.x0, x),
                       .y0 = std::min(screen_rect.y0, y),
                       .x1 = std::max(screen_rect.x1, x),
                       .y1 = std::max(screen_rect.y1, y)};
        any = true;
    }
    *rect = screen_rect;
    return any;
}

Thing thing_of(uint32_t id, bool sector)
{
    Thing thing;
    thing.id = id;
    int index = nodes::get_find(id);
    uint32_t source = id;
    if (sector) {
        thing.family = nodes::Family::kScenery;
        for (const scenery::Replicate& clone : scenery::get_clones()) {
            if (clone.checksum == id) {
                source = clone.source;
            }
        }
        thing.source = source;
        OrientedBox box{};
        if (scenery::cuboid(id, &box)) {
            thing.size = box_size(box);
        }
        if (index < 0) {
            index = nodes::get_find(source);
        }
    } else {
        thing.family = nodes::Family::kObject;
        for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
            if (copy.name == id) {
                source = copy.source; // (a copy: as its source)
            }
        }
        if (index < 0) {
            index = nodes::get_find(source);
        }
        thing.model = object_edit::get_model_file(source);
    }
    if (index >= 0) {
        const nodes::Elem& elem = core::at(nodes::get_all(), index);
        thing.family = elem.family;
        thing.cls = elem.cls;
        thing.type = elem.type;
    }
    return thing;
}

const char* similar_name(Similar by)
{
    switch (by) {
    case Similar::kMesh:
        return "mesh";
    case Similar::kClass:
        return "class";
    case Similar::kFamily:
        return "family";
    case Similar::kKindTag:
        return "type";
    }
    return "?";
}

std::string& outliner_search()
{
    static core::NoDestroy<std::string> outliner_search; // (asked for by Select similar on a node: the Outliner shows the family)
    return outliner_search.get();
}

} // namespace

std::string take_outliner_search()
{
    std::string text;
    text.swap(outliner_search());
    return text;
}

void prepare_all()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    layout::Extension extension;
    extension.keywords = {"group", "label", "edlock", "nopick", "snapshot"};
    extension.capture = []() {
        std::vector<std::string> lines;
        std::istringstream in(write_records(level_editor_organize_state().state_));
        for (std::string line; std::getline(in, line);) {
            lines.push_back(line);
        }
        return lines;
    };
    extension.apply = [](const std::vector<std::string>& lines) {
        State next;
        for (const std::string& line : lines) {
            if (!read_record(line, &next)) {
                get_log("level editor: a layout's editor record isn't readable - skipped: %s", line.c_str());
            }
        }
        level_editor_organize_state().state_ = next;
    };
    extension.reset = []() {
        level_editor_organize_state().state_ = State{};
        level_editor_organize_state().hidden_gizmos_.clear();
    };
    layout::add_extension(extension);
}

const State& at_hand() { return level_editor_organize_state().state_; }

void get_change(const State& next, const char* label)
{
    if (same_state(next, level_editor_organize_state().state_)) {
        return;
    }
    push_module_undo_labelled(std::make_shared<OrganizeStep>(level_editor_organize_state().state_), label);
    level_editor_organize_state().state_ = next;
}

bool hidden_in_editor(const nodes::Elem& node)
{
    if (node.family == nodes::Family::kObject) {
        return view::hidden_object(node.name);
    }
    if (node.family == nodes::Family::kScenery) {
        return view::hidden_sector(node.name);
    }
    return get_has(level_editor_organize_state().hidden_gizmos_, node.name);
}

void set_hidden_in_editor(const nodes::Elem& node, bool hidden)
{
    if (node.family == nodes::Family::kObject || node.family == nodes::Family::kScenery) {
        const std::vector<uint32_t> one = {node.name};
        const std::vector<uint32_t> none;
        const bool object = node.family == nodes::Family::kObject;
        if (hidden) {
            view::conceal(object ? none : one, object ? one : none);
        } else {
            view::get_show(object ? none : one, object ? one : none);
        }
        return;
    }
    set_flag(&level_editor_organize_state().hidden_gizmos_, node.name, hidden);
}

bool latched(uint32_t id) { return get_has(level_editor_organize_state().state_.locked, id); }

void set_locked(uint32_t id, bool locked)
{
    State next = level_editor_organize_state().state_;
    set_flag(&next.locked, id, locked);
    get_change(next, locked ? "Lock (no changes)" : "Unlock");
}

bool get_selectable(const nodes::Elem& node)
{
    if (node.family == nodes::Family::kObject) {
        return !picking::latched(picking::kObjects, node.name);
    }
    if (node.family == nodes::Family::kScenery) {
        return !picking::latched(picking::kScenery, node.name);
    }
    return !get_has(level_editor_organize_state().state_.unpickable, node.name);
}

void set_selectable(const nodes::Elem& node, bool selectable)
{
    if (node.family == nodes::Family::kObject || node.family == nodes::Family::kScenery) {
        const uint32_t kind = node.family == nodes::Family::kObject ? picking::kObjects : picking::kScenery;
        if (picking::latched(kind, node.name) == !selectable) {
            return;
        }
        push_locks_undo_labelled(selectable ? "Selectable" : "Not selectable");
        picking::set_locked(kind, node.name, !selectable);
        return;
    }
    State next = level_editor_organize_state().state_;
    set_flag(&next.unpickable, node.name, !selectable);
    get_change(next, selectable ? "Selectable" : "Not selectable");
}

bool gizmo_hidden(uint32_t name) { return !level_editor_organize_state().hidden_gizmos_.empty() && get_has(level_editor_organize_state().hidden_gizmos_, name); }
bool gizmo_pickable(uint32_t name) { return level_editor_organize_state().state_.unpickable.empty() || !get_has(level_editor_organize_state().state_.unpickable, name); }

bool selection_locked(bool say)
{
    if (level_editor_organize_state().state_.locked.empty()) {
        return false;
    }
    const SelectionIds selection_ids = current_selection();
    bool locked = ((selection_ids.node != 0U) && latched(selection_ids.node));
    for (const uint32_t id : selection_ids.objects) {
        locked = locked || latched(id);
    }
    for (const uint32_t id : selection_ids.sectors) {
        locked = locked || latched(id);
    }
    if (locked && say) {
        show_message("Locked: unlock in the Outliner to edit");
    }
    return locked;
}

// --- groups ---

void make_group(const std::string& wanted)
{
    const SelectionIds selection_ids = current_selection();
    GroupSet group_set;
    for (const uint32_t id : selection_ids.objects) {
        group_set.members.push_back({.kind = 'o', .id = id});
    }
    for (const uint32_t id : selection_ids.sectors) {
        group_set.members.push_back({.kind = 's', .id = id});
    }
    if (selection_ids.node != 0U) {
        group_set.members.push_back({.kind = 'n', .id = selection_ids.node});
    }
    if (group_set.members.empty()) {
        show_message("Nothing selected");
        return;
    }
    std::string name;
    if (!clean_name(wanted, &name)) {
        name = "Group";
    }
    // (a name of its own: "Group 2" when "Group" is taken)
    const std::string base = name;
    for (int suffix = 2;; ++suffix) {
        const bool taken =
            std::ranges::any_of(level_editor_organize_state().state_.groups,
                                [&](const GroupSet& existing_group) { return existing_group.name == name; });
        if (!taken) {
            break;
        }
        name = base + " " + std::to_string(suffix);
    }
    group_set.name = name;
    State next = level_editor_organize_state().state_;
    next.groups.push_back(group_set);
    get_change(next, "Make group");
    show_message("Created group \"" + name + "\" (" + std::to_string(group_set.members.size()) + ")");
}

void select_group(int index)
{
    if (index < 0 || std::cmp_greater_equal(index, level_editor_organize_state().state_.groups.size())) {
        return;
    }
    SelectionIds ids;
    uint32_t node = 0;
    int missing = 0;
    for (const Member& member : core::at(level_editor_organize_state().state_.groups, index).members) {
        if (member.kind == 'o') {
            if (live_object(member.id)) {
                ids.objects.push_back(member.id);
            } else {
                ++missing;
            }
        } else if (member.kind == 's') {
            OrientedBox box{};
            if (scenery::cuboid(member.id, &box)) {
                ids.sectors.push_back(member.id);
            } else {
                ++missing;
            }
        } else if (node == 0U) {
            node = member.id;
        }
    }
    std::string note;
    if (!ids.objects.empty()) {
        if (!ids.sectors.empty() || (node != 0U)) {
            note = " (objects only)";
        }
        ids.sectors.clear();
    } else if (!ids.sectors.empty()) {
        if (node != 0U) {
            note = " (scenery only)";
        }
    } else {
        ids.node = node;
    }
    set_selection(ids);
    show_message("Group \"" + core::at(level_editor_organize_state().state_.groups, index).name + "\" selected" + note +
                 ((missing != 0) ? " - " + std::to_string(missing) + " missing" : ""));
}

void delete_group(int index)
{
    if (index < 0 || std::cmp_greater_equal(index, level_editor_organize_state().state_.groups.size())) {
        return;
    }
    State next = level_editor_organize_state().state_;
    next.groups.erase(next.groups.begin() + index);
    get_change(next, "Delete group");
}

void rename_group(int index, const std::string& wanted)
{
    std::string name;
    if (index < 0 || std::cmp_greater_equal(index, level_editor_organize_state().state_.groups.size()) ||
        !clean_name(wanted, &name)) {
        return;
    }
    for (int i = 0; std::cmp_less(i, level_editor_organize_state().state_.groups.size()); ++i) {
        if (i != index && core::at(level_editor_organize_state().state_.groups, i).name == name) {
            show_message("Group name already in use");
            return;
        }
    }
    State next = level_editor_organize_state().state_;
    core::at(next.groups, index).name = name;
    get_change(next, "Rename group");
}

void add_to_group(int index)
{
    if (index < 0 || std::cmp_greater_equal(index, level_editor_organize_state().state_.groups.size())) {
        return;
    }
    const SelectionIds selection_ids = current_selection();
    State next = level_editor_organize_state().state_;
    std::vector<Member>& members = core::at(next.groups, index).members;
    auto add = [&](char kind, uint32_t id) {
        const Member member = {.kind = kind, .id = id};
        if (std::ranges::find(members, member) == members.end()) {
            members.push_back(member);
        }
    };
    for (const uint32_t id : selection_ids.objects) {
        add('o', id);
    }
    for (const uint32_t id : selection_ids.sectors) {
        add('s', id);
    }
    if (selection_ids.node != 0U) {
        add('n', selection_ids.node);
    }
    get_change(next, "Add to group");
}

bool in_group(const std::string& group, const nodes::Elem& node)
{
    for (const GroupSet& group_set : level_editor_organize_state().state_.groups) {
        if (get_lower(group_set.name).compare(0, group.size(), get_lower(group)) != 0) {
            continue;
        }
        for (const Member& member : group_set.members) {
            if (member.id == node.name) {
                return true;
            }
        }
    }
    return false;
}

// --- box select ---

void arm_box()
{
    box_drag().armed = true;
    show_message("Box Select: drag in the view");
}
bool box_armed() { return box_drag().armed; }

void box_press(maths::Vec2 at, uint8_t mods)
{
    const auto x = at.x;
    const auto y = at.y;
    box_drag().armed = false;
    box_drag().pressed = true;
    box_drag().active = false;
    box_drag().x0 = box_drag().x1 = x;
    box_drag().y0 = box_drag().y1 = y;
    box_drag().mods = mods;
}

namespace {

void apply_box()
{
    const Rect2 box = rect_of(box_drag().x0, box_drag().y0, box_drag().x1, box_drag().y1);
    const bool crossing = crossing_drag(box_drag().x0, box_drag().x1);
    const int op = ((box_drag().mods & actions::kShift) != 0) ? 1 : ((box_drag().mods & actions::kCtrl) != 0) ? 2 : 0;
    const uint32_t filter = pick_filter_now();
    std::vector<uint32_t> objects;
    std::vector<uint32_t> sectors;
    if ((filter & picking::kObjects) != 0U) {
        for (const uint32_t id : selectable_objects()) {
            OrientedBox ob{};
            Rect2 rect{};
            if (object_edit::model_box(live_object(id), &ob) && screen_box(ob, !crossing, &rect) &&
                box_takes(box, rect, crossing)) {
                objects.push_back(id);
            }
        }
    }
    if ((filter & picking::kScenery) != 0U) {
        for (const uint32_t checksum : selectable_sectors()) {
            OrientedBox ob{};
            Rect2 rect{};
            if (scenery::cuboid(checksum, &ob) && screen_box(ob, !crossing, &rect) && box_takes(box, rect, crossing)) {
                sectors.push_back(checksum);
            }
        }
    }
    const SelectionIds now = current_selection();
    SelectionIds next;
    const char* how = crossing ? "crossing" : "inside";
    std::array<char, kBoxStatusSize> text{};
    if (!objects.empty() || (op == 2 && !now.objects.empty())) {
        next.objects = combine(now.objects, objects, op);
        text::format_to(text, "Box (%s): %d objects - %d selected", how, static_cast<int>(objects.size()),
                        static_cast<int>(next.objects.size()));
    } else if (!sectors.empty() || (op == 2 && !now.sectors.empty())) {
        next.sectors = combine(now.sectors, sectors, op);
        text::format_to(text, "Box (%s): %d scenery pieces - %d selected", how, static_cast<int>(sectors.size()),
                        static_cast<int>(next.sectors.size()));
    } else {
        // (no objects or scenery: the point node nearest the box's middle - nodes select one at a time)
        uint32_t best = 0;
        float best_d = kFarthest;
        int in_box = 0;
        if ((filter & picking::kNodes) != 0U) {
            const float cx = (box.x0 + box.x1) * 0.5F;
            const float cy = (box.y0 + box.y1) * 0.5F;
            for (const nodes::Elem& elem : nodes::get_all()) {
                if (!elem.has_pos_ || (elem.name == 0U) || elem.family == nodes::Family::kObject ||
                    elem.family == nodes::Family::kScenery || nodes::shape_of(elem.family) == nodes::Shape::kNone ||
                    gizmo_hidden(elem.name) || !gizmo_pickable(elem.name)) {
                    continue;
                }
                float x = 0.0F;
                float y = 0.0F;
                if (!project_to_back(elem.pos, &x, &y) || x < box.x0 || x > box.x1 || y < box.y0 || y > box.y1) {
                    continue;
                }
                ++in_box;
                const float distance = ((x - cx) * (x - cx)) + ((y - cy) * (y - cy));
                if (distance < best_d) {
                    best_d = distance;
                    best = elem.name;
                }
            }
        }
        if ((best != 0U) && op != 2) {
            next.node = best;
            text::format_to(text, "Box: %d nodes, nearest selected", in_box);
        } else if (op == 0) {
            text::format_to(text, "Box (%s): nothing there", how);
        } else {
            return; // (adding / removing nothing: the selection stays)
        }
    }
    set_selection(next);
    show_message(text.data());
}

} // namespace

void box_track(bool down, float x, float y)
{
    if (!box_drag().pressed) {
        return;
    }
    if (down) {
        box_drag().x1 = x;
        box_drag().y1 = y;
        if (!box_drag().active && std::fabs(x - box_drag().x0) + std::fabs(y - box_drag().y0) >= kBoxDragThreshold) {
            box_drag().active = true;
        }
        return;
    }
    box_drag().pressed = false;
    if (box_drag().active) {
        apply_box();
    }
    box_drag().active = false;
}

bool box_active() { return box_drag().pressed && box_drag().active; }

bool box_outline(Rect2* rect, bool* crossing)
{
    if (!box_active()) {
        return false;
    }
    *rect = rect_of(box_drag().x0, box_drag().y0, box_drag().x1, box_drag().y1);
    *crossing = crossing_drag(box_drag().x0, box_drag().x1);
    return true;
}

// --- select similar / all / invert ---

void select_similar(Similar by)
{
    const SelectionIds selection_ids = current_selection();
    if (!selection_ids.objects.empty() || !selection_ids.sectors.empty()) {
        const bool sectors = selection_ids.objects.empty();
        std::vector<Thing> chosen;
        for (const uint32_t id : sectors ? selection_ids.sectors : selection_ids.objects) {
            chosen.push_back(thing_of(id, sectors));
        }
        SelectionIds next;
        std::vector<uint32_t>& out = sectors ? next.sectors : next.objects;
        for (const uint32_t id : sectors ? selectable_sectors() : selectable_objects()) {
            Thing thing = thing_of(id, sectors);
            if (std::ranges::any_of(chosen,
                                    [&](const Thing& chosen_thing) { return alike(chosen_thing, thing, by); })) {
                out.push_back(id);
            }
        }
        // (the selection's own first: the primary stays)
        out = combine(sectors ? selection_ids.sectors : selection_ids.objects, out, 1);
        set_selection(next);
        show_message("Selected " + std::to_string(out.size()) + (sectors ? " scenery pieces" : " objects") +
                     " of the same " + similar_name(by));
        return;
    }
    if (selection_ids.node != 0U) {
        // (nodes select one at a time: the Outliner shows the similar ones - its search set to the family)
        const int index = nodes::get_find(selection_ids.node);
        if (index < 0) {
            return;
        }
        const nodes::Elem& elem = core::at(nodes::get_all(), index);
        int count = 0;
        for (const nodes::Elem& other_elem : nodes::get_all()) {
            Thing thing;
            Thing other_thing;
            thing.family = elem.family, thing.cls = elem.cls, thing.type = elem.type;
            other_thing.family = other_elem.family, other_thing.cls = other_elem.cls,
            other_thing.type = other_elem.type;
            count += alike(thing, other_thing, by == Similar::kMesh ? Similar::kKindTag : by) ? 1 : 0;
        }
        const std::string family = nodes::family_name(elem.family);
        outliner_search() = get_lower(family) + ":";
        show_message(std::to_string(count) + " similar nodes listed in the Outliner: " + family);
        return;
    }
    show_message("Nothing selected");
}

void select_family() { select_similar(Similar::kFamily); }

void select_all()
{
    const SelectionIds selection_ids = current_selection();
    SelectionIds next;
    if (!selection_ids.sectors.empty() || ((pick_filter_now() & picking::kObjects) == 0U)) {
        next.sectors = combine(selection_ids.sectors, selectable_sectors(), 1);
    } else {
        next.objects = combine(selection_ids.objects, selectable_objects(), 1);
    }
    set_selection(next);
    show_message("Selected " + std::to_string(next.objects.empty() ? next.sectors.size() : next.objects.size()) +
                 (next.objects.empty() ? " scenery pieces" : " objects"));
}

void invert_selection()
{
    const SelectionIds selection_ids = current_selection();
    SelectionIds next;
    if (!selection_ids.sectors.empty()) {
        for (const uint32_t id : selectable_sectors()) {
            if (!get_has(selection_ids.sectors, id)) {
                next.sectors.push_back(id);
            }
        }
    } else {
        for (const uint32_t id : selectable_objects()) {
            if (!get_has(selection_ids.objects, id)) {
                next.objects.push_back(id);
            }
        }
    }
    set_selection(next);
    show_message("Inverted: " + std::to_string(next.objects.empty() ? next.sectors.size() : next.objects.size()) +
                 (next.objects.empty() && !selection_ids.sectors.empty() ? " scenery pieces" : " objects"));
}

// --- snapshots ---

std::vector<std::string> snapshots()
{
    std::vector<std::string> names;
    const uint32_t scene = scene_now();
    if (scene == 0U) {
        return names;
    }
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA((layouts_folder() + "\\" + hex_text(scene) + ".*.txt").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) {
        return names;
    }
    for (bool again = true; again; again = (FindNextFileA(find, &found) != FALSE)) {
        std::string name;
        if (((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) &&
            snapshot_of(std::data(found.cFileName), scene, &name)) {
            names.push_back(name);
        }
    }
    FindClose(find);
    std::ranges::sort(names);
    return names;
}

std::string active_snapshot() { return level_editor_organize_state().state_.snapshot; }

namespace {

bool write_snapshot_file(const std::string& name, std::string* message)
{
    std::string text;
    if (!layout::capture_text(&text)) {
        *message = "Not in a level";
        return false;
    }
    CreateDirectoryA(layouts_folder().c_str(), nullptr);
    const std::string path = layouts_folder() + "\\" + snapshot_file_name(scene_now(), name);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) {
        *message = "Couldn't write " + path;
        return false;
    }
    get_log("level editor: layout snapshot %s written - %s", name.c_str(), path.c_str());
    return true;
}

} // namespace

bool save_snapshot(const std::string& name, std::string* message)
{
    if (!valid_snapshot_name(name)) {
        *message = "Invalid name: use letters, digits, _ and - (max 32)";
        return false;
    }
    const std::string before = level_editor_organize_state().state_.snapshot;
    level_editor_organize_state().state_.snapshot = name; // (the layout knows which snapshot it is: the active one)
    if (!write_snapshot_file(name, message)) {
        level_editor_organize_state().state_.snapshot = before;
        return false;
    }
    *message = "Snapshot \"" + name + "\" saved";
    return true;
}

bool switch_snapshot(const std::string& name, std::string* message)
{
    const std::string path = layouts_folder() + "\\" + snapshot_file_name(scene_now(), name);
    const std::ifstream in(path, std::ios::binary);
    if ((scene_now() == 0U) || !in) {
        *message = "No snapshot \"" + name + "\" for this level";
        return false;
    }
    std::stringstream text;
    text << in.rdbuf();
    // (the layout as it is now kept first: in the active snapshot, else in "previous")
    const std::string keep = level_editor_organize_state().state_.snapshot.empty() || level_editor_organize_state().state_.snapshot == name ? "previous" : level_editor_organize_state().state_.snapshot;
    if (keep != name) {
        const std::string kept = level_editor_organize_state().state_.snapshot;
        level_editor_organize_state().state_.snapshot = keep;
        std::string why;
        if (!write_snapshot_file(keep, &why)) {
            get_log("level editor: the layout before the switch couldn't be kept - %s", why.c_str());
        }
        level_editor_organize_state().state_.snapshot = kept;
    }
    if (!apply_layout_step(text.str(), "Switch snapshot", message)) {
        return false;
    }
    level_editor_organize_state().state_.snapshot = name;
    actions::fire_off(actions::kSave); // (saved as the edited copy's layout - the (EDITED) entry's: the active one)
    *message = "Snapshot \"" + name + "\" loaded (previous kept as \"" + keep + "\")";
    return true;
}

bool delete_snapshot(const std::string& name, std::string* message)
{
    const std::string path = layouts_folder() + "\\" + snapshot_file_name(scene_now(), name);
    if (!valid_snapshot_name(name) || (DeleteFileA(path.c_str()) == FALSE)) {
        *message = "Couldn't delete the snapshot \"" + name + "\"";
        return false;
    }
    if (level_editor_organize_state().state_.snapshot == name) {
        level_editor_organize_state().state_.snapshot.clear();
    }
    *message = "Snapshot \"" + name + "\" deleted";
    return true;
}

} // namespace frontend::level_editor::organize

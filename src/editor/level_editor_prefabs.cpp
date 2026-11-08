// NOT ORIGINAL - the level editor's prefabs (ours). See level_editor_prefabs.h.

#include "editor/level_editor_prefabs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <istream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/down_cast.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_clip.h"
#include "editor/level_editor_content.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_ghost.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_import.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_props.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui_app.h"
#include "editor/level_editor_ui_internal.h"
#include "editor/level_editor_ui_layout.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/objects/entity.h"
#include "engine/ui/ui_backend.h"
#include "imgui.h"
#include "platform/run_log.h"

#include <SDL3/SDL_scancode.h>

namespace frontend::level_editor::prefabs {

namespace {

constexpr int kVersion = 1;
constexpr int kHexBase = 16;
constexpr size_t kIdLength = 40;
constexpr size_t kNameSize = 64;
constexpr float kNearBox = 10.0F; // (rails over a piece: within this of its box; nodes within the parts' box)
constexpr float kHalf = 0.5F;
constexpr float kTile = 96.0F;
constexpr float kTileInset = 4.0F;
constexpr float kTileRounding = 4.0F;
constexpr float kSelectedAlpha = 0.35F;
constexpr float kHoverAlpha = 0.15F;
constexpr float kGhostAlpha = 0.9F;
constexpr float kGhostLine = 2.0F;
constexpr float kDragTipOffset = 16.0F;
constexpr float kTinyScale = 0.0001F;
constexpr int kBoxCorners = 8;
constexpr int kBoxEdges = 12;
constexpr std::array<std::pair<int, int>, kBoxEdges> kEdges = {
    {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

bool same_name(const std::string& a, const std::string& b)
{
    return checksums::hash_from_text(a.c_str()) == checksums::hash_from_text(b.c_str());
}

std::string hex8(uint32_t value)
{
    std::array<char, kNameSize> text{};
    text::format_to(text, "%08X", value);
    return text.data();
}

} // namespace

// --- the pure parts -------------------------------------------------------------------------------------------------

int part_count(const Prefab& prefab)
{
    const layout::FileLoad& parts = prefab.parts;
    return static_cast<int>(parts.clones.size() + parts.copies.size() + parts.added.size() + parts.triggers.size() +
                            parts.chains.points.size());
}

std::string prefab_text(const Prefab& prefab)
{
    layout::FileLoad parts = prefab.parts;
    parts.extra.clear();
    std::array<char, kNameSize * 4> bounds{};
    text::format_to(bounds, "bounds %.9g %.9g %.9g %.9g %.9g %.9g", prefab.min.x, prefab.min.y, prefab.min.z, prefab.max.x,
                    prefab.max.y, prefab.max.z);
    parts.extra.push_back("prefab " + std::to_string(kVersion) + " " + prefab.name);
    parts.extra.push_back("source " + hex8(prefab.scene) + " " + (prefab.level.empty() ? "-" : prefab.level));
    parts.extra.emplace_back(bounds.data());
    for (const std::string& pack : prefab.packs) {
        parts.extra.push_back("prop " + pack);
    }
    parts.scene = prefab.scene;
    return std::string(kHeader) + " - " + prefab.name + "\n" + layout::emit_out(parts);
}

bool read_prefab(const std::string& text, Prefab* prefab)
{
    *prefab = {};
    const size_t header = std::char_traits<char>::length(kHeader);
    if (text.compare(0, header, kHeader) != 0) {
        return false;
    }
    int line = 0;
    if (!layout::get_take_in(text.substr(text.find('\n') + 1), &prefab->parts, &line)) {
        return false;
    }
    bool named = false;
    std::vector<std::string> rest;
    for (const std::string& extra : prefab->parts.extra) {
        std::istringstream words(extra);
        std::string word;
        words >> word;
        if (word == "prefab") {
            int version = 0;
            words >> version;
            std::getline(words >> std::ws, prefab->name);
            named = version >= 1 && !prefab->name.empty();
        } else if (word == "source") {
            std::string scene;
            words >> scene >> prefab->level;
            prefab->scene = static_cast<uint32_t>(std::strtoul(scene.c_str(), nullptr, kHexBase));
            if (prefab->level == "-") {
                prefab->level.clear();
            }
        } else if (word == "bounds") {
            words >> prefab->min.x >> prefab->min.y >> prefab->min.z >> prefab->max.x >> prefab->max.y >> prefab->max.z;
        } else if (word == "prop") {
            std::string pack;
            words >> pack;
            if (!pack.empty() && std::ranges::find(prefab->packs, pack) == prefab->packs.end()) {
                prefab->packs.push_back(pack);
            }
        } else {
            rest.push_back(extra);
        }
    }
    prefab->parts.extra = rest;
    return named;
}

std::string id_of(const std::string& name)
{
    std::string out;
    bool gap = false;
    for (const char character : name) {
        const auto letter = static_cast<unsigned char>(character);
        if (std::isalnum(letter) != 0) {
            if (gap && !out.empty()) {
                out.push_back('_');
            }
            gap = false;
            out.push_back(static_cast<char>(std::tolower(letter)));
        } else {
            gap = true;
        }
        if (out.size() >= kIdLength) {
            break;
        }
    }
    return out.empty() ? "prefab" : out;
}

void translate(layout::FileLoad* parts, V3 delta)
{
    for (layout::CloneRecord& clone : parts->clones) {
        clone.placement.offset = clone.placement.offset + delta;
    }
    for (layout::CopyRecord& copy : parts->copies) {
        core::at(copy.pos, 0) += delta.x;
        core::at(copy.pos, 1) += delta.y;
        core::at(copy.pos, 2) += delta.z;
    }
    for (node_create::Spec& spec : parts->added) {
        spec.pos = spec.pos + delta;
    }
    for (triggers::Placed& trigger : parts->triggers) {
        trigger.pos = trigger.pos + delta;
    }
    for (chains::Point& point : parts->chains.points) {
        point.pos = point.pos + delta;
    }
}

std::string unique_name(const std::string& name, const std::function<bool(const std::string&)>& taken)
{
    if (!taken(name)) {
        return name;
    }
    for (int number = 2;; ++number) {
        std::string candidate = name + "_" + std::to_string(number);
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

namespace {

// A placement under way: the names in use, the parts' new names, what it has added.
class Placer {
public:
    Placer(const PlaceHooks& hooks, layout::FileLoad* into) : hooks_(&hooks), into_(into)
    {
        used_.reserve(into->added.size() + into->triggers.size() + into->chains.points.size());
        for (const node_create::Spec& spec : into->added) {
            used_.push_back(spec.name);
        }
        for (const triggers::Placed& trigger : into->triggers) {
            used_.push_back(trigger.spec.name);
        }
        for (const chains::Point& point : into->chains.points) {
            used_.push_back(point.name);
        }
    }

    [[nodiscard]] bool taken(const std::string& name) const
    {
        return std::ranges::any_of(used_, [&](const std::string& other) { return same_name(other, name); }) ||
               (hooks_->name_taken_ && hooks_->name_taken_(name));
    }

    // The nodes' new names (chain points and triggers: theirs or name_2...; the editor's kinds: their stem's next).
    std::vector<node_create::Spec> rename_all(const layout::FileLoad& parts)
    {
        auto is_taken = [this](const std::string& name) { return taken(name); };
        for (const chains::Point& point : parts.chains.points) {
            rename(point.name, unique_name(point.name, is_taken));
        }
        for (const triggers::Placed& trigger : parts.triggers) {
            rename(trigger.spec.name, unique_name(trigger.spec.name, is_taken));
        }
        std::vector<node_create::Spec> specs;
        for (const node_create::Spec& added_spec : parts.added) {
            node_create::Spec spec = added_spec;
            node_create::Kind kind{};
            if (!node_create::kind_of(spec, &kind)) {
                result_.missing.push_back(spec.name + " (not a kind the editor makes)");
                continue;
            }
            if (taken(spec.name)) {
                if (node_create::is_lobby_flag(spec.name) || spec.name.starts_with("TRG_CTF_")) {
                    result_.missing.push_back(spec.name + " (the level has one by that name)");
                    continue;
                }
                spec.name = node_create::fresh_name(node_create::name_stem(kind), is_taken);
            }
            rename(added_spec.name, spec.name);
            specs.push_back(spec);
        }
        return specs;
    }

    void add_scenery(const layout::FileLoad& parts)
    {
        for (const layout::CloneRecord& clone : parts.clones) {
            if (hooks_->sector_exists_ && !hooks_->sector_exists_(clone.source)) {
                result_.missing.push_back("scenery " + hex8(clone.source));
                continue;
            }
            into_->clones.push_back(clone);
            ++result_.clones;
            ++result_.added;
        }
    }

    // Objects, named as Ctrl+V names its copies (the old name kept for the paths given to them).
    void add_objects(const layout::FileLoad& parts)
    {
        unsigned counter = 0;
        for (const layout::CopyRecord& record : parts.copies) {
            if (hooks_->object_copyable_ && !hooks_->object_copyable_(record.source)) {
                result_.missing.push_back("object " + hex8(record.source));
                continue;
            }
            layout::CopyRecord copy = record;
            for (copy.name = copy_name(record.source, ++counter); id_used(copy.name);) {
                copy.name = copy_name(record.source, ++counter);
            }
            copy_names_[record.name] = copy.name;
            into_->copies.push_back(copy);
            result_.copies.push_back(copy.name);
            ++result_.added;
        }
    }

    void add_nodes(const layout::FileLoad& parts, const std::vector<node_create::Spec>& specs)
    {
        for (const node_create::Spec& spec : specs) {
            into_->added.push_back(spec);
            ++result_.added;
        }
        for (const chains::Point& source : parts.chains.points) {
            chains::Point point = source;
            point.name = new_name_of(node_create::name_hash(source.name));
            point.links = links_to_parts(source.links);
            point.cluster = 0; // (a cluster names a piece: the copy's own is made as the layout goes in)
            into_->chains.points.push_back(point);
            ++result_.added;
        }
        // (a path given to a copied object - Car / Ped on Path: its links= to the parts' waypoints)
        for (const chains::LinkEdit& edit : parts.chains.links) {
            const auto copy = copy_names_.find(edit.name);
            if (copy == copy_names_.end()) {
                continue;
            }
            const chains::LinkEdit made{.name = copy->second, .links = links_to_parts(edit.links)};
            std::erase_if(into_->chains.links, [&](const chains::LinkEdit& other) { return other.name == made.name; });
            into_->chains.links.push_back(made);
        }
    }

    // Triggers: their targets and gaps following the parts (a fresh GapID for each gap).
    void add_triggers(const layout::FileLoad& parts)
    {
        std::map<std::string, std::string> gaps;
        auto gap_taken = [&](const std::string& id) {
            return std::ranges::any_of(into_->triggers,
                                       [&](const triggers::Placed& other) { return same_name(other.spec.gap, id); }) ||
                   std::ranges::any_of(gaps, [&](const auto& entry) { return same_name(entry.second, id); });
        };
        for (const triggers::Placed& source : parts.triggers) {
            triggers::Placed trigger = source;
            trigger.spec.name = new_name_of(node_create::name_hash(source.spec.name));
            if (!trigger.spec.target.empty() && renamed_.contains(node_create::name_hash(trigger.spec.target))) {
                trigger.spec.target = new_name_of(node_create::name_hash(trigger.spec.target));
            }
            if (!trigger.spec.gap.empty()) {
                auto found = gaps.find(trigger.spec.gap);
                if (found == gaps.end()) {
                    found = gaps.emplace(trigger.spec.gap, node_create::fresh_name("ed_gap", gap_taken)).first;
                }
                trigger.spec.gap = found->second;
            }
            trigger.spec.replaces = 0; // (a level trigger it stood in for is the source level's)
            into_->triggers.push_back(trigger);
            ++result_.added;
        }
    }

    [[nodiscard]] const PlaceResult& result() const { return result_; }

private:
    void rename(const std::string& name, const std::string& fresh)
    {
        renamed_[node_create::name_hash(name)] = fresh;
        used_.push_back(fresh);
        result_.nodes.push_back(fresh);
    }

    [[nodiscard]] std::string new_name_of(uint32_t old) const
    {
        const auto found = renamed_.find(old);
        return found != renamed_.end() ? found->second : std::string();
    }

    [[nodiscard]] std::vector<uint32_t> links_to_parts(const std::vector<uint32_t>& links) const
    {
        std::vector<uint32_t> out;
        for (const uint32_t link : links) {
            if (renamed_.contains(link)) {
                out.push_back(node_create::name_hash(new_name_of(link)));
            }
        }
        return out;
    }

    static uint32_t copy_name(uint32_t source, unsigned number)
    {
        std::array<char, kNameSize> text{};
        text::format_to(text, "level_editor_copy_%08X_p%u", source, number);
        return fields::hash_of_name(text.data());
    }

    [[nodiscard]] bool id_used(uint32_t id) const
    {
        return std::ranges::any_of(into_->copies, [&](const layout::CopyRecord& other) { return other.name == id; }) ||
               (hooks_->id_taken_ && hooks_->id_taken_(id));
    }

    const PlaceHooks* hooks_;
    layout::FileLoad* into_;
    std::vector<std::string> used_;
    std::map<uint32_t, std::string> renamed_;  // (an old name's checksum -> its new name)
    std::map<uint32_t, uint32_t> copy_names_; // (an object's old name -> its copy's)
    PlaceResult result_;
};

} // namespace

PlaceResult instantiate(const Prefab& prefab, V3 at, const PlaceHooks& hooks, layout::FileLoad* into)
{
    layout::FileLoad parts = prefab.parts;
    translate(&parts, at);
    Placer placer(hooks, into);
    const std::vector<node_create::Spec> specs = placer.rename_all(parts);
    placer.add_scenery(parts);
    placer.add_objects(parts);
    placer.add_nodes(parts, specs);
    placer.add_triggers(parts);
    // The packs its pieces come from.
    for (const std::string& pack : prefab.packs) {
        const std::string line = "prop " + pack;
        if (std::ranges::find(into->extra, line) == into->extra.end()) {
            into->extra.push_back(line);
        }
    }
    return placer.result();
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

struct Ask {
    int group = -1;       // the group it's made of (-1: the selection)
    std::string replace;  // Update from Selection: the prefab's id
};

struct PrefabsState {
    std::vector<Prefab> list;
    std::string selected; // an id
    std::string drag;     // the prefab dragged
    std::string packs_loaded; // (the prefab whose packs this drag's preview loaded)
    std::optional<Ask> ask;
    std::optional<Ask> create;
    std::string create_name;
    std::optional<std::string> place_at_cursor;
    std::optional<std::pair<std::string, V3>> place_at;
    std::array<char, kNameSize> name_text{};
    V3 ghost{};
    float drag_back_x = 0.0F, drag_back_y = 0.0F;
    int act_create = -1;
    bool listed = false;
    bool prepared = false;
    bool dragging = false;
    bool over_scene = false;
    bool have_ghost = false;
    bool ghost_seen = false;   // (`ghost` is where this drag's preview last showed it: the drop puts it there)
    bool pieces_shown = false; // (its scenery pieces shown as ghosts - the box only for what else it holds)
    bool drop_asked = false;
    bool ask_open = false;
};

PrefabsState& state()
{
    static core::NoDestroy<PrefabsState> prefabs_state;
    return prefabs_state.get();
}

std::string folder() { return import::output_folder() + "\\" + kFolder; }

std::string file_of(const std::string& id) { return folder() + "\\" + id + ".txt"; }

std::string picture_name(const std::string& id) { return "prefabs\\" + id; }

void list_prefabs()
{
    PrefabsState& prefabs = state();
    prefabs.list.clear();
    fs::list((folder() + "\\*.txt").c_str(), [&](const fs::Found& found) {
        std::string text;
        Prefab prefab;
        if (!found.folder && fs::read_text(folder() + "\\" + found.name, &text) && read_prefab(text, &prefab)) {
            prefab.id = std::string(found.name).substr(0, std::string(found.name).size() - 4);
            prefabs.list.push_back(std::move(prefab));
        }
        return true;
    });
    std::ranges::sort(prefabs.list, {}, &Prefab::name);
    prefabs.listed = true;
}

const Prefab* find_prefab(const std::string& id)
{
    if (!state().listed) {
        list_prefabs();
    }
    for (const Prefab& prefab : state().list) {
        if (prefab.id == id || same_name(prefab.name, id)) {
            return &prefab;
        }
    }
    return nullptr;
}

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && ((object->object_flags() & objects::kObjectFlagDead) == 0U) ? object : nullptr;
}

// The level's folder name for a scene (its checksum is its level= text's: "BO"; "" when the name isn't known).
std::string level_folder_of(uint32_t scene)
{
    const std::string name = nodes::caption(scene);
    return node_create::valid_name(name) ? name : std::string();
}

// The name a level's chain node gets as a point of the editor's: its own, else pf_<checksum>.
std::string level_point_name(uint32_t name)
{
    const std::string text = nodes::caption(name);
    return node_create::valid_name(text) ? text : "pf_" + hex8(name);
}

// What a prefab is made of: sectors, objects and nodes by name (checksums).
struct Members {
    std::vector<uint32_t> sectors, objects, nodes;
};

void add_once(std::vector<uint32_t>* list, uint32_t id)
{
    if (id != 0U && std::ranges::find(*list, id) == list->end()) {
        list->push_back(id);
    }
}

bool inside(V3 point, V3 min, V3 max, float grow)
{
    return point.x >= min.x - grow && point.x <= max.x + grow && point.y >= min.y - grow && point.y <= max.y + grow &&
           point.z >= min.z - grow && point.z <= max.z + grow;
}

// The parts' box (scenery's, objects' model boxes or places, nodes' places).
bool box_of(const Members& members, V3* min, V3* max)
{
    bool have = false;
    for (const uint32_t sector : members.sectors) {
        OrientedBox box{};
        V3 low{};
        V3 high{};
        if (scenery::cuboid(sector, &box)) {
            bounds_of(box, &low, &high);
            grow_box(min, max, have, low, high);
            have = true;
        }
    }
    for (const uint32_t id : members.objects) {
        objects::GameEntity* object = live_object(id);
        OrientedBox box{};
        V3 low{};
        V3 high{};
        if (object && object_edit::model_box(object, &box)) {
            bounds_of(box, &low, &high);
        } else if (object) {
            low = high = object_edit::pos_of(object);
        } else {
            continue;
        }
        grow_box(min, max, have, low, high);
        have = true;
    }
    for (const uint32_t name : members.nodes) {
        const int index = nodes::get_find(name);
        if (index >= 0 && core::at(nodes::get_all(), index).has_pos_) {
            const V3 pos = core::at(nodes::get_all(), index).pos;
            grow_box(min, max, have, pos, pos);
            have = true;
        }
    }
    return have;
}

// What comes with the parts chosen: the rail and ladder points over a chosen piece, the editor's nodes and triggers
// within the parts' box, a teleport's exit, a gap's other end, a rail's whole chain.
void gather_more(Members* members)
{
    // (a rail or ladder chosen by one of its points: all of it)
    const size_t chosen = members->nodes.size();
    for (size_t at = 0; at < chosen; ++at) {
        const uint32_t name = core::at(members->nodes, static_cast<int>(at));
        const int index = nodes::get_find(name);
        const nodes::Family family = index >= 0 ? core::at(nodes::get_all(), index).family : nodes::Family::kOther;
        if (family == nodes::Family::kRail || family == nodes::Family::kLadder || family == nodes::Family::kRouteWay) {
            for (const uint32_t other : chains::chain_of(name)) {
                add_once(&members->nodes, other);
            }
        }
    }
    std::vector<std::pair<V3, V3>> boxes;
    for (const uint32_t sector : members->sectors) {
        OrientedBox box{};
        V3 low{};
        V3 high{};
        if (scenery::cuboid(sector, &box)) {
            bounds_of(box, &low, &high);
            boxes.emplace_back(low, high);
        }
    }
    V3 min{};
    V3 max{};
    const bool have = box_of(*members, &min, &max);
    for (const nodes::Elem& elem : nodes::get_all()) {
        if (elem.hidden || !elem.has_pos_) {
            continue;
        }
        const bool chain = elem.family == nodes::Family::kRail || elem.family == nodes::Family::kLadder;
        const bool over_piece =
            chain && std::ranges::any_of(boxes, [&](const auto& box) { return inside(elem.pos, box.first, box.second, kNearBox); });
        const bool editors = have && elem.added && inside(elem.pos, min, max, kNearBox) &&
                             elem.family != nodes::Family::kScenery && elem.family != nodes::Family::kObject;
        if (over_piece || editors) {
            add_once(&members->nodes, elem.name);
        }
    }
    // (a teleport: its exit; a gap: its other end)
    const std::vector<triggers::Placed> placed = triggers::capture_added();
    for (size_t at = 0; at < members->nodes.size(); ++at) {
        const uint32_t name = core::at(members->nodes, static_cast<int>(at));
        for (const triggers::Placed& trigger : placed) {
            if (node_create::name_hash(trigger.spec.name) != name) {
                continue;
            }
            if (!trigger.spec.target.empty() && triggers::is_exit(trigger.spec.target)) {
                add_once(&members->nodes, node_create::name_hash(trigger.spec.target));
            }
            for (const triggers::Placed& other : placed) {
                if (!trigger.spec.gap.empty() && same_name(other.spec.gap, trigger.spec.gap)) {
                    add_once(&members->nodes, node_create::name_hash(other.spec.name));
                }
            }
        }
    }
}

// A level's node turned into a chain point of the editor's (its name, class, place, terrain, links by name).
bool point_of_level_node(const nodes::Elem& elem, chains::Point* point)
{
    if (elem.family == nodes::Family::kRail) {
        point->cls = "RailNode";
    } else if (elem.family == nodes::Family::kLadder) {
        point->cls = "ClimbingNode";
        point->facing = elem.angles.y;
    } else if (elem.family == nodes::Family::kRouteWay) {
        point->cls = "Waypoint";
    } else {
        return false;
    }
    point->name = level_point_name(elem.name);
    point->pos = elem.pos;
    if (elem.terrain != 0U) {
        const char* terrain = object_edit::get_name_of(elem.terrain);
        point->terrain = terrain ? terrain : "";
    }
    for (const int link : elem.links) {
        if (link >= 0 && std::cmp_less(link, nodes::get_all().size())) {
            point->links.push_back(core::at(nodes::get_all(), link).name); // (names; kept to the parts below)
        }
    }
    return true;
}

// Scenery as Ctrl+C copies it (a copy: of its source, placed relative to the source as loaded), and the prop packs
// its pieces come from.
void capture_scenery(const Members& members, Prefab* prefab)
{
    for (const uint32_t checksum : members.sectors) {
        layout::CloneRecord record;
        record.source = checksum;
        Placement now{};
        scenery::placement_now(checksum, &now);
        record.placement = now;
        for (const scenery::Replicate& clone : scenery::get_clones()) {
            if (clone.checksum == checksum) {
                record.source = clone.source;
                record.placement = compose(now, clone.source_at_copy_);
            }
        }
        prefab->parts.clones.push_back(record);
        for (const std::string& id : props::loaded_packs()) {
            props::Manifest manifest;
            if (props::find_manifest(id, &manifest) &&
                std::ranges::find(manifest.sectors, record.source) != manifest.sectors.end() &&
                std::ranges::find(prefab->packs, id) == prefab->packs.end()) {
                prefab->packs.push_back(id);
            }
        }
    }
}

// Objects as copy records (`name`: the object's own id - the key of the path given to it), with their paths.
void capture_objects(const Members& members, const chains::State& chain_state, layout::FileLoad* parts,
                     std::vector<std::string>* skipped)
{
    for (const uint32_t id : members.objects) {
        objects::GameEntity* object = live_object(id);
        if (!object) {
            continue;
        }
        uint32_t source = id;
        for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
            if (copy.name == id) {
                source = copy.source;
            }
        }
        if (!object_edit::can_copy(source)) {
            skipped->push_back(object_edit::caption(id));
            continue;
        }
        layout::CopyRecord record = {
            .name = id, .source = source, .pos = {object->at_point().x, object->at_point().y, object->at_point().z}, .rows = {}};
        for (int axis = 0; axis < 3; ++axis) {
            core::at(record.rows, (axis * 3) + 0) = core::at(object->basis_frame().row, axis).x;
            core::at(record.rows, (axis * 3) + 1) = core::at(object->basis_frame().row, axis).y;
            core::at(record.rows, (axis * 3) + 2) = core::at(object->basis_frame().row, axis).z;
        }
        record.scale = scale_tool::get_object_scale(object);
        parts->copies.push_back(record);
        for (const chains::LinkEdit& edit : chain_state.links) {
            if (edit.name == id) {
                parts->chains.links.push_back(edit); // (its path: the waypoints, when they're parts too)
            }
        }
    }
}

// One node: the editor's trigger, chain point or node as it is; a level's chain node as a chain point of the editor's,
// its spawn / crown / light / sound / proximity node as a node of its kind. False: none of those.
bool capture_node(uint32_t name, const chains::State& chain_state, const std::vector<triggers::Placed>& placed,
                  const node_create::LinkChain& specs, layout::FileLoad* parts)
{
    for (const triggers::Placed& trigger : placed) {
        if (node_create::name_hash(trigger.spec.name) == name) {
            parts->triggers.push_back(trigger);
            return true;
        }
    }
    for (const chains::Point& point : chain_state.points) {
        if (node_create::name_hash(point.name) == name) {
            parts->chains.points.push_back(point);
            return true;
        }
    }
    for (const node_create::Spec& spec : specs) {
        node_create::Kind kind{};
        if (node_create::name_hash(spec.name) == name && !spec.owned && node_create::kind_of(spec, &kind)) {
            parts->added.push_back(spec);
            return true;
        }
    }
    const int index = nodes::get_find(name);
    if (index < 0 || core::at(nodes::get_all(), index).added) {
        return false;
    }
    const nodes::Elem& elem = core::at(nodes::get_all(), index);
    chains::Point point;
    if (point_of_level_node(elem, &point)) {
        parts->chains.points.push_back(point);
        return true;
    }
    node_create::Spec spec;
    if (clip::level_node_spec(elem, &spec)) {
        spec.name = nodes::caption(elem.name);
        parts->added.push_back(spec);
        return true;
    }
    return false;
}

// The chain points' links kept to the parts (a rail's links to a point that isn't one go), by the names they have
// in the prefab.
void keep_links_to_parts(layout::FileLoad* parts)
{
    std::set<uint32_t> names;
    for (const chains::Point& point : parts->chains.points) {
        names.insert(node_create::name_hash(point.name));
    }
    for (chains::Point& point : parts->chains.points) {
        std::vector<uint32_t> kept;
        for (const uint32_t link : point.links) {
            const int index = nodes::get_find(link);
            const uint32_t as_named = index >= 0 && !core::at(nodes::get_all(), index).added
                                          ? node_create::name_hash(level_point_name(link))
                                          : link;
            if (names.contains(as_named)) {
                kept.push_back(as_named);
            }
        }
        point.links = kept;
    }
}

// The parts as a layout fragment (where they are now, relative to the pivot), the packs their pieces come from.
bool capture(const Members& chosen, Prefab* prefab, std::string* message)
{
    render::WorldScene* scene = scenery::main_scene();
    if (!scene) {
        *message = "Not in a level";
        return false;
    }
    Members members = chosen;
    gather_more(&members);
    layout::FileLoad& parts = prefab->parts;
    parts.scene = scenery::world_scene_key(scene);
    prefab->scene = parts.scene;
    prefab->level = level_folder_of(parts.scene);
    std::vector<std::string> skipped;
    capture_scenery(members, prefab);
    const chains::State chain_state = chains::at_hand();
    capture_objects(members, chain_state, &parts, &skipped);
    const std::vector<triggers::Placed> placed = triggers::capture_added();
    const node_create::LinkChain specs = node_create::at_hand();
    for (const uint32_t name : members.nodes) {
        if (!capture_node(name, chain_state, placed, specs, &parts)) {
            skipped.push_back(nodes::caption(name));
        }
    }
    keep_links_to_parts(&parts);
    if (part_count(*prefab) == 0) {
        *message = skipped.empty() ? "Nothing selected to make a prefab of" : "Can't make a prefab of " + skipped.front();
        return false;
    }
    // The pivot: the middle of the parts' box, on its bottom.
    V3 min{};
    V3 max{};
    if (!box_of(members, &min, &max)) {
        min = max = {};
    }
    const V3 pivot{.x = (min.x + max.x) * kHalf, .y = min.y, .z = (min.z + max.z) * kHalf};
    translate(&parts, V3{.x = -pivot.x, .y = -pivot.y, .z = -pivot.z});
    prefab->min = min - pivot;
    prefab->max = max - pivot;
    for (const std::string& name : skipped) {
        get_log("level editor: prefab - not taken: %s", name.c_str());
    }
    return true;
}

Members selection_members()
{
    const SelectionIds selection = current_selection();
    Members members;
    members.sectors = selection.sectors;
    members.objects = selection.objects;
    add_once(&members.nodes, selection.node);
    return members;
}

Members group_members(int group)
{
    Members members;
    const organize::State& organized = organize::at_hand();
    if (group < 0 || std::cmp_greater_equal(group, organized.groups.size())) {
        return members;
    }
    for (const organize::Member& member : core::at(organized.groups, group).members) {
        if (member.kind == 'o') {
            add_once(&members.objects, member.id);
        } else if (member.kind == 's') {
            add_once(&members.sectors, member.id);
        } else {
            add_once(&members.nodes, member.id);
        }
    }
    return members;
}

void ask_thumbnail(const std::string& id)
{
    content::forget_picture(picture_name(id));
    content::run_tool("prefab-thumb", {"--prefab", file_of(id)}, id);
}

// What Create Prefab is asked for: the name wanted, and the prefab made again (Update from Selection: its id; "": a new one).
struct Naming {
    std::string wanted;
    std::string replace;
};

bool create(const Members& members, const Naming& naming, std::string* message)
{
    const std::string& wanted = naming.wanted;
    const std::string& replace = naming.replace;
    Prefab prefab;
    if (!capture(members, &prefab, message)) {
        return false;
    }
    std::string name;
    if (!organize::clean_name(wanted, &name)) {
        name = "Prefab";
    }
    prefab.name = name;
    if (!replace.empty()) {
        prefab.id = replace;
    } else {
        prefab.id = unique_name(id_of(name), [](const std::string& id) {
            return fs::disk().kind(file_of(id).c_str()) == fs::PathKind::kFile;
        });
    }
    fs::disk().make_folders(folder().c_str());
    if (!fs::write_text(file_of(prefab.id), prefab_text(prefab)).closed) {
        *message = "Couldn't write " + file_of(prefab.id);
        return false;
    }
    list_prefabs();
    state().selected = prefab.id;
    ask_thumbnail(prefab.id);
    get_log("level editor: prefab %s (%s) %s: %d parts (%u pieces, %u objects, %u nodes, %u triggers, %u chain points), "
            "%u packs, box (%.0f, %.0f, %.0f)-(%.0f, %.0f, %.0f)",
            prefab.id.c_str(), prefab.name.c_str(), replace.empty() ? "made" : "made again", part_count(prefab),
            static_cast<unsigned>(prefab.parts.clones.size()), static_cast<unsigned>(prefab.parts.copies.size()),
            static_cast<unsigned>(prefab.parts.added.size()), static_cast<unsigned>(prefab.parts.triggers.size()),
            static_cast<unsigned>(prefab.parts.chains.points.size()), static_cast<unsigned>(prefab.packs.size()),
            prefab.min.x, prefab.min.y, prefab.min.z, prefab.max.x, prefab.max.y, prefab.max.z);
    *message = (replace.empty() ? "Created prefab " : "Updated prefab ") + prefab.name + " (" +
               std::to_string(part_count(prefab)) + " parts)";
    return true;
}

// The group a placement makes: "<name> 1", "<name> 2"... (its copies of scenery as their clone records' places).
std::string group_line(const Prefab& prefab, const layout::FileLoad& into, int first_clone, const PlaceResult& result)
{
    std::set<std::string> names;
    for (const std::string& line : into.extra) {
        std::istringstream words(line);
        std::string word;
        std::string escaped;
        std::string name;
        words >> word >> escaped;
        if (word == "group" && fields::unescape(escaped, &name)) {
            names.insert(name);
        }
    }
    std::string name;
    for (int number = 1;; ++number) {
        name = prefab.name + " " + std::to_string(number);
        if (!names.contains(name)) {
            break;
        }
    }
    std::string line = "group " + fields::escape(name) + " ";
    std::vector<std::string> members;
    members.reserve(static_cast<size_t>(result.clones) + result.copies.size() + result.nodes.size());
    for (int index = 0; index < result.clones; ++index) {
        members.push_back("c:" + hex8(static_cast<uint32_t>(first_clone + index)));
    }
    for (const uint32_t copy : result.copies) {
        members.push_back("o:" + hex8(copy));
    }
    for (const std::string& node : result.nodes) {
        members.push_back("n:" + hex8(node_create::name_hash(node)));
    }
    for (size_t index = 0; index < members.size(); ++index) {
        line += (index != 0U ? "," : "") + core::at(members, static_cast<int>(index));
    }
    return members.empty() ? std::string() : line;
}

bool place(const std::string& id, V3 at, std::string* message)
{
    const Prefab* found = find_prefab(id);
    if (!found) {
        *message = "No prefab " + id;
        return false;
    }
    const Prefab prefab = *found;
    for (const std::string& pack : prefab.packs) {
        std::string why;
        if (!props::load(pack, &why)) {
            get_log("level editor: prefab %s: %s", prefab.id.c_str(), why.c_str());
        }
    }
    std::string current;
    layout::FileLoad now;
    int line = 0;
    if (!layout::capture_text(&current) || !layout::get_take_in(current, &now, &line)) {
        *message = "Not in a level";
        return false;
    }
    PlaceHooks hooks;
    hooks.name_taken_ = [](const std::string& name) { return node_create::name_taken(name) || triggers::is_trigger(node_create::name_hash(name)); };
    hooks.id_taken_ = [](uint32_t object) { return objects::find_object(object) != nullptr; };
    hooks.sector_exists_ = [](uint32_t sector) { return scenery::sector_of(sector) != nullptr; };
    hooks.object_copyable_ = [](uint32_t object) { return object_edit::can_copy(object); };
    const int first_clone = static_cast<int>(now.clones.size());
    const PlaceResult result = instantiate(prefab, at, hooks, &now);
    if (result.added == 0) {
        *message = "Nothing of " + prefab.name + " is in this level" +
                   (result.missing.empty() ? std::string() : ": " + result.missing.front());
        return false;
    }
    const std::string group = group_line(prefab, now, first_clone, result);
    if (!group.empty()) {
        now.extra.push_back(group);
    }
    std::string why;
    if (!apply_layout_step(layout::emit_out(now), "Place prefab", &why)) {
        *message = "Couldn't place " + prefab.name + ": " + why;
        return false;
    }
    const organize::State& organized = organize::at_hand();
    if (!group.empty() && !organized.groups.empty()) {
        organize::select_group(static_cast<int>(organized.groups.size()) - 1);
    }
    for (const std::string& name : result.missing) {
        get_log("level editor: prefab %s - not in this level: %s", prefab.id.c_str(), name.c_str());
    }
    get_log("level editor: placed prefab %s at (%.0f, %.0f, %.0f) - %d parts (%d pieces, %u objects, %u nodes), %u missing",
            prefab.id.c_str(), at.x, at.y, at.z, result.added, result.clones, static_cast<unsigned>(result.copies.size()),
            static_cast<unsigned>(result.nodes.size()), static_cast<unsigned>(result.missing.size()));
    *message = "Placed " + prefab.name +
               (result.missing.empty() ? std::string()
                                       : " - not in this level: " + result.missing.front() +
                                             (result.missing.size() > 1 ? " and " + std::to_string(result.missing.size() - 1) + " more" : ""));
    return true;
}

bool delete_prefab(const std::string& id, std::string* message)
{
    if (!fs::disk().remove(file_of(id).c_str())) {
        *message = "Couldn't delete " + file_of(id);
        return false;
    }
    list_prefabs();
    *message = "Deleted the prefab " + id;
    return true;
}

// --- the folder ---

void tooltip(const Prefab& prefab)
{
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(prefab.name.c_str());
    ui::internal::get_muted(("Made in " + (prefab.level.empty() ? std::string("another level") : prefab.level)).c_str());
    gui::text("%.0f x %.0f x %.0f", prefab.max.x - prefab.min.x, prefab.max.y - prefab.min.y, prefab.max.z - prefab.min.z);
    gui::text("%d pieces, %d objects, %d nodes, %d triggers, %d rail points", static_cast<int>(prefab.parts.clones.size()),
              static_cast<int>(prefab.parts.copies.size()), static_cast<int>(prefab.parts.added.size()),
              static_cast<int>(prefab.parts.triggers.size()), static_cast<int>(prefab.parts.chains.points.size()));
    if (!prefab.packs.empty()) {
        gui::text("%d prop packs", static_cast<int>(prefab.packs.size()));
    }
    ImGui::EndTooltip();
}

void context_menu(const Prefab& prefab)
{
    if (!ImGui::BeginPopupContextItem("##prefab")) {
        return;
    }
    PrefabsState& prefabs = state();
    if (ImGui::MenuItem("Place at Cursor")) {
        prefabs.place_at_cursor = prefab.id;
    }
    if (ImGui::MenuItem("Update from Selection")) {
        prefabs.create = Ask{.group = -1, .replace = prefab.id};
        prefabs.create_name = prefab.name;
    }
    if (ImGui::MenuItem("Copy Name")) {
        ImGui::SetClipboardText(prefab.name.c_str());
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete")) {
        std::string message;
        delete_prefab(prefab.id, &message);
        show_message(message);
    }
    ImGui::EndPopup();
}

void item_behaviour(const Prefab& prefab)
{
    PrefabsState& prefabs = state();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        prefabs.selected = prefab.id;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        prefabs.place_at_cursor = prefab.id;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !prefabs.dragging) {
        prefabs.dragging = true;
        prefabs.drag = prefab.id;
        prefabs.selected = prefab.id;
        prefabs.packs_loaded.clear();
        prefabs.ghost_seen = prefabs.pieces_shown = false;
    }
    if (ImGui::IsItemHovered() && !prefabs.dragging) {
        tooltip(prefab);
    }
    context_menu(prefab);
}

void tile(const Prefab& prefab, float size)
{
    const float label = ImGui::GetTextLineHeight();
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tile", ImVec2(size, size + label));
    ImDrawList* list = ImGui::GetWindowDrawList();
    const bool selected = state().selected == prefab.id;
    if (selected || ImGui::IsItemHovered()) {
        list->AddRectFilled(corner, ImVec2(corner.x + size, corner.y + size + label),
                            ImGui::GetColorU32(ui::internal::get_accent(selected ? kSelectedAlpha : kHoverAlpha)), kTileRounding);
    }
    const ImVec2 top_left(corner.x + kTileInset, corner.y + kTileInset);
    const ImVec2 bottom_right(corner.x + size - kTileInset, corner.y + size - kTileInset);
    if (const ui_backend::Picture* picture = content::picture(picture_name(prefab.id))) {
        list->AddImage(static_cast<ImTextureID>(picture->texture), top_left, bottom_right, ImVec2(0, picture->bottom_up ? 1.0F : 0.0F),
                       ImVec2(1, picture->bottom_up ? 0.0F : 1.0F));
    } else {
        list->AddRectFilled(top_left, bottom_right, ImGui::GetColorU32(ImGuiCol_FrameBg), kTileRounding);
        const ImVec2 glyph = ImGui::CalcTextSize("prefab");
        list->AddText(ImVec2((top_left.x + bottom_right.x - glyph.x) / 2, (top_left.y + bottom_right.y - glyph.y) / 2),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), "prefab");
    }
    list->AddText(ImVec2(top_left.x + kTileInset, top_left.y + kTileInset), ImGui::GetColorU32(ui::internal::get_accent(1.0F)), "P");
    list->PushClipRect(ImVec2(corner.x, corner.y + size), ImVec2(corner.x + size, corner.y + size + label), true);
    const ImVec2 name_size = ImGui::CalcTextSize(prefab.name.c_str());
    list->AddText(ImVec2(corner.x + std::max(kTileInset, (size - name_size.x) / 2), corner.y + size), ImGui::GetColorU32(ImGuiCol_Text),
                  prefab.name.c_str());
    list->PopClipRect();
    item_behaviour(prefab);
}

// Create Prefab's name.
void name_popup()
{
    PrefabsState& prefabs = state();
    constexpr const char* kPopup = "Create Prefab###prefabname";
    if (prefabs.ask && !prefabs.ask_open) {
        ImGui::OpenPopup(kPopup);
        prefabs.ask_open = true;
    }
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        prefabs.ask_open = prefabs.ask_open && ImGui::IsPopupOpen(kPopup);
        if (!prefabs.ask_open) {
            prefabs.ask.reset();
        }
        return;
    }
    ImGui::TextUnformatted(prefabs.ask && prefabs.ask->group >= 0 ? "A prefab of the group" : "A prefab of the selection");
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool entered = ImGui::InputText("Name", prefabs.name_text.data(), prefabs.name_text.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (entered || ImGui::Button("Create")) {
        prefabs.create = prefabs.ask;
        prefabs.create_name = prefabs.name_text.data();
        prefabs.ask.reset();
        prefabs.ask_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        prefabs.ask.reset();
        prefabs.ask_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

ImVec2 window_per_back()
{
    int width = 0;
    int height = 0;
    ui_backend::back_buffer_size({.width = &width, .height = &height});
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    return {width > 0 ? display.x / static_cast<float>(width) : 1.0F, height > 0 ? display.y / static_cast<float>(height) : 1.0F};
}

// The drag: a tooltip by the cursor; over the Scene, the prefab's box where it would land.
void drag_feedback(float scale)
{
    PrefabsState& prefabs = state();
    if (!prefabs.dragging) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        prefabs.dragging = false;
        prefabs.have_ghost = false;
        show_message("Placing cancelled");
        return;
    }
    const Prefab* prefab = find_prefab(prefabs.drag);
    if (!prefab) {
        prefabs.dragging = false;
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ui_layout::Rect2 scene = ui::app::scene_rect();
    prefabs.over_scene = ui_layout::get_holds(scene, mouse.x, mouse.y);
    const ImVec2 to_window = window_per_back();
    prefabs.drag_back_x = mouse.x / std::max(kTinyScale, to_window.x);
    prefabs.drag_back_y = mouse.y / std::max(kTinyScale, to_window.y);
    ImGui::SetNextWindowPos(ImVec2(mouse.x + (kDragTipOffset * scale), mouse.y + (kDragTipOffset * scale)));
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(prefab->name.c_str());
    ui::internal::get_muted(prefabs.over_scene ? "Release to place - Esc cancels" : "Drag into the Scene to place it");
    ImGui::EndTooltip();
    // (its scenery pieces show as themselves - tick_frame's ghosts; the box while they can't, or for objects and nodes)
    const bool box = !prefabs.pieces_shown || !prefab->parts.copies.empty() || !prefab->parts.added.empty() ||
                     !prefab->parts.triggers.empty() || !prefab->parts.chains.points.empty();
    if (prefabs.over_scene && prefabs.have_ghost && box) {
        std::array<ImVec2, kBoxCorners> corners{};
        bool all = true;
        for (int index = 0; index < kBoxCorners; ++index) {
            const V3 corner{.x = prefabs.ghost.x + ((index & 1) != 0 ? prefab->max.x : prefab->min.x),
                            .y = prefabs.ghost.y + ((index & 2) != 0 ? prefab->max.y : prefab->min.y),
                            .z = prefabs.ghost.z + ((index & 4) != 0 ? prefab->max.z : prefab->min.z)};
            float back_x = 0.0F;
            float back_y = 0.0F;
            all = all && project_to_back(corner, &back_x, &back_y);
            core::at(corners, index) = ImVec2(back_x * to_window.x, back_y * to_window.y);
        }
        if (all) {
            ImDrawList* list = ImGui::GetForegroundDrawList();
            const ImU32 colour = ImGui::GetColorU32(ui::internal::get_accent(kGhostAlpha));
            list->PushClipRect(ImVec2(scene.x, scene.y), ImVec2(scene.x + scene.width, scene.y + scene.height), false);
            for (const auto& [from, to] : kEdges) {
                list->AddLine(core::at(corners, from), core::at(corners, to), colour, kGhostLine * scale);
            }
            list->PopClipRect();
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        prefabs.dragging = false;
        prefabs.drop_asked = prefabs.over_scene;
        prefabs.have_ghost = prefabs.have_ghost && prefabs.over_scene;
    }
}

// The dragged prefab's scenery pieces as ghosts at `at` (level_editor_ghost.h): each piece's source as it is now, drawn
// where its `clone` record puts it (relative to the source as loaded) - its packs loaded first, once a drag. Whether any
// is shown.
bool show_pieces(const Prefab& prefab, V3 at)
{
    PrefabsState& prefabs = state();
    if (prefabs.packs_loaded != prefab.id) {
        prefabs.packs_loaded = prefab.id;
        for (const std::string& pack : prefab.packs) {
            std::string why;
            if (!props::load(pack, &why)) {
                get_log("level editor: prefab %s's preview: %s", prefab.id.c_str(), why.c_str());
            }
        }
    }
    std::vector<ghost::Part> parts;
    for (const layout::CloneRecord& clone : prefab.parts.clones) {
        if (scenery::sector_of(clone.source) == nullptr) {
            continue;
        }
        Placement now{};
        scenery::placement_now(clone.source, &now);
        ghost::Matrix undo{};
        if (!ghost::inverse_of(ghost::matrix_of(now), &undo)) {
            continue;
        }
        Placement placed = clone.placement;
        placed.offset = placed.offset + at; // (as instantiate's translate)
        parts.push_back({.sector = clone.source, .world = ghost::after(undo, ghost::matrix_of(placed))});
    }
    ghost::show(parts);
    return !parts.empty();
}

std::vector<uint32_t> hex_list(const std::string& text)
{
    std::vector<uint32_t> out;
    std::istringstream list(text);
    for (std::string item; std::getline(list, item, ',');) {
        if (!item.empty()) {
            out.push_back(static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, kHexBase)));
        }
    }
    return out;
}

bool parse_v3(const std::string& text, V3* point)
{
    std::array<float, 3> values{};
    std::istringstream list(text);
    int count = 0;
    for (std::string item; std::getline(list, item, ',') && count < 3; ++count) {
        core::at(values, count) = std::strtof(item.c_str(), nullptr);
    }
    *point = {.x = core::at(values, 0), .y = core::at(values, 1), .z = core::at(values, 2)};
    return count == 3;
}

} // namespace

void prepare_all()
{
    PrefabsState& prefabs = state();
    if (prefabs.prepared) {
        return;
    }
    prefabs.prepared = true;
    actions::ActStep create_step;
    create_step.id = "create_prefab";
    create_step.name = "Create Prefab...";
    create_step.category = "Edit";
    create_step.tip = "Save the selection (pieces, objects, rails, triggers) as a prefab in the Content Browser";
    create_step.kind = actions::Kind::kPress;
    // (Ctrl+Alt+G: Ctrl+Shift+G is Ungroup, as most editors have it - level_editor_scenery_units.cpp)
    create_step.defaults = create_step.keys = {actions::Chord{.scancode = SDL_SCANCODE_G, .mods = actions::kCtrl | actions::kAlt}};
    create_step.run = [] { ask_create(-1); };
    prefabs.act_create = actions::enrol(create_step);
}

void ask_create(int group)
{
    PrefabsState& prefabs = state();
    prefabs.ask = Ask{.group = group, .replace = {}};
    std::string name = "Prefab";
    const organize::State& organized = organize::at_hand();
    if (group >= 0 && std::cmp_less(group, organized.groups.size())) {
        name = core::at(organized.groups, group).name;
    }
    text::format_to(prefabs.name_text, "%s", name.c_str());
}

void tick_frame()
{
    PrefabsState& prefabs = state();
    if (prefabs.act_create >= 0 && actions::squeezed(prefabs.act_create)) {
        ask_create(-1);
    }
    if (prefabs.create) {
        const Ask ask = *prefabs.create;
        prefabs.create.reset();
        std::string message;
        create(ask.group >= 0 ? group_members(ask.group) : selection_members(), {.wanted = prefabs.create_name, .replace = ask.replace}, &message);
        show_message(message);
    }
    prefabs.have_ghost = false;
    if (prefabs.dragging && prefabs.over_scene) {
        V3 point{};
        if (edit::cursor_point_at(prefabs.drag_back_x, prefabs.drag_back_y, &point)) {
            prefabs.ghost = point;
            prefabs.have_ghost = prefabs.ghost_seen = true;
            const Prefab* prefab = find_prefab(prefabs.drag);
            prefabs.pieces_shown = prefab != nullptr && show_pieces(*prefab, point);
        }
    }
    if (prefabs.drop_asked) {
        prefabs.drop_asked = false;
        // (where the preview last showed it - else the ground under the cursor now)
        V3 at = prefabs.ghost;
        std::string message;
        if (prefabs.ghost_seen || edit::cursor_point_at(prefabs.drag_back_x, prefabs.drag_back_y, &at)) {
            place(prefabs.drag, at, &message);
            show_message(message);
        }
        prefabs.ghost_seen = prefabs.pieces_shown = false;
    }
    if (prefabs.place_at_cursor) {
        const std::string id = *prefabs.place_at_cursor;
        prefabs.place_at_cursor.reset();
        std::string message;
        place(id, edit::cursor_point(), &message);
        show_message(message);
    }
    if (prefabs.place_at) {
        const auto [id, at] = *prefabs.place_at;
        prefabs.place_at.reset();
        std::string message;
        place(id, at, &message);
        show_message(message);
    }
}

void folder_body(float scale)
{
    PrefabsState& prefabs = state();
    if (!prefabs.listed) {
        list_prefabs();
    }
    if (ImGui::SmallButton("New from Selection")) {
        ask_create(-1);
    }
    ImGui::SameLine();
    ui::internal::get_muted("or drag a group from the Outliner here");
    if (prefabs.list.empty()) {
        ui::internal::get_muted("No prefabs yet - select pieces, rails and triggers, then Edit > Create Prefab");
        return;
    }
    const float size = kTile * scale;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (size + spacing)));
    for (int index = 0; std::cmp_less(index, prefabs.list.size()); ++index) {
        if (index % columns != 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(index);
        ImGui::BeginGroup();
        tile(core::at(prefabs.list, index), size);
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void accept_group_drop()
{
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kGroupPayload)) {
        int group = -1;
        if (std::cmp_equal(payload->DataSize, sizeof(group))) {
            std::memcpy(&group, payload->Data, sizeof(group));
            ask_create(group);
        }
    }
    ImGui::EndDragDropTarget();
}

void overlay(float scale)
{
    name_popup();
    drag_feedback(scale);
}

int count()
{
    if (!state().listed) {
        list_prefabs();
    }
    return static_cast<int>(state().list.size());
}

std::string automation(const std::vector<std::string>& words)
{
    // (words: "prefab", the verb, its arguments)
    const std::string verb = words.size() > 1 ? core::at(words, 1) : std::string();
    std::string message;
    if (verb == "list") {
        list_prefabs();
        std::string out = "ok " + std::to_string(state().list.size()) + " prefabs";
        for (const Prefab& prefab : state().list) {
            out += " " + prefab.id + "(" + std::to_string(part_count(prefab)) + ")";
        }
        return out;
    }
    if (verb == "create" && words.size() > 2) {
        Members members;
        bool given = false;
        for (size_t at = 3; at < words.size(); ++at) {
            const std::string& word = core::at(words, static_cast<int>(at));
            const size_t equals = word.find('=');
            const std::string key = word.substr(0, equals);
            const std::string value = equals == std::string::npos ? std::string() : word.substr(equals + 1);
            if (key == "sectors") {
                members.sectors = hex_list(value);
                given = true;
            } else if (key == "objects") {
                members.objects = hex_list(value);
                given = true;
            } else if (key == "nodes") {
                std::istringstream list(value);
                for (std::string item; std::getline(list, item, ',');) {
                    add_once(&members.nodes, node_create::name_hash(item));
                }
                given = true;
            }
        }
        const bool made = create(given ? members : selection_members(), {.wanted = core::at(words, 2), .replace = {}}, &message);
        return (made ? "ok " : "error: ") + message;
    }
    if (verb == "place" && words.size() > 2) {
        V3 at = edit::cursor_point();
        if (words.size() > 3 && !parse_v3(core::at(words, 3), &at)) {
            return "error: place <name> [x,y,z]";
        }
        const bool placed = place(core::at(words, 2), at, &message);
        return (placed ? "ok " : "error: ") + message;
    }
    return "error: prefab list | create <name> [sectors=..] [objects=..] [nodes=..] | place <name> [x,y,z]";
}

} // namespace frontend::level_editor::prefabs

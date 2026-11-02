// NOT ORIGINAL - the level editor's added and hidden nodes (ours). See level_editor_node_create.h.

#include "editor/level_editor_node_create.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor_nodes.h"
#include "engine/core/checksums.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_objects.h"
#include "engine/gfx/scene_lights.h"
#include "engine/gfx/screen.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/entity.h"
#include "engine/objects/proximity.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "engine/gfx/light_manager.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/light_sets.h"

namespace frontend::level_editor::node_create {

namespace {

bool same_text(const std::string& a, const char* b)
{
    if (a.size() != std::strlen(b))
        return false;
    const std::string_view b_text(b);
    for (size_t i = 0; i < a.size(); ++i) {
        char x = core::at(a, i), y = core::at(b_text, i);
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}
bool starts_with(const std::string& text, const char* prefix)
{
    const size_t n = std::strlen(prefix);
    return text.size() >= n && same_text(text.substr(0, n), prefix);
}
bool ends_with(const std::string& text, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    return text.size() >= n && same_text(text.substr(text.size() - n), suffix);
}

const std::array<const char*, kTeams> kTeamNames = {{"Red", "Blue", "Green", "Yellow"}};

// The team a name-driven node's name or type ends in ("Red"...; -1: none).
int team_of(const std::string& text)
{
    std::string t = text;
    if (ends_with(t, "_Base"))
        t.resize(t.size() - 5);
    for (int i = 0; i < kTeams; ++i) {
        const std::string suffix = std::string("_") + core::at(kTeamNames, i);
        if (ends_with(t, suffix.c_str()))
            return i;
    }
    return -1;
}

std::string get_lower(std::string text)
{
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

constexpr float kTwoPi = 6.28318531f;

float wrapped(float yaw)
{
    while (yaw > 3.14159265f)
        yaw -= kTwoPi;
    while (yaw <= -3.14159265f)
        yaw += kTwoPi;
    return yaw;
}

} // namespace

const char* kind_name(Kind kind)
{
    switch (kind) {
    case Kind::kSpawn:
        return "Multiplayer spawn";
    case Kind::kTeamSpawn:
        return "Team spawn";
    case Kind::kCrown:
        return "Crown";
    case Kind::kFlagSet:
        return "CTF flag set";
    case Kind::kLight:
        return "Level light";
    case Kind::kSound:
        return "Sound emitter";
    case Kind::kProximityNode:
        return "Proximity node (sphere)";
    }
    return "?";
}

const char* get_team_name(Team team)
{
    const int i = static_cast<int>(team);
    return i >= 0 && i < kTeams ? core::at(kTeamNames, i) : "?";
}

bool same_spec(const Spec& a, const Spec& b)
{
    return a.name == b.name && a.cls == b.cls && a.type == b.type && a.pos.x == b.pos.x && a.pos.y == b.pos.y &&
           a.pos.z == b.pos.z && a.angles.x == b.angles.x && a.angles.y == b.angles.y && a.angles.z == b.angles.z &&
           a.types == b.types && core::at(a.colour, 0) == core::at(b.colour, 0) && core::at(a.colour, 1) == core::at(b.colour, 1) &&
           core::at(a.colour, 2) == core::at(b.colour, 2) && a.brightness == b.brightness && a.inner == b.inner && a.outer == b.outer &&
           a.radius == b.radius && a.like == b.like && a.terrain == b.terrain && a.links == b.links &&
           a.owned == b.owned && a.extra.size() == b.extra.size() &&
           std::equal(a.extra.begin(), a.extra.end(), b.extra.begin(), [](const Prop& x, const Prop& y) {
               return x.type == y.type && x.key == y.key && x.text == y.text && core::at(x.value, 0) == core::at(y.value, 0) &&
                      core::at(x.value, 1) == core::at(y.value, 1) && core::at(x.value, 2) == core::at(y.value, 2) && x.integer == y.integer &&
                      x.list == y.list && x.integers == y.integers;
           });
}

bool kind_of(const Spec& spec, Kind* kind)
{
    if (spec.owned)
        return false; // (another module's node, its fields its own: e.g. a goal's restart - level_editor_goals.h)
    if (same_text(spec.cls, "Restart"))
        *kind = starts_with(spec.name, "TRG_CTF_Restart_") ? Kind::kTeamSpawn : Kind::kSpawn;
    else if (same_text(spec.cls, "GenericNode") && same_text(spec.type, "Crown"))
        *kind = Kind::kCrown;
    else if (same_text(spec.cls, "GameObject") && starts_with(spec.type, "Flag_") && team_of(spec.type) >= 0)
        *kind = Kind::kFlagSet;
    else if (same_text(spec.cls, "LevelLight"))
        *kind = Kind::kLight;
    else if (same_text(spec.cls, "EmitterObject"))
        *kind = Kind::kSound;
    else if (same_text(spec.cls, "ProximNode"))
        *kind = Kind::kProximityNode;
    else
        return false;
    return true;
}

Spec default_spec(Kind kind, Team team, V3 pos, float yaw, const std::string& name)
{
    Spec s;
    s.name = name;
    s.pos = pos;
    s.angles = {.x = 0.0f, .y = wrapped(yaw), .z = 0.0f};
    const std::string colour = get_team_name(team);
    switch (kind) {
    case Kind::kSpawn:
        s.cls = "Restart";
        s.type = "MultiPlayer";
        s.types = {"MultiPlayer", "Horse"};
        break;
    case Kind::kTeamSpawn:
        s.cls = "Restart";
        s.type = "CTF";
        s.types = {"CTF", "MultiPlayer", "Horse"};
        break;
    case Kind::kCrown:
        s.cls = "GenericNode";
        s.type = "Crown";
        break;
    case Kind::kFlagSet:
        s.cls = "GameObject";
        s.type = "Flag_" + colour;
        break;
    case Kind::kLight:
        s.cls = "LevelLight";
        core::at(s.colour, 0) = 255;
        core::at(s.colour, 1) = 214;
        core::at(s.colour, 2) = 170;
        s.brightness = 100.0f;
        s.inner = 150.0f;
        s.outer = 400.0f;
        break;
    case Kind::kSound:
        s.cls = "EmitterObject";
        s.radius = 300.0f;
        break;
    case Kind::kProximityNode:
        s.cls = "ProximNode";
        s.radius = 200.0f;
        break;
    }
    return s;
}

Spec current_base(const Spec& flag)
{
    Spec base = flag;
    base.name = flag.name + "_Base";
    base.type = flag.type + "_Base";
    return base;
}

std::string pinned_node_name(Kind kind, Team team)
{
    if (kind == Kind::kTeamSpawn)
        return std::string("TRG_CTF_Restart_") + get_team_name(team);
    if (kind == Kind::kFlagSet)
        return std::string("TRG_CTF_") + get_team_name(team);
    return "";
}

std::string lobby_flag_name(Team team) { return std::string("TRG_Flag_") + get_team_name(team); }

bool is_lobby_flag(const std::string& name) { return starts_with(name, "TRG_Flag_"); }

const char* name_stem(Kind kind)
{
    switch (kind) {
    case Kind::kSpawn:
    case Kind::kTeamSpawn:
        return "ed_restart";
    case Kind::kCrown:
        return "ed_crown";
    case Kind::kFlagSet:
        return "ed_flag";
    case Kind::kLight:
        return "ed_light";
    case Kind::kSound:
        return "ed_sound";
    case Kind::kProximityNode:
        return "ed_proxim";
    }
    return "ed_node";
}

std::string fresh_name(const std::string& stem, const std::function<bool(const std::string&)>& taken)
{
    for (int n = 1; n < 100000; ++n) {
        std::array<char, 16> number{};
        text::format_to(number, "%02d", n);
        std::string name = stem + number.data();
        if (!taken(name))
            return name;
    }
    return "";
}

bool valid_name(const std::string& name)
{
    if (name.empty() || name.size() > 63)
        return false;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

std::vector<Prop> get_fields(const Spec& spec)
{
    std::vector<Prop> fields;
    auto name = [&](const char* key, const std::string& value) {
        Prop f = {.type = Prop::kName, .key = key};
        f.text = value;
        fields.push_back(f);
    };
    auto flag = [&](const char* value) {
        Prop f = {.type = Prop::kFlag, .key = ""};
        f.text = value;
        fields.push_back(f);
    };
    auto vector = [&](const char* key, V3 v) {
        Prop f = {.type = Prop::kVector, .key = key};
        core::at(f.value, 0) = v.x;
        core::at(f.value, 1) = v.y;
        core::at(f.value, 2) = v.z;
        fields.push_back(f);
    };
    auto number = [&](const char* key, float v) {
        Prop f = {.type = Prop::kFloat, .key = key};
        core::at(f.value, 0) = v;
        fields.push_back(f);
    };
    auto integer = [&](const char* key, int v) {
        Prop f = {.type = Prop::kInteger, .key = key};
        f.integer = v;
        fields.push_back(f);
    };
    auto string = [&](const char* key, const std::string& v) {
        Prop f = {.type = Prop::kString, .key = key};
        f.text = v;
        fields.push_back(f);
    };
    vector("Pos", spec.pos);
    vector("Angles", spec.angles);
    name("Name", spec.name);
    name("Class", spec.cls);
    if (!spec.type.empty())
        name("Type", spec.type);
    // Chain points (level_editor_chains.h): made at the start; a rail's terrain (the rail manager reads it - an
    // unknown one would crash 0x004442C0, so a known one always: the editor's default, a grind metal).
    if (same_text(spec.cls, "RailNode") || same_text(spec.cls, "Waypoint")) {
        flag("CreatedAtStart");
        if (same_text(spec.cls, "RailNode"))
            name("TerrainType", spec.terrain.empty() ? std::string("TERRAIN_GRINDMETAL") : spec.terrain);
        return fields;
    }
    if (same_text(spec.cls, "ClimbingNode")) {
        flag("CreatedAtStart"); // (a ladder's point - its Type Ladder above: as the levels' ladders)
        fields.insert(fields.end(), spec.extra.begin(), spec.extra.end()); // (a ledge's HangLeft / HangRight)
        return fields;
    }
    Kind kind{};
    if (!kind_of(spec, &kind)) {
        fields.insert(fields.end(), spec.extra.begin(), spec.extra.end()); // (another module's node)
        return fields;
    }
    const int team = team_of(kind == Kind::kFlagSet ? spec.type : spec.name);
    const std::string colour = team >= 0 ? core::at(kTeamNames, team) : "";
    switch (kind) {
    case Kind::kSpawn:
    case Kind::kTeamSpawn: {
        flag("CreatedAtStart");
        string("RestartName", kind == Kind::kTeamSpawn ? "Team: CTF " + colour : std::string("Multi: Restart"));
        Prop types = {.type = Prop::kNames, .key = "restart_types"};
        types.list = spec.types;
        fields.push_back(types);
        break;
    }
    case Kind::kCrown:
        flag("CreatedAtStart");
        break;
    case Kind::kFlagSet: {
        const bool base = ends_with(spec.type, "_Base");
        const std::string model = "flag_" + get_lower(colour) + (base ? "_base" : "");
        flag("NeverSuspend");
        string("Model", "gameobjects\\flags\\" + model + "\\" + model + ".mdl");
        integer("lod_dist1", 800);
        integer("lod_dist2", 801);
        // (a team lobby flag, TRG_Flag_<Colour>: its level's TRG_Flag_<Colour>Script - SK4_Scripts' Create_Team_Flags
        // makes it by name in a lobby; a CTF flag's TRG_CTF_<Colour>Script)
        if (!base)
            name("TriggerScript", (is_lobby_flag(spec.name) ? "TRG_Flag_" : "TRG_CTF_") + colour + "Script");
        break;
    }
    case Kind::kLight: {
        flag("CreatedAtStart");
        number("Brightness", spec.brightness);
        number("InnerRadius", spec.inner);
        number("OuterRadius", spec.outer);
        Prop c = {.type = Prop::kIntegers, .key = "Color"};
        c.integers = {core::at(spec.colour, 0), core::at(spec.colour, 1), core::at(spec.colour, 2)};
        fields.push_back(c);
        break;
    }
    case Kind::kSound:
    case Kind::kProximityNode:
        flag("CreatedAtStart");
        name("Shape", "Sphere");
        number("Radius", spec.radius);
        break;
    }
    fields.insert(fields.end(), spec.extra.begin(), spec.extra.end());
    return fields;
}

const char* limit_message(Kind kind, const Counts& counts)
{
    switch (kind) {
    case Kind::kSpawn:
    case Kind::kTeamSpawn:
        return counts.restarts >= kMaxRestarts ? "Restart limit reached (256)" : nullptr;
    case Kind::kCrown:
        return counts.crowns >= kMaxCrowns ? "Crown limit reached (10)" : nullptr;
    case Kind::kLight:
        if (counts.dynamic_lights_ >= kMaxDynamicLights)
            return "The game's 256 dynamic lights are all in use";
        return counts.level_lights_ >= kMaxLevelLights ? "The game's 150 level lights are all in use" : nullptr;
    default:
        return nullptr;
    }
}

bool hide_leaves_no_restart(uint32_t types, uint32_t others)
{
    for (const uint32_t bit : {static_cast<uint32_t>(nodes::kPlayer1), static_cast<uint32_t>(nodes::kMultiplayer)}) {
        if ((types & bit) && !(others & bit))
            return true;
    }
    return false;
}

int get_index_of(const LinkChain& list, const std::string& name)
{
    for (size_t i = 0; i < list.size(); ++i) {
        if (same_text(core::at(list, i).name, name.c_str()))
            return static_cast<int>(i);
    }
    return -1;
}

int append_one(LinkChain* list, const Spec& spec)
{
    list->push_back(spec);
    return static_cast<int>(list->size()) - 1;
}

bool get_take(LinkChain* list, const std::string& name, Spec* spec, int* index)
{
    const int i = get_index_of(*list, name);
    if (i < 0)
        return false;
    *spec = core::at((*list), i);
    *index = i;
    list->erase(list->begin() + i);
    return true;
}

void put_back(LinkChain* list, const Spec& spec, int index)
{
    index = std::max(0, std::min(index, static_cast<int>(list->size())));
    list->insert(list->begin() + index, spec);
}

namespace {
bool is_crown(const Spec& s)
{
    Kind kind{};
    return kind_of(s, &kind) && kind == Kind::kCrown;
}
} // namespace

std::vector<std::string> crown_order(const LinkChain& list)
{
    std::vector<std::string> order;
    for (const Spec& s : list) {
        if (is_crown(s))
            order.push_back(s.name);
    }
    return order;
}

bool move_crown(LinkChain* list, const std::string& name, int delta)
{
    const int at = get_index_of(*list, name);
    if (at < 0 || !is_crown(core::at((*list), at)) || (delta != -1 && delta != 1))
        return false;
    for (int i = at + delta; i >= 0 && i < ints::as_signed(list->size()); i += delta) {
        if (is_crown(core::at((*list), i))) {
            std::swap(core::at((*list), at), core::at((*list), i));
            return true;
        }
    }
    return false;
}

void apply_crown_order(LinkChain* list, const std::vector<std::string>& order)
{
    std::vector<int> slots;
    std::vector<Spec> crowns;
    for (size_t i = 0; i < list->size(); ++i) {
        if (is_crown(core::at((*list), i))) {
            slots.push_back(static_cast<int>(i));
            crowns.push_back(core::at((*list), i));
        }
    }
    std::vector<Spec> ordered;
    std::vector<bool> used(crowns.size(), false);
    for (const std::string& name : order) {
        for (size_t c = 0; c < crowns.size(); ++c) {
            if (!core::at(used, c) && same_text(core::at(crowns, c).name, name.c_str())) {
                core::at(used, c) = true;
                ordered.push_back(core::at(crowns, c));
                break;
            }
        }
    }
    for (size_t c = 0; c < crowns.size(); ++c) {
        if (!core::at(used, c))
            ordered.push_back(core::at(crowns, c));
    }
    for (size_t i = 0; i < slots.size(); ++i)
        core::at((*list), core::at(slots, i)) = core::at(ordered, i);
}

// --- the game side ----------------------------------------------------------------------------------------------

uint32_t name_hash(const std::string& name) { return checksums::hash_from_text(name.c_str()); }

namespace {

constexpr uint32_t kClass = 0x12B4E660;  // class
constexpr uint32_t kType = 0x7321A8D6;   // type
constexpr uint32_t kPos = 0x7F261953;    // pos
constexpr uint32_t kAngles = 0x9D2D0915; // angles

// NOT ORIGINAL grouping - level_editor_node_create.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_node_create_state()).
struct LevelEditorNodeCreateState {
    // The overlay: its specs (names, classes...; places are the structures') and structures, in array order.
    LinkChain list_{};
    std::vector<script::Struct*> structs_{};
    script::Array* array_{};
    bool registered_{};
    uint32_t level_array_{}; // (the array the overlay was added after: it leaves with it)
};

LevelEditorNodeCreateState& level_editor_node_create_state()
{
    static core::NoDestroy<LevelEditorNodeCreateState> state;
    return state.get();
}

// Hidden nodes: the structure (the level's array still the one loaded) and what was active.
struct HiddenNode {
    uint32_t name;
    script::Struct* node;
    bool proxim_active_, emitter_active_;
};
std::vector<HiddenNode>& stored_hidden()
{
    static core::NoDestroy<std::vector<HiddenNode>> hidden;
    return hidden.get();
}

level_nodes::NodeManager* hub() { return level_nodes::node_manager_instance().get(); }

// The game's managers made at the level's load (null outside a level).
bool have_proxim() { return objects::proximity_hub() != nullptr; }
bool have_emitters() { return objects::emitter_table() != nullptr; }

void add_light(script::Struct* node) { render::create_light_from_node(node); }
void remove_light(uint32_t name) { render::delete_scene_light(name); }

void get_forget();

using ArrayRemovedListener = void (*)(uint32_t name);
ArrayRemovedListener& array_removed_listener() // (set_array_removed_listener)
{
    static ArrayRemovedListener listener{};
    return listener;
}

// The node manager's hook: the level's array going takes the overlay with it.
void on_array_removed(uint32_t name)
{
    if (array_removed_listener())
        array_removed_listener()(name);
    if (name == nodes::overlay_array() || !level_editor_node_create_state().registered_ || name != level_editor_node_create_state().level_array_)
        return;
    get_log("level editor: the level's node array left - its %u added nodes go with it",
            static_cast<unsigned>(level_editor_node_create_state().structs_.size()));
    get_forget();
}

gsl::owner<script::Struct*> get_build(const Spec& spec)
{
    gsl::owner<script::Struct*> node = script::spawn_param_bag();
    if (!node)
        return nullptr;
    new (node) script::Struct;
    // A chain point drawn on from another copies that one's fields (its own name, place and links aside; not a trick
    // object of the other's cluster) - level_editor_chains.h.
    script::Struct* like = spec.like && hub() ? level_nodes::fetch_vertex(spec.like, false) : nullptr;
    if (like) {
        node->merge_structure(like);
        for (const char* key : {"Name", "Pos", "Angles", "Links", "Cluster", "ed_hidden_class", "ed_hidden_type"})
            node->erase_module(name_hash(key));
        node->remove_flag(name_hash("TrickObject"));
        node->remove_flag(name_hash("ed_grind_flat")); // (its own choice: level_editor_chains.h)
    }
    for (const Prop& f : get_fields(spec)) {
        const uint32_t key = f.key.empty() ? 0u : name_hash(f.key);
        if (like && f.type == Prop::kFlag && node->has_bare_flag(name_hash(f.text)))
            continue;
        if (like && f.key == "TerrainType" && spec.terrain.empty())
            continue; // (the copied one)
        if (like && key)
            node->erase_module(key);
        switch (f.type) {
        case Prop::kFlag:
            node->put_name_key(0u, name_hash(f.text));
            break;
        case Prop::kName:
            node->put_name_key(key, name_hash(f.text));
            script::note_qb_name(name_hash(f.text), f.text.c_str());
            break;
        case Prop::kString:
            node->append_word(key, f.text.c_str());
            break;
        case Prop::kInteger:
            node->put_int(key, f.integer);
            break;
        case Prop::kFloat:
            node->push_float(key, core::at(f.value, 0));
            break;
        case Prop::kVector:
            node->put_vec3(key, core::at(f.value, 0), core::at(f.value, 1), core::at(f.value, 2));
            break;
        case Prop::kNames:
        case Prop::kIntegers: {
            gsl::owner<script::Array*> array = script::alloc_script_array();
            if (!array)
                break;
            new (array) script::Array;
            const bool names = f.type == Prop::kNames;
            auto count = static_cast<uint32_t>(names ? f.list.size() : f.integers.size());
            array->configure_size_kind(count, names ? script::kQbvalName : script::kQbvalInteger);
            for (uint32_t i = 0; i < count; ++i)
                array->set_element(i, names ? name_hash(core::at(f.list, i)) : static_cast<uint32_t>(core::at(f.integers, i)));
            node->append_array_ref(key, array);
            break;
        }
        }
    }
    return node;
}

void dealloc(script::Struct* node)
{
    if (!node)
        return;
    node->get_clear();
    script::delete_struct(node);
}

constexpr uint32_t kLinks = 0x2E7D5EE7; // links

// A structure's links= set to these names (none: removed).
void write_links(script::Struct* node, const std::vector<uint32_t>& links)
{
    node->erase_module(kLinks);
    if (links.empty())
        return;
    gsl::owner<script::Array*> array = script::alloc_script_array();
    if (!array)
        return;
    new (array) script::Array;
    array->configure_size_kind(static_cast<uint32_t>(links.size()), script::kQbvalName);
    for (size_t i = 0; i < links.size(); ++i)
        array->set_element(static_cast<uint32_t>(i), core::at(links, i));
    node->append_array_ref(kLinks, array);
}

// The overlay registered again with its structures as they are now (none: not registered).
bool get_sync()
{
    level_nodes::NodeManager* manager = hub();
    if (!manager)
        return false;
    level_nodes::set_node_array_removed_hook(&on_array_removed);
    level_nodes::set_editor_node_array(nodes::overlay_array());
    if (level_editor_node_create_state().registered_) {
        level_editor_node_create_state().registered_ = false;
        manager->remove_node_array(nodes::overlay_array());
    }
    nodes::mark_stale();
    if (level_editor_node_create_state().structs_.empty())
        return true;
    if (!level_editor_node_create_state().array_) {
        const uint32_t name = nodes::overlay_array();
        script::note_qb_name(name, nodes::kOverlayArrayName);
        if (script::SymbolTableEntry* old = script::find_symbol(name))
            script::clean_up_and_remove_symbol(old);
        gsl::owner<script::Array*> made = script::new_array(); // (the symbol's, below)
        if (!made)
            return false;
        level_editor_node_create_state().array_ = made;
        script::SymbolTableEntry* symbol = script::create_symbol(name);
        symbol->type = script::kQbvalArray;
        symbol->value.set_array(level_editor_node_create_state().array_);
    }
    if (manager->num_ >= static_cast<uint32_t>(level_nodes::NodeManager::kMaxArrays)) {
        get_log("level editor: the node manager's 20 node arrays are in use - added nodes not registered");
        return false;
    }
    level_editor_node_create_state().level_array_ = manager->get_last_node_array_name(); // (the overlay passed over: the level's)
    level_editor_node_create_state().array_->configure_size_kind(static_cast<uint32_t>(level_editor_node_create_state().structs_.size()), script::kQbvalStruct);
    for (size_t i = 0; i < level_editor_node_create_state().structs_.size(); ++i) {
        level_editor_node_create_state().array_->set_cell(static_cast<uint32_t>(i), script::Cell::of(core::at(level_editor_node_create_state().structs_, i)));
        // (links= are names already: kept out of AddNodeArray's resolve_links, which reads indices - put on after)
        core::at(level_editor_node_create_state().structs_, i)->erase_module(kLinks);
    }
    level_editor_node_create_state().registered_ = manager->add_node_array(nodes::overlay_array(), false);
    for (size_t i = 0; i < level_editor_node_create_state().structs_.size(); ++i)
        write_links(core::at(level_editor_node_create_state().structs_, i), core::at(level_editor_node_create_state().list_, i).links);
    nodes::mark_stale();
    return level_editor_node_create_state().registered_;
}

// What the game would have made for the node at the level's load (cfunc_parse_node_array), made now.
void make_live(const Spec& spec, script::Struct* node)
{
    Kind kind{};
    if (!kind_of(spec, &kind))
        return;
    const uint32_t name = name_hash(spec.name);
    const maths::Vec4 pos = {.x = spec.pos.x, .y = spec.pos.y, .z = spec.pos.z, .w = 1.0f};
    if (kind == Kind::kProximityNode && have_proxim()) {
        if (objects::ProximNode* proxim = objects::proxim_get_node(name)) {
            // (made before and removed: the game can't free one - used again)
            proxim->at_point() = pos;
            proxim->radius() = spec.radius;
            proxim->set_reach_sq(spec.radius * spec.radius);
            objects::proximity_node_enable(name, 1);
        } else {
            objects::nearby_zone_add_node(name, true);
        }
    } else if (kind == Kind::kSound && have_emitters()) {
        if (objects::EmitterNode* emitter = objects::emitter_get_node(name)) {
            emitter->at_point_ = pos;
            emitter->radius_ = spec.radius;
            emitter->reach_sq_ = spec.radius * spec.radius;
            emitter->active_ = true;
        } else {
            objects::emitter_add_node(name, true);
        }
    } else if (kind == Kind::kLight) {
        add_light(node);
    }
}

void kill_live(const Spec& spec)
{
    Kind kind{};
    if (!kind_of(spec, &kind))
        return;
    const uint32_t name = name_hash(spec.name);
    if (kind == Kind::kProximityNode && have_proxim())
        objects::proximity_node_enable(name, 0);
    else if (kind == Kind::kSound && have_emitters())
        objects::emitter_set_active(name, 0);
    else if (kind == Kind::kLight)
        remove_light(name);
}

// A spec's place as its structure has it now.
Spec with_place(const Spec& spec, script::Struct* node)
{
    Spec s = spec;
    maths::Vec4 pos, angles;
    level_nodes::fetch_place(node, &pos);
    level_nodes::read_turn_angles(node, &angles);
    s.pos = {.x = pos.x, .y = pos.y, .z = pos.z};
    s.angles = {.x = angles.x, .y = angles.y, .z = angles.z};
    return s;
}

void get_forget()
{
    level_nodes::NodeManager* manager = hub();
    if (level_editor_node_create_state().registered_ && manager) {
        level_editor_node_create_state().registered_ = false;
        manager->remove_node_array(nodes::overlay_array());
    }
    level_editor_node_create_state().registered_ = false;
    for (const Spec& spec : level_editor_node_create_state().list_)
        node_edit::get_forget(name_hash(spec.name));
    for (script::Struct* node : level_editor_node_create_state().structs_)
        dealloc(node);
    level_editor_node_create_state().structs_.clear();
    level_editor_node_create_state().list_.clear();
    if (level_editor_node_create_state().array_)
        level_editor_node_create_state().array_->configure_size_kind(0, script::kQbvalStruct);
    stored_hidden().clear();
    nodes::mark_stale();
}

bool in_level()
{
    const level_nodes::NodeManager* manager = hub();
    return manager && manager->num_ > (level_editor_node_create_state().registered_ ? 1u : 0u) && render::current_level() != 0;
}

} // namespace

void set_array_removed_listener(void (*listener)(uint32_t name))
{
    array_removed_listener() = listener;
    level_nodes::set_node_array_removed_hook(&on_array_removed);
}

LinkChain at_hand()
{
    LinkChain list;
    for (size_t i = 0; i < level_editor_node_create_state().list_.size(); ++i)
        list.push_back(with_place(core::at(level_editor_node_create_state().list_, i), core::at(level_editor_node_create_state().structs_, i)));
    return list;
}

bool is_added(uint32_t name)
{
    for (const Spec& spec : level_editor_node_create_state().list_) {
        if (name_hash(spec.name) == name)
            return true;
    }
    return false;
}

bool name_taken(const std::string& name)
{
    const uint32_t checksum = name_hash(name);
    if (get_index_of(level_editor_node_create_state().list_, name) >= 0)
        return true;
    if (hub() && level_nodes::fetch_vertex(checksum, false))
        return true;
    return objects::find_object(checksum) != nullptr;
}

Counts counts_now()
{
    Counts counts;
    static const uint32_t kRestart = name_hash("Restart"), kCrown = name_hash("Crown");
    for (const nodes::Elem& node : nodes::get_all()) {
        if (node.hidden)
            continue;
        if (node.type == kCrown)
            ++counts.crowns; // (SpawnCrown counts the type, whatever the class)
        if (node.cls == kRestart)
            ++counts.restarts;
    }
    counts.dynamic_lights_ = render::light_sets().dynamic_light_count();
    counts.level_lights_ = render::light_sets().scene_light_count();
    return counts;
}

const char* get_why_not(Kind kind, Team team)
{
    if (!in_level())
        return "Not in a level";
    if (const char* limit = limit_message(kind, counts_now()))
        return limit;
    const std::string fixed = pinned_node_name(kind, team);
    if (!fixed.empty() && (name_taken(fixed) || (kind == Kind::kFlagSet && name_taken(fixed + "_Base"))))
        return kind == Kind::kFlagSet ? "This team flag already exists" : "This team spawn already exists";
    return nullptr;
}

const char* why_not_lobby_flag(Team team)
{
    if (!in_level())
        return "Not in a level";
    if (name_taken(lobby_flag_name(team)))
        return "This team flag already exists";
    return nullptr;
}

bool fabricate(const Spec& spec, int index, std::string* message)
{
    const LinkChain one = {spec};
    if (!in_level()) {
        *message = "Not in a level";
        return false;
    }
    if (!valid_name(spec.name) || name_taken(spec.name)) {
        *message = "A node named " + spec.name + " is there already";
        return false;
    }
    gsl::owner<script::Struct*> node = get_build(spec);
    if (!node) {
        *message = "Couldn't make the node";
        return false;
    }
    if (index < 0 || std::cmp_greater(index, level_editor_node_create_state().list_.size()))
        index = static_cast<int>(level_editor_node_create_state().list_.size());
    level_editor_node_create_state().list_.insert(level_editor_node_create_state().list_.begin() + index, spec);
    level_editor_node_create_state().structs_.insert(level_editor_node_create_state().structs_.begin() + index, node);
    if (!get_sync()) {
        level_editor_node_create_state().list_.erase(level_editor_node_create_state().list_.begin() + index);
        level_editor_node_create_state().structs_.erase(level_editor_node_create_state().structs_.begin() + index);
        dealloc(node);
        get_sync();
        *message = "Couldn't add it to the node manager";
        return false;
    }
    make_live(spec, node);
    get_log("level editor: added %s (%s %s) at (%.1f, %.1f, %.1f)", spec.name.c_str(), spec.cls.c_str(),
            spec.type.c_str(), spec.pos.x, spec.pos.y, spec.pos.z);
    return true;
}

int create_all(const LinkChain& specs, std::string* message)
{
    if (!in_level()) {
        *message = "Not in a level";
        return 0;
    }
    std::vector<std::pair<Spec, script::Struct*>> made;
    for (const Spec& spec : specs) {
        bool duplicate = false;
        for (const auto& m : made)
            duplicate = duplicate || same_text(m.first.name, spec.name.c_str());
        if (!valid_name(spec.name) || duplicate || name_taken(spec.name)) {
            get_log("level editor: the layout's node %s is there already - skipped", spec.name.c_str());
            continue;
        }
        if (gsl::owner<script::Struct*> node = get_build(spec)) {
            level_editor_node_create_state().list_.push_back(spec);
            level_editor_node_create_state().structs_.push_back(node);
            made.emplace_back(spec, node);
        }
    }
    if (made.empty())
        return 0;
    if (!get_sync()) {
        for (size_t i = 0; i < made.size(); ++i) {
            level_editor_node_create_state().list_.pop_back();
            dealloc(level_editor_node_create_state().structs_.back());
            level_editor_node_create_state().structs_.pop_back();
        }
        get_sync();
        *message = "Couldn't add the layout's nodes to the node manager";
        return 0;
    }
    for (const auto& m : made)
        make_live(m.first, m.second);
    return static_cast<int>(made.size());
}

bool drop_out(uint32_t name, Spec* spec, int* index)
{
    for (size_t i = 0; i < level_editor_node_create_state().list_.size(); ++i) {
        if (name_hash(core::at(level_editor_node_create_state().list_, i).name) != name)
            continue;
        *spec = with_place(core::at(level_editor_node_create_state().list_, i), core::at(level_editor_node_create_state().structs_, i));
        *index = static_cast<int>(i);
        script::Struct* node = core::at(level_editor_node_create_state().structs_, i);
        kill_live(*spec); // (the light lists by name - before the node goes)
        level_editor_node_create_state().list_.erase(level_editor_node_create_state().list_.begin() + static_cast<std::ptrdiff_t>(i));
        level_editor_node_create_state().structs_.erase(level_editor_node_create_state().structs_.begin() + static_cast<std::ptrdiff_t>(i));
        get_sync();
        node_edit::get_forget(name);
        dealloc(node);
        get_log("level editor: removed the added node %s", spec->name.c_str());
        return true;
    }
    return false;
}

bool set_links(uint32_t name, const std::vector<uint32_t>& links)
{
    for (size_t i = 0; i < level_editor_node_create_state().list_.size(); ++i) {
        if (name_hash(core::at(level_editor_node_create_state().list_, i).name) != name)
            continue;
        core::at(level_editor_node_create_state().list_, i).links = links;
        write_links(core::at(level_editor_node_create_state().structs_, i), links);
        nodes::mark_stale();
        return true;
    }
    return false;
}

std::vector<std::string> get_order()
{
    std::vector<std::string> names;
    for (const Spec& spec : level_editor_node_create_state().list_)
        names.push_back(spec.name);
    return names;
}

void set_order(const std::vector<std::string>& names)
{
    LinkChain list;
    std::vector<script::Struct*> structs;
    std::vector<bool> used(level_editor_node_create_state().list_.size(), false);
    for (const std::string& name : names) {
        const int i = get_index_of(level_editor_node_create_state().list_, name);
        if (i >= 0 && !core::at(used, i)) {
            core::at(used, i) = true;
            list.push_back(core::at(level_editor_node_create_state().list_, i));
            structs.push_back(core::at(level_editor_node_create_state().structs_, i));
        }
    }
    for (size_t i = 0; i < level_editor_node_create_state().list_.size(); ++i) {
        if (!core::at(used, i)) {
            list.push_back(core::at(level_editor_node_create_state().list_, i));
            structs.push_back(core::at(level_editor_node_create_state().structs_, i));
        }
    }
    level_editor_node_create_state().list_.swap(list);
    level_editor_node_create_state().structs_.swap(structs);
    get_sync();
}

bool move_crown(uint32_t name, int delta, std::string* message)
{
    for (const Spec& spec : level_editor_node_create_state().list_) {
        if (name_hash(spec.name) != name)
            continue;
        LinkChain list = level_editor_node_create_state().list_;
        if (!node_create::move_crown(&list, spec.name, delta)) {
            *message = delta < 0 ? "Already the first added crown" : "Already the last crown";
            return false;
        }
        std::vector<std::string> order;
        for (const Spec& s : list)
            order.push_back(s.name);
        set_order(order);
        return true;
    }
    *message = "Only added crowns can be reordered";
    return false;
}

void apply_crown_order(const std::vector<std::string>& order)
{
    LinkChain list = level_editor_node_create_state().list_;
    node_create::apply_crown_order(&list, order);
    std::vector<std::string> names;
    for (const Spec& s : list)
        names.push_back(s.name);
    set_order(names);
}

bool can_hide(const nodes::Elem& node, const char** why)
{
    *why = nullptr;
    if (node.added) {
        *why = "Added nodes are deleted, not hidden";
        return false;
    }
    if (!node.name) {
        *why = "A node without a name can't be hidden";
        return false;
    }
    switch (node.family) {
    case nodes::Family::kSpawn:
    case nodes::Family::kCrown:
    case nodes::Family::kBitMark:
    case nodes::Family::kLight:
    case nodes::Family::kSound:
    case nodes::Family::kFireOff:
        break;
    default:
        *why = "This node type can't be hidden";
        return false;
    }
    if (node.hidden)
        return true;
    if (node.family == nodes::Family::kCrown && nodes::walked(nodes::Family::kCrown) <= 1) {
        *why = "Can't hide the last crown";
        return false;
    }
    if (node.family == nodes::Family::kSpawn) {
        uint32_t others = 0;
        for (const nodes::Elem& n : nodes::get_all()) {
            if (n.family == nodes::Family::kSpawn && !n.hidden && n.name != node.name)
                others |= n.restart_types_;
        }
        if (hide_leaves_no_restart(node.restart_types_, others)) {
            *why = "Can't hide the last start point";
            return false;
        }
    }
    return true;
}

bool get_hidden(uint32_t name)
{
    return std::ranges::any_of(stored_hidden(), [&](const HiddenNode& h) { return h.name == name; });
}

std::vector<uint32_t> hidden_names()
{
    std::vector<uint32_t> names;
    names.reserve(stored_hidden().size());
    for (const HiddenNode& h : stored_hidden())
        names.push_back(h.name);
    return names;
}

bool conceal(uint32_t name, bool hide)
{
    if (!hub() || is_added(name))
        return false;
    script::Struct* node = level_nodes::fetch_vertex(name, false);
    if (!node)
        return false;
    auto it = std::ranges::find_if(stored_hidden(), [&](const HiddenNode& h) { return h.name == name; });
    static const uint32_t kLight = name_hash("LevelLight"), kProxim = name_hash("ProximNode"),
                          kEmitter = name_hash("EmitterObject");
    if (hide) {
        if (it != stored_hidden().end())
            return true;
        HiddenNode h = {.name = name, .node = node, .proxim_active_ = false, .emitter_active_ = false};
        uint32_t cls = 0, type = 0;
        node->read_hash_value(kClass, &cls, 0);
        node->read_hash_value(kType, &type, 0);
        if (cls == nodes::hidden_marker())
            return false;
        node->put_name_key(nodes::hidden_class_key(), cls);
        node->put_name_key(nodes::hidden_type_key(), type);
        node->put_name_key(kClass, nodes::hidden_marker());
        node->put_name_key(kType, nodes::hidden_marker());
        if (cls == kProxim && have_proxim()) {
            if (const objects::ProximNode* proxim = objects::proxim_get_node(name)) {
                h.proxim_active_ = proxim->active();
                objects::proximity_node_enable(name, 0);
            }
        }
        if (cls == kEmitter && have_emitters()) {
            if (const objects::EmitterNode* emitter = objects::emitter_get_node(name)) {
                h.emitter_active_ = emitter->active_;
                objects::emitter_set_active(name, 0);
            }
        }
        if (cls == kLight)
            remove_light(name);
        // (a flag a CTF game made: killed - it comes back when the game makes it again)
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(name));
        if (object && !(object->object_flags() & objects::kObjectFlagDead) && cls != kProxim && cls != kEmitter &&
            cls != kLight)
            object->flag_as_dead();
        stored_hidden().push_back(h);
        get_log("level editor: hid the level's node %s", object_edit::caption(name).c_str());
    } else {
        if (it == stored_hidden().end())
            return true;
        const HiddenNode h = *it;
        stored_hidden().erase(it);
        uint32_t cls = 0, type = 0;
        node->read_hash_value(nodes::hidden_class_key(), &cls, 0);
        node->read_hash_value(nodes::hidden_type_key(), &type, 0);
        node->erase_module(nodes::hidden_class_key());
        node->erase_module(nodes::hidden_type_key());
        node->erase_module(kClass);
        node->erase_module(kType);
        node->put_name_key(kClass, cls);
        if (type)
            node->put_name_key(kType, type);
        if (cls == kProxim && have_proxim() && h.proxim_active_)
            objects::proximity_node_enable(name, 1);
        if (cls == kEmitter && have_emitters() && h.emitter_active_)
            objects::emitter_set_active(name, 1);
        if (cls == kLight)
            add_light(node);
        get_log("level editor: showed the level's node %s again", object_edit::caption(name).c_str());
    }
    nodes::mark_stale();
    return true;
}

void reset_all()
{
    const std::vector<uint32_t> hidden = hidden_names();
    for (const uint32_t name : hidden)
        conceal(name, false);
    const std::vector<std::string> names = get_order();
    for (const std::string& name : names) {
        Spec spec;
        int index = 0;
        drop_out(name_hash(name), &spec, &index);
    }
}

void forget_level()
{
    // (a level whose node array stays loaded: hidden nodes shown, added ones removed; another level's: forgotten)
    if (hub()) {
        for (const HiddenNode& h : std::vector<HiddenNode>(stored_hidden())) {
            if (level_nodes::fetch_vertex(h.name, false) == h.node)
                conceal(h.name, false);
        }
    }
    get_forget();
}

} // namespace frontend::level_editor::node_create

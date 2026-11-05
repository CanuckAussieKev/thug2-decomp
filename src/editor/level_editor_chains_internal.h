#pragma once

// NOT ORIGINAL - what level_editor_chains.cpp's pieces share (the chains' state, the graph and the helpers more than
// one piece uses). They were the one file's anonymous namespace; here they are in `internal`, with a using-directive,
// so every piece sees them as it saw them there.

#include "editor/level_editor_chains.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include "editor/level_editor.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "game/world/trick_objects.h"
#include "game/commands/game_cfuncs.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "game/skate/skate_module.h"
#include <gsl/pointers>
#include "base/maths/vec4.h"

namespace frontend::level_editor::chains {

namespace internal {

bool same_text(const std::string& a, const char* b);

template <typename T>
void sort_by_name(std::vector<T>* list)
{
    std::sort(list->begin(), list->end(), [](const T& a, const T& b) { return a.name < b.name; });
}

constexpr uint32_t kSurfaceKindKey = 0x54CF8532; // TerrainType

uint32_t name_hash(const std::string& text);

bool in_level();

script::Struct* look_up_node_params(uint32_t name);

objects::GrindRailHub* get_rails();

objects::GrindPoint* rail_node_named(uint32_t name);

std::vector<uint32_t> own_links(script::Struct* node);

void set_own_links(script::Struct* node, const std::vector<uint32_t>& links);

void write_pos(script::Struct* node, V3 pos);

void write_angles(script::Struct* node, V3 angles);

uint32_t class_of(script::Struct* node);

uint32_t rail_node_class();

uint32_t climbing_node_class();

uint32_t pedestrian_class();

uint32_t vehicle_class();

bool known_terrain(uint32_t terrain);

bool terrain_byte(const std::string& name, uint8_t* value);

// What the editor has done to the level's own nodes: links= as the level had them (the structure, to know it's still
// the level loaded), the dropped points (and whether their rail node was on), terrains set (and the level's bytes).
struct OriginalLinks {
    script::Struct* node{};
    std::vector<uint32_t> links;
};

std::map<uint32_t, OriginalLinks>& original_links();

std::vector<uint32_t>& stored_dropped();

std::map<uint32_t, bool>& drop_active();

struct TerrainSet {
    std::string terrain;
    uint8_t original{};
};

std::map<uint32_t, TerrainSet>& terrains_state();

// NOT ORIGINAL grouping - level_editor_chains.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_chains_state()).
struct LevelEditorChainsState {
    // The rail manager's node arrays replaced by rebuild_rails (freed with the level).
    std::vector<void*> retired_;
    objects::GrindPoint* built_{}; // (the node array rebuild_rails made last)
    // Objects to make again from their nodes (their path changed) once the game has removed them.
    std::vector<uint32_t> remake_;
    // Drawing: the kind, the last point drawn (0: none yet). An armed pick: what for, and from which node.
    Blit draw_ = Blit::kNone;
    uint32_t draw_last_{};
    ChainOp pick_ = ChainOp::kCancel;
    uint32_t pick_from_{};
    // Drawing a ladder: its foot (once the first click found a wall) and its heading.
    bool ladder_have_foot_{};
    V3 ladder_foot_{};
    float ladder_facing_{};
    // The scenery piece the last snapped click hit (0: none) - a new rail's trick-object cluster.
    uint32_t snap_sector_{};
};

LevelEditorChainsState& level_editor_chains_state();

// Added points' fields that aren't in their nodes (by name): a rail's cluster, grindable on flat ground.
struct Extras {
    uint32_t cluster = 0;
    bool grind_flat_ = false;
    float speed = 0.0F; // (a speed-up's rate - apply_speeds)
};

constexpr uint32_t kTriggerScriptKey = 0x2CA8A299; // TriggerScript

std::map<uint32_t, float>& level_speeds();

// The speed-ups in place: each node's structure, its own TriggerScript before ours (to put back) and the script that
// counted then (called first by ours); the per-point scripts last defined.
struct SpeedApplied {
    script::Struct* node = nullptr;
    fields::Own original;
    uint32_t original_script_ = 0;
};

std::map<uint32_t, SpeedApplied>& speed_applied();

// The level's own speed-ups (a RailAccelerate in a rail point's TriggerScript): found once for each rail manager
// array (name, rate).
struct LevelSpeedCache {
    const objects::GrindPoint* nodes = nullptr;
    int count = -1;
    std::vector<std::pair<uint32_t, float>> found;
};

LevelSpeedCache& level_speed_cache();

std::unordered_map<uint32_t, Extras>& extras_state();

// Level ladder points turned: their facing and the rail manager's orientation as the level had it.
struct FacingSet {
    float facing;
    maths::Vec4 original;
};

std::map<uint32_t, FacingSet>& facings_state();

std::vector<uint32_t>& level_flat();

std::map<uint32_t, uint32_t>& trick_state();

// RailClear's override (flat_grindable): the rail manager's nodes whose segment to the next is grindable on flat
// ground, found again when the nodes or the marks change.
struct FlatCache {
    const objects::GrindPoint* nodes = nullptr;
    int count = -1;
    unsigned generation = ~0U;
    std::vector<const objects::GrindPoint*> marked;
};

// NOT ORIGINAL grouping - the flattened chains and their generation (were globals). Made on first use, never destroyed
// (flat_chains()).
struct FlatChains {
    FlatCache flat_cache_{};
    unsigned flat_generation_{};
};

FlatChains& flat_chains();

std::vector<Point> added_points();

node_create::Spec spec_of(const Point& chain_point);

maths::Vec4 climb_of(float facing);

float facing_now(uint32_t name);

float facing_original(uint32_t name);

bool same_facing(float a, float b);

std::vector<uint32_t> level_links_now(uint32_t name);

std::vector<uint32_t> level_links_original(uint32_t name);

void set_level_links(uint32_t name, const std::vector<uint32_t>& links);

void cuboid(objects::GrindPoint* node);

void get_remake(uint32_t name);

void register_trick_objects();

void set_dropped_states(std::span<objects::GrindPoint> nodes, std::unordered_map<uint32_t, int>& index);

void link_rail_nodes(std::span<objects::GrindPoint> nodes, const std::unordered_map<uint32_t, int>& index);

void refresh_flat(objects::GrindRailHub* rails);

bool flat_hook(const maths::Vec4* a, const maths::Vec4* b);

Graph graph_of(nodes::Family family);

nodes::Family family_of_name(uint32_t name);

} // namespace internal

// What the source file's anonymous namespace held that its pieces share: visible as it was.
using namespace internal;

} // namespace frontend::level_editor::chains

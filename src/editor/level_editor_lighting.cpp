// NOT ORIGINAL - the level editor's per-piece lighting switches: light group and no skater shadow (ours). See
// level_editor_lighting.h.

#include "editor/level_editor_lighting.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_ui.h"
#include "engine/gfx/mesh_records.h"
#include "engine/gfx/sector.h"
#include "engine/script/param_struct.h"
#include "imgui.h"

namespace frontend::level_editor::lighting {

namespace {

constexpr uint32_t kMeshNoShadow = 2;  // a mesh flag: no projected shadow drawn on it (shadow_base.cpp)
constexpr int kTickEvery = 10;         // the edits looked at again every this many frames
constexpr const char* kNoShadowName = "ed_no_skater_shadow";

const std::array<Group, kGroups> kGroupList = {{{.label = "Outdoor", .name = "Outdoor"},
                                                {.label = "Indoor", .name = "Indoor"},
                                                {.label = "No Level Lights", .name = "NoLevelLights"}}};

uint32_t key_light_group()
{
    static const uint32_t kKey = fields::hash_of_name("LightGroup");
    return kKey;
}
uint32_t key_no_shadow()
{
    static const uint32_t kKey = fields::hash_of_name(kNoShadowName);
    return kKey;
}
bool is_piece_class(uint32_t cls)
{
    static const uint32_t kGeometry = fields::hash_of_name("LevelGeometry");
    static const uint32_t kObject = fields::hash_of_name("LevelObject");
    return cls == kGeometry || cls == kObject;
}

// What was put on a sector, and what it had before (put back when it's no longer wanted).
struct GroupApplied {
    render::WorldCell* sector{};
    uint32_t before{};
};
struct ShadowApplied {
    render::WorldCell* sector{};
    std::vector<bool> before; // each mesh's flag 2
};

// NOT ORIGINAL grouping - this module's state. Made on first use, never destroyed (lighting_state()).
struct LightingState {
    int frame_ = 0;
    uint32_t generation_ = 0xFFFFFFFF;
    std::vector<uint32_t> baked_; // pieces whose node has ed_no_skater_shadow as the level loaded it (a bake)
    std::map<uint32_t, GroupApplied> groups_;
    std::map<uint32_t, ShadowApplied> shadows_;
};

LightingState& lighting_state()
{
    static core::NoDestroy<LightingState> state;
    return state.get();
}

scenery::GeomMeshes meshes_of(render::WorldCell* sector)
{
    const auto* geom = static_cast<const render::Geom*>(sector->geom_handle());
    return geom ? scenery::meshes_of(geom) : scenery::GeomMeshes{};
}

void set_no_shadow(uint32_t piece, render::WorldCell* sector)
{
    ShadowApplied applied{.sector = sector, .before = {}};
    const scenery::GeomMeshes meshes = meshes_of(sector);
    for (uint32_t i = 0; i < meshes.count; ++i) {
        render::MeshData* mesh = meshes.at(i);
        applied.before.push_back(mesh != nullptr && ((mesh->flags() & kMeshNoShadow) != 0U));
        if (mesh) {
            mesh->flags() |= kMeshNoShadow;
        }
    }
    lighting_state().shadows_[piece] = applied;
}

void put_shadow_back(const ShadowApplied& applied)
{
    const scenery::GeomMeshes meshes = meshes_of(applied.sector);
    for (uint32_t i = 0; i < meshes.count && i < applied.before.size(); ++i) {
        if (render::MeshData* mesh = meshes.at(i)) {
            mesh->flags() = core::at(applied.before, i) ? (mesh->flags() | kMeshNoShadow) : (mesh->flags() & ~kMeshNoShadow);
        }
    }
}

// Everything put back (`restore` false: the sectors are gone - forgotten only).
void put_all_back(bool restore)
{
    if (restore) {
        for (const auto& entry : lighting_state().groups_) {
            if (scenery::sector_of(entry.first) == entry.second.sector) {
                entry.second.sector->set_light_group(entry.second.before);
            }
        }
        for (const auto& entry : lighting_state().shadows_) {
            if (scenery::sector_of(entry.first) == entry.second.sector) {
                put_shadow_back(entry.second);
            }
        }
    }
    lighting_state().groups_.clear();
    lighting_state().shadows_.clear();
}

// The level's pieces with the shadow flag in their node (a baked level's), read again when the nodes change; records
// of sectors no longer there forgotten.
void scan_nodes()
{
    LightingState& state = lighting_state();
    state.generation_ = nodes::get_generation();
    state.baked_.clear();
    for (const nodes::Elem& node : nodes::get_all()) {
        if (node.added || node.hidden || !is_piece_class(node.cls)) {
            continue;
        }
        if (script::Struct* structure = nodes::struct_of(node); structure && structure->has_bare_flag(key_no_shadow())) {
            state.baked_.push_back(node.name);
        }
    }
    std::erase_if(state.groups_, [](const auto& entry) { return scenery::sector_of(entry.first) != entry.second.sector; });
    std::erase_if(state.shadows_, [](const auto& entry) { return scenery::sector_of(entry.first) != entry.second.sector; });
}

// The group a piece's node gives it now (Outdoor without one - as ParseNodeArray).
uint32_t node_group(uint32_t piece)
{
    const int index = nodes::get_find(piece);
    uint32_t group = render::kOutdoorLightGroup;
    if (index >= 0) {
        if (script::Struct* structure = nodes::struct_of(core::at(nodes::get_all(), index))) {
            structure->read_hash_value(key_light_group(), &group, 0);
        }
    }
    return group;
}

bool node_no_shadow(uint32_t piece)
{
    const int index = nodes::get_find(piece);
    script::Struct* structure = index >= 0 ? nodes::struct_of(core::at(nodes::get_all(), index)) : nullptr;
    return (structure != nullptr) && structure->has_bare_flag(key_no_shadow());
}

} // namespace

const Group& group_at(int index) { return core::at(kGroupList, std::clamp(index, 0, kGroups - 1)); }

void tick()
{
    LightingState& state = lighting_state();
    if (layout::get_refused()) {
        put_all_back(true); // (edited levels are offline only)
        return;
    }
    if (++state.frame_ < kTickEvery && state.generation_ == nodes::get_generation()) {
        return;
    }
    state.frame_ = 0;
    if (state.generation_ != nodes::get_generation()) {
        scan_nodes();
    }
    // What's wanted: the edited pieces' groups, and the pieces whose node has the shadow flag (edited or baked).
    std::map<uint32_t, uint32_t> groups;
    std::vector<uint32_t> candidates = state.baked_;
    for (const fields::NodeEdits& node_edits : fields::get_changed()) {
        for (const fields::Edit& edit : node_edits.edits) {
            if (edit.key == key_light_group()) {
                groups[node_edits.name] = node_group(node_edits.name);
            } else if (edit.key == key_no_shadow()) {
                candidates.push_back(node_edits.name);
            }
        }
    }
    std::vector<uint32_t> no_shadow;
    for (const uint32_t piece : candidates) {
        if (node_no_shadow(piece) && std::ranges::find(no_shadow, piece) == no_shadow.end()) {
            no_shadow.push_back(piece); // (the node as it is now: an edit, its undo, a bake)
        }
    }
    // Light groups: set where wanted, put back where no longer.
    for (auto it = state.groups_.begin(); it != state.groups_.end();) {
        if (!groups.contains(it->first)) {
            it->second.sector->set_light_group(it->second.before);
            it = state.groups_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [piece, group] : groups) {
        render::WorldCell* sector = scenery::sector_of(piece);
        if (!sector) {
            continue;
        }
        if (!state.groups_.contains(piece)) {
            state.groups_[piece] = GroupApplied{.sector = sector, .before = sector->light_group()};
        }
        sector->set_light_group(group);
    }
    // No skater shadow likewise.
    for (auto it = state.shadows_.begin(); it != state.shadows_.end();) {
        if (std::ranges::find(no_shadow, it->first) == no_shadow.end()) {
            put_shadow_back(it->second);
            it = state.shadows_.erase(it);
        } else {
            ++it;
        }
    }
    for (const uint32_t piece : no_shadow) {
        render::WorldCell* sector = scenery::sector_of(piece);
        if (sector && !state.shadows_.contains(piece)) {
            set_no_shadow(piece, sector);
        }
    }
}

void inspector_section(uint32_t sector, std::vector<ui::Request>* requests)
{
    const int index = sector != 0U ? nodes::get_find(sector) : -1;
    if (index < 0) {
        return;
    }
    const nodes::Elem& node = core::at(nodes::get_all(), index);
    if (node.added || node.hidden || !is_piece_class(node.cls)) {
        return;
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Lighting");
    const uint32_t group = node_group(sector);
    int chosen = -1;
    for (int i = 0; i < kGroups; ++i) {
        chosen = fields::hash_of_name(group_at(i).name) == group ? i : chosen;
    }
    const std::string shown =
        chosen >= 0 ? std::string(group_at(chosen).label) : fields::get_name_text(group, fields::get_name_of);
    if (ImGui::BeginCombo("Light Group", shown.c_str())) {
        for (int i = 0; i < kGroups; ++i) {
            if (ImGui::Selectable(group_at(i).label, i == chosen) && i != chosen) {
                ui::Request request = {.kind = ui::Request::Kind::kFieldSet};
                request.node = sector;
                request.id = key_light_group();
                request.value = static_cast<int>(fields::Kind::kName);
                request.text = group_at(i).name;
                request.label = group_at(i).name;
                requests->push_back(request);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Outdoor: tinted by the time of day. Indoor: no time-of-day tint, no level lights. "
                     "No Level Lights: tinted, no level lights. Full effect on reload");
    }
    bool no_shadow = node_no_shadow(sector);
    if (ImGui::Checkbox("No Skater Shadow", &no_shadow)) {
        ui::Request request = {.kind = no_shadow ? ui::Request::Kind::kFieldSet : ui::Request::Kind::kFieldRemove};
        request.node = sector;
        request.id = key_no_shadow();
        request.value = static_cast<int>(fields::Kind::kBitMark);
        request.text = "on";
        request.label = kNoShadowName;
        requests->push_back(request);
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("No skater or ped shadow drawn on this piece (this game only)");
    }
}

} // namespace frontend::level_editor::lighting

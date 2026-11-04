// NOT ORIGINAL - the level editor's Inspector row for HORSE spots (ours; the feature map - level_editor_ui.h): a restart
// whose restart_types has Horse is one of the spots a HORSE game sends the skaters to (GameObjects::get_restart_nodes 0x0046B6C0
// reads the node's restart_types each time it's asked - live). The checkbox adds / takes out Horse through the
// Inspector's own field edit (a kFieldSet of restart_types: one undo step, the `set` record's f.restart_types=, put back
// by Home / Reset level). The H badge on the gizmo is drawn with the spawns (level_editor.cpp). Only ImGui's API here.

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

#include "editor/level_editor_fields.h"
#include "engine/core/enum_ops.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_ui.h"
#include "engine/script/param_struct.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::ui {

void spawn_section(int node_index, std::vector<Request>* requests)
{
    const std::vector<nodes::Elem>& all = nodes::get_all();
    if (node_index < 0 || std::cmp_greater_equal(node_index, all.size())) {
        return;
    }
    const nodes::Elem& node = core::at(all, node_index);
    if (node.family != nodes::Family::kSpawn || node.hidden) {
        return;
    }
    static const uint32_t kHorse = fields::hash_of_name("Horse");
    // (the restart_types as the node has them now, in order)
    std::vector<uint32_t> types;
    if (script::Struct* structure = nodes::struct_of(node)) {
        script::Array* list = nullptr;
        if (structure->fetch_list(fields::key_restart_types(), &list, 0) && list) {
            for (uint32_t i = 0; i < list->extent(); ++i) {
                types.push_back(list->fetch_part(i));
            }
        }
    }
    bool horse = core::any(node.restart_types_ & nodes::SpawnPointKind::kHorse);
    ImGui::BeginDisabled(node.added);
    if (ImGui::Checkbox("HORSE Spot", &horse)) {
        std::string text;
        for (const uint32_t type : types) {
            if (type == kHorse) {
                continue;
            }
            text += (text.empty() ? "" : ", ") + fields::get_name_text(type, fields::get_name_of);
        }
        if (horse) {
            text += std::string(text.empty() ? "" : ", ") + "Horse";
        }
        Request request = {.kind = Request::Kind::kFieldSet};
        request.node = node.name;
        request.id = fields::key_restart_types();
        request.value = static_cast<int>(fields::Kind::kNames);
        request.text = text;
        requests->push_back(request);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        gui::tooltip("%s", node.added ? "Set by the spawn type" : "Use as a HORSE spot");
    }
}

} // namespace frontend::level_editor::ui

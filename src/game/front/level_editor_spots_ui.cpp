// NOT ORIGINAL - the level editor's Inspector row for HORSE spots (ours; the feature map - level_editor_ui.h): a restart
// whose restart_types has Horse is one of the spots a HORSE game sends the skaters to (Obj::GetRestartNodes 0x0046B6C0
// reads the node's restart_types each time it's asked - live). The checkbox adds / takes out Horse through the
// Inspector's own field edit (a kFieldSet of restart_types: one undo step, the `set` record's f.restart_types=, put back
// by Home / Reset level). The H badge on the gizmo is drawn with the spawns (level_editor.cpp). Only ImGui's API here.

#include <string>
#include <vector>

#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_ui.h"
#include "game/script/struct.h"
#include "imgui.h"

namespace Front::LevelEditor::Ui {

void SpawnSection(int nodeIndex, std::vector<Request>* requests)
{
    const std::vector<Nodes::Node>& all = Nodes::All();
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(all.size()))
        return;
    const Nodes::Node& node = all[nodeIndex];
    if (node.family != Nodes::Family::Spawn || node.hidden)
        return;
    static const uint32_t kHorse = Fields::NameChecksum("Horse");
    // (the restart_types as the node has them now, in order)
    std::vector<uint32_t> types;
    if (Script::CStruct* s = Nodes::StructOf(node)) {
        Script::CArray* list = nullptr;
        if (s->GetArray(Fields::KeyRestartTypes(), &list, 0) && list) {
            for (uint32_t i = 0; i < list->m_size; ++i)
                types.push_back(list->GetElement(i));
        }
    }
    bool horse = (node.restartTypes & Nodes::kHorse) != 0;
    ImGui::BeginDisabled(node.added);
    if (ImGui::Checkbox("HORSE Spot", &horse)) {
        std::string text;
        for (uint32_t type : types) {
            if (type == kHorse)
                continue;
            text += (text.empty() ? "" : ", ") + Fields::NameText(type, Fields::NameOf);
        }
        if (horse)
            text += std::string(text.empty() ? "" : ", ") + "Horse";
        Request request = {Request::kFieldSet};
        request.node = node.name;
        request.id = Fields::KeyRestartTypes();
        request.value = static_cast<int>(Fields::Kind::Names);
        request.text = text;
        requests->push_back(request);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", node.added ? "Set by the spawn type" : "Use as a HORSE spot");
}

} // namespace Front::LevelEditor::Ui

// NOT ORIGINAL - the level editor's Inspector section for chains (ours; level_editor_chains.h, level_editor_ui.h): a
// rail's / path's point's operations, a rail's terrain, a ped's / car's path. Only ImGui's own API here.

#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/front/level_editor_chains.h"
#include "game/front/level_editor_ui.h"
#include "imgui.h"

namespace Front::LevelEditor::Ui {

namespace {

void Ask(std::vector<Request>* requests, int op, uint32_t arg = 0)
{
    Request request = {Request::kChainOp};
    request.value = op;
    request.id = arg;
    requests->push_back(request);
}

// A button that can be off (its tooltip says what it does, or why not).
void OpButton(const char* label, bool can, const char* what, const char* whyNot, std::vector<Request>* requests, int op)
{
    ImGui::BeginDisabled(!can);
    if (ImGui::Button(label))
        Ask(requests, op);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", can ? what : whyNot);
}

// "TERRAIN_GRINDMETAL" -> "Metal" (the list's labels).
std::string TerrainLabel(const std::string& name)
{
    const std::string prefix = "TERRAIN_GRIND";
    std::string label = name.compare(0, prefix.size(), prefix) == 0 ? name.substr(prefix.size()) : name;
    for (size_t i = 1; i < label.size(); ++i)
        label[i] = static_cast<char>(label[i] >= 'A' && label[i] <= 'Z' ? label[i] - 'A' + 'a' : label[i]);
    return label;
}

} // namespace

void ChainSection(const Chains::View& chain, std::vector<Request>* requests)
{
    if (!chain.show)
        return;
    if (chain.object) {
        ImGui::SeparatorText("Path");
        if (chain.pathStart.empty())
            ImGui::TextDisabled("No path (it stands, or its script moves it)");
        else
            ImGui::Text("Starts at %s", chain.pathStart.c_str());
        if (ImGui::Button("Assign a path..."))
            Ask(requests, Chains::kOpAssignArm);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Then click the waypoint its path starts at. It is made again from its node at once\n"
                              "(the game reads a ped's / car's path when it starts), and the layout keeps it.");
        ImGui::SameLine();
        ImGui::BeginDisabled(chain.pathStart.empty());
        if (ImGui::Button("No path"))
            Ask(requests, Chains::kOpUnassign);
        ImGui::EndDisabled();
        if (chain.waiting)
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", chain.waiting);
        return;
    }
    const char* noun = chain.path ? "path" : chain.ladder ? "ladder" : "rail";
    ImGui::SeparatorText(chain.path ? "Path" : chain.ladder ? "Ladder" : "Rail");
    ImGui::Text("%d point%s%s", chain.points, chain.points == 1 ? "" : "s", chain.added ? " (this one added)" : "");
    if (!chain.prev.empty())
        ImGui::TextDisabled("after %s", chain.prev.c_str());
    if (!chain.next.empty())
        ImGui::TextDisabled("before %s", chain.next.c_str());
    if (!chain.why.empty()) {
        ImGui::TextWrapped("%s", chain.why.c_str());
        return;
    }
    OpButton("Insert", chain.canInsert, "A point halfway to the next one (Insert)",
             "It's the last point - Extend draws on from it", requests, Chains::kOpInsert);
    ImGui::SameLine();
    OpButton("Delete", chain.canDelete, "This point deleted, the points either side linked (Del)", "", requests,
             Chains::kOpDelete);
    ImGui::SameLine();
    OpButton("Extend", chain.canExtend, "Draw on from this end: click points, Enter finishes (End)",
             "Only from an end (the first or last point)", requests, Chains::kOpExtend);
    OpButton("Link to...", chain.canLink, "Then click another chain's end: linked to it (J)",
             "Only an end can be linked on", requests, Chains::kOpLinkArm);
    ImGui::SameLine();
    OpButton("Unlink", chain.canUnlink, "Its link to the next point removed: the chain splits here (U)",
             "It has no next point", requests, Chains::kOpUnlink);
    if (chain.rail && !chain.terrains.empty()) {
        std::string current = chain.terrain >= 0 && chain.terrain < static_cast<int>(chain.terrains.size())
                                  ? TerrainLabel(chain.terrains[chain.terrain])
                                  : std::string("(other)");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##terrain", ("Terrain: " + current).c_str())) {
            for (int i = 0; i < static_cast<int>(chain.terrains.size()); ++i) {
                if (ImGui::Selectable(TerrainLabel(chain.terrains[i]).c_str(), i == chain.terrain))
                    Ask(requests, Chains::kOpTerrain, Crc::GenerateCRCFromString(chain.terrains[i].c_str()));
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The sound and sparks of grinding it - for the whole %s, at once", noun);
    }
    if (chain.waiting)
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", chain.waiting);
}

} // namespace Front::LevelEditor::Ui

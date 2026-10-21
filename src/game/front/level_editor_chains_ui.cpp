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
            ImGui::TextDisabled("No path");
        else
            ImGui::Text("Starts at %s", chain.pathStart.c_str());
        if (ImGui::Button("Assign Path..."))
            Ask(requests, Chains::kOpAssignArm);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click the first waypoint in the view");
        ImGui::SameLine();
        ImGui::BeginDisabled(chain.pathStart.empty());
        if (ImGui::Button("No path"))
            Ask(requests, Chains::kOpUnassign);
        ImGui::EndDisabled();
        if (chain.waiting)
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", chain.waiting);
        return;
    }
    const char* noun = chain.path ? "path" : chain.ladder ? "ladder" : chain.ledge ? "ledge" : "rail";
    ImGui::SeparatorText(chain.path ? "Path" : chain.ladder ? "Ladder" : chain.ledge ? "Ledge" : "Rail");
    ImGui::Text("%d point%s%s", chain.points, chain.points == 1 ? "" : "s", chain.added ? " (added)" : "");
    if (!chain.prev.empty())
        ImGui::TextDisabled("after %s", chain.prev.c_str());
    if (!chain.next.empty())
        ImGui::TextDisabled("before %s", chain.next.c_str());
    if (!chain.why.empty()) {
        ImGui::TextWrapped("%s", chain.why.c_str());
        return;
    }
    if (chain.ladder) {
        // (phase 4b: turned and deleted whole; its foot / top moved as points - L off)
        ImGui::Text("Facing %.0f\xC2\xB0%s", static_cast<double>(chain.facingDegrees),
                    chain.turned ? " (turned)" : "");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Direction the skater faces when climbing");
        if (ImGui::Button("Turn Left"))
            Ask(requests, Chains::kOpTurn, static_cast<uint32_t>(-1));
        ImGui::SameLine();
        if (ImGui::Button("Turn Right"))
            Ask(requests, Chains::kOpTurn, 1u);
        ImGui::SameLine();
        OpButton("Revert Facing", chain.turned, "Restore original facing", "Already original", requests, Chains::kOpFacingHome);
        OpButton("Delete Ladder", chain.canDelete, "Delete the ladder  (Del)", "",
                 requests, Chains::kOpDelete);
        ImGui::TextDisabled("Turn off Whole Ladder to move the foot or top");
        if (chain.waiting)
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", chain.waiting);
        return;
    }
    OpButton("Insert", chain.canInsert, "Insert a point after this one  (Insert)", "Last point: use Extend", requests, Chains::kOpInsert);
    ImGui::SameLine();
    OpButton("Delete", chain.canDelete, "Delete this point  (Del)", "", requests,
             Chains::kOpDelete);
    ImGui::SameLine();
    OpButton("Extend", chain.canExtend, "Draw more points from this end  (End)", "Select an end point", requests, Chains::kOpExtend);
    OpButton("Link To...", chain.canLink, "Link to another chain's end  (J)", "Select an end point", requests, Chains::kOpLinkArm);
    ImGui::SameLine();
    OpButton("Unlink", chain.canUnlink, "Split the chain after this point  (U)", "No next point", requests, Chains::kOpUnlink);
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
            ImGui::SetTooltip("Grind sound and sparks for the whole %s", noun);
    }
    if (chain.rail) {
        // (phase 4b: trick objects, grinding on flat ground - level_editor_chains.h)
        bool trick = chain.trickObject;
        ImGui::BeginDisabled(!chain.canTrickObject);
        if (ImGui::Checkbox("Trick Object", &trick))
            Ask(requests, Chains::kOpTrickObject, trick ? 1u : 0u);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", chain.canTrickObject
                                        ? "Counts as a trick object for scoring and graffiti"
                                  : "Original rails keep their setting");
        if (chain.trickObject && !chain.cluster.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("cluster %s", chain.cluster.c_str());
        }
        bool flat = chain.grindFlat;
        if (ImGui::Checkbox("Grind on Flat Ground", &flat))
            Ask(requests, Chains::kOpGrindFlat, flat ? 1u : 0u);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Allow grinding where ground is level with the rail (this game only)");
    }
    if (chain.ledge) {
        // (the feature map: a ledge's side to hang from - THUG2's HangLeft / HangRight flags, the whole ledge)
        static const char* const kSides[3] = {"Auto", "Left", "Right"};
        int side = chain.hang >= 0 && chain.hang <= 2 ? chain.hang : 0;
        ImGui::BeginDisabled(!chain.added);
        if (ImGui::Combo("Hang from", &side, kSides, 3) && side != chain.hang)
            Ask(requests, Chains::kOpHang, static_cast<uint32_t>(side));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", chain.added ? "Side the skater hangs from" : "Original ledges keep their setting");
        ImGui::TextDisabled("Hang and shimmy ledge");
    }
    if (chain.rail) {
        // (the feature map: a speed-up - THAW's RailAccelerate - on this point's segment, or the whole rail)
        if (chain.levelSpeed)
            ImGui::TextDisabled("Original speed-up: +%.1f%% per frame",
                                static_cast<double>(chain.levelRate * 100.0f));
        bool on = chain.speed > 0.0f;
        if (ImGui::Checkbox("Speed-up", &on))
            Ask(requests, Chains::kOpSpeed, on ? static_cast<uint32_t>(Chains::kSpeedDefault * 10000.0f + 0.5f) : 0u);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Speed up the skater while grinding uphill");
        if (on) {
            static float s_percent = -1.0f;
            float percent = s_percent >= 0.0f ? s_percent : chain.speed * 100.0f;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("##speed", &percent, 0.05f, Chains::kSpeedMin * 100.0f, Chains::kSpeedMax * 100.0f,
                                 "+%.1f%% / frame", ImGuiSliderFlags_AlwaysClamp))
                s_percent = percent;
            if (ImGui::IsItemDeactivatedAfterEdit() && s_percent >= 0.0f) {
                Ask(requests, Chains::kOpSpeed, static_cast<uint32_t>(s_percent * 100.0f + 0.5f));
                s_percent = -1.0f;
            } else if (!ImGui::IsItemActive()) {
                s_percent = -1.0f;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Speed gain per frame (typical: 2-10%%)");
            ImGui::SameLine();
            if (ImGui::SmallButton("Whole rail"))
                Ask(requests, Chains::kOpSpeedRail, static_cast<uint32_t>(chain.speed * 10000.0f + 0.5f));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Apply to the whole rail");
        } else if (chain.points > 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear Whole Rail"))
                Ask(requests, Chains::kOpSpeedRail, 0u);
        }
    }
    if (chain.waiting)
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", chain.waiting);
}

} // namespace Front::LevelEditor::Ui

// NOT ORIGINAL - the level editor's Inspector section for chains (ours; level_editor_chains.h, level_editor_ui.h): a
// rail's / path's point's operations, a rail's terrain, a ped's / car's path. Only ImGui's own API here.

#include <string>
#include <vector>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_ui.h"
#include "imgui.h"

namespace frontend::level_editor::ui {

namespace {

void get_ask(std::vector<Request>* requests, int op, uint32_t arg = 0)
{
    Request request = {Request::Kind::kChainOp};
    request.value = op;
    request.id = arg;
    requests->push_back(request);
}

// A button that can be off (its tooltip says what it does, or why not).
void op_button(const char* label, bool can, const char* what, const char* why_not, std::vector<Request>* requests,
               int op)
{
    ImGui::BeginDisabled(!can);
    if (ImGui::Button(label))
        get_ask(requests, op);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", can ? what : why_not);
}

// "TERRAIN_GRINDMETAL" -> "Metal" (the list's labels).
std::string terrain_label(const std::string& name)
{
    const std::string prefix = "TERRAIN_GRIND";
    std::string label = name.starts_with(prefix) ? name.substr(prefix.size()) : name;
    for (size_t i = 1; i < label.size(); ++i)
        label[i] = static_cast<char>(label[i] >= 'A' && label[i] <= 'Z' ? label[i] - 'A' + 'a' : label[i]);
    return label;
}

} // namespace

void chain_section(const chains::View& chain, std::vector<Request>* requests)
{
    if (!chain.show)
        return;
    if (chain.object) {
        ImGui::SeparatorText("Path");
        if (chain.path_start_.empty())
            ImGui::TextDisabled("No path");
        else
            ImGui::Text("Starts at %s", chain.path_start_.c_str());
        if (ImGui::Button("Assign Path..."))
            get_ask(requests, chains::kOpAssignArm);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click the first waypoint in the view");
        ImGui::SameLine();
        ImGui::BeginDisabled(chain.path_start_.empty());
        if (ImGui::Button("No path"))
            get_ask(requests, chains::kOpUnassign);
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
        ImGui::Text("Facing %.0f\xC2\xB0%s", static_cast<double>(chain.facing_degrees_),
                    chain.turned ? " (turned)" : "");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Direction the skater faces when climbing");
        if (ImGui::Button("Turn Left"))
            get_ask(requests, chains::kOpTurn, static_cast<uint32_t>(-1));
        ImGui::SameLine();
        if (ImGui::Button("Turn Right"))
            get_ask(requests, chains::kOpTurn, 1u);
        ImGui::SameLine();
        op_button("Revert Facing", chain.turned, "Restore original facing", "Already original", requests,
                  chains::kOpFacingHome);
        op_button("Delete Ladder", chain.can_delete_, "Delete the ladder  (Del)", "", requests, chains::kOpDelete);
        ImGui::TextDisabled("Turn off Whole Ladder to move the foot or top");
        if (chain.waiting)
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", chain.waiting);
        return;
    }
    op_button("Insert", chain.can_insert_, "Insert a point after this one  (Insert)", "Last point: use Extend",
              requests, chains::kOpInsert);
    ImGui::SameLine();
    op_button("Delete", chain.can_delete_, "Delete this point  (Del)", "", requests, chains::kOpDelete);
    ImGui::SameLine();
    op_button("Extend", chain.can_extend_, "Draw more points from this end  (End)", "Select an end point", requests,
              chains::kOpExtend);
    op_button("Link To...", chain.can_link_, "Link to another chain's end  (J)", "Select an end point", requests,
              chains::kOpLinkArm);
    ImGui::SameLine();
    op_button("Unlink", chain.can_unlink_, "Split the chain after this point  (U)", "No next point", requests,
              chains::kOpUnlink);
    if (chain.rail && !chain.terrains.empty()) {
        const std::string current = chain.terrain >= 0 && std::cmp_less(chain.terrain, chain.terrains.size())
                                        ? terrain_label(chain.terrains[chain.terrain])
                                        : std::string("(other)");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##terrain", ("Terrain: " + current).c_str())) {
            for (int i = 0; std::cmp_less(i, chain.terrains.size()); ++i) {
                if (ImGui::Selectable(terrain_label(chain.terrains[i]).c_str(), i == chain.terrain))
                    get_ask(requests, chains::kOpTerrain, checksums::hash_from_text(chain.terrains[i].c_str()));
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Grind sound and sparks for the whole %s", noun);
    }
    if (chain.rail) {
        // (phase 4b: trick objects, grinding on flat ground - level_editor_chains.h)
        bool trick = chain.trick_object_;
        ImGui::BeginDisabled(!chain.can_trick_object_);
        if (ImGui::Checkbox("Trick Object", &trick))
            get_ask(requests, chains::kOpTrickObject, trick ? 1u : 0u);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", chain.can_trick_object_ ? "Counts as a trick object for scoring and graffiti"
                                                            : "Original rails keep their setting");
        if (chain.trick_object_ && !chain.cluster.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("cluster %s", chain.cluster.c_str());
        }
        bool flat = chain.grind_flat_;
        if (ImGui::Checkbox("Grind on Flat Ground", &flat))
            get_ask(requests, chains::kOpGrindFlat, flat ? 1u : 0u);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Allow grinding where ground is level with the rail (this game only)");
    }
    if (chain.ledge) {
        // (the feature map: a ledge's side to hang from - THUG2's HangLeft / HangRight flags, the whole ledge)
        static const std::array<const char*, 3> kSides = {{"Auto", "Left", "Right"}};
        int side = chain.hang >= 0 && chain.hang <= 2 ? chain.hang : 0;
        ImGui::BeginDisabled(!chain.added);
        if (ImGui::Combo("Hang from", &side, kSides.data(), 3) && side != chain.hang)
            get_ask(requests, chains::kOpHang, static_cast<uint32_t>(side));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", chain.added ? "Side the skater hangs from" : "Original ledges keep their setting");
        ImGui::TextDisabled("Hang and shimmy ledge");
    }
    if (chain.rail) {
        // (the feature map: a speed-up - THAW's RailAccelerate - on this point's segment, or the whole rail)
        if (chain.level_speed_)
            ImGui::TextDisabled("Original speed-up: +%.1f%% per frame",
                                static_cast<double>(chain.level_rate_ * 100.0f));
        bool on = chain.speed > 0.0f;
        if (ImGui::Checkbox("Speed-up", &on))
            get_ask(requests, chains::kOpSpeed,
                    // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                    on ? static_cast<uint32_t>(chains::kSpeedDefault * 10000.0f + 0.5f) : 0u);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Speed up the skater while grinding uphill");
        if (on) {
            static float s_percent = -1.0f;
            float percent = s_percent >= 0.0f ? s_percent : chain.speed * 100.0f;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("##speed", &percent, 0.05f, chains::kSpeedMin * 100.0f, chains::kSpeedMax * 100.0f,
                                 "+%.1f%% / frame", ImGuiSliderFlags_AlwaysClamp))
                s_percent = percent;
            if (ImGui::IsItemDeactivatedAfterEdit() && s_percent >= 0.0f) {
                // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                get_ask(requests, chains::kOpSpeed, static_cast<uint32_t>(s_percent * 100.0f + 0.5f));
                s_percent = -1.0f;
            } else if (!ImGui::IsItemActive()) {
                s_percent = -1.0f;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Speed gain per frame (typical: 2-10%%)");
            ImGui::SameLine();
            if (ImGui::SmallButton("Whole rail"))
                // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
                get_ask(requests, chains::kOpSpeedRail, static_cast<uint32_t>(chain.speed * 10000.0f + 0.5f));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Apply to the whole rail");
        } else if (chain.points > 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear Whole Rail"))
                get_ask(requests, chains::kOpSpeedRail, 0u);
        }
    }
    if (chain.waiting)
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.45f, 1.0f), "%s", chain.waiting);
}

} // namespace frontend::level_editor::ui

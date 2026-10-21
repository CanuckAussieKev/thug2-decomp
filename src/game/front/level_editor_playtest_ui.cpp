// NOT ORIGINAL - play testing's UI (ours; level_editor_playtest.h): the Trail window, the toolbar's Trail and Simulate,
// a restart's Play from here. Only ImGui's API here.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_playtest.h"
#include "imgui.h"

namespace Front::LevelEditor::PlayTest {

namespace {

ImVec4 Colour(uint32_t argb)
{
    return ImVec4(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f,
                  ((argb >> 24) & 0xFF) / 255.0f);
}

void Tip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}

void Heading(const char* title, ImFont* heading)
{
    ImGui::Spacing();
    if (heading)
        ImGui::PushFont(heading);
    ImGui::TextUnformatted(title);
    if (heading)
        ImGui::PopFont();
    ImGui::Separator();
}

} // namespace

void ToolbarButtons()
{
    bool open = WindowOpen();
    if (open)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::SmallButton("Trail (F7)"))
        WindowOpen() = !open;
    if (open)
        ImGui::PopStyleColor();
    Tip("The skater's trails from your test runs, the gaps landed, Simulate");
    ImGui::SameLine();
    bool simulating = Simulating();
    if (simulating)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.15f, 1.0f));
    if (ImGui::SmallButton(simulating ? "Stop (F6)###simulate" : "Simulate (F6)###simulate"))
        Request(Ask::ToggleSimulate);
    if (simulating)
        ImGui::PopStyleColor();
    Tip(simulating ? "Stop the simulation: the game paused again, objects put back (unless Keep is on)"
                   : "Run the world - peds, cars, moving objects, particles - while you watch with the free camera "
                     "(the skater stays frozen). Clicking in the view or an editing key stops it.");
}

void RestartSection(int nodeIndex)
{
    const std::vector<Nodes::Node>& all = Nodes::All();
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(all.size()))
        return;
    const Nodes::Node& node = all[nodeIndex];
    if (node.family != Nodes::Family::Spawn || node.hidden)
        return;
    uint32_t name = node.name;
    if (ImGui::Button("Play from here"))
        PlayFromRestart(name);
    Tip("Close the editor into play with the skater at this restart (as F8's restart tester puts it: ResetSkaters). "
        "F5 or Ctrl+P brings you back with the same camera and selection; the run is recorded as a trail.");
    ImGui::SameLine();
}

void Window(float scale, ImFont* heading, ImFont* small)
{
    if (!WindowOpen())
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.30f, io.DisplaySize.y * 0.18f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Trail (F7)###editortrail", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open)
            WindowOpen() = false;
        return;
    }
    const LevelTrails* trails = CurrentTrails();
    Heading("Test runs", heading);
    ImGui::Checkbox("Record runs in play", &Recording());
    Tip("While the level is its edited copy and the editor is closed, each run's path is recorded (until the editor "
        "opens again)");
    ImGui::SameLine();
    ImGui::Checkbox("Show", &ShowTrails());
    // The legend.
    for (int m = 0; m < kMotions; ++m) {
        if (m % 5)
            ImGui::SameLine();
        ImGui::TextColored(Colour(MotionColour(static_cast<Motion>(m))), "%s", MotionName(static_cast<Motion>(m)));
    }
    ImGui::TextColored(Colour(0xFF40FF60), "o gap landed");
    ImGui::SameLine();
    ImGui::TextColored(Colour(0xFFFF4040), "o gap lost");
    ImGui::SameLine();
    ImGui::TextColored(Colour(0xFFFFE040), "o pending");
    ImGui::SameLine();
    ImGui::TextColored(Colour(0xFFFF3030), "x bail");
    if (!trails || trails->runs.empty()) {
        ImGui::TextDisabled("No runs yet: F5 (play from here) and skate - the trail shows here when you come back.");
    } else if (ImGui::BeginTable("##runs", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("Run");
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Gaps");
        ImGui::TableSetupColumn("Bails");
        ImGui::TableSetupColumn("Best combo", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        int remove = 0;
        for (auto it = trails->runs.rbegin(); it != trails->runs.rend(); ++it) {
            const Run& run = *it;
            ImGui::TableNextRow();
            ImGui::PushID(run.number);
            ImGui::TableNextColumn();
            bool visible = run.visible;
            if (ImGui::Checkbox("##v", &visible))
                SetRunVisible(run.number, visible);
            ImGui::TableNextColumn();
            ImGui::Text("%d%s", run.number, run.full ? "+" : "");
            if (ImGui::IsItemHovered()) {
                std::string text;
                char part[64];
                for (int m = 0; m < kMotions; ++m) {
                    if (run.motionSeconds[m] < 0.05f)
                        continue;
                    std::snprintf(part, sizeof(part), "%s %.1f s\n", MotionName(static_cast<Motion>(m)), run.motionSeconds[m]);
                    text += part;
                }
                ImGui::SetTooltip("%s%u points%s", text.c_str(), static_cast<unsigned>(run.samples.size()),
                                  run.full ? " (full: recording stopped)" : "");
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.1f s", run.seconds);
            ImGui::TableNextColumn();
            ImGui::Text("%d", run.GapsLanded());
            ImGui::TableNextColumn();
            ImGui::Text("%d", run.Bails());
            ImGui::TableNextColumn();
            ImGui::Text("%d", run.BestCombo());
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                remove = run.number;
            Tip("Delete this run");
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (remove)
            DeleteRun(remove);
    }
    if (ImGui::Button("Clear"))
        Request(Ask::Clear);
    Tip("Every run of this level and the gaps' landed marks forgotten (the file, if saved, stays until Save)");
    ImGui::SameLine();
    if (ImGui::Button("Save"))
        Request(Ask::Save);
    Tip("Written to thug2-decomp-layouts\\<scene>.trails.txt - read again when the editor next opens in this level");
    ImGui::SameLine();
    if (ImGui::Button("Load"))
        Request(Ask::Load);
    Tip("The level's trails file read again (replaces the runs in memory)");

    // The gaps validated by skating.
    std::vector<GapRead> gaps = LevelGaps();
    int landed = 0;
    for (const GapRead& g : gaps)
        landed += GapLanded(g.name) ? 1 : 0;
    char title[96];
    std::snprintf(title, sizeof(title), "Gaps landed in test runs: %d / %u", landed, static_cast<unsigned>(gaps.size()));
    Heading(title, heading);
    static bool untestedOnly;
    ImGui::Checkbox("Untested only", &untestedOnly);
    if (gaps.empty()) {
        ImGui::TextDisabled("This level has no gaps in its gap list.");
    } else if (ImGui::BeginChild("##gaps", ImVec2(0.0f, 150.0f * scale), ImGuiChildFlags_Borders)) {
        if (small)
            ImGui::PushFont(small);
        for (const GapRead& g : gaps) {
            bool ok = GapLanded(g.name);
            if (untestedOnly && ok)
                continue;
            ImGui::TextColored(ok ? Colour(0xFF40FF60) : Colour(0xFFFFB040), "%s", ok ? "landed  " : "untested");
            ImGui::SameLine();
            ImGui::Text("%s  (%d)", g.name.c_str(), g.score);
        }
        if (small)
            ImGui::PopFont();
    }
    if (!gaps.empty())
        ImGui::EndChild();

    Heading("Simulate (F6)", heading);
    bool simulating = Simulating();
    if (ImGui::Button(simulating ? "Stop" : "Simulate"))
        Request(Ask::ToggleSimulate);
    ImGui::SameLine();
    bool keep = KeepSimulation();
    if (ImGui::Checkbox("Keep where they got to", &keep))
        Request(Ask::ToggleKeepSimulation);
    Tip("Off (the default): stopping puts every object back where it was when the simulation started (place, "
        "turn, speed - their paths and scripts carry on from where they got to). On: they stay where they are.");
    ImGui::TextDisabled("%s", Status().c_str());
    ImGui::End();
    if (!open)
        WindowOpen() = false;
}

} // namespace Front::LevelEditor::PlayTest

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

void RestartSection(int nodeIndex)
{
    const std::vector<Nodes::Node>& all = Nodes::All();
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(all.size()))
        return;
    const Nodes::Node& node = all[nodeIndex];
    if (node.family != Nodes::Family::Spawn || node.hidden)
        return;
    uint32_t name = node.name;
    if (ImGui::Button("Play from Here"))
        PlayFromRestart(name);
    Tip("Play from this restart  (F5 returns)");
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
    if (!ImGui::Begin("Trails###editortrail", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open)
            WindowOpen() = false;
        return;
    }
    const LevelTrails* trails = CurrentTrails();
    Heading("Test runs", heading);
    ImGui::Checkbox("Record Runs", &Recording());
    Tip("Record a trail of each test run");
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
        ImGui::TextDisabled("No runs recorded");
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
                                  run.full ? " (full)" : "");
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
    Tip("Clear all runs and landed gaps");
    ImGui::SameLine();
    if (ImGui::Button("Save"))
        Request(Ask::Save);
    Tip("Save trails to disk");
    ImGui::SameLine();
    if (ImGui::Button("Load"))
        Request(Ask::Load);
    Tip("Reload trails from disk");

    // The gaps validated by skating.
    std::vector<GapRead> gaps = LevelGaps();
    int landed = 0;
    for (const GapRead& g : gaps)
        landed += GapLanded(g.name) ? 1 : 0;
    char title[96];
    std::snprintf(title, sizeof(title), "Gaps Landed: %d / %u", landed, static_cast<unsigned>(gaps.size()));
    Heading(title, heading);
    static bool untestedOnly;
    ImGui::Checkbox("Untested Only", &untestedOnly);
    if (gaps.empty()) {
        ImGui::TextDisabled("No gaps");
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

    Heading("Simulate", heading);
    bool simulating = Simulating();
    if (ImGui::Button(simulating ? "Stop" : "Simulate"))
        Request(Ask::ToggleSimulate);
    ImGui::SameLine();
    bool keep = KeepSimulation();
    if (ImGui::Checkbox("Keep Positions", &keep))
        Request(Ask::ToggleKeepSimulation);
    Tip("Keep objects where they are when stopping");
    ImGui::TextDisabled("%s", Status().c_str());
    ImGui::End();
    if (!open)
        WindowOpen() = false;
}

} // namespace Front::LevelEditor::PlayTest

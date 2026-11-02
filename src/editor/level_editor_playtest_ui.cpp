// NOT ORIGINAL - play testing's UI (ours; level_editor_playtest.h): the Trail window, the toolbar's Trail and Simulate,
// a restart's Play from here. Only ImGui's API here.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor_nodes.h"
#include "editor/level_editor_playtest.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::play_test {

namespace {

ImVec4 get_colour(uint32_t argb)
{
    return {static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(argb & 0xFF) / 255.0f, static_cast<float>((argb >> 24) & 0xFF) / 255.0f};
}

void get_tip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        gui::tooltip("%s", text);
}

void current_heading(const char* title, ImFont* heading)
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

void restart_section(int node_index)
{
    const std::vector<nodes::Elem>& all = nodes::get_all();
    if (node_index < 0 || std::cmp_greater_equal(node_index, all.size()))
        return;
    const nodes::Elem& node = core::at(all, node_index);
    if (node.family != nodes::Family::kSpawn || node.hidden)
        return;
    const uint32_t name = node.name;
    if (ImGui::Button("Play from Here"))
        play_from_restart(name);
    get_tip("Play from this restart  (F5 returns)");
    ImGui::SameLine();
}

void pane(float scale, ImFont* heading, ImFont* small)
{
    if (!window_open())
        return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.30f, io.DisplaySize.y * 0.18f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Trails###editortrail", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open)
            window_open() = false;
        return;
    }
    const LevelTrails* trails = current_trails();
    current_heading("Test runs", heading);
    ImGui::Checkbox("Record Runs", &recording());
    get_tip("Record a trail of each test run");
    ImGui::SameLine();
    ImGui::Checkbox("Show", &show_trails());
    // The legend.
    for (int m = 0; m < kMotions; ++m) {
        if (m % 5)
            ImGui::SameLine();
        gui::text_colored(get_colour(motion_colour(static_cast<Motion>(m))), "%s",
                           motion_name(static_cast<Motion>(m)));
    }
    gui::text_colored(get_colour(0xFF40FF60), "o gap landed");
    ImGui::SameLine();
    gui::text_colored(get_colour(0xFFFF4040), "o gap lost");
    ImGui::SameLine();
    gui::text_colored(get_colour(0xFFFFE040), "o pending");
    ImGui::SameLine();
    gui::text_colored(get_colour(0xFFFF3030), "x bail");
    if (!trails || trails->runs.empty()) {
        gui::text_disabled("No runs recorded");
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
                set_run_visible(run.number, visible);
            ImGui::TableNextColumn();
            gui::text("%d%s", run.number, run.full ? "+" : "");
            if (ImGui::IsItemHovered()) {
                std::string text;
                std::array<char, 64> part{};
                for (int m = 0; m < kMotions; ++m) {
                    if (core::at(run.motion_seconds_, m) < 0.05f)
                        continue;
                    text::format_to(part, "%s %.1f s\n", motion_name(static_cast<Motion>(m)),
                                    core::at(run.motion_seconds_, m));
                    text += part.data();
                }
                gui::tooltip("%s%u points%s", text.c_str(), static_cast<unsigned>(run.samples.size()),
                                  run.full ? " (full)" : "");
            }
            ImGui::TableNextColumn();
            gui::text("%.1f s", run.seconds);
            ImGui::TableNextColumn();
            gui::text("%d", run.gaps_landed());
            ImGui::TableNextColumn();
            gui::text("%d", run.bails());
            ImGui::TableNextColumn();
            gui::text("%d", run.top_combo_value());
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                remove = run.number;
            get_tip("Delete this run");
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (remove)
            delete_run(remove);
    }
    if (ImGui::Button("Clear"))
        get_request(Ask::kClear);
    get_tip("Clear all runs and landed gaps");
    ImGui::SameLine();
    if (ImGui::Button("Save"))
        get_request(Ask::kStoreAway);
    get_tip("Save trails to disk");
    ImGui::SameLine();
    if (ImGui::Button("Load"))
        get_request(Ask::kReadIn);
    get_tip("Reload trails from disk");

    // The gaps validated by skating.
    const std::vector<GapRead> gaps = get_level_gaps();
    int landed = 0;
    for (const GapRead& g : gaps)
        landed += gap_landed(g.name) ? 1 : 0;
    std::array<char, 96> title{};
    text::format_to(title, "Gaps Landed: %d / %u", landed, static_cast<unsigned>(gaps.size()));
    current_heading(title.data(), heading);
    static bool g_untested_only;
    ImGui::Checkbox("Untested Only", &g_untested_only);
    if (gaps.empty()) {
        gui::text_disabled("No gaps");
    } else if (ImGui::BeginChild("##gaps", ImVec2(0.0f, 150.0f * scale), ImGuiChildFlags_Borders)) {
        if (small)
            ImGui::PushFont(small);
        for (const GapRead& g : gaps) {
            const bool ok = gap_landed(g.name);
            if (g_untested_only && ok)
                continue;
            gui::text_colored(ok ? get_colour(0xFF40FF60) : get_colour(0xFFFFB040), "%s",
                               ok ? "landed  " : "untested");
            ImGui::SameLine();
            gui::text("%s  (%d)", g.name.c_str(), g.score);
        }
        if (small)
            ImGui::PopFont();
    }
    if (!gaps.empty())
        ImGui::EndChild();

    current_heading("Simulate", heading);
    const bool simulating = get_simulating();
    if (ImGui::Button(simulating ? "Stop" : "Simulate"))
        get_request(Ask::kToggleSimulate);
    ImGui::SameLine();
    bool keep = keep_simulation();
    if (ImGui::Checkbox("Keep Positions", &keep))
        get_request(Ask::kToggleKeepSimulation);
    get_tip("Keep objects where they are when stopping");
    gui::text_disabled("%s", get_standing().c_str());
    ImGui::End();
    if (!open)
        window_open() = false;
}

} // namespace frontend::level_editor::play_test

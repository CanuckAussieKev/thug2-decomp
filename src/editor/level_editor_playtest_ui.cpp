// NOT ORIGINAL - play testing's UI (ours; level_editor_playtest.h): the Trail window, the toolbar's Trail and Simulate,
// a restart's Play from here. Only ImGui's API here.

#include <cstddef>
#include <cstdint>
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
#include "editor/level_editor.h"

namespace frontend::level_editor::play_test {

namespace {

constexpr std::uint32_t kChannelByteMask = 0xFF;
constexpr float kWindowLeft = 0.30F; // the Playtest window starting place, as fractions of the display
constexpr float kWindowTop = 0.18F;
constexpr float kWindowWidth = 400.0F; // at scale 1
constexpr int kSeparatorEvery = 5;
constexpr std::uint32_t kGapLandedColour = 0xFF40FF60; // the overlay markers colours, repeated in the legend
constexpr std::uint32_t kGapLostColour = 0xFFFF4040;
constexpr std::uint32_t kGapPendingColour = 0xFFFFE040;
constexpr std::uint32_t kBailColour = 0xFFFF3030;
constexpr std::uint32_t kGapOtherColour = 0xFFFFB040;
constexpr int kRunColumns = 6;
constexpr std::size_t kPartSize = 64;
constexpr float kMinShownSeconds = 0.05F; // a motion that lasted less is left out of the run summary
constexpr std::size_t kTitleSize = 96;
constexpr float kGapListHeight = 150.0F; // at scale 1

constexpr float kChannelMax = 255.0F; // the largest 8-bit colour channel

ImVec4 get_colour(uint32_t argb)
{
    return {static_cast<float>((argb >> kRedShift) & kChannelByteMask) / kChannelMax, static_cast<float>((argb >> kGreenShift) & kChannelByteMask) / kChannelMax,
            static_cast<float>(argb & kChannelByteMask) / kChannelMax, static_cast<float>((argb >> kAlphaShift) & kChannelByteMask) / kChannelMax};
}

void get_tip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        gui::tooltip("%s", text);
    }
}

void current_heading(const char* title, ImFont* heading)
{
    ImGui::Spacing();
    if (heading) {
        ImGui::PushFont(heading);
    }
    ImGui::TextUnformatted(title);
    if (heading) {
        ImGui::PopFont();
    }
    ImGui::Separator();
}

} // namespace

void restart_section(int node_index)
{
    const std::vector<nodes::Elem>& all = nodes::get_all();
    if (node_index < 0 || std::cmp_greater_equal(node_index, all.size())) {
        return;
    }
    const nodes::Elem& node = core::at(all, node_index);
    if (node.family != nodes::Family::kSpawn || node.hidden) {
        return;
    }
    const uint32_t name = node.name;
    if (ImGui::Button("Play from Here")) {
        play_from_restart(name);
    }
    get_tip("Play from this restart  (F5 returns)");
    ImGui::SameLine();
}

namespace {

// The test runs' table, newest first: shown or not, the run (its time in each motion), time, gaps, bails, best
// combo, delete.
void runs_table(const LevelTrails* trails)
{
    ImGui::TableSetupColumn("");
    ImGui::TableSetupColumn("Run");
    ImGui::TableSetupColumn("Time");
    ImGui::TableSetupColumn("Gaps");
    ImGui::TableSetupColumn("Bails");
    ImGui::TableSetupColumn("Best combo", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    int remove = 0;
    for (auto it = trails->runs().rbegin(); it != trails->runs().rend(); ++it) {
        const Run& run = *it;
        ImGui::TableNextRow();
        ImGui::PushID(run.number);
        ImGui::TableNextColumn();
        bool visible = run.visible;
        if (ImGui::Checkbox("##v", &visible)) {
            set_run_visible(run.number, visible);
        }
        ImGui::TableNextColumn();
        gui::text("%d%s", run.number, run.full_ ? "+" : "");
        if (ImGui::IsItemHovered()) {
            std::string text;
            std::array<char, kPartSize> part{};
            for (int motion = 0; motion < kMotions; ++motion) {
                if (core::at(run.motion_seconds_, motion) < kMinShownSeconds) {
                    continue;
                }
                text::format_to(part, "%s %.1f s\n", motion_name(static_cast<Motion>(motion)),
                                core::at(run.motion_seconds_, motion));
                text += part.data();
            }
            gui::tooltip("%s%u points%s", text.c_str(), static_cast<unsigned>(run.samples_.size()),
                         run.full_ ? " (full)" : "");
        }
        ImGui::TableNextColumn();
        gui::text("%.1f s", run.seconds);
        ImGui::TableNextColumn();
        gui::text("%d", gaps_landed(run));
        ImGui::TableNextColumn();
        gui::text("%d", bails(run));
        ImGui::TableNextColumn();
        gui::text("%d", top_combo_value(run));
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            remove = run.number;
        }
        get_tip("Delete this run");
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (remove != 0) {
        delete_run(remove);
    }
}

// The level's gaps, landed or untested (only the untested ones when asked).
void gap_list(const std::vector<GapRead>& gaps, ImFont* small, bool untested_only)
{
    if (small) {
        ImGui::PushFont(small);
    }
    for (const GapRead& gap : gaps) {
        const bool ok = gap_landed(gap.name);
        if (untested_only && ok) {
            continue;
        }
        gui::text_colored(ok ? get_colour(kGapLandedColour) : get_colour(kGapOtherColour), "%s",
                          ok ? "landed  " : "untested");
        ImGui::SameLine();
        gui::text("%s  (%d)", gap.name.c_str(), gap.score);
    }
    if (small) {
        ImGui::PopFont();
    }
}

} // namespace

void pane(float scale, PaneFonts fonts)
{
    auto* const heading = fonts.heading;
    auto* const small = fonts.small_font_;
    if (!window_open()) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * kWindowLeft, io.DisplaySize.y * kWindowTop), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(kWindowWidth * scale, 0.0F), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Trails###editortrail", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open) {
            window_open() = false;
        }
        return;
    }
    const LevelTrails* trails = current_trails();
    current_heading("Test runs", heading);
    ImGui::Checkbox("Record Runs", &recording());
    get_tip("Record a trail of each test run");
    ImGui::SameLine();
    ImGui::Checkbox("Show", &show_trails());
    // The legend.
    for (int motion = 0; motion < kMotions; ++motion) {
        if ((motion % kSeparatorEvery) != 0) {
            ImGui::SameLine();
        }
        gui::text_colored(get_colour(motion_colour(static_cast<Motion>(motion))), "%s",
                          motion_name(static_cast<Motion>(motion)));
    }
    gui::text_colored(get_colour(kGapLandedColour), "o gap landed");
    ImGui::SameLine();
    gui::text_colored(get_colour(kGapLostColour), "o gap lost");
    ImGui::SameLine();
    gui::text_colored(get_colour(kGapPendingColour), "o pending");
    ImGui::SameLine();
    gui::text_colored(get_colour(kBailColour), "x bail");
    if (!trails || trails->runs().empty()) {
        gui::text_disabled("No runs recorded");
    } else if (ImGui::BeginTable("##runs", kRunColumns, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        runs_table(trails);
    }
    if (ImGui::Button("Clear")) {
        get_request(Ask::kClear);
    }
    get_tip("Clear all runs and landed gaps");
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        get_request(Ask::kStoreAway);
    }
    get_tip("Save trails to disk");
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        get_request(Ask::kReadIn);
    }
    get_tip("Reload trails from disk");

    // The gaps validated by skating.
    const std::vector<GapRead> gaps = get_level_gaps();
    int landed = 0;
    for (const GapRead& gap : gaps) {
        landed += gap_landed(gap.name) ? 1 : 0;
    }
    std::array<char, kTitleSize> title{};
    text::format_to(title, "Gaps Landed: %d / %u", landed, static_cast<unsigned>(gaps.size()));
    current_heading(title.data(), heading);
    static bool g_untested_only;
    ImGui::Checkbox("Untested Only", &g_untested_only);
    if (gaps.empty()) {
        gui::text_disabled("No gaps");
    } else if (ImGui::BeginChild("##gaps", ImVec2(0.0F, kGapListHeight * scale), ImGuiChildFlags_Borders)) {
        gap_list(gaps, small, g_untested_only);
    }
    if (!gaps.empty()) {
        ImGui::EndChild();
    }

    current_heading("Simulate", heading);
    const bool simulating = get_simulating();
    if (ImGui::Button(simulating ? "Stop" : "Simulate")) {
        get_request(Ask::kToggleSimulate);
    }
    ImGui::SameLine();
    bool keep = keep_simulation();
    if (ImGui::Checkbox("Keep Positions", &keep)) {
        get_request(Ask::kToggleKeepSimulation);
    }
    get_tip("Keep objects where they are when stopping");
    gui::text_disabled("%s", get_standing().c_str());
    ImGui::End();
    if (!open) {
        window_open() = false;
    }
}

} // namespace frontend::level_editor::play_test

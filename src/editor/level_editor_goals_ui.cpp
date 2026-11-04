// NOT ORIGINAL - the level editor's goals: the Inspector's sections (Dear ImGui). See level_editor_goals.h.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <array>
#include <vector>

#include "editor/level_editor_goals.h"
#include "editor/level_editor_hud.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ui_theme.h"

namespace frontend::level_editor::goal_edit {

namespace {

constexpr std::size_t kTextBufferSize = 128; // a text field
constexpr float kMultilineLines = 3.2F;      // a multi-line field is this many text lines high
constexpr float kFieldWidth = 150.0F;        // the narrow fields
constexpr float kWideFieldWidth = 170.0F;
constexpr int kNameFieldLimit = 16;           // characters the name field takes
constexpr int kPedFieldLimit = 16;
constexpr int kMessageFieldLimit = 99;        // the description and the win message
constexpr int kTrickFieldLimit = 49;
constexpr float kScoreDragSpeed = 250.0F;
constexpr float kCountDragSpeed = 0.1F; // the small whole-number fields
constexpr float kCheckpointDragSpeed = 0.2F;
constexpr float kSpinDragSpeed = 2.0F;
constexpr float kToWinDragSpeed = 0.5F;
constexpr float kPercentRounding = 0.01F; // added before truncating the percentage
constexpr float kPercentDragSpeed = 0.2F;
constexpr int kMaxAccelPercent = 100;
constexpr float kComboTimeDragSpeed = 5.0F;
constexpr float kStopDragSpeed = 0.5F;
constexpr float kGapListMaxHeight = 160.0F; // the gap list scrolls beyond this
constexpr float kGapRowHeight = 22.0F;
constexpr float kGapListPadding = 4.0F;
constexpr std::size_t kCheckpointPrefixLength = 11; // "Checkpoint "
constexpr ImVec4 kCompleteColour = ImVec4(0.55F, 0.85F, 0.55F, 1.0F);
constexpr ImVec4 kNotPlayedColour = ImVec4(0.95F, 0.65F, 0.35F, 1.0F);
constexpr ImVec4 kDeletedColour = ImVec4(0.95F, 0.45F, 0.40F, 1.0F);
constexpr ImVec4 kCompleteTitleColour = ImVec4(0.90F, 0.80F, 0.45F, 1.0F);
constexpr ImVec4 kIncompleteTitleColour = ImVec4(0.75F, 0.55F, 0.40F, 1.0F);

int& new_type()
{
    static int new_type{0};
    return new_type;
}

// A text field: typed, applied when let go (Enter or leaving it); its buffer follows `value` while it isn't edited.
struct TextBuffer {
    std::array<char, kTextBufferSize> text = {};
    bool editing = false;
};
std::map<std::string, TextBuffer>& buffers()
{
    static std::map<std::string, TextBuffer> value;
    return value;
}

bool text_field(const char* label, const std::string& key, std::string* value, size_t limit, bool multiline)
{
    TextBuffer& b = buffers()[key];
    if (!b.editing) {
        text::format_to(b.text, "%s", value->c_str());
    }
    const size_t size = std::min(sizeof(b.text), limit + 1);
    if (multiline) {
        ImGui::InputTextMultiline(label, b.text.data(), size,
                                  ImVec2(-1.0F, ImGui::GetTextLineHeight() * kMultilineLines));
    } else {
        ImGui::InputText(label, b.text.data(), size);
    }
    b.editing = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        *value = b.text.data();
        return true;
    }
    return false;
}

// A number dragged: `*done` false while it is (the drag is one undo step).
bool int_field(const char* label, int* value, float speed, int low, int high, const char* format, bool* done)
{
    ImGui::SetNextItemWidth(kFieldWidth * ImGui::GetFontSize() / ui_layout::kReferenceFontSize);
    const bool changed = ImGui::DragInt(label, value, speed, low, high, format, ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivated()) {
        *done = true;
    }
    return changed;
}

void get_apply(const Goal& g, bool done)
{
    const Goal& changed = g;
    get_queue([changed, done] { get_edit(changed.id, changed, done); });
}

void current_settings(const Goal& goal, ImFont* mono)
{
    Goal g = goal;
    bool done = false;
    bool changed = false;
    const std::string key = g.id + "/";
    if (text_field("Name##goal", key + "name", &g.name, kNameFieldLimit, false)) {
        changed = done = true; // (Create-a-Goal's keyboard: 16 characters)
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Name shown in View Goals");
    }
    if (text_field("Ped name##goal", key + "ped", &g.ped, kPedFieldLimit, false)) {
        changed = done = true;
    }
    ImGui::TextUnformatted("Goal text");
    if (text_field("##goaltext", key + "text", &g.text, kMessageFieldLimit, true)) {
        changed = done = true;
    }
    ImGui::TextUnformatted("Win message");
    if (text_field("##goalwin", key + "win", &g.win, kMessageFieldLimit, true)) {
        changed = done = true;
    }
    ImGui::PushFont(mono);
    if (int_field("Time limit##goal", &g.time, 1.0F, kMinTimeLimit, kMaxTimeLimit, "%d s", &done)) {
        changed = true;
    }
    if (g.type == KindTag::kHighScore || g.type == KindTag::kHighCombo) {
        if (int_field(g.type == KindTag::kHighScore ? "High score##goal" : "High combo##goal", &g.score,
                      kScoreDragSpeed, kMinHighScore, kMaxHighScore, "%d", &done)) {
            changed = true;
        }
    }
    if (g.type == KindTag::kRace) {
        if (int_field("Laps##goal", &g.laps, kCountDragSpeed, 1, kMaxLaps, "%d", &done)) {
            changed = true;
        }
        if (int_field("Per-checkpoint##goal", &g.checkpoint_time_, kCheckpointDragSpeed, 0, kMaxCheckpointTime,
                      (g.checkpoint_time_ != 0) ? "%d s" : "Off", &done)) {
            changed = true;
        }
    }
    ImGui::PopFont();
    // (goal_update_control: SKATE letters and gap goals keep theirs; the trick goals have none)
    if (!get_trick_type(g.type) && g.type != KindTag::kSkate && g.type != KindTag::kGap) {
        int control = g.walk ? 1 : 0;
        ImGui::SetNextItemWidth(kFieldWidth * ImGui::GetFontSize() / ui_layout::kReferenceFontSize);
        if (ImGui::Combo("Control##goal", &control, "Skate\0Walk\0")) {
            g.walk = control == 1;
            changed = done = true;
        }
    }
    if (g.type == KindTag::kGap) {
        ImGui::TextUnformatted("Gaps");
        std::vector<std::string> gaps = get_level_gaps();
        for (const std::string& name : g.gaps) {
            if (std::ranges::find(gaps, name) == gaps.end()) {
                gaps.push_back(name); // (one the list hasn't now: shown so it can be taken off)
            }
        }
        if (gaps.empty()) {
            gui::text_disabled("No gaps");
        }
        ImGui::BeginChild("##goalgaps",
                          ImVec2(0.0F, std::min(kGapListMaxHeight,
                                                (kGapRowHeight * static_cast<float>(gaps.size())) + kGapListPadding)),
                          ImGuiChildFlags_Borders);
        for (const std::string& name : gaps) {
            auto it = std::ranges::find(g.gaps, name);
            bool on = it != g.gaps.end();
            if (ImGui::Checkbox(name.c_str(), &on)) {
                if (on && g.gaps.size() < kMaxGaps) {
                    g.gaps.push_back(name);
                } else if (!on) {
                    g.gaps.erase(it);
                }
                changed = done = true;
            }
        }
        ImGui::EndChild();
        if (text_field("Required trick##goal", key + "trick", &g.trick, kTrickFieldLimit, false)) {
            changed = done = true;
        }
    }
    if (get_trick_type(g.type)) {
        ImGui::TextUnformatted("Key Combo Sets");
        for (int set = 0; set < combo_set_count(g.type); ++set) {
            bool on = (g.sets & (1U << set)) != 0;
            if (ImGui::Checkbox(combo_set_name(set), &on)) {
                g.sets = on ? g.sets | (1U << set) : g.sets & ~(1U << set);
                changed = done = true;
            }
        }
        ImGui::PushFont(mono);
        if (g.type != KindTag::kTrickBeat) {
            if (int_field("Spin##goal", &g.spin, kSpinDragSpeed, 0, kMaxSpin, (g.spin != 0) ? "%d" : "Off", &done)) {
                changed = true;
            }
        }
        if (g.type == KindTag::kComboSkatetris) {
            if (ImGui::Checkbox("Single combo##goal", &g.single_combo_)) {
                changed = done = true;
            }
            if (int_field("Combo size##goal", &g.combo_size_, kCountDragSpeed, 1, kMaxTrickCount, "%d", &done)) {
                changed = true;
            }
        }
        if (g.type == KindTag::kTrickTris) {
            if (int_field("Block size##goal", &g.block_size_, kCountDragSpeed, 1, kMaxTrickCount, "%d", &done)) {
                changed = true;
            }
            if (int_field("Total to win##goal", &g.to_win_, kToWinDragSpeed, 1, kMaxToWin, "%d", &done)) {
                changed = true;
            }
        }
        if (g.type == KindTag::kSkatetris || (g.type == KindTag::kComboSkatetris && !g.single_combo_)) {
            if (int_field("Accel. interval##goal", &g.accel_interval_, kCountDragSpeed, 1, kMaxAccelInterval, "%d",
                          &done)) {
                changed = true;
            }
            int percent = static_cast<int>((g.accel_percent_ * 100.0F) + kPercentRounding);
            if (int_field("Accel. percent##goal", &percent, kPercentDragSpeed, 1, kMaxAccelPercent, "%d%%", &done)) {
                g.accel_percent_ = static_cast<float>(percent) / 100.0F;
                changed = true;
            }
            if (int_field("Trick time##goal", &g.combo_time_, kComboTimeDragSpeed, kMinComboTime, kMaxComboTime, "%d",
                          &done)) {
                changed = true;
            }
            if (int_field("Stop adding at##goal", &g.stop_at_, kStopDragSpeed, kMinStopAt,
                          std::max(kMinStopAt, g.time - kStopAtMargin), "%d s", &done)) {
                changed = true;
            }
            if (int_field("Max tricks##goal", &g.max_tricks_, kCountDragSpeed, 1, kMaxTrickCount, "%d", &done)) {
                changed = true;
            }
        }
        ImGui::PopFont();
    }
    if (changed) {
        get_apply(g, done);
    } else if (done) {
        get_queue([] { end_drag(); });
    }
}

} // namespace

void inspector_section(uint32_t node, ImFont* mono, ImFont* small_font)
{
    std::string id;
    int point = -1;
    if ((node == 0U) || !point_of_node(node, &id, &point)) {
        return;
    }
    std::vector<Goal> all = get_all();
    auto it = std::ranges::find_if(all, [&](const Goal& g) { return g.id == id; });
    if (it == all.end()) {
        return;
    }
    const Goal& g = *it;
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Goal###goal", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    gui::text_wrapped("%s - %s (%s)", g.name.c_str(), type_title(g.type), point_label(g.type, point).c_str());
    std::string why;
    if (get_complete(g, &why)) {
        gui::text_colored(kCompleteColour, "Complete");
    } else {
        gui::text_colored(kNotPlayedColour, "Not played: %s", why.c_str());
    }
    const int missing = get_missing(g.id);
    if (missing > 0) {
        gui::text_colored(kDeletedColour, "%d point%s deleted", missing, missing == 1 ? "" : "s");
        ImGui::SameLine();
        const std::string goal_id = g.id;
        if (ImGui::SmallButton("Restore Points")) {
            get_queue([goal_id] { restore_points(goal_id); });
        }
    }
    // The point: its height (letters float above their node).
    if (point >= 2) {
        Goal changed = g;
        float height = core::at(changed.points, point).height;
        ImGui::PushFont(mono);
        ImGui::SetNextItemWidth(kFieldWidth * ImGui::GetFontSize() / ui_layout::kReferenceFontSize);
        const bool dragged =
            ImGui::DragFloat("Height##goalpoint", &height, 0.5F, 0.0F, 1000.0F, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        const bool done = ImGui::IsItemDeactivated();
        ImGui::PopFont();
        if (dragged) {
            core::at(changed.points, point).height = height;
            get_apply(changed, done);
        } else if (done) {
            get_queue([] { end_drag(); });
        }
    }
    // The goal's points: select one.
    ImGui::PushFont(small_font);
    ImGui::TextUnformatted("Points:");
    for (size_t k = 0; k < g.points.size(); ++k) {
        ImGui::SameLine();
        std::string label = point_label(g.type, static_cast<int>(k));
        if (label.starts_with("Checkpoint ")) {
            label = "CP" + label.substr(kCheckpointPrefixLength);
        }
        label += "##goalpt" + std::to_string(k);
        const std::string goal_id = g.id;
        const int index = static_cast<int>(k);
        if (ImGui::SmallButton(label.c_str())) {
            get_queue([goal_id, index] { choose_one(goal_id, index); });
        }
    }
    ImGui::PopFont();
    if (g.type == KindTag::kRace) {
        const std::string goal_id = g.id;
        if (static_cast<int>(g.points.size()) < max_points(g.type) && ImGui::Button("Add checkpoint##goal")) {
            get_queue([goal_id] { add_checkpoint(goal_id); });
        }
        if (static_cast<int>(g.points.size()) > min_points(g.type)) {
            ImGui::SameLine();
            if (ImGui::Button("Remove last##goal")) {
                get_queue([goal_id] { remove_checkpoint(goal_id); });
            }
        }
    }
    current_settings(g, mono);
    ImGui::Spacing();
    const std::string goal_id = g.id;
    if (ImGui::Button("Copy to Create-a-Goal##goal")) {
        get_queue([goal_id] {
            std::string message;
            export_goal(goal_id, &message);
            hud::get_message(message, hud::kLongMessageSeconds);
        });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Copy to the game's Create-a-Goal list");
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ui_theme::kDangerButtonColour);
    if (ImGui::Button("Delete goal##goal")) {
        get_queue([goal_id] { drop_out(goal_id); });
    }
    ImGui::PopStyleColor();
}

void list_section(ImFont* mono, ImFont* small_font)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Goals###goals")) {
        return;
    }
    ImGui::PushFont(small_font);
    gui::text_disabled("Goals for this level, saved with the layout");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(kWideFieldWidth * ImGui::GetFontSize() / ui_layout::kReferenceFontSize);
    if (ImGui::BeginCombo("##goaltype", type_title(static_cast<KindTag>(new_type())))) {
        for (int t = 0; t < kTypes; ++t) {
            if (ImGui::Selectable(type_title(static_cast<KindTag>(t)), t == new_type())) {
                new_type() = t;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const std::vector<Goal> all = get_all();
    if (static_cast<int>(all.size()) >= kMaxGoals) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("New goal")) {
        auto type = static_cast<KindTag>(new_type());
        get_queue([type] { fabricate(type); });
    }
    if (static_cast<int>(all.size()) >= kMaxGoals) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Add at view center");
    }
    if (all.empty()) {
        gui::text_disabled("No goals (max 10)");
    }
    ImGui::PushFont(mono);
    for (const Goal& g : all) {
        ImGui::PushID(g.id.c_str());
        const bool complete = get_complete(g);
        gui::text_colored(complete ? kCompleteTitleColour : kIncompleteTitleColour, "%s",
                           g.name.c_str());
        if (ImGui::IsItemHovered()) {
            std::string why;
            gui::tooltip("%s - %s", type_title(g.type), get_complete(g, &why) ? "complete" : why.c_str());
        }
        ImGui::SameLine();
        const std::string goal_id = g.id;
        if (ImGui::SmallButton("Select")) {
            get_queue([goal_id] { choose_one(goal_id, 0); });
        }
        ImGui::PopID();
    }
    ImGui::PopFont();
}

} // namespace frontend::level_editor::goal_edit

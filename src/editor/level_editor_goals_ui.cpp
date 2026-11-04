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
    TextBuffer& text_buffer = buffers()[key];
    if (!text_buffer.editing) {
        text::format_to(text_buffer.text, "%s", value->c_str());
    }
    const size_t size = std::min(sizeof(text_buffer.text), limit + 1);
    if (multiline) {
        ImGui::InputTextMultiline(label, text_buffer.text.data(), size,
                                  ImVec2(-1.0F, ImGui::GetTextLineHeight() * kMultilineLines));
    } else {
        ImGui::InputText(label, text_buffer.text.data(), size);
    }
    text_buffer.editing = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        *value = text_buffer.text.data();
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

void get_apply(const Goal& goal, bool done)
{
    const Goal& changed = goal;
    get_queue([changed, done] { get_edit(changed.id, changed, done); });
}

// The goal being edited, whether a field changed and whether its edit is done (one undo step), the fields' ids' key,
// the numbers' font.
struct GoalEdit {
    Goal edited;
    bool done{};
    bool changed{};
    std::string key;
    ImFont* mono{};
};

// Skatetris' (and a combo Skatetris' not single combo) acceleration, trick time, stop and max tricks.
void skatetris_fields(GoalEdit& edit)
{
    Goal& edited = edit.edited;
    bool& done = edit.done;
    bool& changed = edit.changed;
    if (int_field("Accel. interval##goal", &edited.accel_interval_, kCountDragSpeed, 1, kMaxAccelInterval, "%d",
                  &done)) {
        changed = true;
    }
    int percent = static_cast<int>((edited.accel_percent_ * 100.0F) + kPercentRounding);
    if (int_field("Accel. percent##goal", &percent, kPercentDragSpeed, 1, kMaxAccelPercent, "%d%%", &done)) {
        edited.accel_percent_ = static_cast<float>(percent) / 100.0F;
        changed = true;
    }
    if (int_field("Trick time##goal", &edited.combo_time_, kComboTimeDragSpeed, kMinComboTime, kMaxComboTime, "%d",
                  &done)) {
        changed = true;
    }
    if (int_field("Stop adding at##goal", &edited.stop_at_, kStopDragSpeed, kMinStopAt,
                  std::max(kMinStopAt, edited.time - kStopAtMargin), "%d s", &done)) {
        changed = true;
    }
    if (int_field("Max tricks##goal", &edited.max_tricks_, kCountDragSpeed, 1, kMaxTrickCount, "%d", &done)) {
        changed = true;
    }
}

// The goal's name, ped name, text and win message.
void goal_text_fields(GoalEdit& edit)
{
    Goal& edited = edit.edited;
    bool& done = edit.done;
    bool& changed = edit.changed;
    const std::string& key = edit.key;
    if (text_field("Name##goal", key + "name", &edited.name, kNameFieldLimit, false)) {
        changed = done = true; // (Create-a-Goal's keyboard: 16 characters)
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Name shown in View Goals");
    }
    if (text_field("Ped name##goal", key + "ped", &edited.ped, kPedFieldLimit, false)) {
        changed = done = true;
    }
    ImGui::TextUnformatted("Goal text");
    if (text_field("##goaltext", key + "text", &edited.text, kMessageFieldLimit, true)) {
        changed = done = true;
    }
    ImGui::TextUnformatted("Win message");
    if (text_field("##goalwin", key + "win", &edited.win, kMessageFieldLimit, true)) {
        changed = done = true;
    }
}

// Its time limit; a high score's or combo's score; a race's laps and checkpoint time.
void goal_number_fields(GoalEdit& edit)
{
    Goal& edited = edit.edited;
    bool& done = edit.done;
    bool& changed = edit.changed;
    ImFont* mono = edit.mono;
    ImGui::PushFont(mono);
    if (int_field("Time limit##goal", &edited.time, 1.0F, kMinTimeLimit, kMaxTimeLimit, "%d s", &done)) {
        changed = true;
    }
    if (edited.type == KindTag::kHighScore || edited.type == KindTag::kHighCombo) {
        if (int_field(edited.type == KindTag::kHighScore ? "High score##goal" : "High combo##goal", &edited.score,
                      kScoreDragSpeed, kMinHighScore, kMaxHighScore, "%d", &done)) {
            changed = true;
        }
    }
    if (edited.type == KindTag::kRace) {
        if (int_field("Laps##goal", &edited.laps, kCountDragSpeed, 1, kMaxLaps, "%d", &done)) {
            changed = true;
        }
        if (int_field("Per-checkpoint##goal", &edited.checkpoint_time_, kCheckpointDragSpeed, 0, kMaxCheckpointTime,
                      (edited.checkpoint_time_ != 0) ? "%d s" : "Off", &done)) {
            changed = true;
        }
    }
    ImGui::PopFont();
}

// (goal_update_control: SKATE letters and gap goals keep theirs; the trick goals have none)
void goal_control_field(GoalEdit& edit)
{
    Goal& edited = edit.edited;
    bool& done = edit.done;
    bool& changed = edit.changed;
    if (!get_trick_type(edited.type) && edited.type != KindTag::kSkate && edited.type != KindTag::kGap) {
        int control = edited.walk ? 1 : 0;
        ImGui::SetNextItemWidth(kFieldWidth * ImGui::GetFontSize() / ui_layout::kReferenceFontSize);
        if (ImGui::Combo("Control##goal", &control, "Skate\0Walk\0")) {
            edited.walk = control == 1;
            changed = done = true;
        }
    }
}

// A gap goal's gaps (the level's, and any it has the level hasn't now) and its required trick.
void goal_gap_fields(GoalEdit& edit)
{
    Goal& edited = edit.edited;
    bool& done = edit.done;
    bool& changed = edit.changed;
    const std::string& key = edit.key;
    if (edited.type == KindTag::kGap) {
        ImGui::TextUnformatted("Gaps");
        std::vector<std::string> gaps = get_level_gaps();
        for (const std::string& name : edited.gaps) {
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
            auto it = std::ranges::find(edited.gaps, name);
            bool on = it != edited.gaps.end();
            if (ImGui::Checkbox(name.c_str(), &on)) {
                if (on && edited.gaps.size() < kMaxGaps) {
                    edited.gaps.push_back(name);
                } else if (!on) {
                    edited.gaps.erase(it);
                }
                changed = done = true;
            }
        }
        ImGui::EndChild();
        if (text_field("Required trick##goal", key + "trick", &edited.trick, kTrickFieldLimit, false)) {
            changed = done = true;
        }
    }
}

// A trick goal's key combo sets, spin, combo and block sizes and Skatetris' settings.
void goal_trick_fields(GoalEdit& edit)
{
    Goal& edited = edit.edited;
    bool& done = edit.done;
    bool& changed = edit.changed;
    ImFont* mono = edit.mono;
    if (get_trick_type(edited.type)) {
        ImGui::TextUnformatted("Key Combo Sets");
        for (int set = 0; set < combo_set_count(edited.type); ++set) {
            bool on = (edited.sets & (1U << set)) != 0;
            if (ImGui::Checkbox(combo_set_name(set), &on)) {
                edited.sets = on ? edited.sets | (1U << set) : edited.sets & ~(1U << set);
                changed = done = true;
            }
        }
        ImGui::PushFont(mono);
        if (edited.type != KindTag::kTrickBeat) {
            if (int_field("Spin##goal", &edited.spin, kSpinDragSpeed, 0, kMaxSpin, (edited.spin != 0) ? "%d" : "Off",
                          &done)) {
                changed = true;
            }
        }
        if (edited.type == KindTag::kComboSkatetris) {
            if (ImGui::Checkbox("Single combo##goal", &edited.single_combo_)) {
                changed = done = true;
            }
            if (int_field("Combo size##goal", &edited.combo_size_, kCountDragSpeed, 1, kMaxTrickCount, "%d", &done)) {
                changed = true;
            }
        }
        if (edited.type == KindTag::kTrickTris) {
            if (int_field("Block size##goal", &edited.block_size_, kCountDragSpeed, 1, kMaxTrickCount, "%d", &done)) {
                changed = true;
            }
            if (int_field("Total to win##goal", &edited.to_win_, kToWinDragSpeed, 1, kMaxToWin, "%d", &done)) {
                changed = true;
            }
        }
        if (edited.type == KindTag::kSkatetris || (edited.type == KindTag::kComboSkatetris && !edited.single_combo_)) {
            skatetris_fields(edit);
        }
        ImGui::PopFont();
    }
}

void current_settings(const Goal& goal, ImFont* mono)
{
    GoalEdit edit{.edited = goal, .done = false, .changed = false, .key = goal.id + "/", .mono = mono};
    goal_text_fields(edit);
    goal_number_fields(edit);
    goal_control_field(edit);
    goal_gap_fields(edit);
    goal_trick_fields(edit);
    if (edit.changed) {
        get_apply(edit.edited, edit.done);
    } else if (edit.done) {
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
    auto it = std::ranges::find_if(all, [&](const Goal& goal) { return goal.id == id; });
    if (it == all.end()) {
        return;
    }
    const Goal& goal = *it;
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Goal###goal", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    gui::text_wrapped("%s - %s (%s)", goal.name.c_str(), type_title(goal.type), point_label(goal.type, point).c_str());
    std::string why;
    if (get_complete(goal, &why)) {
        gui::text_colored(kCompleteColour, "Complete");
    } else {
        gui::text_colored(kNotPlayedColour, "Not played: %s", why.c_str());
    }
    const int missing = get_missing(goal.id);
    if (missing > 0) {
        gui::text_colored(kDeletedColour, "%d point%s deleted", missing, missing == 1 ? "" : "s");
        ImGui::SameLine();
        const std::string goal_id = goal.id;
        if (ImGui::SmallButton("Restore Points")) {
            get_queue([goal_id] { restore_points(goal_id); });
        }
    }
    // The point: its height (letters float above their node).
    if (point >= 2) {
        Goal changed = goal;
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
    for (size_t k = 0; k < goal.points.size(); ++k) {
        ImGui::SameLine();
        std::string label = point_label(goal.type, static_cast<int>(k));
        if (label.starts_with("Checkpoint ")) {
            label = "CP" + label.substr(kCheckpointPrefixLength);
        }
        label += "##goalpt" + std::to_string(k);
        const std::string goal_id = goal.id;
        const int index = static_cast<int>(k);
        if (ImGui::SmallButton(label.c_str())) {
            get_queue([goal_id, index] { choose_one(goal_id, index); });
        }
    }
    ImGui::PopFont();
    if (goal.type == KindTag::kRace) {
        const std::string goal_id = goal.id;
        if (static_cast<int>(goal.points.size()) < max_points(goal.type) && ImGui::Button("Add checkpoint##goal")) {
            get_queue([goal_id] { add_checkpoint(goal_id); });
        }
        if (static_cast<int>(goal.points.size()) > min_points(goal.type)) {
            ImGui::SameLine();
            if (ImGui::Button("Remove last##goal")) {
                get_queue([goal_id] { remove_checkpoint(goal_id); });
            }
        }
    }
    current_settings(goal, mono);
    ImGui::Spacing();
    const std::string goal_id = goal.id;
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
        for (int type_index = 0; type_index < kTypes; ++type_index) {
            if (ImGui::Selectable(type_title(static_cast<KindTag>(type_index)), type_index == new_type())) {
                new_type() = type_index;
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
    for (const Goal& goal : all) {
        ImGui::PushID(goal.id.c_str());
        const bool complete = get_complete(goal);
        gui::text_colored(complete ? kCompleteTitleColour : kIncompleteTitleColour, "%s", goal.name.c_str());
        if (ImGui::IsItemHovered()) {
            std::string why;
            gui::tooltip("%s - %s", type_title(goal.type), get_complete(goal, &why) ? "complete" : why.c_str());
        }
        ImGui::SameLine();
        const std::string goal_id = goal.id;
        if (ImGui::SmallButton("Select")) {
            get_queue([goal_id] { choose_one(goal_id, 0); });
        }
        ImGui::PopID();
    }
    ImGui::PopFont();
}

} // namespace frontend::level_editor::goal_edit

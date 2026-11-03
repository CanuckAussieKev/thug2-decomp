// NOT ORIGINAL - the level editor's trigger volumes in the UI (ours; see level_editor_triggers.h): the Triggers window (T)
// and the Inspector's section for a trigger. ImGui only; every change is queued for the editor's next update
// (Triggers::Queue).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor_cameras.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_triggers.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "editor/level_editor_playtest.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "engine/core/no_destroy.h"
#include "editor/level_editor.h"

namespace frontend::level_editor::triggers {

namespace {
constexpr std::uint32_t kChannelByteMask = 0xFF;
constexpr std::size_t kTextBufferSize = 64; // the target, script, text and sound fields
constexpr float kNewProximRadius = 200.0F;  // a new proximity node default size
constexpr float kNewProximWidth = 400.0F;
constexpr float kNewProximHeight = 200.0F;
constexpr float kNewProximDepth = 400.0F;
constexpr float kRadiusDragStep = 2.0F;
constexpr float kSizeDragStep = 5.0F;
constexpr float kMinSize = 10.0F; // a trigger box side limits
constexpr float kMaxSize = 50000.0F;
constexpr std::uint32_t kDisabledColour = 0xFFA0A0A0U;
constexpr float kPaneInset = 8.0F; // the Triggers pane gap from the view right edge and top, at scale 1
constexpr float kPaneTop = 40.0F;
constexpr float kPaneWidth = 380.0F;
constexpr int kSeparatorEvery = 5; // a separator after every fifth list row
constexpr float kListHeight = 180.0F;
constexpr int kMaxListed = 400;
constexpr int kScoreStep = 50;
constexpr int kScoreStepFast = 500;
constexpr int kMaxScore = 1000000;
constexpr ImVec4 kDisabledTextColour{1.0F, 0.75F, 0.4F, 1.0F};
constexpr ImVec4 kLandedColour{0.25F, 1.0F, 0.4F, 1.0F};
constexpr ImVec4 kNotLandedColour{1.0F, 0.7F, 0.25F, 1.0F};
constexpr ImVec4 kNotActiveColour{1.0F, 0.55F, 0.45F, 1.0F};

constexpr float kChannelMax = 255.0F; // the largest 8-bit colour channel

ImVec4 get_colour(uint32_t argb)
{
    return {static_cast<float>((argb >> kRedShift) & kChannelByteMask) / kChannelMax, static_cast<float>((argb >> kGreenShift) & kChannelByteMask) / kChannelMax,
            static_cast<float>(argb & kChannelByteMask) / kChannelMax, static_cast<float>(argb >> kAlphaShift) / kChannelMax};
}

std::string get_name_of(uint32_t checksum) { return name_for(checksum, script::kept_qb_name(checksum)); }

// NOT ORIGINAL grouping - level_editor_triggers_ui.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_triggers_ui_state()).
struct LevelEditorTriggersUiState {
    // The level's restarts (names), and the scripts its nodes' TriggerScripts name - made again when the nodes change.
    std::vector<std::string> restarts_; // (map4: the editor's camera nodes)
    std::vector<std::string> scripts_;
    std::vector<std::string> cameras_;
    uint32_t lists_generation_ = 0xFFFFFFFF;
};

LevelEditorTriggersUiState& level_editor_triggers_ui_state()
{
    static core::NoDestroy<LevelEditorTriggersUiState> state;
    return state.get();
}

void update_lists()
{
    if (level_editor_triggers_ui_state().lists_generation_ == nodes::get_generation()) {
        return;
    }
    level_editor_triggers_ui_state().lists_generation_ = nodes::get_generation();
    level_editor_triggers_ui_state().restarts_.clear();
    level_editor_triggers_ui_state().scripts_.clear();
    level_editor_triggers_ui_state().cameras_.clear();
    for (const cameras::ViewCam& camera : cameras::get_all()) {
        level_editor_triggers_ui_state().cameras_.push_back(camera.name);
    }
    static const uint32_t kTriggerScript = 0x2CA8A299;
    for (const nodes::Elem& node : nodes::get_all()) {
        if (node.family == nodes::Family::kSpawn && !node.hidden && node.name) {
            level_editor_triggers_ui_state().restarts_.push_back(get_name_of(node.name));
        }
        if (!node.trigger_script_ || node.added) {
            continue;
        }
        script::Struct* s = nodes::struct_of(node);
        uint32_t script = 0;
        if (s && s->read_hash_value(kTriggerScript, &script, 0) && script) {
            const std::string name = get_name_of(script);
            if (std::ranges::find(level_editor_triggers_ui_state().scripts_, name) ==
                level_editor_triggers_ui_state().scripts_.end()) {
                level_editor_triggers_ui_state().scripts_.push_back(name);
            }
        }
    }
    std::ranges::sort(level_editor_triggers_ui_state().scripts_);
}

// A name field: typed, applied when let go (Enter or leaving it). Its buffer follows `value` while it isn't edited.
bool name_field(const char* label, std::string* value, char* buffer, size_t size, bool* editing)
{
    if (!*editing) {
        text::format_to(std::span(buffer, size), "%s", value->c_str());
    }
    ImGui::InputText(label, buffer, size);
    *editing = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        *value = buffer;
        return true;
    }
    return false;
}

// A choice among `names` (and the value itself if it isn't one of them).
bool name_combo(const char* label, std::string* value, const std::vector<std::string>& names)
{
    bool changed = false;
    if (ImGui::BeginCombo(label, value->empty() ? "(none)" : value->c_str(), ImGuiComboFlags_HeightLarge)) {
        for (const std::string& name : names) {
            const bool chosen = name == *value;
            if (ImGui::Selectable(name.c_str(), chosen)) {
                *value = name;
                changed = true;
            }
            if (chosen) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// A breakable's sound: one of the shared hits, typed, or none.
bool sound_combo(std::string* sound)
{
    bool changed = false;
    if (ImGui::BeginCombo("Sound", sound->empty() ? "(none)" : sound->c_str())) {
        if (ImGui::Selectable("(none)", sound->empty()) && !sound->empty()) {
            sound->clear();
            changed = true;
        }
        for (const char* name : break_sounds()) {
            if (ImGui::Selectable(name, *sound == name) && *sound != name) {
                *sound = name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    static std::array<char, kTextBufferSize> typed;
    static bool editing;
    changed |= name_field("Sound Name", sound, typed.data(), sizeof(typed), &editing);
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Must be a sound the level loads");
    }
    return changed;
}

// A proximity node's scripts and shape (level_editor_triggers.h: ProximEdit).
void proxim_section(uint32_t node)
{
    update_lists();
    ImGui::Spacing();
    ImGui::SeparatorText("Proximity Node");
    const ProximEdit* now = proxim_of(node);
    ProximEdit e = now ? *now : ProximEdit{};
    e.node = node;
    gui::text_disabled("Scripts run on enter and exit");
    static std::array<char, kTextBufferSize> g_enter_buffer;
    static std::array<char, kTextBufferSize> g_exit_buffer;
    static bool g_enter_editing;
    static bool g_exit_editing;
    bool changed = false;
    changed |= name_combo("On entering", &e.enter, level_editor_triggers_ui_state().scripts_);
    changed |= name_field("Or by name##enter", &e.enter, g_enter_buffer.data(), sizeof(g_enter_buffer), &g_enter_editing);
    changed |= name_combo("On leaving", &e.exit, level_editor_triggers_ui_state().scripts_);
    changed |= name_field("Or by name##exit", &e.exit, g_exit_buffer.data(), sizeof(g_exit_buffer), &g_exit_editing);
    gui::text_disabled("(none: node's own script)");
    int shape = e.shape ? (e.box ? 2 : 1) : 0;
    ImGui::TextUnformatted("Shape");
    ImGui::SameLine();
    bool shape_changed = ImGui::RadioButton("Original", &shape, 0);
    ImGui::SameLine();
    shape_changed |= ImGui::RadioButton("sphere", &shape, 1);
    ImGui::SameLine();
    shape_changed |= ImGui::RadioButton("box", &shape, 2);
    if (shape_changed) {
        e.shape = shape != 0;
        e.box = shape == 2;
        if (e.shape && !e.box && e.radius <= 0.0F) {
            e.radius = kNewProximRadius;
        }
        if (e.box && e.size.x <= 0.0F) {
            e.size = {.x = kNewProximWidth, .y = kNewProximHeight, .z = kNewProximDepth};
        }
        changed = true;
    }
    if (e.shape && !e.box) {
        float radius = e.radius;
        if (ImGui::DragFloat("Radius", &radius, kRadiusDragStep, kProximMin, kProximMax, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
            ProximEdit sized = e;
            sized.radius = radius;
            get_queue([sized] { set_proxim(sized, false); });
        }
        if (ImGui::IsItemActivated()) {
            get_queue([] { begin_edit(); });
        }
    } else if (e.shape) {
        std::array<float, 3> size = {{e.size.x, e.size.y, e.size.z}};
        if (ImGui::DragFloat3("Size##proxim", size.data(), kSizeDragStep, kProximMin, kProximMax, "%.0f",
                              ImGuiSliderFlags_AlwaysClamp)) {
            ProximEdit sized = e;
            sized.size = {.x = core::at(size, 0), .y = core::at(size, 1), .z = core::at(size, 2)};
            get_queue([sized] { set_proxim(sized, false); });
        }
        if (ImGui::IsItemActivated()) {
            get_queue([] { begin_edit(); });
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("World-aligned box");
        }
    }
    if (now && ImGui::Button("Revert")) {
        get_queue([node] { set_proxim(ProximEdit{.node = node}, true); });
    }
    gui::text_disabled("Drag the handles to resize");
    if (changed) {
        get_queue([e] { set_proxim(e, true); });
    }
}

const Added* added_named(uint32_t name)
{
    for (const Added& a : added_triggers()) {
        if (node_create::name_hash(a.spec.name) == name) {
            return &a;
        }
    }
    return nullptr;
}

const LevelTrigger* level_named(uint32_t sector)
{
    for (const LevelTrigger& l : level_triggers()) {
        if (l.sector == sector) {
            return &l;
        }
    }
    return nullptr;
}

void level_trigger_details(const LevelTrigger& l)
{
    gui::text_colored(get_colour(l.off ? kDisabledColour : kind_colour(l.kind)), "%s", describe_level_trigger(l).c_str());
    gui::text_disabled("Level trigger %s: %d trigger faces%s", nodes::caption(l.sector).c_str(), l.face_count_,
                        l.has_node_ ? "" : " (no node)");
    gui::text_disabled("TriggerScript: %s", l.script ? nodes::caption(l.script).c_str() : "(none)");
    gui::text_disabled("Box (%.0f, %.0f, %.0f) .. (%.0f, %.0f, %.0f)", l.min.x, l.min.y, l.min.z, l.max.x, l.max.y,
                        l.max.z);
    const uint32_t sector = l.sector;
    const bool switchable = l.wall || l.all_trigger_;
    if (l.off) {
        gui::text_colored(kDisabledTextColour, "Disabled");
    }
    ImGui::BeginDisabled(!switchable);
    if (ImGui::Button(l.off ? "Switch on" : "Switch off")) {
        get_queue([sector, off = !l.off] { turn_off(sector, off); });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        gui::tooltip("%s", switchable ? "Enable or disable this trigger" : "Has non-trigger faces; can't be disabled");
    }
    if (l.wall) {
        ImGui::SameLine();
        if (ImGui::Button("Move it")) {
            get_queue([sector] { select_wall(sector); });
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Move as an invisible wall (size is fixed)");
        }
    }
    ImGui::BeginDisabled(!switchable || !l.script);
    if (ImGui::Button("Make Editable Copy")) {
        get_queue([sector] { make_editable(sector); });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        gui::tooltip("Replace with an editable trigger volume; the original is disabled");
    }
}

} // namespace

void pane(PaneArea area, PaneState pane_state)
{
    const auto scale = pane_state.scale;
    const auto selected_node = pane_state.selected_node_;
    const auto view_x = area.x;
    const auto view_y = area.y;
    const auto view_width = area.width;
    const auto view_height = area.height;
    (void)view_height;
    (void)selected_node;
    if (!get_shown()) {
        return;
    }
    update_lists();
    ImGui::SetNextWindowPos(ImVec2(view_x + view_width - (kPaneInset * scale), view_y + (kPaneTop * scale)),
                            ImGuiCond_FirstUseEver, ImVec2(1.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(kPaneWidth * scale, 0.0F), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Triggers###triggers", &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open) {
            get_queue([] { set_shown(false); });
        }
        return;
    }
    gui::text_disabled("Add at view center:");
    if (ImGui::Button("Kill zone")) {
        get_queue([] { add_kill(false); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Bail and respawn at a restart");
    }
    ImGui::SameLine();
    if (ImGui::Button("Water")) {
        get_queue([] { add_kill(true); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Splash and respawn at a restart");
    }
    ImGui::SameLine();
    if (ImGui::Button("Teleport")) {
        get_queue([] { add_teleport(); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Teleport the skater to a node");
    }
    ImGui::SameLine();
    if (ImGui::Button("Script")) {
        get_queue([] { append_script(); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Run a level script");
    }
    ImGui::SameLine();
    if (ImGui::Button("Gap")) {
        get_queue([] { register_gap_entry(); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Gap start and end");
    }
    ImGui::SameLine();
    if (ImGui::Button("Jump boost")) {
        get_queue([] { add_boost(); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Launch the skater upward");
    }
    ImGui::SameLine();
    if (ImGui::Button("Camera")) {
        get_queue([] { add_camera(); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Play a camera shot");
    }
    gui::text_disabled("%s", describe().c_str());
    gui::text_disabled("Breakables: select scenery, then Breaks When Hit");
    // The legend.
    for (int k = 0; k < kKinds; ++k) {
        if (k % kSeparatorEvery) {
            ImGui::SameLine();
        }
        gui::text_colored(get_colour(kind_colour(static_cast<Kind>(k))), "%s", kind_name(static_cast<Kind>(k)));
    }
    ImGui::Separator();
    // The nearest (the editor's, then the level's), a click selects / inspects.
    if (ImGui::BeginChild("##triggerlist", ImVec2(0.0F, kListHeight * scale), ImGuiChildFlags_Borders)) {
        for (const Added& a : added_triggers()) {
            if (!a.alive) {
                continue;
            }
            std::string text = a.spec.name + "  " + kind_name(a.spec.kind);
            if ((a.spec.kind == Kind::kGapStart || a.spec.kind == Kind::kGapEnd) && !a.spec.text.empty()) {
                text += std::string("  - ") + play_test::gap_note(clean_text(a.spec.text)); // (landed in a test run?)
            }
            ImGui::PushStyleColor(ImGuiCol_Text, get_colour(a.problem ? kDisabledColour : kind_colour(a.spec.kind)));
            const uint32_t name = node_create::name_hash(a.spec.name);
            if (ImGui::Selectable((text + "##a" + a.spec.name).c_str(), false)) {
                get_queue([name] { select_node_named(name); });
            }
            ImGui::PopStyleColor();
        }
        int shown = 0;
        for (const LevelTrigger& l : level_triggers()) {
            if (++shown > kMaxListed) {
                break;
            }
            const std::string text =
                nodes::caption(l.sector) + "  " + describe_level_trigger(l) + (l.off ? "  (off)" : "");
            ImGui::PushStyleColor(ImGuiCol_Text, get_colour(l.off ? kDisabledColour : kind_colour(l.kind)));
            const uint32_t sector = l.sector;
            const bool has_node = l.has_node_;
            if (ImGui::Selectable((text + "##l" + std::to_string(l.sector)).c_str(), l.sector == inspected_sector())) {
                get_queue([sector, has_node] {
                    inspect(sector);
                    if (has_node) {
                        select_node_named(sector);
                    }
                });
            }
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    if (const LevelTrigger* l = level_named(inspected_sector())) {
        ImGui::Separator();
        level_trigger_details(*l);
    }
    ImGui::End();
    if (!open) {
        get_queue([] { set_shown(false); });
    }
}

void inspector_section(int index)
{
    const std::vector<nodes::Elem>& all = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, all.size())) {
        return;
    }
    const uint32_t name = core::at(all, index).name;
    if (const LevelTrigger* l = level_named(name)) {
        ImGui::Spacing();
        ImGui::SeparatorText("Trigger Volume");
        level_trigger_details(*l);
        return;
    }
    const Added* a = added_named(name);
    if (!a) {
        if (is_proxim(name)) {
            proxim_section(name);
        }
        return;
    }
    update_lists();
    ImGui::Spacing();
    ImGui::SeparatorText("Trigger Volume");
    Spec s = a->spec;
    const std::string key = s.name;
    bool changed = false;
    if (s.kind == Kind::kBreak) {
        // (a breakable: its piece and sound - its box is the piece's)
        gui::text_colored(get_colour(kind_colour(s.kind)), "Breakable: %s%s", s.target.c_str(),
                           a->broken ? " (broken)" : "");
        if (sound_combo(&s.sound)) {
            get_queue([key, s] { set_spec(key, s, true); });
        }
        gui::text_disabled("Shatters when hit");
        return;
    }
    // The kind.
    const auto kinds = std::to_array<Kind>({Kind::kKill,   Kind::kJumpTo, Kind::kScripts, Kind::kGapStart,
                          Kind::kGapEnd, Kind::kBoost,  Kind::kViewCam});
    if (ImGui::BeginCombo("Kind", kind_name(s.kind))) {
        for (const Kind k : kinds) {
            if (ImGui::Selectable(kind_name(k), k == s.kind) && k != s.kind) {
                s.kind = k;
                if ((k == Kind::kGapStart || k == Kind::kGapEnd) && s.gap.empty()) {
                    s.gap = s.name; // (a gap of its own until it's given another end)
                }
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    // The size (one undo step a drag).
    std::array<float, 3> size = {{s.size.x, s.size.y, s.size.z}};
    if (ImGui::DragFloat3("Size", size.data(), kSizeDragStep, kMinSize, kMaxSize, "%.0f")) {
        Spec sized = s;
        sized.size = {.x = core::at(size, 0), .y = core::at(size, 1), .z = core::at(size, 2)};
        get_queue([key, sized] { set_spec(key, sized, false); });
    }
    if (ImGui::IsItemActivated()) {
        get_queue([] { begin_edit(); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Width, height, depth");
    }
    static std::array<char, kTextBufferSize> target;
    static std::array<char, kTextBufferSize> script;
    static std::array<char, kTextBufferSize> text;
    static bool g_target_editing;
    static bool g_script_editing;
    static bool g_text_editing;
    switch (s.kind) {
    case Kind::kKill:
        changed |= name_combo("Restart", &s.target, level_editor_triggers_ui_state().restarts_);
        changed |= ImGui::Checkbox("Water", &s.water);
        break;
    case Kind::kJumpTo:
        changed |= name_combo("To", &s.target, level_editor_triggers_ui_state().restarts_);
        changed |= name_field("Or a node", &s.target, target.data(), sizeof(target), &g_target_editing);
        break;
    case Kind::kScripts:
        changed |= name_combo("Script", &s.script, level_editor_triggers_ui_state().scripts_);
        changed |= name_field("Or by name", &s.script, script.data(), sizeof(script), &g_script_editing);
        if (!a->problem && (a->calls.kill || a->calls.teleport || a->calls.end_gap_ || a->calls.start_gap_)) {
            gui::text_disabled("It calls: %s", kind_name(kind_of_calls(a->calls, true)));
        }
        break;
    case Kind::kGapStart:
    case Kind::kGapEnd: {
        gui::text_disabled("Gap %s (%s)", s.gap.c_str(), s.kind == Kind::kGapStart ? "StartGap here" : "EndGap here");
        if (!s.text.empty()) {
            // (validated by skating - level_editor_playtest.h)
            const bool landed = play_test::gap_landed(clean_text(s.text));
            gui::text_colored(landed ? kLandedColour : kNotLandedColour, "%s",
                               landed ? "Landed in a test run" : "Not yet landed in a test run");
        }
        changed |= name_field("Name", &s.text, text.data(), sizeof(text), &g_text_editing);
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Gap name");
        }
        int score = s.score;
        if (ImGui::InputInt("Score", &score, kScoreStep, kScoreStepFast) && score != s.score) {
            s.score = std::max(0, std::min(kMaxScore, score));
            changed = true;
        }
        if (ImGui::TreeNode("Flags")) {
            for (const char* flag : gap_option_labels()) {
                bool on = std::ranges::find(s.flags, flag) != s.flags.end();
                if (ImGui::Checkbox(flag, &on)) {
                    if (on) {
                        s.flags.emplace_back(flag);
                    } else {
                        std::erase(s.flags, flag);
                    }
                    changed = true;
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::Button("Select Other End")) {
            get_queue([key] { select_partner(key); });
        }
        break;
    }
    case Kind::kBoost: {
        int strength = s.strength;
        if (ImGui::SliderInt("Strength", &strength, kBoostMin, kBoostMax, "%d", ImGuiSliderFlags_AlwaysClamp) &&
            strength != s.strength) {
            Spec boosted = s;
            boosted.strength = strength;
            get_queue([key, boosted] { set_spec(key, boosted, false); });
        }
        if (ImGui::IsItemActivated()) {
            get_queue([] { begin_edit(); });
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Upward speed (600 hop, 900 jump, 2000+ launch)");
        }
        break;
    }
    case Kind::kViewCam: {
        // (map4) the camera played and for how long
        changed |= name_combo("Camera", &s.target, level_editor_triggers_ui_state().cameras_);
        changed |= name_field("Or a node", &s.target, target.data(), sizeof(target), &g_target_editing);
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Camera node");
        }
        float seconds = s.seconds;
        if (ImGui::SliderFloat("Seconds", &seconds, kCameraMinSeconds, kCameraMaxSeconds, "%.1f",
                               ImGuiSliderFlags_AlwaysClamp) &&
            seconds != s.seconds) {
            Spec timed = s;
            timed.seconds = seconds;
            get_queue([key, timed] { set_spec(key, timed, false); });
        }
        if (ImGui::IsItemActivated()) {
            get_queue([] { begin_edit(); });
        }
        gui::text_disabled("LevelEditor_PlayCamera camera=%s time=%.1f",
                            s.target.empty() ? "<camera>" : s.target.c_str(), s.seconds);
        break;
    }
    case Kind::kOther:
    case Kind::kBreak:
        break;
    }
    changed |= ImGui::Checkbox("Also on Exit", &s.exit);
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Run the script when leaving too");
    }
    if (s.replaces) {
        gui::text_disabled("Replaces %s", nodes::caption(s.replaces).c_str());
    }
    if (a->problem) {
        gui::text_colored(kNotActiveColour, "Not active: %s", a->problem);
    } else {
        gui::text_colored(get_colour(kind_colour(s.kind)), "Runs %s on enter%s", get_script_name(s).c_str(),
                           s.exit ? " and exit" : "");
    }

    if (changed) {
        get_queue([key, s] { set_spec(key, s, true); });
    }
}

void scenery_section(uint32_t sector)
{
    if (!sector) {
        return;
    }
    const Added* a = breakable_of(sector);
    bool on = a != nullptr;
    ImGui::Spacing();
    if (ImGui::Checkbox("Breaks When Hit", &on)) {
        get_queue([sector, on] { set_breakable(sector, on); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Shatter when the skater hits it");
    }
    if (!a) {
        return;
    }
    Spec s = a->spec;
    const std::string key = s.name;
    if (sound_combo(&s.sound)) {
        get_queue([key, s] { set_spec(key, s, true); });
    }
    if (a->broken) {
        gui::text_disabled("Broken in play; restored in the editor");
    }
}

} // namespace frontend::level_editor::triggers

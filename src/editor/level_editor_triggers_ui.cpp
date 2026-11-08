// NOT ORIGINAL - the level editor's trigger volumes in the UI (ours; see level_editor_triggers.h): the Triggers window (T)
// and the Inspector's section for a trigger. ImGui only; every change is queued for the editor's next update
// (Triggers::Queue).

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor_cameras.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_presets.h"
#include "editor/level_editor_triggers.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "editor/level_editor_playtest.h"
#include "imgui.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor.h"
#include "editor/level_editor_ui_app.h"

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
        if (node.family == nodes::Family::kSpawn && !node.hidden && (node.name != 0U)) {
            level_editor_triggers_ui_state().restarts_.push_back(get_name_of(node.name));
        }
        if (!node.trigger_script_ || node.added) {
            continue;
        }
        script::Struct* structure = nodes::struct_of(node);
        uint32_t script = 0;
        if (structure && structure->read_hash_value(kTriggerScript, &script, 0) && (script != 0U)) {
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
    ProximEdit proxim_edit = now ? *now : ProximEdit{};
    proxim_edit.node = node;
    gui::text_disabled("Scripts run on enter and exit");
    static std::array<char, kTextBufferSize> g_enter_buffer;
    static std::array<char, kTextBufferSize> g_exit_buffer;
    static bool g_enter_editing;
    static bool g_exit_editing;
    bool changed = false;
    changed |= name_combo("On entering", &proxim_edit.enter, level_editor_triggers_ui_state().scripts_);
    changed |= name_field("Or by name##enter", &proxim_edit.enter, g_enter_buffer.data(), sizeof(g_enter_buffer),
                          &g_enter_editing);
    changed |= name_combo("On leaving", &proxim_edit.exit, level_editor_triggers_ui_state().scripts_);
    changed |=
        name_field("Or by name##exit", &proxim_edit.exit, g_exit_buffer.data(), sizeof(g_exit_buffer), &g_exit_editing);
    gui::text_disabled("(none: node's own script)");
    int shape = 0; // (none: 0, a sphere: 1, a box: 2)
    if (proxim_edit.shape) {
        shape = proxim_edit.box ? 2 : 1;
    }
    ImGui::TextUnformatted("Shape");
    ImGui::SameLine();
    bool shape_changed = ImGui::RadioButton("Original", &shape, 0);
    ImGui::SameLine();
    shape_changed |= ImGui::RadioButton("sphere", &shape, 1);
    ImGui::SameLine();
    shape_changed |= ImGui::RadioButton("box", &shape, 2);
    if (shape_changed) {
        proxim_edit.shape = shape != 0;
        proxim_edit.box = shape == 2;
        if (proxim_edit.shape && !proxim_edit.box && proxim_edit.radius <= 0.0F) {
            proxim_edit.radius = kNewProximRadius;
        }
        if (proxim_edit.box && proxim_edit.size.x <= 0.0F) {
            proxim_edit.size = {.x = kNewProximWidth, .y = kNewProximHeight, .z = kNewProximDepth};
        }
        changed = true;
    }
    if (proxim_edit.shape && !proxim_edit.box) {
        float radius = proxim_edit.radius;
        if (ImGui::DragFloat("Radius", &radius, kRadiusDragStep, kProximMin, kProximMax, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
            ProximEdit sized = proxim_edit;
            sized.radius = radius;
            get_queue([sized] { set_proxim(sized, false); });
        }
        if (ImGui::IsItemActivated()) {
            get_queue([] { begin_edit(); });
        }
    } else if (proxim_edit.shape) {
        std::array<float, 3> size = {{proxim_edit.size.x, proxim_edit.size.y, proxim_edit.size.z}};
        if (ImGui::DragFloat3("Size##proxim", size.data(), kSizeDragStep, kProximMin, kProximMax, "%.0f",
                              ImGuiSliderFlags_AlwaysClamp)) {
            ProximEdit sized = proxim_edit;
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
    if ((now != nullptr) && ImGui::Button("Revert")) {
        get_queue([node] { set_proxim(ProximEdit{.node = node}, true); });
    }
    gui::text_disabled("Drag the handles to resize");
    if (changed) {
        get_queue([proxim_edit] { set_proxim(proxim_edit, true); });
    }
}

// Two node names the same (case doesn't matter, as checksums).
bool same_name(const std::string& a, const std::string& b)
{
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

const Added* added_named(uint32_t name)
{
    for (const Added& added_trigger : added_triggers()) {
        if (node_create::name_hash(added_trigger.spec.name) == name) {
            return &added_trigger;
        }
    }
    return nullptr;
}

const LevelTrigger* level_named(uint32_t sector)
{
    for (const LevelTrigger& level_trigger : level_triggers()) {
        if (level_trigger.sector == sector) {
            return &level_trigger;
        }
    }
    return nullptr;
}

void level_trigger_details(const LevelTrigger& level_trigger)
{
    gui::text_colored(get_colour(level_trigger.off ? kDisabledColour : kind_colour(level_trigger.kind)), "%s",
                      describe_level_trigger(level_trigger).c_str());
    gui::text_disabled("Level trigger %s: %d trigger faces%s", nodes::caption(level_trigger.sector).c_str(),
                       level_trigger.face_count_, level_trigger.has_node_ ? "" : " (no node)");
    gui::text_disabled("TriggerScript: %s",
                       (level_trigger.script != 0U) ? nodes::caption(level_trigger.script).c_str() : "(none)");
    gui::text_disabled("Box (%.0f, %.0f, %.0f) .. (%.0f, %.0f, %.0f)", level_trigger.min.x, level_trigger.min.y,
                       level_trigger.min.z, level_trigger.max.x, level_trigger.max.y, level_trigger.max.z);
    const uint32_t sector = level_trigger.sector;
    const bool switchable = level_trigger.wall || level_trigger.all_trigger_;
    if (level_trigger.off) {
        gui::text_colored(kDisabledTextColour, "Disabled");
    }
    ImGui::BeginDisabled(!switchable);
    if (ImGui::Button(level_trigger.off ? "Switch on" : "Switch off")) {
        get_queue([sector, off = !level_trigger.off] { turn_off(sector, off); });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        gui::tooltip("%s", switchable ? "Enable or disable this trigger" : "Has non-trigger faces; can't be disabled");
    }
    if (level_trigger.wall) {
        ImGui::SameLine();
        if (ImGui::Button("Move it")) {
            get_queue([sector] { select_wall(sector); });
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("Move as an invisible wall (size is fixed)");
        }
    }
    ImGui::BeginDisabled(!switchable || (level_trigger.script == 0U));
    if (ImGui::Button("Make Editable Copy")) {
        get_queue([sector] { make_editable(sector); });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        gui::tooltip("Replace with an editable trigger volume; the original is disabled");
    }
}

// A preset button: `action` queued when clicked, `tip` on hover; on the same line as the last unless `first`.
void preset_button(const char* label, std::function<void()> action, const char* tip, bool first = false)
{
    if (!first) {
        ImGui::SameLine();
    }
    if (ImGui::Button(label)) {
        get_queue(std::move(action));
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("%s", tip);
    }
}

// The presets: one-click setups where the view looks (level_editor_triggers.h, level_editor_presets.h).
void preset_buttons()
{
    gui::text_disabled("Presets:");
    preset_button("Booster Pad", [] { add_booster(false); }, "Speed pad: pushes the skater the way the arrow points",
                  true);
    preset_button("Booster Area", [] { add_booster(true); }, "Speed zone: a wide floor area");
    preset_button("Teleport + Exit", [] { add_teleport_pair(); }, "Teleport and the spot it sends the skater to");
    preset_button("Two-way Gap", [] { add_two_way_gap(); }, "A gap each way between two spots", true);
    preset_button("Car on Path", [] { presets::add_on_path(true); }, "A copy of one of the level's cars on a new loop path");
    preset_button("Ped on Path", [] { presets::add_on_path(false); }, "A copy of one of the level's peds on a new loop path");
    ImGui::Spacing();
}

// The add buttons: a trigger of each kind at the view's centre.
void add_buttons()
{
    preset_buttons();
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
}

// The nearest triggers (the editor's, then the level's); a click selects / inspects one.
void trigger_list()
{
    for (const Added& added_trigger : added_triggers()) {
        if (!added_trigger.alive) {
            continue;
        }
        std::string text = added_trigger.spec.name + "  " + kind_name(added_trigger.spec.kind);
        if ((added_trigger.spec.kind == Kind::kGapStart || added_trigger.spec.kind == Kind::kGapEnd) &&
            !added_trigger.spec.text.empty()) {
            text += std::string("  - ") +
                    play_test::gap_note(clean_text(added_trigger.spec.text)); // (landed in a test run?)
        }
        ImGui::PushStyleColor(
            ImGuiCol_Text, get_colour(added_trigger.problem ? kDisabledColour : kind_colour(added_trigger.spec.kind)));
        const uint32_t name = node_create::name_hash(added_trigger.spec.name);
        if (ImGui::Selectable((text + "##a" + added_trigger.spec.name).c_str(), false)) {
            get_queue([name] { select_node_named(name); });
        }
        ImGui::PopStyleColor();
    }
    int shown = 0;
    for (const LevelTrigger& level_trigger : level_triggers()) {
        if (++shown > kMaxListed) {
            break;
        }
        const std::string text = nodes::caption(level_trigger.sector) + "  " + describe_level_trigger(level_trigger) +
                                 (level_trigger.off ? "  (off)" : "");
        ImGui::PushStyleColor(ImGuiCol_Text,
                              get_colour(level_trigger.off ? kDisabledColour : kind_colour(level_trigger.kind)));
        const uint32_t sector = level_trigger.sector;
        const bool has_node = level_trigger.has_node_;
        if (ImGui::Selectable((text + "##l" + std::to_string(level_trigger.sector)).c_str(),
                              level_trigger.sector == inspected_sector())) {
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

// A trigger's kind chosen (a gap one: a gap of its own until it's given another end); whether it changed.
bool kind_combo(Spec& spec, std::span<const Kind> kinds)
{
    bool changed = false;
    if (ImGui::BeginCombo("Kind", kind_name(spec.kind))) {
        for (const Kind kind : kinds) {
            if (ImGui::Selectable(kind_name(kind), kind == spec.kind) && kind != spec.kind) {
                spec.kind = kind;
                if ((kind == Kind::kGapStart || kind == Kind::kGapEnd) && spec.gap.empty()) {
                    spec.gap = spec.name; // (a gap of its own until it's given another end)
                }
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// A gap trigger's fields: whether it was landed, its name, score and flags, the other end; whether they changed.
bool gap_fields(Spec& spec, const std::string& key, std::array<char, kTextBufferSize>& text, bool* text_editing)
{
    bool changed = false;
    gui::text_disabled("Gap %s (%s)", spec.gap.c_str(), spec.kind == Kind::kGapStart ? "StartGap here" : "EndGap here");
    if (!spec.text.empty()) {
        // (validated by skating - level_editor_playtest.h)
        const bool landed = play_test::gap_landed(clean_text(spec.text));
        gui::text_colored(landed ? kLandedColour : kNotLandedColour, "%s",
                          landed ? "Landed in a test run" : "Not yet landed in a test run");
    }
    changed |= name_field("Name", &spec.text, text.data(), sizeof(text), text_editing);
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Gap name");
    }
    int score = spec.score;
    if (ImGui::InputInt("Score", &score, kScoreStep, kScoreStepFast) && score != spec.score) {
        spec.score = std::max(0, std::min(kMaxScore, score));
        changed = true;
    }
    if (ImGui::TreeNode("Flags")) {
        for (const char* flag : gap_option_labels()) {
            bool on = std::ranges::find(spec.flags, flag) != spec.flags.end();
            if (ImGui::Checkbox(flag, &on)) {
                if (on) {
                    spec.flags.emplace_back(flag);
                } else {
                    std::erase(spec.flags, flag);
                }
                changed = true;
            }
        }
        ImGui::TreePop();
    }
    if (ImGui::Button("Select Other End")) {
        get_queue([key] { select_partner(key); });
    }
    return changed;
}

// A camera trigger's fields (map4): the camera played and for how long; whether they changed.
bool camera_fields(Spec& spec, const std::string& key, std::array<char, kTextBufferSize>& target, bool* target_editing)
{
    bool changed = false;
    changed |= name_combo("Camera", &spec.target, level_editor_triggers_ui_state().cameras_);
    changed |= name_field("Or a node", &spec.target, target.data(), sizeof(target), target_editing);
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Camera node");
    }
    float seconds = spec.seconds;
    if (ImGui::SliderFloat("Seconds", &seconds, kCameraMinSeconds, kCameraMaxSeconds, "%.1f",
                           ImGuiSliderFlags_AlwaysClamp) &&
        seconds != spec.seconds) {
        Spec timed = spec;
        timed.seconds = seconds;
        get_queue([key, timed] { set_spec(key, timed, false); });
    }
    if (ImGui::IsItemActivated()) {
        get_queue([] { begin_edit(); });
    }
    gui::text_disabled("LevelEditor_PlayCamera camera=%s time=%.1f",
                       spec.target.empty() ? "<camera>" : spec.target.c_str(), spec.seconds);
    return changed;
}

// (presets) A teleport exit's section: what leads there.
void exit_section(const Spec& spec)
{
    // (a teleport's destination: no box, no script - the skater arrives here, facing the arrow)
    gui::text_colored(get_colour(kind_colour(spec.kind)), "Teleport exit");
    int users = 0;
    for (const Added& other : added_triggers()) {
        users += other.spec.kind == Kind::kJumpTo && same_name(other.spec.target, spec.name) ? 1 : 0;
    }
    gui::text_disabled("%d teleport%s lead here", users, users == 1 ? "" : "s");
    gui::text_disabled("Turn it to set the exit heading");
}

// (presets) A teleport's Keep Heading and Add Exit; whether they changed it.
bool teleport_fields(Spec& spec)
{
    const bool changed = ImGui::Checkbox("Keep Heading", &spec.keep);
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Keep the skater's heading and speed (off: face the way the exit faces)");
    }
    if (!is_exit(spec.target) && ImGui::Button("Add Exit")) {
        get_queue([key = spec.name] { add_exit_for(key); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Place an exit in front of it and send the skater there");
    }
    return changed;
}

// (presets) A booster's speed (one undo step a drag) and Keep Direction; whether the latter changed.
bool booster_fields(Spec& spec)
{
    int speed = spec.strength;
    if (ImGui::SliderInt("Speed", &speed, kBoostMin, kBoostMax, "%d", ImGuiSliderFlags_AlwaysClamp) &&
        speed != spec.strength) {
        Spec boosted = spec;
        boosted.strength = speed;
        get_queue([key = spec.name, boosted] { set_spec(key, boosted, false); });
    }
    if (ImGui::IsItemActivated()) {
        get_queue([] { begin_edit(); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Skater speed after the boost (1000 fast, 2500 launch)");
    }
    const bool changed = ImGui::Checkbox("Keep Direction", &spec.along);
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Boost the way the skater is going (off: push the way the arrow points)");
    }
    return changed;
}

} // namespace

void pane(PaneArea area, PaneState pane_state){
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
    if (ui::app::place_floating("Triggers###triggers", false)) {
        ImGui::SetNextWindowPos(ImVec2(view_x + view_width - (kPaneInset * scale), view_y + (kPaneTop * scale)),
                                ImGuiCond_FirstUseEver, ImVec2(1.0F, 0.0F));
        ImGui::SetNextWindowSize(ImVec2(kPaneWidth * scale, 0.0F), ImGuiCond_FirstUseEver);
    }
    bool open = true;
    if (!ImGui::Begin("Triggers###triggers", &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open) {
            get_queue([] { set_shown(false); });
        }
        return;
    }
    add_buttons();
    gui::text_disabled("%s", describe().c_str());
    gui::text_disabled("Breakables: select scenery, then Breaks When Hit");
    // The legend.
    for (int k = 0; k < kKinds; ++k) {
        if ((k % kSeparatorEvery) != 0) {
            ImGui::SameLine();
        }
        gui::text_colored(get_colour(kind_colour(static_cast<Kind>(k))), "%s", kind_name(static_cast<Kind>(k)));
    }
    ImGui::Separator();
    // The nearest (the editor's, then the level's), a click selects / inspects.
    if (ImGui::BeginChild("##triggerlist", ImVec2(0.0F, kListHeight * scale), ImGuiChildFlags_Borders)) {
        trigger_list();
    }
    ImGui::EndChild();
    if (const LevelTrigger* level_trigger = level_named(inspected_sector())) {
        ImGui::Separator();
        level_trigger_details(*level_trigger);
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
    if (const LevelTrigger* level_trigger = level_named(name)) {
        ImGui::Spacing();
        ImGui::SeparatorText("Trigger Volume");
        level_trigger_details(*level_trigger);
        return;
    }
    const Added* added = added_named(name);
    if (!added) {
        if (is_proxim(name)) {
            proxim_section(name);
        }
        return;
    }
    update_lists();
    ImGui::Spacing();
    ImGui::SeparatorText("Trigger Volume");
    Spec spec = added->spec;
    const std::string key = spec.name;
    bool changed = false;
    if (spec.kind == Kind::kBreak) {
        // (a breakable: its piece and sound - its box is the piece's)
        gui::text_colored(get_colour(kind_colour(spec.kind)), "Breakable: %s%s", spec.target.c_str(),
                          added->broken ? " (broken)" : "");
        if (sound_combo(&spec.sound)) {
            get_queue([key, spec] { set_spec(key, spec, true); });
        }
        gui::text_disabled("Shatters when hit");
        return;
    }
    if (spec.kind == Kind::kExit) {
        exit_section(spec);
        return;
    }
    // The kind.
    const auto kinds = std::to_array<Kind>({Kind::kKill, Kind::kJumpTo, Kind::kScripts, Kind::kGapStart,
                                            Kind::kGapEnd, Kind::kBoost, Kind::kSpeed, Kind::kViewCam});
    changed |= kind_combo(spec, kinds);
    // The size (one undo step a drag).
    std::array<float, 3> size = {{spec.size.x, spec.size.y, spec.size.z}};
    if (ImGui::DragFloat3("Size", size.data(), kSizeDragStep, kMinSize, kMaxSize, "%.0f")) {
        Spec sized = spec;
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
    switch (spec.kind) {
    case Kind::kKill:
        changed |= name_combo("Restart", &spec.target, level_editor_triggers_ui_state().restarts_);
        changed |= ImGui::Checkbox("Water", &spec.water);
        break;
    case Kind::kJumpTo:
        changed |= name_combo("To", &spec.target, level_editor_triggers_ui_state().restarts_);
        changed |= name_field("Or a node", &spec.target, target.data(), sizeof(target), &g_target_editing);
        changed |= teleport_fields(spec);
        break;
    case Kind::kScripts:
        changed |= name_combo("Script", &spec.script, level_editor_triggers_ui_state().scripts_);
        changed |= name_field("Or by name", &spec.script, script.data(), sizeof(script), &g_script_editing);
        if (!added->problem &&
            (added->calls.kill || added->calls.teleport || added->calls.end_gap_ || added->calls.start_gap_)) {
            gui::text_disabled("It calls: %s", kind_name(kind_of_calls(added->calls, true)));
        }
        break;
    case Kind::kGapStart:
    case Kind::kGapEnd: {
        changed |= gap_fields(spec, key, text, &g_text_editing);
        break;
    }
    case Kind::kBoost: {
        int strength = spec.strength;
        if (ImGui::SliderInt("Strength", &strength, kBoostMin, kBoostMax, "%d", ImGuiSliderFlags_AlwaysClamp) &&
            strength != spec.strength) {
            Spec boosted = spec;
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
        changed |= camera_fields(spec, key, target, &g_target_editing);
        break;
    }
    case Kind::kSpeed:
        changed |= booster_fields(spec);
        break;
    case Kind::kOther:
    case Kind::kBreak:
    case Kind::kExit:
        break;
    }
    changed |= ImGui::Checkbox("Also on Exit", &spec.exit);
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Run the script when leaving too");
    }
    if (spec.replaces != 0U) {
        gui::text_disabled("Replaces %s", nodes::caption(spec.replaces).c_str());
    }
    if (added->problem) {
        gui::text_colored(kNotActiveColour, "Not active: %s", added->problem);
    } else {
        gui::text_colored(get_colour(kind_colour(spec.kind)), "Runs %s on enter%s", get_script_name(spec).c_str(),
                          spec.exit ? " and exit" : "");
    }

    if (changed) {
        get_queue([key, spec] { set_spec(key, spec, true); });
    }
}

void scenery_section(uint32_t sector)
{
    if (sector == 0U) {
        return;
    }
    const Added* added = breakable_of(sector);
    bool on = added != nullptr;
    ImGui::Spacing();
    if (ImGui::Checkbox("Breaks When Hit", &on)) {
        get_queue([sector, on] { set_breakable(sector, on); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Shatter when the skater hits it");
    }
    if (!added) {
        return;
    }
    Spec spec = added->spec;
    const std::string key = spec.name;
    if (sound_combo(&spec.sound)) {
        get_queue([key, spec] { set_spec(key, spec, true); });
    }
    if (added->broken) {
        gui::text_disabled("Broken in play; restored in the editor");
    }
}

} // namespace frontend::level_editor::triggers

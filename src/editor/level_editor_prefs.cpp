// NOT ORIGINAL - the level editor's preferences and first-run tips (ours). See level_editor_prefs.h. Only ImGui's own
// API here (the renderer-independence rules: level_editor_ui_backend.h).

#include "editor/level_editor_prefs.h"

#include <algorithm>
#include <array>
#include <span>

#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "editor/level_editor_gui_text.h"
#include "engine/core/no_destroy.h"
#include "editor/level_editor.h"

namespace frontend::level_editor::prefs {

namespace {

constexpr int kFlySpeedMin = 50; // the preference limits, used to clamp what is read and as the sliders ends
constexpr int kFlySpeedMax = 20000;
constexpr int kFlyAccelMax = 3000;
constexpr int kLookMin = 10;
constexpr int kLookMax = 500;
constexpr int kGizmoMin = 25;
constexpr int kGizmoMax = 400;
constexpr int kUiScaleMin = 75;
constexpr int kUiScaleMax = 400;
constexpr int kAutosaveMin = 10;
constexpr int kAutosaveMax = 3600;
constexpr int kSnapMoveMax = 1000;
constexpr int kSnapTurnMax = 90;
constexpr int kAutoScaleDefaultPercent = 100; // the scale a fixed UI scale starts from when automatic is turned off
constexpr float kPrefsWidth = 440.0F;         // the Preferences window, at scale 1
constexpr float kSliderWidth = 200.0F;
constexpr float kSwatchGap = 6.0F; // between the accent colour swatches
constexpr float kSelectedSwatchSize = 22.0F;
constexpr float kSwatchSize = 16.0F;
constexpr float kKeysListHeight = 380.0F;
constexpr float kTipsTop = 0.42F; // of the display height
constexpr float kTipsWidth = 470.0F;
constexpr float kTipTextGreen = 0.78F; // the tips highlight colour (orange)
constexpr float kTipTextBlue = 0.45F;
constexpr float kTipWrapWidth = 300.0F;

} // namespace

namespace {

constexpr float kHalf = 0.5F; // half

} // namespace

Values get_clamped(Values v)
{
    v.fly_speed_ = std::clamp(v.fly_speed_, kFlySpeedMin, kFlySpeedMax);
    v.fly_accel_ms_ = std::clamp(v.fly_accel_ms_, 0, kFlyAccelMax);
    v.look_pct_ = std::clamp(v.look_pct_, kLookMin, kLookMax);
    v.gizmo_pct_ = std::clamp(v.gizmo_pct_, kGizmoMin, kGizmoMax);
    v.ui_scale_pct_ = v.ui_scale_pct_ <= 0 ? 0 : std::clamp(v.ui_scale_pct_, kUiScaleMin, kUiScaleMax);
    v.autosave_seconds_ = std::clamp(v.autosave_seconds_, kAutosaveMin, kAutosaveMax);
    v.snap_move_ = std::clamp(v.snap_move_, 1, kSnapMoveMax);
    v.snap_turn_ = std::clamp(v.snap_turn_, 1, kSnapTurnMax);
    v.accent = std::clamp(v.accent, 0, kAccentCount - 1);
    return v;
}

void get_accent_rgb(int accent, std::span<float, 3> rgb)
{
    static const std::array<std::array<float, 3>, kAccentCount> kRgb = {{
        {0.82F, 0.34F, 0.11F}, // orange (the plan's mockup: the editor's own until now)
        {0.20F, 0.48F, 0.86F}, // blue
        {0.24F, 0.62F, 0.30F}, // green
        {0.58F, 0.36F, 0.84F}, // purple
        {0.80F, 0.22F, 0.26F}, // red
        {0.10F, 0.60F, 0.62F}, // teal
    }};
    const auto& c = core::at(kRgb, std::clamp(accent, 0, kAccentCount - 1));
    core::at(rgb, 0) = core::at(c, 0);
    core::at(rgb, 1) = core::at(c, 1);
    core::at(rgb, 2) = core::at(c, 2);
}

const char* accent_name(int accent)
{
    static const std::array<const char*, kAccentCount> kNames = {{"Orange", "Blue", "Green", "Purple", "Red", "Teal"}};
    return core::at(kNames, std::clamp(accent, 0, kAccentCount - 1));
}

namespace {

// NOT ORIGINAL grouping - level_editor_prefs.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_prefs_state()).
struct LevelEditorPrefsState {
    bool loaded_{};
    Values values_{};
    int style_generation_{};
    bool open_{};
    bool tips_{}; // (the tips window is up)
    bool tips_shown_this_run_{};
};

LevelEditorPrefsState& level_editor_prefs_state()
{
    static core::NoDestroy<LevelEditorPrefsState> state;
    return state.get();
}

// One [editor] key per value: its name, and the value as an int.
struct Key {
    const char* name;
    int Values::* field;
};
constexpr auto kIntKeys = std::to_array<Key>({
    {.name = "fly_speed", .field = &Values::fly_speed_},
    {.name = "fly_accel_ms", .field = &Values::fly_accel_ms_},
    {.name = "look_pct", .field = &Values::look_pct_},
    {.name = "gizmo_pct", .field = &Values::gizmo_pct_},
    {.name = "ui_scale_pct", .field = &Values::ui_scale_pct_},
    {.name = "autosave_s", .field = &Values::autosave_seconds_},
    {.name = "snap_move", .field = &Values::snap_move_},
    {.name = "snap_turn", .field = &Values::snap_turn_},
    {.name = "accent", .field = &Values::accent},
});
struct BitMark {
    const char* name;
    bool Values::* field;
};
constexpr auto kFlags = std::to_array<BitMark>({
    {.name = "invert_y", .field = &Values::invert_y_},
    {.name = "wasd_flies", .field = &Values::wasd_flies_},
    {.name = "speed_by_selection", .field = &Values::speed_by_selection_},
    {.name = "tips_seen", .field = &Values::tips_seen_},
});

void read_in()
{
    Values v;
    for (const Key& k : kIntKeys) {
        v.*k.field = config_int("editor", k.name, v.*k.field);
    }
    for (const BitMark& f : kFlags) {
        v.*f.field = config_int("editor", f.name, v.*f.field ? 1 : 0) != 0;
    }
    level_editor_prefs_state().values_ = get_clamped(v);
    level_editor_prefs_state().loaded_ = true;
}

void store_away(const Values& before, const Values& after)
{
    for (const Key& k : kIntKeys) {
        if (before.*k.field != after.*k.field) {
            config_set_int("editor", k.name, after.*k.field);
        }
    }
    for (const BitMark& f : kFlags) {
        if (before.*f.field != after.*f.field) {
            config_set_int("editor", f.name, after.*f.field ? 1 : 0);
        }
    }
}

void save_all()
{
    for (const Key& k : kIntKeys) {
        config_set_int("editor", k.name, level_editor_prefs_state().values_.*k.field);
    }
    for (const BitMark& f : kFlags) {
        config_set_int("editor", f.name, level_editor_prefs_state().values_.*f.field ? 1 : 0);
    }
}

void get_tip(const char* text)
{
    ImGui::SameLine();
    gui::text_disabled("(?)");
    if (ImGui::IsItemHovered()) {
        gui::tooltip("%s", text);
    }
}

} // namespace

const Values& get_pull()
{
    if (!level_editor_prefs_state().loaded_) {
        read_in();
    }
    return level_editor_prefs_state().values_;
}

void assign_value(const Values& values, bool save)
{
    const Values before = get_pull();
    const Values after = get_clamped(values);
    if (after.ui_scale_pct_ != before.ui_scale_pct_ || after.accent != before.accent) {
        ++level_editor_prefs_state().style_generation_;
    }
    level_editor_prefs_state().values_ = after;
    if (save) {
        store_away(before, after);
    }
}

int style_generation() { return level_editor_prefs_state().style_generation_; }

bool& unseal() { return level_editor_prefs_state().open_; }

void pane(float scale, PaneFonts fonts)
{
    auto* const heading = fonts.heading;
    auto* const small = fonts.small_font_;
    if (!level_editor_prefs_state().open_) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * kHalf, io.DisplaySize.y * kHalf), ImGuiCond_Appearing,
                            ImVec2(kHalf, kHalf));
    ImGui::SetNextWindowSize(ImVec2(kPrefsWidth * scale, 0.0F), ImGuiCond_Appearing);
    if (!ImGui::Begin("Preferences###editorprefs", &level_editor_prefs_state().open_,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    Values v = get_pull();
    bool changed = false;
    // (changes apply at once; the ini is written when a slider is let go, or at a click)
    static bool s_prev_focused;
    bool active = false;
    auto section = [&](const char* title) {
        ImGui::Spacing();
        if (heading) {
            ImGui::PushFont(heading);
        }
        ImGui::TextUnformatted(title);
        if (heading) {
            ImGui::PopFont();
        }
        ImGui::Separator();
    };
    // A slider's value format and its tooltip.
    struct SliderText {
        const char* format{};
        const char* tip{};
    };
    auto slider = [&](const char* label, int* value, int lo, int hi, SliderText slider_text) {
        const auto* const format = slider_text.format;
        const auto* const tip = slider_text.tip;
        ImGui::SetNextItemWidth(kSliderWidth * scale);
        if (ImGui::SliderInt(label, value, lo, hi, format, ImGuiSliderFlags_AlwaysClamp)) {
            changed = true;
        }
        active |= ImGui::IsItemActive();
        if (tip) {
            get_tip(tip);
        }
    };
    auto check = [&](const char* label, bool* value, const char* tip) {
        if (ImGui::Checkbox(label, value)) {
            changed = true;
        }
        if (tip) {
            get_tip(tip);
        }
    };
    section("Camera");
    slider("Fly Speed", &v.fly_speed_, kFlySpeedMin, kFlySpeedMax, {.format = "%d units/s", .tip = "Initial fly speed"});
    slider("Acceleration", &v.fly_accel_ms_, 0, kFlyAccelMax, {.format = "%d ms", .tip = "Time to reach full speed"});
    slider("Mouse Sensitivity", &v.look_pct_, kLookMin, kLookMax, {.format = "%d%%", .tip = "Look and orbit sensitivity"});
    check("Invert Mouse Y", &v.invert_y_, nullptr);
    check("WASD Always Flies", &v.wasd_flies_, "Fly without holding the right mouse button");
    check("Fly Speed from Selection Size", &v.speed_by_selection_, "Frame Selection sets fly speed from its size");
    section("Editing");
    slider("Gizmo Size", &v.gizmo_pct_, kGizmoMin, kGizmoMax, {.format = "%d%%", .tip = nullptr});
    slider("Grid Size", &v.snap_move_, 1, kSnapMoveMax, {.format = "%d units", .tip = nullptr});
    slider("Rotation Snap", &v.snap_turn_, 1, kSnapTurnMax, {.format = "%d degrees", .tip = nullptr});
    slider("Autosave Interval", &v.autosave_seconds_, kAutosaveMin, kAutosaveMax, {.format = "%d s", .tip = nullptr});
    section("Look");
    bool auto_scale = v.ui_scale_pct_ == 0;
    if (ImGui::Checkbox("Automatic UI Scale", &auto_scale)) {
        v.ui_scale_pct_ = auto_scale ? 0 : kAutoScaleDefaultPercent;
        changed = true;
    }
    get_tip("Based on window size (1080p: 100%, 1440p: 125%, 4K: 200%)");
    if (!auto_scale) {
        slider("UI Scale", &v.ui_scale_pct_, kUiScaleMin, kUiScaleMax, {.format = "%d%%", .tip = nullptr});
    }
    ImGui::TextUnformatted("Accent");
    for (int i = 0; i < kAccentCount; ++i) {
        std::array<float, 3> rgb{};
        get_accent_rgb(i, rgb);
        ImGui::SameLine(0.0F, kSwatchGap * scale);
        ImGui::PushID(i);
        const ImVec4 colour(core::at(rgb, 0), core::at(rgb, 1), core::at(rgb, 2), 1.0F);
        if (ImGui::ColorButton(accent_name(i), colour, ImGuiColorEditFlags_NoTooltip,
                               ImVec2((i == v.accent ? kSelectedSwatchSize : kSwatchSize) * scale, kSwatchSize * scale))) {
            v.accent = i;
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("%s", accent_name(i));
        }
        ImGui::PopID();
    }
    ux8::prefs_section(); // (UX8: controller mode - level_editor_ux8.h)
    // The keymap (level_editor_actions.h - UX3's Keys section, [editor_keys] in the ini).
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Keys###prefskeys")) {
        ImGui::BeginChild("##prefskeys", ImVec2(0.0F, kKeysListHeight * scale), ImGuiChildFlags_Borders);
        ux3::keys_section();
        ImGui::EndChild();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (small) {
        ImGui::PushFont(small);
    }
    gui::text_disabled("Saved in thug2-decomp.ini [editor]");
    if (small) {
        ImGui::PopFont();
    }
    if (ImGui::Button("Defaults")) {
        Values d;
        d.tips_seen_ = v.tips_seen_;
        v = d;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Show tips")) {
        show_tips();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        level_editor_prefs_state().open_ = false;
    }
    if (changed || active) {
        assign_value(v, false);
    }
    if ((changed && !active) || (s_prev_focused && !active)) {
        save_all();
    }
    s_prev_focused = active;
    ImGui::End();
}

void show_tips()
{
    level_editor_prefs_state().tips_ = true;
    level_editor_prefs_state().tips_shown_this_run_ = true;
}

void tips_window(float scale, ImFont* heading, ImFont* mono)
{
    // (the first time the editor opens: once a run until dismissed for good)
    if (!level_editor_prefs_state().tips_ && !level_editor_prefs_state().tips_shown_this_run_ && !get_pull().tips_seen_) {
        level_editor_prefs_state().tips_ = true;
        level_editor_prefs_state().tips_shown_this_run_ = true;
    }
    if (!level_editor_prefs_state().tips_) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * kHalf, io.DisplaySize.y * kTipsTop), ImGuiCond_Appearing,
                            ImVec2(kHalf, kHalf));
    ImGui::SetNextWindowSize(ImVec2(kTipsWidth * scale, 0.0F), ImGuiCond_Appearing);
    bool open = true;
    ImGui::Begin("Level Editor###editortips", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    if (heading) {
        ImGui::PushFont(heading);
    }
    ImGui::TextUnformatted("Essential Keys");
    if (heading) {
        ImGui::PopFont();
    }
    ImGui::Spacing();
    static constexpr auto kTips = std::to_array<std::array<const char*, 2>>({
        {"Right mouse + W A S D", "Fly (Q / E: down / up, Shift: faster)"},
        {"Click, drag", "Select and move (Shift+click: add, Esc: deselect)"},
        {"F", "Frame selection (Alt+drag: orbit)"},
        {"F5", "Play from here; F5 again returns"},
        {"F1", "Keyboard shortcuts (Ctrl+S: save)"},
    });
    if (ImGui::BeginTable("##tips", 2, ImGuiTableFlags_SizingFixedFit)) {
        for (const auto& tip : kTips) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (mono) {
                ImGui::PushFont(mono);
            }
            gui::text_colored(ImVec4(1.0F, kTipTextGreen, kTipTextBlue, 1.0F), "%s", core::at(tip, 0));
            if (mono) {
                ImGui::PopFont();
            }
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (kTipWrapWidth * scale));
            ImGui::TextUnformatted(core::at(tip, 1));
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    const bool dismiss = ImGui::Button("Got it");
    ImGui::SameLine();
    gui::text_disabled("(Help > Show Tips shows this again)");
    ImGui::End();
    if (dismiss || !open) {
        level_editor_prefs_state().tips_ = false;
        if (!get_pull().tips_seen_) {
            Values v = get_pull();
            v.tips_seen_ = true;
            assign_value(v);
        }
    }
}

} // namespace frontend::level_editor::prefs

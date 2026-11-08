// NOT ORIGINAL - the level editor's rotation tools (ours). See level_editor_rotate.h.

#include "editor/level_editor_rotate.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_ui_theme.h"
#include "imgui.h"

namespace frontend::level_editor::rotate_tool {

namespace {

constexpr float kHalf = 0.5F;
constexpr double kDegreesPerRadian = 57.295779513082321;
constexpr float kNearlyUp = 0.9F;              // an axis this much along up takes x for its first square direction
constexpr std::size_t kReadoutSize = 32;       // "-123.4°"
constexpr float kDialogWidth = 360.0F;         // the question's width (unscaled)
constexpr float kDialogTop = 60.0F;            // and its distance from the top
constexpr float kFullTurn = 6.2831853F;        // a ring's whole turn

struct RotateState {
    int frame_level_ = -1;
    int revert_ = -1;
    int vertex_snap_ = -1;
    int modal_rotate_ = -1;
    bool asking_ = false;
    bool confirmed_ = false;
    bool copy_drag_ = false;
    std::size_t copy_depth_ = 0;
};

RotateState& rotate_state()
{
    static core::NoDestroy<RotateState> state;
    return state.get();
}

actions::ActStep make(const char* id, const char* name, const char* category, actions::Kind kind,
                      const std::vector<actions::Chord>& keys, const char* tip)
{
    actions::ActStep act_step;
    act_step.id = id;
    act_step.name = name;
    act_step.category = category;
    act_step.kind = kind;
    act_step.defaults = keys;
    act_step.keys = keys;
    act_step.tip = tip;
    return act_step;
}

// A hint (a gesture or a key of a mode - listed, not rebindable).
struct Hint {
    const char* id;
    const char* name;
    const char* keys;
    const char* tip;
};
void hint(const Hint& hint_text)
{
    actions::ActStep act_step =
        make(hint_text.id, hint_text.name, "Transform", actions::Kind::kDetailsOf, {}, hint_text.tip);
    act_step.keys_text_ = hint_text.keys;
    act_step.rebindable = false;
    actions::enrol(act_step);
}

} // namespace

// --- the pure parts ---------------------------------------------------------------------------------------------

V3 turn_about_axis(V3 v, V3 axis, float radians)
{
    // Rodrigues: v cos + (k x v) sin + k (k . v) (1 - cos)
    const double cosine = std::cos(static_cast<double>(radians));
    const double sine = std::sin(static_cast<double>(radians));
    const V3 cross = {.x = (axis.y * v.z) - (axis.z * v.y), .y = (axis.z * v.x) - (axis.x * v.z), .z = (axis.x * v.y) - (axis.y * v.x)};
    const double along = get_dot(axis, v) * (1.0 - cosine);
    return {.x = static_cast<float>((v.x * cosine) + (cross.x * sine) + (axis.x * along)),
            .y = static_cast<float>((v.y * cosine) + (cross.y * sine) + (axis.y * along)),
            .z = static_cast<float>((v.z * cosine) + (cross.z * sine) + (axis.z * along))};
}

Square square_to(V3 axis)
{
    // (for up: u = +z, v = up x z = +x - turn_around_axis's atan2(x, z))
    const V3 seed = std::fabs(axis.y) > kNearlyUp ? V3{.x = 0.0F, .y = 0.0F, .z = 1.0F} : V3{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const V3 first = make_unit(seed - axis * get_dot(seed, axis));
    return {.u = first,
            .v = {.x = (axis.y * first.z) - (axis.z * first.y), .y = (axis.z * first.x) - (axis.x * first.z),
                  .z = (axis.x * first.y) - (axis.y * first.x)}};
}

float angle_about(Ray axis, V3 point)
{
    const Square square = square_to(axis.dir);
    const V3 offset = point - axis.origin;
    return static_cast<float>(
        std::atan2(static_cast<double>(get_dot(offset, square.v)), static_cast<double>(get_dot(offset, square.u))));
}

std::vector<V3> ring_about(Ray axis, xform::RingShape ring)
{
    const Square square = square_to(axis.dir);
    std::vector<V3> points;
    points.reserve(static_cast<std::size_t>(std::max(ring.count, 0)));
    for (int i = 0; i < ring.count; ++i) {
        const float angle = kFullTurn * static_cast<float>(i) / static_cast<float>(ring.count);
        points.push_back(axis.origin + (square.u * (std::cos(angle) * ring.radius)) +
                         (square.v * (std::sin(angle) * ring.radius)));
    }
    return points;
}

V3 euler_of(std::span<const V3, 3> rows)
{
    // at = (cos x sin y, -sin x, cos x cos y); right seen through (Ry Rx)^-1 is (cos z, sin z, 0)
    const V3 at = core::at(rows, 2);
    const double pitch = std::asin(std::clamp(static_cast<double>(-at.y), -1.0, 1.0));
    const double heading = std::atan2(static_cast<double>(at.x), static_cast<double>(at.z));
    const V3 right = core::at(rows, 0);
    // (Ry Rx)^-1 = Rx^-1 Ry^-1: undo the heading, then the pitch
    const V3 unturned = turn_about_up(right, static_cast<float>(-heading));
    const double cp = std::cos(-pitch);
    const double sp = std::sin(-pitch);
    const double y = (unturned.y * cp) - (unturned.z * sp);
    const double roll = std::atan2(y, static_cast<double>(unturned.x));
    return {.x = static_cast<float>(pitch * kDegreesPerRadian), .y = static_cast<float>(heading * kDegreesPerRadian),
            .z = static_cast<float>(roll * kDegreesPerRadian)};
}

void rows_of(V3 degrees, std::span<V3, 3> rows)
{
    const auto x = static_cast<float>(degrees.x / kDegreesPerRadian);
    const auto y = static_cast<float>(degrees.y / kDegreesPerRadian);
    const auto z = static_cast<float>(degrees.z / kDegreesPerRadian);
    static constexpr std::array<V3, 3> kAxes = {
        {{.x = 1.0F, .y = 0.0F, .z = 0.0F}, {.x = 0.0F, .y = 1.0F, .z = 0.0F}, {.x = 0.0F, .y = 0.0F, .z = 1.0F}}};
    for (int i = 0; i < 3; ++i) {
        // Rz, then Rx, then Ry
        const V3 rolled = turn_about_axis(core::at(kAxes, i), core::at(kAxes, 2), z);
        const V3 pitched = turn_about_axis(rolled, core::at(kAxes, 0), x);
        core::at(rows, i) = turn_about_up(pitched, y);
    }
}

// --- the game side ----------------------------------------------------------------------------------------------

void prepare_all()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    RotateState& state = rotate_state();
    state.frame_level_ = actions::enrol(make("frame_level", "Frame Level", "Camera", actions::Kind::kPress,
                                             {{.scancode = SDL_SCANCODE_HOME, .mods = 0}}, "Show the whole level"));
    // (Revert to Original is the built-in "home" - its key now none, the Edit menu's: asked first)
    actions::set_run(actions::kHome, [] { ask_revert(); });
    state.revert_ = actions::kHome;
    state.vertex_snap_ = actions::enrol(make("vertex_snap", "Vertex Snap", "Transform", actions::Kind::kHold,
                                             {{.scancode = SDL_SCANCODE_V, .mods = 0}},
                                             "Hold while moving: the nearest corner snaps to the one under the cursor"));
    state.modal_rotate_ = actions::enrol(make("modal_rotate", "Rotate (Modal)", "Transform", actions::Kind::kPress,
                                              {{.scancode = SDL_SCANCODE_R, .mods = actions::kAlt}},
                                              "Rotate without holding the mouse; X / Y / Z picks the axis"));
    hint({.id = "hint_fine_turn", .name = "Turn 1 Degree", .keys = "Shift+Z / Shift+X",
          .tip = "Turn by one degree instead of the snap"});
    hint({.id = "hint_alt_duplicate", .name = "Duplicate and Move", .keys = "Alt+drag an arrow",
          .tip = "Move a copy, leave the original"});
    hint({.id = "hint_rotate_rings", .name = "Rotate Rings", .keys = "Drag a ring",
          .tip = "X / Y / Z rings, the outer ring turns about the view, inside turns freely"});
}

int frame_level_action() { return rotate_state().frame_level_; }
int revert_action() { return rotate_state().revert_; }
int vertex_snap_action() { return rotate_state().vertex_snap_; }
int modal_rotate_action() { return rotate_state().modal_rotate_; }

void ask_revert() { rotate_state().asking_ = true; }

bool asking() { return rotate_state().asking_; }

bool take_revert() { return std::exchange(rotate_state().confirmed_, false); }

void dialogs(float scale)
{
    RotateState& state = rotate_state();
    if (!state.asking_) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * kHalf, kDialogTop * scale), ImGuiCond_Always, ImVec2(kHalf, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(kDialogWidth * scale, 0.0F));
    ImGui::Begin("Revert to Original###confirmrevert", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    gui::text_wrapped("Put the selection back as the level has it?");
    gui::text_disabled("Undo brings your changes back.");
    ImGui::Spacing();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        state.asking_ = false;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ui_theme::kDangerButtonColour);
    if (ImGui::Button("Revert")) {
        state.asking_ = false;
        state.confirmed_ = true;
    }
    ImGui::PopStyleColor();
    ImGui::End();
}

void note_copy_drag(std::size_t depth)
{
    rotate_state().copy_drag_ = true;
    rotate_state().copy_depth_ = depth;
}

bool take_copy_drag(std::size_t* depth)
{
    *depth = rotate_state().copy_depth_;
    return std::exchange(rotate_state().copy_drag_, false);
}

std::string readout(float radians)
{
    std::array<char, kReadoutSize> text{};
    text::format_to(text, "%+.1f\xC2\xB0", static_cast<double>(radians) * kDegreesPerRadian);
    return text.data();
}

} // namespace frontend::level_editor::rotate_tool

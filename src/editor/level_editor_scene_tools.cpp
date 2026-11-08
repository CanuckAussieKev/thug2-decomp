// NOT ORIGINAL - the Scene panel's tool strip (ours). See level_editor_scene_tools.h.

#include "editor/level_editor_scene_tools.h"

#include <array>
#include <cstdint>
#include <cmath>
#include <string>

#include "base/core/checked_index.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_hints.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_ui_internal.h"
#include "imgui.h"

namespace frontend::level_editor::scene_tools {

namespace {

constexpr float kButton = 26.0F;  // a button's side
constexpr float kGap = 2.0F;      // between buttons
constexpr float kGroupGap = 6.0F; // between the tools and World / Local
constexpr float kPad = 3.0F;      // the strip's edge round its buttons
constexpr float kRounding = 4.0F;
constexpr float kActiveAlpha = 0.9F;
constexpr ImU32 kStripColour = IM_COL32(20, 21, 24, 170);
constexpr ImU32 kHoverColour = IM_COL32(60, 62, 70, 230);
// The icons (sizes in pixels at scale 1; shares of half an icon's side).
constexpr float kLine = 1.6F;     // their lines
constexpr float kThinShare = 0.8F; // thinner lines, and an arrow head's half width against its length
constexpr float kIconHalf = 7.0F; // half an icon's side
constexpr float kArrowHead = 3.0F;
constexpr float kPi = 3.14159265F;
constexpr int kArcSegments = 20;
constexpr float kRingShare = 0.85F;          // the turning arrow's circle
constexpr float kRingFrom = -0.35F * kPi;    // its arc (radians, clockwise on the screen)
constexpr float kRingTo = 1.25F * kPi;
constexpr float kRingHeadAhead = 0.6F;       // its head beyond the arc's end (of a head's length)
constexpr float kRingHeadShare = 1.3F;       // its head's length (of a head)
constexpr float kInnerBoxShare = 0.1F;       // the scale icon's small box: from its corner to this near the middle
constexpr float kPullHeadShare = 1.2F;       // the scale icon's arrow head (of a head)
constexpr float kDiagonal = 0.70710678F;     // (a unit step along a diagonal)
constexpr float kMeridianShare = 0.45F;      // the globe's meridian, its half width
constexpr float kBoxSideShare = 0.87F;       // the box seen from a corner: its sides' x, its corners' y
constexpr float kBoxCornerShare = 0.5F;
constexpr int kBoxRim = 6;
constexpr std::array<int, 3> kBoxInnerEdges = {{1, 3, 5}}; // (the rim's corners its middle joins)

enum class Icon : uint8_t { kMove, kRotate, kScale, kWorld, kLocal };

struct Button {
    const char* id;     // the action's id (its key shown in the tooltip)
    const char* name;   // the tooltip's first line
    const char* detail; // its second line
};

// What an icon is drawn with: the list, the UI's scale and the colour.
struct Pen {
    ImDrawList* list;
    float scale;
    ImU32 colour;
};

// A direction on the screen (a unit step).
struct Step {
    float x;
    float y;
};

// The action's key as the registry shows it ("" none).
std::string key_of(const char* id)
{
    const int index = actions::get_find(id);
    return index >= 0 ? hints::leading_key_name(actions::get_keys_text(index)) : std::string();
}

// An arrow's head with its tip at `tip`, pointing along `along`, `length` long.
void arrow_head(const Pen& pen, ImVec2 tip, Step along, float length)
{
    const ImVec2 back(tip.x - (along.x * length), tip.y - (along.y * length));
    const ImVec2 side(-along.y * length * kThinShare, along.x * length * kThinShare);
    pen.list->AddTriangleFilled(tip, ImVec2(back.x + side.x, back.y + side.y), ImVec2(back.x - side.x, back.y - side.y),
                                pen.colour);
}

void draw_icon(const Pen& pen, Icon icon, ImVec2 centre)
{
    ImDrawList* list = pen.list;
    const ImU32 colour = pen.colour;
    const float half = kIconHalf * pen.scale;
    const float line = kLine * pen.scale;
    const float thin = line * kThinShare;
    const float head = kArrowHead * pen.scale;
    switch (icon) {
    case Icon::kMove: { // (four arrows out from the middle)
        list->AddLine(ImVec2(centre.x - half + head, centre.y), ImVec2(centre.x + half - head, centre.y), colour, line);
        list->AddLine(ImVec2(centre.x, centre.y - half + head), ImVec2(centre.x, centre.y + half - head), colour, line);
        arrow_head(pen, ImVec2(centre.x + half, centre.y), {.x = 1.0F, .y = 0.0F}, head);
        arrow_head(pen, ImVec2(centre.x - half, centre.y), {.x = -1.0F, .y = 0.0F}, head);
        arrow_head(pen, ImVec2(centre.x, centre.y - half), {.x = 0.0F, .y = -1.0F}, head);
        arrow_head(pen, ImVec2(centre.x, centre.y + half), {.x = 0.0F, .y = 1.0F}, head);
        break;
    }
    case Icon::kRotate: { // (most of a circle, an arrow at its end going on round)
        const float radius = half * kRingShare;
        list->PathArcTo(centre, radius, kRingFrom, kRingTo, kArcSegments);
        list->PathStroke(colour, ImDrawFlags_None, line);
        const Step onward = {.x = -std::sin(kRingTo), .y = std::cos(kRingTo)};
        const ImVec2 end(centre.x + (std::cos(kRingTo) * radius), centre.y + (std::sin(kRingTo) * radius));
        const float ahead = head * kRingHeadAhead;
        arrow_head(pen, ImVec2(end.x + (onward.x * ahead), end.y + (onward.y * ahead)), onward, head * kRingHeadShare);
        break;
    }
    case Icon::kScale: { // (a box, a small one in its corner, an arrow pulling out the far corner)
        const float inner = half * kInnerBoxShare;
        list->AddRect(ImVec2(centre.x - half, centre.y - half), ImVec2(centre.x + half, centre.y + half), colour, 0.0F, 0,
                      thin);
        list->AddRectFilled(ImVec2(centre.x - half, centre.y + inner), ImVec2(centre.x - inner, centre.y + half), colour);
        list->AddLine(ImVec2(centre.x - inner, centre.y + inner), ImVec2(centre.x + half - head, centre.y - half + head),
                      colour, line);
        arrow_head(pen, ImVec2(centre.x + half, centre.y - half), {.x = kDiagonal, .y = -kDiagonal}, head * kPullHeadShare);
        break;
    }
    case Icon::kWorld: { // (a globe: its outline, the equator, a meridian)
        list->AddCircle(centre, half, colour, kArcSegments, line);
        list->AddLine(ImVec2(centre.x - half, centre.y), ImVec2(centre.x + half, centre.y), colour, thin);
        list->AddEllipse(centre, ImVec2(half * kMeridianShare, half), colour, 0.0F, kArcSegments, thin);
        break;
    }
    case Icon::kLocal: { // (a box seen from a corner: its own axes)
        const float side_x = half * kBoxSideShare;
        const float corner_y = half * kBoxCornerShare;
        const std::array<ImVec2, kBoxRim> rim = {
            {ImVec2(centre.x, centre.y - half), ImVec2(centre.x + side_x, centre.y - corner_y),
             ImVec2(centre.x + side_x, centre.y + corner_y), ImVec2(centre.x, centre.y + half),
             ImVec2(centre.x - side_x, centre.y + corner_y), ImVec2(centre.x - side_x, centre.y - corner_y)}};
        list->AddPolyline(rim.data(), kBoxRim, colour, ImDrawFlags_Closed, line);
        for (const int corner : kBoxInnerEdges) {
            list->AddLine(centre, core::at(rim, corner), colour, thin);
        }
        break;
    }
    }
}

// One button: its square, lit when `on`, its icon, its tooltip. Whether it was clicked.
bool tool_button(const Button& button, Icon icon, bool on, ImVec2 at, float scale)
{
    const float side = kButton * scale;
    ImGui::SetCursorScreenPos(at);
    const bool clicked = ImGui::InvisibleButton(button.id, ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 to(at.x + side, at.y + side);
    if (on) {
        list->AddRectFilled(at, to, ImGui::GetColorU32(ui::internal::get_accent(kActiveAlpha)), kRounding * scale);
    } else if (hovered) {
        list->AddRectFilled(at, to, kHoverColour, kRounding * scale);
    }
    const float middle = side / 2;
    draw_icon({.list = list, .scale = scale, .colour = ImGui::GetColorU32(ImGuiCol_Text)}, icon,
              ImVec2(at.x + middle, at.y + middle));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        const std::string key = key_of(button.id);
        ImGui::BeginTooltip();
        if (key.empty()) {
            ImGui::TextUnformatted(button.name);
        } else {
            gui::text("%s  (%s)", button.name, key.c_str());
        }
        ui::internal::get_muted(button.detail);
        ImGui::EndTooltip();
    }
    return clicked;
}

} // namespace

bool draw(float x, float y, float scale)
{
    static constexpr std::array<Button, 3> kTools = {{
        {.id = "tool_move", .name = "Move", .detail = "Drag the selection, or an arrow of the gizmo to move along it"},
        {.id = "tool_rotate",
         .name = "Rotate",
         .detail = "Drag a ring to turn about it. Objects: X, Y and Z rings, the outer ring turns about the view,\n"
                   "drag inside the rings to turn freely"},
        {.id = "tool_scale", .name = "Scale", .detail = "Drag the gizmo's handles to scale the selection"},
    }};
    static constexpr std::array<Icon, 3> kIcons = {{Icon::kMove, Icon::kRotate, Icon::kScale}};
    xform::Settings& settings = xform::at_hand();
    const float side = kButton * scale;
    const float gap = kGap * scale;
    const float pad = kPad * scale;
    const float height = (pad * 2) + (side * 4) + (gap * 2) + (kGroupGap * scale);
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(ImVec2(x, y), ImVec2(x + side + (pad * 2), y + height), kStripColour, kRounding * scale);
    ImGui::PushID("scene_tools");
    float at_y = y + pad;
    for (int slot = 0; slot < 3; ++slot) {
        const auto mode = static_cast<xform::Mode>(slot);
        if (tool_button(core::at(kTools, slot), core::at(kIcons, slot), settings.mode == mode, ImVec2(x + pad, at_y), scale)) {
            settings.mode = mode;
        }
        at_y += side + gap;
    }
    at_y += (kGroupGap * scale) - gap;
    const bool local = settings.space == xform::Space::kLocal;
    const Button space = {.id = "tool_space",
                          .name = local ? "Local axes" : "World axes",
                          .detail = local ? "The gizmo follows the selection's own axes - click for the world's"
                                          : "The gizmo follows the world's axes - click for the selection's own"};
    if (tool_button(space, local ? Icon::kLocal : Icon::kWorld, false, ImVec2(x + pad, at_y), scale)) {
        settings.space = local ? xform::Space::kWorld : xform::Space::kLocal;
    }
    ImGui::PopID();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    return mouse.x >= x && mouse.x < x + side + (pad * 2) && mouse.y >= y && mouse.y < y + height;
}

} // namespace frontend::level_editor::scene_tools

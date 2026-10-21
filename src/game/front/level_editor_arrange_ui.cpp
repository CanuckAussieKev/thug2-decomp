// NOT ORIGINAL - the level editor's arrange tools and adjust-last panel on the UI (ours; see level_editor_arrange.h and
// level_editor_adjust.h). Dear ImGui only (the renderer-independence rules: level_editor_ui_backend.h); what the
// buttons ask for is queued and done in the editor's next Update (Arrange::Update runs the queue).

#include <cstdio>
#include <functional>
#include <vector>

#include "game/front/level_editor_adjust.h"
#include "game/front/level_editor_arrange.h"
#include "game/front/level_editor_links.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_transform.h"
#include "imgui.h"

namespace Front::LevelEditor::Arrange {

namespace {

std::vector<std::function<void()>> g_queue;
void Later(std::function<void()> action) { g_queue.push_back(std::move(action)); }

void Tip(const char* text)
{
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

} // namespace

// (called from Update: what the window asked for)
void RunQueued()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
}

void Window(float uiScale)
{
    Settings& s = Current();
    if (!s.window)
        return;
    ImGui::SetNextWindowSize(ImVec2(330.0f * uiScale, 0.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(420.0f * uiScale, 120.0f * uiScale), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Arrange###arrange", &s.window, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Align / distribute");
    static const char* const kAxes[] = {"X", "Y", "Z"};
    for (int axis = 0; axis < 3; ++axis) {
        if (axis)
            ImGui::SameLine();
        ImGui::RadioButton(kAxes[axis], &s.axis, axis);
    }
    ImGui::SameLine(0.0f, 14.0f * uiScale);
    ImGui::Checkbox("Local", &s.local);
    Tip("Along the first piece's own axes (an object's turn, scenery's quarter turns) - else the world's");
    int reference = static_cast<int>(s.reference);
    ImGui::RadioButton("On the selection's bounds", &reference, 0);
    ImGui::SameLine();
    ImGui::RadioButton("On the first", &reference, 1);
    s.reference = static_cast<Reference>(reference);
    static const char* const kEdges[] = {"Min", "Centre", "Max"};
    for (int edge = 0; edge < 3; ++edge) {
        if (edge)
            ImGui::SameLine();
        char label[32];
        std::snprintf(label, sizeof(label), "Align %s", kEdges[edge]);
        if (ImGui::Button(label)) {
            int axis = s.axis;
            Later([axis, edge] {
                Align(axis, static_cast<Edge>(edge), Current().reference, Current().local);
            });
        }
    }
    if (ImGui::Button("Distribute")) {
        int axis = s.axis;
        Later([axis] { Distribute(axis, Current().gaps, Current().local); });
    }
    Tip("Three or more pieces spread along the axis: the first and last stay");
    ImGui::SameLine();
    ImGui::Checkbox("Equal gaps", &s.gaps);
    Tip("Off: their centres evenly apart; on: the gaps between them equal");

    ImGui::SeparatorText("Replace");
    if (ImGui::Button("Replace selection with the Add palette's item"))
        Later([] { Replace(); });
    Tip("Open the Add palette (B), highlight a piece (scenery selected) or an object (objects selected): each selected "
        "one is replaced, keeping its place, turn and scale. A level's own object gets the object's model");

    ImGui::SeparatorText("Linked copies");
    if (ImGui::Button("Duplicate linked (Ctrl+Alt+D)"))
        Later([] { Links::LinkedDuplicate(); });
    ImGui::SameLine();
    if (ImGui::Button("Unlink"))
        Later([] { Links::UnlinkSelection(); });
    std::string linked = Links::Describe();
    ImGui::TextDisabled("%s", linked.empty() ? "(the selection isn't linked)" : linked.c_str());

    ImGui::SeparatorText("Rail along an edge");
    bool picking = EdgePicking();
    if (ImGui::Checkbox("Pick a top edge (F7)", &picking))
        Later([picking] { SetEdgePicking(picking); });
    Tip("A scenery piece selected: its top outline drawn cyan - click one edge to lay a grind rail along it");

    ImGui::SeparatorText("While moving and placing");
    ImGui::Checkbox("Edge-to-edge snapping (scenery)", &s.edgeSnap);
    Tip("A moved piece's box faces snap flush to a neighbour's within a few units (green: the faces met). Ctrl / Alt "
        "snapping held instead: off for that drag");
    ImGui::Checkbox("Measurements while dragging", &s.measure);
    ImGui::Checkbox("Placement ghost", &s.ghost);
    ImGui::Checkbox("Click in the view places the Add palette's item", &s.clickPlaces);
    ImGui::Checkbox("Ghost / placed object turned to the slope", &s.ghostSlope);
    ImGui::End();
}

void BrowserExtras(float uiScale)
{
    (void)uiScale;
    if (ImGui::SmallButton("Replace selection"))
        Later([] { Replace(); });
    Tip("The selected pieces / object copies replaced by this one, keeping their places, turns and scales");
    ImGui::SameLine();
    ImGui::Checkbox("Click places", &Current().clickPlaces);
    Tip("A click in the view places it where the ghost shows (Shift+click: more)");
}

} // namespace Front::LevelEditor::Arrange

namespace Front::LevelEditor::Adjust {

void Panel(float x, float y, float uiScale)
{
    const Action& last = Last();
    if (!Shown() || last.kind == Kind::None)
        return;
    static Action edit;
    static bool editing;
    if (!editing)
        edit = last;
    bool adjustable = Adjustable();
    ImGui::SetNextWindowPos(ImVec2(x + 8.0f * uiScale, y - 8.0f * uiScale), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(280.0f * uiScale, 0.0f));
    char title[64];
    std::snprintf(title, sizeof(title), "Adjust: %s (F6)###adjust_last", Title(last.kind));
    if (!ImGui::Begin(title, nullptr,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        editing = false;
        return;
    }
    ImGui::BeginDisabled(!adjustable);
    bool live = false, done = false;
    auto track = [&](bool changed) {
        live = live || changed;
        done = done || ImGui::IsItemDeactivatedAfterEdit();
    };
    float speed = 1.0f;
    switch (edit.kind) {
    case Kind::Move:
        track(ImGui::DragFloat3("Offset", &edit.offset.x, speed, -100000.0f, 100000.0f, "%.1f"));
        track(ImGui::Checkbox("On the grid", &edit.grid));
        break;
    case Kind::Turn:
        track(ImGui::DragFloat("Angle", &edit.degrees, edit.quarters ? 3.0f : 0.5f, -3600.0f, 3600.0f,
                               edit.quarters ? "%.0f (quarters)" : "%.1f"));
        break;
    case Kind::Scale:
        if (edit.uniform)
            track(ImGui::DragFloat("Factor", &edit.factor.x, 0.01f, 0.05f, 20.0f, "%.3f"));
        else
            track(ImGui::DragFloat3("Factor", &edit.factor.x, 0.01f, 0.05f, 20.0f, "%.3f"));
        break;
    case Kind::Duplicate:
        // (copies are made only when a field is let go: dozens of copies each frame would be slow)
        ImGui::DragInt("Copies", &edit.count, 0.2f, 1, kMaxCopies);
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat3("Offset", &edit.offset.x, 1.0f, -100000.0f, 100000.0f, "%.1f");
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Turn", &edit.degrees, edit.quarters ? 3.0f : 0.5f, -360.0f, 360.0f,
                         edit.quarters ? "%.0f (quarters)" : "%.1f");
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("On the grid", &edit.grid))
            done = true;
        break;
    case Kind::Align:
    case Kind::Distribute: {
        static const char* const kAxes[] = {"X", "Y", "Z"};
        for (int axis = 0; axis < 3; ++axis) {
            if (axis)
                ImGui::SameLine();
            track(ImGui::RadioButton(kAxes[axis], &edit.axis, axis));
        }
        if (edit.kind == Kind::Align) {
            static const char* const kEdges[] = {"Min", "Centre", "Max"};
            for (int e = 0; e < 3; ++e) {
                if (e)
                    ImGui::SameLine();
                track(ImGui::RadioButton(kEdges[e], &edit.edge, e));
            }
            track(ImGui::RadioButton("Bounds", &edit.reference, 0));
            ImGui::SameLine();
            track(ImGui::RadioButton("First", &edit.reference, 1));
        } else {
            track(ImGui::Checkbox("Equal gaps", &edit.gaps));
        }
        track(ImGui::Checkbox("Local", &edit.local));
        break;
    }
    case Kind::Place:
        track(ImGui::DragFloat3("Position", &edit.pos.x, 1.0f, -100000.0f, 100000.0f, "%.1f"));
        track(ImGui::DragFloat("Turn", &edit.degrees, edit.quarters ? 3.0f : 0.5f, -360.0f, 360.0f,
                               edit.quarters ? "%.0f (quarters)" : "%.1f"));
        break;
    case Kind::None:
        break;
    }
    ImGui::EndDisabled();
    editing = ImGui::IsAnyItemActive() && ImGui::IsWindowFocused();
    if (!adjustable)
        ImGui::TextDisabled("Changed since - only the newest action adjusts");
    else if (edit.kind == Kind::Duplicate)
        ImGui::TextDisabled("Let go of a field to make the copies again");
    ImGui::End();
    if (adjustable && (live || done))
        Request(edit, done);
}

} // namespace Front::LevelEditor::Adjust

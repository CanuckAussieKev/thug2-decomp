// NOT ORIGINAL - the level editor's UX9 tools: the glue and the Ground tool's form (ours). See level_editor_ux9.h.

#include "game/front/level_editor_ux9.h"

#include <SDL2/SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_gap_wizard.h"
#include "game/front/level_editor_ground.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_skate_view.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "hook/game.h"
#include "game/sys/log.h"
#include "imgui.h"

namespace Front::LevelEditor::Ux9 {

namespace {

int g_gapAction = -1, g_skateAction = -1, g_groundAction = -1;

// --- the Ground tool ---

enum class Stage { Off, First, Second, Form };
Stage g_stage = Stage::Off;
bool g_editing;          // (the form edits a piece that is there - g_draft.name)
Ground::Spec g_draft;
float g_grid = 50.0f;
bool g_haveHover;
V3 g_hover, g_first;
uint32_t g_floor;        // (the piece the first corner was clicked on: the material by default)
std::string g_formMessage;
std::vector<std::function<void()>> g_queue;

V3 Snapped(V3 p) { return {Ground::SnapTo(p.x, g_grid), p.y, Ground::SnapTo(p.z, g_grid)}; }

// The collision ray: the point and the piece hit (0: none / a movable).
bool Cast(V3 from, V3 to, V3* hit, uint32_t* sector)
{
    alignas(16) uint8_t feeler[0x100] = {};
    auto* ray = reinterpret_cast<Nx::CFeeler*>(feeler);
    ray->Construct();
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!ray->GetCollisionAlong(&start, &end, false))
        return false;
    const float* p = reinterpret_cast<const float*>(feeler + 0x34);
    *hit = {p[0], p[1], p[2]};
    *sector = *reinterpret_cast<void* const*>(feeler + 0x54) ? *reinterpret_cast<const uint32_t*>(feeler + 0x58) : 0;
    return true;
}

// The ground's height under a point (cast down from 300 above; `fallback` when nothing).
float GroundAt(V3 p, float fallback)
{
    V3 hit;
    uint32_t sector = 0;
    if (Cast(p + V3{0.0f, 300.0f, 0.0f}, p - V3{0.0f, 3000.0f, 0.0f}, &hit, &sector) && !Ground::IsPiece(sector))
        return hit.y;
    return fallback;
}

void SetStage(Stage stage)
{
    g_stage = stage;
    g_haveHover = false;
    if (stage == Stage::First)
        ShowMessage("Ground Tool: click the first corner",
                    6.0);
}

void OpenForm(const Ground::Spec& spec, bool editing)
{
    g_draft = spec;
    g_editing = editing;
    g_formMessage.clear();
    g_stage = Stage::Form;
}

void Apply()
{
    std::string why;
    Ground::Spec spec = g_draft;
    if (!g_editing && spec.name.empty())
        spec.name = Ground::FreshName(Ground::All());
    if (!Ground::Put(spec, &why)) {
        g_formMessage = "Not made: " + why;
        ShowMessage(g_formMessage, 8.0);
        return;
    }
    if (const Ground::Spec* made = Ground::Find(spec.name))
        g_draft = *made;
    g_editing = true;
    g_formMessage = "Applied";
    ShowMessage(std::string(spec.lower ? "Lowered" : "Raised") + " ground: " + spec.name, 5.0);
}

const char* RampName(Ground::Ramp ramp)
{
    return ramp == Ground::Ramp::Kicker ? "Kicker" : ramp == Ground::Ramp::Quarter ? "Quarter pipe" : "None (wall)";
}

void GroundForm(float viewX, float viewY, float viewWidth, float scale)
{
    if (g_stage != Stage::Form)
        return;
    ImGui::SetNextWindowPos(ImVec2(viewX + viewWidth - 12.0f * scale, viewY + 60.0f * scale), ImGuiCond_Appearing,
                            ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(340.0f * scale, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    std::string title = (g_editing ? "Ground piece " + g_draft.name : std::string("Raise / lower ground")) +
                        "###groundForm";
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open)
            g_queue.push_back([] { SetStage(Stage::Off); });
        return;
    }
    Ground::Spec& d = g_draft;
    int mode = d.lower ? 1 : 0;
    ImGui::RadioButton("Raise", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Lower", &mode, 1);
    d.lower = mode == 1;
    ImGui::DragFloat(d.lower ? "Depth" : "Height", &d.height, 1.0f, Ground::kMinHeight, Ground::kMaxHeight, "%.0f");
    float rect[4] = {d.x0, d.z0, d.x1, d.z1};
    if (ImGui::DragFloat4("x0 z0 x1 z1", rect, 1.0f, 0.0f, 0.0f, "%.0f")) {
        d.x0 = rect[0], d.z0 = rect[1], d.x1 = rect[2], d.z1 = rect[3];
    }
    ImGui::DragFloat("Ground at", &d.base, 1.0f, 0.0f, 0.0f, "%.1f");
    static const char* kSides[4] = {"-X side", "+X side", "-Z side", "+Z side"};
    for (int side = 0; side < 4; ++side) {
        ImGui::PushID(side);
        if (ImGui::BeginCombo(kSides[side], RampName(d.ramps[side]))) {
            for (int r = 0; r < 3; ++r) {
                auto ramp = static_cast<Ground::Ramp>(r);
                if (ImGui::Selectable(RampName(ramp), d.ramps[side] == ramp))
                    d.ramps[side] = ramp;
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    ImGui::DragFloat("Ramp length", &d.length, 1.0f, 0.0f, Ground::kMaxLength, d.length > 0.0f ? "%.0f" : "auto");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("0: automatic");
    ImGui::DragFloat("Grid", &g_grid, 1.0f, 0.0f, 500.0f, g_grid > 0.0f ? "%.0f" : "off");
    ImGui::TextDisabled("Material: %s", d.material ? Objects::Label(d.material).c_str() : "(automatic)");
    if (ImGui::Button("Use Selected Material"))
        g_queue.push_back([] {
            const std::vector<uint32_t>& selection = Scenery::Selection();
            std::string why;
            Ground::Spec spec = g_draft;
            if (selection.empty())
                g_formMessage = "Select a scenery piece to copy its material";
            else if (const char* no = Ground::WhyNotDonor(selection[0]))
                g_formMessage = std::string("Can't use this material: ") + no;
            else if (Ground::ChooseMaterial(selection[0], &spec, &why))
                g_draft.material = spec.material, g_draft.uv = spec.uv, g_draft.colour = spec.colour,
                g_draft.terrain = spec.terrain, g_formMessage.clear();
        });
    if (d.lower)
        ImGui::TextDisabled("Pits need floor pieces inside the rectangle");
    if (!g_formMessage.empty())
        ImGui::TextWrapped("%s", g_formMessage.c_str());
    ImGui::Separator();
    if (ImGui::Button(g_editing ? "Apply" : "Make"))
        g_queue.push_back([] { Apply(); });
    if (g_editing) {
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
            g_queue.push_back([] {
                Ground::Remove(g_draft.name);
                ShowMessage("Removed ground piece " + g_draft.name);
                SetStage(Stage::Off);
            });
    }
    ImGui::SameLine();
    if (ImGui::Button("New Rectangle"))
        g_queue.push_back([] { SetStage(Stage::First); });
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        g_queue.push_back([] { SetStage(Stage::Off); });
    // (the pieces of this level)
    if (!Ground::All().empty() && ImGui::CollapsingHeader("Pieces in this level")) {
        for (const Ground::Spec& s : Ground::All()) {
            char row[160];
            std::snprintf(row, sizeof(row), "%s  %s %.0f x %.0f, %.0f", s.name.c_str(), s.lower ? "pit" : "platform",
                          s.x1 - s.x0, s.z1 - s.z0, s.height);
            if (ImGui::Selectable(row, g_editing && s.name == d.name)) {
                std::string name = s.name;
                g_queue.push_back([name] {
                    if (const Ground::Spec* found = Ground::Find(name)) {
                        OpenForm(*found, true);
                        GoToPoint({(found->x0 + found->x1) * 0.5f, found->base, (found->z0 + found->z1) * 0.5f},
                                  std::max(found->x1 - found->x0, found->z1 - found->z0) * 1.5f + 300.0f);
                    }
                });
            }
        }
    }
    ImGui::End();
    if (!open)
        g_queue.push_back([] { SetStage(Stage::Off); });
}

void GroundHover(V3 origin, V3 unitDir)
{
    g_haveHover = false;
    if (g_stage != Stage::First && g_stage != Stage::Second)
        return;
    V3 hit;
    uint32_t sector = 0;
    if (Cast(origin, origin + unitDir * 20000.0f, &hit, &sector)) {
        g_hover = Snapped(hit);
        g_haveHover = true;
    }
}

bool GroundClick(V3 origin, V3 unitDir)
{
    if (g_stage != Stage::First && g_stage != Stage::Second)
        return false;
    V3 hit;
    uint32_t sector = 0;
    if (!Cast(origin, origin + unitDir * 20000.0f, &hit, &sector)) {
        ShowMessage("Ground Tool: click on the ground");
        return true;
    }
    if (g_stage == Stage::First && Ground::IsPiece(sector)) {
        for (const Ground::Spec& s : Ground::All()) {
            if (hit.x >= s.x0 - 1000.0f && hit.x <= s.x1 + 1000.0f && hit.z >= s.z0 - 1000.0f && hit.z <= s.z1 + 1000.0f) {
                OpenForm(s, true);
                return true;
            }
        }
    }
    V3 corner = Snapped(hit);
    if (g_stage == Stage::First) {
        g_first = corner;
        g_floor = Ground::IsPiece(sector) ? 0 : sector;
        g_stage = Stage::Second;
        ShowMessage("Ground Tool: click the opposite corner", 6.0);
        return true;
    }
    Ground::Spec spec;
    spec.x0 = g_first.x, spec.z0 = g_first.z, spec.x1 = corner.x, spec.z1 = corner.z;
    Ground::Normalise(&spec);
    V3 centre = {(spec.x0 + spec.x1) * 0.5f, std::max(g_first.y, corner.y), (spec.z0 + spec.z1) * 0.5f};
    spec.base = GroundAt(centre, g_first.y);
    std::string why;
    if (!Ground::ChooseMaterial(g_floor, &spec, &why)) {
        ShowMessage("Ground Tool: " + why, 8.0);
        SetStage(Stage::Off);
        return true;
    }
    OpenForm(spec, false);
    return true;
}

} // namespace

void Init()
{
    static bool done;
    if (done)
        return;
    done = true;
    auto make = [](const char* id, const char* name, const char* category, int scancode, const char* tip) {
        Actions::Action a;
        a.id = id;
        a.name = name;
        a.category = category;
        a.tip = tip;
        a.kind = Actions::Kind::Press;
        a.defaults = a.keys = {Actions::Chord{scancode, Actions::kAlt}};
        return Actions::Register(a);
    };
    g_gapAction = make("gap_wizard", "Gap Wizard", "Triggers", SDL_SCANCODE_J, "Create a gap from a start and end point");
    g_skateAction = make("skateability", "Skateability", "View", SDL_SCANCODE_K, "Colour surfaces by skateability");
    Actions::SetChecked(g_skateAction, [] { return SkateView::Shown() ? 1 : 0; });
    g_groundAction = make("ground_tool", "Ground Tool", "Edit", SDL_SCANCODE_U, "Raise or lower a rectangle of ground");
    Layout::Extension e;
    e.keywords = {"ground"};
    e.capture = [] { return Ground::CaptureRecords(); };
    e.apply = [](const std::vector<std::string>& lines) { Ground::ApplyRecords(lines); };
    e.reset = [] { Ground::ResetAll(); };
    Layout::AddExtension(e);
}

void Start()
{
    Ground::Start();
    GapWizard::SetArmed(false);
    g_stage = Stage::Off;
}

void Stop()
{
    GapWizard::SetArmed(false);
    g_stage = Stage::Off;
    SkateView::Stop();
    Ground::Stop();
    g_queue.clear();
}

void Update()
{
    if (g_gapAction >= 0 && Actions::Pressed(g_gapAction)) {
        bool on = !GapWizard::Armed();
        g_stage = Stage::Off;
        GapWizard::SetArmed(on);
        if (!on)
            ShowMessage("Gap Wizard off");
    }
    if (g_skateAction >= 0 && Actions::Pressed(g_skateAction)) {
        SkateView::SetShown(!SkateView::Shown());
        ShowMessage(SkateView::Shown() ? "Skateability on" : "Skateability off");
    }
    if (g_groundAction >= 0 && Actions::Pressed(g_groundAction)) {
        GapWizard::SetArmed(false);
        if (g_stage == Stage::Off)
            SetStage(Stage::First);
        else {
            SetStage(Stage::Off);
            ShowMessage("Ground Tool off");
        }
    }
    GapWizard::Update();
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
}

bool Busy() { return GapWizard::Armed() || g_stage != Stage::Off; }

int PickingTool()
{
    if (GapWizard::Picking())
        return 1;
    return g_stage == Stage::First || g_stage == Stage::Second ? 2 : 0;
}

bool Cancel()
{
    if (GapWizard::Cancel())
        return true;
    if (g_stage == Stage::Off)
        return false;
    SetStage(Stage::Off);
    ShowMessage("Ground Tool off");
    return true;
}

void Hover(V3 origin, V3 unitDir, float x, float y, bool haveRay)
{
    if (!haveRay) {
        g_haveHover = false;
        return;
    }
    GapWizard::Hover(origin, unitDir, x, y);
    GroundHover(origin, unitDir);
}

bool Click(V3 origin, V3 unitDir, float x, float y)
{
    return GapWizard::Click(origin, unitDir, x, y) || GroundClick(origin, unitDir);
}

void AddOverlay(const Overlays::Painter& painter)
{
    SkateView::AddOverlay(painter);
    GapWizard::AddOverlay(painter);
    constexpr uint32_t kRect = 0xFFFFE070, kPreview = 0xE0FFE070;
    if (g_stage == Stage::Second) {
        V3 b = g_haveHover ? g_hover : g_first;
        float y = g_first.y + 2.0f;
        V3 c[4] = {{g_first.x, y, g_first.z}, {b.x, y, g_first.z}, {b.x, y, b.z}, {g_first.x, y, b.z}};
        for (int i = 0; i < 4; ++i)
            painter.Line(c[i], c[(i + 1) % 4], kRect, 2.0f);
        char text[64];
        std::snprintf(text, sizeof(text), "%.0f x %.0f", std::fabs(b.x - g_first.x), std::fabs(b.z - g_first.z));
        painter.Text(b, kRect, text);
    } else if (g_stage == Stage::First && g_haveHover) {
        painter.Cross(g_hover, 25.0f, kRect, 2.0f);
    }
    if (g_stage == Stage::Form)
        Ground::AddPreview(painter, g_draft, kPreview);
}

void Windows(float viewX, float viewY, float viewWidth, float viewHeight, float scale)
{
    GapWizard::Window(viewX, viewY, viewWidth, viewHeight, scale);
    GroundForm(viewX, viewY, viewWidth, scale);
    SkateView::Legend(viewX, viewY, viewWidth, viewHeight, scale);
}

std::string Describe()
{
    std::string text;
    if (!Ground::All().empty())
        text += std::to_string(Ground::All().size()) + " ground pieces";
    if (SkateView::Shown())
        text += std::string(text.empty() ? "" : ", ") + "skateability overlay on";
    if (GapWizard::Armed())
        text += std::string(text.empty() ? "" : ", ") + "gap wizard armed";
    return text;
}

} // namespace Front::LevelEditor::Ux9

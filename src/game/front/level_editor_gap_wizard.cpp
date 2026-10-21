// NOT ORIGINAL - the level editor's two-pick gap wizard (ours). See level_editor_gap_wizard.h.

#include "game/front/level_editor_gap_wizard.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_triggers.h"
#include "game/mth/vector.h"
#include "game/obj/rail_manager.h"
#include "hook/game.h"
#include "hook/log.h"
#include "imgui.h"

namespace Front::LevelEditor::GapWizard {

// --- the pure parts -----------------------------------------------------------------------------------------------

const char* SpotName(SpotKind kind)
{
    switch (kind) {
    case SpotKind::Lip: return "a lip";
    case SpotKind::Wall: return "a wall";
    case SpotKind::Rail: return "a rail";
    default: return "an area";
    }
}

SpotKind KindOfFace(float normalY, uint16_t faceFlags)
{
    bool vert = (faceFlags & 0x08) != 0;
    if (!vert && normalY >= 0.7f)
        return SpotKind::Area;
    if (!vert && std::fabs(normalY) < 0.2f)
        return SpotKind::Wall;
    return SpotKind::Lip;
}

const char* TypeName(Type type)
{
    switch (type) {
    case Type::Grind: return "Grind";
    case Type::Lip: return "Lip";
    case Type::Manual: return "Manual";
    case Type::Wallride: return "Wallride";
    default: return "Air";
    }
}

std::vector<std::string> FlagsFor(Type type)
{
    switch (type) {
    case Type::Grind: return {"REQUIRE_RAIL", "CANCEL_GROUND"};
    case Type::Lip: return {"REQUIRE_LIP"};
    case Type::Manual: return {"PURE_MANUAL"};
    case Type::Wallride: return {"REQUIRE_WALLRIDE", "CANCEL_GROUND"};
    default: return {"CANCEL_GROUND"};
    }
}

Type Suggest(const Spot& start, const Spot& end)
{
    if (start.kind == SpotKind::Rail && end.kind == SpotKind::Rail)
        return Type::Grind;
    if (start.kind == SpotKind::Wall || end.kind == SpotKind::Wall)
        return Type::Wallride;
    return Type::Air;
}

float HeadingOf(V3 d)
{
    if (std::fabs(d.x) < 1e-6f && std::fabs(d.z) < 1e-6f)
        return 0.0f;
    return std::atan2(d.x, d.z);
}

Gate GateFor(const Spot& spot, V3 toward)
{
    Gate gate;
    V3 to = toward - spot.point;
    switch (spot.kind) {
    case SpotKind::Rail: {
        // (across the rail, the skater grinding through it: its thin side along the rail, pointing to the other end)
        V3 along = spot.along;
        if (Dot(along, to) < 0.0f)
            along = along * -1.0f;
        gate.yaw = HeadingOf(Dot(along, along) > 1e-6f ? along : to);
        gate.size = {120.0f, 120.0f, 60.0f};
        gate.pos = spot.point + V3{0.0f, 30.0f, 0.0f};
        break;
    }
    case SpotKind::Lip:
        gate.yaw = HeadingOf(to);
        gate.size = {300.0f, 400.0f, 60.0f};
        gate.pos = spot.point + V3{0.0f, 200.0f, 0.0f};
        break;
    case SpotKind::Wall: {
        V3 out = {spot.normal.x, 0.0f, spot.normal.z};
        float length = std::sqrt(Dot(out, out));
        out = length > 1e-4f ? out * (1.0f / length) : V3{0.0f, 0.0f, 0.0f};
        gate.yaw = HeadingOf(to);
        gate.size = {300.0f, 300.0f, 60.0f};
        gate.pos = spot.point + out * 80.0f;
        break;
    }
    default:
        gate.yaw = HeadingOf(to);
        gate.size = {300.0f, 300.0f, 60.0f};
        gate.pos = spot.point + V3{0.0f, 150.0f, 0.0f};
        break;
    }
    return gate;
}

int ClampScore(int score) { return std::max(kMinScore, std::min(kMaxScore, score)); }

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

bool g_armed;
int g_picked; // (0, 1 or 2 ends)
Spot g_spots[2];
bool g_haveHover;
Spot g_hover;
// The form.
bool g_formOpen;
char g_name[64] = "My Gap";
int g_score = 250;
int g_type;
std::vector<std::function<void()>> g_queue;

struct Hit {
    V3 point, normal;
    uint16_t flags;
};

// The game's collision ray (a CFeeler, as level_editor_ruler.cpp's): the point, the face's normal and flags.
bool Cast(V3 from, V3 to, Hit* hit)
{
    alignas(16) uint8_t feeler[0x100] = {};
    GameFunction<void*(__thiscall*)(void*)>(0x00417760)(feeler);
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, int)>(0x00417710)(feeler, &start,
                                                                                                        &end, 0))
        return false;
    // (the feeler's hit record at +0x24: normal +0x00, point +0x10, face flags +0x2C - coll_line.h's CollisionHit)
    const float* n = reinterpret_cast<const float*>(feeler + 0x24);
    const float* p = reinterpret_cast<const float*>(feeler + 0x34);
    hit->normal = {n[0], n[1], n[2]};
    hit->point = {p[0], p[1], p[2]};
    hit->flags = *reinterpret_cast<const uint16_t*>(feeler + 0x50);
    return true;
}

Obj::CRailManager* Rails()
{
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478);
    auto* manager = skate ? *reinterpret_cast<Obj::CRailManager**>(skate + 0x24) : nullptr;
    return manager && !manager->m_onObject && manager->m_nodes ? manager : nullptr;
}

// The rail segment nearest the cursor within 14 pixels (and not behind what the ray hits first): the point on it.
bool RailNear(V3 origin, V3 unitDir, float x, float y, float maxT, Spot* spot)
{
    Obj::CRailManager* rails = Rails();
    if (!rails)
        return false;
    float best = 14.0f;
    bool found = false;
    for (int i = 0; i < rails->m_count; ++i) {
        const Obj::CRailNode& node = rails->m_nodes[i];
        if (!node.m_next || (node.m_flags & Obj::RAIL_FLAG_CLIMBING) || !(node.m_flags & Obj::RAIL_FLAG_ACTIVE))
            continue;
        V3 a = {node.m_pos.x, node.m_pos.y, node.m_pos.z};
        V3 b = {node.m_next->m_pos.x, node.m_next->m_pos.y, node.m_next->m_pos.z};
        float ax, ay, bx, by;
        if (!ProjectToBack(a, &ax, &ay) || !ProjectToBack(b, &bx, &by))
            continue;
        float d = DistanceToSegment(x, y, ax, ay, bx, by);
        if (d >= best)
            continue;
        // (where along it: the screen's nearest point, carried to the world segment)
        float sx = bx - ax, sy = by - ay;
        float length2 = sx * sx + sy * sy;
        float t = length2 > 1e-6f ? std::clamp(((x - ax) * sx + (y - ay) * sy) / length2, 0.0f, 1.0f) : 0.0f;
        V3 p = a + (b - a) * t;
        V3 to = p - origin;
        if (Dot(to, unitDir) > maxT + 60.0f)
            continue; // (behind a wall)
        best = d;
        found = true;
        spot->kind = SpotKind::Rail;
        spot->point = p;
        spot->normal = {0.0f, 1.0f, 0.0f};
        spot->along = Normalize(b - a);
    }
    return found;
}

bool SpotAt(V3 origin, V3 unitDir, float x, float y, Spot* spot)
{
    Hit hit;
    bool haveHit = Cast(origin, origin + unitDir * 20000.0f, &hit);
    float maxT = haveHit ? Dot(hit.point - origin, unitDir) : 20000.0f;
    if (RailNear(origin, unitDir, x, y, maxT, spot))
        return true;
    if (!haveHit)
        return false;
    spot->kind = KindOfFace(hit.normal.y, hit.flags);
    spot->point = hit.point;
    spot->normal = hit.normal;
    spot->along = {};
    return true;
}

void DrawGate(const Overlays::Painter& painter, const Gate& gate, uint32_t argb)
{
    OrientedBox box = Triggers::BoxOf(gate.pos, gate.yaw, gate.size);
    V3 c[8];
    BoxCorners(box.origin, box.axes, box.min, box.max, c);
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges)
        painter.Line(c[e[0]], c[e[1]], argb, 2.0f);
}

void Make()
{
    Triggers::Placed start, end;
    Gate a = GateFor(g_spots[0], g_spots[1].point), b = GateFor(g_spots[1], g_spots[0].point);
    // (a box is the same turned half round: the end's gate facing back to the start lies across the same line)
    start.pos = a.pos, start.yaw = a.yaw, start.spec.size = a.size;
    end.pos = b.pos, end.yaw = b.yaw, end.spec.size = b.size;
    start.spec.text = g_name;
    start.spec.score = ClampScore(g_score);
    start.spec.flags = FlagsFor(static_cast<Type>(g_type));
    Log("level editor: gap wizard - \"%s\" (%d, %s) from %s to %s", g_name, start.spec.score,
        TypeName(static_cast<Type>(g_type)), SpotName(g_spots[0].kind), SpotName(g_spots[1].kind));
    Triggers::Queue([start, end] { Triggers::AddGapPair(start, end); });
}

} // namespace

bool Armed() { return g_armed || g_formOpen; }
bool Picking() { return g_armed && g_picked < 2; }

void SetArmed(bool on)
{
    g_armed = on;
    g_picked = 0;
    g_formOpen = false;
    g_haveHover = false;
    if (on)
        ShowMessage("Gap wizard: click where the gap starts (a rail, a lip, a wall or the ground) - Esc cancels", 6.0);
}

bool Cancel()
{
    if (!Armed())
        return false;
    SetArmed(false);
    ShowMessage("Gap wizard cancelled");
    return true;
}

void Hover(V3 origin, V3 unitDir, float x, float y)
{
    g_haveHover = g_armed && g_picked < 2 && SpotAt(origin, unitDir, x, y, &g_hover);
}

bool Click(V3 origin, V3 unitDir, float x, float y)
{
    if (!g_armed || g_picked >= 2)
        return false;
    Spot spot;
    if (!SpotAt(origin, unitDir, x, y, &spot)) {
        ShowMessage("Gap wizard: nothing there - click a rail, a lip, a wall or the ground");
        return true;
    }
    g_spots[g_picked++] = spot;
    if (g_picked == 1) {
        ShowMessage(std::string("Gap starts at ") + SpotName(spot.kind) + " - now click where it ends", 6.0);
    } else {
        g_armed = false;
        g_formOpen = true;
        g_type = static_cast<int>(Suggest(g_spots[0], g_spots[1]));
        ShowMessage(std::string("Gap ends at ") + SpotName(spot.kind) + " - name it, score it and make it", 6.0);
    }
    return true;
}

void AddOverlay(const Overlays::Painter& painter)
{
    if (!Armed())
        return;
    constexpr uint32_t kStart = 0xFF62E26A, kEnd = 0xFFF2D24A, kHover = 0xC0FFFFFF;
    if (g_picked >= 1) {
        V3 other = g_picked >= 2 ? g_spots[1].point : g_haveHover ? g_hover.point : g_spots[0].point + V3{0, 0, 1};
        DrawGate(painter, GateFor(g_spots[0], other), kStart);
        painter.Text(g_spots[0].point, kStart, std::string("start: ") + SpotName(g_spots[0].kind));
    }
    if (g_picked >= 2) {
        Gate b = GateFor(g_spots[1], g_spots[0].point);
        DrawGate(painter, b, kEnd);
        painter.Text(g_spots[1].point, kEnd, std::string("end: ") + SpotName(g_spots[1].kind));
        painter.Line(g_spots[0].point, g_spots[1].point, 0xFFFFFFFF, 2.0f);
        V3 d = g_spots[1].point - g_spots[0].point;
        char text[64];
        std::snprintf(text, sizeof(text), "%.0f across, %.0f %s", std::sqrt(d.x * d.x + d.z * d.z), std::fabs(d.y),
                      d.y >= 0.0f ? "up" : "down");
        painter.Text((g_spots[0].point + g_spots[1].point) * 0.5f, 0xFFFFFFFF, text);
    } else if (g_haveHover) {
        painter.Dot(g_hover.point, 5.0f, kHover);
        painter.Text(g_hover.point, kHover, SpotName(g_hover.kind));
        if (g_picked == 1)
            painter.Line(g_spots[0].point, g_hover.point, 0x90FFFFFF, 1.0f);
    }
}

void Update()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
}

void Window(float viewX, float viewY, float viewWidth, float viewHeight, float scale)
{
    (void)viewHeight;
    if (!g_formOpen)
        return;
    ImGui::SetNextWindowPos(ImVec2(viewX + viewWidth * 0.5f, viewY + 80.0f * scale), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(320.0f * scale, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::Begin("Gap wizard###gapWizard", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open)
            g_queue.push_back([] { SetArmed(false); });
        return;
    }
    ImGui::TextDisabled("From %s to %s", SpotName(g_spots[0].kind), SpotName(g_spots[1].kind));
    ImGui::InputText("Name", g_name, sizeof(g_name));
    ImGui::SliderInt("Score", &g_score, kMinScore, kMaxScore, "%d", ImGuiSliderFlags_AlwaysClamp);
    for (int t = 0; t < kTypes; ++t) {
        if (t)
            ImGui::SameLine();
        ImGui::RadioButton(TypeName(static_cast<Type>(t)), &g_type, t);
    }
    std::string flags;
    for (const std::string& f : FlagsFor(static_cast<Type>(g_type)))
        flags += (flags.empty() ? "" : ", ") + f;
    ImGui::TextDisabled("StartGap flags: %s", flags.c_str());
    ImGui::BeginDisabled();
    ImGui::RadioButton("Skitch", false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("THUG2's gaps have no skitch flag - a skitch gap can't be made");
    ImGui::Separator();
    bool named = std::any_of(g_name, g_name + std::strlen(g_name), [](char c) { return c != ' '; });
    ImGui::BeginDisabled(!named);
    if (ImGui::Button("Make the gap"))
        g_queue.push_back([] {
            Make();
            g_formOpen = false;
            g_picked = 0;
        });
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Pick again"))
        g_queue.push_back([] { SetArmed(true); });
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        g_queue.push_back([] { SetArmed(false); });
    ImGui::End();
    if (!open)
        g_queue.push_back([] { SetArmed(false); });
}

} // namespace Front::LevelEditor::GapWizard

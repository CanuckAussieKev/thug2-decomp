// NOT ORIGINAL - the level editor's View and Overlays menus (ours). See level_editor_overlays.h; the menus themselves
// are level_editor_overlays_ui.cpp's.

#include "editor/level_editor_overlays.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "editor/level_editor_carry.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_view.h"
#include "engine/gfx/view_mode.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"

namespace MenuSystem::LevelEditor::Overlays {

// --- the pieces -------------------------------------------------------------------------------------------------

std::vector<GridLine> GridLines(V3 centre, float y, float radius, float spacing)
{
    std::vector<GridLine> lines;
    if (!(spacing > 0.0f) || !(radius > 0.0f) || radius / spacing > 200.0f)
        return lines;
    // (the lines on multiples of the spacing, within the square round the centre)
    long first[2] = {static_cast<long>(std::ceil((centre.x - radius) / spacing)),
                     static_cast<long>(std::ceil((centre.z - radius) / spacing))};
    long last[2] = {static_cast<long>(std::floor((centre.x + radius) / spacing)),
                    static_cast<long>(std::floor((centre.z + radius) / spacing))};
    for (long i = first[0]; i <= last[0]; ++i) {
        float x = static_cast<float>(i) * spacing;
        lines.push_back({{x, y, centre.z - radius}, {x, y, centre.z + radius}, i % 5 == 0});
    }
    for (long i = first[1]; i <= last[1]; ++i) {
        float z = static_cast<float>(i) * spacing;
        lines.push_back({{centre.x - radius, y, z}, {centre.x + radius, y, z}, i % 5 == 0});
    }
    return lines;
}

uint32_t ParseBits(int value, bool present)
{
    if (!present || value < 0)
        return kDefaultBits;
    return static_cast<uint32_t>(value) & kAllBits;
}

const std::vector<BitInfo>& Bits()
{
    static const std::vector<BitInfo> bits = {
        {kLabels, "Labels", "", "Names and values over the view"},
        {kFeatureMap, "Feature Map", "", "Speed-ups, boosts, proximity shapes and breakables"},
        {kGrindPreview, "Grind Preview", "", "Direction, terrain and connections of the rail under the cursor"},
        {kJumpArcs, "Jump Arcs", "Y", "Launch arcs from the selected ramp"},
        {kRuler, "Ruler", "M", "The ruler's measurement"},
        {kSkateability, "Skateability", "Alt+K", "Surfaces coloured by skateability"},
        {kGrid, "Grid", "", "100-unit ground grid"},
        {kCarryHints, "Carried Objects", "", "Outline objects carried by moving scenery"},
        {kLegend, "Collision Legend", "", "Collision colour key"},
        {kOutlinerMarks, "Outliner Markers", "", "Change letters on Outliner rows"},
    };
    return bits;
}

// --- the setting ------------------------------------------------------------------------------------------------

namespace {

bool g_read;
uint32_t g_bits = kDefaultBits;
int g_viewMode;
bool g_forcedCollision; // (Collision only turned the collision view on: off again when it ends)

void TakeIn()
{
    if (g_read)
        return;
    g_read = true;
    int value = ConfigInt("editor", "overlays", -1);
    g_bits = ParseBits(value, value >= 0);
}

} // namespace

uint32_t AtHand()
{
    TakeIn();
    return g_bits;
}

bool Shown(uint32_t bit) { return (AtHand() & bit) != 0; }

void AssignValue(uint32_t bit, bool on)
{
    TakeIn();
    uint32_t bits = on ? g_bits | bit : g_bits & ~bit;
    if (bits == g_bits)
        return;
    g_bits = bits & kAllBits;
    ConfigSetInt("editor", "overlays", static_cast<int>(g_bits));
}

void Kickoff()
{
    TakeIn();
    g_viewMode = 0;
    g_forcedCollision = false;
    Engine3D::ViewMode::AssignValue(Engine3D::ViewMode::Mode::kLit);
}

void Halt() { ChangeViewMode(0); }

int ViewMode() { return g_viewMode; }

void ChangeViewMode(int mode)
{
    if (mode < 0 || mode >= kViewModes || mode == g_viewMode)
        return;
    constexpr int kCollisionOnly = static_cast<int>(Engine3D::ViewMode::Mode::kCollisionOnly);
    if (mode == kCollisionOnly && !View::ShowCollision()) {
        View::SetShowCollision(true);
        g_forcedCollision = true;
    } else if (g_viewMode == kCollisionOnly && g_forcedCollision) {
        if (View::ShowCollision())
            View::SetShowCollision(false);
        g_forcedCollision = false;
    }
    g_viewMode = mode;
    Engine3D::ViewMode::AssignValue(static_cast<Engine3D::ViewMode::Mode>(mode));
}

const char* ViewModeName(int mode) { return Engine3D::ViewMode::Name(static_cast<Engine3D::ViewMode::Mode>(mode)); }

// --- drawing ----------------------------------------------------------------------------------------------------

bool Painter::Pixel(V3 p, float* x, float* y) const
{
    float nx, ny;
    if (!CastOnto(camera, frame, lens, p, nearZ, &nx, &ny))
        return false;
    NdcToPixel(viewport, nx, ny, x, y);
    return true;
}

bool Painter::Seg(V3 a, V3 b, uint32_t argb, float thickness) const
{
    if (!ClipToNear(camera, frame, nearZ + 0.01f, &a, &b))
        return false;
    float ax, ay, bx, by;
    if (!Pixel(a, &ax, &ay) || !Pixel(b, &bx, &by))
        return false;
    overlay->lines.push_back({ax, ay, bx, by, argb, thickness});
    return true;
}

void Painter::Dot(V3 p, float radius, uint32_t argb, bool filled) const
{
    float x, y;
    if (Pixel(p, &x, &y))
        overlay->circles.push_back({x, y, radius, argb, filled, 1.5f});
}

void Painter::Text(V3 p, uint32_t argb, const std::string& text, float dx, float dy) const
{
    float x, y;
    if (Pixel(p, &x, &y))
        overlay->texts.push_back({x + dx, y + dy, argb, text});
}

void Painter::Perpendicular(V3 p, float size, uint32_t argb, float thickness) const
{
    Seg(p - V3{size, 0.0f, 0.0f}, p + V3{size, 0.0f, 0.0f}, argb, thickness);
    Seg(p - V3{0.0f, 0.0f, size}, p + V3{0.0f, 0.0f, size}, argb, thickness);
}

float Painter::SpanTo(V3 p) const
{
    V3 d = p - camera;
    return std::sqrt(LevelEditor::Dot(d, d));
}

bool PainterNow(Ui::Overlay* overlay, Painter* painter)
{
    ViewNow view;
    if (!CurrentView(&view))
        return false;
    painter->camera = view.pos;
    painter->frame = FrameFromYawPitch(view.yaw, view.pitch);
    painter->lens = view.lens;
    for (int i = 0; i < 4; ++i)
        painter->viewport[i] = view.viewport[i];
    painter->nearZ = view.nearZ;
    painter->overlay = overlay;
    return true;
}

namespace {

// The ground's height below a point (the game's collision ray: a CProbeRay, constructor 0x00417760, cast 0x00417710 -
// static collision only; the hit's point at +0x34), as level_editor.cpp's GroundBelow.
bool GroundY(V3 from, float* y)
{
    alignas(16) uint8_t feeler[0x100] = {};
    auto* ray = reinterpret_cast<Engine3D::CProbeRay*>(feeler);
    ray->MakeNew();
    Maths::Vec4 start = {from.x, from.y, from.z, 1.0f}, end = {from.x, from.y - 5000.0f, from.z, 1.0f};
    if (!ray->GetCollisionAlong(&start, &end, false))
        return false;
    *y = reinterpret_cast<const float*>(feeler + 0x34)[1];
    return true;
}

} // namespace

void AddOverlay(const Painter& painter, bool moving, V3 selectionBase, bool haveSelection)
{
    if (Shown(kGrid)) {
        // (on the ground below the selection, else below the camera; the ground found again only when it moved)
        static V3 lastFrom = {1e30f, 0.0f, 0.0f};
        static float groundY = 0.0f;
        static bool haveGround = false;
        V3 from = haveSelection ? selectionBase + V3{0.0f, 2.0f, 0.0f} : painter.camera;
        V3 d = from - lastFrom;
        if (Dot(d, d) > 25.0f * 25.0f) {
            lastFrom = from;
            haveGround = GroundY(from, &groundY);
        }
        if (haveGround) {
            V3 centre = {std::round(from.x / 100.0f) * 100.0f, groundY, std::round(from.z / 100.0f) * 100.0f};
            for (const GridLine& line : GridLines(centre, groundY + 0.5f, 1500.0f, 100.0f))
                painter.Seg(line.a, line.b, line.major ? 0x70E0E8F0u : 0x38C0C8D0u, line.major ? 1.25f : 1.0f);
        }
    }
    if (Shown(kCarryHints) && moving) {
        constexpr uint32_t kCarried = 0xE060F080;
        for (uint32_t id : Carry::CarriedObjects()) {
            auto* object = static_cast<GameObjects::CGameEntity*>(GameObjects::FindObject(id));
            OrientedBox box;
            if (!object || (object->m_objectFlags & GameObjects::OBJECT_FLAG_DEAD) || !Objects::ModelBox(object, &box))
                continue;
            V3 corners[8];
            BoxCorners(box.origin, box.axes, box.min, box.max, corners);
            static const int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                              {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& e : kEdges)
                painter.Seg(corners[e[0]], corners[e[1]], kCarried, 1.25f);
        }
        const std::vector<Nodes::Elem>& all = Nodes::All();
        for (uint32_t name : Carry::CarriedNodes()) {
            int index = Nodes::Find(name);
            if (index < 0 || !all[index].hasPos)
                continue;
            painter.Dot(all[index].pos, 5.0f, kCarried, false);
        }
    }
}

} // namespace MenuSystem::LevelEditor::Overlays

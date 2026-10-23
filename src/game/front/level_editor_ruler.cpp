// NOT ORIGINAL - the level editor's ruler (ours). See level_editor_ruler.h.

#include "game/front/level_editor_ruler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/front/level_editor_arcs.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_scenery.h"
#include "game/mth/vector.h"
#include "game/nx/coll_tri.h"
#include "game/nx/feeler.h"
#include "hook/game.h"

namespace Front::LevelEditor::Ruler {

// --- the pieces -------------------------------------------------------------------------------------------------

Measure MeasureOf(V3 a, V3 b)
{
    V3 d = b - a;
    Measure m;
    m.distance = std::sqrt(Dot(d, d));
    m.horizontal = std::sqrt(d.x * d.x + d.z * d.z);
    m.height = d.y;
    return m;
}

int NearestWithin(const std::vector<std::pair<float, float>>& pixels, float x, float y, float radius)
{
    int best = -1;
    float bestD2 = radius * radius;
    for (size_t i = 0; i < pixels.size(); ++i) {
        float dx = pixels[i].first - x, dy = pixels[i].second - y;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) {
            bestD2 = d2;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::string Describe(const Measure& m)
{
    std::string text = Arcs::Distance(m.distance) + " - across " + Arcs::Distance(m.horizontal) + ", ";
    text += m.height < 0.0f ? "down " : "up ";
    return text + Arcs::Distance(std::fabs(m.height));
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kCollStaticVtable = 0x00645C18; // (CCollStatic)

bool g_armed;
int g_ends;        // (0, 1 or 2 ends placed)
V3 g_a, g_b;
bool g_haveHover, g_hoverSnapped;
V3 g_hover;

// The game's collision ray (a CFeeler as level_editor_arcs.cpp's): the point hit and the collision object's checksum.
bool CastFeeler(V3 from, V3 to, V3* hit, uint32_t* sector)
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

// The corners of a sector's collision near `near` (within `reach`), at most 20000 looked at.
std::vector<V3> CornersNear(uint32_t checksum, V3 near, float reach)
{
    std::vector<V3> corners;
    uint8_t* sector = checksum ? Scenery::SectorOf(checksum) : nullptr;
    if (!sector)
        return corners;
    uint8_t* coll = *reinterpret_cast<uint8_t**>(sector + 0x18);
    if (!coll || *reinterpret_cast<uint32_t*>(coll) != kCollStaticVtable)
        return corners;
    const auto* tri = *reinterpret_cast<const Nx::CCollObjTriData* const*>(coll + 0x0C);
    if (!tri || !tri->m_vertices)
        return corners;
    int count = tri->m_numVertices < 20000 ? tri->m_numVertices : 20000;
    for (int i = 0; i < count; ++i) {
        Mth::Vector v = tri->GetRawVertexPos(i);
        V3 p = {v.x, v.y, v.z};
        V3 d = p - near;
        if (Dot(d, d) <= reach * reach)
            corners.push_back(p);
    }
    return corners;
}

} // namespace

bool Armed() { return g_armed; }

void SetArmed(bool on)
{
    g_armed = on;
    g_haveHover = false;
}

void Clear()
{
    g_ends = 0;
    g_haveHover = false;
}

void Hover(V3 origin, V3 unitDir, float x, float y, bool noSnap)
{
    g_haveHover = false;
    if (!g_armed)
        return;
    V3 hit;
    uint32_t sector = 0;
    if (!CastFeeler(origin, origin + unitDir * 20000.0f, &hit, &sector))
        return;
    g_haveHover = true;
    g_hover = hit;
    g_hoverSnapped = false;
    ViewNow view;
    if (noSnap || !CurrentView(&view))
        return;
    // (the corners of the piece hit, near enough to the hit to be within the snap on screen)
    V3 d = hit - origin;
    float reach = std::max(50.0f, std::sqrt(Dot(d, d)) * 0.08f);
    std::vector<V3> corners = CornersNear(sector, hit, reach);
    Frame frame = FrameFromYawPitch(view.yaw, view.pitch);
    std::vector<std::pair<float, float>> pixels;
    std::vector<V3> shown;
    for (V3 c : corners) {
        float nx, ny, px, py;
        if (!Project(view.pos, frame, view.lens, c, view.nearZ, &nx, &ny))
            continue;
        NdcToPixel(view.viewport, nx, ny, &px, &py);
        pixels.push_back({px, py});
        shown.push_back(c);
    }
    int nearest = NearestWithin(pixels, x, y, kSnapPixels);
    if (nearest >= 0) {
        g_hover = shown[nearest];
        g_hoverSnapped = true;
    }
}

std::string Click()
{
    if (!g_armed || !g_haveHover)
        return "Ruler: no surface under cursor";
    if (g_ends == 1) {
        g_b = g_hover;
        g_ends = 2;
        return "Ruler: " + Describe(MeasureOf(g_a, g_b)) + " (Shift+M clears)";
    }
    g_a = g_hover;
    g_ends = 1;
    return "Ruler: click the second point";
}

void AddOverlay(const Overlays::Painter& painter)
{
    if (!Overlays::Shown(Overlays::kRuler))
        return;
    constexpr uint32_t kRuler = 0xFFFFE040, kGap = 0xC040C0FF, kRise = 0xC0FF8040;
    auto end = [&](V3 p, bool snapped) {
        painter.Dot(p, snapped ? 6.0f : 4.0f, kRuler, !snapped);
        if (snapped)
            painter.Dot(p, 2.0f, kRuler, true);
    };
    if (g_ends >= 1)
        end(g_a, false);
    V3 b = g_ends == 2 ? g_b : g_hover;
    bool haveB = g_ends == 2 || (g_armed && g_ends == 1 && g_haveHover);
    if (g_armed && g_haveHover && g_ends != 1)
        end(g_hover, g_hoverSnapped); // (where the next click puts an end)
    if (g_ends >= 1 && haveB) {
        if (g_ends == 1)
            end(b, g_hoverSnapped);
        else
            end(b, false);
        Measure m = MeasureOf(g_a, b);
        painter.Line(g_a, b, kRuler, 2.0f);
        // (the horizontal gap at the lower end's height, the rise straight up from it)
        V3 low = g_a.y <= b.y ? g_a : b, high = g_a.y <= b.y ? b : g_a;
        V3 corner = {high.x, low.y, high.z};
        painter.Line(low, corner, kGap, 1.5f);
        painter.Line(corner, high, kRise, 1.5f);
        painter.Text((g_a + b) * 0.5f, kRuler, Arcs::Distance(m.distance));
        painter.Text((low + corner) * 0.5f, kGap, "across " + Arcs::Distance(m.horizontal), 6.0f, 6.0f);
        if (std::fabs(m.height) > 0.5f)
            painter.Text((corner + high) * 0.5f, kRise, (m.height < 0.0f ? "down " : "up ") + Arcs::Distance(std::fabs(m.height)));
    }
}

std::string Describe()
{
    if (g_ends < 2)
        return g_armed ? (g_ends == 1 ? "ruler: click the second end" : "ruler: click the first end") : "";
    return "ruler: " + Describe(MeasureOf(g_a, g_b));
}

} // namespace Front::LevelEditor::Ruler

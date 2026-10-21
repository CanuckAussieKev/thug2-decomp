// NOT ORIGINAL - the level editor's skateability overlay (ours). See level_editor_skate_view.h.

#include "game/front/level_editor_skate_view.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_triggers.h"
#include "game/front/level_editor_view.h"
#include "game/mth/vector.h"
#include "game/nx/coll_tri.h"
#include "game/obj/rail_manager.h"
#include "hook/game.h"
#include "imgui.h"

namespace Front::LevelEditor::SkateView {

// --- the pure parts -----------------------------------------------------------------------------------------------

Kind Classify(uint16_t flags, float normalY, float threshold, int trigger)
{
    if (trigger == 1)
        return Kind::Kill;
    if (trigger == 2)
        return Kind::Water;
    if (flags & 0x10)
        return Kind::None; // (passed through)
    bool skatable;
    bool vert = (flags & 0x08) != 0;
    if (flags & 0x01)
        skatable = true;
    else if (flags & 0x02)
        return Kind::NotSkatable;
    else if (vert)
        skatable = true;
    else if (flags & 0x04)
        return Kind::Wallride;
    else
        skatable = normalY >= threshold;
    if (!skatable)
        return Kind::NotSkatable;
    if (vert)
        return Kind::Vert;
    return normalY >= 0.97f ? Kind::Floor : Kind::Bank;
}

uint32_t Colour(Kind kind)
{
    switch (kind) {
    case Kind::Floor: return 0x6840D850;
    case Kind::Bank: return 0x7040C8C0;
    case Kind::Vert: return 0x88FF8A20;
    case Kind::Wallride: return 0x78FFD830;
    case Kind::NotSkatable: return 0x70E03838;
    case Kind::Kill: return 0x90FF1050;
    case Kind::Water: return 0x903070FF;
    default: return 0;
    }
}

const char* Name(Kind kind)
{
    switch (kind) {
    case Kind::Floor: return "Skatable floor";
    case Kind::Bank: return "Bank / ramp (skatable slope)";
    case Kind::Vert: return "Vert (quarter pipe: straight up)";
    case Kind::Wallride: return "Wallride wall";
    case Kind::NotSkatable: return "Not skatable (bail / wall)";
    case Kind::Kill: return "Kill zone (bail, back to a restart)";
    case Kind::Water: return "Water (kill zone)";
    default: return "";
    }
}

float Threshold(float angle, float factor)
{
    if (!std::isfinite(angle) || !std::isfinite(factor) || !(factor > 0.0f))
        return std::sin(45.0f * 3.14159265f / 180.0f);
    return std::max(0.0f, std::min(1.0f, static_cast<float>(std::sin(static_cast<double>(angle) * factor))));
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kCollStaticVtable = 0x00645C18;
constexpr float kRadius = 3000.0f;
constexpr int kMaxFaces = 12000;
constexpr uint32_t kRail = 0xF040F0FF;

struct Cached {
    const Nx::CCollObjTriData* tri = nullptr;
    V3 min = {}, max = {}, first = {}, last = {};
    int faces = 0;
    float threshold = -1.0f;
    std::vector<V3> points;
    std::vector<uint8_t> kinds;
};
std::unordered_map<uint32_t, Cached> g_cache;
int g_counts[kKinds];
bool g_capped;
int g_railSegments;
float g_threshold = -1.0f;

V3 ToV3(const Mth::Vector& v) { return {v.x, v.y, v.z}; }

float GameThreshold()
{
    if (g_threshold < 0.0f) {
        // (0x005003A0: the script float 0x3EEDE4D3 - 0x00478F00 with its assert on, as it asks - times 0x00645464)
        float angle = GameFunction<float(__cdecl*)(uint32_t, int)>(0x00478F00)(0x3EEDE4D3, 1);
        float factor = *reinterpret_cast<const float*>(0x00645464);
        g_threshold = Threshold(angle, factor);
    }
    return g_threshold;
}

const Cached& CacheOf(uint32_t checksum, const Nx::CCollObjTriData* tri, int trigger)
{
    V3 min = ToV3(tri->m_min), max = ToV3(tri->m_max), first = {}, last = {};
    if (tri->m_vertices && tri->m_numVertices) {
        first = ToV3(tri->GetRawVertexPos(0));
        last = ToV3(tri->GetRawVertexPos(tri->m_numVertices - 1));
    }
    auto same = [](V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    float threshold = GameThreshold();
    Cached& c = g_cache[checksum];
    if (c.tri == tri && c.faces == tri->m_numFaces && c.threshold == threshold && same(c.min, min) &&
        same(c.max, max) && same(c.first, first) && same(c.last, last))
        return c;
    c = Cached{};
    c.tri = tri, c.min = min, c.max = max, c.first = first, c.last = last, c.faces = tri->m_numFaces;
    c.threshold = threshold;
    if (!tri->m_faces || !tri->m_vertices)
        return c;
    for (int f = 0; f < c.faces; ++f) {
        V3 p[3];
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            uint32_t index = tri->FaceVertex(f, k);
            ok = index < tri->m_numVertices;
            if (ok)
                p[k] = ToV3(tri->GetRawVertexPos(static_cast<int>(index)));
        }
        if (!ok)
            continue;
        uint16_t flags = *reinterpret_cast<const uint16_t*>(tri->m_faces + f * (tri->m_smallFaces ? 8 : 10));
        V3 n = Cross(p[1] - p[0], p[2] - p[0]);
        float length = std::sqrt(Dot(n, n));
        float ny = length > 0.0f ? n.y / length : 0.0f;
        Kind kind = Classify(flags, ny, threshold, (flags & 0x40) ? trigger : 0);
        if (kind == Kind::None)
            continue;
        c.points.insert(c.points.end(), p, p + 3);
        c.kinds.push_back(static_cast<uint8_t>(kind));
    }
    return c;
}

} // namespace

bool Shown() { return Overlays::Shown(Overlays::kSkateability); }
void SetShown(bool on) { Overlays::Set(Overlays::kSkateability, on); }
void Stop() { g_cache.clear(); }

void AddOverlay(const Overlays::Painter& painter)
{
    std::memset(g_counts, 0, sizeof(g_counts));
    g_capped = false;
    g_railSegments = 0;
    if (!Shown() || !painter.overlay)
        return;
    // The level's kill / water triggers by sector (1 kill, 2 water).
    std::unordered_map<uint32_t, int> triggers;
    for (const Triggers::LevelTrigger& t : Triggers::LevelTriggers()) {
        if (t.kind == Triggers::Kind::Kill && !t.off)
            triggers[t.sector] = t.calls.water ? 2 : 1;
    }
    struct Candidate {
        uint32_t checksum;
        const Nx::CCollObjTriData* tri;
        float distance;
    };
    struct Walk {
        V3 camera;
        std::vector<Candidate> found;
    } walk = {painter.camera, {}};
    Scenery::ForEachLevelSector(
        [](void* context, uint32_t checksum, uint8_t* sector) {
            auto* w = static_cast<Walk*>(context);
            uint8_t* coll = *reinterpret_cast<uint8_t**>(sector + 0x18);
            if (!coll || *reinterpret_cast<uint32_t*>(coll) != kCollStaticVtable || (coll[0x08] & 4))
                return; // (no collision, or switched off)
            const auto* tri = *reinterpret_cast<const Nx::CCollObjTriData* const*>(coll + 0x0C);
            if (!tri || !tri->m_numFaces || View::HiddenSector(checksum))
                return;
            float distance = View::DistanceToBox(w->camera, ToV3(tri->m_min), ToV3(tri->m_max));
            if (distance <= kRadius)
                w->found.push_back({checksum, tri, distance});
        },
        &walk);
    std::sort(walk.found.begin(), walk.found.end(),
              [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    struct Polygon {
        float depth;
        uint32_t argb;
        int count;
        float x[4], y[4];
    };
    std::vector<Polygon> polygons;
    for (const Candidate& candidate : walk.found) {
        auto trigger = triggers.find(candidate.checksum);
        const Cached& c = CacheOf(candidate.checksum, candidate.tri, trigger == triggers.end() ? 0 : trigger->second);
        for (size_t f = 0; f < c.kinds.size(); ++f) {
            if (static_cast<int>(polygons.size()) >= kMaxFaces) {
                g_capped = true;
                break;
            }
            V3 clipped[4];
            int count = View::ClipTriangleToNear(painter.camera, painter.frame, painter.nearZ, &c.points[f * 3], clipped);
            if (!count)
                continue;
            Polygon poly = {};
            poly.count = count;
            bool left = true, right = true, below = true, above = true;
            float depth = 0.0f;
            for (int k = 0; k < count; ++k) {
                float nx = 0.0f, ny = 0.0f;
                Project(painter.camera, painter.frame, painter.lens, clipped[k], painter.nearZ * 0.5f, &nx, &ny);
                left = left && nx < -1.0f, right = right && nx > 1.0f, below = below && ny < -1.0f;
                above = above && ny > 1.0f;
                NdcToPixel(painter.viewport, nx, ny, &poly.x[k], &poly.y[k]);
                depth += -Dot(clipped[k] - painter.camera, painter.frame.back);
            }
            if (left || right || below || above)
                continue;
            Kind kind = static_cast<Kind>(c.kinds[f]);
            poly.depth = depth / static_cast<float>(count);
            poly.argb = Colour(kind);
            polygons.push_back(poly);
            ++g_counts[c.kinds[f]];
        }
        if (g_capped)
            break;
    }
    std::sort(polygons.begin(), polygons.end(), [](const Polygon& a, const Polygon& b) { return a.depth > b.depth; });
    auto& under = painter.overlay->under;
    for (const Polygon& p : polygons) {
        under.push_back({p.x[0], p.y[0], p.x[1], p.y[1], p.x[2], p.y[2], p.argb});
        if (p.count == 4)
            under.push_back({p.x[0], p.y[0], p.x[2], p.y[2], p.x[3], p.y[3], p.argb});
    }
    // The editor's own kill / water triggers: their boxes outlined.
    for (const Triggers::Added& a : Triggers::AddedTriggers()) {
        if (!a.alive || a.spec.kind != Triggers::Kind::Kill)
            continue;
        OrientedBox box = Triggers::BoxOf(a.pos, a.yaw, a.spec.size);
        V3 corner[8];
        BoxCorners(box.origin, box.axes, box.min, box.max, corner);
        const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        uint32_t colour = (Colour(a.spec.water ? Kind::Water : Kind::Kill) & 0x00FFFFFF) | 0xFF000000;
        for (const auto& e : edges)
            painter.Line(corner[e[0]], corner[e[1]], colour, 2.0f);
    }
    // Grind rails: the rail manager's active segments near the camera.
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478);
    auto* rails = skate ? *reinterpret_cast<Obj::CRailManager**>(skate + 0x24) : nullptr;
    if (rails && !rails->m_onObject && rails->m_nodes) {
        int drawn = 0;
        for (int i = 0; i < rails->m_count; ++i) {
            const Obj::CRailNode& node = rails->m_nodes[i];
            if (!node.m_next || (node.m_flags & Obj::RAIL_FLAG_CLIMBING) || !(node.m_flags & Obj::RAIL_FLAG_ACTIVE))
                continue;
            V3 a = {node.m_pos.x, node.m_pos.y, node.m_pos.z}, b = {node.m_next->m_pos.x, node.m_next->m_pos.y,
                                                                   node.m_next->m_pos.z};
            if (painter.Distance(a) > kRadius && painter.Distance(b) > kRadius)
                continue;
            if (painter.Line(a, b, kRail, 3.0f))
                ++drawn;
        }
        g_railSegments = drawn;
    }
}

void Legend(float viewX, float viewY, float viewWidth, float viewHeight, float scale)
{
    if (!Shown())
        return;
    ImGui::SetNextWindowPos(ImVec2(viewX + viewWidth - 12.0f * scale, viewY + viewHeight - 12.0f * scale),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("##skateLegend", nullptr, flags)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("Skateability (Alt+K)");
    float box = ImGui::GetTextLineHeight();
    auto row = [&](uint32_t argb, const char* name, int count) {
        ImVec2 at = ImGui::GetCursorScreenPos();
        uint32_t abgr = (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFF) | ((argb & 0xFF) << 16);
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + box, at.y + box), abgr | 0xFF000000u);
        ImGui::Dummy(ImVec2(box, box));
        ImGui::SameLine();
        if (count >= 0)
            ImGui::Text("%s  %d", name, count);
        else
            ImGui::TextUnformatted(name);
    };
    for (int k = 1; k < kKinds; ++k)
        row(Colour(static_cast<Kind>(k)), Name(static_cast<Kind>(k)), g_counts[k]);
    row(kRail, "Grind rail (the rail manager's)", g_railSegments);
    ImGui::TextDisabled("Plain faces skatable up to normal y %.2f (the game's slope limit)", GameThreshold());
    if (g_capped)
        ImGui::TextDisabled("(the nearest %d faces only)", kMaxFaces);
    ImGui::End();
}

} // namespace Front::LevelEditor::SkateView

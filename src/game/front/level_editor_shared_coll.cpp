// NOT ORIGINAL - the level editor's shared collision (ours, added at the user's request). See
// level_editor_shared_coll.h.

#include "game/front/level_editor_shared_coll.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <string>

#include "game/core/checksums.h"
#include "game/core/mem.h"
#include "game/front/level_editor_scenery.h"
#include "game/gfx/sub_mesh.h"
#include "game/engine3d/coll_cache.h"
#include "game/engine3d/coll_grid.h"
#include "game/script/names.h"
#include "game/script/qb.h"
#include "game/sys/run_log.h"
#include "game/core/game_vtables.h"

namespace MenuSystem::LevelEditor::SharedCollision {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

namespace {

float LenOf(V3 v) { return std::sqrt(Dot(v, v)); }

float SegmentDistance(V3 p, V3 a, V3 b)
{
    V3 ab = b - a;
    float len2 = Dot(ab, ab);
    float t = len2 > 0.0f ? std::clamp(Dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return LenOf(p - (a + ab * t));
}

} // namespace

float PointTriangleDistance(V3 p, V3 a, V3 b, V3 c)
{
    V3 ab = b - a, ac = c - a;
    V3 n = Perpendicular(ab, ac);
    float n2 = Dot(n, n);
    float scale = std::max({Dot(ab, ab), Dot(ac, ac), Dot(c - b, c - b)});
    if (!(n2 > scale * scale * 1e-12f)) // (degenerate: a line or a point)
        return std::min({SegmentDistance(p, a, b), SegmentDistance(p, b, c), SegmentDistance(p, a, c)});
    // Inside the triangle's prism: the distance to its plane; else to the nearest edge.
    V3 ap = p - a, bp = p - b, cp = p - c;
    bool inside = Dot(Perpendicular(ab, ap), n) >= 0.0f && Dot(Perpendicular(c - b, bp), n) >= 0.0f && Dot(Perpendicular(a - c, cp), n) >= 0.0f;
    if (inside)
        return std::fabs(Dot(ap, n)) / std::sqrt(n2);
    return std::min({SegmentDistance(p, a, b), SegmentDistance(p, b, c), SegmentDistance(p, c, a)});
}

float StripDistance(V3 p, const uint8_t* positions, uint32_t stride, uint32_t vertexCount, const uint16_t* indices,
                    uint32_t indexCount, float best)
{
    auto at = [&](uint16_t i) {
        const float* f = reinterpret_cast<const float*>(positions + static_cast<size_t>(i) * stride);
        return V3{f[0], f[1], f[2]};
    };
    for (uint32_t i = 2; i < indexCount; ++i) {
        uint16_t a = indices[i - 2], b = indices[i - 1], c = indices[i];
        if (a == b || b == c || a == c || a >= vertexCount || b >= vertexCount || c >= vertexCount)
            continue;
        V3 va = at(a), vb = at(b), vc = at(c);
        // (a quick reject: the triangle's box farther than the best so far)
        float dx = std::max({std::min({va.x, vb.x, vc.x}) - p.x, p.x - std::max({va.x, vb.x, vc.x}), 0.0f});
        float dy = std::max({std::min({va.y, vb.y, vc.y}) - p.y, p.y - std::max({va.y, vb.y, vc.y}), 0.0f});
        float dz = std::max({std::min({va.z, vb.z, vc.z}) - p.z, p.z - std::max({va.z, vb.z, vc.z}), 0.0f});
        if (dx * dx + dy * dy + dz * dz >= best * best)
            continue;
        best = std::min(best, PointTriangleDistance(p, va, vb, vc));
    }
    return best;
}

bool Belongs(float own, float nearestOther) { return own <= kNear && own < nearestOther; }

FaceCopy ReadFace(const Engine3D::CTriMeshSource* geometry, int face)
{
    FaceCopy copy{};
    const uint8_t* data = geometry->m_faces + face * (geometry->m_smallFaces ? 8 : 10);
    std::memcpy(&copy.flags, data, 2);
    std::memcpy(&copy.terrain, data + 2, 2);
    for (int k = 0; k < 3; ++k) {
        uint32_t vertex = geometry->FaceVertex(face, k);
        Maths::Vec4 v;
        geometry->ReadStoredVertex(static_cast<int>(vertex), &v);
        copy.v[k] = {v.x, v.y, v.z};
        copy.bytes[k] = geometry->m_vertexBytes ? geometry->m_vertexBytes[vertex] : 0x80;
    }
    return copy;
}

Companion::Companion(uint32_t checksum, const std::vector<FaceCopy>& faces)
{
    size_t count = std::min(faces.size(), static_cast<size_t>(kMostFaces));
    m_faces.resize(count * 10);
    for (size_t f = 0; f < count; ++f) {
        const FaceCopy& face = faces[f];
        uint8_t* out = &m_faces[f * 10];
        std::memcpy(out, &face.flags, 2);
        std::memcpy(out + 2, &face.terrain, 2);
        for (int k = 0; k < 3; ++k) {
            // (corners shared where exactly equal, with the same byte)
            size_t index = m_base.size();
            for (size_t i = 0; i < m_base.size(); ++i) {
                if (m_base[i].x == face.v[k].x && m_base[i].y == face.v[k].y && m_base[i].z == face.v[k].z &&
                    m_bytes[i] == face.bytes[k]) {
                    index = i;
                    break;
                }
            }
            if (index == m_base.size()) {
                m_base.push_back(face.v[k]);
                m_bytes.push_back(face.bytes[k]);
            }
            uint16_t index16 = static_cast<uint16_t>(index);
            std::memcpy(out + 4 + 2 * k, &index16, 2);
        }
    }
    m_vertices.resize(m_base.size() * 3);
    m_rejection.assign(count ? count : 1, 0);
    if (m_bytes.empty())
        m_bytes.push_back(0);

    m_geometry.m_checksum = checksum;
    m_geometry.m_unknown04 = 0;
    m_geometry.m_numVertices = static_cast<uint16_t>(m_base.size());
    m_geometry.m_triCount = static_cast<uint16_t>(count);
    m_geometry.m_smallFaces = false;
    m_geometry.m_compressed = false;
    m_geometry.m_faces = m_faces.empty() ? nullptr : m_faces.data();
    m_geometry.m_vertices = m_vertices.empty() ? nullptr : m_vertices.data();
    m_geometry.m_bsp = nullptr;
    m_geometry.m_vertexBytes = m_bytes.data();
    m_geometry.m_unknown3C = 0; // (the rejection scale 0.0f: no face rejected by distance)

    m_entity.MakeNew(); // (the sector collision's vtable, no flags)
    m_entity.m_checksum = checksum;
    m_entity.m_visitStamp = 0;
    m_entity.m_geometry = &m_geometry;
    m_entity.m_octets = m_rejection.data();
    m_entity.m_unknown14 = 0;
    Place({0, {0.0f, 0.0f, 0.0f}});
}

void Companion::Place(const Placement& placement)
{
    for (size_t i = 0; i < m_base.size(); ++i) {
        V3 v = Apply(placement, m_base[i]);
        m_vertices[i * 3] = v.x;
        m_vertices[i * 3 + 1] = v.y;
        m_vertices[i * 3 + 2] = v.z;
    }
    UpdateBox();
}

void Companion::Rebase()
{
    for (size_t i = 0; i < m_base.size(); ++i)
        m_base[i] = Vertex(static_cast<int>(i));
}

V3 Companion::Vertex(int i) const
{
    return {m_vertices[i * 3], m_vertices[i * 3 + 1], m_vertices[i * 3 + 2]};
}

std::vector<FaceCopy> Companion::Faces() const
{
    std::vector<FaceCopy> faces(m_geometry.m_triCount);
    for (int f = 0; f < m_geometry.m_triCount; ++f)
        faces[f] = ReadFace(&m_geometry, f);
    return faces;
}

void Companion::ChangeLive(bool active)
{
    m_entity.m_flagBits = static_cast<uint16_t>(active ? m_entity.m_flagBits & ~4u : m_entity.m_flagBits | 4u);
}

bool Companion::LiveNow() const { return !(m_entity.m_flagBits & 4); }

void Companion::ChangeTriFlags(int face, uint16_t flags)
{
    if (face >= 0 && face < m_geometry.m_triCount)
        std::memcpy(&m_faces[static_cast<size_t>(face) * 10], &flags, 2);
}

void Companion::UpdateBox()
{
    // (as the loaded geometries keep it: w = 1 - Translate moves w by the delta's 0)
    V3 lo = m_base.empty() ? V3{0, 0, 0} : Vertex(0), hi = lo;
    for (size_t i = 1; i < m_base.size(); ++i) {
        V3 v = Vertex(static_cast<int>(i));
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    m_geometry.m_lowerLimit = {lo.x, lo.y, lo.z, 1.0f};
    m_geometry.m_upperLimit = {hi.x, hi.y, hi.z, 1.0f};
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

uint8_t* GeomOf(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x14); }
Engine3D::CStaticCollider* CollOf(uint8_t* sector) { return *reinterpret_cast<Engine3D::CStaticCollider**>(sector + 0x18); }
void* GridOf(uint8_t* scene) { return *reinterpret_cast<void**>(scene + 0xD4); }

// A face of a collision-only sector turned off for a companion, and its flags before.
struct Taken {
    Engine3D::CTriMeshSource* geometry;
    int face;
    uint16_t flags;
};

struct TableRow {
    uint8_t* scene = nullptr;
    uint8_t* sector = nullptr;
    uint8_t* geom = nullptr;
    void* grid = nullptr;
    std::unique_ptr<Companion> companion; // (null: nothing belongs to it)
    std::vector<Taken> taken;
    bool inGrid = false;
    bool moved = false;
};
std::map<uint32_t, TableRow> g_entries;
uint32_t g_generation; // (faces taken or given back: the collision view's cache made again)

bool IsSwitchedOn()
{
    static const bool enabled = ConfigInt("editor", "shared_collision", 1) != 0;
    return enabled;
}

// A piece's triangles as drawn: its meshes' first index buffer as a strip (billboards left out, as picking does).
template <typename F>
void ForEachStrip(uint8_t* geom, F fn)
{
    auto** meshes = *reinterpret_cast<Engine3D::meshRef***>(geom + 0x3C);
    uint32_t count = *reinterpret_cast<uint32_t*>(geom + 0x40);
    for (uint32_t i = 0; meshes && i < count && i < 4096; ++i) {
        Engine3D::meshRef* mesh = meshes[i];
        if (!mesh || mesh->billboard || (mesh->flags & 0x10))
            continue;
        Engine3D::sVertexBuffer* vertices = mesh->vertexBufferCount ? mesh->vertexBuffers[0] : nullptr;
        Engine3D::sIdxStore* indices = mesh->indexBuffers[0];
        if (!vertices || !vertices->data || !indices || !indices->indices || mesh->stride < 12)
            continue;
        fn(vertices->data, static_cast<uint32_t>(mesh->stride), static_cast<uint32_t>(mesh->vertexCount),
           indices->indices, static_cast<uint32_t>(mesh->indexCounts[0]));
    }
}

float GeomDistance(uint8_t* geom, V3 p, float best)
{
    ForEachStrip(geom, [&](const uint8_t* positions, uint32_t stride, uint32_t vertexCount, const uint16_t* indices,
                           uint32_t indexCount) {
        best = StripDistance(p, positions, stride, vertexCount, indices, indexCount, best);
    });
    return best;
}

// The farthest of a face's corners from a piece, found no farther than `limit` (past it: the limit's next float).
float FaceDistance(uint8_t* geom, const FaceCopy& face, float limit)
{
    float worst = 0.0f;
    float over = std::nextafter(limit, std::numeric_limits<float>::infinity());
    for (const V3& v : face.v) {
        float d = GeomDistance(geom, v, over);
        if (d > limit)
            return over;
        worst = std::max(worst, d);
    }
    return worst;
}

bool Inside(V3 p, V3 min, V3 max)
{
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
}
bool FaceInside(const FaceCopy& face, V3 min, V3 max)
{
    return Inside(face.v[0], min, max) && Inside(face.v[1], min, max) && Inside(face.v[2], min, max);
}

// Where a piece is now, grown by `grow` (false: no box).
bool Bounds(uint32_t checksum, float grow, V3* min, V3* max)
{
    OrientedBox box;
    if (!Scenery::Cuboid(checksum, &box))
        return false;
    BoundsOf(box, min, max);
    *min = *min - V3{grow, grow, grow};
    *max = *max + V3{grow, grow, grow};
    return true;
}

// A collision-only sector that can give faces: no geometry, collision on with geometry.
Engine3D::CTriMeshSource* CollisionOnly(uint8_t* sector)
{
    if (!sector || GeomOf(sector))
        return nullptr;
    Engine3D::CStaticCollider* coll = CollOf(sector);
    if (!coll || (coll->m_flagBits & 6) || !coll->m_geometry || !coll->m_geometry->m_faces)
        return nullptr;
    return coll->m_geometry;
}

const char* NameOf(uint32_t checksum)
{
    if (const char* name = Scripts::KeptQbName(checksum))
        return name;
    return Scripts::LookUpHashName(checksum);
}

// The collision objects named after a piece (<name>_col and the like) that exist as collision-only sectors.
std::vector<uint32_t> NamedFor(uint32_t checksum)
{
    static const char* const kSuffixes[] = {"_col", "_ic", "_col01", "_ic01"};
    std::vector<uint32_t> found;
    const char* name = NameOf(checksum);
    if (!name || !*name)
        return found;
    uint32_t base = Checksums::HashFromText(name);
    if (base != checksum) // (a name that isn't the piece's own)
        return found;
    for (const char* suffix : kSuffixes) {
        uint32_t named = Checksums::FoldTextIntoChecksum(base, suffix);
        if (CollisionOnly(Scenery::SectorOf(named)))
            found.push_back(named);
    }
    return found;
}

// Every collision object named after some piece of the level (kept per level: its main scene and sector count).
const std::map<uint32_t, uint32_t>& NamedObjects()
{
    static std::map<uint32_t, uint32_t> named;
    static uint8_t* scene = nullptr;
    static int sectors = -1;
    int count = 0;
    Scenery::ForEachLevelSector([](void* context, uint32_t, uint8_t*) { ++*static_cast<int*>(context); }, &count);
    if (scene == Scenery::MainScene() && sectors == count)
        return named;
    scene = Scenery::MainScene();
    sectors = count;
    named.clear();
    std::vector<uint32_t> pieces;
    Scenery::ForEachLevelSector(
        [](void* context, uint32_t checksum, uint8_t* sector) {
            if (GeomOf(sector))
                static_cast<std::vector<uint32_t>*>(context)->push_back(checksum);
        },
        &pieces);
    for (uint32_t piece : pieces) {
        for (uint32_t object : NamedFor(piece))
            named.emplace(object, piece);
    }
    return named;
}

struct Other {
    uint32_t checksum;
    uint8_t* geom;
    V3 min, max;
};

// The faces that belong to piece `checksum` (see the header) and, when `taken` is given, where they came from.
std::vector<FaceCopy> Gather(uint32_t checksum, std::vector<Taken>* taken)
{
    std::vector<FaceCopy> faces;
    uint8_t* sector = Scenery::SectorOf(checksum);
    uint8_t* geom = sector ? GeomOf(sector) : nullptr;
    V3 min, max;
    if (!geom || Scenery::SeeThrough(checksum) || !Bounds(checksum, kNear, &min, &max))
        return faces;
    const auto& namedObjects = NamedObjects();
    std::vector<uint32_t> mine = NamedFor(checksum);

    // The other solid pieces near it (the editor's copies left out: they'd tie with their sources).
    struct CtxInfo {
        uint32_t self;
        V3 min, max;
        std::vector<Other> others;
        std::vector<std::pair<uint32_t, uint8_t*>> sources;
    } context{checksum, min, max, {}, {}};
    Scenery::ForEachLevelSector(
        [](void* raw, uint32_t other, uint8_t* sector) {
            auto* c = static_cast<CtxInfo*>(raw);
            if (CollisionOnly(sector)) {
                c->sources.push_back({other, sector});
                return;
            }
            uint8_t* geom = GeomOf(sector);
            if (other == c->self || !geom || Scenery::IsClone(other) || Scenery::SeeThrough(other))
                return;
            V3 lo, hi;
            if (!Bounds(other, kNear, &lo, &hi))
                return;
            if (lo.x > c->max.x || hi.x < c->min.x || lo.y > c->max.y || hi.y < c->min.y || lo.z > c->max.z ||
                hi.z < c->min.z)
                return;
            c->others.push_back({other, geom, lo, hi});
        },
        &context);

    for (const auto& source : context.sources) {
        Engine3D::CTriMeshSource* geometry = CollisionOnly(source.second);
        const Maths::Vec4& lo = geometry->m_lowerLimit;
        const Maths::Vec4& hi = geometry->m_upperLimit;
        if (lo.x > max.x || hi.x < min.x || lo.y > max.y || hi.y < min.y || lo.z > max.z || hi.z < min.z)
            continue;
        bool named = std::find(mine.begin(), mine.end(), source.first) != mine.end();
        if (!named && namedObjects.count(source.first))
            continue; // (named after another piece: that one's)
        V3 namedMin = min - V3{100.0f, 100.0f, 100.0f}, namedMax = max + V3{100.0f, 100.0f, 100.0f};
        for (int f = 0; f < geometry->m_triCount && faces.size() < static_cast<size_t>(kMostFaces); ++f) {
            FaceCopy face = ReadFace(geometry, f);
            if (!Takeable(face.flags))
                continue;
            if (named) {
                if (!FaceInside(face, namedMin, namedMax))
                    continue;
            } else {
                if (!FaceInside(face, min, max))
                    continue;
                float own = FaceDistance(geom, face, kNear);
                if (!(own <= kNear))
                    continue;
                float nearest = std::numeric_limits<float>::infinity();
                for (const Other& other : context.others) {
                    if (!FaceInside(face, other.min, other.max))
                        continue;
                    nearest = std::min(nearest, FaceDistance(other.geom, face, own));
                    if (!Belongs(own, nearest))
                        break;
                }
                if (!Belongs(own, nearest))
                    continue;
            }
            faces.push_back(face);
            if (taken)
                taken->push_back({geometry, f, face.flags});
        }
    }
    return faces;
}

// A list of collision objects for CCollGrid::TickFrame (ListKit::Foremost / ListKit::Elem<CHitMesh>, on the stack: Update only reads
// them).
struct GridList {
    MemSys::ListNode head;
    std::vector<MemSys::ListNode> nodes;

    explicit GridList(const std::vector<Engine3D::CHitMesh*>& objects) : nodes(objects.size())
    {
        head.vtable = Vt::kCollGridHead;
        head.data = reinterpret_cast<void*>(-1);
        head.priority = 0;
        head.next = head.prev = &head;
        for (size_t i = 0; i < objects.size(); ++i) {
            MemSys::ListNode& node = nodes[i];
            node.vtable = Vt::kCollGridNode;
            node.data = objects[i];
            node.priority = 0;
            node.prev = head.prev;
            node.next = &head;
            head.prev->next = &node;
            head.prev = &node;
        }
    }
    GridList(const GridList&) = delete;
};

// The grid brought up to date for these objects (the top-down heap current, as UpdateSuperSectors has it).
void TickGrid(void* grid, const std::vector<Engine3D::CHitMesh*>& added, const std::vector<Engine3D::CHitMesh*>& removed,
                const std::vector<Engine3D::CHitMesh*>& moved)
{
    if (!grid || (added.empty() && removed.empty() && moved.empty()))
        return;
    GridList addedList(added), removedList(removed), movedList(moved);
    MemSys::EnterHeapScope(MemSys::TopDownHeap());
    static_cast<Engine3D::CCollGrid*>(grid)->TickFrame(&addedList.head, &removedList.head, &movedList.head);
    MemSys::LeaveHeapScope();
}

bool Live(const TableRow& entry, uint32_t checksum)
{
    return Scenery::SceneLive(entry.scene) && GridOf(entry.scene) == entry.grid &&
           Scenery::SectorOf(checksum) == entry.sector && GeomOf(entry.sector) == entry.geom;
}

// An entry dropped: out of its grid and the originals back on while its piece is still there (a level gone: its grid
// and geometries are gone with it - nothing touched); out of every collision cache.
void Drop(std::map<uint32_t, TableRow>::iterator it)
{
    TableRow& entry = it->second;
    if (entry.companion) {
        if (Live(entry, it->first)) {
            if (entry.inGrid)
                TickGrid(entry.grid, {}, {entry.companion->Object()}, {});
            for (const Taken& t : entry.taken)
                *reinterpret_cast<uint16_t*>(t.geometry->m_faces + t.face * (t.geometry->m_smallFaces ? 8 : 10)) = t.flags;
        }
        Engine3D::RemoveFromCaches(entry.companion->Object());
        ++g_generation;
    }
    g_entries.erase(it);
}

TableRow* Find(uint32_t checksum)
{
    auto it = g_entries.find(checksum);
    if (it == g_entries.end())
        return nullptr;
    if (!Live(it->second, checksum)) {
        Drop(it);
        return nullptr;
    }
    return &it->second;
}

void AppendOne(uint32_t checksum, uint8_t* scene, uint8_t* sector, uint8_t* geom, std::vector<FaceCopy> faces,
         std::vector<Taken> taken)
{
    TableRow& entry = g_entries[checksum];
    entry.scene = scene;
    entry.sector = sector;
    entry.geom = geom;
    entry.grid = GridOf(scene);
    if (!faces.empty()) {
        entry.companion = std::make_unique<Companion>(checksum, faces);
        entry.companion->Object()->m_geometry->SetUpAllFaces(); // (the shared no-tree face list, 0x0067C6B8)
        for (const Taken& t : taken)
            *reinterpret_cast<uint16_t*>(t.geometry->m_faces + t.face * (t.geometry->m_smallFaces ? 8 : 10)) =
                static_cast<uint16_t>(t.flags | 0x10);
        entry.taken = std::move(taken);
        ++g_generation;
    }
}

} // namespace

void Ensure(uint32_t checksum, uint8_t* scene, uint8_t* sector, uint8_t* geom)
{
    if (!IsSwitchedOn() || Find(checksum))
        return;
    std::vector<Taken> taken;
    std::vector<FaceCopy> faces = Gather(checksum, &taken);
    if (DebugEnabled() || !faces.empty())
        Log("level editor: shared collision of %08X: %u face%s taken along", checksum, static_cast<unsigned>(faces.size()),
            faces.size() == 1 ? "" : "s");
    AppendOne(checksum, scene, sector, geom, std::move(faces), std::move(taken));
}

void Place(uint32_t checksum, const Placement& placement)
{
    TableRow* entry = Find(checksum);
    if (!entry || !entry->companion)
        return;
    entry->companion->Place(placement);
    entry->moved = true;
}

void Rebase(uint32_t checksum)
{
    TableRow* entry = Find(checksum);
    if (entry && entry->companion)
        entry->companion->Rebase();
}

void ChangeLive(uint32_t checksum, bool active)
{
    TableRow* entry = Find(checksum);
    if (entry && entry->companion)
        entry->companion->ChangeLive(active);
}

std::shared_ptr<std::vector<FaceCopy>> PrepareCopy(uint32_t source)
{
    if (!IsSwitchedOn())
        return nullptr;
    if (TableRow* entry = Find(source))
        return entry->companion ? std::make_shared<std::vector<FaceCopy>>(entry->companion->Faces()) : nullptr;
    auto faces = std::make_shared<std::vector<FaceCopy>>(Gather(source, nullptr));
    return faces->empty() ? nullptr : faces;
}

void FinishCopy(const std::shared_ptr<std::vector<FaceCopy>>& faces, uint32_t copy, uint8_t* scene, uint8_t* sector,
                uint8_t* geom)
{
    if (!faces || faces->empty() || !scene || !sector || !geom)
        return;
    auto it = g_entries.find(copy);
    if (it != g_entries.end())
        Drop(it);
    AppendOne(copy, scene, sector, geom, *faces, {});
    if (DebugEnabled())
        Log("level editor: shared collision of copy %08X: %u faces", copy, static_cast<unsigned>(faces->size()));
}

void Finalise(uint8_t* scene)
{
    std::vector<Engine3D::CHitMesh*> added, moved;
    void* grid = nullptr;
    for (auto it = g_entries.begin(); it != g_entries.end();) {
        auto next = std::next(it);
        if (!Live(it->second, it->first)) {
            Drop(it);
        } else if (it->second.scene == scene && it->second.companion) {
            TableRow& entry = it->second;
            grid = entry.grid;
            if (!entry.inGrid)
                added.push_back(entry.companion->Object());
            else if (entry.moved)
                moved.push_back(entry.companion->Object());
            entry.inGrid = true;
            entry.moved = false;
        }
        it = next;
    }
    TickGrid(grid, added, {}, moved);
}

int TotalOf(int* faces)
{
    int pieces = 0, total = 0;
    for (const auto& entry : g_entries) {
        if (entry.second.companion) {
            ++pieces;
            total += entry.second.companion->FaceCount();
        }
    }
    if (faces)
        *faces = total;
    return pieces;
}

void ForEachCompanion(void (*each)(void* context, uint32_t checksum, const Companion& companion), void* context)
{
    for (auto it = g_entries.begin(); it != g_entries.end();) {
        auto next = std::next(it);
        if (!Live(it->second, it->first))
            Drop(it); // (its level gone: forgotten, nothing touched)
        else if (it->second.companion && it->second.companion->LiveNow())
            each(context, it->first, *it->second.companion);
        it = next;
    }
}

std::vector<int> TakenFaces(const Engine3D::CTriMeshSource* geometry)
{
    std::vector<int> faces;
    for (const auto& entry : g_entries) {
        for (const Taken& t : entry.second.taken) {
            if (t.geometry == geometry)
                faces.push_back(t.face);
        }
    }
    return faces;
}

uint32_t Generation() { return g_generation; }

bool TakenFace(const Engine3D::CTriMeshSource* geometry, int face, uint32_t* piece, int* index, uint16_t* flags)
{
    for (const auto& entry : g_entries) {
        const std::vector<Taken>& taken = entry.second.taken;
        for (size_t i = 0; i < taken.size(); ++i) {
            if (taken[i].geometry == geometry && taken[i].face == face) {
                if (piece)
                    *piece = entry.first;
                if (index)
                    *index = static_cast<int>(i);
                if (flags)
                    *flags = taken[i].flags;
                return true;
            }
        }
    }
    return false;
}

bool OriginOf(uint32_t piece, int index, Engine3D::CTriMeshSource** geometry, int* face)
{
    TableRow* entry = Find(piece);
    // (the companion's faces are the taken ones, in order - Gather / Add; a copy's took none)
    if (!entry || !entry->companion || index < 0 || index >= static_cast<int>(entry->taken.size()))
        return false;
    *geometry = entry->taken[index].geometry;
    *face = entry->taken[index].face;
    return true;
}

const Companion* CompanionOf(uint32_t piece)
{
    TableRow* entry = Find(piece);
    return entry ? entry->companion.get() : nullptr;
}

bool SetTakenFlags(const Engine3D::CTriMeshSource* geometry, int face, uint16_t flags)
{
    for (auto& entry : g_entries) {
        std::vector<Taken>& taken = entry.second.taken;
        for (size_t i = 0; i < taken.size(); ++i) {
            if (taken[i].geometry != geometry || taken[i].face != face)
                continue;
            taken[i].flags = flags;
            if (entry.second.companion)
                entry.second.companion->ChangeTriFlags(static_cast<int>(i), flags);
            *reinterpret_cast<uint16_t*>(taken[i].geometry->m_faces +
                                         taken[i].face * (taken[i].geometry->m_smallFaces ? 8 : 10)) =
                static_cast<uint16_t>(flags | 0x10);
            ++g_generation;
            return true;
        }
    }
    return false;
}

} // namespace MenuSystem::LevelEditor::SharedCollision

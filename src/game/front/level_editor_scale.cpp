// NOT ORIGINAL - the level editor's scaling of scenery and objects (ours, added at the user's request). See
// level_editor_scale.h.

#include "game/front/level_editor_scale.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/gfx/mesh.h"
#include "game/gfx/model.h"
#include "game/nx/coll_movable.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "game/obj/collision_component.h"
#include "game/obj/object.h"
#include "game/core/code_address.h"
#include "game/sys/log.h"
#include "game/core/game_vtables.h"
#include "game/nx/coll_line.h"
#include "game/mc/memcard.h"
#include "game/nx/feeler.h"
#include "game/gfx/viewport.h"
#include "game/core/stubs.h"

namespace MenuSystem::LevelEditor::StretchBy {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

void ScaleAbout(Placement* placement, V3 pivot, V3 factor)
{
    // x'' = F (x' - pivot) + pivot, x' = Q S x + offset: x'' = Q (F' S) x + F (offset - pivot) + pivot, F' = F as seen
    // through Q (F Q = Q F': odd turns swap its x and z).
    placement->scale = PerAxis(TurnedScale(factor, placement->quarters), placement->scale);
    placement->offset = PerAxis(placement->offset - pivot, factor) + pivot;
}

namespace {

// One axis of AllowedFactor.
double AllowedOnAxis(double factor, double low, double high)
{
    if (factor > 1.0 && high < factor)
        factor = std::max(1.0, high); // (already past the limit: no further)
    if (factor < 1.0 && low > factor)
        factor = std::min(1.0, low);
    return factor;
}

float Axis(V3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

} // namespace

V3 AllowedFactor(V3 factor, const std::vector<V3>& scales, const std::vector<V3>& limits, bool uniform)
{
    double out[3];
    for (int a = 0; a < 3; ++a) {
        double f = Axis(factor, a);
        if (!(f > 0.0) || !std::isfinite(f)) {
            out[a] = 1.0;
            continue;
        }
        double low = 0.0, high = 1e30;
        for (size_t i = 0; i < scales.size(); ++i) {
            double scale = Axis(scales[i], a) > 0.0f ? Axis(scales[i], a) : 1.0;
            double limit = i < limits.size() ? std::min<double>(Axis(limits[i], a), kMax) : kMax;
            low = std::max(low, kMin / scale);
            high = std::min(high, limit / scale);
        }
        out[a] = AllowedOnAxis(f, low, high);
    }
    if (uniform) {
        // (the factor asked for is the same on every axis: the one nearest 1 is what all of them allow)
        double f = Axis(factor, 0);
        double common = out[0];
        for (int a = 1; a < 3; ++a)
            common = f >= 1.0 ? std::min(common, out[a]) : std::max(common, out[a]);
        out[0] = out[1] = out[2] = common;
    }
    return {static_cast<float>(out[0]), static_cast<float>(out[1]), static_cast<float>(out[2])};
}

float CompressedLimit(float extent)
{
    if (!(extent > 0.0f))
        return kMax;
    return std::min(kMax, kCompressedExtent / extent);
}

float Confine(float scale)
{
    if (!(scale == scale))
        return 1.0f;
    return std::max(kMin, std::min(kMax, scale));
}

int MainAxis(V3 d)
{
    float x = std::fabs(d.x), y = std::fabs(d.y), z = std::fabs(d.z);
    if (y >= x && y >= z)
        return 1;
    return x >= z ? 0 : 2;
}

V3 AlongAxis(int axis, float f) { return {axis == 0 ? f : 1.0f, axis == 1 ? f : 1.0f, axis == 2 ? f : 1.0f}; }

int NearestAxis(const V3 axes[3], int worldAxis)
{
    int best = worldAxis;
    float bestDot = -1.0f;
    for (int i = 0; i < 3; ++i) {
        float length = std::sqrt(Dot(axes[i], axes[i]));
        float d = length > 0.0f ? std::fabs(Axis(axes[i], worldAxis)) / length : 0.0f;
        if (d > bestDot) {
            bestDot = d;
            best = i;
        }
    }
    return best;
}

std::string Describe(V3 scale)
{
    char text[48];
    if (IsUniformScale(scale))
        std::snprintf(text, sizeof(text), "%.2fx", scale.x);
    else
        std::snprintf(text, sizeof(text), "%.2f x %.2f x %.2f", scale.x, scale.y, scale.z);
    return text;
}

RebuildFrame FrameFor(int copiedQuarters, V3 copiedPosition, int quarters0, int quarters, V3 position, V3 scale)
{
    // The copy was taken after `copied` quarter turns of the piece (scale 1): copy - from = Q_c (x - x0); now
    // x' - to = Q S (x - x0) = Q_t (Q_c S Q_c^-1) (copy - from), Q_t the turns since, Q_c S Q_c^-1 S seen through Q_c.
    int copied = (copiedQuarters - quarters0) & 3;
    return {copiedPosition, position, (quarters - copiedQuarters) & 3, TurnedScale(scale, copied), (quarters - quarters0) & 3};
}

V3 Rebuilt(V3 p, V3 from, V3 to, int quarters, V3 scale)
{
    return to + QuarterTurn(PerAxis(p - from, scale), quarters);
}

void RewritePositions(uint8_t* data, uint32_t stride, uint32_t count, const float* original, V3 from, V3 to,
                      int quarters, V3 scale)
{
    for (uint32_t v = 0; v < count; ++v, data += stride, original += 3) {
        V3 p = Rebuilt({original[0], original[1], original[2]}, from, to, quarters, scale);
        auto* out = reinterpret_cast<float*>(data);
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
    }
}

V3 ScaledNormal(V3 n, V3 scale, int quarters)
{
    if (!IsUnitScale(scale)) {
        V3 m = {n.x / scale.x, n.y / scale.y, n.z / scale.z};
        float length = std::sqrt(Dot(m, m));
        n = length > 0.0f ? m * (1.0f / length) : m;
    }
    return QuarterTurn(n, quarters);
}

void RewriteNormals(uint8_t* data, uint32_t stride, uint32_t count, uint32_t offset, const float* original, V3 scale,
                    int quarters)
{
    for (uint32_t v = 0; v < count; ++v, data += stride, original += 3) {
        V3 n = ScaledNormal({original[0], original[1], original[2]}, scale, quarters);
        auto* out = reinterpret_cast<float*>(data + offset);
        out[0] = n.x;
        out[1] = n.y;
        out[2] = n.z;
    }
}

int SurfaceNudge(uint32_t fvf, uint32_t stride)
{
    constexpr uint32_t kPositionMask = 0x400E, kXyz = 0x002, kNormal = 0x010;
    // (an FVF's texture coordinate size bits are 0 for the scene's 2D coordinates: a larger value is a shader handle)
    if (fvf >= 0x10000 || (fvf & kPositionMask) != kXyz || !(fvf & kNormal) || stride < 24)
        return -1;
    return 12;
}

std::string Token(V3 scale)
{
    if (IsUnitScale(scale))
        return "";
    char text[64];
    if (IsUniformScale(scale))
        std::snprintf(text, sizeof(text), " scale=%.9g", scale.x);
    else
        std::snprintf(text, sizeof(text), " scale=%.9g,%.9g,%.9g", scale.x, scale.y, scale.z);
    return text;
}

bool ReadToken(const std::string& field, V3* scale)
{
    if (field.compare(0, 6, "scale=") != 0 || field.size() == 6)
        return false;
    float values[3];
    const char* at = field.c_str() + 6;
    // (one number, or exactly three separated by commas)
    for (int count = 0;;) {
        char* end = nullptr;
        float value = std::strtof(at, &end);
        if (end == at || !(value > 0.0f) || !std::isfinite(value))
            return false;
        values[count++] = value;
        if (*end == '\0') {
            if (count == 1)
                *scale = Uniform(values[0]);
            else if (count == 3)
                *scale = {values[0], values[1], values[2]};
            return count == 1 || count == 3;
        }
        if (*end != ',' || count == 3)
            return false;
        at = end + 1;
    }
}

// --- the collision's copy ---------------------------------------------------------------------------------------

namespace {

void SplitNodes(Engine3D::CHitTreeNode* node, std::vector<CollisionCopy::Elem>* out, int depth)
{
    // (iterative down the greater side, as the game's own walks; a guard against a broken tree)
    while (node && !node->IsEndNode() && depth < 256) {
        out->push_back({node, node->m_axisTag, node->m_chainLink});
        SplitNodes(node->LessChild(), out, depth + 1);
        node = node->GreaterChild();
        ++depth;
    }
}

uint32_t VertexBytes(const Engine3D::CTriMeshSource* data) { return data->m_numVertices * (data->m_compressed ? 6u : 12u); }

} // namespace

CollisionCopy CopyCollision(Engine3D::CTriMeshSource* data)
{
    CollisionCopy copy;
    copy.data = data;
    if (!data)
        return copy;
    copy.numVertices = data->m_numVertices;
    copy.compressed = data->m_compressed;
    copy.vertexArray = data->m_vertices;
    if (data->m_vertices) {
        const auto* bytes = static_cast<const uint8_t*>(data->m_vertices);
        copy.vertices.assign(bytes, bytes + VertexBytes(data));
    }
    copy.min = data->m_lowerLimit;
    copy.max = data->m_upperLimit;
    copy.rejection = data->m_unknown3C;
    SplitNodes(data->m_bsp, &copy.nodes, 0);
    return copy;
}

bool SameCollision(const CollisionCopy& copy)
{
    const Engine3D::CTriMeshSource* data = copy.data;
    return data && data->m_vertices == copy.vertexArray && data->m_numVertices == copy.numVertices &&
           data->m_compressed == copy.compressed && copy.vertices.size() == VertexBytes(data);
}

void RebuildCollision(const CollisionCopy& copy, int quarters, V3 from, V3 to, V3 scale)
{
    Engine3D::CTriMeshSource* data = copy.data;
    if (!data || !data->m_vertices)
        return;
    std::memcpy(data->m_vertices, copy.vertices.data(), copy.vertices.size());
    data->m_lowerLimit = copy.min;
    data->m_upperLimit = copy.max;
    data->m_unknown3C = copy.rejection;
    for (const CollisionCopy::Elem& n : copy.nodes) {
        n.node->m_axisTag = n.split;
        n.node->m_chainLink = n.link;
    }
    Maths::Vec4 centre = {from.x, from.y, from.z, 0.0f};
    if (!IsUnitScale(scale)) {
        Maths::Vec4 factor = {scale.x, scale.y, scale.z, 1.0f};
        data->StretchBy(&centre, &factor);
    }
    if (quarters & 3)
        data->RotateY(&centre, quarters & 3);
    V3 delta = to - from;
    if (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f) {
        Maths::Vec4 by = {delta.x, delta.y, delta.z, 0.0f};
        data->Slide(&by);
    }
}

V3 CollisionLimit(const Engine3D::CTriMeshSource* data, V3 scale)
{
    V3 limit = Uniform(kMax);
    if (!data || !data->m_compressed)
        return limit;
    if (scale.x > 0.0f)
        limit.x = CompressedLimit((data->m_upperLimit.x - data->m_lowerLimit.x) / scale.x);
    if (scale.y > 0.0f)
        limit.y = CompressedLimit((data->m_upperLimit.y - data->m_lowerLimit.y) / scale.y);
    if (scale.z > 0.0f)
        limit.z = CompressedLimit((data->m_upperLimit.z - data->m_lowerLimit.z) / scale.z);
    return limit;
}

// --- the game side: scenery -------------------------------------------------------------------------------------

namespace {

// A mesh's copy: each vertex buffer's positions (and normals, when its format has them), its bounding sphere and its
// position list (+0xAC / +0xB0 - moved with the mesh by meshRef::MoveTo, not turned by TurnY: rewritten the same way,
// scaled).
struct MeshCopy {
    Engine3D::meshRef* mesh;
    uint16_t vertexCount;
    uint8_t stride;
    int normalOffset;
    std::vector<Engine3D::sVertexBuffer*> buffers;
    std::vector<uint8_t*> data;
    std::vector<std::vector<float>> positions; // (per buffer: x, y, z a vertex)
    std::vector<std::vector<float>> normals;   // (per buffer, when normalOffset >= 0)
    V3 centre;
    float radius;
    uint32_t listCount;
    float* list;
    std::vector<float> listCopy;
};

uint8_t* CollOf(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x18); }
Engine3D::CTriMeshSource* CollisionData(uint8_t* sector)
{
    auto* coll = reinterpret_cast<Engine3D::CHitMesh*>(CollOf(sector));
    return coll ? coll->m_geometry : nullptr;
}

// The mesh's vertex format (+0x38: a Direct3D FVF, or a built-in vertex shader's handle).
uint32_t VertexFormat(const Engine3D::meshRef* mesh) { return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(mesh) + 0x38); }

// The vertex buffer written back to the card (0x004D40E0 after the inlined lock of the whole buffer, as meshRef::MoveTo).
void Upload(Engine3D::sVertexBuffer* buffer)
{
    buffer->lockOffset = 0;
    buffer->lockSize = buffer->size;
    buffer->lockFlags = 0;
    buffer->Unlock();
}

// The largest of a per-axis scale (a sphere's radius grows by it).
float Largest(V3 s) { return std::max({s.x, s.y, s.z}); }

} // namespace

struct Original {
    uint8_t* geom;
    Engine3D::meshRef** meshList;
    uint32_t meshCount;
    int quarters;
    V3 position;
    std::vector<MeshCopy> meshes;
    bool haveCollision;
    CollisionCopy collision;
};

std::shared_ptr<Original> Capture(uint8_t* sector, uint8_t* geom, int quarters, V3 position)
{
    auto original = std::make_shared<Original>();
    original->geom = geom;
    original->meshList = *reinterpret_cast<Engine3D::meshRef***>(geom + 0x3C);
    original->meshCount = *reinterpret_cast<uint32_t*>(geom + 0x40);
    original->quarters = quarters;
    original->position = position;
    if (!original->meshList || original->meshCount == 0 || original->meshCount > 4096)
        return nullptr;
    for (uint32_t i = 0; i < original->meshCount; ++i) {
        Engine3D::meshRef* mesh = original->meshList[i];
        if (!mesh || mesh->stride < 12 || mesh->vertexBufferCount > 3)
            return nullptr;
        MeshCopy copy = {mesh, mesh->vertexCount, mesh->stride, SurfaceNudge(VertexFormat(mesh), mesh->stride),
                         {}, {}, {}, {}, {mesh->centre[0], mesh->centre[1], mesh->centre[2]}, mesh->radius,
                         mesh->positionCount, mesh->positions, {}};
        for (int b = 0; b < mesh->vertexBufferCount; ++b) {
            Engine3D::sVertexBuffer* buffer = mesh->vertexBuffers[b];
            if (!buffer || !buffer->data || buffer->size < static_cast<uint32_t>(mesh->vertexCount) * mesh->stride)
                return nullptr;
            std::vector<float> positions(3u * mesh->vertexCount);
            std::vector<float> normals(copy.normalOffset >= 0 ? 3u * mesh->vertexCount : 0u);
            const uint8_t* vertex = buffer->data;
            for (uint32_t v = 0; v < mesh->vertexCount; ++v, vertex += mesh->stride) {
                std::memcpy(&positions[3u * v], vertex, 12);
                if (copy.normalOffset >= 0)
                    std::memcpy(&normals[3u * v], vertex + copy.normalOffset, 12);
            }
            copy.buffers.push_back(buffer);
            copy.data.push_back(buffer->data);
            copy.positions.push_back(std::move(positions));
            copy.normals.push_back(std::move(normals));
        }
        if (mesh->positions && mesh->positionCount)
            copy.listCopy.assign(mesh->positions, mesh->positions + 3u * mesh->positionCount);
        original->meshes.push_back(std::move(copy));
    }
    Engine3D::CTriMeshSource* data = CollisionData(sector);
    original->haveCollision = data && data->m_vertices;
    if (original->haveCollision)
        original->collision = CopyCollision(data);
    return original;
}

bool Reassemble(const Original& original, uint8_t* sector, uint8_t* geom, int quarters, V3 position, V3 scale,
             int quarters0)
{
    // (still the sector copied: its meshes, buffers and collision where and as they were)
    if (geom != original.geom || *reinterpret_cast<Engine3D::meshRef***>(geom + 0x3C) != original.meshList ||
        *reinterpret_cast<uint32_t*>(geom + 0x40) != original.meshCount)
        return false;
    for (uint32_t i = 0; i < original.meshCount; ++i) {
        const MeshCopy& copy = original.meshes[i];
        Engine3D::meshRef* mesh = original.meshList[i];
        if (mesh != copy.mesh || mesh->vertexCount != copy.vertexCount || mesh->stride != copy.stride ||
            mesh->vertexBufferCount != copy.buffers.size() || mesh->positions != copy.list ||
            mesh->positionCount != copy.listCount)
            return false;
        for (size_t b = 0; b < copy.buffers.size(); ++b) {
            if (mesh->vertexBuffers[b] != copy.buffers[b] || copy.buffers[b]->data != copy.data[b])
                return false;
        }
    }
    Engine3D::CTriMeshSource* data = CollisionData(sector);
    if (original.haveCollision && (data != original.collision.data || !SameCollision(original.collision)))
        return false;

    RebuildFrame f = FrameFor(original.quarters, original.position, quarters0, quarters, position, scale);
    for (const MeshCopy& copy : original.meshes) {
        Engine3D::meshRef* mesh = copy.mesh;
        for (size_t b = 0; b < copy.buffers.size(); ++b) {
            RewritePositions(copy.buffers[b]->data, copy.stride, copy.vertexCount, copy.positions[b].data(), f.from,
                             f.to, f.turns, f.copyScale);
            if (copy.normalOffset >= 0)
                RewriteNormals(copy.buffers[b]->data, copy.stride, copy.vertexCount,
                               static_cast<uint32_t>(copy.normalOffset), copy.normals[b].data(), scale, f.normalTurns);
            Upload(copy.buffers[b]);
        }
        V3 centre = Rebuilt(copy.centre, f.from, f.to, f.turns, f.copyScale);
        mesh->centre[0] = centre.x;
        mesh->centre[1] = centre.y;
        mesh->centre[2] = centre.z;
        mesh->radius = copy.radius * Largest(scale);
        for (uint32_t k = 0; k < copy.listCount && !copy.listCopy.empty(); ++k) {
            V3 p = Rebuilt({copy.listCopy[3 * k], copy.listCopy[3 * k + 1], copy.listCopy[3 * k + 2]}, f.from, f.to,
                           0, f.copyScale);
            mesh->positions[3 * k + 0] = p.x;
            mesh->positions[3 * k + 1] = p.y;
            mesh->positions[3 * k + 2] = p.z;
        }
    }
    if (original.haveCollision)
        RebuildCollision(original.collision, f.turns, f.from, f.to, f.copyScale);
    return true;
}

V3 Limit(uint8_t* sector, V3 worldScale)
{
    return sector ? CollisionLimit(CollisionData(sector), worldScale) : Uniform(kMax);
}

// --- the game side: objects -------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kModelComponent = 0x286A8D26;
void** const& kMovTriVtable = Vt::kCollMovTri;
void** const& kMovBBoxVtable = Vt::kCollMovBBox;
void** const& kMultiVtable = Vt::kCollMulti;
void* const kListHeadData = reinterpret_cast<void*>(-1);

// A scaled object: its collision object then, its triangle objects' own geometries (copied, as they were unscaled)
// and its boxes as they were, and the scale.
struct ObjectEntry {
    uint32_t id;
    GameObjects::CGameEntity* object;
    Engine3D::CHitMesh* collision;
    V3 scale;
    struct Tri {
        Engine3D::CHitMovingTriangles* tri;
        CollisionCopy copy;
        uint32_t rejection; // (the triangle object's own copy of the face-rejection scale, +0xD0)
    };
    std::vector<Tri> tris;
    struct Cuboid {
        Engine3D::CHitMovingBox* box;
        Maths::BBox original;
    };
    std::vector<Cuboid> boxes;
};
std::vector<ObjectEntry> g_objects;

uint8_t* ModelOf(GameObjects::CGameEntity* object)
{
    GameObjects::CObjectModule* component = object->FetchModule(kModelComponent);
    return component ? *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208) : nullptr;
}

Engine3D::CHitMesh* CollisionOf(GameObjects::CGameEntity* object)
{
    auto* component = static_cast<GameObjects::CContactShapeModule*>(object->FetchModule(GameObjects::CContactShapeModule::kType));
    return component ? component->m_hitShape : nullptr;
}

bool Alive(GameObjects::CGameEntity* object)
{
    return object && !(object->m_objectFlags & GameObjects::OBJECT_FLAG_DEAD) &&
           !(object->m_compositeFlags & GameObjects::COMPOSITE_FLAG_DESTROYING);
}

// The entry of an object, if it's still the object (and the collision) it was made for.
ObjectEntry* EntryOf(GameObjects::CGameEntity* object)
{
    for (size_t i = 0; i < g_objects.size(); ++i) {
        ObjectEntry& entry = g_objects[i];
        if (entry.id != object->m_ident)
            continue;
        bool same = entry.object == object && Alive(object) && CollisionOf(object) == entry.collision;
        for (const ObjectEntry::Tri& t : entry.tris)
            same = same && t.tri->m_geometry == t.copy.data && SameCollision(t.copy);
        if (same)
            return &entry;
        g_objects.erase(g_objects.begin() + static_cast<std::ptrdiff_t>(i)); // (another object of that name now)
        return nullptr;
    }
    return nullptr;
}

// A triangle object given a geometry of its own (unless it owns one already) and kept as copied.
bool TakeTri(Engine3D::CHitMovingTriangles* tri, ObjectEntry* entry)
{
    Engine3D::CTriMeshSource* geometry = tri->m_geometry;
    if (!geometry)
        return true;
    bool owns = (tri->m_flagBits & Engine3D::CHitMesh::kOwnsGeometry) && !(tri->m_flagBits & Engine3D::CHitMesh::kSharedGeometry);
    if (!owns) {
        Engine3D::CTriMeshSource* own = geometry->Replicate(0, false);
        if (!own)
            return false;
        tri->m_flagBits = static_cast<uint16_t>((tri->m_flagBits | Engine3D::CHitMesh::kOwnsGeometry) & ~Engine3D::CHitMesh::kSharedGeometry);
        tri->AssignShapeData(own);
        geometry = own;
    }
    entry->tris.push_back({tri, CopyCollision(geometry), tri->m_unknownD0});
    return true;
}

bool TakeCollision(Engine3D::CHitMesh* collision, ObjectEntry* entry)
{
    if (!collision)
        return true;
    if (collision->m_vtable == kMovTriVtable)
        return TakeTri(static_cast<Engine3D::CHitMovingTriangles*>(collision), entry);
    if (collision->m_vtable == kMovBBoxVtable) {
        auto* box = static_cast<Engine3D::CHitMovingBox*>(collision);
        entry->boxes.push_back({box, box->m_box});
        return true;
    }
    if (collision->m_vtable == kMultiVtable) {
        MemSys::ListNode* head = static_cast<Engine3D::CHitMeshGroup*>(collision)->m_subNodes;
        for (MemSys::ListNode* node = head ? head->next : nullptr; node && node != head && node->data != kListHeadData;
             node = node->next) {
            if (!TakeCollision(static_cast<Engine3D::CHitMesh*>(node->data), entry))
                return false;
        }
        return true;
    }
    Log("level editor: an object's collision of an unknown kind (vtable %p) isn't scaled", static_cast<void*>(collision->m_vtable));
    return true;
}

void ApplyCollision(ObjectEntry& entry)
{
    for (ObjectEntry::Tri& t : entry.tris) {
        RebuildCollision(t.copy, 0, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, entry.scale);
        t.tri->AssignShapeData(t.copy.data); // (its radius from the vertices; its box made again)
        t.tri->m_unknownD0 = IsUnitScale(entry.scale) ? t.rejection : t.copy.data->m_unknown3C;
    }
    for (ObjectEntry::Cuboid& b : entry.boxes) {
        const Maths::Vec4& lo = b.original.min;
        const Maths::Vec4& hi = b.original.max;
        b.box->m_box.min = {lo.x * entry.scale.x, lo.y * entry.scale.y, lo.z * entry.scale.z, lo.w};
        b.box->m_box.max = {hi.x * entry.scale.x, hi.y * entry.scale.y, hi.z * entry.scale.z, hi.w};
    }
    if (entry.collision && entry.collision->m_vtable == kMultiVtable)
        static_cast<Engine3D::CHitMeshGroup*>(entry.collision)->m_dirty = true;
}

} // namespace

V3 ObjectScale(GameObjects::CGameEntity* object)
{
    ObjectEntry* entry = object ? EntryOf(object) : nullptr;
    return entry ? entry->scale : Uniform(1.0f);
}

bool ObjectScalable(GameObjects::CGameEntity* object) { return object && Alive(object) && ModelOf(object); }

V3 ObjectLimit(GameObjects::CGameEntity* object)
{
    V3 limit = Uniform(kMax);
    if (!object)
        return limit;
    ObjectEntry* entry = EntryOf(object);
    auto narrow = [&limit](const Engine3D::CTriMeshSource* data, V3 scale) {
        V3 l = CollisionLimit(data, scale);
        limit = {std::min(limit.x, l.x), std::min(limit.y, l.y), std::min(limit.z, l.z)};
    };
    if (entry) {
        for (const ObjectEntry::Tri& t : entry->tris)
            narrow(t.copy.data, entry->scale);
        return limit;
    }
    std::vector<Engine3D::CHitMesh*> pending = {CollisionOf(object)};
    while (!pending.empty()) {
        Engine3D::CHitMesh* c = pending.back();
        pending.pop_back();
        if (!c)
            continue;
        if (c->m_vtable == kMovTriVtable && c->m_geometry)
            narrow(c->m_geometry, Uniform(1.0f));
        if (c->m_vtable == kMultiVtable) {
            MemSys::ListNode* head = static_cast<Engine3D::CHitMeshGroup*>(c)->m_subNodes;
            for (MemSys::ListNode* node = head ? head->next : nullptr; node && node != head && node->data != kListHeadData;
                 node = node->next)
                pending.push_back(static_cast<Engine3D::CHitMesh*>(node->data));
        }
    }
    return limit;
}

bool SetObjectScale(GameObjects::CGameEntity* object, V3 scale)
{
    if (!ObjectScalable(object))
        return false;
    ObjectEntry* entry = EntryOf(object);
    if (!entry && IsUnitScale(scale))
        return true; // (never scaled: nothing to put back)
    if (!entry) {
        ObjectEntry made = {object->m_ident, object, CollisionOf(object), Uniform(1.0f), {}, {}};
        if (!TakeCollision(made.collision, &made)) {
            Log("level editor: %08X's collision couldn't be copied - not scaled", object->m_ident);
            return false;
        }
        g_objects.push_back(std::move(made));
        entry = &g_objects.back();
    }
    V3 limit = ObjectLimit(object);
    scale = {std::max(kMin, std::min(limit.x, Confine(scale.x))), std::max(kMin, std::min(limit.y, Confine(scale.y))),
             std::max(kMin, std::min(limit.z, Confine(scale.z)))};
    entry->scale = scale;
    // The model drawn scaled (CRenderModel::ChangeScale: the scale at +0x1C, flag 8 of +0x44 unless 1, 1, 1).
    Maths::Vec4 factor = {scale.x, scale.y, scale.z, 1.0f};
    reinterpret_cast<Engine3D::CRenderModel*>(ModelOf(object))->ChangeScale(&factor);
    ApplyCollision(*entry);
    object->MarkJumpedAhead(true); // (its components placed again at once)
    return true;
}

void ForgetObjects() { g_objects.clear(); }

} // namespace MenuSystem::LevelEditor::StretchBy

namespace Vt {
static VtableSlot kCollMulti_slots[] = {&Engine3D::CHitMeshGroup::ScalarDelete, &Engine3D::CHitMeshGroup::IsTriangleHit,
                Stubs::ReturnZero1, &Engine3D::CollObjStubs::NoTerrain, &Engine3D::CHitMeshGroup::SetGeometryAll,
                &Engine3D::CHitMeshGroup::PlaceAtAbsolutePoint, &Engine3D::CHitMeshGroup::ChangeFacing, &Engine3D::CHitMeshGroup::GetAbsolutePoint,
                &Engine3D::CHitMeshGroup::FetchFacing, &Engine3D::CHitMeshGroup::ReadVertexPoint, &Engine3D::CHitMeshGroup::GetValue38,
                &Engine3D::CHitMeshGroup::SetValue38, &Engine3D::CHitMeshGroup::Replicate, &Engine3D::CHitMeshGroup::InsideBounds,
                &Engine3D::CHitMeshGroup::CollideWithLine, &Engine3D::CViewRegion::QueryPoint, &Engine3D::CViewRegion::QueryPoint,
                &Engine3D::CViewRegion::QueryPoint, &Engine3D::CollGeometryForwarders::IsPointBehindFaces, &Engine3D::CHitMeshGroup::PassDown,
                &Engine3D::CHitMeshGroup::GetLocalBounds};
void** const kCollMulti = reinterpret_cast<void**>(kCollMulti_slots);
static VtableSlot kCollMovBBox_slots[] = {&Engine3D::CHitMovingBox::ScalarDelete, Stubs::ReturnFalse,
                &Engine3D::CHitMovingBox::GetTriangleAttrs, &Engine3D::CHitMovingBox::GetFaceTerrain, &Engine3D::CHitMovingBox::AssignShapeData,
                &Engine3D::CProbeRay::ChangeFinish, &Engine3D::CHitMovingMesh::ChangeFacing, &Engine3D::CHitMovingMesh::GetAbsolutePoint,
                &Engine3D::CHitMovingMesh::FetchFacing, &Engine3D::CHitMovingMesh::ReadVertexPoint, &Engine3D::CHitMovingMesh::GetValueA0,
                &Engine3D::CHitMovingMesh::SetValueA0, &Engine3D::CHitMovingBox::Replicate, &MemCard::SaveCard::AlwaysTrue,
                &Engine3D::CHitMovingBox::CollideWithLine, &Engine3D::CViewRegion::QueryPoint, &Engine3D::CViewRegion::QueryPoint,
                &Engine3D::CViewRegion::QueryPoint, &Engine3D::CollGeometryForwarders::IsPointBehindFaces,
                &Engine3D::CHitMovingBox::DrawDiagnostic, Stubs::ReturnZero};
void** const kCollMovBBox = reinterpret_cast<void**>(kCollMovBBox_slots);
static VtableSlot kCollMovTri_slots[] = {&Engine3D::CHitMovingTriangles::ScalarDelete, Stubs::ReturnTrue,
                &Engine3D::CollFaceAccess::GetTriangleAttrs, &Engine3D::CollFaceAccess::GetFaceTerrain, &Engine3D::CHitMovingTriangles::AssignShapeData,
                &Engine3D::CHitMovingTriangles::PlaceAtAbsolutePoint, &Engine3D::CHitMovingMesh::ChangeFacing,
                &Engine3D::CHitMovingMesh::GetAbsolutePoint, &Engine3D::CHitMovingMesh::FetchFacing, &Engine3D::CHitMovingMesh::ReadVertexPoint,
                &Engine3D::CHitMovingMesh::GetValueA0, &Engine3D::CHitMovingMesh::SetValueA0, &Engine3D::CHitMovingTriangles::Replicate,
                &Engine3D::CHitMovingTriangles::InsideBounds, &Engine3D::CHitMovingTriangles::CollideWithLine, &Engine3D::CViewRegion::QueryPoint,
                &Engine3D::CViewRegion::QueryPoint, &Engine3D::CViewRegion::QueryPoint, &Engine3D::CollGeometryForwarders::IsPointBehindFaces,
                &Engine3D::CHitMovingTriangles::DrawDiagnostic, &Engine3D::CHitMovingTriangles::GetLocalBounds};
void** const kCollMovTri = reinterpret_cast<void**>(kCollMovTri_slots);
} // namespace Vt

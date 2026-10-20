// NOT ORIGINAL - the level editor's scaling of scenery (ours, added at the user's request). See level_editor_scale.h.

#include "game/front/level_editor_scale.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/gfx/mesh.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_tri.h"
#include "hook/game.h"

namespace Front::LevelEditor::Scale {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

void ScaleAbout(Placement* placement, V3 pivot, float factor)
{
    // x'' = f (x' - pivot) + pivot, x' = Q s x + offset: x'' = Q (f s) x + f (offset - pivot) + pivot.
    placement->scale *= factor;
    placement->offset = (placement->offset - pivot) * factor + pivot;
}

float AllowedFactor(float factor, const std::vector<float>& scales, const std::vector<float>& limits)
{
    if (!(factor > 0.0f) || !std::isfinite(factor))
        return 1.0f;
    double low = 0.0, high = 1e30;
    for (size_t i = 0; i < scales.size(); ++i) {
        double scale = scales[i] > 0.0f ? scales[i] : 1.0;
        double limit = i < limits.size() ? std::min<double>(limits[i], kMax) : kMax;
        low = std::max(low, kMin / scale);
        high = std::min(high, limit / scale);
    }
    double f = factor;
    if (f > 1.0 && high < f)
        f = std::max(1.0, high); // (already past the limit: no further)
    if (f < 1.0 && low > f)
        f = std::min(1.0, low);
    return static_cast<float>(f);
}

float CompressedLimit(float extent)
{
    if (!(extent > 0.0f))
        return kMax;
    return std::min(kMax, kCompressedExtent / extent);
}

float Clamp(float scale)
{
    if (!(scale == scale))
        return 1.0f;
    return std::max(kMin, std::min(kMax, scale));
}

V3 Rebuilt(V3 p, V3 from, V3 to, int quarters, float scale)
{
    return to + QuarterTurn((p - from) * scale, quarters);
}

void RewritePositions(uint8_t* data, uint32_t stride, uint32_t count, const float* original, V3 from, V3 to,
                      int quarters, float scale)
{
    for (uint32_t v = 0; v < count; ++v, data += stride, original += 3) {
        V3 p = Rebuilt({original[0], original[1], original[2]}, from, to, quarters, scale);
        auto* out = reinterpret_cast<float*>(data);
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
    }
}

std::string Token(float scale)
{
    if (scale == 1.0f)
        return "";
    char text[32];
    std::snprintf(text, sizeof(text), " scale=%.9g", scale);
    return text;
}

bool ReadToken(const std::string& field, float* scale)
{
    if (field.compare(0, 6, "scale=") != 0 || field.size() == 6)
        return false;
    char* end = nullptr;
    float value = std::strtof(field.c_str() + 6, &end);
    if (!end || *end != '\0' || !(value > 0.0f) || !std::isfinite(value))
        return false;
    *scale = value;
    return true;
}

// --- the collision's copy ---------------------------------------------------------------------------------------

namespace {

void SplitNodes(Nx::CCollBSPNode* node, std::vector<CollisionCopy::Node>* out, int depth)
{
    // (iterative down the greater side, as the game's own walks; a guard against a broken tree)
    while (node && !node->IsLeaf() && depth < 256) {
        out->push_back({node, node->m_split, node->m_link});
        SplitNodes(node->LessChild(), out, depth + 1);
        node = node->GreaterChild();
        ++depth;
    }
}

uint32_t VertexBytes(const Nx::CCollObjTriData* data) { return data->m_numVertices * (data->m_compressed ? 6u : 12u); }

} // namespace

CollisionCopy CopyCollision(Nx::CCollObjTriData* data)
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
    copy.min = data->m_min;
    copy.max = data->m_max;
    copy.rejection = data->m_unknown3C;
    SplitNodes(data->m_bsp, &copy.nodes, 0);
    return copy;
}

bool SameCollision(const CollisionCopy& copy)
{
    const Nx::CCollObjTriData* data = copy.data;
    return data && data->m_vertices == copy.vertexArray && data->m_numVertices == copy.numVertices &&
           data->m_compressed == copy.compressed && copy.vertices.size() == VertexBytes(data);
}

void RebuildCollision(const CollisionCopy& copy, int quarters, V3 from, V3 to, float scale)
{
    Nx::CCollObjTriData* data = copy.data;
    if (!data || !data->m_vertices)
        return;
    std::memcpy(data->m_vertices, copy.vertices.data(), copy.vertices.size());
    data->m_min = copy.min;
    data->m_max = copy.max;
    data->m_unknown3C = copy.rejection;
    for (const CollisionCopy::Node& n : copy.nodes) {
        n.node->m_split = n.split;
        n.node->m_link = n.link;
    }
    Mth::Vector centre = {from.x, from.y, from.z, 0.0f};
    if (scale != 1.0f) {
        Mth::Vector factor = {scale, scale, scale, 1.0f};
        data->Scale(&centre, &factor);
    }
    if (quarters & 3)
        data->RotateY(&centre, quarters & 3);
    V3 delta = to - from;
    if (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f) {
        Mth::Vector by = {delta.x, delta.y, delta.z, 0.0f};
        data->Translate(&by);
    }
}

float CollisionLimit(const Nx::CCollObjTriData* data, float scale)
{
    if (!data || !data->m_compressed || !(scale > 0.0f))
        return kMax;
    float extent = std::max({data->m_max.x - data->m_min.x, data->m_max.y - data->m_min.y,
                             data->m_max.z - data->m_min.z});
    return CompressedLimit(extent / scale);
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

// A mesh's copy: each vertex buffer's positions, its bounding sphere and its position list (+0xAC / +0xB0 - moved
// with the mesh by sMesh::MoveTo, not turned by TurnY: rewritten the same way, scaled).
struct MeshCopy {
    Nx::sMesh* mesh;
    uint16_t vertexCount;
    uint8_t stride;
    std::vector<Nx::sVertexBuffer*> buffers;
    std::vector<uint8_t*> data;
    std::vector<std::vector<float>> positions; // (per buffer: x, y, z a vertex)
    V3 centre;
    float radius;
    uint32_t listCount;
    float* list;
    std::vector<float> listCopy;
};

uint8_t* CollOf(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x18); }
Nx::CCollObjTriData* CollisionData(uint8_t* sector)
{
    auto* coll = reinterpret_cast<Nx::CCollObj*>(CollOf(sector));
    return coll ? coll->m_geometry : nullptr;
}

// The vertex buffer written back to the card (0x004D40E0 after the inlined lock of the whole buffer, as sMesh::MoveTo).
void Upload(Nx::sVertexBuffer* buffer)
{
    buffer->lockOffset = 0;
    buffer->lockSize = buffer->size;
    buffer->lockFlags = 0;
    GameFunction<void(__thiscall*)(Nx::sVertexBuffer*)>(0x004D40E0)(buffer);
}

} // namespace

struct Original {
    uint8_t* geom;
    Nx::sMesh** meshList;
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
    original->meshList = *reinterpret_cast<Nx::sMesh***>(geom + 0x3C);
    original->meshCount = *reinterpret_cast<uint32_t*>(geom + 0x40);
    original->quarters = quarters;
    original->position = position;
    if (!original->meshList || original->meshCount == 0 || original->meshCount > 4096)
        return nullptr;
    for (uint32_t i = 0; i < original->meshCount; ++i) {
        Nx::sMesh* mesh = original->meshList[i];
        if (!mesh || mesh->stride < 12 || mesh->vertexBufferCount > 3)
            return nullptr;
        MeshCopy copy = {mesh, mesh->vertexCount, mesh->stride, {}, {}, {}, {mesh->centre[0], mesh->centre[1], mesh->centre[2]},
                         mesh->radius, mesh->positionCount, mesh->positions, {}};
        for (int b = 0; b < mesh->vertexBufferCount; ++b) {
            Nx::sVertexBuffer* buffer = mesh->vertexBuffers[b];
            if (!buffer || !buffer->data || buffer->size < static_cast<uint32_t>(mesh->vertexCount) * mesh->stride)
                return nullptr;
            std::vector<float> positions(3u * mesh->vertexCount);
            const uint8_t* vertex = buffer->data;
            for (uint32_t v = 0; v < mesh->vertexCount; ++v, vertex += mesh->stride)
                std::memcpy(&positions[3u * v], vertex, 12);
            copy.buffers.push_back(buffer);
            copy.data.push_back(buffer->data);
            copy.positions.push_back(std::move(positions));
        }
        if (mesh->positions && mesh->positionCount)
            copy.listCopy.assign(mesh->positions, mesh->positions + 3u * mesh->positionCount);
        original->meshes.push_back(std::move(copy));
    }
    Nx::CCollObjTriData* data = CollisionData(sector);
    original->haveCollision = data && data->m_vertices;
    if (original->haveCollision)
        original->collision = CopyCollision(data);
    return original;
}

bool Rebuild(const Original& original, uint8_t* sector, uint8_t* geom, int quarters, V3 position, float scale)
{
    // (still the sector copied: its meshes, buffers and collision where and as they were)
    if (geom != original.geom || *reinterpret_cast<Nx::sMesh***>(geom + 0x3C) != original.meshList ||
        *reinterpret_cast<uint32_t*>(geom + 0x40) != original.meshCount)
        return false;
    for (uint32_t i = 0; i < original.meshCount; ++i) {
        const MeshCopy& copy = original.meshes[i];
        Nx::sMesh* mesh = original.meshList[i];
        if (mesh != copy.mesh || mesh->vertexCount != copy.vertexCount || mesh->stride != copy.stride ||
            mesh->vertexBufferCount != copy.buffers.size() || mesh->positions != copy.list ||
            mesh->positionCount != copy.listCount)
            return false;
        for (size_t b = 0; b < copy.buffers.size(); ++b) {
            if (mesh->vertexBuffers[b] != copy.buffers[b] || copy.buffers[b]->data != copy.data[b])
                return false;
        }
    }
    Nx::CCollObjTriData* data = CollisionData(sector);
    if (original.haveCollision && (data != original.collision.data || !SameCollision(original.collision)))
        return false;

    int turns = (quarters - original.quarters) & 3;
    V3 from = original.position;
    for (const MeshCopy& copy : original.meshes) {
        Nx::sMesh* mesh = copy.mesh;
        for (size_t b = 0; b < copy.buffers.size(); ++b) {
            RewritePositions(copy.buffers[b]->data, copy.stride, copy.vertexCount, copy.positions[b].data(), from,
                             position, turns, scale);
            Upload(copy.buffers[b]);
        }
        V3 centre = Rebuilt(copy.centre, from, position, turns, scale);
        mesh->centre[0] = centre.x;
        mesh->centre[1] = centre.y;
        mesh->centre[2] = centre.z;
        mesh->radius = copy.radius * scale;
        for (uint32_t k = 0; k < copy.listCount && !copy.listCopy.empty(); ++k) {
            V3 p = Rebuilt({copy.listCopy[3 * k], copy.listCopy[3 * k + 1], copy.listCopy[3 * k + 2]}, from, position,
                           0, scale);
            mesh->positions[3 * k + 0] = p.x;
            mesh->positions[3 * k + 1] = p.y;
            mesh->positions[3 * k + 2] = p.z;
        }
    }
    if (original.haveCollision)
        RebuildCollision(original.collision, turns, from, position, scale);
    return true;
}

float Limit(uint8_t* sector, float scale) { return sector ? CollisionLimit(CollisionData(sector), scale) : kMax; }

} // namespace Front::LevelEditor::Scale

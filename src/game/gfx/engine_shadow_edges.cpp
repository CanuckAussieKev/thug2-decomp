// The shadow volume's silhouette edges (CShadowVolume::SetModel, 0x004A22F0): see engine_shadows.h.

#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_shadows.h"
#include "game/lst/hashtable.h"
#include "game/nx/coll_tri.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// An edge (0x50 bytes): its two ends as each of the two faces sharing it sees them - [0] / [2] the first face's
// ends, [1] / [3] the second's (set when it's found) - each with the face's normal.
struct EdgeCorner {
    uint16_t vertex;
    uint16_t pad; // (stack garbage in the original's records: zeroed here)
    Mth::Vector normal;
};
struct ShadowEdge {
    EdgeCorner corners[4];
};
static_assert(sizeof(ShadowEdge) == 0x50, "layout must match the original");

CShadowVolume** ShadowVolumes() { return &GameVariable<CShadowVolume*>(0x007013D8); }
Lst::HashTable& EdgeTable() { return GameVariable<Lst::HashTable>(0x00701418); }

// Into the first free slot of the volumes drawn (none: not drawn).
void AddShadowVolume(CShadowVolume* volume)
{
    for (int i = 0; i < 16; ++i) {
        if (!ShadowVolumes()[i]) {
            ShadowVolumes()[i] = volume;
            return;
        }
    }
}

// The meshes of the model's collision scene: +0x88 their count, +0x8C the array (the volumes' key).
int MeshCount(const uint8_t* assetData) { return *reinterpret_cast<const int*>(assetData + 0x88); }

bool Skipped(const CCollObjTriData& mesh) { return (reinterpret_cast<const uint16_t*>(mesh.m_faces)[0] & 0x40) != 0; }

// The edge from `from` to `to` of a face with `normal`: an existing edge the other way round, or the same way, gets
// its second face's corners; otherwise a new edge is added (past the end of the array if the estimate was short,
// as the original).
void AddEdge(ShadowEdges* edges, int& count, uint16_t from, uint16_t to, const Mth::Vector& normal)
{
    ShadowEdge* all = reinterpret_cast<ShadowEdge*>(edges->edges);
    int i = 0;
    for (; i < count; ++i) {
        ShadowEdge& edge = all[i];
        if (edge.corners[0].vertex == from && edge.corners[2].vertex == to) {
            edge.corners[1].vertex = from;
            edge.corners[3].vertex = to;
        } else if (edge.corners[2].vertex == from && edge.corners[0].vertex == to) {
            edge.corners[3].vertex = from;
            edge.corners[1].vertex = to;
        } else {
            continue;
        }
        edge.corners[1].normal = normal;
        edge.corners[3].normal = normal;
        break;
    }
    if (i != count)
        return;
    ShadowEdge& edge = all[count++];
    for (int k = 0; k < 4; ++k) {
        edge.corners[k].vertex = k < 2 ? from : to;
        edge.corners[k].pad = 0;
        edge.corners[k].normal = normal;
    }
}

} // namespace

// 0x004A22F0
void CShadowVolume::SetModel(CModel*, uint8_t* assetData)
{
    if (assetData)
        m_modelKey = *reinterpret_cast<uint32_t*>(assetData + 0x8C);
    mp_edges = FindShadowEdges(m_modelKey);
    if (mp_edges) {
        ++mp_edges->users;
        AddShadowVolume(this);
        return;
    }
    mp_edges = static_cast<ShadowEdges*>(Mem::Calloc(0x10)); // (not checked, as the original)
    auto* meshes = reinterpret_cast<CCollObjTriData*>(m_modelKey);
    // (with a key but no asset data, read through null - as the original)
    int faces = 0, vertices = 0;
    if (m_modelKey) {
        for (int i = 0; i < MeshCount(assetData); ++i) {
            if (!Skipped(meshes[i])) {
                faces += meshes[i].m_numFaces;
                vertices += meshes[i].m_numVertices;
            }
        }
    }
    int estimate = vertices + faces - 2; // (Euler's formula for a closed convex mesh)
    if (estimate >= 1) {
        uint32_t size = static_cast<uint32_t>(estimate) * sizeof(ShadowEdge);
        mp_edges->edges = static_cast<uint8_t*>(Mem::New(size));
        std::memset(mp_edges->edges, 0, size);
    }
    int count = 0;
    if (m_modelKey) {
        for (int m = 0; m < MeshCount(assetData); ++m) {
            const CCollObjTriData& mesh = meshes[m];
            if (Skipped(mesh))
                continue;
            count = 0; // (each mesh starts the edges again: only the last one's are kept)
            for (int f = 0; f < mesh.m_numFaces; ++f) {
                uint16_t a, b, c;
                if (mesh.m_smallFaces) {
                    const uint8_t* face = mesh.m_faces + f * 8;
                    a = face[4];
                    b = face[5];
                    c = face[6];
                } else {
                    const auto* face = reinterpret_cast<const uint16_t*>(mesh.m_faces + f * 10);
                    a = face[2];
                    b = face[3];
                    c = face[4];
                }
                Mth::Vector normal = mesh.GetFaceNormal(f);
                mp_edges->lastMesh = m;
                AddEdge(mp_edges, count, a, b, normal);
                AddEdge(mp_edges, count, b, c, normal);
                AddEdge(mp_edges, count, c, a, normal);
            }
        }
    }
    if (count <= estimate && count > 0) {
        mp_edges->count = count;
        ++mp_edges->users;
        uint32_t key = m_modelKey;
        EdgeTable().PutItem(key, mp_edges);
        AddShadowVolume(this);
        return;
    }
    Dbg::Printf("Breaking Euler's formula: %df + %dv - 2 != %d!  Make sure the model is convex.", faces, vertices,
                count);
    if (mp_edges->edges) {
        Mem::Manager::Get()->Free(mp_edges->edges);
        mp_edges->edges = nullptr;
    }
    Mem::Manager::Get()->Free(mp_edges);
    mp_edges = nullptr;
}

} // namespace Nx

REPLACE(0x004A22F0, &Nx::CShadowVolume::SetModel);

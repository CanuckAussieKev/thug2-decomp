// NOT ORIGINAL - the level editor's raised / lowered ground: the game side (ours). See level_editor_ground.h.

#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>

#include "game/core/crc.h"
#include "game/core/mem.h"
#include "game/front/level_editor_ground.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_scenery.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/mesh.h"
#include "game/gfx/sector.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "hook/game.h"
#include "hook/log.h"
#include "game/core/game_vtables.h"

namespace Front::LevelEditor::Ground {

namespace {

void** const& kPcGeomVtable = Vt::kPcGeom;
void** const& kCollStaticVtable = Vt::kCollStatic;

uint8_t* GeomOf(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x14); }
uint8_t* CollOf(uint8_t* sector) { return *reinterpret_cast<uint8_t**>(sector + 0x18); }
Nx::sMesh** MeshesOf(uint8_t* geom) { return *reinterpret_cast<Nx::sMesh***>(geom + 0x3C); }
uint32_t MeshCountOf(uint8_t* geom) { return *reinterpret_cast<uint32_t*>(geom + 0x40); }
uint32_t VertexFormat(const Nx::sMesh* mesh)
{
    return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(mesh) + 0x38);
}

uint8_t* LookupSector(uint8_t* scene, uint32_t checksum)
{
    return scene ? static_cast<uint8_t*>(reinterpret_cast<Nx::CScene*>(scene)->GetSector(checksum)) : nullptr;
}
// CSector::SetActive (0x0049E9B0) / IsActive (0x0049E9E0): drawn and collided with, or neither.
void SetActive(uint8_t* sector, bool on) { reinterpret_cast<Nx::CSector*>(sector)->SetActive(on); }
bool IsActive(uint8_t* sector) { return reinterpret_cast<Nx::CSector*>(sector)->IsActive(); }

Nx::CCollStatic* StaticOf(uint8_t* sector)
{
    uint8_t* coll = CollOf(sector);
    if (!coll || *reinterpret_cast<void***>(coll) != kCollStaticVtable)
        return nullptr;
    return reinterpret_cast<Nx::CCollStatic*>(coll);
}

// The buffers as sMesh::Clone (mesh.cpp) makes them: a vertex buffer (0x004D5760: its memory copy and the card's) and
// an index buffer (the C runtime's malloc 0x00624031, constructor 0x004D4270).
Nx::sVertexBuffer* NewVertexBuffer(uint32_t size)
{
    return Nx::NewVertexBuffer(size, 0, 0);
}
void UploadVertices(Nx::sVertexBuffer* buffer)
{
    buffer->lockOffset = 0;
    buffer->lockSize = buffer->size;
    buffer->lockFlags = 0;
    buffer->Unlock();
}
Nx::sIndexBuffer* NewIndexBuffer(uint32_t bytes)
{
    void* memory = GameFunction<void*(__cdecl*)(size_t)>(0x00624031)(0x18);
    if (!memory)
        return nullptr;
    return static_cast<Nx::sIndexBuffer*>(memory)->Construct(bytes, 0, 0x65);
}
void UploadIndices(Nx::sIndexBuffer* buffer)
{
    auto* d3d = static_cast<IDirect3DIndexBuffer9*>(buffer->buffer);
    void* locked = nullptr;
    if (!d3d || FAILED(d3d->Lock(0, 0, &locked, 0)))
        return;
    std::memcpy(locked, buffer->indices, buffer->size);
    d3d->Unlock();
}

// --- state ---

std::vector<Spec> g_specs;
struct Made {
    uint32_t sector = 0; // the sector made for it (Crc(name + "_" + material) - a new donor: a new copy)
    Spec built;
};
std::map<std::string, Made> g_made;   // (the pieces in place)
std::set<uint32_t> g_pool;           // (every sector made this level - kept, hidden, when its piece goes)
std::map<uint32_t, int> g_tileHides; // (tiles hidden by pits, and by how many)
uint8_t* g_scene;
uint32_t g_sceneName;

uint32_t SectorName(const Spec& spec)
{
    char text[96];
    std::snprintf(text, sizeof(text), "%s_%08X", spec.name.c_str(), spec.material);
    return Crc::GenerateCRCFromString(text);
}

// A new level (another scene): what the editor knew of the last one forgotten (its sectors are gone).
void NoteLevel()
{
    uint8_t* scene = Scenery::MainScene();
    uint32_t name = Scenery::SceneName(scene);
    if (scene == g_scene && name == g_sceneName)
        return;
    g_scene = scene;
    g_sceneName = name;
    g_specs.clear();
    g_made.clear();
    g_pool.clear();
    g_tileHides.clear();
}

void HideTiles(const std::vector<uint32_t>& tiles, bool hide)
{
    for (uint32_t tile : tiles) {
        uint8_t* sector = Scenery::SectorOf(tile);
        if (!sector)
            continue;
        int& count = g_tileHides[tile];
        if (hide) {
            if (count++ == 0)
                SetActive(sector, false);
        } else if (count > 0 && --count == 0) {
            SetActive(sector, true);
        }
    }
}

struct GroundStep : ModuleStep {
    std::vector<Spec> specs;
    explicit GroundStep(std::vector<Spec> s) : specs(std::move(s)) {}
    void Restore() override
    {
        std::string message;
        SetAll(specs, &message);
    }
    std::shared_ptr<ModuleStep> Now() const override { return std::make_shared<GroundStep>(g_specs); }
};

// The generated piece written into a copy of its donor (see the header). False and why.
bool Write(uint8_t* sector, const Spec& spec, const Mesh& mesh, std::string* why)
{
    uint8_t* geom = GeomOf(sector);
    if (!geom || *reinterpret_cast<void***>(geom) != kPcGeomVtable || MeshCountOf(geom) == 0) {
        *why = "the copy has no drawn geometry";
        return false;
    }
    Nx::sMesh** meshes = MeshesOf(geom);
    Nx::sMesh* first = meshes[0];
    Layout layout;
    if (!first || !LayoutFor(VertexFormat(first), first->stride, &layout) || first->vertexBufferCount < 1 ||
        first->vertexBufferCount > 3) {
        *why = "the copy's vertex format can't be written";
        return false;
    }
    Nx::CCollStatic* coll = StaticOf(sector);
    if (!coll || !coll->m_geometry || !(coll->m_flags & Nx::CCollObj::kOwnsGeometry)) {
        *why = "the copy has no collision of its own";
        return false;
    }
    std::vector<uint16_t> strip = Strip(mesh.faces, true);
    if (mesh.faces.empty() || mesh.faces.size() > static_cast<size_t>(kMaxFaces) || mesh.vertices.size() > 0xFFFF ||
        strip.size() > 0xFFFF) {
        *why = "the piece has too many faces (at most 2048)";
        return false;
    }
    uint32_t count = static_cast<uint32_t>(mesh.vertices.size());
    // The render: new vertex buffers (each of the mesh's) and index buffers (each level of detail it draws).
    std::vector<uint8_t> data(static_cast<size_t>(count) * first->stride);
    WriteVertices(mesh, layout, first->stride, spec.colour, data.data());
    for (int b = 0; b < first->vertexBufferCount; ++b) {
        Nx::sVertexBuffer* buffer = NewVertexBuffer(static_cast<uint32_t>(data.size()));
        if (!buffer || !buffer->data) {
            *why = "no vertex buffer";
            return false;
        }
        std::memcpy(buffer->data, data.data(), data.size());
        UploadVertices(buffer);
        first->vertexBuffers[b] = buffer;
    }
    first->vertexCount = static_cast<uint16_t>(count);
    for (int k = 0; k < 8; ++k) {
        if (k > 0 && !first->indexCounts[k])
            continue;
        Nx::sIndexBuffer* buffer = NewIndexBuffer(static_cast<uint32_t>(strip.size() * 2));
        if (!buffer || !buffer->indices) {
            *why = "no index buffer";
            return false;
        }
        std::memcpy(buffer->indices, strip.data(), strip.size() * 2);
        UploadIndices(buffer);
        first->indexBuffers[k] = buffer;
        first->indexCounts[k] = static_cast<uint16_t>(strip.size());
    }
    if (first->perVertexBytes) {
        first->perVertexBytes = static_cast<uint8_t*>(Mem::New(count));
        if (first->perVertexBytes)
            std::memset(first->perVertexBytes, 0, count);
    }
    first->positionCount = 0; // (the copied lists aren't the piece's)
    first->triangleCount = 0;
    V3 centre = (mesh.min + mesh.max) * 0.5f;
    V3 half = mesh.max - centre;
    first->centre[0] = centre.x, first->centre[1] = centre.y, first->centre[2] = centre.z;
    first->radius = std::sqrt(Dot(half, half));
    // The other meshes: nothing drawn (their strips all one index - degenerate).
    for (uint32_t i = 1; i < MeshCountOf(geom); ++i) {
        Nx::sMesh* other = meshes[i];
        if (!other)
            continue;
        for (int k = 0; k < 8; ++k) {
            Nx::sIndexBuffer* buffer = other->indexBuffers[k];
            if (!other->indexCounts[k] || !buffer || !buffer->indices)
                continue;
            if (std::any_of(buffer->indices, buffer->indices + other->indexCounts[k], [](uint16_t v) { return v != 0; })) {
                std::memset(buffer->indices, 0, other->indexCounts[k] * 2u);
                UploadIndices(buffer);
            }
        }
        other->centre[0] = centre.x, other->centre[1] = centre.y, other->centre[2] = centre.z;
        other->radius = 1.0f;
    }
    float* box = reinterpret_cast<float*>(geom + 0x14);
    box[0] = mesh.min.x, box[1] = mesh.min.y, box[2] = mesh.min.z;
    box[4] = mesh.max.x, box[5] = mesh.max.y, box[6] = mesh.max.z;

    // The collision: new arrays (Mem::New - the game frees them with Mem::Free, CCollObjTriData::Destruct), floats,
    // 10-byte faces, one leaf listing every face.
    Nx::CCollObjTriData* tri = coll->m_geometry;
    uint32_t faces = static_cast<uint32_t>(mesh.faces.size());
    auto* vertices = static_cast<float*>(Mem::New(count * 12));
    auto* faceData = static_cast<uint8_t*>(Mem::New(faces * 10));
    auto* nodeMemory = static_cast<uint32_t*>(Mem::New(8 + 4));
    auto* leafFaces = static_cast<uint16_t*>(Mem::New(faces * 2));
    uint8_t* bytes = tri->m_vertexBytes ? static_cast<uint8_t*>(Mem::New(count)) : nullptr;
    if (!vertices || !faceData || !nodeMemory || !leafFaces || (tri->m_vertexBytes && !bytes)) {
        *why = "out of memory for the collision";
        return false;
    }
    for (uint32_t v = 0; v < count; ++v)
        std::memcpy(vertices + 3 * v, &mesh.vertices[v].pos, 12);
    for (uint32_t f = 0; f < faces; ++f) {
        const Face& face = mesh.faces[f];
        uint16_t words[5] = {face.flags, spec.terrain, face.a, face.b, face.c};
        std::memcpy(faceData + 10 * f, words, 10);
        leafFaces[f] = static_cast<uint16_t>(f);
    }
    if (bytes)
        std::memset(bytes, 0x80, count);
    if (tri->m_vertices)
        Mem::Free(tri->m_vertices);
    if (tri->m_faces)
        Mem::Free(tri->m_faces);
    if (tri->m_vertexBytes)
        Mem::Free(tri->m_vertexBytes);
    tri->DeleteBSP();
    tri->m_vertices = vertices;
    tri->m_faces = faceData;
    tri->m_vertexBytes = bytes;
    tri->m_numVertices = static_cast<uint16_t>(count);
    tri->m_numFaces = static_cast<uint16_t>(faces);
    tri->m_smallFaces = false;
    tri->m_compressed = false;
    tri->m_min.x = mesh.min.x, tri->m_min.y = mesh.min.y, tri->m_min.z = mesh.min.z;
    tri->m_max.x = mesh.max.x, tri->m_max.y = mesh.max.y, tri->m_max.z = mesh.max.z;
    tri->m_unknown3C = 0; // (no face skipped by its distance)
    // (new CCollBSPNode[1]: the count, then one leaf - axis bits 3, its face count at +2, its faces at +4)
    nodeMemory[0] = 1;
    auto* leaf = reinterpret_cast<Nx::CCollBSPNode*>(nodeMemory + 1);
    leaf->m_split = 3 | static_cast<int32_t>(faces << 16);
    leaf->m_link = reinterpret_cast<uint32_t>(leafFaces);
    tri->m_bsp = leaf;
    tri->SetUpAllFaces();
    // (the face-rejection bytes: one a face, on the bottom-up heap as CCollStatic::Clone keeps them)
    if (coll->m_buffer)
        Mem::FreeMalloc(coll->m_buffer);
    Mem::PushContext(Mem::BottomUpHeap());
    coll->m_buffer = static_cast<uint8_t*>(Mem::Malloc(faces));
    Mem::PopContext();
    if (coll->m_buffer)
        std::memset(coll->m_buffer, 0xFF, faces);
    return true;
}

// The piece put in place: its sector made (or one made before reused) and written, shown; its pit's tiles hidden.
bool Make(const Spec& spec, std::string* why)
{
    uint8_t* scene = Scenery::MainScene();
    if (!scene) {
        *why = "no level";
        return false;
    }
    Mesh mesh = Build(spec);
    if (mesh.faces.empty()) {
        *why = "the piece has no faces";
        return false;
    }
    uint32_t name = SectorName(spec);
    uint8_t* sector = LookupSector(scene, name);
    if (sector && !g_pool.count(name)) {
        *why = "a sector of that name is the level's";
        return false;
    }
    if (!sector) {
        if (const char* no = WhyNotDonor(spec.material)) {
            *why = std::string("its material's piece can't be copied: ") + no;
            return false;
        }
        uint32_t made = reinterpret_cast<Nx::CScene*>(scene)->CloneSectorNamed(
            spec.material, reinterpret_cast<Nx::CScene*>(scene), 0, true, name);
        sector = made ? LookupSector(scene, made) : nullptr;
        if (!sector) {
            *why = "the material's piece wasn't copied";
            return false;
        }
        g_pool.insert(made);
        name = made;
    }
    if (!Write(sector, spec, mesh, why)) {
        SetActive(sector, false);
        return false;
    }
    SetActive(sector, true);
    Scenery::NoteCollisionMoved(name);
    Scenery::Commit();
    // (a piece already there under this name: its old sector hidden when it was another, its tiles shown again)
    auto it = g_made.find(spec.name);
    if (it != g_made.end()) {
        if (it->second.sector != name)
            if (uint8_t* old = LookupSector(scene, it->second.sector))
                SetActive(old, false);
        HideTiles(it->second.built.hides, false);
    }
    HideTiles(spec.hides, true);
    g_made[spec.name] = {name, spec};
    Log("level editor: ground piece %s (%s, %.0f x %.0f, %.0f %s, %u faces) in place - material %s", spec.name.c_str(),
        spec.lower ? "pit" : "platform", spec.x1 - spec.x0, spec.z1 - spec.z0, spec.height, spec.lower ? "deep" : "high",
        static_cast<unsigned>(mesh.faces.size()), Objects::Label(spec.material).c_str());
    return true;
}

void Unmake(const std::string& pieceName)
{
    auto it = g_made.find(pieceName);
    if (it == g_made.end())
        return;
    if (uint8_t* sector = LookupSector(Scenery::MainScene(), it->second.sector))
        SetActive(sector, false);
    HideTiles(it->second.built.hides, false);
    g_made.erase(it);
}

} // namespace

const std::vector<Spec>& All() { return g_specs; }

bool IsPiece(uint32_t sector) { return g_pool.count(sector) != 0; }

const Spec* Find(const std::string& name)
{
    for (const Spec& s : g_specs) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

void Start() { NoteLevel(); }
void Stop() {}

const char* WhyNotDonor(uint32_t checksum)
{
    uint8_t* scene = Scenery::MainScene();
    uint8_t* sector = LookupSector(scene, checksum);
    if (!sector)
        return "not a piece of the level's main scene";
    if (g_pool.count(checksum))
        return "a generated piece";
    uint8_t* geom = GeomOf(sector);
    if (!geom || *reinterpret_cast<void***>(geom) != kPcGeomVtable)
        return "nothing drawn";
    if (*reinterpret_cast<void**>(geom + 0x4C))
        return "an instance";
    uint32_t meshes = MeshCountOf(geom);
    if (!MeshesOf(geom) || meshes == 0 || meshes > 64)
        return "no meshes (or too many)";
    Nx::sMesh* first = MeshesOf(geom)[0];
    Layout layout;
    if (!first || first->billboard || (first->flags & 0x11))
        return "a billboard or shared mesh";
    if (!LayoutFor(VertexFormat(first), first->stride, &layout) || layout.texCount < 1)
        return "its vertex format (no plain positions and texture coordinates)";
    if (first->vertexBufferCount < 1 || first->vertexBufferCount > 3 || !first->vertexBuffers[0] ||
        !first->vertexBuffers[0]->data)
        return "its vertex buffers";
    if (first->perVertexData)
        return "per-vertex data the editor can't make";
    if (!first->material ||
        Picking::MaterialSeeThrough(*reinterpret_cast<const uint32_t*>(first->material + 0x20),
                                    *reinterpret_cast<const uint32_t*>(first->material + 0x80)))
        return "a see-through material";
    Nx::CCollStatic* coll = StaticOf(sector);
    if (!coll || !coll->m_geometry || !coll->m_geometry->m_numFaces)
        return "no collision";
    return nullptr;
}

bool ChooseMaterial(uint32_t wanted, Spec* spec, std::string* why)
{
    NoteLevel();
    uint32_t chosen = wanted && !WhyNotDonor(wanted) ? wanted : 0;
    if (!chosen) {
        // (the nearest piece that can lend its material - a floor-like one, its top near the ground, first)
        struct Walk {
            const Spec* spec;
            uint32_t best = 0, bestFloor = 0;
            float bestDistance = 1e30f, bestFloorDistance = 1e30f;
        } walk = {spec};
        Scenery::ForEachLevelSector(
            [](void* context, uint32_t checksum, uint8_t* sector) {
                auto* w = static_cast<Walk*>(context);
                if (!IsActive(sector) || WhyNotDonor(checksum))
                    return;
                const Nx::CCollObjTriData* tri = StaticOf(sector)->m_geometry;
                V3 c = {(w->spec->x0 + w->spec->x1) * 0.5f, w->spec->base, (w->spec->z0 + w->spec->z1) * 0.5f};
                V3 lo = {tri->m_min.x, tri->m_min.y, tri->m_min.z}, hi = {tri->m_max.x, tri->m_max.y, tri->m_max.z};
                V3 nearest = {std::clamp(c.x, lo.x, hi.x), std::clamp(c.y, lo.y, hi.y), std::clamp(c.z, lo.z, hi.z)};
                V3 d = nearest - c;
                float distance = std::sqrt(Dot(d, d));
                if (distance < w->bestDistance)
                    w->best = checksum, w->bestDistance = distance;
                if (std::fabs(hi.y - c.y) < 30.0f && distance < w->bestFloorDistance)
                    w->bestFloor = checksum, w->bestFloorDistance = distance;
            },
            &walk);
        chosen = walk.bestFloor ? walk.bestFloor : walk.best;
    }
    if (!chosen) {
        *why = wanted ? std::string("that piece can't lend its material (") + WhyNotDonor(wanted) +
                            ") and no other piece of the level can"
                      : "no piece of the level can lend its material";
        return false;
    }
    spec->material = chosen;
    // What it looks like: its first mesh's average colour, how many units a texture repeat spans, its commonest terrain.
    uint8_t* sector = LookupSector(Scenery::MainScene(), chosen);
    Nx::sMesh* mesh = MeshesOf(GeomOf(sector))[0];
    Layout layout;
    LayoutFor(VertexFormat(mesh), mesh->stride, &layout);
    const uint8_t* data = mesh->vertexBuffers[0]->data;
    if (layout.diffuse >= 0 && mesh->vertexCount) {
        double sum[4] = {};
        for (uint32_t v = 0; v < mesh->vertexCount; ++v) {
            uint32_t c;
            std::memcpy(&c, data + v * mesh->stride + layout.diffuse, 4);
            for (int k = 0; k < 4; ++k)
                sum[k] += (c >> (8 * k)) & 0xFF;
        }
        uint32_t colour = 0;
        for (int k = 0; k < 4; ++k)
            colour |= static_cast<uint32_t>(sum[k] / mesh->vertexCount + 0.5) << (8 * k);
        spec->colour = colour;
    }
    Nx::sIndexBuffer* indices = mesh->indexBuffers[0];
    if (indices && indices->indices && mesh->indexCounts[0] >= 3) {
        double world = 0.0, texture = 0.0;
        for (uint32_t i = 0; i + 1 < mesh->indexCounts[0]; ++i) {
            uint16_t a = indices->indices[i], b = indices->indices[i + 1];
            if (a == b || a >= mesh->vertexCount || b >= mesh->vertexCount)
                continue;
            const float* pa = reinterpret_cast<const float*>(data + a * mesh->stride);
            const float* pb = reinterpret_cast<const float*>(data + b * mesh->stride);
            const float* ta = reinterpret_cast<const float*>(data + a * mesh->stride + layout.tex);
            const float* tb = reinterpret_cast<const float*>(data + b * mesh->stride + layout.tex);
            double dw = std::sqrt(double(pa[0] - pb[0]) * (pa[0] - pb[0]) + double(pa[1] - pb[1]) * (pa[1] - pb[1]) +
                                  double(pa[2] - pb[2]) * (pa[2] - pb[2]));
            double dt = std::sqrt(double(ta[0] - tb[0]) * (ta[0] - tb[0]) + double(ta[1] - tb[1]) * (ta[1] - tb[1]));
            if (dt > 1e-4 && std::isfinite(dt) && std::isfinite(dw)) {
                world += dw;
                texture += dt;
            }
        }
        if (texture > 1e-4)
            spec->uv = std::clamp(static_cast<float>(world / texture), 16.0f, 4096.0f);
    }
    const Nx::CCollObjTriData* tri = StaticOf(sector)->m_geometry;
    std::map<uint16_t, int> terrains;
    for (int f = 0; f < tri->m_numFaces && tri->m_faces; ++f)
        ++terrains[reinterpret_cast<const uint16_t*>(tri->m_faces + f * (tri->m_smallFaces ? 8 : 10))[1]];
    int most = -1;
    for (const auto& t : terrains) {
        if (t.second > most)
            most = t.second, spec->terrain = t.first;
    }
    return true;
}

bool FindPitTiles(Spec* spec, std::string* why)
{
    NoteLevel();
    spec->hides.clear();
    struct Walk {
        Spec* spec;
        std::vector<uint32_t> tiles;
        std::string refused;
    } walk = {spec, {}, {}};
    Scenery::ForEachLevelSector(
        [](void* context, uint32_t checksum, uint8_t* sector) {
            auto* w = static_cast<Walk*>(context);
            // (a tile a pit hides now still counts - the pit being edited)
            bool mine = g_tileHides.count(checksum) && g_tileHides[checksum] > 0;
            if (!w->refused.empty() || g_pool.count(checksum) || (!IsActive(sector) && !mine))
                return;
            Nx::CCollStatic* coll = StaticOf(sector);
            const Nx::CCollObjTriData* tri = coll ? coll->m_geometry : nullptr;
            if (!tri || !tri->m_faces || !tri->m_vertices || ((coll->m_flags & 4) && !mine))
                return;
            const Spec& s = *w->spec;
            if (tri->m_max.x < s.x0 || tri->m_min.x > s.x1 || tri->m_max.z < s.z0 || tri->m_min.z > s.z1 ||
                tri->m_max.y < s.base - 40.0f || tri->m_min.y > s.base + 40.0f)
                return;
            bool ground = false;
            int faces = std::min<int>(tri->m_numFaces, 60000);
            for (int f = 0; f < faces && !ground; ++f) {
                uint16_t flags = *reinterpret_cast<const uint16_t*>(tri->m_faces + f * (tri->m_smallFaces ? 8 : 10));
                if (flags & 0x10)
                    continue;
                V3 c[3];
                bool ok = true;
                for (int k = 0; k < 3 && ok; ++k) {
                    uint32_t index = tri->FaceVertex(f, k);
                    ok = index < tri->m_numVertices;
                    if (ok) {
                        Mth::Vector p = tri->GetRawVertexPos(static_cast<int>(index));
                        c[k] = {p.x, p.y, p.z};
                    }
                }
                ground = ok && GroundFaceUnder(c, s, 30.0f);
            }
            if (!ground)
                return;
            V3 lo = {tri->m_min.x, tri->m_min.y, tri->m_min.z}, hi = {tri->m_max.x, tri->m_max.y, tri->m_max.z};
            if (TileInside(lo, hi, s, 25.0f)) {
                w->tiles.push_back(checksum);
                return;
            }
            char text[320];
            std::snprintf(text, sizeof(text),
                          "the floor here is %s%s, %.0f x %.0f - bigger than the rectangle. THUG2's floors can't be cut: "
                          "make the rectangle cover that whole piece, or raise around it instead",
                          Objects::Label(checksum).c_str(), GeomOf(sector) ? "" : " (collision only)", hi.x - lo.x,
                          hi.z - lo.z);
            w->refused = text;
        },
        &walk);
    if (!walk.refused.empty()) {
        *why = walk.refused;
        return false;
    }
    if (walk.tiles.empty()) {
        *why = "no floor piece was found at the ground's height under the rectangle";
        return false;
    }
    spec->hides = walk.tiles;
    return true;
}

bool Put(const Spec& in, std::string* why)
{
    NoteLevel();
    Spec spec = in;
    if (!Normalise(&spec)) {
        *why = "a value isn't a number";
        return false;
    }
    if (spec.lower && !FindPitTiles(&spec, why))
        return false;
    if (!spec.lower)
        spec.hides.clear();
    std::vector<Spec> before = g_specs;
    if (!Make(spec, why))
        return false;
    PushModuleUndoLabelled(std::make_shared<GroundStep>(before), spec.lower ? "Lower ground" : "Raise ground");
    bool found = false;
    for (Spec& s : g_specs) {
        if (s.name == spec.name) {
            s = spec;
            found = true;
        }
    }
    if (!found)
        g_specs.push_back(spec);
    return true;
}

void Remove(const std::string& name)
{
    NoteLevel();
    if (!Find(name))
        return;
    PushModuleUndoLabelled(std::make_shared<GroundStep>(g_specs), "Remove ground piece");
    Unmake(name);
    g_specs.erase(std::remove_if(g_specs.begin(), g_specs.end(), [&](const Spec& s) { return s.name == name; }),
                  g_specs.end());
}

int SetAll(const std::vector<Spec>& specs, std::string* message)
{
    NoteLevel();
    // (gone: taken out; changed or new: made; the same: left)
    std::vector<std::string> gone;
    for (const auto& made : g_made) {
        if (std::none_of(specs.begin(), specs.end(), [&](const Spec& s) { return s.name == made.first; }))
            gone.push_back(made.first);
    }
    for (const std::string& name : gone)
        Unmake(name);
    std::vector<Spec> kept;
    int placed = 0;
    for (const Spec& spec : specs) {
        auto it = g_made.find(spec.name);
        std::string why;
        if ((it != g_made.end() && SameSpec(it->second.built, spec)) || Make(spec, &why)) {
            ++placed;
        } else {
            Log("level editor: ground piece %s not made: %s", spec.name.c_str(), why.c_str());
            if (message && message->empty())
                *message = "ground piece " + spec.name + " not made: " + why;
        }
        kept.push_back(spec); // (kept even when not made: its record isn't lost)
    }
    g_specs = kept;
    return placed;
}

std::vector<std::string> CaptureRecords()
{
    std::vector<std::string> lines;
    for (const Spec& s : g_specs)
        lines.push_back("ground " + Record(s, Objects::Label(s.material).c_str()));
    return lines;
}

void ApplyRecords(const std::vector<std::string>& lines)
{
    std::vector<Spec> specs;
    for (const std::string& line : lines) {
        Spec s;
        std::string body = line.size() > 7 ? line.substr(7) : std::string();
        if (line.rfind("ground ", 0) == 0 && ReadRecord(body, &s))
            specs.push_back(s);
        else
            Log("level editor: a layout's ground record isn't readable - skipped: %s", line.c_str());
    }
    std::string message;
    int placed = SetAll(specs, &message);
    if (!specs.empty())
        Log("level editor: the layout's %d of %u ground pieces put in place", placed, static_cast<unsigned>(specs.size()));
}

void ResetAll()
{
    // (a new level: nothing of the old one touched - its sectors are gone)
    uint8_t* scene = Scenery::MainScene();
    if (scene == g_scene && Scenery::SceneName(scene) == g_sceneName) {
        std::vector<std::string> names;
        for (const auto& made : g_made)
            names.push_back(made.first);
        for (const std::string& name : names)
            Unmake(name);
    }
    g_specs.clear();
    g_made.clear();
    g_tileHides.clear();
    if (scene != g_scene || Scenery::SceneName(scene) != g_sceneName)
        NoteLevel();
}

void AddPreview(const Overlays::Painter& painter, const Spec& spec, uint32_t argb)
{
    Mesh mesh = Build(spec);
    size_t faces = std::min<size_t>(mesh.faces.size(), 3000);
    for (size_t f = 0; f < faces; ++f) {
        const Face& face = mesh.faces[f];
        V3 a = mesh.vertices[face.a].pos, b = mesh.vertices[face.b].pos, c = mesh.vertices[face.c].pos;
        painter.Line(a, b, argb);
        painter.Line(b, c, argb);
        painter.Line(c, a, argb);
    }
}

} // namespace Front::LevelEditor::Ground

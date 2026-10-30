// A scene's mesh list and materials: see mesh_list.h.

#include "game/gfx/mesh_list.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// A mesh's material (+0x14) and what the list looks at.
uint8_t* MaterialOf(const uint8_t* mesh) { return *reinterpret_cast<uint8_t* const*>(mesh + 0x14); }
bool Transparent(const uint8_t* material) { return material && (material[0x20] & 0x40); }
uint16_t& LoadIndex(uint8_t* mesh) { return *reinterpret_cast<uint16_t*>(mesh + 0x20); }
uint16_t& IndexCount(uint8_t* mesh) { return *reinterpret_cast<uint16_t*>(mesh + 0x24); }
void*& IndexBufferOf(uint8_t* mesh) { return *reinterpret_cast<void**>(mesh + 0x54); }

// The index buffers (not rewritten yet; thiscall): Lock gives the indices (a copy kept in memory).
uint16_t* LockIndices(void* buffer)
{
    uint16_t* indices = nullptr;
    static_cast<sIndexBuffer*>(buffer)->Lock(0, 0, &indices, 0);
    return indices;
}
void UnlockIndices(void* buffer) { static_cast<sIndexBuffer*>(buffer)->Unlock(); }
void ReleaseIndexBuffer(void* buffer) { static_cast<sIndexBuffer*>(buffer)->Destroy(); }
void* NewIndexBuffer(uint32_t bytes)
{
    void* memory = GameFunction<void*(__cdecl*)(uint32_t)>(0x004D3BC0)(0x18); // (the renderer's operator new)
    if (!memory)
        return nullptr;
    return static_cast<sIndexBuffer*>(memory)->Construct(bytes, 0, 0x65);
}
// A mesh's vertex buffers (+0xCC) and their release.
void* VertexBuffer(uint8_t* mesh, int index) { return reinterpret_cast<sMesh*>(mesh)->GetVertexBuffer(index); }
void SetVertexBuffer(uint8_t* mesh, int index, void* buffer)
{
    reinterpret_cast<sMesh*>(mesh)->SetVertexBuffer(index, static_cast<sVertexBuffer*>(buffer));
}
void ReleaseVertexBuffer(void* buffer) { static_cast<sVertexBuffer*>(buffer)->Destroy(); }
// A material's destructor body (not rewritten yet; thiscall).
void DestroyMaterial(void* material) { static_cast<sMaterial*>(material)->DestroyBody(); }

GAME_GLOBAL(uint16_t[596], g_stripBuffer, 0x007CAA40); // the rebuilt strip

// The rebuilt strip (0x007CAA40).
uint16_t* StripBuffer() { return g_stripBuffer; }

// The C runtime's qsort (the game's, so equal meshes stay in the same order).
void Qsort(void* base, uint32_t count, uint32_t size, int(__cdecl* compare)(const void*, const void*))
{
    GameFunction<void(__cdecl*)(void*, uint32_t, uint32_t, int(__cdecl*)(const void*, const void*))>(0x006242A0)(
        base, count, size, compare);
}

// Iterate a hash table's values: the first (or next), null at the end.
void* NextValue(Lst::HashTable* table)
{
    if (!table->IterateNext() || !table->m_iterItem)
        return nullptr;
    return table->m_iterItem->value;
}
void* FirstValue(Lst::HashTable* table)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    return NextValue(table);
}

} // namespace

// 0x004DDD50
int __cdecl CompareMeshes(const void* a, const void* b)
{
    const uint8_t* meshA = *static_cast<uint8_t* const*>(a);
    const uint8_t* meshB = *static_cast<uint8_t* const*>(b);
    const uint8_t* materialA = MaterialOf(meshA);
    const uint8_t* materialB = MaterialOf(meshB);
    float keyA = *reinterpret_cast<const float*>(materialA + 0x1C);
    float keyB = *reinterpret_cast<const float*>(materialB + 0x1C);
    if (!(keyA == keyB)) // (also not a number)
        return keyA > keyB ? 1 : -1;
    if (materialA[0xC] != materialB[0xC])
        return materialA[0xC] != 0 ? 1 : -1;
    if (materialA == materialB)
        return 0;
    bool flagA = (materialA[0x20] & 0x10) != 0, flagB = (materialB[0x20] & 0x10) != 0;
    if (flagA != flagB)
        return flagA ? 1 : -1;
    uint32_t orderA = *reinterpret_cast<const uint32_t*>(meshA + 0x44);
    uint32_t orderB = *reinterpret_cast<const uint32_t*>(meshB + 0x44);
    if (orderA != orderB)
        return orderB < orderA ? 1 : -1;
    return reinterpret_cast<uintptr_t>(materialB) < reinterpret_cast<uintptr_t>(materialA) ? 1 : -1;
}

// 0x004DE300
sScene* sScene::Construct()
{
    new (&bounds) Mth::BBox();
    flags = 0;
    meshCount = 0;
    transparentCount = 0;
    filled = 0;
    flaggedCount = 0;
    unknown40 = 0;
    meshes = nullptr;
    materials = nullptr;
    extra = nullptr;
    unknown78 = 0;
    comObject = nullptr;
    unknown80 = 0;
    firstFlagged = -1;
    firstTransparent = -1;
    return this;
}

// 0x004DE490
void sScene::DestroyBody()
{
    if (Lst::HashTable* table = materials) {
        if (table->m_buckets) {
            table->FlushAllItems();
            Mem::Manager::Instance()->Free(table->m_buckets);
            table->m_buckets = nullptr;
        }
        Mem::Manager::Instance()->Free(table);
    }
    for (int i = 0; i < meshCount; ++i) {
        uint8_t* mesh = meshes[i];
        if (!mesh)
            continue;
        if (void* buffer = VertexBuffer(mesh, 0)) {
            ReleaseVertexBuffer(buffer);
            SetVertexBuffer(meshes[i], 0, nullptr);
        }
    }
    if (meshes)
        Mem::Manager::Instance()->Free(meshes);
    if (extra) // (delete[]: the records' destructor is empty)
        Mem::Manager::Instance()->Free(extra - 4);
    if (comObject)
        reinterpret_cast<unsigned long(__stdcall*)(void*)>((*static_cast<void***>(comObject))[2])(comObject);
}

// 0x004DE340
void sScene::UpdateSphere()
{
    double halfX = (static_cast<double>(bounds.max.x) - bounds.min.x) * 0.5;
    double halfY = (static_cast<double>(bounds.max.y) - bounds.min.y) * 0.5; // (kept in double precision)
    float sizeZ = static_cast<float>(static_cast<double>(bounds.max.z) - bounds.min.z);
    float halfXFloat = static_cast<float>(halfX);
    double halfZ = static_cast<double>(sizeZ) * 0.5;
    centre[0] = static_cast<float>(static_cast<double>(halfXFloat) + bounds.min.x);
    centre[1] = static_cast<float>(halfY + bounds.min.y);
    centre[2] = static_cast<float>(halfZ + bounds.min.z);
    radius = static_cast<float>(
        std::sqrt((halfZ * halfZ + halfY * halfY) + static_cast<double>(halfXFloat) * halfXFloat));
}

// 0x004DE3E0
void sScene::AllocateMeshList()
{
    if (meshCount > 0)
        meshes = static_cast<uint8_t**>(Mem::New(meshCount * 4));
}

// 0x004DDDF0
void sScene::CountMeshes(int count, uint8_t* const* list)
{
    for (int i = 0; i < count; ++i) {
        ++meshCount;
        if (Transparent(MaterialOf(list[i])))
            ++transparentCount;
    }
}

// 0x004DDE20
void sScene::AddMeshes(int count, uint8_t* const* list)
{
    for (int i = 0; i < count; ++i)
        meshes[filled++] = list[i];
}

void sScene::Recount()
{
    transparentCount = 0;
    firstTransparent = meshCount;
    firstFlagged = meshCount;
    flaggedCount = 0;
    for (int i = 0; i < meshCount; ++i) {
        const uint8_t* material = MaterialOf(meshes[i]);
        if (!Transparent(material))
            continue;
        ++transparentCount;
        if (i < firstTransparent)
            firstTransparent = i;
        if (MaterialOf(meshes[i])[0xC]) {
            ++flaggedCount;
            if (i < firstFlagged)
                firstFlagged = i;
        }
    }
}

// 0x004DDE60
void sScene::RemoveMeshes(int count, uint8_t* const* list)
{
    int removed = 0;
    for (int k = 0; k < count; ++k) {
        for (int j = 0; j < meshCount; ++j) {
            if (meshes[j] == list[k]) {
                meshes[j] = nullptr;
                ++removed;
                break;
            }
        }
    }
    // Close each gap. (A null left in the list before is taken as one; if none is found it never ends.)
    while (removed > 0) {
        int n = meshCount;
        int j = 0;
        while (j < meshCount && meshes[j])
            ++j;
        if (j >= meshCount)
            continue;
        if (j < n - 1)
            std::memmove(&meshes[j], &meshes[j + 1], (n - j - 1) * sizeof(meshes[0]));
        --removed;
        --meshCount;
        --filled;
    }
    Recount();
}

// 0x004DDFA0
void sScene::SortMeshes()
{
    Qsort(meshes, meshCount, sizeof(meshes[0]), &CompareMeshes);
    Recount();
}

// 0x004DE020
uint8_t* sScene::FindMesh(uint32_t index) const
{
    for (int i = 0; i < meshCount; ++i) {
        if (LoadIndex(meshes[i]) == index)
            return meshes[i];
    }
    return nullptr;
}

// 0x004DE420
void* sScene::FindMaterial(uint32_t checksum) const
{
    if (!materials)
        return nullptr;
    for (void* material = FirstValue(materials); material; material = NextValue(materials)) {
        if (*static_cast<uint32_t*>(material) == checksum)
            return material;
    }
    return nullptr;
}

// 0x004DE060
void sScene::BreakStrips(uint32_t mask, const uint32_t* records, uint32_t count)
{
    if (count == 0 || mask == 0)
        return;
    // Breaks found per mesh load index. (The original's table has 64 entries; a larger index overruns it.)
    int breaks[64] = {};
    for (uint32_t r = 0; r < count; ++r, records += 4) {
        if (!(records[0] & mask))
            continue;
        uint32_t meshIndex = records[1] >> 16;
        uint8_t* mesh = FindMesh(meshIndex);
        if (!mesh)
            continue;
        uint32_t a = records[1] & 0xFFFF, b = records[2] & 0xFFFF, c = records[2] >> 16;
        uint16_t* indices = LockIndices(IndexBufferOf(mesh));
        uint32_t i = records[3];
        uint32_t first = indices[i] & 0x7FFF, second = indices[i + 1] & 0x7FFF;
        for (i += 2; i < IndexCount(mesh); ++i) {
            uint32_t third = indices[i] & 0x7FFF;
            if (first == a && second == c && third == b) {
                if (meshIndex < 64)
                    ++breaks[meshIndex];
                indices[i - 1] |= 0x8000;
                break;
            }
            first = second;
            second = third;
        }
        UnlockIndices(IndexBufferOf(mesh));
    }
    for (int m = 0; m < meshCount; ++m) {
        uint8_t* mesh = meshes[m];
        uint32_t index = LoadIndex(mesh);
        if (index >= 64 || breaks[index] <= 0)
            continue;
        uint16_t* indices = LockIndices(IndexBufferOf(mesh));
        uint32_t n = 0;
        for (uint32_t i = 0; i < IndexCount(mesh); ++i) {
            uint16_t value = indices[i] & 0x7FFF;
            StripBuffer()[n++] = value;
            if (indices[i] & 0x8000) { // repeated: a strip break
                StripBuffer()[n++] = value;
                StripBuffer()[n++] = value;
            }
        }
        ReleaseIndexBuffer(IndexBufferOf(mesh));
        IndexCount(mesh) = static_cast<uint16_t>(n);
        void* buffer = NewIndexBuffer(n * 2);
        IndexBufferOf(mesh) = buffer;
        uint16_t* rebuilt = LockIndices(buffer);
        std::memcpy(rebuilt, StripBuffer(), IndexCount(mesh) * 2);
        UnlockIndices(IndexBufferOf(mesh));
    }
}

// 0x004DE560
void __cdecl DeleteScene(sScene* scene)
{
    if (Lst::HashTable* table = scene->materials) {
        for (void* material = FirstValue(table); material; material = NextValue(scene->materials)) {
            DestroyMaterial(material);
            Mem::Manager::Instance()->Free(material);
        }
    }
    scene->DestroyBody();
    Mem::Manager::Instance()->Free(scene);
}

} // namespace Nx

REPLACE(0x004DDD50, Nx::CompareMeshes);
REPLACE(0x004DE300, &Nx::sScene::Construct);
REPLACE(0x004DE490, &Nx::sScene::DestroyBody);
REPLACE(0x004DE340, &Nx::sScene::UpdateSphere);
REPLACE(0x004DE3E0, &Nx::sScene::AllocateMeshList);
REPLACE(0x004DDDF0, &Nx::sScene::CountMeshes);
REPLACE(0x004DDE20, &Nx::sScene::AddMeshes);
REPLACE(0x004DDE60, &Nx::sScene::RemoveMeshes);
REPLACE(0x004DDFA0, &Nx::sScene::SortMeshes);
REPLACE(0x004DE020, &Nx::sScene::FindMesh);
REPLACE(0x004DE420, &Nx::sScene::FindMaterial);
REPLACE(0x004DE060, &Nx::sScene::BreakStrips);
REPLACE(0x004DE560, Nx::DeleteScene);

// The engine's scene (Nx::CScene): its members of 0x0049C910-0x0049D18E (see engine_scene.h), and two destructors of
// the collision it keeps (the grid's body, the static collision objects' deleting destructor). 0x0049CAF0 isn't a
// Ghidra function.

#include "game/gfx/engine_scene.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/file/pip.h"
#include "game/gfx/engine_glue.h"
#include "game/lst/hashtable.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr int kCollStaticSize = 0x18;
constexpr int kTriDataSize = 0x40;

template <typename R = void, typename... A>
R Virtual(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

// The engine's sector (not rewritten here; thiscall): its flags (+8) read / set / cleared, its name (+4 - the folded
// getter 0x005480C0), its box, drawn and collided with or not.
uint32_t SectorFlags(void* sector) { return GameFunction<uint32_t(__thiscall*)(void*)>(0x0049E810)(sector); }
void SetSectorFlags(void* sector, uint32_t flags)
{
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0049E890)(sector, flags);
}
void ClearSectorFlags(void* sector, uint32_t flags)
{
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0049E8A0)(sector, flags);
}
uint32_t SectorName(void* sector) { return GameFunction<uint32_t(__thiscall*)(void*)>(0x005480C0)(sector); }
const Mth::BBox* SectorBox(void* sector)
{
    return GameFunction<const Mth::BBox*(__thiscall*)(void*)>(0x0049E6A0)(sector);
}
void SetSectorActive(void* sector, uint32_t on) { GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0049E9B0)(sector, on); }

// (a static collision object's own print, compiled out: the empty 0x005480A0 with it in ECX)
inline void PrintCollStatic(const CCollStatic*) {}

// A C string copied (unbounded, as the original's inlined strcpy).
void CopyString(char* to, const char* from)
{
    while ((*to++ = *from++) != 0) {
    }
}

// The table's items in turn, as far as the first null item or value.
template <typename Body>
void ForEachSector(Lst::HashTable* table, Body body)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    while (table->IterateNext()) {
        Lst::HashTable::Item* item = table->m_iterItem;
        if (!item || !item->value)
            break;
        body(item->value);
    }
}

} // namespace

// 0x0049C910
void CScene::FinishLoad(const char* name, void* arena)
{
    CopyString(m_name, name);
    Virtual(this, 1, arena);
}

// 0x0049C940
void CScene::FinishAdd(const char* name, uint32_t data)
{
    CopyString(m_addName, name);
    m_addData = data;
    m_added = true;
    Virtual(this, 2);
}

// 0x0049C9A0
void CScene::PrintCollStatics()
{
    for (int i = 0; i < m_collStaticCount; ++i) {
        Dbg::Printf("%4d/%d ", i, m_collStaticCount);
        PrintCollStatic(GetCollStatic(i));
    }
}

// 0x0049C9F0
CCollStatic* CScene::GetCollStatic(int index)
{
    return reinterpret_cast<CCollStatic*>(reinterpret_cast<uint8_t*>(mp_collStatics) + index * kCollStaticSize);
}

// 0x0049CA10
void* CScene::CreateSector()
{
    auto* sector = Virtual<uint8_t*>(this, 7);
    *reinterpret_cast<uint32_t*>(sector + 0x14) = 0;
    *reinterpret_cast<uint32_t*>(sector + 0x18) = 0;
    return sector;
}

// 0x0049CA20
int __cdecl MakeCollisionFileName(char* out, const char* level)
{
    const char* extension = PlatformExtension();
    return std::snprintf(out, 0x7FFFFFFF, "levels\\%s\\%s%s.col.%s", level, level, "", extension);
}

// 0x0049CB00
void* CScene::GetSector(uint32_t name)
{
    Lst::HashTable* table = mp_sectors;
    Lst::HashTable::Item* item = &table->m_buckets[name & ((1u << table->m_bits) - 1)];
    for (; item; item = item->next) {
        if (item->key == name && item->value)
            return item->value;
    }
    return nullptr;
}

// 0x0049CB40
bool CScene::DeleteSector(void* sector)
{
    if (SectorFlags(sector) & 0x40) {
        SetSectorFlags(sector, 8);
        m_sectorKept = true;
        return false;
    }
    if (GetSector(SectorName(sector)) == sector) {
        uint32_t name = SectorName(sector);
        mp_sectors->FlushItem(name);
    }
    if (sector)
        Virtual<void*>(sector, 0, 1u);
    return true;
}

// 0x0049CF80
bool CScene::DeleteSectorNamed(uint32_t name)
{
    void* sector = GetSector(name);
    return sector ? DeleteSector(sector) : false;
}

// 0x0049CBB0
bool CScene::LoadCollisionData(const uint8_t* data, int* count, CCollStatic** statics, uint8_t** objects,
                               Mth::BBox* box, int)
{
    auto word = [data](int offset) { return *reinterpret_cast<const int*>(data + offset); };
    auto align = [](uintptr_t at, uintptr_t to) { return (at + to - 1) & ~(to - 1); };
    // the data after the header (+4 objects, +8 vertex bytes, +0xC faces, +0x10 nodes, +0x14 / +0x18 vertices) and
    // the objects (0x40 bytes each)
    *count = word(4);
    *objects = const_cast<uint8_t*>(data) + 0x20;
    uintptr_t vertices = align(reinterpret_cast<uintptr_t>(*objects) + static_cast<uintptr_t>(*count) * kTriDataSize, 16);
    uintptr_t vertexBytes = vertices + 6 * static_cast<uintptr_t>(word(0x18) + 2 * word(0x14));
    uintptr_t faces = align(vertexBytes + static_cast<uintptr_t>(word(8)), 4);
    uintptr_t nodesAt = static_cast<uintptr_t>(10 * word(0xC) + 8 * word(0x10));
    uintptr_t buffer = faces + nodesAt;
    uintptr_t facesSize = align(static_cast<uintptr_t>(word(0xC)) + nodesAt, 4);
    uintptr_t nodes = faces + facesSize + 4;
    uintptr_t faceLists = nodes + *reinterpret_cast<const uint32_t*>(faces + facesSize);
    bool anyVertices = false;
    // the static objects: an array new (the count before them)
    int made = word(4);
    auto* block = static_cast<int*>(Mem::New(static_cast<unsigned int>(made) * kCollStaticSize + 4));
    CCollStatic* array = nullptr;
    if (block) {
        *block = made;
        array = reinterpret_cast<CCollStatic*>(block + 1);
        for (int i = 0; i < made; ++i)
            reinterpret_cast<CCollStatic*>(reinterpret_cast<uint8_t*>(array) + i * kCollStaticSize)->Construct();
    }
    *statics = array;
    for (int i = 0; i < word(4); ++i) {
        auto* geometry = reinterpret_cast<CCollObjTriData*>(*objects + i * kTriDataSize);
        geometry->Relocate(reinterpret_cast<uint32_t>(this), static_cast<uint32_t>(vertices),
                           static_cast<uint32_t>(vertexBytes), static_cast<uint32_t>(faces),
                           static_cast<uint32_t>(nodes), static_cast<uint32_t>(faceLists));
        geometry->SetUpAllFaces();
        auto* object = reinterpret_cast<CCollStatic*>(reinterpret_cast<uint8_t*>(*statics) + i * kCollStaticSize);
        Virtual(object, 4, geometry); // (SetGeometry)
        buffer = reinterpret_cast<uintptr_t>(object->TakeBuffer(reinterpret_cast<uint8_t*>(buffer), geometry));
        auto* bytes = reinterpret_cast<uint8_t*>(geometry);
        if (*reinterpret_cast<uint16_t*>(bytes + 8) > 0) {
            box->AddPoint(*reinterpret_cast<const Mth::Vector*>(bytes + 0x10));
            box->AddPoint(*reinterpret_cast<const Mth::Vector*>(bytes + 0x20));
            anyVertices = true;
        }
    }
    if (!anyVertices) {
        Mth::Vector low = {-100.0f, -100.0f, -100.0f, 1.0f};
        box->AddPoint(low);
        Mth::Vector high = {100.0f, 100.0f, 100.0f, 1.0f};
        box->AddPoint(high);
    }
    return true;
}

// 0x0049CE60
bool CScene::InitCollisionGrid(const Mth::BBox* box)
{
    if (!m_hasCollision)
        return false;
    m_collisionBox = *box;
    auto* grid = static_cast<CCollGrid*>(Mem::New(sizeof(CCollGrid)));
    if (grid) {
        new (&grid->m_box) Mth::BBox();
        for (int x = 0; x < CCollGrid::kSize; ++x)
            for (int z = 0; z < CCollGrid::kSize; ++z)
                grid->m_cells[x][z].Construct();
    }
    mp_collGrid = grid;
    grid->Init(&m_collisionBox); // (no check, as the original)
    return true;
}

// 0x0049CFB0
bool CScene::UnloadCollision()
{
    if (CCollGrid* grid = mp_collGrid) {
        for (int i = CCollGrid::kSize * CCollGrid::kSize - 1; i >= 0; --i) // (the grid's destructor, 0x0049C980)
            (&grid->m_cells[0][0])[i].Destruct();
        Mem::Free(grid);
        mp_collGrid = nullptr;
    }
    if (mp_collStatics) {
        int* block = reinterpret_cast<int*>(mp_collStatics) - 1;
        if (*block) // (the deleting destructor of the array, through the first's vtable: 0x0049CE00)
            Virtual<void*>(mp_collStatics, 0, 3u);
        else
            Mem::Free(block);
        mp_collStatics = nullptr;
    }
    if (m_pipName[0]) {
        Pip::Unload(m_pipName);
        m_pipName[0] = 0;
    }
    return true;
}

// 0x0049D030
void CScene::DeleteMarkedSectors()
{
    if (!mp_collGrid)
        return;
    ForEachSector(mp_sectors, [](void* sector) {
        if (SectorFlags(sector) & 8) {
            ClearSectorFlags(sector, 0x40);
            Virtual<void*>(sector, 0, 1u);
        }
    });
    mp_collGrid->Clear();
    mp_sectors->FlushAllItems();
}

// 0x0049D0B0
void CScene::SetSectorsInBox(const Mth::Vector* box, uint32_t on, int)
{
    float minX = box[0].x, minY = box[0].y, minZ = box[0].z;
    float maxX = box[1].x, maxY = box[1].y, maxZ = box[1].z;
    ForEachSector(mp_sectors, [&](void* sector) {
        const Mth::BBox* bounds = SectorBox(sector);
        const auto* f = reinterpret_cast<const float*>(bounds);
        // (each "not past it": NaN counts as touching)
        if (!(minX > f[4]) && !(f[0] > maxX) && !(minZ > f[6]) && !(f[2] > maxZ) && !(minY > f[5]) && !(f[1] > maxY))
            SetSectorActive(sector, on);
    });
}

// --- the virtuals' base bodies ---

// 0x0049CA50 / 0x0049CA60 / 0x0049CA70
void CScene::PostLoad(void*) { Dbg::Printf("STUB: PostLoad\n"); }
void CScene::SetRendered(int) { Dbg::Printf("STUB: plat_set_rendered\n"); }
void CScene::PostAdd() { Dbg::Printf("STUB: PostAdd\n"); }
// 0x0049CA80 / 0x0049CAA0 / 0x0049CAC0 / 0x0049CAE0
bool CScene::LoadTextures(const char*)
{
    Dbg::Printf("STUB: LoadTextures\n");
    return false;
}
bool CScene::LoadCollision(const char*)
{
    Dbg::Printf("STUB: LoadCollision\n");
    return false;
}
bool CScene::AddCollision(const char*)
{
    Dbg::Printf("STUB: AddCollision\n");
    return false;
}
bool CScene::UnloadAddScene()
{
    Dbg::Printf("STUB: UnloadAddScene\n");
    return false;
}
// 0x0049CAF0
void* CScene::PlatCreateSector()
{
    Dbg::Printf("STUB: plat_create_sector\n");
    return nullptr;
}
// 0x0049CF60
uint8_t* CScene::GetDefaultColour(uint8_t* out)
{
    out[0] = out[1] = out[2] = out[3] = 0x80;
    return out;
}

} // namespace Nx

namespace {

// The collision's destructors (this scene's): the grid's body, and the static objects' vector deleting destructor.
struct SceneCollisionAdapters {
    // 0x0049C980: the grid's cells destroyed, last first (the compiler's array destructor).
    void GridDestroyBody()
    {
        auto* grid = reinterpret_cast<Nx::CCollGrid*>(this);
        for (int i = Nx::CCollGrid::kSize * Nx::CCollGrid::kSize - 1; i >= 0; --i)
            (&grid->m_cells[0][0])[i].Destruct();
    }
    // 0x0049CE00 (CCollStatic's vtable slot 0): flags & 2 - the array this starts (its count before it) destroyed
    // last first, freed with flags & 1, the block returned; else this one destroyed and freed with flags & 1.
    void* CollStaticDelete(unsigned int flags)
    {
        auto* object = reinterpret_cast<Nx::CCollStatic*>(this);
        if (flags & 2) {
            int* block = reinterpret_cast<int*>(this) - 1;
            for (int i = *block - 1; i >= 0; --i)
                reinterpret_cast<Nx::CCollStatic*>(reinterpret_cast<uint8_t*>(object) + i * 0x18)->Destruct();
            if (flags & 1)
                Mem::Free(block);
            return block;
        }
        object->Destruct();
        if (flags & 1)
            Mem::Free(object);
        return object;
    }
};

} // namespace

REPLACE(0x0049C910, &Nx::CScene::FinishLoad);
REPLACE(0x0049C940, &Nx::CScene::FinishAdd);
REPLACE(0x0049C980, &SceneCollisionAdapters::GridDestroyBody);
REPLACE(0x0049C9A0, &Nx::CScene::PrintCollStatics);
REPLACE(0x0049C9F0, &Nx::CScene::GetCollStatic);
REPLACE(0x0049CA10, &Nx::CScene::CreateSector);
REPLACE(0x0049CA20, Nx::MakeCollisionFileName);
REPLACE(0x0049CA50, &Nx::CScene::PostLoad);
REPLACE(0x0049CA60, &Nx::CScene::SetRendered);
REPLACE(0x0049CA70, &Nx::CScene::PostAdd);
REPLACE(0x0049CA80, &Nx::CScene::LoadTextures);
REPLACE(0x0049CAA0, &Nx::CScene::LoadCollision);
REPLACE(0x0049CAC0, &Nx::CScene::AddCollision);
REPLACE(0x0049CAE0, &Nx::CScene::UnloadAddScene);
REPLACE(0x0049CAF0, &Nx::CScene::PlatCreateSector);
REPLACE(0x0049CB00, &Nx::CScene::GetSector);
REPLACE(0x0049CB40, &Nx::CScene::DeleteSector);
REPLACE(0x0049CBB0, &Nx::CScene::LoadCollisionData);
REPLACE(0x0049CE00, &SceneCollisionAdapters::CollStaticDelete);
REPLACE(0x0049CE60, &Nx::CScene::InitCollisionGrid);
REPLACE(0x0049CF60, &Nx::CScene::GetDefaultColour);
REPLACE(0x0049CF80, &Nx::CScene::DeleteSectorNamed);
REPLACE(0x0049CFB0, &Nx::CScene::UnloadCollision);
REPLACE(0x0049D030, &Nx::CScene::DeleteMarkedSectors);
REPLACE(0x0049D0B0, &Nx::CScene::SetSectorsInBox);

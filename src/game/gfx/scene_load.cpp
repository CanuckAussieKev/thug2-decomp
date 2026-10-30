// The PC engine's scene and mesh loaders: see scene_load.h.

#include "game/gfx/scene_load.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/file/file.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/frame_source.h"
#include "game/gfx/geom.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_mesh.h"
#include "game/gfx/pc_scene.h"
#include "game/gfx/render_state.h"
#include "game/gfx/sector.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Nx {

GAME_GLOBAL(Mem::PoolPair, g_frameSourcePools, 0x006FA7F4);

namespace {

// The engine's scenes and sectors (not rewritten yet; thiscall).
uint8_t* NewPcScene(int hashBits)
{
    void* memory = Mem::New(0x284);
    return memory ? reinterpret_cast<uint8_t*>(static_cast<CPcScene*>(memory)->Construct(hashBits)) : nullptr;
}
void* CreateSector(uint8_t* scene) { return reinterpret_cast<CScene*>(scene)->CreateSector(); }
void SetGeom(void* sector, void* geom) { static_cast<CSector*>(sector)->SetGeom(geom); }
void AddSector(uint8_t* scene, void* sector) { reinterpret_cast<CScene*>(scene)->AddSector(static_cast<CSector*>(sector)); }
void SetUpSectors(uint8_t* scene, const char* name)
{
    reinterpret_cast<CScene*>(scene)->FinishLoad(name, nullptr);
}
// A PC geom for a scene's sector (0x64 bytes, zeroed; the scene at +0x34).
uint8_t* NewSectorGeom(uint8_t* scene)
{
    void* memory = Mem::Calloc(0x64);
    auto* geom = memory ? reinterpret_cast<uint8_t*>(static_cast<CPcGeom*>(memory)->Construct()) : nullptr;
    // (no check: a failed allocation writes through null, as the original)
    *reinterpret_cast<uint8_t**>(geom + 0x34) = scene;
    return geom;
}
void InitGeom(uint8_t* geom) { reinterpret_cast<CPcGeom*>(geom)->StartMeshList(); }
// The 2D clip rectangle off (0x004D3CE0).
void ClearScreenClip() { ClearSkinScaling(); }

// The scene's hash table size: 2^n entries (n 1-12), the first at least twice the sectors.
int HashBits(int sectors)
{
    unsigned int wanted = 2 * static_cast<unsigned int>(sectors);
    unsigned int size = 2;
    int bits = 1;
    while (wanted > size && bits < 12) {
        size <<= 1;
        ++bits;
    }
    return bits;
}

// The hierarchy's array (new[]: its count in the word before it), constructed.
sHierarchyNode* NewHierarchy(int count)
{
    void* memory = Mem::New(count * sizeof(sHierarchyNode) + 4);
    if (!memory)
        return nullptr;
    *static_cast<int*>(memory) = count;
    auto* nodes = reinterpret_cast<sHierarchyNode*>(static_cast<uint8_t*>(memory) + 4);
    for (int i = 0; i < count; ++i)
        nodes[i].Construct();
    return nodes;
}

// A new mesh list with the flag at +0x40.
sScene* NewMeshList(bool meshFlag)
{
    void* memory = Mem::New(sizeof(sScene));
    sScene* list = memory ? static_cast<sScene*>(memory)->Construct() : nullptr;
    list->unknown40 = meshFlag;
    return list;
}

uint8_t* NewScene(int sectors, bool addSuperSectors, bool flag8D, sScene* list)
{
    uint8_t* scene = NewPcScene(HashBits(sectors));
    scene[0x8C] = addSuperSectors;
    scene[0x8D] = flag8D;
    *reinterpret_cast<sScene**>(scene + 0x280) = list;
    return scene;
}

// The mesh made around a loaded scene: the scene (+0xC4) and its hierarchy (+0xB4, +0xB8); the textures at +0x04.
void SetMeshScene(CPcMesh* mesh, uint8_t* scene)
{
    mesh->SetScene(scene);
}

} // namespace

// 0x004B2D30
sHierarchyNode* sHierarchyNode::Construct()
{
    checksum = 0;
    parentChecksum = 0;
    parentIndex = 0xFFFF;
    boneIndex = 0;
    for (int i = 0; i < 16; ++i)
        matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    return this;
}

// 0x004B3490
void* __cdecl LoadSceneFromMemory(const uint8_t* data, void* textures, bool addSuperSectors, bool flag8D, bool meshFlag)
{
    sScene* list = NewMeshList(meshFlag);
    const uint8_t* p = data + 0xC; // (past the header)
    list->materials = LoadMaterialsFromMemory(
        const_cast<uint8_t**>(&p),
        static_cast<const Lst::HashTable*>(*reinterpret_cast<void**>(static_cast<uint8_t*>(textures) + 0xC)));
    int sectors = *reinterpret_cast<const int*>(p);
    p += 4;
    uint8_t* scene = NewScene(sectors, addSuperSectors, flag8D, list);
    for (int i = sectors; i > 0; --i) {
        void* sector = CreateSector(scene);
        uint8_t* geom = NewSectorGeom(scene);
        SetGeom(sector, geom);
        InitGeom(geom);
        if (static_cast<CPcSector*>(sector)->LoadFromMemory(const_cast<uint8_t**>(&p)))
            AddSector(scene, sector);
    }
    list->UpdateSphere();
    int nodes = *reinterpret_cast<const int*>(p);
    p += 4;
    if (nodes > 0) {
        sHierarchyNode* hierarchy = NewHierarchy(nodes);
        list->extra = reinterpret_cast<uint8_t*>(hierarchy);
        // (no check: a failed allocation copies to null, as the original)
        std::memcpy(hierarchy, p, nodes * sizeof(sHierarchyNode));
        list->unknown78 = static_cast<uint32_t>(nodes);
    }
    return scene;
}

// 0x004B36D0
void* __cdecl LoadSceneFromFile(const char* name, void* textures, bool addSuperSectors, bool flag8D, bool meshFlag,
                                int sectorFlags)
{
    sScene* list = NewMeshList(meshFlag);
    File::Handle file = File::Open(name, "rb");
    if (!file)
        return nullptr; // (the mesh list isn't freed, as the original)
    uint32_t header[3];
    File::Read(&header[0], 4, 1, file);
    File::Read(&header[1], 4, 1, file);
    File::Read(&header[2], 4, 1, file);
    list->materials = LoadMaterialsFromFile(
        file, static_cast<const Lst::HashTable*>(*reinterpret_cast<void**>(static_cast<uint8_t*>(textures) + 0xC)));
    int sectors;
    File::Read(&sectors, 4, 1, file);
    uint8_t* scene = NewScene(sectors, addSuperSectors, flag8D, list);
    for (int i = 0; i < sectors; ++i) {
        void* sector = CreateSector(scene);
        uint8_t* geom = NewSectorGeom(scene);
        SetGeom(sector, geom);
        InitGeom(geom);
        if (static_cast<CPcSector*>(sector)->LoadFromFile(file, static_cast<uint32_t>(sectorFlags)))
            AddSector(scene, sector);
    }
    list->UpdateSphere();
    int nodes;
    File::Read(&nodes, 4, 1, file);
    if (nodes > 0) {
        sHierarchyNode* hierarchy = NewHierarchy(nodes);
        list->extra = reinterpret_cast<uint8_t*>(hierarchy);
        File::Read(hierarchy, sizeof(sHierarchyNode), nodes, file); // (null when out of memory, as the original)
        list->unknown78 = static_cast<uint32_t>(nodes);
    }
    File::Close(file);
    return scene;
}

// 0x004B3990
void* __cdecl CreateFrameSource()
{
    void* memory = g_frameSourcePools.Allocate(0x214);
    return memory ? static_cast<CFrameSource*>(memory)->Construct() : nullptr;
}

// 0x004B3A20
void* __cdecl LoadMeshFile(const char* name, void* textures)
{
    auto* scene = static_cast<uint8_t*>(LoadSceneFromFile(name, textures, false, false, false, 0));
    *reinterpret_cast<uint32_t*>(scene + 0x84) = Crc::GenerateCRCFromString(name);
    *reinterpret_cast<void**>(scene + 0x88) = textures;
    SetUpSectors(scene, name);
    ClearScreenClip();
    void* memory = Mem::Calloc(0xC8);
    CPcMesh* mesh = memory ? static_cast<CPcMesh*>(memory)->ConstructFromFile(name) : nullptr;
    SetMeshScene(mesh, scene);
    *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(mesh) + 4) = textures; // (the base mesh's)
    return mesh;
}

// 0x004B3AD0
void* __cdecl LoadMeshFromMemory(uint32_t checksum, const uint8_t* data, int /*unused*/, void* skin, void* textures)
{
    char name[16];
    std::snprintf(name, sizeof(name), "%d\n", static_cast<int>(checksum)); // (with a line feed, as the original)
    auto* scene = static_cast<uint8_t*>(LoadSceneFromMemory(data, textures, false, false, false));
    *reinterpret_cast<uint32_t*>(scene + 0x84) = Crc::GenerateCRCFromString(name);
    *reinterpret_cast<void**>(scene + 0x88) = textures;
    SetUpSectors(scene, name);
    void* memory = Mem::Calloc(0xC8);
    CPcMesh* mesh = memory ? static_cast<CPcMesh*>(memory)->Construct() : nullptr;
    mesh->LoadSkin(static_cast<const uint8_t*>(skin));
    ClearScreenClip();
    SetMeshScene(mesh, scene);
    *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(mesh) + 4) = textures; // (the base mesh's)
    return mesh;
}

} // namespace Nx

REPLACE(0x004B2D30, &Nx::sHierarchyNode::Construct);
REPLACE(0x004B3490, Nx::LoadSceneFromMemory);
REPLACE(0x004B36D0, Nx::LoadSceneFromFile);
REPLACE(0x004B3990, Nx::CreateFrameSource);
REPLACE(0x004B3A20, Nx::LoadMeshFile);
REPLACE(0x004B3AD0, Nx::LoadMeshFromMemory);

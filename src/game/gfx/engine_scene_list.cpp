// The engine's loaded scenes: see engine_scene_list.h.

#include "game/gfx/engine_scene_list.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/scene_load.h"
#include "game/gfx/texture_dict.h"
#include "game/mth/geometry.h"
#include "game/mth/vector.h"
#include "game/nx/coll_grid.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kHeadVtable = 0x00648A3C;

// The list's first node (the head's next, 0x006FA7EC; the head itself when the list is empty).
Mem::ListNode* FirstNode() { return GameVariable<Mem::ListNode*>(0x006FA7EC); }
bool IsHead(const Mem::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
// The node after `node` (null at the end).
Mem::ListNode* NextNode(const Mem::ListNode* node)
{
    Mem::ListNode* next = node->next;
    return IsHead(next) ? nullptr : next;
}
CScene* SceneOf(const Mem::ListNode* node) { return static_cast<CScene*>(node->data); }

// The first scene whose m_isSky is `sky` (the original's walk: the first node's data checked before the node).
CScene* FirstScene(bool sky)
{
    Mem::ListNode* node = FirstNode();
    if (IsHead(node) || !node)
        return nullptr;
    for (;;) {
        Mem::ListNode* next = NextNode(node);
        if (SceneOf(node)->m_isSky == sky)
            return SceneOf(node);
        if (!next)
            return nullptr;
        node = next;
    }
}

// The rest of the engine's scene calls (0x0048C560-0x0048C74F, not rewritten yet; cdecl): a scene registered (its
// name's checksum, its textures, PostLoad unless `skipPostLoad`, a node at the end of the list; the sixth argument
// isn't read), a scene unloaded (with `flag` its collision too), the scene of a checksum, every scene's added scene
// shown or hidden.
void RegisterScene(CScene* scene, const char* name, CTexDict* textures, int skipPostLoad, void* arena, int unused)
{
    GameFunction<void(__cdecl*)(CScene*, const char*, CTexDict*, int, void*, int)>(0x0048C560)(
        scene, name, textures, skipPostLoad, arena, unused);
}
void UnloadEngineScene(CScene* scene, int flag) { GameFunction<void(__cdecl*)(CScene*, int)>(0x0048C690)(scene, flag); }
CScene* GetScene(uint32_t checksum) { return GameFunction<CScene*(__cdecl*)(uint32_t)>(0x0048C700)(checksum); }
void ToggleAddScenes() { GameFunction<void(__cdecl*)()>(0x0048C650)(); }

// The scene list's node's destructor body (0x0048C4B0: unlinked, its own vtable).
void DestroyNodeBody(Mem::ListNode* node) { GameFunction<void(__thiscall*)(Mem::ListNode*)>(0x0048C4B0)(node); }

// Compiled out (0x005B9E50, false): the platform's own load of an added scene's geometry.
bool PlatLoadAddScene(CScene* /*scene*/, const char* /*fileName*/) { return false; }
// "levels\<name>\<name>.scn.<the platform's extension>" (the original's sprintf; no length limit there).
void SceneFileName(char* out, size_t size, const char* name)
{
    std::snprintf(out, size, "levels\\%s\\%s%s.scn.%s", name, name, "", PlatformExtension());
}

} // namespace

// 0x0048CC30
CSceneListHead* CSceneListHead::Construct()
{
    data = reinterpret_cast<void*>(-1);
    priority = 0;
    next = this;
    prev = this;
    vtable = reinterpret_cast<void**>(kHeadVtable);
    return this;
}

// 0x0048CC50
CSceneListHead* CSceneListHead::Destroy(unsigned int flags)
{
    DestroyNodeBody(this);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0048C7F0
Mem::ListNode* __cdecl FirstSceneNode()
{
    Mem::ListNode* node = FirstNode();
    return IsHead(node) ? nullptr : node;
}

// 0x0048C750
CScene* __cdecl GetSceneByName(const char* name)
{
    return GetScene(Crc::GenerateCRCFromString(name));
}

// 0x0048C770
CScene* __cdecl GetMainScene()
{
    return FirstScene(false);
}

// 0x0048C7B0
CScene* __cdecl GetSkyScene()
{
    return FirstScene(true);
}

// 0x0048C810
bool __cdecl UnloadAllScenes()
{
    Mem::ListNode* node = FirstNode();
    if (!IsHead(node) && node) {
        do {
            Mem::ListNode* next = NextNode(node);
            CScene* scene = SceneOf(node);
            CTexDict* textures = scene->mp_texDict;
            UnloadEngineScene(scene, 1);
            if (textures)
                UnloadTexDict(textures);
            node = next;
        } while (node);
    }
    ClearDebugLines(); // (the original pushes a 0 it doesn't read)
    return true;
}

// 0x0048C870
void __cdecl AddShadowCaster(const uint8_t* texture, void* model, CScene* scene)
{
    if (!scene)
        scene = GetMainScene();
    AddTextureShadowCaster(texture, model, reinterpret_cast<uint32_t>(scene));
}

// 0x0048C8F0
void __cdecl PrintCollStatics()
{
    Tmr::GetTime();
    Mem::ListNode* node = FirstNode();
    if (!IsHead(node) && node) {
        do {
            Mem::ListNode* next = NextNode(node);
            CScene* scene = SceneOf(node);
            if (!scene->m_isSky)
                scene->PrintCollStatics();
            node = next;
        } while (node);
    }
    Tmr::GetTime();
}

// 0x0048C940
CCollGrid* __cdecl GetFirstCollGrid()
{
    Mem::ListNode* node = FirstNode();
    if (IsHead(node) || !node)
        return nullptr;
    do {
        Mem::ListNode* next = NextNode(node);
        if (CCollGrid* grid = SceneOf(node)->mp_collGrid)
            return grid;
        node = next;
    } while (node);
    return nullptr;
}

// 0x0048C980
CCollGrid** __cdecl GetCollGridsOnLine(const Mth::Line* line)
{
    auto* scenes = &GameVariable<CScene*>(0x006FA738);
    auto* grids = &GameVariable<CCollGrid*>(0x006FA778);
    Mth::BBox box(line->start);
    box.AddPoint(line->end);
    uint32_t count = 0;
    Mem::ListNode* node = FirstNode();
    if (IsHead(node) || !node)
        return nullptr;
    do {
        Mem::ListNode* next = NextNode(node);
        CScene* scene = SceneOf(node);
        CCollGrid* grid = scene->mp_collGrid;
        // (each "past" test as the original: NaN doesn't rule a grid out)
        if (grid && !(grid->m_box.min.x > box.max.x) && !(box.min.x > grid->m_box.max.x) &&
            !(grid->m_box.min.z > box.max.z) && !(box.min.z > grid->m_box.max.z) &&
            !(grid->m_box.min.y > box.max.y) && !(box.min.y > grid->m_box.max.y)) {
            scenes[count] = scene;
            grids[count] = grid;
            if (++count >= 15)
                break;
        }
        node = next;
    } while (node);
    if (count == 0)
        return nullptr;
    grids[count] = nullptr;
    scenes[count] = nullptr;
    return grids;
}

// 0x0048CA80
uint32_t __cdecl NewSectorName()
{
    uint32_t& counter = GameVariable<uint32_t>(0x0067DC84);
    for (;;) {
        uint32_t name = counter;
        bool used = false;
        Mem::ListNode* node = FirstNode();
        if (!IsHead(node) && node) {
            do {
                Mem::ListNode* next = NextNode(node);
                if (SceneOf(node)->GetSector(name)) {
                    used = true;
                    break;
                }
                node = next;
            } while (node);
        }
        if (!used)
            return counter++;
        ++counter;
    }
}

// 0x0048CAF0
bool __cdecl CFunc_ToggleAddScenes(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    ToggleAddScenes();
    return true;
}

// 0x0048CB00
void __cdecl GetSceneDebugInfo(Script::CStruct* info)
{
    if (GetMainScene()) {
        Script::CStruct* main = Script::NewStruct();
        if (main)
            main->Init();
        GetMainScene()->GetDebugInfo(main);
        info->AddStructurePointer(0x9B10525Cu, main);
    }
    if (GetSkyScene()) {
        Script::CStruct* sky = Script::NewStruct();
        if (sky)
            sky->Init();
        GetSkyScene()->GetDebugInfo(sky);
        info->AddStructurePointer(0x02C5EA34u, sky);
    }
}

// 0x0048CC80
CScene* __cdecl NewScene(const char* name, CTexDict* textures, bool addSuperSectors)
{
    auto* scene = static_cast<CScene*>(Nx::CreateScene(name, textures, addSuperSectors));
    RegisterScene(scene, name, textures, 1, nullptr, 0);
    return scene;
}

// 0x0048CCB0
CScene* __cdecl LoadScene(const char* name, CTexDict* textures, bool addSuperSectors, bool isSky, bool meshFlag,
                          int unused)
{
    char file[0x80];
    SceneFileName(file, sizeof(file), name);
    Mem::PushMemProfile(file);
    uint32_t vertexArena[4] = {0, 0, 0, 0};
    void* arena = GameVariable<uint8_t>(0x00786AB6) ? vertexArena : nullptr;
    auto* scene = static_cast<CScene*>(LoadSceneFromFile(file, textures, addSuperSectors, isSky, meshFlag,
                                                         reinterpret_cast<int>(arena)));
    Mem::PopMemProfile();
    RegisterScene(scene, name, textures, 0, arena, unused);
    return scene;
}

// 0x0048CD80
CScene* __cdecl LoadNamedSceneFromMemory(const char* name, const uint8_t* data, CTexDict* textures, bool addSuperSectors,
                                    bool isSky, bool meshFlag, int unused)
{
    Mem::PushMemProfile("Scene Processing");
    auto* scene = static_cast<CScene*>(Nx::LoadSceneFromMemory(data, textures, addSuperSectors, isSky, meshFlag));
    Mem::PopMemProfile();
    RegisterScene(scene, name, textures, 0, nullptr, unused);
    return scene;
}

// 0x0048CDD0
CScene* __cdecl AddScene(const char* sceneName, const char* addName)
{
    char file[0x80];
    char texFile[0x80];
    SceneFileName(file, sizeof(file), addName);
    std::snprintf(texFile, sizeof(texFile), "levels\\%s\\%s.tex", addName, addName);
    CScene* scene = GetScene(Crc::GenerateCRCFromString(sceneName));
    if (std::strcmp(scene->m_addName, file) == 0) {
        scene->UnloadAddedScene();
        if (CTexDict* old = FindTexDict(Crc::GenerateCRCFromString(texFile)))
            UnloadTexDict(old);
    }
    CTexDict* textures = LoadTexDict(texFile, 1, 0, 0, 0);
    PlatLoadAddScene(scene, file);
    scene->FinishAdd(file, reinterpret_cast<uint32_t>(textures));
    return scene;
}

// 0x0048CEF0
bool __cdecl ReloadScene(const char* name)
{
    CScene* scene = GetScene(Crc::GenerateCRCFromString(name));
    if (!scene)
        return false;
    bool isSky = scene->m_isSky;
    CTexDict* textures = scene->mp_texDict;
    UnloadEngineScene(scene, 1);
    LoadScene(name, textures, !isSky, isSky, false, 0);
    return true;
}

} // namespace Nx

REPLACE(0x0048C750, Nx::GetSceneByName);
REPLACE(0x0048C770, Nx::GetMainScene);
REPLACE(0x0048C7B0, Nx::GetSkyScene);
REPLACE(0x0048C7F0, Nx::FirstSceneNode);
REPLACE(0x0048C810, Nx::UnloadAllScenes);
REPLACE(0x0048C870, Nx::AddShadowCaster);
REPLACE(0x0048C8F0, Nx::PrintCollStatics);
REPLACE(0x0048C940, Nx::GetFirstCollGrid);
REPLACE(0x0048C980, Nx::GetCollGridsOnLine);
REPLACE(0x0048CA80, Nx::NewSectorName);
REPLACE(0x0048CAF0, Nx::CFunc_ToggleAddScenes);
REPLACE(0x0048CB00, Nx::GetSceneDebugInfo);
REPLACE(0x0048CC30, &Nx::CSceneListHead::Construct);
REPLACE(0x0048CC50, &Nx::CSceneListHead::Destroy);
REPLACE(0x0048CC80, Nx::NewScene);
REPLACE(0x0048CCB0, Nx::LoadScene);
REPLACE(0x0048CD80, Nx::LoadNamedSceneFromMemory);
REPLACE(0x0048CDD0, Nx::AddScene);
REPLACE(0x0048CEF0, Nx::ReloadScene);

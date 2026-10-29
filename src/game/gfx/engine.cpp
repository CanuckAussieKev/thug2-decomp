// The engine's own entry points (Nx::Engine): see engine.h. From the disassembly of 0x0048BF20-0x0048C741.

#include "game/gfx/engine.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/particles.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/pc_font.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/render.h"
#include "game/gfx/scene_load.h"
#include "game/gfx/screen.h"
#include "game/gfx/spawned_polys.h"
#include "game/gfx/viewport.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (sprintf / strcpy: the original's file names, unbounded as it builds them)

namespace Nx {

namespace {

void** const kSceneNodeVtable = reinterpret_cast<void**>(0x00648A38);

constexpr uint32_t kCameraFov = 0x99529205;     // camera_fov
constexpr float kAspectRatio = 1.33333337f;     // 0x3FAAAAAB (4:3)
const char* const kSceneFileFormat = "levels\\%s\\%s%s.scn.%s"; // 0x00648A20 (the third: "", 0x006469B4)
const char* const kModelFileFormat = "%s.%s.%s";                // 0x006458F4

// The scenes' list (0x006FA7E0: its head, data -1; the first node at +0xC, the last at +0x10).
Mem::ListNode& SceneList() { return GameVariable<Mem::ListNode>(0x006FA7E0); }
const auto kNoScene = reinterpret_cast<void*>(static_cast<uintptr_t>(0xFFFFFFFF));
// The node after `node`, null at the end (the head).
CSceneNode* NextScene(Mem::ListNode* node)
{
    return node->next->data == kNoScene ? nullptr : static_cast<CSceneNode*>(node->next);
}
// The first node (null when there's none - the original reads the head's data before checking for no list).
CSceneNode* FirstScene()
{
    Mem::ListNode* first = SceneList().next;
    if (first->data == kNoScene || !first)
        return nullptr;
    return static_cast<CSceneNode*>(first);
}
CScene* SceneOf(Mem::ListNode* node) { return static_cast<CScene*>(node->data); }

// The engine's managers: the flexible particle manager (0x006FA7C0), the imposter manager (0x006FA7BC, 8 bytes:
// constructor 0x0048E3E0, its frame 0x0048E2D0 - not rewritten yet), the particle manager and the weather (made by
// StartEngine).
void*& FlexibleParticleManager() { return GameVariable<void*>(0x006FA7C0); }
void*& ImposterManager() { return GameVariable<void*>(0x006FA7BC); }
CParticleManager* ParticleManager() { return GameVariable<CParticleManager*>(0x006FA7C4); }
void* Weather() { return GameVariable<void*>(0x006FA7C8); }
bool SkipRender() { return GameVariable<bool>(0x007D1EC0); }
int ViewportCount() { return GameVariable<int>(0x00701378); }

// (0x004904E0, not rewritten yet: the frame's ... after the imposters)
void UpdateAfterImposters() { GameFunction<void(__cdecl*)()>(0x004904E0)(); }

// The compiled-out call (0x005480A0, no arguments) DeleteGeomObject and UnloadScene make first.
inline void DebugBeforeDelete() {}

} // namespace

// 0x0048C4B0
void CSceneNode::DestroyBody() { UnlinkAs(kSceneNodeVtable); }

// 0x0048C4D0
CSceneNode* CSceneNode::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

namespace Engine {

// 0x0048C1D0
void __cdecl Init(uint32_t textPoolSize)
{
    Mem::PushMemProfile("sAllocTextPool");
    AllocateTextPool(textPoolSize);
    Mem::PopMemProfile();
    Mem::PushMemProfile("sAllocWindow2DPool");
    CreateWindowPool();
    Mem::PopMemProfile();
    SetScreenAspectRatio(kAspectRatio);
    SetScreenAngle(Script::GetFloat(kCameraFov));
    SetScreenFieldOfView(0.0f);
    Mem::PushMemProfile("CViewportManager");
    CreateViewports();
    SetScreenMode(0);
    Mem::PopMemProfile();
    Mem::PushMemProfile("CFlexibleParticleManager");
    void* memory = Mem::New(sizeof(CFlexibleParticleManager));
    FlexibleParticleManager() = memory ? static_cast<CFlexibleParticleManager*>(memory)->Construct() : nullptr;
    Mem::PopMemProfile();
    Mem::PushMemProfile("CImposterManager");
    memory = Mem::New(8);
    ImposterManager() = memory ? GameFunction<void*(__thiscall*)(void*)>(0x0048E3E0)(memory) : nullptr;
    Mem::PopMemProfile();
    Mem::PushMemProfile("MiscFXInitialize");
    InitEffects();
    Mem::PopMemProfile();
    StartEngine();
}

// 0x0048BF20
void __cdecl EndRender()
{
    FinishRendering();
    DeleteMarkedCameras();
}

// 0x0048C330
void __cdecl RenderFrame()
{
    UpdateFlashes();
    UpdateSpawnedPolys();
    AgeAllTextureSplats();
    UpdateShatters();
    ParticleManager()->UpdateParticles();
    // the weather's frame (vtable slot 2 - through the folded jump 0x004E3F90), by the slow-motion frame time
    void* weather = Weather();
    reinterpret_cast<void(__thiscall*)(void*, float)>((*static_cast<void***>(weather))[2])(
        weather, static_cast<float>(Tmr::GetSlomoFrameSeconds()));
    GameFunction<void(__thiscall*)(void*)>(0x0048E2D0)(ImposterManager());
    UpdateAfterImposters();
    if (!SkipRender())
        RenderWorld();
}

// 0x0048C0A0
void __cdecl SetObjectManager(void* manager) { GameVariable<void*>(0x006FA720) = manager; }

// 0x0048C0B0
uint32_t* __cdecl GetCollisionScenes() { return &GameVariable<uint32_t>(0x006FA738); }

// 0x0048C0D0
void __cdecl ToggleRenderMode()
{
    int& mode = GameVariable<int>(0x006FA734);
    if (++mode == 4)
        mode = 0;
}

// 0x0048C0F0
void __cdecl ToggleMetrics()
{
    bool& on = GameVariable<bool>(0x006FA7D6);
    on = !on;
}

// 0x0048C110
void __cdecl SetRenderIgnore(int ignore1, int ignore0)
{
    GameVariable<int>(0x006FA724) = ignore0;
    GameVariable<int>(0x006FA728) = ignore1;
}

// 0x0048C1C0
IdleFunction __cdecl GetIdleFunction() { return GameVariable<IdleFunction>(0x006FA7D8); }

// 0x0048BFE0
bool __cdecl SplitSkinName(const char* name, char* base)
{
    std::strcpy(base, name);
    char* skin = std::strstr(base, ".skin");
    bool found = skin != nullptr;
    if (found)
        *skin = '\0';
    if (char* model = std::strstr(base, ".mdl"))
        *model = '\0';
    return found;
}

// 0x0048C030
void* __cdecl LoadModelFromMemory(uint32_t checksum, const uint8_t* data, int size, void* skin, void* textures, int,
                                  uint32_t, uint32_t)
{
    Mem::PushMemProfile("model_data_buffer");
    void* model = LoadMeshFromMemory(checksum, data, size, skin, textures);
    Mem::PopMemProfile();
    return model;
}

// 0x0048C390
void __cdecl SceneFileName(char* out, const char* name, uint32_t)
{
    std::sprintf(out, kSceneFileFormat, name, name, "", PlatformExtension());
}

// 0x0048C3C0
bool __cdecl DeleteGeomObject(void* object)
{
    DebugBeforeDelete();
    return DeleteModelLights(static_cast<CPcModelLights*>(object));
}

// 0x0048C3D0
void* __cdecl LoadModelFile(const char* fileName, void* textures, int, bool)
{
    char base[0x200];
    bool skinned = SplitSkinName(fileName, base);
    Str::LowerCase(base);
    char path[0x200];
    std::sprintf(path, kModelFileFormat, base, skinned ? "skin" : "mdl", PlatformExtension());
    Mem::PushMemProfile(path);
    // (the original also passes the texture dictionary offset, whether it's skinned and the shadow volume flag: the
    // mesh load doesn't read them)
    void* mesh = LoadMeshFile(path, textures);
    Mem::PopMemProfile();
    return mesh;
}

// 0x0048C490
int __cdecl SphereVisibleInOneViewport(const Mth::Vector* centre, float radius)
{
    if (ViewportCount() > 1)
        return 0;
    return SphereVisible(centre, radius);
}

// 0x0048C510
void* __cdecl GetSector(uint32_t checksum)
{
    for (CSceneNode* node = FirstScene(); node;) {
        CSceneNode* next = NextScene(node);
        if (void* sector = SceneOf(node)->GetSector(checksum))
            return sector;
        node = next;
    }
    return nullptr;
}

// 0x0048C560
void __cdecl AddScene(CScene* scene, const char* name, void* textures, bool loaded, void* arena)
{
    char file[0x80];
    std::sprintf(file, kSceneFileFormat, name, name, "", PlatformExtension());
    scene->m_checksum = Crc::GenerateCRCFromString(name);
    scene->mp_texDict = static_cast<CTexDict*>(textures);
    if (!loaded)
        scene->FinishLoad(file, arena);
    Mem::PushMemProfile("Asset Overhead");
    auto* node = static_cast<CSceneNode*>(Mem::Calloc(sizeof(CSceneNode)));
    if (node) {
        node->vtable = kSceneNodeVtable;
        node->data = scene;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    // at the end of the list (no memory: a crash, as the original)
    Mem::ListNode& head = SceneList();
    node->prev = head.prev;
    node->next = &head;
    head.prev->next = node;
    head.prev = node;
    Mem::PopMemProfile();
}

// 0x0048C650
void __cdecl ToggleAddScenes()
{
    for (CSceneNode* node = FirstScene(); node;) {
        CSceneNode* next = NextScene(node);
        SceneOf(node)->ToggleAddScene();
        node = next;
    }
}

// 0x0048C690
bool __cdecl UnloadScene(CScene* scene, int /*flags*/)
{
    DebugBeforeDelete();
    scene->UnloadAddedScene();
    if (!Nx::UnloadScene(scene)) // (the platform's: the original passes `flags` on too - unused)
        return false;
    if (scene)
        reinterpret_cast<void*(__thiscall*)(CScene*, unsigned int)>((*reinterpret_cast<void***>(scene))[0])(scene, 1);
    for (CSceneNode* node = FirstScene(); node;) {
        CSceneNode* next = NextScene(node);
        if (SceneOf(node) == scene) {
            reinterpret_cast<void*(__thiscall*)(CSceneNode*, unsigned int)>(node->vtable[0])(node, 1);
            return true;
        }
        node = next;
    }
    return false;
}

// 0x0048C700
CScene* __cdecl FindScene(uint32_t checksum)
{
    for (CSceneNode* node = FirstScene(); node;) {
        CSceneNode* next = NextScene(node);
        if (SceneOf(node)->m_checksum == checksum)
            return SceneOf(node);
        node = next;
    }
    return nullptr;
}

} // namespace Engine

} // namespace Nx

REPLACE(0x0048BF20, Nx::Engine::EndRender);
REPLACE_SOURCE_ONLY(0x0048BFC0, Nx::CreateFrameSource);  // a jump to 0x004B3990
REPLACE(0x0048BFE0, Nx::Engine::SplitSkinName);
REPLACE(0x0048C030, Nx::Engine::LoadModelFromMemory);
REPLACE_SOURCE_ONLY(0x0048C080, Nx::DeleteModelLights); // a jump to 0x004B6920
REPLACE(0x0048C0A0, Nx::Engine::SetObjectManager);
REPLACE(0x0048C0B0, Nx::Engine::GetCollisionScenes);
REPLACE(0x0048C0D0, Nx::Engine::ToggleRenderMode);
REPLACE(0x0048C0F0, Nx::Engine::ToggleMetrics);
REPLACE(0x0048C110, Nx::Engine::SetRenderIgnore);
REPLACE(0x0048C1C0, Nx::Engine::GetIdleFunction);
REPLACE(0x0048C1D0, Nx::Engine::Init);
REPLACE(0x0048C330, Nx::Engine::RenderFrame);
REPLACE(0x0048C390, Nx::Engine::SceneFileName);
REPLACE(0x0048C3C0, Nx::Engine::DeleteGeomObject);
REPLACE(0x0048C3D0, Nx::Engine::LoadModelFile);
REPLACE(0x0048C490, Nx::Engine::SphereVisibleInOneViewport);
REPLACE(0x0048C4B0, &Nx::CSceneNode::DestroyBody);
REPLACE(0x0048C4D0, &Nx::CSceneNode::Destroy);
REPLACE(0x0048C510, Nx::Engine::GetSector);
REPLACE(0x0048C560, Nx::Engine::AddScene);
REPLACE(0x0048C650, Nx::Engine::ToggleAddScenes);
REPLACE(0x0048C690, Nx::Engine::UnloadScene);
REPLACE(0x0048C700, Nx::Engine::FindScene);

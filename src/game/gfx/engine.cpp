// The engine's own entry points (Nx::Engine): see engine.h. From the disassembly of 0x0048BF20-0x0048C741.

#include "game/gfx/engine.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_imposters.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/particles.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/pc_font.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/render.h"
#include "game/gfx/scene_lights.h"
#include "game/gfx/scene_load.h"
#include "game/gfx/screen.h"
#include "game/gfx/spawned_polys.h"
#include "game/gfx/viewport.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/obj/particle_component.h"
#include "game/skate/skate_module.h"
#include "game/sys/startup_init.h"

#pragma warning(disable : 4996) // (sprintf / strcpy: the original's file names, unbounded as it builds them)

namespace Nx {

bool g_skipRender; // rendering skipped (frames not drawn or shown)

void* g_flexibleParticleManager;
void* g_imposterManager;
int g_renderMode;
// The scenes' list (its head: data -1 once made; its first node, the head's next, is at +0xC, the last at +0x10).
Mem::ListNode g_sceneList;
namespace {

extern void** const kSceneNodeVtable;

constexpr uint32_t kCameraFov = 0x99529205;     // camera_fov
constexpr float kAspectRatio = 1.33333337f;     // 0x3FAAAAAB (4:3)
const char* const kSceneFileFormat = "levels\\%s\\%s%s.scn.%s"; // 0x00648A20 (the third: "", 0x006469B4)
const char* const kModelFileFormat = "%s.%s.%s";                // 0x006458F4

// The scenes' list (0x006FA7E0: its head, data -1; the first node at +0xC, the last at +0x10).
Mem::ListNode& SceneList() { return g_sceneList; }
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

void* g_objectManager;
int g_renderIgnore0;
int g_renderIgnore1;
bool g_metricsOn;
Engine::IdleFunction g_idleFunction;

// The engine's managers: the flexible particle manager (0x006FA7C0), the imposter manager (0x006FA7BC, 8 bytes:
// constructor 0x0048E3E0, its frame 0x0048E2D0 - not rewritten yet), the particle manager and the weather (made by
// StartEngine).
void*& FlexibleParticleManager() { return g_flexibleParticleManager; }
void*& ImposterManager() { return g_imposterManager; }
CParticleManager* ParticleManager() { return Obj::g_particleManager; }
void* Weather() { return g_weather; }
bool SkipRender() { return g_skipRender; }
int ViewportCount() { return Mdl::g_viewportCount; }

// (0x004904E0, not rewritten yet: the frame's ... after the imposters)
void UpdateAfterImposters() { UpdateSceneLightQueue(); }

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
    ImposterManager() = memory ? static_cast<CImposterManager*>(memory)->Construct() : nullptr;
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
    static_cast<CImposterManager*>(ImposterManager())->Update();
    UpdateAfterImposters();
    if (!SkipRender())
        RenderWorld();
}

// 0x0048C0A0
void __cdecl SetObjectManager(void* manager) { g_objectManager = manager; }

// 0x0048C0B0
uint32_t* __cdecl GetCollisionScenes() { return reinterpret_cast<uint32_t*>(g_collisionScenes); }

// 0x0048C0D0
void __cdecl ToggleRenderMode()
{
    int& mode = g_renderMode;
    if (++mode == 4)
        mode = 0;
}

// 0x0048C0F0
void __cdecl ToggleMetrics()
{
    bool& on = g_metricsOn;
    on = !on;
}

// 0x0048C110
void __cdecl SetRenderIgnore(int ignore1, int ignore0)
{
    g_renderIgnore0 = ignore0;
    g_renderIgnore1 = ignore1;
}

// 0x0048C1C0
IdleFunction __cdecl GetIdleFunction() { return g_idleFunction; }

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

namespace {
static VtableSlot kSceneNodeVtable_slots[] = {&Nx::CSceneNode::Destroy};
void** const kSceneNodeVtable = reinterpret_cast<void**>(kSceneNodeVtable_slots);
} // namespace

} // namespace Nx

namespace Nx {

// 0x00640B00 (a static initialiser): the scene list, an empty head.
void ConstructSceneList() { reinterpret_cast<CSceneListHead*>(&g_sceneList)->Construct(); }

} // namespace Nx

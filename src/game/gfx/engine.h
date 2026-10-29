#pragma once

#include <cstdint>

#include "game/core/alloc.h"

// The engine's own entry points (Nx::CEngine's statics, cdecl) at 0x0048BF20-0x0048C74F: starting it, a frame's
// update and render, model and scene files, and the list of loaded scenes. (The ones that only jump into the
// platform are in engine_glue.h.) From the disassembly of 0x0048BF20-0x0048C741.
namespace Mth {
class Vector;
}

namespace Nx {

struct CScene;

// A loaded scene's node in the engine's list (Lst::Node<CScene>, 0x14 bytes, vtable 0x00648A38; data = the scene).
// The list's head is at 0x006FA7E0 (its data -1).
struct CSceneNode : Mem::ListNode {
    // 0x0048C4B0: the destructor body: out of the list (and linked to itself).
    void DestroyBody();
    // 0x0048C4D0: the scalar deleting destructor (vtable slot 0). Returns this.
    CSceneNode* Destroy(unsigned int flags);
};

namespace Engine {

// 0x0048C1D0: the engine started: the text pool (`textPoolSize` entries), the 2D window pool, the screen (4:3, the
// script float camera_fov's angle, no extra field of view), the viewports, screen mode 0, the flexible particle
// manager (0x006FA7C0), the imposter manager (0x006FA7BC), the effects, then the platform (StartEngine).
void __cdecl Init(uint32_t textPoolSize);
// 0x0048BF20: the frame finished (FinishRendering), then the cameras marked for deletion deleted.
void __cdecl EndRender();
// 0x0048C330: a frame: the flashes, spawned polygons, texture splats, shatters and particles updated, the weather
// (by the slow-motion frame time) and the imposters; 0x004904E0; then the world rendered unless rendering is skipped
// (0x007D1EC0).
void __cdecl RenderFrame();

// 0x0048C0A0: the object manager the engine's code uses (0x006FA720).
void __cdecl SetObjectManager(void* manager);
// 0x0048C0B0: the scenes the last collision query's grids belong to (0x006FA738).
uint32_t* __cdecl GetCollisionScenes();
// 0x0048C0D0: the next render mode (0x006FA734: 0-3 in turn).
void __cdecl ToggleRenderMode();
// 0x0048C0F0: the metrics display (0x006FA7D6) switched on / off.
void __cdecl ToggleMetrics();
// 0x0048C110: the render's debug ignore values (ignore_1 0x006FA728, ignore_0 0x006FA724).
void __cdecl SetRenderIgnore(int ignore1, int ignore0);
// 0x0048C1C0: the function the timer runs while waiting for a vblank (0x006FA7D8; null: none).
using IdleFunction = void(__cdecl*)();
IdleFunction __cdecl GetIdleFunction();

// 0x0048BFE0: `name` copied to `base` without a ".skin" (then a ".mdl") in it. Whether it had ".skin".
bool __cdecl SplitSkinName(const char* name, char* base);
// 0x0048C030: a model from geometry in memory (LoadMeshFromMemory, under the "model_data_buffer" profile). The last
// three arguments aren't used.
void* __cdecl LoadModelFromMemory(uint32_t checksum, const uint8_t* data, int size, void* skin, void* textures,
                                  int unused5, uint32_t unused6, uint32_t unused7);
// 0x0048C390: a level's scene file name, "levels\<name>\<name>.scn.<platform>". The third argument isn't used.
void __cdecl SceneFileName(char* out, const char* name, uint32_t unused);
// 0x0048C3C0: an engine object deleted (a compiled-out call, then DeleteModelLights' virtual delete). Its result.
bool __cdecl DeleteGeomObject(void* object);
// 0x0048C3D0: a model's mesh loaded from "<name>.skin.<platform>" (a name with ".skin") or "<name>.mdl.<platform>"
// (lower case). The last three arguments are passed on (the mesh load reads only the name and textures).
void* __cdecl LoadModelFile(const char* fileName, void* textures, int texDictOffset, bool shadowVolume);
// 0x0048C490: SphereVisible - always 0 with more than one viewport (0x00701378).
int __cdecl SphereVisibleInOneViewport(const Mth::Vector* centre, float radius);

// --- the loaded scenes (the list 0x006FA7E0) ---
// 0x0048C510: the sector `checksum` from the first scene that has it, or null.
void* __cdecl GetSector(uint32_t checksum);
// 0x0048C560: `scene` (from the file of the level `name`) named and given `textures`; unless it's `loaded`, its
// load finished (with `arena`); added at the end of the list. (No scene: a crash, as the original.)
void __cdecl AddScene(CScene* scene, const char* name, void* textures, bool loaded, void* arena);
// 0x0048C650: every scene's added scenes toggled.
void __cdecl ToggleAddScenes();
// 0x0048C690: `scene` unloaded (its added scene first) and deleted, its node too. False when the platform couldn't
// unload it or it isn't in the list.
bool __cdecl UnloadScene(CScene* scene, int flags);
// 0x0048C700: the scene named `checksum`, or null.
CScene* __cdecl FindScene(uint32_t checksum);

} // namespace Engine

} // namespace Nx

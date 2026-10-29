#pragma once

#include <cstdint>

#include "game/core/alloc.h"

namespace Mth {
struct Line;
}
namespace Script {
class CScript;
class CStruct;
}

// The engine's loaded scenes (Nx::CEngine's statics, cdecl): the list of them (Lst::Head<CScene> at 0x006FA7E0 - its
// first node at 0x006FA7EC; nodes {vtable 0x00648A38, the scene, priority, next, prev}, the head's data -1), the main
// scene and the sky told apart by the scene's m_isSky, scenes loaded from a file or from memory and registered,
// reloaded, unloaded, given a scene added over them; and what's asked of all of them - the collision grids a line
// reaches, a sector name no scene uses, their statistics.
namespace Nx {

class CCollGrid;
struct CScene;
class CTexDict;

// The list's head (Lst::Head<CScene>: vtable 0x00648A3C; the node's destructor body is 0x0048C4B0).
struct CSceneListHead : Mem::ListNode {
    // 0x0048CC30: construct - data -1, priority 0, linked to itself. Returns this.
    CSceneListHead* Construct();
    // 0x0048CC50 (virtual [0]): destroyed (unlinked, the node's body), and freed with flags & 1. Returns this.
    CSceneListHead* Destroy(unsigned int flags);
};

// 0x0048C7F0: the first node of the list (null when it's empty).
Mem::ListNode* __cdecl FirstSceneNode();
// 0x0048C750: the scene named `name` (its checksum looked up, 0x0048C700), or null.
CScene* __cdecl GetSceneByName(const char* name);
// 0x0048C770 / 0x0048C7B0: the first scene that isn't a sky / that is one (null if none).
CScene* __cdecl GetMainScene();
CScene* __cdecl GetSkyScene();
// 0x0048C810: every scene unloaded with its collision (0x0048C690), then its texture dictionary; the debug lines
// cleared. True.
bool __cdecl UnloadAllScenes();
// 0x0048C870: a texture's shadow caster added for `model` (0x004B2C30) - in `scene`, or (null) the main scene.
void __cdecl AddShadowCaster(const uint8_t* texture, void* model, CScene* scene);
// 0x0048C8F0: the static collision objects of every scene but the skies listed (timed: the time is read before and
// after, and not used).
void __cdecl PrintCollStatics();
// 0x0048C940: the first scene's collision grid that there is (null if none has one).
CCollGrid* __cdecl GetFirstCollGrid();
// 0x0048C980: the collision grids whose boxes the line's box touches (NaN doesn't rule one out), at most 15, their
// scenes alongside (0x006FA738): a null-terminated list (0x006FA778), or null if none.
CCollGrid** __cdecl GetCollGridsOnLine(const Mth::Line* line);
// 0x0048CA80: a sector name (0x0067DC84, counted on) that no scene has a sector of.
uint32_t __cdecl NewSectorName();
// 0x0048CAF0: the ToggleAddScenes script command - every scene's added scene shown or hidden (0x0048C650). True.
bool __cdecl CFunc_ToggleAddScenes(Script::CStruct* params, Script::CScript* script);
// 0x0048CB00: the main scene's and the sky's statistics (CScene::GetDebugInfo) each into a new structure, added to
// `info` (0x9B10525C, 0x02C5EA34).
void __cdecl GetSceneDebugInfo(Script::CStruct* info);
// 0x0048CC80: a new, empty scene (the platform's) registered under `name` with `textures` (0x0048C560, already
// loaded).
CScene* __cdecl NewScene(const char* name, CTexDict* textures, bool addSuperSectors);
// 0x0048CCB0: a scene loaded from "levels\<name>\<name>.scn.<ext>" (with a vertex arena of its own when 0x00786AB6
// is set) and registered (0x0048C560 - `unused` passed on, and not used there).
CScene* __cdecl LoadScene(const char* name, CTexDict* textures, bool addSuperSectors, bool isSky, bool meshFlag,
                          int unused);
// 0x0048CD80: a scene loaded from geometry in memory and registered under `name` (the same way).
CScene* __cdecl LoadNamedSceneFromMemory(const char* name, const uint8_t* data, CTexDict* textures, bool addSuperSectors,
                                    bool isSky, bool meshFlag, int unused);
// 0x0048CDD0: the scene "levels\<add>\<add>.scn.<ext>" added over the scene `sceneName` (that scene's added scene
// let go of first, with its texture dictionary, if it's this one), with its texture dictionary ("levels\<add>\
// <add>.tex"). The scene.
CScene* __cdecl AddScene(const char* sceneName, const char* addName);
// 0x0048CEF0: the scene `name` unloaded and loaded again (its textures, sky or not kept). Whether there was one.
bool __cdecl ReloadScene(const char* name);

} // namespace Nx

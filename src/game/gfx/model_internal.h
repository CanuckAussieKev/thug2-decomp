#pragma once

// What the model's source (model.cpp, model_geoms.cpp) reaches of the engine not rewritten yet - the engine's CGeom
// calls (each a jump through the geom's vtable: the slots are CPcGeom's, geom.h) and the scene / object calls - and
// the list walk. Internal to those files.

#include <cstdint>

#include "game/gfx/engine.h"
#include "game/gfx/engine_geom.h"
#include "game/gfx/model.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/skeleton.h"
#include "game/gfx/texture_dict.h"
#include "game/net/buddy_status.h"
#include "hook/game.h"

namespace Nx::ModelGeoms {

template <typename R, typename... A>
R GeomCall(uint32_t slot, void* geom, A... args)
{
    // (the geom's virtual slot `slot`: the original goes through a jump stub for each)
    return reinterpret_cast<R(__thiscall*)(void*, A...)>((*static_cast<void***>(geom))[slot])(geom, args...);
}

// The geom's slots: [3] made an instance of a scene for a model (0x0048DDF0 first sets its +4 to 1 and its +0x10 -
// multicolour - to the flag), [8] colour cleared, [9] a material pass coloured, [11] / [12] the viewport mask, [13]
// active, [27] the lights, [28] the bone matrices, [29] bones copied, [30] the bones pointed at, [31] strips broken,
// [32] the shadow, [44] - [47] UV offsets / matrix / matrix parameters.
inline void LoadGeom(void* geom, uint8_t* scene, CModel* model, bool multicolor)
{
    static_cast<CGeom*>(geom)->InitInstance(scene, model, multicolor);
}
inline void ClearGeomColor(void* geom) { GeomCall<void>(8, geom); }
inline bool SetGeomMaterialColor(void* geom, uint32_t material, int pass, uint32_t rgba)
{
    return GeomCall<bool>(9, geom, material, pass, rgba);
}
inline void SetGeomVisibility(void* geom, uint32_t mask) { GeomCall<void>(11, geom, mask); }
inline uint32_t GetGeomVisibility(void* geom) { return GeomCall<uint32_t>(12, geom); }
inline void SetGeomActive(void* geom, uint8_t active) { GeomCall<void>(13, geom, active); }
inline void SetGeomLights(void* geom, uint8_t* lights) { GeomCall<void>(27, geom, lights); }
inline void SetGeomBoneMatrices(void* geom, const Mth::Matrix* transform, uint8_t* bones, int count, int flag,
                                int index)
{
    GeomCall<void>(28, geom, transform, bones, count, flag, index);
}
inline void CopyGeomBones(void* geom, uint8_t* bones, int count) { GeomCall<void>(29, geom, bones, count); }
inline void SetGeomBones(void* geom, uint8_t* bones, int count) { GeomCall<void>(30, geom, bones, count); }
inline void BreakGeomStrips(void* geom, uint32_t mask) { GeomCall<void>(31, geom, mask); }
inline void CleanGeomInstanceStrips(void* geom) { GeomCall<void>(4, geom); }
inline void SetGeomShadow(void* geom, uint8_t on) { GeomCall<void>(32, geom, on); }
// Slot [6], called straight through the vtable: the geom's colour (RGBA).
inline void SetGeomColor(void* geom, uint32_t rgba)
{
    auto** vtable = *static_cast<void***>(geom);
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(vtable[6])(geom, rgba);
}
// The geom's multicolour flag (+0x10, the folded getter 0x005480B0).
inline bool IsGeomMulticolor(void* geom) { return reinterpret_cast<GameNet::FoldedGetters*>(geom)->Byte10(); }

// An engine object deleted (0x0048C3C0) / a scene or lights deleted (0x0048C080, cdecl: the second argument only
// some callers pass); a texture dictionary unloaded (0x0049EFD0).
inline void DeleteGeomObject(void* object) { Engine::DeleteGeomObject(object); }
inline void DeleteScene(void* scene) { DeleteModelLights(static_cast<CPcModelLights*>(scene)); }
inline void DeleteScene(void* scene, int) { DeleteModelLights(static_cast<CPcModelLights*>(scene)); }
inline void UnloadTextures(void* textures) { UnloadTexDict(static_cast<CTexDict*>(textures)); }

// The model's own virtual slots.
template <typename R, typename... A>
R ModelVirtual(CModel* model, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CModel*, A...)>(model->vtable[slot])(model, args...);
}

// A skeleton's bone matrices (+0x04, the folded getter 0x005480C0) and bone count (+0x14, 0x004A2BA0).
inline uint8_t* SkeletonBones(uint8_t* skeleton)
{
    return reinterpret_cast<uint8_t*>(reinterpret_cast<GameNet::FoldedGetters*>(skeleton)->Word04());
}
inline int SkeletonBoneCount(uint8_t* skeleton)
{
    return reinterpret_cast<Gfx::CSkeleton*>(skeleton)->GetNumBones();
}

// The list's first node (none without a head - which the original then reads through anyway).
inline sModelGeomNode* First(const CModel* model) { return model->m_geoms ? model->m_geoms->next : nullptr; }
inline sModelGeom* Entry(sModelGeomNode* node) { return reinterpret_cast<sModelGeom*>(node->entry); }
inline void* Geom(sModelGeomNode* node) { return Entry(node)->geom; }

// A bit of the visible-geoms mask (the original shifts by the index's low five bits).
inline uint32_t GeomBit(int index) { return 1u << (index & 31); }

} // namespace Nx::ModelGeoms

#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/entity.h"

// The model builder (Gfx2D::CModelAssembler, an GameObjects::CEntity, 0x54 bytes, vtable 0x00648920): builds a model from an
// appearance by running a build script on itself - the script's member functions (ModelAddGeom, GeomReplaceTexture,
// ModelApplyBoneScale, ...) each do one step on the model, its skeleton and a copy of the appearance, which are held
// in globals while Build runs. The steps are virtual (slots 9-25; the script's names go through InvokeMethodByName).
// From the disassembly of 0x004897D0-0x0048B32D.
namespace Engine3D {
class CRenderModel;
}
namespace Gfx2D {
class CRig;
class CModelLook;

class CModelAssembler : public GameObjects::CEntity {
public:
    // 0x004897D0: a CEntity with its own vtable; `viaResourceStore` and the texture dictionary offset its geoms are
    // loaded with. Returns this.
    CModelAssembler* MakeNew(bool viaResourceStore, int texDictOffset);
    // 0x0048A2C0: the scalar deleting destructor (vtable slot 0: CEntity's destructor body - 0x00420060 - and freed
    // with flags & 1). Returns this.
    CModelAssembler* Teardown(unsigned int flags);

    // 0x0048A2F0 (slot 8): `model` (with `skeleton`) built from `appearance` by the build script `script`: a copy of
    // the appearance made on the top-down heap, the script run on the builder, the copy deleted.
    void Build(CModelLook* appearance, Engine3D::CRenderModel* model, CRig* skeleton, uint32_t script);
    // 0x0048A3B0 (slot 3): the build script's member functions (their parameters read here, then the step's slot);
    // anything else CEntity's.
    bool InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);

    // --- the steps (vtable slots 9-25, in order; each returns whether it did what it was asked, mostly true) ---
    // 0x00489820 (slot 9): the geoms of the part in the slot `part`: with a face texture, its mesh_if_facemapped
    // alone if it has one; else each of mesh, mesh1 ... mesh4 it has.
    bool AddGeom(uint32_t part);
    // 0x0048A9C0 (slot 10): the slot `part` taken out of the appearance.
    bool RemovePart(uint32_t part);
    // 0x00489800 (slot 11): the model's geom `geom` hidden or shown.
    bool ConcealMesh(uint32_t geom, bool hide);
    // 0x00489A50 (slot 12): the colour of the geom of the slot `params` names (part): its own h / s / v, or the part's
    // defaults (then false; none: the colour cleared, false). PaperBag and bigfoot parts aren't coloured (false).
    bool BlendTintOver(Scripts::CStruct* params);
    // 0x0048B030 (slot 13): unless the slot `part` keeps its default UVs (no use_default_uv, or non-zero: false), the
    // material's pass gets a UV matrix: turned about the middle by uv_rot, scaled by uv_scale %, moved by uv_u / uv_v %.
    bool ChooseUvShift(uint32_t part, uint32_t material, int pass);
    // 0x00489C30 (slot 14): the material's pass given UV matrix parameters.
    bool CreateUvAnimParams(uint32_t material, int pass);
    // 0x00489C50 (slot 15): the part's texture replacements (replace / with / in, then 1-6). The last one's result
    // (false: none, or no part).
    bool SwapTexture(uint32_t part);
    // 0x0048ADD0 (slot 16): a part that is a CreatedGraphic: its temporary texture drawn into the cag_workspace
    // texture (scaled, turned, over the deck's base texture), which replaces the model's logo texture. The
    // replacement's result (false: no such part).
    bool SetCreatedTexture(uint32_t part);
    // 0x00489A30 / 0x00489A40 (slots 17 / 18): the model's geoms cleared / their own strips broken. True.
    bool ClearAllGeoms();
    bool DetachPolys();
    // 0x0048A9E0 (slot 19): the model's scale 1 and the skeleton's bone scales reset (false: no skeleton).
    bool RestoreScale();
    // 0x0048AA30 (slot 20): the appearance's body_shape applied to the skeleton's bones (false: none).
    bool ApplyBodyShape();
    // 0x0048AA70 (slot 21): the slot `part`'s (or else `castPart`'s option's) x / y / z % scale applied to each bone in
    // the global array `part` (local, unless the bone is in nonlocal_bones). Always false.
    bool ApplyBoneSizing(uint32_t part, uint32_t castPart);
    // 0x0048AC40 (slot 22): the model's scale from the appearance's body type, else its body_shape, else the
    // object_scaling slot's x / y / z %. Whether it was set (true with an incomplete object_scaling too).
    bool ApplyObjectScale();
    // 0x0048A220 (slot 23): the model's instance strips cleaned. True.
    bool Seal();
    // 0x00489F10 (slot 24): the head's colour (its own, else its part's defaults) given to the torso (unless None,
    // men only), the hands (only the Hands part), the lower legs, the shoes (bare feet only) and the body. False
    // without a head.
    bool PropagateHeadColor();
    // 0x0048B200 (slot 25): the script `target` held in the part's option run on the appearance until it ends. True.
    bool LaunchScript(uint32_t part, uint32_t target);

    bool m_viaAssetStore; // +0x4C
    int m_dictShift;    // +0x50

private:
    template <typename R, typename... A>
    R Step(int slot, A... args)
    {
        return reinterpret_cast<R(__thiscall*)(CModelAssembler*, A...)>(m_vtable[slot])(this, args...);
    }
};
static_assert(offsetof(CModelAssembler, m_viaAssetStore) == 0x4C && offsetof(CModelAssembler, m_dictShift) == 0x50 &&
                  sizeof(CModelAssembler) == 0x54,
              "layout must match the original");

} // namespace Gfx

#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Nx {
class CFeeler;
class CModel;
} // namespace Nx
namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// One turn of a model's display about an axis (RotateDisplay): from `base` by `range` degrees over `period`
// milliseconds - linearly (power 0), eased in (power > 0: sin(pi/2 t) squared power - 1 times) or out (power < 0:
// 1 - sin(pi/2 - pi/2 t)) -, then off (or held at its end).
struct CModelSpin {
    // 0x00420590: its angle now (degrees; 0 when off; unrounded, as the original leaves it in ST0). Past its period:
    // switched off (0), or held at base + range. The time is the game clock's (slow motion counted), or the real one
    // (MODEL_SPIN_REAL_TIME).
    double Angle();

    float m_period;      // +0x00 milliseconds
    uint32_t m_start;    // +0x04 when it started (the game clock's time, or the real time)
    float m_base;        // +0x08 degrees
    float m_range;       // +0x0C degrees
    int m_power;         // +0x10 SinePower
    uint8_t m_flags;     // +0x14 MODEL_SPIN_*
    uint8_t pad15[3];
};
static_assert(sizeof(CModelSpin) == 0x18, "layout must match the original");

// CModelSpin::m_flags
constexpr uint8_t MODEL_SPIN_ON = 0x1;
constexpr uint8_t MODEL_SPIN_HOLD = 0x2;      // HoldOnLastAngle
constexpr uint8_t MODEL_SPIN_REAL_TIME = 0x4; // dontpause: the real time, not the game clock's

// A model's turns about one axis: up to six at once, added up.
struct CModelSpinAxis {
    // 0x004204F0: every turn off, inactive.
    void Clear();
    // 0x00420520: a new turn in the first free slot (none free: nothing), the axis active.
    void Add(float period, uint32_t start, float base, float range, int power, bool hold, bool realTime);
    // 0x004213E0: the angles of the turns still on, added up (degrees). Inactive once none is.
    float Angle();

    CModelSpin m_spins[6]; // +0x00
    bool m_active;         // +0x90
    uint8_t pad91[3];
};
static_assert(sizeof(CModelSpinAxis) == 0x94, "layout must match the original");

// The model component (type 0x286A8D26 "model", vtable 0x00646128, 0x238 bytes; made by 0x00421490): an object's
// renderable model (Nx::CModel, +0x208) - made from its structure (Obj_InitModel: a level object's sector, a clone of
// another object's model, a model file or an asset), placed every frame at its object's display matrix (turned by
// RotateDisplay's spins, flipped by EnableDisplayFlip, moved by the display offset) - and the script members that
// change it (visibility of its geoms, colours, lights, scaling, textures).
//
// Vtable: 0 the scalar deleting destructor (0x00420270), 1 Update, 2 InitFromStructure, 3 Finalize, 8 Hide, 9
// Teleport, 12 CallMemberFunction, 13 InitModelFromProfile, 14 SetModel; the rest the base's / shared.
class CModelComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x286A8D26;

    // 0x00421490 (registered with the composite object manager): a new one (Construct; 0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x00420BF0: the base's constructor, its vtable and type, a new model (0x0048BFA0), the spins off, no display
    // offset (w 1), scale 1, no colour values, the pivot (0, 0, 0, 1). Returns this.
    CModelComponent* Construct();
    // 0x00420000: its vtable back, the model deleted (0x0048C3C0), then the base's body.
    void DestroyBody();
    // 0x00420270 (vtable slot 0)
    CModelComponent* Destroy(unsigned int flags);
    // 0x004214F0 (vtable slot 2): UseModelLights (the model's lights made - their position the object's, their
    // count Max_Scene_Lights - or, without it, taken away), the skeleton given to the model, ShadowVolume, then
    // InitModel.
    void InitFromStructure(Script::CStruct* params);
    // 0x004204B0 (vtable slot 3): its object's skeleton, animation blender and suspend components.
    void Finalize();
    // 0x004219F0 (vtable slot 1): a level object (InitModel's refObjectName) follows its sector's model; otherwise an
    // active, shown model goes to the display matrix (with the skeleton and whether the blender's animation is
    // ready).
    void Update();
    // 0x00420070 (vtable slot 8)
    void Hide(bool hide);
    // 0x00420480 (vtable slot 9): Update now, then the model's lights' brightness.
    void Teleport();
    // 0x00421AE0 (vtable slot 12): its script members (a switch on the name); 2 when it's not one of them.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x00421230 (vtable slot 13): the model built from a profile (Gfx: 0x004897D0 / 0x0048A2F0 - the build script,
    // 0x91C1EC93 when 0), then as InitModel ends.
    void InitModelFromProfile(void* profile, bool useAssetManager, int texDictOffset, uint32_t buildScript);
    // 0x00421360 (vtable slot 14): another model; its lights' brightness and bounding sphere taken.
    void SetModel(Nx::CModel* model);

    // 0x00420D10: the model from the structure (see the class comment), then (with the object's
    // COMPOSITE_FLAG_HIDDEN?) Update and the lights, the bounding sphere into m_boundingSphere and the bone volumes.
    void InitModel(Script::CStruct* params);
    // 0x0041FFA0: the level sector `name`'s geometry cloned into the model (a level object).
    void InitFromSector(uint32_t name);
    // 0x00420350: the skeleton's bone volumes (shared by every model with that skeleton) given to the model.
    void InitBoneVolumes();
    // 0x004215F0: the display matrix: the object's, turned by the spins about X, Y and Z (about the pivot when only
    // one... see the source), flipped (EnableDisplayFlip), at the object's position plus the spins' offset and the
    // display offset.
    void GetDisplayMatrix(Mth::Matrix* out);
    // 0x00421960: a line against the model (the skeleton's bones): the hit, 0 without a skeleton or bone volumes.
    int TestCollision(Mth::Vector* start, uint32_t bone, uint32_t a, uint32_t b, uint32_t c);

    // 0x00420090: colour value `index` (1 to the count) - nothing without a model or outside.
    void SetColorValue(int index, float value);
    // 0x004200C0: the model's lights' brightness.
    void UpdateBrightness();
    // 0x004200E0: InitModelFromProfile(profile, true, 0, buildScript) through the vtable, when there's a model.
    void RebuildFromProfile(void* profile, uint32_t buildScript);
    // 0x00420100
    uint32_t GetRefObjectName() const { return m_refObjectName; }
    // 0x00420110: the model's texture `src` replaced by `dest` in `mesh` (All: in every mesh); false without a model.
    bool ReplaceTexture(uint32_t mesh, const char* src, const char* dest);
    // 0x004202A0: geom `name` hidden or shown (sent to the server first when `send` and its player is this machine's).
    // True.
    bool HideGeom(uint32_t name, bool hide, bool send);
    // 0x00420670: the brightness of what a feeler hit, doubled, into m_brightness and the lights (lerped by `rate`
    // when asked); without the model's lights, the global brightness.
    void ApplyBrightness(Nx::CFeeler* feeler, bool lerp, float rate);
    // 0x00420700: the colour of geom 0 (InitFromSector's model only).
    void SetColor(uint32_t color);
    // 0x00420730: turns of `duration` frames about the axes asked (each from `start` to `end` degrees), with the pivot
    // (0, 30, 0, 1); sent to the server (message 0x76) when its player is this machine's.
    void RotateDisplay(float duration, float start, float end, int power, bool hold, bool x, bool y, bool z);
    // 0x004208D0: every turn off; sent (message 0x8C).
    void CancelRotateDisplay();

    Mth::Vector m_boundingSphere; // +0x18 the model's (0x00492F30)
    Mth::Vector m_pivot;          // +0x28 RotationOffset
    CModelSpinAxis m_spin[3];     // +0x38 about X, Y, Z
    bool m_flip;                  // +0x1F4 EnableDisplayFlip
    uint8_t pad1F5[3];
    Mth::Vector m_offset;         // +0x1F8 added to the display position (w 1)
    Nx::CModel* mp_model;         // +0x208
    CBaseComponent* mp_skeleton;  // +0x20C its object's skeleton component (Finalize)
    CBaseComponent* mp_animBlender; // +0x210
    CBaseComponent* mp_suspend;   // +0x214
    float m_colorValues[4];       // +0x218 (1 to m_colorValueCount)
    int m_colorValueCount;        // +0x228
    float m_brightness;           // +0x22C
    uint32_t m_refObjectName;     // +0x230 refObjectName: a level object's sector
    bool m_levelObject;           // +0x234
    bool m_sectorModel;           // +0x235 InitFromSector's
    uint8_t pad236[2];
};
static_assert(offsetof(CModelComponent, m_boundingSphere) == 0x18 && offsetof(CModelComponent, m_pivot) == 0x28 &&
                  offsetof(CModelComponent, m_spin) == 0x38 && offsetof(CModelComponent, m_flip) == 0x1F4 &&
                  offsetof(CModelComponent, m_offset) == 0x1F8 && offsetof(CModelComponent, mp_model) == 0x208 &&
                  offsetof(CModelComponent, m_colorValues) == 0x218 &&
                  offsetof(CModelComponent, m_colorValueCount) == 0x228 &&
                  offsetof(CModelComponent, m_refObjectName) == 0x230 && sizeof(CModelComponent) == 0x238,
              "layout must match the original (0x00421490 allocates 0x238 bytes)");

} // namespace Obj

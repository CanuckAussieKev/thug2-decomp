#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The shadow component (type 0x8A897DD2, "shadow"; vtable 0x00646498, 0x48 bytes; made by 0x00431800): the shadow
// under its object - one of the renderer's shadow objects (not rewritten yet), chosen by ShadowType: detailed (type 1,
// 0x004A19C0: from the model, cast along (0, -1, 0) over 72), simple (type 2, 0x004A1750: a shadow model -
// ShadowModel=, "Ped_Shadow" by default - at the ground, scaled by ShadowScale=) or Volume (type 3, 0x004A1F80), or
// none. Each update it follows the object (unless the structure had the flag 0x7E773A11) and is shown while the
// model is visible.
//
// The shadow object's vtable as used here: 0 the scalar deleting destructor; 1 (+0x04) Update(position, matrix,
// ground normal by value); 2 (+0x08) SetPosition(by value); 3 (+0x0C) SetLightDirection(direction); 4 (+0x10)
// SetMatrix(matrix); 5 (+0x14) Hide; 6 (+0x18) Show; 7 (+0x1C) SetModel(model, asset data). Its +0x04 is its type,
// +0x0C the simple shadow's scale (the model's +0x1C at first), +0x10 its offset.
//
// The component's vtable: 0 the scalar deleting destructor; 1 Update; 2 InitFromStructure; 3 Finalize; 4 the empty
// 0x005480A0; 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 Hide; 9 0x004A1090 (Teleport, shared);
// 10 the empty 0x005BAB90; 11 the base's member function table entry 0x0045D720; 12 CallMemberFunction.
class CShadowComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x8A897DD2;

    // 0x00431800 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x00431380: the base's constructor, its vtable and type; no shadow, at the origin (w 1) with the ground normal
    // (0, 1, 0, 1), type none, following its object. Returns this.
    CShadowComponent* Construct();
    // 0x00431300: the shadow deleted; the base's body.
    void DestroyBody();
    // 0x004313D0 (vtable slot 0)
    CShadowComponent* Destroy(unsigned int flags);
    // 0x00431860 (vtable slot 2): ShadowType (simple by default) and the 0x7E773A11 flag (don't follow the object);
    // the old shadow removed and the new one made (see the class). For a simple shadow, its scale (ShadowScale=, 1)
    // and a first Update.
    void InitFromStructure(Script::CStruct* params);
    // 0x00431400 (vtable slot 3): the model component found and kept; the shadow (if any) given the model and its
    // asset data (the asset manager's, by the model's checksum +0x30).
    void Finalize();
    // 0x004319D0 (vtable slot 1): following the object: the shadow's position = the object's, and the shadow given
    // the display matrix at that position - or, when the model has a hierarchy (0x00493040), the root's matrix times
    // the display matrix at the object's position plus the root's translation; then RefreshShadow.
    void Update();
    // 0x00431570 (vtable slot 8): the shadow hidden or shown.
    void Hide(bool hide);
    // 0x00431B10 (vtable slot 12): Obj_ShadowOff (the shadow removed) and Obj_ShadowOn (InitFromStructure with
    // ShadowType simple when not given; a simple shadow's offset= (0.2) set). 1 for those, 2 for other names.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x00431450: with a shadow, a model component and a model: while the model is visible (its flags +0x44 have
    // 0x2 and not 0x4) the shadow updated for its type (detailed: placed 36 above the object, along its up; simple:
    // its scale = the model's +0x1C, then as Volume: Update with the position, the object's matrix and the normal)
    // and shown; otherwise hidden.
    void RefreshShadow();
    // 0x004315A0: a shadow of `type` (1-3; see the class) made unless there is one; types 2 and 3 updated at once.
    void CreateShadow(int type, const char* model);
    // 0x00431780: the shadow (if any) deleted - the model told it has none first (0x004949B0(false)).
    void DestroyShadow();
    // 0x004317C0 / 0x004317E0: the shadow's position / the ground normal under it.
    void SetPosition(const Mth::Vector* position);
    void SetNormal(const Mth::Vector* normal);
    // 0x00431360 (not a Ghidra function): the light's direction handed to the shadow (if any).
    void SetLightDirection(const Mth::Vector* direction);

    void* mp_shadow;              // +0x18 the renderer's shadow object
    Mth::Vector m_pos;            // +0x1C where it is
    Mth::Vector m_normal;         // +0x2C the ground's normal under it
    uint32_t m_shadowType;        // +0x3C ShadowType (none 0x806FFF30, simple, detailed, Volume)
    CBaseComponent* mp_model;     // +0x40 the model component (Finalize)
    bool m_followObject;          // +0x44 each update puts it at the object's position
    uint8_t pad45[3];
};
static_assert(offsetof(CShadowComponent, mp_shadow) == 0x18 && offsetof(CShadowComponent, m_pos) == 0x1C &&
                  offsetof(CShadowComponent, m_normal) == 0x2C && offsetof(CShadowComponent, m_shadowType) == 0x3C &&
                  offsetof(CShadowComponent, mp_model) == 0x40 && offsetof(CShadowComponent, m_followObject) == 0x44 &&
                  sizeof(CShadowComponent) == 0x48,
              "layout must match the original");

} // namespace Obj

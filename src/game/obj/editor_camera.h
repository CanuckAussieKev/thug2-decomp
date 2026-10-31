#pragma once

#include <cstddef>
#include <cstdint>

#include "game/maths/vec4.h"
#include "game/obj/entity.h"

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

namespace GameObjects {

// The editor camera component (type 0xAC8946E2, vtable 0x0064B2D0, 0x9C bytes; made by 0x004E5940): the
// create-a-goal / park editor camera. A cursor point is walked over the ground with the left stick (kept on it
// with collision feelers, never up walls or onto objects whose node scripts are flagged), the camera orbits it
// at a heading and tilt from the right stick, zooms in and out, raises and lowers its height above the ground,
// and the cursor has a shadow ("Ped_Shadow") where the ground is under it.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 ConfigureFromData, 3 Finalize, 8 Hide (the shadow),
// 12 InvokeMethodByName; the rest the base's / shared.
class CEditorViewModule : public CObjectModule {
public:
    // 0x004E5940 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CObjectModule* __cdecl Fabricate();
    // 0x004E5060: the base's constructor; the points (0, 0, 0, 1), everything else 0. Returns this.
    CEditorViewModule* MakeNew();
    // 0x004E5100: the shadow deleted, then the base's destructor body.
    void DestroyBody();
    // 0x004E59A0 (slot 0)
    CEditorViewModule* Teardown(unsigned int flags);
    // 0x004E4ED0 (slot 2): min_height (0), the zoom range 0x52EECB98 / 0x53E2512C (100 / 2000), the flags
    // SimpleCollision and 0xF5680DEF.
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x004E51F0 (slot 3): the object's Input component.
    void Seal();
    // 0x004E59D0 (slot 8): hidden - no shadow; shown - the shadow made.
    void Conceal(bool hide);
    // 0x004E5210 (slot 12): 0x69CB3AE1 puts the cursor at Position= (asserted) and updates; 0xF362BBBF puts it
    // there quietly (zoom 0xC48391A5 700, tilt 0.6, height 0xB8D629CA 0). 2 for anything else.
    int InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);
    // 0x004E5C20 (slot 1): one frame (see the class).
    void TickFrame();

    // 0x004E5170: the shadow made if there's none (a 0x14-byte 0x004A1750 object, scale 1.2, "Ped_Shadow").
    void BuildBlobShadow();
    // 0x004E5A10: the shadow placed on the ground under the cursor (or deleted with no ground).
    void ResyncGroundShadow();
    // 0x004E5460: the ground's height between `start` and `end` into `height`: false with no ground, when it's
    // too steep (normal y below 0.087) or (without SimpleCollision) its object's node script is flagged, or the
    // way to it from `start` is blocked by one.
    bool GroundHeight(Maths::Vec4 start, Maths::Vec4 end, float* height);
    // 0x004E5640: the cursor (moved from `previous`) put on the ground within `step` up of it; false (and the
    // caller puts it back) where it can't go.
    bool PlaceCursor(Maths::Vec4 previous, float step);

    uint32_t unknown18;
    Maths::Vec4 m_focus;        // +0x1C what the camera looks at (the cursor, the height eased)
    Maths::Vec4 m_viewer;       // +0x2C the camera's position
    Maths::Vec4 m_cursor;       // +0x3C on the ground
    Maths::Vec4 m_rightSide;        // +0x4C the heading's axes (flat)
    Maths::Vec4 m_forward;      // +0x5C
    float m_zoom;               // +0x6C the camera's distance
    float m_minZoom, m_maxZoom; // +0x70
    float m_heading;            // +0x78 0..2 pi
    float m_leanAngle;               // +0x7C
    float m_minTilt, m_maxTilt; // +0x80 (from the script globals each frame)
    float m_levelY;             // +0x88 above the cursor
    float m_minHeight;          // +0x8C
    bool m_simpleCollision;     // +0x90
    bool m_noWallCheck;         // +0x91 (0xF5680DEF)
    uint8_t pad92[2];
    uint8_t* m_inputPtr;          // +0x94 the Input component
    void* m_shadowHandle;            // +0x98
};
static_assert(offsetof(CEditorViewModule, m_focus) == 0x1C, "m_focus");
static_assert(offsetof(CEditorViewModule, m_cursor) == 0x3C, "m_cursor");
static_assert(offsetof(CEditorViewModule, m_zoom) == 0x6C, "m_zoom");
static_assert(offsetof(CEditorViewModule, m_levelY) == 0x88, "m_height");
static_assert(offsetof(CEditorViewModule, m_shadowHandle) == 0x98, "mp_shadow");
static_assert(sizeof(CEditorViewModule) == 0x9C, "size");

// 0x004E4F50: whether the script `name` (or one it calls, by name - with a guard against loops in the list at
// 0x007CCBE8) is one of the four flagged scripts at 0x0067EFD8; 0x0067EFE8's script and those already seen give
// false.
bool __cdecl ScriptFlagged(uint32_t name);
// 0x004E5320: a feeler's hit callback: an object whose node's TriggerScript is flagged sets 0x007CCC38.
void __cdecl FlagScriptedHit(void* feeler);
// 0x004E5390 (`end` in ESI, `start` in EDI): whether a line from `start` to `end` hits a flagged object (faces
// with 0x50 only).
bool FlaggedObjectBetween(const Maths::Vec4& start, const Maths::Vec4& end);

} // namespace Obj

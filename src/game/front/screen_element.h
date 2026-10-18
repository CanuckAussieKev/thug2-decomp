#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"

namespace Obj {
struct CEvent;
}
namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The front end's screen elements: menus, text, sprites, windows... - a tree of objects (the screen element
// manager 0x00701440 holds the root) laid out on a 640x480 layout each frame.
namespace Front {

class CScreenElementManager;

// One of an element's three states (0x44 bytes): the one shown (+0x7C, what a morph starts from), the one it is
// morphing to (+0xC0) and the summed one the layout pass leaves (+0x114: its parents' included).
struct SElementState {
    float alpha;                     // +0x00
    Mth::Vector pos3d;               // +0x04 (pos3D; w 1)
    float x, y;                      // +0x14 the position (its justification point) on the 640x480 layout
    float z;                         // +0x1C (pos3D: the projected depth)
    float left, top;                 // +0x20 its top left (x, y moved by the justification)
    float scaleX, scaleY;            // +0x28
    float baseScaleX, baseScaleY;    // +0x30 (a relative scale is a factor of these)
    float angle;                     // +0x38 radians
    uint8_t rgba[4];                 // +0x3C
    uint32_t unknown40;              // +0x40

    // 0x004A8730: a copy (returns this).
    SElementState* Assign(const SElementState* other);
};
static_assert(sizeof(SElementState) == 0x44, "layout must match the original");

class CScreenElement : public Obj::CObject {
public:
    // m_objectFlags (+0x08) - the element's own bits, beside CObject's.
    enum : uint32_t {
        kMorphCurve = 0x300,          // Anim: 0x100 0xF5B95CE4 (ease out), 0x200 fast_in, 0x300 gentle
        kDirty = 0x400,               // changed since the layout pass looked
        kChildrenDirty = 0x800,       // (the layout pass: the parent changed, so must it)
        kVisibleNow = 0x1000,         // (UnHide; z_priority set) - cleared by the layout pass
        kDimsLocked = 0x2000,         // SetDims' flag
        kFocusSent = 0x4000,          // the element's 0x3056434F event went (cleared: 0x4A57316B)
        kZPriority = 0x8000,          // z_priority given
        kNew = 0x10000,               // (set by the constructor)
        kPauseAsked = 0x20000,        // paused (the layout pass: kPaused from the next frame)
        kPaused = 0x40000,            // its morph doesn't advance
        kPos3d = 0x80000,             // placed by pos3D (projected each frame)
        kOffScreen = 0x100000,        // pos3D behind the camera
        kBlockEvents = 0x200000,      // block_events
        kHidden = 0x400000,           // Hide
        kKeepScale = 0x800000,        // (0x1CC71DFF: pos3D doesn't scale it with distance)
        kFlag1000000 = 0x1000000,     // (0x1F2C6E1D)
        kFlipH = 0x2000000,           // flip_h
        kFlipV = 0x4000000,           // flip_v
        kPriority = 0x10000000,       // Priority > 0 (pad events with priorities)
    };

    // 0x004A8F90: the constructor (returns this): vtable 0x0064A090, no parent / children / siblings, the three
    // states alike (alpha 1, scale 1, rgba 0x80808080, at 0, 0), dirty and new.
    CScreenElement* Construct();
    // 0x004AAB30: the destructor body: out of the tree (SetParent(null)), id 0xDEADBEEF, the links let go, then
    // CObject's.
    void DestroyBody();
    // 0x004AAC90 (vtable slot 0): the scalar deleting destructor.
    CScreenElement* Destroy(uint8_t flags);

    // --- the vtable's slots (0x0064A090) ---
    // 0x004A96B0 (slot 1): Pos / pos3D, dims, parent, just, rgba, rot_angle, the flags, z_priority, (not_)focusable,
    // local_id, unpause, (un)block_events, Hide / UnHide, Priority; then CObject's.
    void SetProperties(Script::CStruct* params);
    // 0x004A89B0 (slot 3): SetProps (slot 1), DoMorph (the script waits `time` seconds; slot 9), Die (the manager
    // destroys it, its parent's focus event resent around it), GetProps (slot 8, into the script's parameters);
    // others: CObject's.
    bool CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004AA710 (slot 8): alpha, rgba (an array), scale (one or a pair), Pos, rot_angle (degrees),
    // events_blocked - added to `out`.
    void GetProperties(Script::CStruct* out);
    // 0x004A9AA0 (slot 9): Pos (relative / proportional), restore_alpha / alpha (remember_alpha), scale (one or a
    // pair, relative_scale), rot_angle, then a morph over `time` seconds with Anim's curve, rgba.
    void SetMorph(Script::CStruct* params);
    // (slot 10 0x005480A0: Update - empty here)

    // --- placement ---
    // 0x004A8610 / 0x004A87C0 / 0x004A87E0: the justification, the position morphed to, the top left morphed to.
    void GetJust(float* x, float* y) const;
    void GetPos(float* x, float* y) const;
    void GetTopLeft(float* x, float* y) const;
    // 0x004A8C30: s->left / top from s's position and scale, the justification and the size.
    void PlaceTopLeft(SElementState* s);
    // 0x004A9100: the position morphed to (with `now` also the one shown).
    void SetPos(float x, float y, int now);
    // 0x004A91F0: pos3D (with `now` also the one shown); kPos3d.
    void SetPos3d(const Mth::Vector* pos, int now);
    // 0x004A92D0: the size; kDimsLocked if `lock`.
    void SetDims(float w, float h, int lock);
    // 0x004A9390: the justification; the position moves so the element stays put.
    void SetJust(float x, float y);
    // 0x004A94A0: the scale morphed to (`relative`: times the base scale; else it becomes the base) and with
    // `now` the one shown.
    void SetScale(float x, float y, bool relative, int now);
    // 0x004A8630 / 0x004A8800 / 0x004A88C0: alpha, rotation (degrees, plus a constant) and rgba morphed to (with
    // `now` also the ones shown).
    void SetAlpha(float alpha, int now);
    void SetRotation(float degrees, int now);
    void SetRGBA(uint32_t rgba, int now);
    // 0x004A8870: a morph lasting `ms` from now (0: at once).
    void StartMorph(int ms);
    // 0x004A9D30: the layout pass over this element and its children (each frame, from the root).
    void UpdateLayout();
    // 0x004AA390 / 0x004AA470: z_priority / Priority for it and every descendant (z: +1 per generation; a z
    // given by the element itself wins).
    void SetZPriority(float z);
    void SetChildrenPriority(int priority);

    // --- the tree ---
    // 0x004A8B80: its parent (a smart pointer into `out`; returns out).
    Obj::CSmtPtr<CScreenElement>* GetParent(Obj::CSmtPtr<CScreenElement>* out) const;
    // 0x004A8BB0: the nearest window element (type WindowElement) above it, or null.
    CScreenElement* GetWindow() const;
    // 0x004AA8E0: out of its old parent's children, last among `parent`'s (none: out of the tree), the parent's
    // priority and z_priority + 1 taken over (no parent: z_priority 0). The second argument isn't used.
    void SetParent(const Obj::CSmtPtr<CScreenElement>* parent, int unused);
    // 0x004A95E0: its last child (null without children).
    CScreenElement* GetLastChild() const;
    // 0x004A9600: how many children.
    int CountChildren() const;
    // 0x004AA2A0: the child with the id `id` (0: the `*index`th; the index found goes to `index` when given) -
    // a smart pointer into `out`.
    Obj::CSmtPtr<CScreenElement>* GetChild(Obj::CSmtPtr<CScreenElement>* out, uint32_t id, int* index);
    // 0x004AA690 / 0x004AA6D0: the same as a plain pointer, by id / by index.
    CScreenElement* GetChildById(uint32_t id, int* index);
    CScreenElement* GetChildByIndex(int index);

    // --- state queries ---
    // 0x004A8F00 / 0x004A8F20 / 0x004A8F40: block_events / priority / hidden, on it or any parent.
    bool EventsBlocked() const;
    bool HasPriority() const;
    bool IsHidden() const;
    // 0x004A86C0: Priority.
    int GetPriority() const;
    // 0x004A86B0: whether the manager has pad-event priorities.
    bool ManagerUsesPriorities() const;
    // 0x004A8680: paused (kPauseAsked) or not (both pause bits cleared).
    void SetPaused(bool pause);
    // 0x004A8940: kFocusSent set (1) or cleared (else), with its event, when it changes.
    void SetFocusSent(int sent);
    // 0x004A87A0 / 0x004A87B0: the element's width / height on the 640x480 layout (+0x160 times +0x144, +0x164
    // times +0x148; returned unrounded in ST0, as the original's x87 product - hence double).
    double GetLayoutWidth() const;
    double GetLayoutHeight() const;

    // --- parameter helpers ---
    // 0x004A8C80: a position from Pos (a pair: relative adds the position morphed to, proportional is a fraction
    // of the parent's size; or a vector's x, y). False without one.
    bool ResolvePos(Script::CStruct* params, float* x, float* y);
    // 0x004A8DC0: a justification from the array `name` (two names: left/top -1, right/bottom 1, else 0; or two
    // floats). False without the array.
    bool ResolveJust(Script::CStruct* params, uint32_t name, float* x, float* y);
    // 0x004A8E90: an rgba from the array `name` (bytes, lowest first). False without the array.
    bool ResolveRGBA(Script::CStruct* params, uint32_t name, uint32_t* rgba);

    // --- fields (after CObject's) ---
    Obj::CSmtPtr<CScreenElement> mp_parent;       // +0x4C
    Obj::CSmtPtr<CScreenElement> mp_firstChild;   // +0x58
    Obj::CSmtPtr<CScreenElement> mp_next;         // +0x64 the next sibling
    Obj::CSmtPtr<CScreenElement> mp_prev;         // +0x70 the previous sibling
    SElementState m_shown;                        // +0x7C
    SElementState m_target;                       // +0xC0
    int m_morphStart;                             // +0x104 (ms)
    int m_morphLength;                            // +0x108 (ms; 0 none)
    float m_morphCurveDone;                       // +0x10C the curve's value last frame
    float m_morphDone;                            // +0x110 the time's fraction last frame
    SElementState m_summed;                       // +0x114
    float m_justX, m_justY;                       // +0x158 (-1 left / top .. 1 right / bottom)
    float m_width, m_height;                      // +0x160 the size (unscaled)
    uint8_t m_rgba168[4];                         // +0x168 (0x80 each)
    float m_zPriority;                            // +0x16C
    float m_rememberedAlpha;                      // +0x170 (remember_alpha / restore_alpha; 1)
    int m_priority;                               // +0x174
};

using ElementPtr = Obj::CSmtPtr<CScreenElement>;

// The element with this id (the screen element manager's lookup, 0x004AAE20), into `out` (which must be empty).
void GetElement(ElementPtr* out, uint32_t id);

} // namespace Front

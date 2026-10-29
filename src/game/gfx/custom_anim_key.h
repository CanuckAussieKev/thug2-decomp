#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/quat.h"
#include "game/mth/vector.h"

namespace Script {
class CStruct;
}

// The custom keys of an animation's frame data (the .ska's key list, made by CreateCustomAnimKey while the frame data
// loads, and a frame data's script keys): each fires once when the animation's time passes its frame - the camera
// moved or its angle changed (cutscene cameras), a script run on the object, a global structure's script run, an
// object killed. From the disassembly of 0x004860C0-0x00486875.
namespace Gfx {

// The base (vtable 0x006487B0: [0] scalar deleting destructor 0x004819F0, [1] WithinRange, [2] Process - pure).
class CCustomAnimKey {
public:
    // 0x004860C0 (vtable slot 1): whether the key's frame lies between `start` and `end` (either way round: `start`
    // after `end` for an animation playing backwards) - `start` always counts, `end` only with `inclusive`.
    bool WithinRange(float start, float end, bool inclusive);
    // 0x00486150
    void SetActive(bool active);
    // 0x00486160: when active: no longer, and processed (vtable slot 2 - a tail jump). False when it wasn't active.
    bool ProcessIfActive(void* object);
    // 0x00486460 (vtable slot 0 of every kind below but the script key): the base's destructor body (0x004819E0), and
    // freed with flags & 1. Returns this.
    CCustomAnimKey* Destroy(unsigned int flags);

    void** m_vtable;
    int m_frame;   // +0x04
    bool m_armed; // +0x08: fires once (ProcessIfActive disarms it; the camera key's Process arms it again)
    uint8_t pad9[3];
};
static_assert(sizeof(CCustomAnimKey) == 0xC, "layout must match the original");

// The camera's angle (kind 1; 0x10 bytes, vtable 0x0064880C).
class CCameraFovKey : public CCustomAnimKey {
public:
    // 0x004868A0 (vtable slot 2): the camera's member function 0x44EA2B6D (the angle in radians) run with the angle as
    // its unnamed float. True.
    bool Process(void* camera);

    float m_fov; // +0x0C
};

// A script run on the object (kind 4; 0x10 bytes, vtable 0x00648818).
class CScriptKey : public CCustomAnimKey {
public:
    // 0x00486180 (vtable slot 2): two scripts are swapped for others first (0x4E917BDA -> 0x69F35FA0, 0x208D7A0D ->
    // 0x04D887B1 - kept). True.
    bool Process(void* object);

    uint32_t m_script; // +0x0C
};

// A global structure given to the script 0xBDF7F843 (kind 5; 0x10 bytes, vtable 0x00648830).
class CStructureScriptKey : public CCustomAnimKey {
public:
    // 0x00486300 (vtable slot 2): nothing when there's no such structure. True.
    bool Process(void* object);

    uint32_t m_structure; // +0x0C
};

// An object killed: the one named (name=) in a global structure (kind 6; 0x10 bytes, vtable 0x0064883C).
class CKillObjectKey : public CCustomAnimKey {
public:
    // 0x00486340 (vtable slot 2): the object's MarkAsDead (vtable slot 6; not checked for null). True.
    bool Process(void* object);

    uint32_t m_structure; // +0x0C
};

// The camera moved (kinds 2 and 7; 0x30 bytes, vtable 0x00648848).
class CCameraKey : public CCustomAnimKey {
public:
    // 0x00486490: at `frame`, turned by `rotation` times a quarter turn about x, at `pos` with y and z swapped (z
    // negated); `cut` (kind 7). Returns this.
    CCameraKey* Construct(int frame, const Mth::Quat* rotation, const Mth::Vector* pos, bool cut);
    // 0x004861D0 (vtable slot 1): as the base's, but a frame either side of its frame counts (without `cut`: up to
    // one before `end`; with it, not before `end`).
    bool WithinRange(float start, float end, bool inclusive);
    // 0x00486390 (vtable slot 2): the camera put there (none at frame 0: false) - and left active, so it's put there
    // again each time. True.
    bool Process(void* camera);

    Mth::Quat m_rotation; // +0x0C
    Mth::Vector m_pos;    // +0x1C (w 1)
    bool m_cut;           // +0x2C
    uint8_t pad2D[3];
};
static_assert(offsetof(CCameraKey, m_pos) == 0x1C && sizeof(CCameraKey) == 0x30, "layout must match the original");

// A frame data's script key (0x14 bytes, made in an array - vtable 0x006487BC: [0] 0x00481DB0, [1] WithinRange, [2]
// Process).
class CAnimScriptKey : public CCustomAnimKey {
public:
    // 0x004865D0: at `frame`, sending `event` with a copy of `params` (a new structure: the old one isn't freed).
    void Set(int frame, uint32_t event, Script::CStruct* params);
    // 0x00486560: the destructor's body - the structure freed; then the base's vtable.
    void DestroyBody();
    // 0x00486440 (vtable slot 2): the event sent by the object to itself. True.
    bool Process(void* object);

    uint32_t m_type;           // +0x0C
    Script::CStruct* mp_params; // +0x10
};
static_assert(sizeof(CAnimScriptKey) == 0x14, "layout must match the original");

// 0x00486680 (cdecl): the next key from a frame data's key list (`*cursor` moved past it): {frame, kind, size} then
// the kind's data. Unknown kinds are skipped (by size); null for them and when memory runs out.
CCustomAnimKey* __cdecl CreateCustomAnimKey(const uint8_t** cursor);

} // namespace Gfx

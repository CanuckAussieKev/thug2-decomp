#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"

namespace script {
class Struct;
}

// The custom keys of an animation's frame data (the .ska's key list, made by create_custom_anim_key while the frame data
// loads, and a frame data's script keys): each fires once when the animation's time passes its frame - the camera
// moved or its angle changed (cutscene cameras), a script run on the object, a global structure's script run, an
// object killed. From the disassembly of 0x004860C0-0x00486875.
namespace gfx {

// The base (vtable 0x006487B0: [0] scalar deleting destructor 0x004819F0, [1] inside_range, [2] Process - pure).
class UserClipKey {
public:
    // 0x004860C0 (vtable slot 1): whether the key's frame lies between `start` and `end` (either way round: `start`
    // after `end` for an animation playing backwards) - `start` always counts, `end` only with `inclusive`.
    bool inside_range(float start, float end, bool inclusive);
    // 0x00486150
    void change_live(bool active);
    // 0x00486160: when active: no longer, and processed (vtable slot 2 - a tail jump). False when it wasn't active.
    bool process_if_active(void* object);
    // 0x00486460 (vtable slot 0 of every kind below but the script key): the base's destructor body (0x004819E0), and
    // freed with flags & 1. Returns this.
    UserClipKey* teardown(unsigned int flags);

    void** vtable_;
    int tick_;   // +0x04
    bool armed_; // +0x08: fires once (process_if_active disarms it; the camera key's Process arms it again)
    uint8_t pad9[3];
};
static_assert(sizeof(UserClipKey) == 0xC, "layout must match the original");

// The camera's angle (kind 1; 0x10 bytes, vtable 0x0064880C).
class CameraFovKey : public UserClipKey {
public:
    // 0x004868A0 (vtable slot 2): the camera's member function 0x44EA2B6D (the angle in radians) run with the angle as
    // its unnamed float. True.
    bool work_on(void* camera);

    float field_of_view_; // +0x0C
};

// A script run on the object (kind 4; 0x10 bytes, vtable 0x00648818).
class ScriptKey : public UserClipKey {
public:
    // 0x00486180 (vtable slot 2): two scripts are swapped for others first (0x4E917BDA -> 0x69F35FA0, 0x208D7A0D ->
    // 0x04D887B1 - kept). True.
    bool work_on(void* object);

    uint32_t own_script_; // +0x0C
};

// A global structure given to the script 0xBDF7F843 (kind 5; 0x10 bytes, vtable 0x00648830).
class StructureScriptKey : public UserClipKey {
public:
    // 0x00486300 (vtable slot 2): nothing when there's no such structure. True.
    bool work_on(void* object);

    uint32_t structure_; // +0x0C
};

// An object killed: the one named (name=) in a global structure (kind 6; 0x10 bytes, vtable 0x0064883C).
class KillObjectKey : public UserClipKey {
public:
    // 0x00486340 (vtable slot 2): the object's flag_as_dead (vtable slot 6; not checked for null). True.
    bool work_on(void* object);

    uint32_t structure_; // +0x0C
};

// The camera moved (kinds 2 and 7; 0x30 bytes, vtable 0x00648848).
class CameraKey : public UserClipKey {
public:
    // 0x00486490: at `frame`, turned by `rotation` times a quarter turn about x, at `pos` with y and z swapped (z
    // negated); `cut` (kind 7). Returns this.
    CameraKey* make_new(int frame, const maths::QuatRotation* rotation, const maths::Vec4* pos, bool cut);
    // 0x004861D0 (vtable slot 1): as the base's, but a frame either side of its frame counts (without `cut`: up to
    // one before `end`; with it, not before `end`).
    bool inside_range(float start, float end, bool inclusive);
    // 0x00486390 (vtable slot 2): the camera put there (none at frame 0: false) - and left active, so it's put there
    // again each time. True.
    bool work_on(void* camera);

    maths::QuatRotation orientation_turn_; // +0x0C
    maths::Vec4 at_point_;                 // +0x1C (w 1)
    bool cut_;                             // +0x2C
    uint8_t pad2d_[3];
};
static_assert(offsetof(CameraKey, at_point_) == 0x1C && sizeof(CameraKey) == 0x30, "layout must match the original");

// A frame data's script key (0x14 bytes, made in an array - vtable 0x006487BC: [0] 0x00481DB0, [1] inside_range, [2]
// Process).
class AnimScriptKey : public UserClipKey {
public:
    // 0x004865D0: at `frame`, sending `event` with a copy of `params` (a new structure: the old one isn't freed).
    void assign_value(int frame, uint32_t event, script::Struct* params);
    // 0x00486560: the destructor's body - the structure freed; then the base's vtable.
    void destroy_body();
    // 0x00486440 (vtable slot 2): the event sent by the object to itself. True.
    bool work_on(void* object);

    uint32_t kind_tag_;         // +0x0C
    script::Struct* call_args_; // +0x10
};
static_assert(sizeof(AnimScriptKey) == 0x14, "layout must match the original");

// 0x00486680 (cdecl): the next key from a frame data's key list (`*cursor` moved past it): {frame, kind, size} then
// the kind's data. Unknown kinds are skipped (by size); null for them and when memory runs out.
UserClipKey* __cdecl create_custom_anim_key(const uint8_t** cursor);

} // namespace gfx

#pragma once

#include <cstddef>
#include <cstdint>

#include "base/maths/quat_rotation.h"
#include "base/maths/vec4.h"
#include "formats/byte_stream.h"
#include "base/core/checksums.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Struct;
}

// The custom keys of an animation's frame data (the .ska's key list, made by create_custom_anim_key while the frame data
// loads, and a frame data's script keys): each fires once when the animation's time passes its frame - the camera
// moved or its angle changed (cutscene cameras), a script run on the object, a global structure's script run, an
// object killed. From the disassembly of 0x004860C0-0x00486875.
namespace gfx {

// The base (vtable 0x006487B0). From the memory manager's current heap (not zeroed, as the original).
class UserClipKey {
public:
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* memory);
    static void* operator new[](std::size_t size) noexcept;
    static void operator delete[](void* memory);

    // At `frame`, inactive.
    explicit UserClipKey(int frame) : tick_(frame) {}
    // 0x004819E0 / 0x004819F0 (the custom keys' 0x00486460 too): nothing to do.
    virtual ~UserClipKey() = default;
    UserClipKey(const UserClipKey&) = delete;
    UserClipKey& operator=(const UserClipKey&) = delete;

    // 0x004860C0 (slot 1): whether the key's frame lies between `start` and `end` (either way round: `start` after
    // `end` for an animation playing backwards) - `start` always counts, `end` only with `inclusive`.
    virtual bool inside_range(float start, float end, bool inclusive);
    // Slot 2 (pure in the base): what the key does, to `object` (an object, or a camera). True when it did it.
    virtual bool work_on(void* object) = 0;

    // 0x00486150
    void change_live(bool active);
    // 0x00486160: when active: no longer, and processed (work_on - a tail jump). False when it wasn't active.
    bool process_if_active(void* object);

    // The fields other code reads and sets.
    [[nodiscard]] int tick() const { return tick_; }
    void set_tick(int value) { tick_ = value; }
    void set_armed(bool value) { armed_ = value; }

private:
    int tick_{};   // +0x04
    bool armed_{}; // +0x08: fires once (process_if_active disarms it; the camera key's work_on arms it again)
    std::array<uint8_t, 3> pad9{};

protected:
    // The animation events' constructor (0x00481A20): the original leaves the frame and the flag as they were (zero
    // here); assign_value sets them.
    UserClipKey() = default;
};
constexpr std::size_t kUserClipKeyBytes = 0xC;

static_assert(sizeof(UserClipKey) == kUserClipKeyBytes, "the original's size");

// The camera's angle (kind 1; 0x10 bytes, vtable 0x0064880C).
class CameraFovKey final : public UserClipKey {
public:
    // A field of view and the frame it is set at.
    struct FovAt {
        int frame;
        float degrees;
    };
    explicit CameraFovKey(FovAt key) : UserClipKey(key.frame), field_of_view_(key.degrees) {}
    // 0x004868A0: the camera's member function 0x44EA2B6D (the angle in radians) run with the angle as its unnamed
    // float. True.
    bool work_on(void* camera) override;

private:
    float field_of_view_; // +0x0C
};

// A marker (kind 3; 0xC bytes): does nothing (its work_on is the shared "true" 0x005BC7F0).
class MarkerKey final : public UserClipKey {
public:
    explicit MarkerKey(int frame) : UserClipKey(frame) {}
    bool work_on(void* /*object*/) override { return true; }
};

// A script run on the object (kind 4; 0x10 bytes, vtable 0x00648818).
class ScriptKey final : public UserClipKey {
public:
    ScriptKey(int frame, checksums::Name script) : UserClipKey(frame), own_script_(script.value()) {}
    // 0x00486180: two scripts are swapped for others first (0x4E917BDA -> 0x69F35FA0, 0x208D7A0D -> 0x04D887B1 -
    // kept). True.
    bool work_on(void* object) override;

private:
    uint32_t own_script_; // +0x0C
};

// A global structure given to the script 0xBDF7F843 (kind 5; 0x10 bytes, vtable 0x00648830).
class StructureScriptKey final : public UserClipKey {
public:
    StructureScriptKey(int frame, checksums::Name structure) : UserClipKey(frame), structure_(structure.value()) {}
    // 0x00486300: nothing when there's no such structure. True.
    bool work_on(void* object) override;

private:
    uint32_t structure_; // +0x0C
};

// An object killed: the one named (name=) in a global structure (kind 6; 0x10 bytes, vtable 0x0064883C).
class KillObjectKey final : public UserClipKey {
public:
    KillObjectKey(int frame, checksums::Name structure) : UserClipKey(frame), structure_(structure.value()) {}
    // 0x00486340: the object's flag_as_dead (vtable slot 6; not checked for null). True.
    bool work_on(void* object) override;

private:
    uint32_t structure_; // +0x0C
};

constexpr std::size_t kCameraKeyAtPointOffset = 0x1C;
constexpr std::size_t kCameraKeyBytes = 0x30;

// The camera moved (kinds 2 and 7; 0x30 bytes, vtable 0x00648848).
class CameraKey final : public UserClipKey {
public:
    // 0x00486490: at `frame`, turned by `rotation` times a quarter turn about x, at `pos` with y and z swapped (z
    // negated); `cut` (kind 7).
    CameraKey(int frame, const maths::QuatRotation* rotation, const maths::Vec4* pos, bool cut);
    // 0x004861D0: as the base's, but a frame either side of its frame counts (without `cut`: up to one before `end`;
    // with it, not before `end`).
    bool inside_range(float start, float end, bool inclusive) override;
    // 0x00486390: the camera put there (none at frame 0: false) - and left active, so it's put there again each
    // time. True.
    bool work_on(void* object) override;

private:
    maths::QuatRotation orientation_turn_{}; // +0x0C
    maths::Vec4 at_point_{};                 // +0x1C (w 1)
    bool cut_;                             // +0x2C
    std::array<uint8_t, 3> pad2d_{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(CameraKey, at_point_) == kCameraKeyAtPointOffset && sizeof(CameraKey) == kCameraKeyBytes, "the original's layout");
    }
};

// An animation track's event (0x14 bytes, vtable 0x006487BC; made in an array - new[] / delete[], the count before
// it as the original's): at a frame, an event type with parameters, sent by the object to itself once the timeline
// passes it while armed.
class AnimScriptKey final : public UserClipKey {
public:
    AnimScriptKey(const AnimScriptKey&) = delete;
    AnimScriptKey& operator=(const AnimScriptKey&) = delete;
    // 0x00481A20: no parameters (the frame and the flag zero - the original left them as they were).
    AnimScriptKey() = default;
    // 0x00486560 / 0x00481DB0: the structure freed.
    ~AnimScriptKey() override;
    // 0x004865D0: at `frame`, sending `event` with a copy of `params` (a new structure: the old one isn't freed).
    void assign_value(int frame, checksums::Name event_name, script::Struct* params);
    // 0x00486440: the event sent by the object to itself. True.
    bool work_on(void* object) override;

private:
    uint32_t kind_tag_{};                // +0x0C
    script::Struct* call_args_{nullptr}; // +0x10
};
constexpr std::size_t kAnimScriptKeyBytes = 0x14;

static_assert(sizeof(AnimScriptKey) == kAnimScriptKeyBytes, "the original's size");

// 0x00486680 (cdecl): the next key from a frame data's key list {frame, kind, size} then the kind's data - read
// through util::ByteReader (anim_format.h), within its bounds (not original: the original moved a pointer on). Unknown
// kinds are skipped (by size); null for them, when it runs past the end and when memory runs out.
gsl::owner<UserClipKey*> create_custom_anim_key(util::ByteReader& reader);

} // namespace gfx

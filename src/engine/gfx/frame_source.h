#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

// An animation frame source (Nx; 0x214 bytes in the original, from the frame sources' pools 0x006FA7F4): the animation
// it plays, its search cursors into the animation's keys and the skeleton's bone masks; its frame data is the asset
// manager's. (The original's vtable 0x00648A68 held only the deleting destructor and it has no subclass: no vtable
// here, nor its word.) The frame data (gfx::JointClipFrames, boned_anim_frame_data.h) reads and moves the cursors.
namespace render {

constexpr std::size_t kBoneKeySlots = 64;

struct FrameSource {
    // From the frame sources' pools (null when they're full); back to them.
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* source);

    // 0x0048CF70: everything after the vtable's word zeroed (here: everything).
    FrameSource() = default;
    // (0x0048D000 the scalar deleting destructor: nothing of its own)

    // The fields other code reads and sets.
    std::array<const uint8_t*, kBoneKeySlots>& quat_at() { return quat_at_; }
    [[nodiscard]] const std::array<const uint8_t*, kBoneKeySlots>& quat_at() const { return quat_at_; }
    std::array<const uint8_t*, kBoneKeySlots>& trans_at() { return trans_at_; }
    [[nodiscard]] const std::array<const uint8_t*, kBoneKeySlots>& trans_at() const { return trans_at_; }
    [[nodiscard]] bool resume() const { return resume_; }
    [[nodiscard]] const uint32_t* bone_masks() const { return bone_masks_; }
    void set_bone_masks(const uint32_t* value) { bone_masks_ = value; }
    [[nodiscard]] int mask_bit() const { return mask_bit_; }
    void set_mask_bit(int value) { mask_bit_ = value; }

private:
    uint32_t anim_ = 0;                    // the animation's checksum
    std::array<const uint8_t*, kBoneKeySlots> quat_at_ = {};      // each bone's rotation key its search last ended at
    std::array<const uint8_t*, kBoneKeySlots> trans_at_ = {};     // and translation key
    bool resume_ = false;                  // set_done's flag: the searches start where they last ended, and the bones
                                           // `bone_masks_` collapse at level of detail `mask_bit_` are skipped
    const uint32_t* bone_masks_ = nullptr; // the skeleton's (a word for each bone: a bit per level of detail)
    int mask_bit_ = 0;                     // the skeleton's level of detail

public:

    // 0x0048CFA0: done or not (resume_); not done, the animation's data (null when done).
    void* set_done(bool done);
    // 0x0048CFD0: the animation `anim` started (not done); its data.
    void* play_clip(uint32_t anim);
};

// 0x0048CF50: the asset manager's data for `checksum` (null for 0).
void* get_anim_data(uint32_t checksum, int unused);

} // namespace render

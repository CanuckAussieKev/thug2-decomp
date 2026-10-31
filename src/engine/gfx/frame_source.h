#pragma once

#include <cstddef>
#include <cstdint>

// An animation frame source (Nx; 0x214 bytes from the frame sources' pools 0x006FA7F4): the animation it plays and
// whether it's done; its frame data is the asset manager's. (The original's vtable 0x00648A68 holds only the deleting
// destructor and it has no subclass: no vtable here - its word is kept, as the frame data's view of the object,
// gfx::FrameSource in boned_anim_frame_data.h, and the cutscene data read the rest by offset. Stage 5.)
namespace render {

struct FrameSource {
    // From the frame sources' pools (null when they're full); back to them.
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* source);

    // 0x0048CF70: everything after the vtable's word zeroed.
    FrameSource();
    // (0x0048D000 the scalar deleting destructor: nothing of its own)

    uint32_t vtable_slot_unused_ = 0;
    uint32_t anim_; // +0x04: the animation's checksum
    uint8_t unknown8_[0x200];
    bool is_finished_; // +0x208
    uint8_t pad209[0xB];

    // 0x0048CFA0: done or not; not done, the animation's data (null when done).
    void* set_done(bool done);
    // 0x0048CFD0: the animation `anim` started (not done); its data.
    void* play_clip(uint32_t anim);
};
static_assert(offsetof(FrameSource, is_finished_) == 0x208 && sizeof(FrameSource) == 0x214,
              "layout must match the original");

// 0x0048CF50: the asset manager's data for `checksum` (null for 0).
void* __cdecl get_anim_data(uint32_t checksum, int unused);

} // namespace render

#pragma once

#include <cstddef>
#include <cstdint>

// An animation frame source (Nx; 0x214 bytes from the frame sources' pools 0x006FA7F4, vtable 0x00648A68 - its one
// slot the deleting destructor): the animation it plays and whether it's done; its frame data is the asset
// manager's.
namespace render {

struct FrameSource {
    void** vtable;
    uint32_t anim_;          // +0x04: the animation's checksum
    uint8_t unknown8_[0x200];
    bool is_finished_;              // +0x208
    uint8_t pad209[0xB];

    // 0x0048CF70: construct - everything after the vtable zeroed. Returns this.
    FrameSource* make_new();
    // 0x0048D000 (virtual [0]): destroyed, and with flags & 1 given back to the pools (0x004645E0). Returns this.
    FrameSource* teardown(unsigned int flags);
    // 0x0048CFA0: done or not; not done, the animation's data (null when done).
    void* set_done(bool done);
    // 0x0048CFD0: the animation `anim` started (not done); its data.
    void* play_clip(uint32_t anim);
};
static_assert(offsetof(FrameSource, is_finished_) == 0x208 && sizeof(FrameSource) == 0x214,
              "layout must match the original");

// 0x0048CF50: the asset manager's data for `checksum` (null for 0).
void* __cdecl get_anim_data(uint32_t checksum, int unused);

} // namespace Nx

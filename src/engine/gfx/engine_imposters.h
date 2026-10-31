#pragma once

#include <cstddef>
#include <cstdint>

namespace lists {
class KeyMap;
}

// The engine's imposter manager (Nx; the object at 0x006FA7BC, 8 bytes): its imposter groups in a hash table, each
// (by its vtable) asked once a frame whether it's near enough to be drawn as itself - else its imposter made, at
// most `max_per_frame_` made or updated a frame - and the imposters drawn between the PC's primitive states.
// (The groups: +4 the distance below which they're themselves, +0x58 a frame count, +0x5C the imposter made; slots
// [1] make the imposter, [2] drop it, [3] bring it up to date (whether it did), [4] draw it, [5] the distance (ST0),
// [6] updated.)
namespace render {

struct ImposterManager {
    lists::KeyMap* groups_ptr_;
    int max_per_frame_;

    // 0x0048E3E0: construct - a table of 2^8 buckets, 1 imposter a frame. Returns this.
    ImposterManager* make_new();
    // 0x0048E260: every group deleted (virtual destructor, flag 1), the table emptied.
    void delete_all();
    // 0x0048E2D0: each group updated, its imposter dropped when it's within its distance, else made or brought up to
    // date while the frame's count allows (made: counted; brought up to date: its frame count +1, counted when it
    // was).
    void tick_frame();
    // 0x0048E370: the imposters drawn (the groups that have one), between set_primitive_states and
    // reset_primitive_states.
    void render();
};
static_assert(sizeof(ImposterManager) == 8, "layout must match the original");

} // namespace render

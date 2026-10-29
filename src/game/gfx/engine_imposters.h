#pragma once

#include <cstddef>
#include <cstdint>

namespace Lst {
class HashTable;
}

// The engine's imposter manager (Nx; the object at 0x006FA7BC, 8 bytes): its imposter groups in a hash table, each
// (by its vtable) asked once a frame whether it's near enough to be drawn as itself - else its imposter made, at
// most `m_maxPerFrame` made or updated a frame - and the imposters drawn between the PC's primitive states.
// (The groups: +4 the distance below which they're themselves, +0x58 a frame count, +0x5C the imposter made; slots
// [1] make the imposter, [2] drop it, [3] bring it up to date (whether it did), [4] draw it, [5] the distance (ST0),
// [6] updated.)
namespace Nx {

struct CImposterManager {
    Lst::HashTable* mp_groups;
    int m_maxPerFrame;

    // 0x0048E3E0: construct - a table of 2^8 buckets, 1 imposter a frame. Returns this.
    CImposterManager* Construct();
    // 0x0048E260: every group deleted (virtual destructor, flag 1), the table emptied.
    void DeleteAll();
    // 0x0048E2D0: each group updated, its imposter dropped when it's within its distance, else made or brought up to
    // date while the frame's count allows (made: counted; brought up to date: its frame count +1, counted when it
    // was).
    void Update();
    // 0x0048E370: the imposters drawn (the groups that have one), between SetPrimitiveStates and
    // ResetPrimitiveStates.
    void Render();
};
static_assert(sizeof(CImposterManager) == 8, "layout must match the original");

} // namespace Nx

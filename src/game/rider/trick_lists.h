#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/core/text_store.h"
#include "engine/maths/vec4.h"

// Two kinds of list entries the skater's trick code (0x00512000-) keeps (each a lists::Elem of its own type with a
// one-slot vtable: the scalar deleting destructor). Their constructors and destructors were placed at
// 0x00578670-0x005787F0 and 0x00579570-0x00579690.
namespace script {
class Array;
}

namespace objects {

// 0x64 bytes (vtable 0x0064EC80; its node type's 0x0064BE88), made by 0x005123D0's caller: +0x14 / +0x1C set by the
// maker, +0x20 the key it's found by, a script array of its own (+0x60).
struct TrickListEntry {
    memory::ListNode list_link_; // (the original's node size: the entries are read by byte offset)
    uint32_t gap_id_;            // +0x14 (as a gap: the gap list's - gap_entry.cpp)
    uint32_t option_labels_;     // +0x18 its options (cancel / require bits)
    int node_;                   // +0x1C
    uint32_t lookup_key_;        // +0x20 the key it's found by (a gap: its key combo)
    int trick_count_;            // +0x24 the score's count of the trick at the start
    uint32_t trick_script_;      // +0x28
    uint32_t trick_text_;        // +0x2C (its checksum)
    int spin_;                   // +0x30 in half turns
    bool perfect_;               // +0x34
    uint8_t pad35[3];
    uint32_t tricks_count_;       // +0x38 the score's tricks then
    int taps_;                    // +0x3C
    maths::Vec4 start_pos_;       // +0x40
    maths::Vec4 start_dir_;       // +0x50
    script::Array* element_list_; // +0x60

    // 0x00578740: an unlinked node (its data itself), the fields cleared, a new empty script array.
    TrickListEntry();
    // 0x00579570: the array's contents and the array freed, then the node unlinked (its node type's vtable).
    // 0x00579820: the scalar deleting destructor.
    ~TrickListEntry();
    TrickListEntry(const TrickListEntry&) = delete;
    TrickListEntry& operator=(const TrickListEntry&) = delete;
    TrickListEntry* teardown(unsigned int flags);
};

// 0x34 bytes (vtable 0x0064EC84; its node type's 0x0064BE8C), made by 0x00512060: a name (+0x14) and what its maker
// sets after it.
struct TrickNameEntry {
    memory::ListNode list_link_;
    text::Chars label_;     // +0x14
    uint32_t gap_id_;       // +0x20 the gap it's part of
    int node_;              // +0x24
    uint32_t trick_script_; // +0x28
    uint8_t done_;          // +0x2C (set_flag; a gap trick: done)
    uint8_t pad2d_[3];
    uint32_t frame_; // +0x30

    // 0x005787F0: an unlinked node (its data itself), an empty name. Returns this.
    TrickNameEntry* make_new();
    // 0x00579630: the name freed, the node unlinked (its node type's vtable). 0x00579600: the scalar deleting
    // destructor.
    void destroy_body();
    TrickNameEntry* teardown(unsigned int flags);
    // 0x00578670: done_ set.
    void set_flag();
};

} // namespace objects

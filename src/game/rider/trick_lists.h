#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/core/text_store.h"

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
    memory::ListNode list_link_;
    uint32_t unknown14_[3];
    uint32_t lookup_key_;           // +0x20
    uint32_t unknown24_;
    uint32_t unknown28_;
    bool flag_2c_;            // +0x2C (0x00578670 sets it)
    uint8_t pad2d_[3];
    uint32_t unknown30_;
    bool unknown34_;
    uint8_t pad35[3];
    uint32_t unknown38_;
    uint32_t unknown3c_;
    uint8_t unknown40_[0x20];
    script::Array* element_list_;  // +0x60

    // 0x00578740: an unlinked node (its data itself), the fields cleared, a new empty script array. Returns this.
    TrickListEntry* make_new();
    // 0x00579570: the array's contents and the array freed, then the node unlinked (its node type's vtable).
    // 0x00579820: the scalar deleting destructor.
    void destroy_body();
    TrickListEntry* teardown(unsigned int flags);
    // 0x00578670
    void set_flag();
};
static_assert(offsetof(TrickListEntry, lookup_key_) == 0x20 && offsetof(TrickListEntry, flag_2c_) == 0x2C &&
                  offsetof(TrickListEntry, unknown38_) == 0x38 && offsetof(TrickListEntry, element_list_) == 0x60 &&
                  sizeof(TrickListEntry) == 0x64,
              "layout must match the original");

// 0x34 bytes (vtable 0x0064EC84; its node type's 0x0064BE8C), made by 0x00512060: a name (+0x14) and what its maker
// sets after it.
struct TrickNameEntry {
    memory::ListNode list_link_;
    text::Chars label_;      // +0x14
    uint32_t unknown20_[5];

    // 0x005787F0: an unlinked node (its data itself), an empty name. Returns this.
    TrickNameEntry* make_new();
    // 0x00579630: the name freed, the node unlinked (its node type's vtable). 0x00579600: the scalar deleting
    // destructor.
    void destroy_body();
    TrickNameEntry* teardown(unsigned int flags);
};
static_assert(offsetof(TrickNameEntry, label_) == 0x14 && sizeof(TrickNameEntry) == 0x34,
              "layout must match the original");

} // namespace Obj

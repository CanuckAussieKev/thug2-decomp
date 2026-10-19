#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/core/string.h"

// Two kinds of list entries the skater's trick code (0x00512000-) keeps (each a Lst::Node of its own type with a
// one-slot vtable: the scalar deleting destructor). Their constructors and destructors were placed at
// 0x00578670-0x005787F0 and 0x00579570-0x00579690.
namespace Script {
class CArray;
}

namespace Obj {

// 0x64 bytes (vtable 0x0064EC80; its node type's 0x0064BE88), made by 0x005123D0's caller: +0x14 / +0x1C set by the
// maker, +0x20 the key it's found by, a script array of its own (+0x60).
struct TrickListEntry {
    Mem::ListNode m_node;
    uint32_t m_unknown14[3];
    uint32_t m_key;           // +0x20
    uint32_t m_unknown24;
    uint32_t m_unknown28;
    bool m_flag2C;            // +0x2C (0x00578670 sets it)
    uint8_t pad2D[3];
    uint32_t m_unknown30;
    bool m_unknown34;
    uint8_t pad35[3];
    uint32_t m_unknown38;
    uint32_t m_unknown3C;
    uint8_t m_unknown40[0x20];
    Script::CArray* m_array;  // +0x60

    // 0x00578740: an unlinked node (its data itself), the fields cleared, a new empty script array. Returns this.
    TrickListEntry* Construct();
    // 0x00579570: the array's contents and the array freed, then the node unlinked (its node type's vtable).
    // 0x00579820: the scalar deleting destructor.
    void DestroyBody();
    TrickListEntry* Destroy(unsigned int flags);
    // 0x00578670
    void SetFlag();
};
static_assert(offsetof(TrickListEntry, m_key) == 0x20 && offsetof(TrickListEntry, m_flag2C) == 0x2C &&
                  offsetof(TrickListEntry, m_unknown38) == 0x38 && offsetof(TrickListEntry, m_array) == 0x60 &&
                  sizeof(TrickListEntry) == 0x64,
              "layout must match the original");

// 0x34 bytes (vtable 0x0064EC84; its node type's 0x0064BE8C), made by 0x00512060: a name (+0x14) and what its maker
// sets after it.
struct TrickNameEntry {
    Mem::ListNode m_node;
    Str::String m_name;      // +0x14
    uint32_t m_unknown20[5];

    // 0x005787F0: an unlinked node (its data itself), an empty name. Returns this.
    TrickNameEntry* Construct();
    // 0x00579630: the name freed, the node unlinked (its node type's vtable). 0x00579600: the scalar deleting
    // destructor.
    void DestroyBody();
    TrickNameEntry* Destroy(unsigned int flags);
};
static_assert(offsetof(TrickNameEntry, m_name) == 0x14 && sizeof(TrickNameEntry) == 0x34,
              "layout must match the original");

} // namespace Obj

#pragma once

#include <cstddef>
#include <cstdint>

#include "game/lst/head.h"

namespace Obj {
class CCompositeObject;
} // namespace Obj

namespace Mdl {

// The skate module (Mdl::Skate, the singleton at 0x007CE478): only what has source so far - its list of skaters.
class Skate {
public:
    // The singleton (0x007CE478; null before the module starts).
    static Skate* Instance();

    // 0x0041AD90: the number of skaters in the list (the nodes from the head's next until the head - checked by its
    // data, -1, before a node is checked for null, as the original does).
    int GetNumSkaters();
    // 0x005665C0 (no source yet): skater `index` of the list.
    Obj::CCompositeObject* GetSkater(int index);

    uint8_t unknown00[0x28];
    Lst::Head m_skaters; // +0x28
};
static_assert(offsetof(Skate, m_skaters) == 0x28, "layout must match the original");

} // namespace Mdl

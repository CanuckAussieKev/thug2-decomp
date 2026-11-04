// The network manager's list records. See gamenet_lists.h.

#include "engine/net/gamenet_lists.h"

#include "engine/core/manager.h"

namespace online {

// 0x005371F0
PendingScriptItem* PendingScriptItem::destroy_record(unsigned int flags)
{
    uncouple();
    if ((flags & 1) != 0U) {
        memory::Hub::dealloc(this);
    }
    return this;
}

} // namespace online

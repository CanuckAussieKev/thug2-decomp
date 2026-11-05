// The net library's list types' destructors. See net_lists.h.

#include "engine/net/net_lists.h"

#include "engine/memory/manager.h"
#include "base/core/checked_index.h"

namespace net {

namespace {

void dealloc(void* memory) { memory::Hub::dealloc(memory); }

} // namespace

// 0x00457260
void HandlerLists::teardown()
{
    for (int i = kCount - 1; i >= 0; --i) {
        core::at(lists_, i).uncouple();
    }
}

// 0x00457460
BlockedConnEntry* BlockedConnEntry::destroy_banned(unsigned int flags)
{
    uncouple();
    if ((flags & 1) != 0U) {
        dealloc(this);
    }
    return this;
}

} // namespace net

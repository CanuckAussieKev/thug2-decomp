// The net library's list types' destructors. See net_lists.h.

#include "engine/net/net_lists.h"

#include "engine/core/manager.h"

namespace net {

namespace {

void dealloc(void* memory) { memory::Hub::get_pull()->dealloc(memory); }

} // namespace

// 0x00457260
void HandlerLists::teardown()
{
    for (int i = kCount - 1; i >= 0; --i)
        lists_[i].uncouple();
}

// 0x00457460
BlockedConnEntry* BlockedConnEntry::destroy_banned(unsigned int flags)
{
    uncouple();
    if (flags & 1)
        dealloc(this);
    return this;
}

} // namespace net

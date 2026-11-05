// NOT ORIGINAL - the input events' handlers (input_event.h).

#include "engine/pad_input/input_event.h"

#include <vector>

#include "base/core/no_destroy.h"

namespace input {

namespace {

// The handlers added, in their order. Made on first use, never destroyed (handlers()).
std::vector<EventHandler>& handlers()
{
    static core::NoDestroy<std::vector<EventHandler>> handlers;
    return handlers.get();
}

} // namespace

void add_event_handler(EventHandler handler) { handlers().push_back(handler); }

void deliver(const Event& event)
{
    for (EventHandler handler : handlers()) {
        handler(event);
    }
}

} // namespace input

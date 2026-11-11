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

EventHandler& captured_by()
{
    static EventHandler handler = nullptr;
    return handler;
}

} // namespace

void add_event_handler(EventHandler handler) { handlers().push_back(handler); }

void capture_events(EventHandler handler) { captured_by() = handler; }

void deliver(const Event& event)
{
    if (EventHandler only = captured_by()) {
        only(event);
        return;
    }
    for (EventHandler handler : handlers()) {
        handler(event);
    }
}

} // namespace input

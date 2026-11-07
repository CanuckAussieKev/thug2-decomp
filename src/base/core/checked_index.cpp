// NOT ORIGINAL - the soft_bounds handler's slot (checked_index.h).

#include "base/core/checked_index.h"

#include <atomic>

namespace core::detail {

std::atomic<OutOfRangeHandler>& out_of_range_slot()
{
    static std::atomic<OutOfRangeHandler> handler{nullptr};
    return handler;
}

} // namespace core::detail

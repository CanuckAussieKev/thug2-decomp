#pragma once

#include "base/core/raw_view.h"

namespace memory {

// A field read or written as another type of its size, as the original's code does in places (a float's bits as an
// integer, an int as unsigned): the same bytes, nothing converted.
template <typename As, typename Field>
As& bits_as(Field& field)
{
    static_assert(sizeof(As) == sizeof(Field), "the same size");
    return *raw::as<As>(&field);
}

} // namespace memory

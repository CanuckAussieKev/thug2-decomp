// The one-pointer member single_instance.h describes (LazyInstance<T> itself is all inline).

#include "engine/core/single_instance.h"

namespace single {

// 0x004E1970
PointerMember* PointerMember::make_new(void* pointer)
{
    pointer_ = pointer;
    return this;
}

// 0x00469490
void* PointerMember::get_pull() { return pointer_; }

} // namespace single

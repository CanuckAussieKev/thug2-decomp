// NOT ORIGINAL - the park editor's state its files share: the copied rails' origin and pools, the edited goal's and created goal's names, the collision and rail editor switches and the park editor's owner (park_editing_state.h): made on first use, never destroyed.

#include "game/park_editing/park_editing_state.h"

#include "engine/core/no_destroy.h"

namespace park_editing {

ParkEditingState& park_editing_state()
{
    static core::NoDestroy<ParkEditingState> state;
    return state.get();
}

} // namespace park_editing

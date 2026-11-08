// NOT ORIGINAL - see-through copies of geometry drawn in the 3D view (ghost_draws.h): what the UI handed over.

#include "engine/gfx/ghost_draws.h"

#include <span>
#include <vector>

#include "base/core/no_destroy.h"

namespace render::ghost_draws {

namespace {

std::vector<Ghost>& held_ghosts()
{
    static core::NoDestroy<std::vector<Ghost>> ghosts;
    return ghosts.get();
}

} // namespace

void set_ghosts(std::span<const Ghost> ghosts) { held_ghosts().assign(ghosts.begin(), ghosts.end()); }

std::span<const Ghost> pending() { return held_ghosts(); }

void clear() { held_ghosts().clear(); }

} // namespace render::ghost_draws

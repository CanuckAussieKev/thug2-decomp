// NOT ORIGINAL - outlines round geometry in the 3D view (outline_draws.h): what the UI handed over.

#include "engine/gfx/outline_draws.h"

#include <span>
#include <vector>

#include "base/core/no_destroy.h"

namespace render::outline_draws {

namespace {

struct OutlineState {
    std::vector<Outline> outlines;
    bool drawn = true; // (until the renderer says otherwise: the first frame tries)
};

OutlineState& outline_state()
{
    static core::NoDestroy<OutlineState> state;
    return state.get();
}

} // namespace

void set_outlines(std::span<const Outline> outlines)
{
    outline_state().outlines.assign(outlines.begin(), outlines.end());
}

bool outlines_drawn() { return outline_state().drawn; }

std::span<const Outline> pending() { return outline_state().outlines; }

void clear() { outline_state().outlines.clear(); }

void note_drawn(bool drawn) { outline_state().drawn = drawn; }

} // namespace render::outline_draws

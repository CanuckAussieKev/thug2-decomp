// NOT ORIGINAL - the lines drawn in the 3D view against its depth (depth_lines.h): what the UI handed over.

#include "engine/gfx/depth_lines.h"

#include <algorithm>
#include <span>
#include <vector>

#include "base/core/no_destroy.h"

namespace render::depth_lines {

namespace {

std::vector<Line>& held_lines()
{
    static core::NoDestroy<std::vector<Line>> lines;
    return lines.get();
}

} // namespace

void set_lines(std::span<const Line> lines)
{
    std::vector<Line>& held = held_lines();
    held.assign(lines.begin(), lines.end());
    // (in Depth's order: the hidden parts under the lines that show, the lines over everything last)
    std::ranges::stable_sort(held, {}, &Line::depth);
}

std::span<const Line> pending() { return held_lines(); }

void clear() { held_lines().clear(); }

} // namespace render::depth_lines

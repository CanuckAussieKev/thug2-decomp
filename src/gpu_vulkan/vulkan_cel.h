#pragma once

#include "engine/gfx/rt_cel.h"

// NOT ORIGINAL - cel shading on the Vulkan Gpu ([Graphics] rt_cel; engine/gfx/rt_cel.h has the mode and its formulas,
// docs/design/vulkan.md "Cel shading"). What app/ sets; vulkan_cel_draw.h is the backend's side.
namespace render::vulkan {

// The settings (rt::set_cel_settings) and what follows from them now: the world's outlines' pass listened for, the
// depth made samplable, the baked sun asked for. Live (the Options screen): the draws read them as they are drawn.
void set_cel_settings(const rt::CelSettings& settings);

} // namespace render::vulkan

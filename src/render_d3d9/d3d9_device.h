#pragma once

#include "engine/gfx/render_device.h"

// NOT ORIGINAL - the Direct3D 9 backend's render::Device (d3d9_device.cpp): each method is the PC platform layer's own
// function. app/ installs it at start-up.
namespace render::d3d9 {

// The Direct3D 9 device (made on first use, never destroyed).
Device& device();

} // namespace render::d3d9

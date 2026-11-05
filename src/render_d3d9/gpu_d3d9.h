#pragma once

#include "engine/gfx/gpu.h"

// NOT ORIGINAL - Direct3D 9's render::Gpu (gpu_d3d9.cpp): each operation is the device call it stands for, with the
// same arguments (Direct3D 9's numbers pass through unchanged). app/ installs it with the device.
namespace render::d3d9 {

// The Direct3D 9 Gpu (made on first use, never destroyed).
Gpu& gpu();

} // namespace render::d3d9

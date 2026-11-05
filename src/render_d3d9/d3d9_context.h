#pragma once

#include "engine/gfx/gpu_context.h"

// NOT ORIGINAL - Direct3D 9's render::GpuContext (d3d9_context.cpp): Direct3DCreate9, CreateDevice, Reset and the
// present parameters they use. app/ installs it with the Gpu.
namespace render::d3d9 {

// The Direct3D 9 context (made on first use, never destroyed).
GpuContext& gpu_context();

} // namespace render::d3d9

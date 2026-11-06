// NOT ORIGINAL - the installed GpuContext (gpu_context.h).

#include "engine/gfx/gpu_context.h"

namespace render {

namespace {

// The context app/ installed (a constant-initialised pointer: no guard, nothing to destroy).
GpuContext*& installed()
{
    static GpuContext* context = nullptr;
    return context;
}

} // namespace

GpuContext& gpu_context() { return *installed(); }

void install_gpu_context(GpuContext& context) { installed() = &context; }

} // namespace render

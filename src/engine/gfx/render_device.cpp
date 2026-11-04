// NOT ORIGINAL - the installed graphics device (render_device.h).

#include "engine/gfx/render_device.h"

namespace render {

namespace {

// The device app/ installed (a constant-initialised pointer: no guard, nothing to destroy).
Device*& installed()
{
    static Device* device = nullptr;
    return device;
}

} // namespace

Device& device() { return *installed(); }

void install_device(Device& device) { installed() = &device; }

} // namespace render

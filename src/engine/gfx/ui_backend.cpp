// NOT ORIGINAL - the installed UI backend (ui_backend.h).

#include "engine/gfx/ui_backend.h"

namespace render {

namespace {

// The UI backend app/ installed (a constant-initialised pointer: no guard, nothing to destroy).
UiBackend*& installed_ui_backend()
{
    static UiBackend* backend = nullptr;
    return backend;
}

} // namespace

UiBackend& ui_backend() { return *installed_ui_backend(); }

void install_ui_backend(UiBackend& backend) { installed_ui_backend() = &backend; }

} // namespace render

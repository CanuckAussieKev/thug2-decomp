// Self-tests for the HD menu UI's markers (src/game/gfx/hd_ui.h; not original): what tools/pipeline/hd_textures/ui.ts writes
// is recognised, the game's own files (0 there) and anything else are not. (The loaders with the game's own,
// unmarked, images and fonts are compared with the originals by selftests_appmain9 / selftests_gapsA, with the
// option on and off.)

#include "game/gfx/hd_ui.h"
#include "hook/selftest.h"

namespace {

void TestHdUiMarkers()
{
    using namespace Nx::HdUi;
    Check(ImageScale(0) == 1 && ImageScale(0x48440004) == 4 && ImageScale(0x48440002) == 2 &&
              ImageScale(0x48440003) == 1 && ImageScale(0x48440104) == 1 && ImageScale(0x00000004) == 1 &&
              ImageScale(0x48450004) == 1,
          "hd ui: image header markers");
    Check(FontScale(0x49554448, 4) == 4 && FontScale(0x49554448, 2) == 2 && FontScale(0x49554448, 8) == 1 &&
              FontScale(0x45004800, 4) == 1 && FontScale(0, 0) == 1,
          "hd ui: font page markers");
}

} // namespace

SELFTEST(Early, TestHdUiMarkers);

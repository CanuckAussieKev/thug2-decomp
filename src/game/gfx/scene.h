#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The world's scenes (Nx engine): the script commands that load them.
namespace Nx {

// 0x00445130: the LoadScene script command - LoadScene scene = "<name>" [is_dictionary] [is_sky] [is_net]
// [no_supersectors]. Loads levels\<name>\<name>.tex (the texture dictionary) and the scene with it; a name
// containing "_sky" (any case) is a sky (drawn in the sky pass, no super sectors), as is.is_sky. Always returns true.
// With thug2-decomp.ini [features] menu_backdrop on, loading the front end's scene ("mainmenu") also loads the
// backdrop level's scenes (see game/front/menu_backdrop.h).
bool __cdecl CFunc_LoadScene(Script::CStruct* params, Script::CScript* script);

} // namespace Nx

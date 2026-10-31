#pragma once

namespace script {
class Struct;
class Script;
} // namespace Script

// The world's scenes (Nx engine): the script commands that load them.
namespace render {

// 0x00445130: the LoadScene script command - LoadScene scene = "<name>" [is_dictionary] [is_sky] [is_net]
// [no_supersectors]. Loads levels\<name>\<name>.tex (the texture dictionary) and the scene with it; a name
// containing "_sky" (any case) is a sky (drawn in the sky pass, no super sectors), as is.is_sky. Always returns true.
bool __cdecl cfunc_load_scene(script::Struct* params, script::Script* script);

} // namespace Nx

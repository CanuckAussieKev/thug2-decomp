#pragma once

// The original's start-up initialisers as source. Before WinMain the original's C runtime start-up
// (_cinit) runs two tables of initialisers: the C ones (only the C runtime's own: its atexit table, clock(), SSE2
// detection, stdio, the multibyte tables, the C++ unhandled-exception filter) and the C++ ones - after the security
// cookie, 77 game initialisers that construct globals (hash tables, list heads, lights, textures, the expression
// evaluator...), fill the script member-function tables' code pointers and create two mutexes. Here every global starts
// with its start value, before any initialiser, so RunStartupInitialisers does each one's work in the original's order.
//
// The pieces are defined next to the globals they make (the owners' files), each named after what it does, with the
// original initialiser's address.
namespace Sys {

// Every effect of the original's start-up initialisers, in their order (the table at 0x0067C000):
// called first thing in WinMain (by then the compiler's own dynamic initialisers have all run, as the original's CRT had
// run its own). Once: it constructs, it doesn't reset.
void RunStartupInitialisers();

} // namespace Sys

namespace Nx {
void ConstructSharedOrientation(); // 0x00640860
void ConstructMovables();          // 0x00640880
void ConstructSceneList();         // 0x00640B00
void ConstructFontTable();         // 0x00640B40
void SetWorldLightsAmbient();      // 0x00640B60
void ConstructWorldLightStack();   // 0x00640B90
void ConstructSceneLights();       // 0x00640BC0
void ConstructLoadedTexDicts();    // 0x00640BF0
void SetUpGameTextureDict();       // 0x00640C10
void SetUpParticleTextureDict();   // 0x00640C60
void ConstructSpareTexDictTable(); // 0x00640CB0
void ConstructShadowEdgeTable();   // 0x00640CD0
void CreateScreenMutex();          // 0x00640D10
void ConstructProjectionBatches(); // 0x00640D30
void ConstructSpareMaterial();     // 0x00640D70
void ConstructGrassTextures();     // 0x00640D90
void ConstructGrassTextureAnimation(); // 0x00640DC0
void ConstructShaderCache();       // 0x00640DF0
void ConstructGlowTable();         // 0x00640E10
} // namespace Nx

namespace Obj {
void ConstructRigidBodyCache();     // 0x00640900
void ConstructSpareObjectList();    // 0x006409F0
void ConstructInstanceList();       // 0x00640A10
void ConstructCutsceneRotations();  // 0x00640A30
void FillProximityMemberFunctions(); // 0x00644910
} // namespace Obj

namespace Terrain {
void ConstructTerrainTypes();   // 0x00640950
void ConstructTerrainActions(); // 0x00640970
} // namespace Terrain

namespace Pcm {
void CreateStreamLock(); // 0x006409D0
} // namespace Pcm

namespace Script {
void ConstructEvaluator(); // 0x00640A70
} // namespace Script

namespace Gfx {
void ConstructSpareRotations();    // 0x00640AA0
void ConstructBoneVolumeTable();   // 0x00640AE0
} // namespace Gfx

namespace CFuncs {
void ConstructCutsceneDestroyList(); // 0x00644A00
} // namespace CFuncs

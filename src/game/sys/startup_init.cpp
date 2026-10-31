// The original's start-up initialisers, in their order (see startup_init.h). The table of the C++ initialisers
// (0x0067C000 - 0x0067C13C, run by _cinit 0x006249D1 before WinMain) entry by entry; data/startup_initialisers.csv lists
// every entry with what it does and where its work is done here.
//
// Not reproduced, and why:
// - The C initialisers (0x0067C144 - 0x0067C160) and entry 1 (0x00628777, the security cookie): the C runtime's own,
//   which the C runtime we link does for itself.
// - Initialisers whose work is zeros over zero storage (.bss): the smart pointers 0x006408D0 / 0x00640920, the script
//   components 0x00640990 / 0x00640A60 / 0x00640A90, the stream frame slot 0x006409A0, the font slots' +0x18 words
//   0x00640B20, the file tables 0x00644A20. And 0x00640CF0 assigns "" to the loading-screen picture's std::string,
//   whose image bytes already are an empty string (capacity 15).
// - 0x00640AD0 and 0x00640D60 only register exit functions; every initialiser's exit function (atexit: free the
//   tables, close the mutexes...) is left out - the game ends with ExitProcess (Sys::FastExit), which never runs them.
// - 0x00644A30: the original's stdext::hash_map of ended occlusion queries (0x007D69FC, the C++ library's): our
//   platform_d3d.cpp keeps them in a function-local std::unordered_map, constructed on first use.
// - 0x004E1950 (Mem::EnsureManager) sits in the slot after the table's end marker (0x0067C140): _cinit never runs it.

#include "game/sys/startup_init.h"

#include <cstdint>

#include "game/script/names.h"
#include "hook/game_global.h"

namespace Sys {

namespace {

// 16 bytes nothing reads, set to a null (virtual-inheritance) member function pointer by 0x006408A0.
GAME_GLOBAL(uint32_t[4], g_nullMemberFunction67D6C0, 0x0067D6C0);

// 0x006408A0
void SetNullMemberFunction()
{
    g_nullMemberFunction67D6C0[0] = 0;
    g_nullMemberFunction67D6C0[1] = 0;
    g_nullMemberFunction67D6C0[2] = 0;
    g_nullMemberFunction67D6C0[3] = 0xFFFFFFFFu;
}

} // namespace

void RunStartupInitialisers()
{
    Nx::ConstructSharedOrientation();        // 0x00640860
    Nx::ConstructMovables();                 // 0x00640880
    SetNullMemberFunction();                 // 0x006408A0
    // 0x006408D0: the proximity skater's smart pointer (zeros)
    Obj::ConstructRigidBodyCache();          // 0x00640900
    // 0x00640920: the rigid bodies' pusher smart pointers (zeros)
    Terrain::ConstructTerrainTypes();        // 0x00640950 (the first allocation: sets the memory manager up)
    Terrain::ConstructTerrainActions();      // 0x00640970
    // 0x00640990: FormatText's script component (zeros); 0x006409A0: the stream frame slot (zeros)
    Pcm::CreateStreamLock();                 // 0x006409D0
    Obj::ConstructSpareObjectList();         // 0x006409F0
    Obj::ConstructInstanceList();            // 0x00640A10
    Obj::ConstructCutsceneRotations();       // 0x00640A30
    // 0x00640A60: the evaluator's result component (zeros)
    Script::ConstructEvaluator();            // 0x00640A70
    // 0x00640A90: the parser's scratch component (zeros)
    Gfx::ConstructSpareRotations();          // 0x00640AA0
    // 0x00640AD0: an exit function only (the hidden models' list)
    Gfx::ConstructBoneVolumeTable();         // 0x00640AE0
    Nx::ConstructSceneList();                // 0x00640B00
    // 0x00640B20: the font slots' +0x18 words (zeros)
    Nx::ConstructFontTable();                // 0x00640B40
    Nx::SetWorldLightsAmbient();             // 0x00640B60
    Nx::ConstructWorldLightStack();          // 0x00640B90
    Nx::ConstructSceneLights();              // 0x00640BC0
    Nx::ConstructLoadedTexDicts();           // 0x00640BF0
    Nx::SetUpGameTextureDict();              // 0x00640C10
    Nx::SetUpParticleTextureDict();          // 0x00640C60
    Nx::ConstructSpareTexDictTable();        // 0x00640CB0
    Nx::ConstructShadowEdgeTable();          // 0x00640CD0
    // 0x00640CF0: the loading-screen picture's name = "" (as the image has it)
    Nx::CreateScreenMutex();                 // 0x00640D10
    Nx::ConstructProjectionBatches();        // 0x00640D30
    // 0x00640D60: an exit function only (it does nothing)
    Nx::ConstructSpareMaterial();            // 0x00640D70
    Nx::ConstructGrassTextures();            // 0x00640D90
    Nx::ConstructGrassTextureAnimation();    // 0x00640DC0
    Nx::ConstructShaderCache();              // 0x00640DF0
    Nx::ConstructGlowTable();                // 0x00640E10
    Script::FillMemberFunctionTables();      // 0x00640E30 - 0x006448C0 (33 tables)
    Obj::FillProximityMemberFunctions();     // 0x00644910
    CFuncs::ConstructCutsceneDestroyList();  // 0x00644A00
    // 0x00644A20: the file tables (zeros); 0x00644A30: the occlusion queries' hash_map (see above)
}

} // namespace Sys

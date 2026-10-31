// The original's start-up initialisers, in their order (see startup_init.h). The table of the C++ initialisers
// (0x0067C000 - 0x0067C13C, run by _cinit 0x006249D1 before WinMain) entry by entry.
//
// Not reproduced, and why:
// - The C initialisers (0x0067C144 - 0x0067C160) and entry 1 (0x00628777, the security cookie): the C runtime's own,
//   which the C runtime we link does for itself.
// - Initialisers whose work is zeros over zero storage (.bss): the smart pointers 0x006408D0 / 0x00640920, the script
//   components 0x00640990 / 0x00640A60 / 0x00640A90, the stream frame slot 0x006409A0, the font slots' +0x18 words
//   0x00640B20, the file tables 0x00644A20. And 0x00640CF0 assigns "" to the loading-screen picture's std::string,
//   whose image bytes already are an empty string (capacity 15).
// - 0x00640AD0 and 0x00640D60 only register exit functions; every initialiser's exit function (atexit: free the
//   tables, close the mutexes...) is left out - the game ends with ExitProcess (SystemCore::FastExit), which never runs them.
// - 0x00644A30: the original's stdext::hash_map of ended occlusion queries (0x007D69FC, the C++ library's): our
//   platform_d3d.cpp keeps them in a function-local std::unordered_map, constructed on first use.
// - 0x004E1950 (MemSys::EnsureManager) sits in the slot after the table's end marker (0x0067C140): _cinit never runs it.

#include "engine/sys/startup_init.h"

#include <cstdint>

#include "engine/script/names.h"

namespace SystemCore {

namespace {

// 16 bytes nothing reads, set to a null (virtual-inheritance) member function pointer by 0x006408A0.
uint32_t g_nullMemberFunction67D6C0[4];

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
    Engine3D::ConstructSharedOrientation();        // 0x00640860
    Engine3D::ConstructMovables();                 // 0x00640880
    SetNullMemberFunction();                 // 0x006408A0
    // 0x006408D0: the proximity skater's smart pointer (zeros)
    GameObjects::ConstructRigidBodyCache();          // 0x00640900
    // 0x00640920: the rigid bodies' pusher smart pointers (zeros)
    SurfaceKind::ConstructTerrainTypes();        // 0x00640950 (the first allocation: sets the memory manager up)
    SurfaceKind::ConstructTerrainActions();      // 0x00640970
    // 0x00640990: FormatText's script component (zeros); 0x006409A0: the stream frame slot (zeros)
    AudioStream::CreateStreamLock();                 // 0x006409D0
    GameObjects::ConstructSpareObjectList();         // 0x006409F0
    GameObjects::ConstructInstanceList();            // 0x00640A10
    GameObjects::ConstructCutsceneRotations();       // 0x00640A30
    // 0x00640A60: the evaluator's result component (zeros)
    Scripts::ConstructEvaluator();            // 0x00640A70
    // 0x00640A90: the parser's scratch component (zeros)
    Gfx2D::ConstructSpareRotations();          // 0x00640AA0
    // 0x00640AD0: an exit function only (the hidden models' list)
    Gfx2D::ConstructBoneVolumeTable();         // 0x00640AE0
    Engine3D::ConstructSceneList();                // 0x00640B00
    // 0x00640B20: the font slots' +0x18 words (zeros)
    Engine3D::ConstructFontTable();                // 0x00640B40
    Engine3D::SetWorldLightsAmbient();             // 0x00640B60
    Engine3D::ConstructWorldLightStack();          // 0x00640B90
    Engine3D::ConstructSceneLights();              // 0x00640BC0
    Engine3D::ConstructLoadedTexDicts();           // 0x00640BF0
    Engine3D::SetUpGameTextureDict();              // 0x00640C10
    Engine3D::SetUpParticleTextureDict();          // 0x00640C60
    Engine3D::ConstructSpareTexDictTable();        // 0x00640CB0
    Engine3D::ConstructShadowEdgeTable();          // 0x00640CD0
    // 0x00640CF0: the loading-screen picture's name = "" (as the image has it)
    Engine3D::CreateScreenMutex();                 // 0x00640D10
    Engine3D::ConstructProjectionBatches();        // 0x00640D30
    // 0x00640D60: an exit function only (it does nothing)
    Engine3D::ConstructSpareMaterial();            // 0x00640D70
    Engine3D::ConstructGrassTextures();            // 0x00640D90
    Engine3D::ConstructGrassTextureAnimation();    // 0x00640DC0
    Engine3D::ConstructShaderCache();              // 0x00640DF0
    Engine3D::ConstructGlowTable();                // 0x00640E10
    Scripts::FillMemberFunctionTables();      // 0x00640E30 - 0x006448C0 (33 tables)
    GameObjects::FillProximityMemberFunctions();     // 0x00644910
    ScriptCommands::ConstructCutsceneDestroyList();  // 0x00644A00
    // 0x00644A20: the file tables (zeros); 0x00644A30: the occlusion queries' hash_map (see above)
}

} // namespace Sys

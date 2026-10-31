// Script commands (CFunctions) of the skate game, 0x0059EDF0-0x0059F610 (see skate_cfuncs.h).

#include "engine/script/skate_cfuncs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/script/cscript.h"
#include "engine/script/node_prefixes.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "platform/stopwatch.h"
#include "engine/sys/model_viewer.h"
#include "engine/gfx/loading_console.h"
#include "engine/gfx/scene_lights.h"
#include "engine/gfx/engine_effects.h"
#include "engine/script/script_startup.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "engine/script/game_cfuncs.h"

namespace ScriptCommands {

char g_levelName[64];
uint32_t g_debounceTimes[22];

namespace {

constexpr uint32_t kTime = 0x906B67BA;           // "time"
constexpr uint32_t kId = 0x40C698AF;             // "id"
constexpr uint32_t kPrefix = 0x6C4E7971;         // "prefix"
constexpr uint32_t kFakeLightsExact = 0x80804991; // (name unknown) FakeLights: the prefix's nodes, not a prefix
constexpr uint32_t kParams = 0x7031F10C;         // "params"
constexpr uint32_t kCallback = 0x86068BD9;       // "callback"
constexpr uint32_t kCallbackParams = 0x0DD85BDA; // "callbackparams"
constexpr uint32_t kOnlineFlag = 0x20209C31;     // "OnlineModeFlag"
constexpr uint32_t kPermanent = 0x23627FD7;      // "permanent"
constexpr uint32_t kNotSessionSpecific = 0x2EBFF4BC; // "NotSessionSpecific"
constexpr uint32_t kKeepSpawnerFile = 0x01917DF2; // (name unknown) SpawnScript: don't note the spawner's .qb file
constexpr uint32_t kFreeze = 0xB96E0BE5;         // "freeze"
constexpr uint32_t kBlank = 0xC3D43B9A;          // "blank"
constexpr uint32_t kOmnigon = 0xAE94C183;        // (name unknown) the Omnigon's checksum
constexpr uint32_t kDumpScriptsFlag = 0x2AB71EEF; // (name unknown) passed to DumpScripts' compiled-out dump

// The button's index in the button table (0x0067D6D8, entries 1-21; 0 if it isn't one).
int GlyphForButtonTag(uint32_t button) { return PadInput::GlyphForButtonTag(button); }

// The last Omnigon checksum sent to the other players (0x007CEBC8).
uint32_t& LastBroadcastedOmnigon() { return g_lastBroadcastedOmnigon; }

// The current level's node array name (0x007CE6D8) and the level's name (0x007CE698, 64 characters).
char* NodeTableKeyText() { return g_nodeListName; }
char* LevelName() { return g_levelName; }

// The viewer (the debug camera and the object it shows, 0x007CE4A8 through its getter 0x00576EA0).
void* Viewer() { return GameModules::GetLiveViewer(); }

// DumpScripts' list of the running scripts - compiled out of this build (an empty call, 0x005480A0).
inline void DumpScriptsList(bool /*flag*/) {}

} // namespace

uint32_t* DebounceTimes() { return g_debounceTimes; }

// 0x0059EE80
bool __cdecl ControllerPressed(const void* controller, uint32_t button)
{
    if (!controller)
        return false;
    auto* input = *reinterpret_cast<PadInputSample* const*>(static_cast<const uint8_t*>(controller) + 0x18);
    if (!input)
        return false;
    int index = GlyphForButtonTag(button);
    if (DebounceTimes()[index] > FrameClock::GetTime())
        return false;
    switch (button) {
    case 0xBC6B118F: return input->up != 0;                        // up
    case 0xE3006FC4: return input->down != 0;                      // down
    case 0x85981897: return input->left != 0;                      // left
    case 0x4B358AEB: return input->right != 0;                     // right
    case 0xB7231A95: return input->up && input->left;              // upleft
    case 0xA50950C5: return input->up && input->right;             // upright
    case 0xD8847EFA: return input->down && input->left;            // downleft
    case 0x786B8B68: return input->down && input->right;           // downright
    case 0x7323E97C: return input->x != 0;                         // x
    case 0x20689278: return input->triangle != 0;                  // triangle
    case 0x321C9756: return input->square != 0;                    // square
    case 0x2B489A86: return input->circle != 0;                    // circle
    case 0x26B0C991: return input->l1 != 0;                        // l1
    case 0xBFB9982B: return input->l2 != 0;                        // l2
    case 0xF2F1F64E: return input->r1 != 0;                        // r1
    case 0x6BF8A7F4: return input->r2 != 0;                        // r2
    case 0x767A45D7: return input->black != 0;                     // black
    case 0xBD30325B: return input->white != 0;                     // white
    case 0x9D2D8850: return input->z != 0;                         // z
    default: return false;
    }
}

// 0x0059EDF0: ControllerDebounce <button> [time=seconds (1)]: the button doesn't count as pressed (for
// ControllerPressed) for `time` seconds from now. Always true.
bool __cdecl CFunc_ControllerDebounce(Scripts::CStruct* params, Scripts::CScript*)
{
    float time = 1.0f;
    params->FetchFloat(kTime, &time, 0);
    uint32_t button = 0;
    if (params->ReadHashValue(0u, &button, 0)) {
        int index = GlyphForButtonTag(button);
        // x87: the unsigned clock + time * 1000, truncated through 64 bits
        double until = static_cast<double>(FrameClock::GetTime()) + static_cast<double>(time) * 1000.0;
        DebounceTimes()[index] = static_cast<uint32_t>(static_cast<int64_t>(until));
    }
    return true;
}

// 0x0059F0F0: FakeLights id=<light> <settings>: change one fake light (0x004901D0); or FakeLights
// prefix=<name> [time=n] <settings>: set up fake lights on the nodes named with that prefix (0x0045BF90 lists
// them; with the flag 0x80804991 the name isn't treated as a prefix) - 0x0048FE40. False without id or prefix.
bool __cdecl CFunc_FakeLights(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t id = 0;
    if (params->ReadHashValue(kId, &id, 0)) {
        Engine3D::UpdateSceneLight(id, params);
        return true;
    }
    int time = 0;
    params->FetchIntValue(kTime, &time, 0);
    uint32_t prefix = 0;
    if (!params->HashFromNameOrText(kPrefix, &prefix, 0))
        return false;
    uint32_t count = 0; // a 16-bit count in a zeroed 32-bit slot
    bool isPrefix = !params->HasBareFlag(kFakeLightsExact);
    // (the original passes isPrefix too: 0x0045BF90 takes two arguments and ignores it)
    (void)isPrefix;
    uint32_t* nodes = LevelNodes::CollectNodesByPrefix(prefix, reinterpret_cast<uint16_t*>(&count));
    Engine3D::QueueSceneLights(nodes, static_cast<uint16_t>(count), time, params);
    return true;
}

// 0x0059F1B0: UpdateFxLighting (0x00491F10).
bool __cdecl CFunc_UpdateFxLighting(Scripts::CStruct*, Scripts::CScript*)
{
    Engine3D::Engine::LightAllTextureSplats();
    return true;
}

// 0x0059F1C0: NodeExists <name>: whether the level's node array has a node of that name (0x0045BE70). The
// name is asserted (a compiled-out assert: missing = 0).
bool __cdecl CFunc_NodeExists(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 1);
    return LevelNodes::NodeExists(name);
}

// 0x0059F1F0: LoadNodeArray "<file>": kill the session's spawned scripts, note the name (0x007CE6D8, copied
// unchecked), load the .qb file (0x005A5960) and set up the node array (0x0045C210).
bool __cdecl CFunc_LoadNodeArray(Scripts::CStruct* params, Scripts::CScript*)
{
    MemSys::PushMemProfile("LoadNodeArray"); // "LoadNodeArray"
    Scripts::KillAllChildScripts();
    const char* name = nullptr;
    params->FetchText(0u, &name, 0);
    char* out = NodeTableKeyText(); // (no length limit, like strcpy)
    for (const char* in = name; (*out++ = *in++) != '\0';) {
    }
    Scripts::LoadQBFile(name, 0);
    LevelNodes::GetLastNodeArray();
    MemSys::PopMemProfile();
    return true;
}

// 0x0059F260: ResetCamera: not implemented on PC - for each of the two viewports with a camera (0x004A0950), a
// debug message. Always true.
bool __cdecl CFunc_ResetCamera(Scripts::CStruct*, Scripts::CScript*)
{
    for (int viewport = 0; viewport < 2; ++viewport) {
        if (Engine3D::ObtainActiveLens(viewport))
            DebugOut::Printf("TODO: ScriptResetCamera not implemented\n"); // "TODO: ScriptResetCamera not implemented\n"
    }
    return true;
}

// 0x0059F290: SetMovementVelocity [speed (400)]: the viewer's movement speed (0x00576E80).
bool __cdecl CFunc_SetMovementVelocity(Scripts::CStruct* params, Scripts::CScript*)
{
    float speed = 400.0f;
    params->FetchFloat(0u, &speed, 0);
    GameModules::SetMovementVelocity(speed);
    return true;
}

// 0x0059F2C0: SetRotateVelocity [speed (120)]: the viewer's rotation speed (0x00576E90).
bool __cdecl CFunc_SetRotateVelocity(Scripts::CStruct* params, Scripts::CScript*)
{
    float speed = 120.0f;
    params->FetchFloat(0u, &speed, 0);
    GameModules::SetRotateVelocity(speed);
    return true;
}

// 0x0059F2F0: ClearViewerObject: the viewer (if any) lets go of the object it shows (0x00576D40).
bool __cdecl CFunc_ClearViewerObject(Scripts::CStruct*, Scripts::CScript*)
{
    if (void* viewer = Viewer())
        static_cast<GameModules::Viewer*>(viewer)->DropViewerEntity();
    return true;
}

// 0x0059F310: CasArtist: whether the global integer cas_artist is set.
bool __cdecl CFunc_CasArtist(Scripts::CStruct*, Scripts::CScript*)
{
    return Scripts::FetchIntValue("cas_artist") != 0; // "cas_artist"
}

// 0x0059F330: SpawnScript <name> [params={...}] [id=n] [callback=<script>] [callbackparams={...}]
// [OnlineModeFlag] [permanent] [NotSessionSpecific=n]: spawn the script at the calling script's node. The new
// script remembers the caller's .qb file (so killing by file kills it) unless the flag 0x01917DF2 is given.
// Always true.
bool __cdecl CFunc_SpawnScript(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    Scripts::CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    uint32_t id = 0;
    params->ReadHashValue(kId, &id, 0);
    uint32_t callback = 0;
    params->ReadHashValue(kCallback, &callback, 0);
    Scripts::CStruct* callbackParams = nullptr;
    params->ReadSubStruct(kCallbackParams, &callbackParams, 0);
    bool replicateFlag = params->HasBareFlag(kOnlineFlag);
    bool permanent = params->HasBareFlag(kPermanent);
    int notSessionSpecific = 0;
    params->FetchIntValue(kNotSessionSpecific, &notSessionSpecific, 0);
    Scripts::CScript* spawned = Scripts::SpawnScript(name, scriptParams, callback, callbackParams, script->m_listLink, id,
                                                   replicateFlag, permanent, notSessionSpecific != 0, false);
    // (as in the original, not checked for null - a C function spawns no script)
    if (!params->HasBareFlag(kKeepSpawnerFile))
        spawned->SetSpawnerFile(script);
    return true;
}

// 0x0059F440: UnBlockInterruptedScript: the level the calling script's interrupt interrupted goes on.
bool __cdecl CFunc_UnBlockInterruptedScript(Scripts::CStruct*, Scripts::CScript* script)
{
    script->UnblockInterruptedScript();
    return true;
}

// 0x0059F450: KillSpawnedScript Name=<script> (kill the spawned scripts running it at any level) or
// Id=<id> (by spawn id, unique id or spawner file). Always true.
bool __cdecl CFunc_KillSpawnedScript(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    if (params->ReadHashValue("Name", &name, 0)) { // "Name"
        Scripts::StopScriptsTargeting(name);
        return true;
    }
    uint32_t id = 0;
    if (params->ReadHashValue("Id", &id, 0)) // "Id"
        Scripts::StopScriptsByTag(id);
    return true;
}

// 0x0059F4C0 / 0x0059F4D0: PauseSpawnedScripts / UnPauseSpawnedScripts.
bool __cdecl CFunc_PauseSpawnedScripts(Scripts::CStruct*, Scripts::CScript*)
{
    Scripts::PauseSpawnedScripts(true);
    return true;
}

bool __cdecl CFunc_UnPauseSpawnedScripts(Scripts::CStruct*, Scripts::CScript*)
{
    Scripts::PauseSpawnedScripts(false);
    return true;
}

// 0x0059F4E0: DisplayLoadingScreen ["<image>" ("images\xxxxx")] [freeze] [blank] [seconds]: show the loading
// screen (0x00490890); with a number, the loading bar's time (0x00490770). Always true.
bool __cdecl CFunc_DisplayLoadingScreen(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* image = "images\\xxxxx"; // "images\xxxxx"
    params->FetchText(0u, &image, 0);
    bool blank = params->HasBareFlag(kBlank);
    bool freeze = params->HasBareFlag(kFreeze);
    Engine3D::DisplayLoadingScreen(image, freeze, blank);
    Gfx2D::LoadingConsole::ScreenShown(freeze, blank); // not original: the loading console (gfx/loading_console.h)
    float seconds = 0.0f; // (the original leaves it unset - unused then)
    if (params->FetchFloat(0u, &seconds, 0))
        Engine3D::SetLoadingBarTime(seconds);
    return true;
}

// 0x0059F550: HideLoadingScreen (0x004908C0).
bool __cdecl CFunc_HideLoadingScreen(Scripts::CStruct*, Scripts::CScript*)
{
    Gfx2D::LoadingConsole::ScreenHidden(); // not original: the loading console (gfx/loading_console.h)
    Engine3D::RemoveLoadingScreen();
    return true;
}

// 0x0059F560: LastBroadcastedOmnigonWas <0xAE94C183=checksum>: whether it's the last Omnigon sent.
bool __cdecl CFunc_LastBroadcastedOmnigonWas(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t omnigon = 0; // (the original leaves it unset when missing - an asserted parameter)
    params->ReadHashValue(kOmnigon, &omnigon, 1);
    return LastBroadcastedOmnigon() == omnigon;
}

// 0x0059F590: OmnigonAllowed <0xAE94C183=checksum>: reads the parameter, always true.
bool __cdecl CFunc_OmnigonAllowed(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t omnigon = 0;
    params->ReadHashValue(kOmnigon, &omnigon, 1);
    return true;
}

// 0x0059F5B0: SetLevelName "<name>": the level's name (0x007CE698). The original passes the name to sprintf
// as the format (kept: a '%' in it is interpreted).
bool __cdecl CFunc_SetLevelName(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* name = nullptr;
    params->FetchText(0u, &name, 1);
#pragma warning(suppress : 4996) // (unbounded, and the name is the format: as in the original)
    std::sprintf(LevelName(), name);
    return true;
}

// 0x0059F5E0: DumpScripts [just_stack]: the script list dump is compiled out of this build. Always true.
bool __cdecl CFunc_DumpScripts(Scripts::CStruct* params, Scripts::CScript*)
{
    if (!params->HasBareFlag("just_stack")) // "just_stack"
        DumpScriptsList(params->HasBareFlag(kDumpScriptsFlag));
    return true;
}

} // namespace ScriptCommands

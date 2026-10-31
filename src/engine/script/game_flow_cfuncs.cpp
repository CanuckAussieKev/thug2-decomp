// Script commands (CFunctions) of the skate game, 0x0059FE50-0x005A0880 and 0x005A3B00-0x005A3B75: cleaning up
// between levels, object flags and exceptions by name or node prefix, skater scripts, pausing, the clock, the
// screen flash (see game_cfuncs.h).

#include "engine/script/game_cfuncs.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include <cstdio>

#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_lists.h"
#include "game/skate/career.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "game/park_editing/park_editor.h"
#include "engine/file/packs.h"
#include "frontend/front_end.h"
#include "engine/gfx/tod_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/path_manager.h"
#include "engine/script/cfuncs_scene.h"
#include "engine/script/cscript.h"
#include "engine/script/node_prefixes.h"
#include "engine/script/skate_cfuncs.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "engine/script/script_startup.h"
#include "game/goals/goal_manager.h"
#include "engine/gfx/engine_imposters.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/anim_cache.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/scene_lights.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/objects/anim_movies.h"
#include "engine/script/debug_cfuncs.h"
#include "engine/objects/movie_manager.h"
#include "engine/gfx/engine.h"

namespace ScriptCommands {

uint32_t g_lastBroadcastedOmnigon;

namespace {

constexpr uint32_t kParams = 0x7031F10C;      // "params"
constexpr uint32_t kSkater = 0x5B8AB877;      // "skater"
constexpr uint32_t kName = 0xA1DC81F9;        // "name"
constexpr uint32_t kPrefix = 0x6C4E7971;      // "prefix"
constexpr uint32_t kSkaterType = 0xBAF9B341;  // (name unknown) the skaters' object type
constexpr uint32_t kKeepSpawnerFile = 0x01917DF2; // (name unknown) don't note the spawner's .qb file (as SpawnScript)
constexpr uint32_t kPausedAnswerKey = 0x3BA1FDB9;  // (name unknown) GameIsPaused's answer
constexpr uint32_t kFlagCommandFlagA = 0x2E7D5EE7; // (names unknown) flags the object flag commands look for and
constexpr uint32_t kFlagCommandFlagB = 0xBCEB479A; // ignore
constexpr uint32_t kCleanupScript1 = 0xFF958F9D; // (names unknown) the scripts Cleanup runs
constexpr uint32_t kCleanupScript2 = 0xDCA9A9A0;
constexpr uint32_t kQbSymbol = 0xC472ECC5;    // (name unknown) the global whose value names a .qb file to unload
constexpr uint32_t kFlashTextureFlag = 0x70FFC649; // (names unknown) DoFlash's flags: 1, 2 or else 4, 8
constexpr uint32_t kFlashFlag2 = 0x19C43CF6;
constexpr uint32_t kFlashFlag4 = 0x387C9ED6;
constexpr uint32_t kFlashFlag8 = 0xDF6436D2;

// The skate module (GameModules::Skate, 0x007CE478) and its code we call through the game.
uint8_t* Skate() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); }
GameModules::Skate* SkateModule() { return reinterpret_cast<GameModules::Skate*>(Skate()); }
GameObjects::CRider* GetRiderAt(int index) { return static_cast<GameObjects::CRider*>(SkateModule()->GetRiderAt(index)); }
GameObjects::CRider* GetHomeRider() { return SkateModule()->GetHomeRider(); }

// The object manager (0x006F0FB8).
GameObjects::CEntityHub* Objects() { return GameObjects::g_compositeRegistry; }
// The game mode (0x007CE470) and two of its values (0x00553170 / 0x00553190: its integers 0x8D38A280 / 0x6DC3D6F1).
void* GameMode() { return static_cast<void*>(GoalSystem::g_gameMode); }
GoalSystem::CMatchRules* GameModeObject() { return static_cast<GoalSystem::CMatchRules*>(GameMode()); }
int GameModeTimeLimit() { return GameModeObject()->GetTimeLimit(); }
OnlinePlay::Hub* OnlineHubHandle() { return OnlinePlay::g_onlineHub; }
MemSys::Hub* MemManager() { return MemSys::Hub::Pull(); }

// Two argument-less calls of Cleanup's to the empty function 0x005480A0 (compiled-out debug code; what it did is
// unknown).
inline void CompiledOutDebugCall() {}

// A .qb file's scripts unloaded and the global 0xC472ECC5 cleared (0x005A59B0).
void UnloadQbFile(uint32_t file) { Scripts::UnloadQBFile(file); }
// The level's .qb files to unload with it (0x00696900, 15).
uint32_t* LevelQbFiles() { return g_levelQbFiles; }
constexpr int kLevelQbFiles = 15;

// What a manager walk does to the objects of a name (0x005A01D0's structure; the object's part from +4).
struct FlagCommand {
    uint32_t id;
    uint32_t value;  // a flag's number, a mask, or an exception
    int action;      // FlagAction
    bool result;
    bool found;
};

// The object's part of it (0x005A0120 gets it in ESI, the object in EAX).
struct FlagCommandData {
    uint32_t value;
    int action;
    bool result;
    bool found;
};

} // namespace

// 0x0059FE50: Cleanup [preserve_skaters]: everything of the level unloaded - the skaters' state, the time of day's
// dead objects, the park editor, two scripts (with the calling script's SCRIPT_FLAG_SPAWNED off meanwhile), the
// skate module's objects, the asset tables, the level's .qb files, the geometry, without preserve_skaters the extra
// skater heaps, the unused .pre files, the paths, the network heaps (rebuilt after, as the network says); the
// physical memory logged. Always true.
bool __cdecl CFunc_Cleanup(Scripts::CStruct* params, Scripts::CScript* script)
{
    Stubs::DoNothing(); // 0x0048C1B0
    GoalSystem::CGoalManager* skaterInfo = GoalSystem::GetObjectiveBoard();
    skaterInfo->DisengageEveryObjective(false);
    skaterInfo->WipeObjectives();
    g_todManager->ClearDeadObjects();
    ParkEditing::CParkBuilderMode::Sole()->Leave();
    CompiledOutDebugCall();
    bool preserveSkaters = false;
    if (params)
        preserveSkaters = params->HasBareFlag("preserve_skaters"); // "preserve_skaters"
    uint8_t spawned = 0;
    if (script) {
        spawned = script->m_spawnFlags & Scripts::SCRIPT_FLAG_SPAWNED;
        script->m_spawnFlags &= ~Scripts::SCRIPT_FLAG_SPAWNED;
    }
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    Scripts::LaunchScript(kCleanupScript1, nullptr, nullptr, false, nullptr);
    Scripts::LaunchScript(kCleanupScript2, nullptr, nullptr, false, nullptr);
    SkateModule()->TidyUp(); // the skate module's objects
    if (script)
        script->m_spawnFlags = static_cast<uint8_t>((script->m_spawnFlags & ~Scripts::SCRIPT_FLAG_SPAWNED) | spawned);
    reinterpret_cast<Engine3D::CImposterManager*&>(Engine3D::g_imposterManager)->DeleteAll();
    Engine3D::Engine::KillAllEffects();
    Gfx2D::CClipStore* particles = Gfx2D::CClipStore::Pull(); // (0x006F92B0)
    particles->Drain(false);
    Engine3D::Engine::EnableFog(false);
    Scripts::DeleteFinishedScripts();
    CompiledOutDebugCall();
    Engine3D::UninitLightManager();
    SoundFx::g_audioMixer->SweepUp();
    assets->UnloadAllAssetsOfType(false, 9);
    DebugOut::Printf("unloading assman tables ...........\n"); // "unloading assman tables ...........\n"
    assets->UnloadAllAssets(false);
    DebugOut::Printf("done unloading assman ...........\n"); // "done unloading assman ...........\n"
    if (Scripts::CSymbolTableEntry* qb = Scripts::FindSymbol(kQbSymbol))
        UnloadQbFile(qb->unknown8);
    for (int i = 0; i < kLevelQbFiles; ++i) {
        UnloadQbFile(LevelQbFiles()[i]);
        LevelQbFiles()[i] = 0;
    }
    DebugOut::Printf("Starting to unload Geometry from the engine .....\n"); // "Starting to unload Geometry from the engine .....\n"
    CFunc_UnloadAllLevelGeometry(nullptr, nullptr);
    DebugOut::Printf("Done unloading Geometry from the engine .....\n"); // "Done unloading Geometry from the engine .....\n"
    if (!preserveSkaters) {
        DebugOut::Printf("Clean up without preserve skaters\n"); // "Clean up without preserve skaters\n"
        MemManager()->DeleteExtraSkaterHeaps();
    }
    // the .pre files nothing uses any more
    for (const char* pre = Packs::TakeNextFinishedArchive(nullptr); pre;) {
        const char* next = Packs::TakeNextFinishedArchive(pre);
        if (!Packs::ArchiveIsBusy(pre))
            Packs::UnloadPre(pre);
        pre = next;
    }
    Engine3D::PurgeDoomedCameras();
    Engine3D::ClearSceneLightQueue();
    Engine3D::DropParticleTextures(false);
    GameObjects::CPathManager::Sole()->Dealloc();
    if (!SkateModule()->InNetGameLevel())
        MemManager()->ReleaseNetScratchZone();
    GameObjects::DestroyCoimHeap();
    MEMORYSTATUS memory;
    GlobalMemoryStatus(&memory);
    char text[0x400];
    int length = wsprintfA(text, "%4d total kb of physical memory.\n", memory.dwTotalPhys >> 10); // "%4d total kb..."
    wsprintfA(text + length, "%4d  free kb of physical memory.\n", memory.dwAvailPhys >> 10);    // "%4d  free kb..."
    OutputDebugStringA(text);
    if (SkateModule()->InNetGameLevel())
        MemManager()->CreateNetScratchZone();
    if (SkateModule()->InFrontEndOrNetMode())
        MemManager()->InitInternetHeaps();
    return true;
}

namespace {

// 0x005A0120: what FlagCommand asks, done to `object`: 0 sets flag `value` (of the 32 script flags), 1 clears it,
// 2 tests it, 3 / 4 set / clear the bits `value` of the object flags, 5 sends the object the exception `value`.
// The result is true for 0, 1, 5 and a set flag (2); 3 and 4 leave it.
void ApplyFlagCommand(FlagCommandData* data, GameObjects::CEntity* object)
{
    data->found = true;
    switch (data->action) {
    case 0:
        object->m_flagBits |= 1u << (data->value & 31);
        data->result = true;
        break;
    case 1:
        object->m_flagBits &= ~(1u << (data->value & 31));
        data->result = true;
        break;
    case 2:
        if (object->m_flagBits & (1u << (data->value & 31)))
            data->result = true;
        break;
    case 3:
        object->m_objectFlags |= data->value;
        break;
    case 4:
        object->m_objectFlags &= ~data->value;
        break;
    case 5:
        object->PostOwnEvent(data->value, nullptr);
        data->result = true;
        break;
    default:
        break;
    }
}

} // namespace

// 0x005A01B0
void __cdecl ApplyFlagCommandToObject(GameObjects::CEntity* object, void* data)
{
    auto* command = static_cast<FlagCommand*>(data);
    if (object->m_ident == command->id)
        ApplyFlagCommand(reinterpret_cast<FlagCommandData*>(&command->value), object);
}

void __cdecl ApplyFlagCommandRegisters(void* data, GameObjects::CEntity* object)
{
    ApplyFlagCommand(static_cast<FlagCommandData*>(data), object);
}

__declspec(naked) void ApplyFlagCommandStub()
{
    __asm {
        push eax
        push esi
        call ApplyFlagCommandRegisters
        add esp, 8
        ret
    }
}

// 0x005A01D0
bool __cdecl FlagCommandByName(Scripts::CStruct* params, Scripts::CScript*, int action)
{
    uint32_t value = 0;
    if (action == kFlagActionException)
        params->ReadHashValue(0u, &value, 0);
    else
        params->FetchIntValue(0u, reinterpret_cast<int*>(&value), 0);
    // (two flags looked for and not used)
    if (!params->HasBareFlag(kFlagCommandFlagA))
        params->HasBareFlag(kFlagCommandFlagB);
    FlagCommand command;
    uint32_t id;
    if (params->ReadHashValue(kName, &id, 0)) {
        command.value = value;
        command.id = id;
        command.action = action;
        command.found = false;
        command.result = false;
        Objects()->ForEach(&ApplyFlagCommandToObject, &command);
        return command.result;
    }
    const char* prefix;
    if (!params->FetchText(kPrefix, &prefix, 0))
        return false;
    uint16_t count = 0;
    uint32_t* nodes = LevelNodes::CollectNodesByPrefix(prefix, &count); // (the original passes a third argument, 1)
    command.value = value;
    command.action = action;
    command.found = false;
    command.result = false;
    for (int i = 0; i < count; ++i) {
        command.id = nodes[i];
        Objects()->ForEach(&ApplyFlagCommandToObject, &command);
    }
    return command.result;
}

// 0x005A0300: MakeSkaterGoto <script> [params={...}] [skater=n]: the skater switches to the script - the calling
// script's own skater if it runs on one, else skater n, else the local skater; only a local skater (+0xBC).
bool __cdecl CFunc_MakeSkaterGoto(Scripts::CStruct* params, Scripts::CScript* script)
{
    Scripts::CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    GameObjects::CEntity* object = script ? script->m_entity.Pull() : nullptr;
    GameObjects::CRider* skater;
    if (object && object->m_kindTag == kSkaterType) {
        skater = static_cast<GameObjects::CRider*>(object);
    } else {
        int index = 0;
        if (params->FetchIntValue(kSkater, &index, 0))
            skater = GetRiderAt(index);
        else
            skater = GetHomeRider();
    }
    // Not original - THAW's BMX (game/rider/thaw_bmx.h SkaterScript): on the bike THAW's PedKnockDown takes its bike
    // branch. Original behaviour: the skater goes to `name` as given.
    if (skater && skater->m_unknownBC)
        name = Skater::ThawBmx::SkaterScript(skater, name, script);
    if (skater && skater->m_unknownBC && name)
        skater->SwitchToScript(name, scriptParams);
    return true;
}

// 0x005A03C0: MakeSkaterGosub <script> [params={...}] [skater=n (0)]: the skater (the calling script's own, else
// skater n) runs the script as a call.
bool __cdecl CFunc_MakeSkaterGosub(Scripts::CStruct* params, Scripts::CScript* script)
{
    Scripts::CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    GameObjects::CEntity* object = script ? script->m_entity.Pull() : nullptr;
    if (!object || object->m_kindTag != kSkaterType) {
        int index = 0;
        params->FetchIntValue(kSkater, &index, 0);
        object = GetRiderAt(index);
    }
    if (object)
        object->RunSubScript(name, scriptParams);
    return true;
}

// 0x005A0460: SpawnSound <script> [Id=id] [Params={...}]: the script spawned on the calling script's object (its
// id the calling script's spawn id unless given), remembering the caller's .qb file unless 0x01917DF2 is given. A
// script without an object: SpawnScript.
bool __cdecl CFunc_SpawnSound(Scripts::CStruct* params, Scripts::CScript* script)
{
    GameObjects::CEntity* object = script->m_entity.Pull();
    if (!object)
        return CFunc_SpawnScript(params, script);
    uint32_t name = 0;
    if (params->ReadHashValue(0u, &name, 0)) {
        uint32_t id = Scripts::GetSpawnId(script);
        params->ReadHashValue("Id", &id, 0); // "Id"
        Scripts::CStruct* scriptParams = nullptr;
        params->ReadSubStruct("Params", &scriptParams, 0); // "Params"
        Scripts::CScript* spawned = script->m_entity.Pull()->SpawnScript(name, scriptParams, id, false);
        if (!params->HasBareFlag(kKeepSpawnerFile))
            spawned->SetSpawnerFile(script);
    }
    return true;
}

// 0x005A0520: PauseSkater <n> [hide]: skater n paused (vtable +0x2C) and hidden or shown.
bool __cdecl CFunc_PauseSkater(Scripts::CStruct* params, Scripts::CScript*)
{
    bool hide = params->HasBareFlag("hide"); // "hide"
    int index = 0; // (unset in the original when missing - an asserted parameter)
    params->FetchIntValue(0u, &index, true);
    if (GameObjects::CRider* skater = GetRiderAt(index)) {
        (*reinterpret_cast<void(__thiscall***)(GameObjects::CRider*)>(skater))[0x2C / 4](skater);
        skater->Conceal(hide);
    }
    return true;
}

// 0x005A0580: UnPauseSkater <n>: skater n unpaused (vtable +0x30) and shown.
bool __cdecl CFunc_UnPauseSkater(Scripts::CStruct* params, Scripts::CScript*)
{
    int index = 0; // (unset in the original when missing - an asserted parameter)
    params->FetchIntValue(0u, &index, true);
    if (GameObjects::CRider* skater = GetRiderAt(index)) {
        (*reinterpret_cast<void(__thiscall***)(GameObjects::CRider*)>(skater))[0x30 / 4](skater);
        skater->Conceal(false);
    }
    return true;
}

// 0x005A05C0: PauseGame: the game paused, the calling script carrying on.
bool __cdecl CFunc_PauseGame(Scripts::CStruct*, Scripts::CScript* script)
{
    GameModules::FrontEndInstance()->PauseGame(true);
    Scripts::ResumeChildScript(script);
    return true;
}

// 0x005A05E0: UnPauseGame.
bool __cdecl CFunc_UnPauseGame(Scripts::CStruct*, Scripts::CScript*)
{
    GameModules::FrontEndInstance()->PauseGame(false);
    return true;
}

// 0x005A05F0: GameIsPaused: whether the game is paused (0x006FA7D4) - then the flag 0x3BA1FDB9 is added to the
// command's own parameters.
bool __cdecl CFunc_GameIsPaused(Scripts::CStruct* params, Scripts::CScript*)
{
    if (!GameObjects::g_simulationHalted)
        return false;
    params->PutNameKey(kPausedAnswerKey, kPausedAnswerKey);
    return true;
}

// 0x005A0620 / 0x005A0630: PauseObjects / UnPauseObjects.
bool __cdecl CFunc_PauseObjects(Scripts::CStruct*, Scripts::CScript*)
{
    Objects()->Freeze(true);
    return true;
}

bool __cdecl CFunc_UnPauseObjects(Scripts::CStruct*, Scripts::CScript*)
{
    Objects()->Freeze(false);
    return true;
}

// 0x005A0640: ResetClock: on the server, the skate module's clock (+0x370) set to the game mode's time limit.
bool __cdecl CFunc_ResetClock(Scripts::CStruct*, Scripts::CScript*)
{
    if (OnlineHubHandle()->OnServer())
        SkateModule()->SetUnknown370(GameModeTimeLimit());
    return true;
}

// 0x005A0670: PadsPluggedIn.
bool __cdecl CFunc_PadsPluggedIn(Scripts::CStruct*, Scripts::CScript*) { return GameModules::FrontEndInstance()->PadsPluggedIn(); }

// 0x005A0680: DoFlash duration=<seconds> [texture="<name>"] [z=<depth>] [skater=n (the calling object's viewport)]
// [start_r ... start_a] [end_r ... end_a] [flags 0x70FFC649 (1), 0x19C43CF6 (2) or else 0x387C9ED6 (4),
// 0xDF6436D2 (8)]: a screen flash (0x004923E0) from the start colour to the end one. Nothing unless duration > 0.
// The eight colour values share one integer (as in the original): a missing one repeats the one before.
bool __cdecl CFunc_DoFlash(Scripts::CStruct* params, Scripts::CScript* script)
{
    float duration = 0.0f;
    float z = 0.0f;
    int viewport = -1;
    const char* texture = nullptr;
    params->FetchFloat("duration", &duration, 0); // "duration"
    if (!(duration > GameObjects::g_floatZero)) // (0.0)
        return true;
    params->FetchText("texture", &texture, 0); // "texture"
    params->FetchFloat("z", &z, 0);        // "z"
    int flags = 0;
    if (params->HasBareFlag(kFlashTextureFlag))
        flags = 1;
    if (params->HasBareFlag(kFlashFlag2))
        flags |= 2;
    else if (params->HasBareFlag(kFlashFlag4))
        flags |= 4;
    params->FetchIntValue(kSkater, &viewport, 0);
    if (viewport == -1)
        viewport = static_cast<GameObjects::CRider*>(script->m_entity.Pull())->m_viewSlot;
    if (params->HasBareFlag(kFlashFlag8))
        flags |= 8;
    uint8_t start[4];
    uint8_t end[4];
    int value = 0;
    static const char* const kStart[4] = {"start_r", "start_g", "start_b", "start_a"}; // "start_r" ... "start_a"
    static const char* const kEnd[4] = {"end_r", "end_g", "end_b", "end_a"};   // "end_r" ... "end_a"
    for (int i = 0; i < 4; ++i) {
        params->FetchIntValue(kStart[i], &value, 0);
        start[i] = static_cast<uint8_t>(value);
    }
    for (int i = 0; i < 4; ++i) {
        params->FetchIntValue(kEnd[i], &value, 0);
        end[i] = static_cast<uint8_t>(value);
    }
    uint32_t startColour;
    uint32_t endColour;
    std::memcpy(&startColour, start, 4);
    std::memcpy(&endColour, end, 4);
    Engine3D::Engine::AddFlash(viewport, startColour, endColour, duration, z, static_cast<uint32_t>(flags), texture);
    return true;
}

// 0x005A3B00 / 0x005A3B20 / 0x005A3B40 / 0x005A3B60: ClearFlag / SendFlag / QueryFlag <flag> name=<id> or
// prefix="<nodes>"; SendException <exception> name= or prefix=: FlagCommandByName with 1 (clear), 0 (set), 2 (test),
// 5 (exception). Whether an object did it (QueryFlag: whether one has the flag).
bool __cdecl CFunc_ClearFlag(Scripts::CStruct* params, Scripts::CScript* script)
{
    return FlagCommandByName(params, script, kFlagActionClear);
}

bool __cdecl CFunc_SendFlag(Scripts::CStruct* params, Scripts::CScript* script)
{
    return FlagCommandByName(params, script, kFlagActionSet);
}

bool __cdecl CFunc_QueryFlag(Scripts::CStruct* params, Scripts::CScript* script)
{
    return FlagCommandByName(params, script, kFlagActionTest);
}

bool __cdecl CFunc_SendException(Scripts::CStruct* params, Scripts::CScript* script)
{
    return FlagCommandByName(params, script, kFlagActionException);
}

// 0x005A11C0: RoundIncrement: the game mode's round (0x005536D0).
bool __cdecl CFunc_RoundIncrement(Scripts::CStruct*, Scripts::CScript*)
{
    GameModeObject()->IncrementRound();
    return true;
}

// 0x005A11D0: GameModeEquals <mode> ...: whether one of the bare names is the game mode's (0x00553220: its own
// integer of that name is set).
bool __cdecl CFunc_GameModeEquals(Scripts::CStruct* params, Scripts::CScript*)
{
    for (Scripts::CComponent* component = params->StepToEntry(nullptr); component;
         component = params->StepToEntry(component)) {
        if (component->name == 0 && component->KindTag() == Scripts::QBVAL_NAME &&
            GameModeObject()->IsTrue(component->checksum))
            return true;
    }
    return false;
}

// 0x005A1230: GetCurrentLevel: the skate module's level (+0x374, as 0x651533EC - a name) and level structure name
// (+0x380, 0x16A0B364) in the script's parameters.
bool __cdecl CFunc_GetCurrentLevel(Scripts::CStruct*, Scripts::CScript* script)
{
    uint8_t* skate = Skate();
    script->m_args->AddComponentOfType(0x651533EC, Scripts::QBVAL_NAME, *reinterpret_cast<uint32_t*>(skate + 0x374));
    script->m_args->PutNameKey(0x16A0B364, *reinterpret_cast<uint32_t*>(skate + 0x380));
    return true;
}

// 0x005A1270: SetLevelStructureName name=<structure>: the skate module's +0x380.
bool __cdecl CFunc_SetLevelStructureName(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (unset in the original when missing - an asserted parameter)
    params->ReadHashValue(kName, &name, 1);
    *reinterpret_cast<uint32_t*>(Skate() + 0x380) = name;
    return true;
}

// 0x005A12A0: GetLevelStructureName: the skate module's +0x380 as the script's 0x63B1214E.
bool __cdecl CFunc_GetLevelStructureName(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(0x63B1214E, *reinterpret_cast<uint32_t*>(Skate() + 0x380));
    return true;
}

// 0x005A12C0: RestartLevel (the skate module's 0x005668A0).
bool __cdecl CFunc_RestartLevel(Scripts::CStruct*, Scripts::CScript*)
{
    SkateModule()->RestartLevel();
    return true;
}

// 0x005A12E0: GameFlow <script>: the skate module's game flow (+0x388) runs it (0x00552E30).
bool __cdecl CFunc_GameFlow(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    (*reinterpret_cast<GoalSystem::CMatchFlow**>(Skate() + 0x388))->Request(name);
    return true;
}

namespace {

// The globals naming the career's Omnigon flags ClearOmnigons clears (in this order).
constexpr uint32_t kOmnigonFlags[] = {
    0x13D5B5DB, 0x8ADCE461, 0xFDDBD4F7, 0x63BF4154, 0x14B871C2, 0x8DB12078, 0xFAB610EE, 0x6A090D7F,
    0x1D0E3DE9, 0x37CBEE45, 0x40CCDED3, 0xD9C58F69, 0xAEC2BFFF, 0x30A62A5C, 0x47A11ACA, 0xDEA84B70,
    0xA9AF7BE6, 0x39106677, 0x4E1756E1, 0x1CE6BD86, 0x6BE18D10, 0xF2E8DCAA, 0x85EFEC3C,
};

constexpr uint32_t kOmnigon = 0xAE94C183;       // (name unknown) the Omnigon (as LastBroadcastedOmnigonWas)
constexpr uint32_t kOmnigonFlag = 0xF649D637;   // (name unknown) sent with it
constexpr uint32_t kCheatText = 0x61414D56;     // (name unknown) TryCheatString's text
constexpr uint32_t kCheatFailed = 0x323D63FF;   // (names unknown) its scripts: no cheat,
constexpr uint32_t kCheatFound = 0xB95B735C;    // a cheat (before the cheat's own script)
constexpr uint32_t kCheatList = 0x75A23DA4;     // (name unknown) the global array of {c1 c2 cheatscript}

uint32_t& LastBroadcastedOmnigon() { return g_lastBroadcastedOmnigon; }

// A skater's score (0x00580480; null if none) and its display brought up to date (0x005626E0).
void UpdateScoreDisplay(GameObjects::CRider* skater)
{
    if (void* score = skater->GetPointsTotal())
        static_cast<GameModules::PointTally*>(score)->RefreshBalanceMeter();
}

} // namespace

// 0x005A1320: ClearOmnigons: the career's 23 Omnigon flags (the values of the globals above) cleared.
bool __cdecl CFunc_ClearOmnigons(Scripts::CStruct*, Scripts::CScript*)
{
    auto* career = *reinterpret_cast<GameObjects::CRiderStoryProgress**>(Skate() + 0x20);
    for (uint32_t flag : kOmnigonFlags)
        career->UnSetGlobalFlag(Scripts::FetchIntValue(flag));
    return true;
}

// 0x005A1540: BroadcastOmnigon <0xAE94C183=omnigon> [0xF649D637]: noted as the last one sent and, from the server,
// sent to every remote connection (message 0x7F, 8 bytes: the Omnigon and the flag; sequenced, group 8).
bool __cdecl CFunc_BroadcastOmnigon(Scripts::CStruct* params, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = OnlineHubHandle();
    NetLink::ConnSearch search = {NetLink::kConnSearchVtable, nullptr};
    uint32_t omnigon = 0; // (unset in the original when missing - an asserted parameter)
    params->ReadHashValue(kOmnigon, &omnigon, true);
    bool flag = params->HasEntryNamed(kOmnigonFlag);
    LastBroadcastedOmnigon() = omnigon;
    NetLink::AppHost* server = gameNet->m_host;
    for (NetLink::Conn* conn = server->BeginConnWalk(&search); conn; conn = server->StepConnWalk(&search)) {
        if (!conn->IsOffMachine())
            continue;
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        struct {
            uint32_t omnigon;
            bool flag;
            uint8_t pad[3]; // (unset in the original: sent as they are; zero here)
        } message = {omnigon, flag, {}};
        desc.m_content = &message;
        desc.m_ident = 0x7F;
        desc.m_byteCount = 8;
        desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
        desc.m_groupId = 8;
        server->PutOnOutbox(conn->m_hdl, &desc);
        LastBroadcastedOmnigon() = omnigon;
    }
    LastBroadcastedOmnigon() = omnigon;
    return true;
}

// 0x005A1720: SetScreenMode <...>: the screen mode (0x004A0C20 with the command's parameters), then every skater's
// score display brought up to date.
bool __cdecl CFunc_SetScreenMode(Scripts::CStruct* params, Scripts::CScript* script)
{
    Engine3D::CFunc_SetScreenMode(params, script);
    for (uint32_t i = 0; i < 8; ++i) {
        if (GameObjects::CRider* skater = GetRiderAt(static_cast<int>(i)))
            UpdateScoreDisplay(skater);
    }
    return true;
}

// 0x005A1770: SetScreenModeFromGameMode: a debug message per local skater; the game mode's screen mode
// (0x00553630) set (0x004A0AD0) unless the two split modes (1 and 2) would only swap; the local skaters' score
// displays brought up to date.
bool __cdecl CFunc_SetScreenModeFromGameMode(Scripts::CStruct*, Scripts::CScript*)
{
    for (uint32_t i = 0; i < 8; ++i) {
        GameObjects::CRider* skater = GetRiderAt(static_cast<int>(i));
        if (skater && skater->m_unknownBC)
            DebugOut::Printf("cfuncs %d: SUTUBBBEDDDDDDDDDDDDDDDDDDDDDDDD\n", 0x16C0); // "cfuncs %d: SUTUBBBED..."
    }
    int mode = GameModeObject()->GetViewLayout();
    int current = Engine3D::GetViewLayout();
    if (!((mode == 1 && current == 2) || (mode == 2 && current == 1)))
        Engine3D::SetScreenMode(mode);
    for (uint32_t i = 0; i < 8; ++i) {
        GameObjects::CRider* skater = GetRiderAt(static_cast<int>(i));
        if (skater && skater->m_unknownBC)
            UpdateScoreDisplay(skater);
    }
    return true;
}

// 0x005A1840: TryCheatString text="<code>": a cheat code is two numbers made from the text: its checksum, then
// 100000 times the checksum of a buffer holding (round + the last checksum) as decimal text, its terminator
// overwritten with 'X' (so the bytes left behind by the longer strings before - the buffer starts as
// "12345678901234567890", then holds the text - follow it up to the next terminator), each character of it added
// times 0x3FF to a sum. c1 = the checksum of the text's last two thirds ^ the sum, c2 = the buffer's last checksum ^
// the sum (logged). The cheats in the global array 0x75A23DA4 with those c1 / c2 run 0xB95B735C and their
// cheatscript; none (or a text under 6 characters): 0x323D63FF. Always false. The text is used as a sprintf format
// (kept: a '%' in it is interpreted).
bool __cdecl CFunc_TryCheatString(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* text = nullptr; // (unset in the original when missing - an asserted parameter)
    params->FetchText(kCheatText, &text, true);
    if (std::strlen(text) < 6) {
        Scripts::LaunchScript(kCheatFailed, nullptr, nullptr, false, nullptr);
        return false;
    }
    char buffer[0x80];
#pragma warning(push)
#pragma warning(disable : 4996) // (unbounded, and the text is the format: as in the original)
    std::sprintf(buffer, "12345678901234567890"); // "12345678901234567890"
    std::sprintf(buffer, text);
    uint32_t checksum = Checksums::HashFromText(text);
    int sum = 0;
    for (int round = 0; round < 100000; ++round) {
        std::sprintf(buffer, "%d", static_cast<int>(round + checksum));
        buffer[std::strlen(buffer)] = 'X';
        for (const char* c = buffer; *c; ++c)
            sum += static_cast<signed char>(*c) * 0x3FF;
        checksum = Checksums::HashFromText(buffer);
    }
#pragma warning(pop)
    uint32_t c1 = Checksums::HashFromText(text + std::strlen(text) / 3) ^ static_cast<uint32_t>(sum);
    uint32_t c2 = Checksums::HashFromText(buffer) ^ static_cast<uint32_t>(sum);
    DebugOut::Printf("\n{c1=%d c2=%d CheatScript=cheat_xxx },  ; %s\n\n", c1, c2, text); // "{c1=%d c2=%d CheatScript=cheat_xxx }..."
    Scripts::CArray* cheats = Scripts::FetchList(kCheatList);
    int count = static_cast<int>(cheats->m_extent);
    bool found = false;
    for (int i = 0; i < count; ++i) {
        auto* cheat = reinterpret_cast<Scripts::CStruct*>(cheats->FetchPart(static_cast<uint32_t>(i)));
        int cheatC1 = 0;
        int cheatC2 = 0;
        uint32_t script = 0;
        cheat->FetchIntValue("c1", &cheatC1, true);  // "c1"
        cheat->FetchIntValue("c2", &cheatC2, true);  // "c2"
        cheat->ReadHashValue("cheatscript", &script, true);  // "cheatscript"
        if (static_cast<uint32_t>(cheatC1) == c1 && static_cast<uint32_t>(cheatC2) == c2) {
            Scripts::LaunchScript(kCheatFound, nullptr, nullptr, false, nullptr);
            Scripts::LaunchScript(script, nullptr, nullptr, false, nullptr);
            found = true;
        }
    }
    if (!found)
        Scripts::LaunchScript(kCheatFailed, nullptr, nullptr, false, nullptr);
    return false;
}

// 0x005A1A90: LevelIs <level>: whether it's the skate module's level (+0x378).
bool __cdecl CFunc_LevelIs(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t level = 0; // (unset in the original when missing - an asserted parameter)
    params->ReadHashValue(0u, &level, true);
    return *reinterpret_cast<uint32_t*>(Skate() + 0x378) == level;
}

// 0x005A1F80: SetStatOverride [value (0)]: the skate module's forced stat value (+0x3D0).
bool __cdecl CFunc_SetStatOverride(Scripts::CStruct* params, Scripts::CScript*)
{
    float value = 0.0f;
    params->FetchFloat(0u, &value, 0);
    *reinterpret_cast<float*>(Skate() + 0x3D0) = value;
    return true;
}

// 0x005A1FB0: ToggleRails: the skate module's rail display (+0x3D4) on / off.
bool __cdecl CFunc_ToggleRails(Scripts::CStruct*, Scripts::CScript*)
{
    bool& rails = *reinterpret_cast<bool*>(Skate() + 0x3D4);
    rails = !rails;
    return true;
}

// 0x005A1FD0: GetTimeLimit: the game mode's time limit as the script's time_limit.
bool __cdecl CFunc_GetTimeLimit(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutInt(0xC51BF6E3, GameModeTimeLimit()); // "time_limit"
    return true;
}

// 0x005A1FF0 / 0x005A2010: GetGoalsMode / GetTeamGraffitiMode: the same game mode value (0x005532D0) as the script's
// 0x38DBE1D0 / 0x6835B854.
bool __cdecl CFunc_GetGoalsMode(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(0x38DBE1D0, GameModeObject()->GetGoalsMode());
    return true;
}

bool __cdecl CFunc_GetTeamGraffitiMode(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(0x6835B854, GameModeObject()->GetGoalsMode());
    return true;
}

// 0x005A2030: GetGameMode: the game mode's name (0x005532A0) as the script's 0x3E04B26B.
bool __cdecl CFunc_GetGameMode(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(0x3E04B26B, GameModeObject()->GetNameKey());
    return true;
}

// 0x005A2090: SetTesterScript <script> [params={...}] (the skate module's 0x00565ED0).
bool __cdecl CFunc_SetTesterScript(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    Scripts::CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    SkateModule()->LaunchScript(name, scriptParams);
    return true;
}

// 0x005A20F0: KillTesterScript (the skate module's 0x00565F80: its answer).
bool __cdecl CFunc_KillTesterScript(Scripts::CStruct*, Scripts::CScript*) { return SkateModule()->KillScript(); }

// 0x005A21A0: AutoRail <...>: the skate module's rail object (+0x24) is given the parameters - a shared stub that
// ignores them.
bool __cdecl CFunc_AutoRail(Scripts::CStruct* params, Scripts::CScript*)
{
    (void)*reinterpret_cast<void**>(Skate() + 0x24); // (the stub's `this`)
    Stubs::IgnoreArgument(static_cast<int>(reinterpret_cast<intptr_t>(params)));
    return true;
}

// 0x005A4170: SpawnSkaterScript <script> [params={...}] [Id=id] [Callback=<script>] [CallbackParams={...}]: the
// script spawned at the calling script's node, running on the local skater.
bool __cdecl CFunc_SpawnSkaterScript(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    Scripts::CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    uint32_t id = 0;
    params->ReadHashValue("Id", &id, 0); // "Id"
    uint32_t callback = 0;
    params->ReadHashValue("Callback", &callback, 0); // "Callback"
    Scripts::CStruct* callbackParams = nullptr;
    params->ReadSubStruct("CallbackParams", &callbackParams, 0); // "CallbackParams"
    Scripts::CScript* spawned = Scripts::SpawnScript(name, scriptParams, callback, callbackParams, script->m_listLink, id,
                                                   false, false, false, false);
    spawned->m_entity.AssignValue(GetHomeRider()); // (not checked for null, as in the original)
    return true;
}

} // namespace ScriptCommands

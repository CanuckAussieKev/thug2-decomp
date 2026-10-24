// Script commands (CFunctions) of the skate game, 0x0059FE50-0x005A0880 and 0x005A3B00-0x005A3B75: cleaning up
// between levels, object flags and exceptions by name or node prefix, skater scripts, pausing, the clock, the
// screen flash (see game_cfuncs.h).

#include "game/script/game_cfuncs.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include <cstdio>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/net_lists.h"
#include "game/skate/career.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/ed/park_editor.h"
#include "game/file/pip.h"
#include "game/front/front_end.h"
#include "game/gfx/tod_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/path_manager.h"
#include "game/script/cfuncs_scene.h"
#include "game/script/cscript.h"
#include "game/script/node_prefixes.h"
#include "game/script/skate_cfuncs.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

constexpr uint32_t kParams = 0x7031F10C;      // "params"
constexpr uint32_t kSkater = 0x5B8AB877;      // "skater"
constexpr uint32_t kName = 0xA1DC81F9;        // "name"
constexpr uint32_t kPrefix = 0x6C4E7971;      // "prefix"
constexpr uint32_t kSkaterType = 0xBAF9B341;  // (name unknown) the skaters' object type
constexpr uint32_t kKeepSpawnerFile = 0x01917DF2; // (name unknown) don't note the spawner's .qb file (as SpawnScript)
constexpr uint32_t kGamePaused = 0x3BA1FDB9;  // (name unknown) GameIsPaused's answer
constexpr uint32_t kFlagCommandFlagA = 0x2E7D5EE7; // (names unknown) flags the object flag commands look for and
constexpr uint32_t kFlagCommandFlagB = 0xBCEB479A; // ignore
constexpr uint32_t kCleanupScript1 = 0xFF958F9D; // (names unknown) the scripts Cleanup runs
constexpr uint32_t kCleanupScript2 = 0xDCA9A9A0;
constexpr uint32_t kQbSymbol = 0xC472ECC5;    // (name unknown) the global whose value names a .qb file to unload
constexpr uint32_t kFlashTextureFlag = 0x70FFC649; // (names unknown) DoFlash's flags: 1, 2 or else 4, 8
constexpr uint32_t kFlashFlag2 = 0x19C43CF6;
constexpr uint32_t kFlashFlag4 = 0x387C9ED6;
constexpr uint32_t kFlashFlag8 = 0xDF6436D2;

// The skate module (Mdl::Skate, 0x007CE478) and its code we call through the game.
uint8_t* Skate() { return GameVariable<uint8_t*>(0x007CE478); }
Obj::CSkater* GetSkater(int index)
{
    return GameFunction<Obj::CSkater*(__thiscall*)(uint8_t*, int)>(0x005665C0)(Skate(), index);
}
Obj::CSkater* GetLocalSkater() { return GameFunction<Obj::CSkater*(__thiscall*)(uint8_t*)>(0x00566520)(Skate()); }
template <typename R = void, typename... A>
R CallSkate(uintptr_t address, A... args)
{
    return GameFunction<R(__thiscall*)(uint8_t*, A...)>(address)(Skate(), args...);
}

// The object manager (0x006F0FB8).
Obj::CCompositeObjectManager* Objects() { return GameVariable<Obj::CCompositeObjectManager*>(0x006F0FB8); }
// The game mode (0x007CE470) and two of its values (0x00553170 / 0x00553190: its integers 0x8D38A280 / 0x6DC3D6F1).
void* GameMode() { return GameVariable<void*>(0x007CE470); }
int GameModeTimeLimit() { return GameFunction<int(__thiscall*)(void*)>(0x00553170)(GameMode()); }
GameNet::Manager* GameNetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
Mem::Manager* MemManager() { return GameVariable<Mem::Manager*>(0x007D1800); }

// Two argument-less calls of Cleanup's to the empty function 0x005480A0 (compiled-out debug code; what it did is
// unknown).
inline void CompiledOutDebugCall() {}

// A .qb file's scripts unloaded and the global 0xC472ECC5 cleared (0x005A59B0).
void UnloadQbFile(uint32_t file) { GameFunction<void(__cdecl*)(uint32_t)>(0x005A59B0)(file); }
// The level's .qb files to unload with it (0x00696900, 15).
uint32_t* LevelQbFiles() { return &GameVariable<uint32_t>(0x00696900); }
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
bool __cdecl CFunc_Cleanup(Script::CStruct* params, Script::CScript* script)
{
    Stubs::DoNothing(); // 0x0048C1B0
    void* skaterInfo = GameFunction<void*(__cdecl*)()>(0x005591B0)();
    GameFunction<void(__thiscall*)(void*, int)>(0x00558250)(skaterInfo, 0);
    GameFunction<void(__thiscall*)(void*)>(0x0055BEF0)(skaterInfo);
    GameVariable<Gfx::CTODManager*>(0x007CE490)->ClearDeadObjects();
    Ed::CParkEditor::Instance()->Leave();
    CompiledOutDebugCall();
    bool preserveSkaters = false;
    if (params)
        preserveSkaters = params->ContainsFlag(reinterpret_cast<const char*>(0x0064FC90)); // "preserve_skaters"
    uint8_t spawned = 0;
    if (script) {
        spawned = script->m_spawnFlags & Script::SCRIPT_FLAG_SPAWNED;
        script->m_spawnFlags &= ~Script::SCRIPT_FLAG_SPAWNED;
    }
    Ass::CAssMan* assets = Ass::AssetManager();
    Script::RunScript(kCleanupScript1, nullptr, nullptr, false, nullptr);
    Script::RunScript(kCleanupScript2, nullptr, nullptr, false, nullptr);
    CallSkate(0x005666A0); // the skate module's objects
    if (script)
        script->m_spawnFlags = static_cast<uint8_t>((script->m_spawnFlags & ~Script::SCRIPT_FLAG_SPAWNED) | spawned);
    GameFunction<void(__thiscall*)(void*)>(0x0048E260)(GameVariable<void*>(0x006FA7BC));
    GameFunction<void(__cdecl*)()>(0x00492340)();
    void* particles = GameFunction<void*(__cdecl*)()>(0x00480660)(); // (0x006F92B0)
    GameFunction<void(__thiscall*)(void*, int)>(0x00480DB0)(particles, 0);
    GameFunction<void(__cdecl*)(bool)>(0x00490DE0)(false);
    Script::DeleteFinishedScripts();
    CompiledOutDebugCall();
    GameFunction<void(__cdecl*)()>(0x0048FB60)();
    GameVariable<Sfx::CSfxManager*>(0x006F85A8)->CleanUp();
    assets->UnloadAllAssetsOfType(false, 9);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FC68)); // "unloading assman tables ...........\n"
    assets->UnloadAllAssets(false);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FC44)); // "done unloading assman ...........\n"
    if (Script::CSymbolTableEntry* qb = Script::FindSymbol(kQbSymbol))
        UnloadQbFile(qb->unknown8);
    for (int i = 0; i < kLevelQbFiles; ++i) {
        UnloadQbFile(LevelQbFiles()[i]);
        LevelQbFiles()[i] = 0;
    }
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FC10)); // "Starting to unload Geometry from the engine .....\n"
    CFunc_UnloadAllLevelGeometry(nullptr, nullptr);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FBE0)); // "Done unloading Geometry from the engine .....\n"
    if (!preserveSkaters) {
        Dbg::Printf(reinterpret_cast<const char*>(0x0064FBBC)); // "Clean up without preserve skaters\n"
        MemManager()->DeleteExtraSkaterHeaps();
    }
    // the .pre files nothing uses any more
    for (const char* pre = Pip::GetNextLoadedPre(nullptr); pre;) {
        const char* next = Pip::GetNextLoadedPre(pre);
        if (!Pip::PreFileIsInUse(pre))
            Pip::UnloadPre(pre);
        pre = next;
    }
    GameFunction<void(__cdecl*)()>(0x004A0760)();
    GameFunction<void(__cdecl*)()>(0x0048FDE0)();
    GameFunction<void(__cdecl*)(int)>(0x0049EFB0)(0);
    Obj::CPathManager::Instance()->Free();
    if (!CallSkate<bool>(0x005656D0))
        MemManager()->DeleteNetMiscHeap();
    Obj::DestroyCoimHeap();
    MEMORYSTATUS memory;
    GlobalMemoryStatus(&memory);
    char text[0x400];
    int length = wsprintfA(text, reinterpret_cast<const char*>(0x0064FB98), memory.dwTotalPhys >> 10); // "%4d total kb..."
    wsprintfA(text + length, reinterpret_cast<const char*>(0x0064FB74), memory.dwAvailPhys >> 10);    // "%4d  free kb..."
    OutputDebugStringA(text);
    if (CallSkate<bool>(0x005656D0))
        MemManager()->InitNetMiscHeap();
    if (CallSkate<bool>(0x00565700))
        MemManager()->InitInternetHeaps();
    return true;
}

namespace {

// 0x005A0120: what FlagCommand asks, done to `object`: 0 sets flag `value` (of the 32 script flags), 1 clears it,
// 2 tests it, 3 / 4 set / clear the bits `value` of the object flags, 5 sends the object the exception `value`.
// The result is true for 0, 1, 5 and a set flag (2); 3 and 4 leave it.
void ApplyFlagCommand(FlagCommandData* data, Obj::CObject* object)
{
    data->found = true;
    switch (data->action) {
    case 0:
        object->m_flags |= 1u << (data->value & 31);
        data->result = true;
        break;
    case 1:
        object->m_flags &= ~(1u << (data->value & 31));
        data->result = true;
        break;
    case 2:
        if (object->m_flags & (1u << (data->value & 31)))
            data->result = true;
        break;
    case 3:
        object->m_objectFlags |= data->value;
        break;
    case 4:
        object->m_objectFlags &= ~data->value;
        break;
    case 5:
        object->SelfEvent(data->value, nullptr);
        data->result = true;
        break;
    default:
        break;
    }
}

} // namespace

// 0x005A01B0
void __cdecl ApplyFlagCommandToObject(Obj::CObject* object, void* data)
{
    auto* command = static_cast<FlagCommand*>(data);
    if (object->m_id == command->id)
        ApplyFlagCommand(reinterpret_cast<FlagCommandData*>(&command->value), object);
}

void __cdecl ApplyFlagCommandRegisters(void* data, Obj::CObject* object)
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
bool __cdecl FlagCommandByName(Script::CStruct* params, Script::CScript*, int action)
{
    uint32_t value = 0;
    if (action == kFlagActionException)
        params->GetChecksum(0u, &value, 0);
    else
        params->GetInteger(0u, reinterpret_cast<int*>(&value), 0);
    // (two flags looked for and not used)
    if (!params->ContainsFlag(kFlagCommandFlagA))
        params->ContainsFlag(kFlagCommandFlagB);
    FlagCommand command;
    uint32_t id;
    if (params->GetChecksum(kName, &id, 0)) {
        command.value = value;
        command.id = id;
        command.action = action;
        command.found = false;
        command.result = false;
        Objects()->ForEach(&ApplyFlagCommandToObject, &command);
        return command.result;
    }
    const char* prefix;
    if (!params->GetString(kPrefix, &prefix, 0))
        return false;
    uint16_t count = 0;
    uint32_t* nodes = SkateScript::GetPrefixedNodes(prefix, &count); // (the original passes a third argument, 1)
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
bool __cdecl CFunc_MakeSkaterGoto(Script::CStruct* params, Script::CScript* script)
{
    Script::CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    Obj::CObject* object = script ? script->m_object.Get() : nullptr;
    Obj::CSkater* skater;
    if (object && object->m_type == kSkaterType) {
        skater = static_cast<Obj::CSkater*>(object);
    } else {
        int index = 0;
        if (params->GetInteger(kSkater, &index, 0))
            skater = GetSkater(index);
        else
            skater = GetLocalSkater();
    }
    if (skater && skater->m_unknownBC)
        skater->SwitchToScript(name, scriptParams);
    return true;
}

// 0x005A03C0: MakeSkaterGosub <script> [params={...}] [skater=n (0)]: the skater (the calling script's own, else
// skater n) runs the script as a call.
bool __cdecl CFunc_MakeSkaterGosub(Script::CStruct* params, Script::CScript* script)
{
    Script::CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    Obj::CObject* object = script ? script->m_object.Get() : nullptr;
    if (!object || object->m_type != kSkaterType) {
        int index = 0;
        params->GetInteger(kSkater, &index, 0);
        object = GetSkater(index);
    }
    if (object)
        object->CallScript(name, scriptParams);
    return true;
}

// 0x005A0460: SpawnSound <script> [Id=id] [Params={...}]: the script spawned on the calling script's object (its
// id the calling script's spawn id unless given), remembering the caller's .qb file unless 0x01917DF2 is given. A
// script without an object: SpawnScript.
bool __cdecl CFunc_SpawnSound(Script::CStruct* params, Script::CScript* script)
{
    Obj::CObject* object = script->m_object.Get();
    if (!object)
        return CFunc_SpawnScript(params, script);
    uint32_t name = 0;
    if (params->GetChecksum(0u, &name, 0)) {
        uint32_t id = Script::GetSpawnId(script);
        params->GetChecksum(reinterpret_cast<const char*>(0x0064FAE4), &id, 0); // "Id"
        Script::CStruct* scriptParams = nullptr;
        params->GetStructure(reinterpret_cast<const char*>(0x0064FCA4), &scriptParams, 0); // "Params"
        Script::CScript* spawned = script->m_object.Get()->SpawnScript(name, scriptParams, id, false);
        if (!params->ContainsFlag(kKeepSpawnerFile))
            spawned->SetSpawnerFile(script);
    }
    return true;
}

// 0x005A0520: PauseSkater <n> [hide]: skater n paused (vtable +0x2C) and hidden or shown.
bool __cdecl CFunc_PauseSkater(Script::CStruct* params, Script::CScript*)
{
    bool hide = params->ContainsFlag(reinterpret_cast<const char*>(0x0064FCAC)); // "hide"
    int index = 0; // (unset in the original when missing - an asserted parameter)
    params->GetInteger(0u, &index, true);
    if (Obj::CSkater* skater = GetSkater(index)) {
        (*reinterpret_cast<void(__thiscall***)(Obj::CSkater*)>(skater))[0x2C / 4](skater);
        skater->Hide(hide);
    }
    return true;
}

// 0x005A0580: UnPauseSkater <n>: skater n unpaused (vtable +0x30) and shown.
bool __cdecl CFunc_UnPauseSkater(Script::CStruct* params, Script::CScript*)
{
    int index = 0; // (unset in the original when missing - an asserted parameter)
    params->GetInteger(0u, &index, true);
    if (Obj::CSkater* skater = GetSkater(index)) {
        (*reinterpret_cast<void(__thiscall***)(Obj::CSkater*)>(skater))[0x30 / 4](skater);
        skater->Hide(false);
    }
    return true;
}

// 0x005A05C0: PauseGame: the game paused, the calling script carrying on.
bool __cdecl CFunc_PauseGame(Script::CStruct*, Script::CScript* script)
{
    Mdl::FrontEndInstance()->PauseGame(true);
    Script::UnpauseSpawnedScript(script);
    return true;
}

// 0x005A05E0: UnPauseGame.
bool __cdecl CFunc_UnPauseGame(Script::CStruct*, Script::CScript*)
{
    Mdl::FrontEndInstance()->PauseGame(false);
    return true;
}

// 0x005A05F0: GameIsPaused: whether the game is paused (0x006FA7D4) - then the flag 0x3BA1FDB9 is added to the
// command's own parameters.
bool __cdecl CFunc_GameIsPaused(Script::CStruct* params, Script::CScript*)
{
    if (!GameVariable<bool>(0x006FA7D4))
        return false;
    params->AddChecksum(kGamePaused, kGamePaused);
    return true;
}

// 0x005A0620 / 0x005A0630: PauseObjects / UnPauseObjects.
bool __cdecl CFunc_PauseObjects(Script::CStruct*, Script::CScript*)
{
    Objects()->Pause(true);
    return true;
}

bool __cdecl CFunc_UnPauseObjects(Script::CStruct*, Script::CScript*)
{
    Objects()->Pause(false);
    return true;
}

// 0x005A0640: ResetClock: on the server, the skate module's clock (+0x370) set to the game mode's time limit.
bool __cdecl CFunc_ResetClock(Script::CStruct*, Script::CScript*)
{
    if (GameNetManager()->OnServer())
        CallSkate(0x00564D50, GameModeTimeLimit());
    return true;
}

// 0x005A0670: PadsPluggedIn.
bool __cdecl CFunc_PadsPluggedIn(Script::CStruct*, Script::CScript*) { return Mdl::FrontEndInstance()->PadsPluggedIn(); }

// 0x005A0680: DoFlash duration=<seconds> [texture="<name>"] [z=<depth>] [skater=n (the calling object's viewport)]
// [start_r ... start_a] [end_r ... end_a] [flags 0x70FFC649 (1), 0x19C43CF6 (2) or else 0x387C9ED6 (4),
// 0xDF6436D2 (8)]: a screen flash (0x004923E0) from the start colour to the end one. Nothing unless duration > 0.
// The eight colour values share one integer (as in the original): a missing one repeats the one before.
bool __cdecl CFunc_DoFlash(Script::CStruct* params, Script::CScript* script)
{
    float duration = 0.0f;
    float z = 0.0f;
    int viewport = -1;
    const char* texture = nullptr;
    params->GetFloat(reinterpret_cast<const char*>(0x0064BB34), &duration, 0); // "duration"
    if (!(duration > GameVariable<float>(0x0066D520))) // (0.0)
        return true;
    params->GetString(reinterpret_cast<const char*>(0x0064FCF8), &texture, 0); // "texture"
    params->GetFloat(reinterpret_cast<const char*>(0x0064FCF4), &z, 0);        // "z"
    int flags = 0;
    if (params->ContainsFlag(kFlashTextureFlag))
        flags = 1;
    if (params->ContainsFlag(kFlashFlag2))
        flags |= 2;
    else if (params->ContainsFlag(kFlashFlag4))
        flags |= 4;
    params->GetInteger(kSkater, &viewport, 0);
    if (viewport == -1)
        viewport = static_cast<Obj::CSkater*>(script->m_object.Get())->m_viewport;
    if (params->ContainsFlag(kFlashFlag8))
        flags |= 8;
    uint8_t start[4];
    uint8_t end[4];
    int value = 0;
    static const uintptr_t kStart[4] = {0x0064FCEC, 0x0064FCE4, 0x0064FCDC, 0x0064FCD4}; // "start_r" ... "start_a"
    static const uintptr_t kEnd[4] = {0x0064FCCC, 0x0064FCC4, 0x0064FCBC, 0x0064FCB4};   // "end_r" ... "end_a"
    for (int i = 0; i < 4; ++i) {
        params->GetInteger(reinterpret_cast<const char*>(kStart[i]), &value, 0);
        start[i] = static_cast<uint8_t>(value);
    }
    for (int i = 0; i < 4; ++i) {
        params->GetInteger(reinterpret_cast<const char*>(kEnd[i]), &value, 0);
        end[i] = static_cast<uint8_t>(value);
    }
    uint32_t startColour;
    uint32_t endColour;
    std::memcpy(&startColour, start, 4);
    std::memcpy(&endColour, end, 4);
    GameFunction<void(__cdecl*)(int, uint32_t, uint32_t, float, float, int, const char*)>(0x004923E0)(
        viewport, startColour, endColour, duration, z, flags, texture);
    return true;
}

// 0x005A3B00 / 0x005A3B20 / 0x005A3B40 / 0x005A3B60: ClearFlag / SendFlag / QueryFlag <flag> name=<id> or
// prefix="<nodes>"; SendException <exception> name= or prefix=: FlagCommandByName with 1 (clear), 0 (set), 2 (test),
// 5 (exception). Whether an object did it (QueryFlag: whether one has the flag).
bool __cdecl CFunc_ClearFlag(Script::CStruct* params, Script::CScript* script)
{
    return FlagCommandByName(params, script, kFlagActionClear);
}

bool __cdecl CFunc_SendFlag(Script::CStruct* params, Script::CScript* script)
{
    return FlagCommandByName(params, script, kFlagActionSet);
}

bool __cdecl CFunc_QueryFlag(Script::CStruct* params, Script::CScript* script)
{
    return FlagCommandByName(params, script, kFlagActionTest);
}

bool __cdecl CFunc_SendException(Script::CStruct* params, Script::CScript* script)
{
    return FlagCommandByName(params, script, kFlagActionException);
}

// 0x005A11C0: RoundIncrement: the game mode's round (0x005536D0).
bool __cdecl CFunc_RoundIncrement(Script::CStruct*, Script::CScript*)
{
    GameFunction<void(__thiscall*)(void*)>(0x005536D0)(GameMode());
    return true;
}

// 0x005A11D0: GameModeEquals <mode> ...: whether one of the bare names is the game mode's (0x00553220: its own
// integer of that name is set).
bool __cdecl CFunc_GameModeEquals(Script::CStruct* params, Script::CScript*)
{
    for (Script::CComponent* component = params->GetNextComponent(nullptr); component;
         component = params->GetNextComponent(component)) {
        if (component->name == 0 && component->Type() == Script::ESYMBOLTYPE_NAME &&
            GameFunction<bool(__thiscall*)(void*, uint32_t)>(0x00553220)(GameMode(), component->checksum))
            return true;
    }
    return false;
}

// 0x005A1230: GetCurrentLevel: the skate module's level (+0x374, as 0x651533EC - a name) and level structure name
// (+0x380, 0x16A0B364) in the script's parameters.
bool __cdecl CFunc_GetCurrentLevel(Script::CStruct*, Script::CScript* script)
{
    uint8_t* skate = Skate();
    script->m_params->AddComponentOfType(0x651533EC, Script::ESYMBOLTYPE_NAME, *reinterpret_cast<uint32_t*>(skate + 0x374));
    script->m_params->AddChecksum(0x16A0B364, *reinterpret_cast<uint32_t*>(skate + 0x380));
    return true;
}

// 0x005A1270: SetLevelStructureName name=<structure>: the skate module's +0x380.
bool __cdecl CFunc_SetLevelStructureName(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (unset in the original when missing - an asserted parameter)
    params->GetChecksum(kName, &name, 1);
    *reinterpret_cast<uint32_t*>(Skate() + 0x380) = name;
    return true;
}

// 0x005A12A0: GetLevelStructureName: the skate module's +0x380 as the script's 0x63B1214E.
bool __cdecl CFunc_GetLevelStructureName(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(0x63B1214E, *reinterpret_cast<uint32_t*>(Skate() + 0x380));
    return true;
}

// 0x005A12C0: RestartLevel (the skate module's 0x005668A0).
bool __cdecl CFunc_RestartLevel(Script::CStruct*, Script::CScript*)
{
    CallSkate(0x005668A0);
    return true;
}

// 0x005A12E0: GameFlow <script>: the skate module's game flow (+0x388) runs it (0x00552E30).
bool __cdecl CFunc_GameFlow(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x00552E30)(*reinterpret_cast<void**>(Skate() + 0x388), name);
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

uint32_t& LastBroadcastedOmnigon() { return GameVariable<uint32_t>(0x007CEBC8); }

// A skater's score (0x00580480; null if none) and its display brought up to date (0x005626E0).
void UpdateScoreDisplay(Obj::CSkater* skater)
{
    if (void* score = skater->GetScore())
        GameFunction<void(__thiscall*)(void*)>(0x005626E0)(score);
}

} // namespace

// 0x005A1320: ClearOmnigons: the career's 23 Omnigon flags (the values of the globals above) cleared.
bool __cdecl CFunc_ClearOmnigons(Script::CStruct*, Script::CScript*)
{
    auto* career = *reinterpret_cast<Obj::CSkaterCareer**>(Skate() + 0x20);
    for (uint32_t flag : kOmnigonFlags)
        career->UnSetGlobalFlag(Script::GetInteger(flag));
    return true;
}

// 0x005A1540: BroadcastOmnigon <0xAE94C183=omnigon> [0xF649D637]: noted as the last one sent and, from the server,
// sent to every remote connection (message 0x7F, 8 bytes: the Omnigon and the flag; sequenced, group 8).
bool __cdecl CFunc_BroadcastOmnigon(Script::CStruct* params, Script::CScript*)
{
    GameNet::Manager* gameNet = GameNetManager();
    Net::ConnSearch search = {Net::kConnSearchVtable, nullptr};
    uint32_t omnigon = 0; // (unset in the original when missing - an asserted parameter)
    params->GetChecksum(kOmnigon, &omnigon, true);
    bool flag = params->ContainsComponentNamed(kOmnigonFlag);
    LastBroadcastedOmnigon() = omnigon;
    Net::App* server = gameNet->m_server;
    for (Net::Conn* conn = server->FirstConnection(&search); conn; conn = server->NextConnection(&search)) {
        if (!conn->IsRemote())
            continue;
        Net::MsgDesc desc;
        desc.Construct();
        struct {
            uint32_t omnigon;
            bool flag;
            uint8_t pad[3]; // (unset in the original: sent as they are; zero here)
        } message = {omnigon, flag, {}};
        desc.m_data = &message;
        desc.m_id = 0x7F;
        desc.m_length = 8;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = 8;
        server->EnqueueMessage(conn->m_handle, &desc);
        LastBroadcastedOmnigon() = omnigon;
    }
    LastBroadcastedOmnigon() = omnigon;
    return true;
}

// 0x005A1720: SetScreenMode <...>: the screen mode (0x004A0C20 with the command's parameters), then every skater's
// score display brought up to date.
bool __cdecl CFunc_SetScreenMode(Script::CStruct* params, Script::CScript* script)
{
    GameFunction<void(__cdecl*)(Script::CStruct*, Script::CScript*)>(0x004A0C20)(params, script);
    for (uint32_t i = 0; i < 8; ++i) {
        if (Obj::CSkater* skater = GetSkater(static_cast<int>(i)))
            UpdateScoreDisplay(skater);
    }
    return true;
}

// 0x005A1770: SetScreenModeFromGameMode: a debug message per local skater; the game mode's screen mode
// (0x00553630) set (0x004A0AD0) unless the two split modes (1 and 2) would only swap; the local skaters' score
// displays brought up to date.
bool __cdecl CFunc_SetScreenModeFromGameMode(Script::CStruct*, Script::CScript*)
{
    for (uint32_t i = 0; i < 8; ++i) {
        Obj::CSkater* skater = GetSkater(static_cast<int>(i));
        if (skater && skater->m_unknownBC)
            Dbg::Printf(reinterpret_cast<const char*>(0x0064FE34), 0x16C0); // "cfuncs %d: SUTUBBBED..."
    }
    int mode = GameFunction<int(__thiscall*)(void*)>(0x00553630)(GameMode());
    int current = GameFunction<int(__cdecl*)()>(0x004A0710)();
    if (!((mode == 1 && current == 2) || (mode == 2 && current == 1)))
        GameFunction<void(__cdecl*)(int)>(0x004A0AD0)(mode);
    for (uint32_t i = 0; i < 8; ++i) {
        Obj::CSkater* skater = GetSkater(static_cast<int>(i));
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
bool __cdecl CFunc_TryCheatString(Script::CStruct* params, Script::CScript*)
{
    const char* text = nullptr; // (unset in the original when missing - an asserted parameter)
    params->GetString(kCheatText, &text, true);
    if (std::strlen(text) < 6) {
        Script::RunScript(kCheatFailed, nullptr, nullptr, false, nullptr);
        return false;
    }
    char buffer[0x80];
#pragma warning(push)
#pragma warning(disable : 4996) // (unbounded, and the text is the format: as in the original)
    std::sprintf(buffer, reinterpret_cast<const char*>(0x0064FEA8)); // "12345678901234567890"
    std::sprintf(buffer, text);
    uint32_t checksum = Crc::GenerateCRCFromString(text);
    int sum = 0;
    for (int round = 0; round < 100000; ++round) {
        std::sprintf(buffer, "%d", static_cast<int>(round + checksum));
        buffer[std::strlen(buffer)] = 'X';
        for (const char* c = buffer; *c; ++c)
            sum += static_cast<signed char>(*c) * 0x3FF;
        checksum = Crc::GenerateCRCFromString(buffer);
    }
#pragma warning(pop)
    uint32_t c1 = Crc::GenerateCRCFromString(text + std::strlen(text) / 3) ^ static_cast<uint32_t>(sum);
    uint32_t c2 = Crc::GenerateCRCFromString(buffer) ^ static_cast<uint32_t>(sum);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FE78), c1, c2, text); // "{c1=%d c2=%d CheatScript=cheat_xxx }..."
    Script::CArray* cheats = Script::GetArray(kCheatList);
    int count = static_cast<int>(cheats->m_size);
    bool found = false;
    for (int i = 0; i < count; ++i) {
        auto* cheat = reinterpret_cast<Script::CStruct*>(cheats->GetElement(static_cast<uint32_t>(i)));
        int cheatC1 = 0;
        int cheatC2 = 0;
        uint32_t script = 0;
        cheat->GetInteger(reinterpret_cast<const char*>(0x0064FE74), &cheatC1, true);  // "c1"
        cheat->GetInteger(reinterpret_cast<const char*>(0x0064FE70), &cheatC2, true);  // "c2"
        cheat->GetChecksum(reinterpret_cast<const char*>(0x0064FE64), &script, true);  // "cheatscript"
        if (static_cast<uint32_t>(cheatC1) == c1 && static_cast<uint32_t>(cheatC2) == c2) {
            Script::RunScript(kCheatFound, nullptr, nullptr, false, nullptr);
            Script::RunScript(script, nullptr, nullptr, false, nullptr);
            found = true;
        }
    }
    if (!found)
        Script::RunScript(kCheatFailed, nullptr, nullptr, false, nullptr);
    return false;
}

// 0x005A1A90: LevelIs <level>: whether it's the skate module's level (+0x378).
bool __cdecl CFunc_LevelIs(Script::CStruct* params, Script::CScript*)
{
    uint32_t level = 0; // (unset in the original when missing - an asserted parameter)
    params->GetChecksum(0u, &level, true);
    return *reinterpret_cast<uint32_t*>(Skate() + 0x378) == level;
}

// 0x005A1F80: SetStatOverride [value (0)]: the skate module's forced stat value (+0x3D0).
bool __cdecl CFunc_SetStatOverride(Script::CStruct* params, Script::CScript*)
{
    float value = 0.0f;
    params->GetFloat(0u, &value, 0);
    *reinterpret_cast<float*>(Skate() + 0x3D0) = value;
    return true;
}

// 0x005A1FB0: ToggleRails: the skate module's rail display (+0x3D4) on / off.
bool __cdecl CFunc_ToggleRails(Script::CStruct*, Script::CScript*)
{
    bool& rails = *reinterpret_cast<bool*>(Skate() + 0x3D4);
    rails = !rails;
    return true;
}

// 0x005A1FD0: GetTimeLimit: the game mode's time limit as the script's time_limit.
bool __cdecl CFunc_GetTimeLimit(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddInteger(0xC51BF6E3, GameModeTimeLimit()); // "time_limit"
    return true;
}

// 0x005A1FF0 / 0x005A2010: GetGoalsMode / GetTeamGraffitiMode: the same game mode value (0x005532D0) as the script's
// 0x38DBE1D0 / 0x6835B854.
bool __cdecl CFunc_GetGoalsMode(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(0x38DBE1D0, GameFunction<uint32_t(__thiscall*)(void*)>(0x005532D0)(GameMode()));
    return true;
}

bool __cdecl CFunc_GetTeamGraffitiMode(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(0x6835B854, GameFunction<uint32_t(__thiscall*)(void*)>(0x005532D0)(GameMode()));
    return true;
}

// 0x005A2030: GetGameMode: the game mode's name (0x005532A0) as the script's 0x3E04B26B.
bool __cdecl CFunc_GetGameMode(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(0x3E04B26B, GameFunction<uint32_t(__thiscall*)(void*)>(0x005532A0)(GameMode()));
    return true;
}

// 0x005A2090: SetTesterScript <script> [params={...}] (the skate module's 0x00565ED0).
bool __cdecl CFunc_SetTesterScript(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    Script::CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    CallSkate(0x00565ED0, name, scriptParams);
    return true;
}

// 0x005A20F0: KillTesterScript (the skate module's 0x00565F80: its answer).
bool __cdecl CFunc_KillTesterScript(Script::CStruct*, Script::CScript*) { return CallSkate<bool>(0x00565F80); }

// 0x005A21A0: AutoRail <...>: the skate module's rail object (+0x24) is given the parameters - a shared stub that
// ignores them.
bool __cdecl CFunc_AutoRail(Script::CStruct* params, Script::CScript*)
{
    (void)*reinterpret_cast<void**>(Skate() + 0x24); // (the stub's `this`)
    Stubs::IgnoreArgument(static_cast<int>(reinterpret_cast<intptr_t>(params)));
    return true;
}

} // namespace CFuncs

REPLACE(0x0059FE50, CFuncs::CFunc_Cleanup);
REPLACE_UNCHECKED(0x005A0120, CFuncs::ApplyFlagCommandStub); // the structure in ESI, the object in EAX
REPLACE(0x005A01B0, CFuncs::ApplyFlagCommandToObject);
REPLACE(0x005A01D0, CFuncs::FlagCommandByName);
REPLACE(0x005A0300, CFuncs::CFunc_MakeSkaterGoto);
REPLACE(0x005A03C0, CFuncs::CFunc_MakeSkaterGosub);
REPLACE(0x005A0460, CFuncs::CFunc_SpawnSound);
REPLACE(0x005A0520, CFuncs::CFunc_PauseSkater);
REPLACE(0x005A0580, CFuncs::CFunc_UnPauseSkater);
REPLACE(0x005A05C0, CFuncs::CFunc_PauseGame);
REPLACE(0x005A05E0, CFuncs::CFunc_UnPauseGame);
REPLACE(0x005A05F0, CFuncs::CFunc_GameIsPaused);
REPLACE(0x005A0620, CFuncs::CFunc_PauseObjects);
REPLACE(0x005A0630, CFuncs::CFunc_UnPauseObjects);
REPLACE(0x005A0640, CFuncs::CFunc_ResetClock);
REPLACE(0x005A0670, CFuncs::CFunc_PadsPluggedIn);
REPLACE(0x005A0680, CFuncs::CFunc_DoFlash);
REPLACE(0x005A3B00, CFuncs::CFunc_ClearFlag);
REPLACE(0x005A3B20, CFuncs::CFunc_SendFlag);
REPLACE(0x005A3B40, CFuncs::CFunc_QueryFlag);
REPLACE(0x005A3B60, CFuncs::CFunc_SendException);
REPLACE(0x005A11C0, CFuncs::CFunc_RoundIncrement);
REPLACE(0x005A11D0, CFuncs::CFunc_GameModeEquals);
REPLACE(0x005A1230, CFuncs::CFunc_GetCurrentLevel);
REPLACE(0x005A1270, CFuncs::CFunc_SetLevelStructureName);
REPLACE(0x005A12A0, CFuncs::CFunc_GetLevelStructureName);
REPLACE(0x005A12C0, CFuncs::CFunc_RestartLevel);
REPLACE(0x005A12E0, CFuncs::CFunc_GameFlow);
REPLACE(0x005A1320, CFuncs::CFunc_ClearOmnigons);
REPLACE(0x005A1540, CFuncs::CFunc_BroadcastOmnigon);
REPLACE(0x005A1720, CFuncs::CFunc_SetScreenMode);
REPLACE(0x005A1770, CFuncs::CFunc_SetScreenModeFromGameMode);
REPLACE(0x005A1840, CFuncs::CFunc_TryCheatString);
REPLACE(0x005A1A90, CFuncs::CFunc_LevelIs);
REPLACE_SOURCE_ONLY(0x005A18A9, CFuncs::CFunc_TryCheatString); // (not a function: the rest of 0x005A1840 after the length test)
REPLACE(0x005A1F80, CFuncs::CFunc_SetStatOverride);
REPLACE(0x005A1FB0, CFuncs::CFunc_ToggleRails);
REPLACE(0x005A1FD0, CFuncs::CFunc_GetTimeLimit);
REPLACE(0x005A1FF0, CFuncs::CFunc_GetGoalsMode);
REPLACE(0x005A2010, CFuncs::CFunc_GetTeamGraffitiMode);
REPLACE(0x005A2030, CFuncs::CFunc_GetGameMode);
REPLACE(0x005A2090, CFuncs::CFunc_SetTesterScript);
REPLACE(0x005A20F0, CFuncs::CFunc_KillTesterScript);
REPLACE(0x005A21A0, CFuncs::CFunc_AutoRail);

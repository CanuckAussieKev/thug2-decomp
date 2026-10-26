// The script manager: the spawned scripts' per-frame update, spawning and running scripts, and the walks over
// the list of all scripts that stop, kill or look scripts up (0x00472D80-0x004759F5, group D of the CScript
// batch - docs/script-runtime.md).
//
// Frame-level scheduling (UpdateSpawnedScripts, the spec for a later port of the script runtime):
// - Every CScript is on one list, the newest at the head. Spawned scripts belong to the list: they are updated
//   here once per frame, from the head (newest) to the tail (oldest); the others (objects' scripts...) are
//   skipped - their owners update them.
// - Before a script is updated, the next spawned script after it is picked out; the walk goes on from that one
//   whatever the update did. So a script spawned during the walk (at the head, behind the walk) first runs next
//   frame; a script stopped during the walk (ClearScript: no code) is still reached this frame, its Update says
//   finished and it is deleted - its callback run first; a script DELETED during the walk while it's the next
//   one is read after being freed (original behaviour - only KillSpawnedScript deletes outright).
// - A spawned script is skipped (not updated) while paused - unless it has no code left, then it is updated,
//   finishes and goes - and while SCRIPT_FLAG_PAUSE_WITH_OBJECT is set and its object says it is paused (the
//   object's vtable +0x1C returns false); with no object it runs.
// - One Update per frame. When it returns FINISHED the callback script (if any) runs to its end at once
//   (RunScript, with the callback parameters, on the script's object, not over the network), then the script is
//   deleted. Any other result (waiting, blocked, an interrupt that returned) keeps it for next frame.
// - DeleteSpawnedScripts called during the walk only sets a flag; the deletion happens after the walk, still in
//   this call. The one-per-frame wait's "a script was released this frame" flag is cleared at the start.
#include <cstddef>
#include <cstdint>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/script/cscript.h"
#include "game/script/load.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Script {

namespace {

// How the original makes and frees a script (no C++ constructor or destructor runs): the pooled operator new
// (0x00449970) and Construct (0x004738E0); Destroy (0x00474420) and the pooled operator delete (0x004499B0).
CScript* NewScript()
{
    void* memory = CScript::operator new(sizeof(CScript));
    return memory ? static_cast<CScript*>(memory)->Construct() : nullptr;
}
void DeleteScript(CScript* script)
{
    if (!script)
        return;
    script->Destroy();
    CScript::operator delete(script);
}
void SetScript(CScript* script, uint32_t name, const uint8_t* code, CStruct* params, Obj::CObject* object)
{
    script->SetScript(name, code, params, object);
}
// Stop a script and report it (the walks have ReportStopped inlined).
void Stop(CScript* script)
{
    script->ClearScript();
    script->ReportStopped();
}
EScriptReturnVal UpdateScript(CScript* script) { return script->Update(); }
bool CallMemberFunctionOf(CMemberFunctionEntry* entry, CStruct* params, Obj::CObject* object, CScript* script)
{
    return CallMemberFunction(entry, params, object, script);
}

// A C function (the value of an ESYMBOLTYPE_CFUNCTION symbol).
using CFunction = bool(__cdecl*)(CStruct* params, CScript* script);

// Game objects (Obj::CObject, not decompiled yet): through their vtable.
void** Vtable(Obj::CObject* object) { return *reinterpret_cast<void***>(object); }
// vtable +0x0C: run the member function `name`.
bool CallMemberFunctionByName(Obj::CObject* object, uint32_t name, CStruct* params, CScript* script)
{
    return reinterpret_cast<bool(__thiscall*)(Obj::CObject*, uint32_t, CStruct*, CScript*)>(Vtable(object)[3])(
        object, name, params, script);
}
// vtable +0x1C: false while the object is paused (its SCRIPT_FLAG_PAUSE_WITH_OBJECT scripts wait).
bool ObjectRunsScripts(Obj::CObject* object)
{
    return reinterpret_cast<bool(__thiscall*)(Obj::CObject*)>(Vtable(object)[7])(object);
}
// +0x14: the object's id (sent to the other players).
uint32_t ObjectId(Obj::CObject* object) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(object) + 0x14); }

// The network game (GameNet::Manager 0x007CCE14).
using NetManager = GameNet::Manager;
NetManager* Net() { return GameVariable<NetManager*>(0x007CCE14); }
// 0x0044E4F0: whether a network game is on.
bool InNetGame(NetManager* net) { return net->InNetGame(); }
// 0x00532EA0: the client connection `index` (+0x08 + 4 * index).
void* GetClient(NetManager* net, int index) { return net->GetClient(index); }
// Net::MsgDesc (net_app.h): a message to be sent; the constructor 0x004549F0 sets the defaults.
using ::Net::MsgDesc;
void InitMsgDesc(MsgDesc* desc) { desc->Construct(); }
// 0x00459E50: queue a message for the server.
void EnqueueMessageToServer(void* client, MsgDesc* desc)
{
    static_cast<::Net::Client*>(client)->EnqueueMessageToServer(desc);
}
// 0x00479E20: `params` written into `buffer` (at most `size` bytes); returns the bytes written.
int WriteToBuffer(CStruct* params, uint8_t* buffer, int size)
{
    return static_cast<int>(Script::WriteToBuffer(params, buffer, static_cast<uint32_t>(size), 1, nullptr));
}
constexpr uint8_t MSG_ID_RUN_SCRIPT = 0x48;
constexpr uint8_t MSG_ID_SPAWN_SCRIPT = 0x49;

bool IsSpawned(const CScript* script) { return (script->m_spawnFlags & SCRIPT_FLAG_SPAWNED) != 0; }
// Whether `object` is the object of `script` at any level (null matches levels with no object).
bool UsesObject(const CScript* script, Obj::CObject* object)
{
    if (script->m_object.Get() == object)
        return true;
    for (int i = 0; i < script->m_numReturnAddresses; ++i) {
        if (script->m_returnAddresses[i].mpObject == object)
            return true;
    }
    return false;
}

} // namespace

// 0x00475920
void UpdateSpawnedScripts()
{
    UpdatingSpawnedScripts() = true;
    OnePerFrameReleased() = false;
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->mp_next;
        while (next && !IsSpawned(next))
            next = next->mp_next;
        uint8_t flags = script->m_spawnFlags;
        bool run = (flags & SCRIPT_FLAG_SPAWNED) && !((flags & SCRIPT_FLAG_PAUSED) && script->m_pc);
        if (run && (flags & SCRIPT_FLAG_PAUSE_WITH_OBJECT) && script->m_object.Get())
            run = ObjectRunsScripts(script->m_object.Get());
        if (run && UpdateScript(script) == ESCRIPTRETURNVAL_FINISHED) {
            if (script->m_callbackScript)
                RunScript(script->m_callbackScript, script->m_callbackParams, script->m_object.Get(), false, nullptr);
            DeleteScript(script);
        }
        script = next;
    }
    UpdatingSpawnedScripts() = false;
    if (DeleteSpawnedScriptsPending()) {
        DeleteSpawnedScripts();
        DeleteSpawnedScriptsPending() = false;
    }
}

// 0x004753C0
void DeleteSpawnedScripts()
{
    if (UpdatingSpawnedScripts()) {
        DeleteSpawnedScriptsPending() = true;
        return;
    }
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->mp_next;
        if (IsSpawned(script) && !(script->m_spawnFlags & SCRIPT_FLAG_NOT_SESSION_SPECIFIC))
            DeleteScript(script);
        script = next;
    }
}

// 0x00475420
CScript* SpawnScript(uint32_t name, CStruct* params, uint32_t callback, CStruct* callbackParams, int node,
                     uint32_t id, bool netEnabled, bool permanent, bool notSessionSpecific, bool pauseWithObject)
{
    CSymbolTableEntry* symbol = LookUpSymbol(name);
    if (symbol && symbol->type == ESYMBOLTYPE_CFUNCTION) {
        // A C function is just called, with a temporary script (made on the script heap if not from the pool).
        Mem::Manager* manager = Mem::Manager::Get();
        manager->PushContext(manager->m_scriptHeap);
        CScript* script = NewScript();
        Mem::Manager::Get()->PopContext();
        reinterpret_cast<CFunction>(symbol->raw)(params, script);
        DeleteScript(script);
        return nullptr;
    }
    // (with no such symbol too: SetScript then stops it at once, and it's deleted at its first update)
    CScript* script = NewScript();
    SetScript(script, name, nullptr, params, nullptr);
    script->m_spawnId = id;
    script->m_node = node;
    script->m_spawnFlags = static_cast<uint8_t>((script->m_spawnFlags & 0xF1) | SCRIPT_FLAG_SPAWNED |
                                                (notSessionSpecific ? SCRIPT_FLAG_NOT_SESSION_SPECIFIC : 0) |
                                                (pauseWithObject ? SCRIPT_FLAG_PAUSE_WITH_OBJECT : 0));
    if (callback) {
        script->m_callbackScript = callback;
        CStruct* copy = NewStruct();
        copy = copy ? copy->Init() : nullptr;
        script->m_callbackParams = copy;
        if (callbackParams)
            copy->AppendStructure(callbackParams);
    }
    if (netEnabled && InNetGame(Net()))
        SendSpawnScriptMessage(name, nullptr, node, permanent);
    return script;
}

// 0x00475670
CScript* SpawnScript(const char* name, CStruct* params, uint32_t callback, CStruct* callbackParams, int node,
                     uint32_t id, bool netEnabled, bool permanent, bool notSessionSpecific, bool pauseWithObject)
{
    return SpawnScript(Crc::GenerateCRCFromString(name), params, callback, callbackParams, node, id, netEnabled,
                       permanent, notSessionSpecific, pauseWithObject);
}

// 0x004756C0
void KillSpawnedScript(CScript* script)
{
    if (script && IsSpawned(script))
        DeleteScript(script);
}

// 0x004756F0
void StopAllScriptsUsingThisObject(Obj::CObject* object) { StopScriptsUsingObject(object, 0); }

// 0x00475700
void DeleteFinishedScripts()
{
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->mp_next;
        if (!script->m_pc && !IsSpawned(script) && !script->m_eventHandlers)
            DeleteScript(script);
        script = next;
    }
}

// 0x00475790
void RunScript(uint32_t name, CStruct* params, Obj::CObject* object, bool netEnabled, const char* /*nameString*/)
{
    CSymbolTableEntry* symbol = LookUpSymbol(name);
    if (!symbol || (symbol->type != ESYMBOLTYPE_QSCRIPT && symbol->type != ESYMBOLTYPE_CFUNCTION &&
                    symbol->type != ESYMBOLTYPE_MEMBERFUNCTION))
        return;
    if (netEnabled)
        SendRunScriptMessage(name, params, object);
    switch (symbol->type) {
    case ESYMBOLTYPE_MEMBERFUNCTION:
        // (no check that there is an object)
        if (auto* entry = reinterpret_cast<CMemberFunctionEntry*>(symbol->raw))
            CallMemberFunctionOf(entry, params, object, nullptr);
        else
            CallMemberFunctionByName(object, name, params, nullptr);
        return;
    case ESYMBOLTYPE_CFUNCTION:
        reinterpret_cast<CFunction>(symbol->raw)(params, nullptr);
        return;
    default: {
        Mem::Manager* manager = Mem::Manager::Get();
        manager->PushContext(manager->m_scriptHeap);
        CScript* script = NewScript();
        SetScript(script, name, nullptr, params, object);
        Mem::Manager::Get()->PopContext();
        // To the end now: waits on frames or time just spin; a script that blocks never returns (original).
        while (UpdateScript(script) != ESCRIPTRETURNVAL_FINISHED) {
        }
        DeleteScript(script);
        return;
    }
    }
}

// 0x004758F0
void RunScript(const char* name, CStruct* params, Obj::CObject* object, bool netEnabled)
{
    RunScript(Crc::GenerateCRCFromString(name), params, object, netEnabled, name);
}

// 0x004752F0
void RunScriptOnce(uint32_t name, CStruct* params, Obj::CObject* object, CStruct* results)
{
    CScript* script = NewScript();
    SetScript(script, name, nullptr, params, object);
    UpdateScript(script);
    if (script->m_params)
        results->AppendStructure(script->m_params); // (no check that there are results)
    DeleteScript(script);
}

// 0x00473350
void SendRunScriptMessage(uint32_t name, CStruct* params, Obj::CObject* object)
{
    NetManager* net = Net();
    if (!InNetGame(net))
        return;
    MsgDesc desc;
    InitMsgDesc(&desc);
    struct {
        uint32_t name;
        uint32_t objectId; // -1: none
        uint8_t params[0x400];
    } message;
    message.name = name;
    message.objectId = object ? ObjectId(object) : 0xFFFFFFFF;
    int size = WriteToBuffer(params, message.params, sizeof(message.params));
    void* client = GetClient(net, 0);
    desc.m_data = &message;
    desc.m_length = static_cast<uint16_t>(size + 8);
    desc.m_id = MSG_ID_RUN_SCRIPT;
    desc.m_queue = MsgDesc::kQueueSequenced;
    desc.m_groupId = 8;
    EnqueueMessageToServer(client, &desc);
}

// 0x00473420
void SendSpawnScriptMessage(uint32_t name, Obj::CObject* object, int node, bool permanent)
{
    NetManager* net = Net();
    if (!InNetGame(net))
        return;
    MsgDesc desc;
    InitMsgDesc(&desc);
    struct {
        uint32_t name;
        uint32_t objectId; // -1: none
        int node;
        bool permanent;
    } message = {}; // (16 bytes sent: the original leaves the last 3 as they were on the stack)
    message.name = name;
    message.objectId = object ? ObjectId(object) : 0xFFFFFFFF;
    message.node = node;
    message.permanent = permanent;
    static_assert(sizeof(message) == 0x10, "the message's size");
    void* client = GetClient(net, 0);
    desc.m_data = &message;
    desc.m_length = sizeof(message);
    desc.m_id = MSG_ID_SPAWN_SCRIPT;
    desc.m_queue = MsgDesc::kQueueSequenced;
    desc.m_groupId = 8;
    EnqueueMessageToServer(client, &desc);
}

// 0x00472D80
void PauseSpawnedScripts(bool pause)
{
    for (CScript* script = ScriptListHead(); script; script = script->mp_next) {
        if (IsSpawned(script))
            script->m_spawnFlags = static_cast<uint8_t>((script->m_spawnFlags & ~SCRIPT_FLAG_PAUSED) |
                                                        (pause ? SCRIPT_FLAG_PAUSED : 0));
    }
}

// 0x00472DC0
void UnpauseSpawnedScript(CScript* script)
{
    if (script && IsSpawned(script))
        script->m_spawnFlags &= ~SCRIPT_FLAG_PAUSED;
}

// 0x00472DE0
bool ScriptExists(uint32_t name)
{
    CSymbolTableEntry* symbol = LookUpSymbol(name);
    return symbol && symbol->type == ESYMBOLTYPE_QSCRIPT;
}

// 0x00472E00
uint32_t GetSpawnId(CScript* script) { return script && IsSpawned(script) ? script->m_spawnId : 0; }

// 0x00472E20
CScript* FindSpawnedScriptWithId(uint32_t id)
{
    for (CScript* script = ScriptListHead(); script; script = script->mp_next) {
        if (IsSpawned(script) && script->m_spawnId == id)
            return script;
    }
    return nullptr;
}

// The kill walks: spawned scripts only; each one's next is read before it is stopped.

// 0x00473FA0
void KillSpawnedScriptsThatReferTo(uint32_t name)
{
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->mp_next;
        if (IsSpawned(script) && ScriptRefersTo(script, name))
            Stop(script);
        script = next;
    }
}

// 0x00474040: `id` is a spawn id, a unique id or a .qb file's checksum (m_spawnerFile: the scripts spawned by
// that file's scripts) - the CFunction KillSpawnedScript id=... and the front end's elements, which pass the
// name of the script they spawned (with spawn id 0 - so only a spawn id or file of that value matches).
void KillSpawnedScriptsWithId(uint32_t id)
{
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->mp_next;
        if (IsSpawned(script) &&
            (script->m_spawnId == id || script->m_uniqueId == id || script->m_spawnerFile == id))
            Stop(script);
        script = next;
    }
}

// 0x004740D0
void KillSpawnedScriptsWithObjectAndId(Obj::CObject* object, uint32_t id, CScript* except)
{
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->mp_next;
        if (IsSpawned(script) && script != except && UsesObject(script, object) && script->m_spawnId == id)
            Stop(script);
        script = next;
    }
}

// 0x00474190
void KillSpawnedScriptsWithObjectAndName(Obj::CObject* object, uint32_t name, CScript* except)
{
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->mp_next;
        if (IsSpawned(script) && script != except && UsesObject(script, object) && script->m_scriptName == name)
            Stop(script);
        script = next;
    }
}

// The stop walks: every script (spawned or not); each one's next is read after it is stopped.

// 0x00474250
void StopAllScripts()
{
    for (CScript* script = ScriptListHead(); script; script = script->mp_next)
        Stop(script);
}

// 0x004742B0
void StopScriptsUsingObject(Obj::CObject* object, uint32_t name)
{
    for (CScript* script = ScriptListHead(); script; script = script->mp_next) {
        if (UsesObject(script, object) && (!name || script->m_scriptName == name))
            Stop(script);
    }
}

// 0x00474350
void StopScriptsUsingObjectThatReferTo(Obj::CObject* object, uint32_t name)
{
    for (CScript* script = ScriptListHead(); script; script = script->mp_next) {
        if (UsesObject(script, object) && (!name || ScriptRefersTo(script, name)))
            Stop(script);
    }
}

} // namespace Script

REPLACE(0x00475920, Script::UpdateSpawnedScripts);
REPLACE(0x004753C0, Script::DeleteSpawnedScripts);
REPLACE(0x00475420, static_cast<Script::CScript* (*)(uint32_t, Script::CStruct*, uint32_t, Script::CStruct*, int, uint32_t,
                                                      bool, bool, bool, bool)>(&Script::SpawnScript));
REPLACE(0x00475670, static_cast<Script::CScript* (*)(const char*, Script::CStruct*, uint32_t, Script::CStruct*, int,
                                                      uint32_t, bool, bool, bool, bool)>(&Script::SpawnScript));
REPLACE(0x004756C0, Script::KillSpawnedScript);
REPLACE(0x004756F0, Script::StopAllScriptsUsingThisObject);
REPLACE(0x00475700, Script::DeleteFinishedScripts);
REPLACE(0x00475790, static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript));
REPLACE(0x004758F0, static_cast<void (*)(const char*, Script::CStruct*, Obj::CObject*, bool)>(&Script::RunScript));
REPLACE(0x004752F0, Script::RunScriptOnce);
REPLACE(0x00473350, Script::SendRunScriptMessage);
REPLACE(0x00473420, Script::SendSpawnScriptMessage);
REPLACE(0x00472D80, Script::PauseSpawnedScripts);
REPLACE(0x00472DC0, Script::UnpauseSpawnedScript);
REPLACE(0x00472DE0, Script::ScriptExists);
REPLACE(0x00472E00, Script::GetSpawnId);
REPLACE(0x00472E20, Script::FindSpawnedScriptWithId);
REPLACE(0x00473FA0, Script::KillSpawnedScriptsThatReferTo);
REPLACE(0x00474040, Script::KillSpawnedScriptsWithId);
REPLACE(0x004740D0, Script::KillSpawnedScriptsWithObjectAndId);
REPLACE(0x00474190, Script::KillSpawnedScriptsWithObjectAndName);
REPLACE(0x00474250, Script::StopAllScripts);
REPLACE(0x004742B0, Script::StopScriptsUsingObject);
REPLACE(0x00474350, Script::StopScriptsUsingObjectThatReferTo);

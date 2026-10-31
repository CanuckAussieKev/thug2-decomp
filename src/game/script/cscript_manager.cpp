// The script manager: the spawned scripts' per-frame update, spawning and running scripts, and the walks over
// the list of all scripts that stop, kill or look scripts up (0x00472D80-0x004759F5, group D of the CScript
// batch - docs/script-runtime.md).
//
// Frame-level scheduling (TickChildScripts, the spec for a later port of the script runtime):
// - Every CScript is on one list, the newest at the head. Spawned scripts belong to the list: they are updated
//   here once per frame, from the head (newest) to the tail (oldest); the others (objects' scripts...) are
//   skipped - their owners update them.
// - Before a script is updated, the next spawned script after it is picked out; the walk goes on from that one
//   whatever the update did. So a script spawned during the walk (at the head, behind the walk) first runs next
//   frame; a script stopped during the walk (ResetScriptState: no code) is still reached this frame, its Update says
//   finished and it is deleted - its callback run first; a script DELETED during the walk while it's the next
//   one is read after being freed (original behaviour - only KillSpawnedScript deletes outright).
// - A spawned script is skipped (not updated) while paused - unless it has no code left, then it is updated,
//   finishes and goes - and while SCRIPT_FLAG_PAUSE_WITH_OBJECT is set and its object says it is paused (the
//   object's vtable +0x1C returns false); with no object it runs.
// - One Update per frame. When it returns FINISHED the callback script (if any) runs to its end at once
//   (LaunchScript, with the callback parameters, on the script's object, not over the network), then the script is
//   deleted. Any other result (waiting, blocked, an interrupt that returned) keeps it for next frame.
// - KillAllChildScripts called during the walk only sets a flag; the deletion happens after the walk, still in
//   this call. The one-per-frame wait's "a script was released this frame" flag is cleared at the start.
#include <cstddef>
#include <cstdint>
#include <new>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/script/cscript.h"
#include "game/script/load.h"
#include "game/script/param_struct.h"
#include "game/script/struct_buffer.h"

namespace Scripts {

namespace {

// How the original makes and frees a script (no C++ constructor or destructor runs): the pooled operator new
// (0x00449970) and Construct (0x004738E0); Destroy (0x00474420) and the pooled operator delete (0x004499B0).
CScript* NewScript()
{
    void* memory = CScript::operator new(sizeof(CScript));
    return memory ? static_cast<CScript*>(memory)->MakeNew() : nullptr;
}
void DeleteScript(CScript* script)
{
    if (!script)
        return;
    script->Teardown();
    CScript::operator delete(script);
}
void BindScript(CScript* script, uint32_t name, const uint8_t* code, CStruct* params, GameObjects::CEntity* object)
{
    script->BindScript(name, code, params, object);
}
// Stop a script and report it (the walks have ReportStopped inlined).
void Halt(CScript* script)
{
    script->ResetScriptState();
    script->ReportStopped();
}
EScriptOutcome UpdateScript(CScript* script) { return script->TickFrame(); }
bool CallMemberFunctionOf(CMemberFunctionEntry* entry, CStruct* params, GameObjects::CEntity* object, CScript* script)
{
    return InvokeMethodByName(entry, params, object, script);
}

// A C function (the value of an QBVAL_NATIVE_FUNC symbol).
using CFunction = bool(__cdecl*)(CStruct* params, CScript* script);

// Game objects (GameObjects::CEntity, not decompiled yet): through their vtable.
void** Vtable(GameObjects::CEntity* object) { return *reinterpret_cast<void***>(object); }
// vtable +0x0C: run the member function `name`.
bool CallMemberFunctionByName(GameObjects::CEntity* object, uint32_t name, CStruct* params, CScript* script)
{
    return reinterpret_cast<bool(__thiscall*)(GameObjects::CEntity*, uint32_t, CStruct*, CScript*)>(Vtable(object)[3])(
        object, name, params, script);
}
// vtable +0x1C: false while the object is paused (its SCRIPT_FLAG_PAUSE_WITH_OBJECT scripts wait).
bool ObjectRunsScripts(GameObjects::CEntity* object)
{
    return reinterpret_cast<bool(__thiscall*)(GameObjects::CEntity*)>(Vtable(object)[7])(object);
}
// +0x14: the object's id (sent to the other players).
uint32_t ObjectId(GameObjects::CEntity* object) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(object) + 0x14); }

// The network game (OnlinePlay::Hub 0x007CCE14).
using NetSessionSlot = OnlinePlay::Hub;
NetSessionSlot* NetLink() { return reinterpret_cast<NetSessionSlot*>(OnlinePlay::g_onlineHub); }
// 0x0044E4F0: whether a network game is on.
bool InNetGame(NetSessionSlot* net) { return net->InNetGame(); }
// 0x00532EA0: the client connection `index` (+0x08 + 4 * index).
void* FetchPatron(NetSessionSlot* net, int index) { return net->FetchPatron(index); }
// NetLink::NetMessageInfo (net_app.h): a message to be sent; the constructor 0x004549F0 sets the defaults.
using ::NetLink::NetMessageInfo;
void InitMsgDesc(NetMessageInfo* desc) { desc->MakeNew(); }
// 0x00459E50: queue a message for the server.
void SendToHost(void* client, NetMessageInfo* desc)
{
    static_cast<::NetLink::Guest*>(client)->SendToHost(desc);
}
// 0x00479E20: `params` written into `buffer` (at most `size` bytes); returns the bytes written.
int EmitIntoBuffer(CStruct* params, uint8_t* buffer, int size)
{
    return static_cast<int>(Scripts::EmitIntoBuffer(params, buffer, static_cast<uint32_t>(size), 1, nullptr));
}
constexpr uint8_t NETOP_SPAWN_SCRIPT = 0x48;
constexpr uint8_t MSG_ID_SPAWN_SCRIPT = 0x49;

bool IsSpawned(const CScript* script) { return (script->m_spawnFlags & SCRIPT_FLAG_SPAWNED) != 0; }
// Whether `object` is the object of `script` at any level (null matches levels with no object).
bool UsesObject(const CScript* script, GameObjects::CEntity* object)
{
    if (script->m_entity.Pull() == object)
        return true;
    for (int i = 0; i < script->m_callDepth; ++i) {
        if (script->m_returnAddresses[i].m_ownerEntity == object)
            return true;
    }
    return false;
}

} // namespace

// 0x00475920
void TickChildScripts()
{
    UpdatingSpawnedScripts() = true;
    OnePerFrameReleased() = false;
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->m_following;
        while (next && !IsSpawned(next))
            next = next->m_following;
        uint8_t flags = script->m_spawnFlags;
        bool run = (flags & SCRIPT_FLAG_SPAWNED) && !((flags & SCRIPT_FLAG_PAUSED) && script->m_pc);
        if (run && (flags & SCRIPT_FLAG_PAUSE_WITH_OBJECT) && script->m_entity.Pull())
            run = ObjectRunsScripts(script->m_entity.Pull());
        if (run && UpdateScript(script) == SCRIPT_OUTCOME_FINISHED) {
            if (script->m_onFinishScript)
                LaunchScript(script->m_onFinishScript, script->m_callbackParams, script->m_entity.Pull(), false, nullptr);
            DeleteScript(script);
        }
        script = next;
    }
    UpdatingSpawnedScripts() = false;
    if (DeleteSpawnedScriptsPending()) {
        KillAllChildScripts();
        DeleteSpawnedScriptsPending() = false;
    }
}

// 0x004753C0
void KillAllChildScripts()
{
    if (UpdatingSpawnedScripts()) {
        DeleteSpawnedScriptsPending() = true;
        return;
    }
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->m_following;
        if (IsSpawned(script) && !(script->m_spawnFlags & SCRIPT_FLAG_NOT_SESSION_SPECIFIC))
            DeleteScript(script);
        script = next;
    }
}

// 0x00475420
CScript* SpawnScript(uint32_t name, CStruct* params, uint32_t callback, CStruct* callbackParams, int node,
                     uint32_t id, bool replicateFlag, bool permanent, bool notSessionSpecific, bool pauseWithObject)
{
    CSymbolTableEntry* symbol = ResolveSymbol(name);
    if (symbol && symbol->type == QBVAL_NATIVE_FUNC) {
        // A C function is just called, with a temporary script (made on the script heap if not from the pool).
        MemSys::Hub* manager = MemSys::Hub::Pull();
        manager->EnterHeapScope(manager->m_scriptHeap);
        CScript* script = NewScript();
        MemSys::Hub::Pull()->LeaveHeapScope();
        reinterpret_cast<CFunction>(symbol->raw)(params, script);
        DeleteScript(script);
        return nullptr;
    }
    // (with no such symbol too: BindScript then stops it at once, and it's deleted at its first update)
    CScript* script = NewScript();
    BindScript(script, name, nullptr, params, nullptr);
    script->m_spawnId = id;
    script->m_listLink = node;
    script->m_spawnFlags = static_cast<uint8_t>((script->m_spawnFlags & 0xF1) | SCRIPT_FLAG_SPAWNED |
                                                (notSessionSpecific ? SCRIPT_FLAG_NOT_SESSION_SPECIFIC : 0) |
                                                (pauseWithObject ? SCRIPT_FLAG_PAUSE_WITH_OBJECT : 0));
    if (callback) {
        script->m_onFinishScript = callback;
        CStruct* copy = SpawnParamBag();
        copy = copy ? copy->PrepareAll() : nullptr;
        script->m_callbackParams = copy;
        if (callbackParams)
            copy->MergeStructure(callbackParams);
    }
    if (replicateFlag && InNetGame(NetLink()))
        SendSpawnScriptMessage(name, nullptr, node, permanent);
    return script;
}

// 0x00475670
CScript* SpawnScript(const char* name, CStruct* params, uint32_t callback, CStruct* callbackParams, int node,
                     uint32_t id, bool replicateFlag, bool permanent, bool notSessionSpecific, bool pauseWithObject)
{
    return SpawnScript(Checksums::HashFromText(name), params, callback, callbackParams, node, id, replicateFlag,
                       permanent, notSessionSpecific, pauseWithObject);
}

// 0x004756C0
void KillSpawnedScript(CScript* script)
{
    if (script && IsSpawned(script))
        DeleteScript(script);
}

// 0x004756F0
void EndScriptsBoundToEntity(GameObjects::CEntity* object) { StopScriptsUsingObject(object, 0); }

// 0x00475700
void DeleteFinishedScripts()
{
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->m_following;
        if (!script->m_pc && !IsSpawned(script) && !script->m_signalRoutes)
            DeleteScript(script);
        script = next;
    }
}

// 0x00475790
void LaunchScript(uint32_t name, CStruct* params, GameObjects::CEntity* object, bool replicateFlag, const char* /*nameString*/)
{
    CSymbolTableEntry* symbol = ResolveSymbol(name);
    if (!symbol || (symbol->type != QBVAL_SCRIPT_BODY && symbol->type != QBVAL_NATIVE_FUNC &&
                    symbol->type != QBVAL_NATIVE_METHOD))
        return;
    if (replicateFlag)
        SendRunScriptMessage(name, params, object);
    switch (symbol->type) {
    case QBVAL_NATIVE_METHOD:
        // (no check that there is an object)
        if (auto* entry = reinterpret_cast<CMemberFunctionEntry*>(symbol->raw))
            CallMemberFunctionOf(entry, params, object, nullptr);
        else
            CallMemberFunctionByName(object, name, params, nullptr);
        return;
    case QBVAL_NATIVE_FUNC:
        reinterpret_cast<CFunction>(symbol->raw)(params, nullptr);
        return;
    default: {
        MemSys::Hub* manager = MemSys::Hub::Pull();
        manager->EnterHeapScope(manager->m_scriptHeap);
        CScript* script = NewScript();
        BindScript(script, name, nullptr, params, object);
        MemSys::Hub::Pull()->LeaveHeapScope();
        // To the end now: waits on frames or time just spin; a script that blocks never returns (original).
        while (UpdateScript(script) != SCRIPT_OUTCOME_FINISHED) {
        }
        DeleteScript(script);
        return;
    }
    }
}

// 0x004758F0
void LaunchScript(const char* name, CStruct* params, GameObjects::CEntity* object, bool replicateFlag)
{
    LaunchScript(Checksums::HashFromText(name), params, object, replicateFlag, name);
}

// 0x004752F0
void RunScriptOnce(uint32_t name, CStruct* params, GameObjects::CEntity* object, CStruct* results)
{
    CScript* script = NewScript();
    BindScript(script, name, nullptr, params, object);
    UpdateScript(script);
    if (script->m_args)
        results->MergeStructure(script->m_args); // (no check that there are results)
    DeleteScript(script);
}

// 0x00473350
void SendRunScriptMessage(uint32_t name, CStruct* params, GameObjects::CEntity* object)
{
    NetSessionSlot* net = NetLink();
    if (!InNetGame(net))
        return;
    NetMessageInfo desc;
    InitMsgDesc(&desc);
    struct {
        uint32_t name;
        uint32_t objectId; // -1: none
        uint8_t params[0x400];
    } message;
    message.name = name;
    message.objectId = object ? ObjectId(object) : 0xFFFFFFFF;
    int size = EmitIntoBuffer(params, message.params, sizeof(message.params));
    void* client = FetchPatron(net, 0);
    desc.m_content = &message;
    desc.m_byteCount = static_cast<uint16_t>(size + 8);
    desc.m_ident = NETOP_SPAWN_SCRIPT;
    desc.m_queue = NetMessageInfo::kLaneOrdered;
    desc.m_groupId = 8;
    SendToHost(client, &desc);
}

// 0x00473420
void SendSpawnScriptMessage(uint32_t name, GameObjects::CEntity* object, int node, bool permanent)
{
    NetSessionSlot* net = NetLink();
    if (!InNetGame(net))
        return;
    NetMessageInfo desc;
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
    void* client = FetchPatron(net, 0);
    desc.m_content = &message;
    desc.m_byteCount = sizeof(message);
    desc.m_ident = MSG_ID_SPAWN_SCRIPT;
    desc.m_queue = NetMessageInfo::kLaneOrdered;
    desc.m_groupId = 8;
    SendToHost(client, &desc);
}

// 0x00472D80
void PauseSpawnedScripts(bool pause)
{
    for (CScript* script = ScriptListHead(); script; script = script->m_following) {
        if (IsSpawned(script))
            script->m_spawnFlags = static_cast<uint8_t>((script->m_spawnFlags & ~SCRIPT_FLAG_PAUSED) |
                                                        (pause ? SCRIPT_FLAG_PAUSED : 0));
    }
}

// 0x00472DC0
void ResumeChildScript(CScript* script)
{
    if (script && IsSpawned(script))
        script->m_spawnFlags &= ~SCRIPT_FLAG_PAUSED;
}

// 0x00472DE0
bool ScriptExists(uint32_t name)
{
    CSymbolTableEntry* symbol = ResolveSymbol(name);
    return symbol && symbol->type == QBVAL_SCRIPT_BODY;
}

// 0x00472E00
uint32_t GetSpawnId(CScript* script) { return script && IsSpawned(script) ? script->m_spawnId : 0; }

// 0x00472E20
CScript* LookUpSpawnedScript(uint32_t id)
{
    for (CScript* script = ScriptListHead(); script; script = script->m_following) {
        if (IsSpawned(script) && script->m_spawnId == id)
            return script;
    }
    return nullptr;
}

// The kill walks: spawned scripts only; each one's next is read before it is stopped.

// 0x00473FA0
void StopScriptsTargeting(uint32_t name)
{
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->m_following;
        if (IsSpawned(script) && ScriptRefersTo(script, name))
            Halt(script);
        script = next;
    }
}

// 0x00474040: `id` is a spawn id, a unique id or a .qb file's checksum (m_spawnerFile: the scripts spawned by
// that file's scripts) - the CFunction KillSpawnedScript id=... and the front end's elements, which pass the
// name of the script they spawned (with spawn id 0 - so only a spawn id or file of that value matches).
void StopScriptsByTag(uint32_t id)
{
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->m_following;
        if (IsSpawned(script) &&
            (script->m_spawnId == id || script->m_uniqueId == id || script->m_spawnerFile == id))
            Halt(script);
        script = next;
    }
}

// 0x004740D0
void StopScriptsOfObjectAndTag(GameObjects::CEntity* object, uint32_t id, CScript* except)
{
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->m_following;
        if (IsSpawned(script) && script != except && UsesObject(script, object) && script->m_spawnId == id)
            Halt(script);
        script = next;
    }
}

// 0x00474190
void EndChildScriptsOfEntityByName(GameObjects::CEntity* object, uint32_t name, CScript* except)
{
    for (CScript* script = ScriptListHead(); script;) {
        CScript* next = script->m_following;
        if (IsSpawned(script) && script != except && UsesObject(script, object) && script->m_scriptTitle == name)
            Halt(script);
        script = next;
    }
}

// The stop walks: every script (spawned or not); each one's next is read after it is stopped.

// 0x00474250
void StopAllScripts()
{
    for (CScript* script = ScriptListHead(); script; script = script->m_following)
        Halt(script);
}

// 0x004742B0
void StopScriptsUsingObject(GameObjects::CEntity* object, uint32_t name)
{
    for (CScript* script = ScriptListHead(); script; script = script->m_following) {
        if (UsesObject(script, object) && (!name || script->m_scriptTitle == name))
            Halt(script);
    }
}

// 0x00474350
void StopScriptsUsingObjectThatReferTo(GameObjects::CEntity* object, uint32_t name)
{
    for (CScript* script = ScriptListHead(); script; script = script->m_following) {
        if (UsesObject(script, object) && (!name || ScriptRefersTo(script, name)))
            Halt(script);
    }
}

} // namespace Script

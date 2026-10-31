// GameObjects::CObject: the base of the game's objects - id, type, tags, flags, its own script, and the member functions
// scripts call on any object (object.h).

#include "game/obj/object.h"

#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/objman.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/object.h"

namespace GameObjects {

using Scripts::CArray;
using Scripts::CComponent;
using Scripts::CScript;
using Scripts::CStruct;

namespace {

extern void** const kObjectVtable;
extern void** const kObjectRefVtable;

// Names (checksums) the object uses.
constexpr uint32_t kUnnamed = 0;                 // an unnamed component
constexpr uint32_t kSetProps = 0x6C63C7C5;        // SetProps
constexpr uint32_t kDie = 0xC6870028;             // Die
constexpr uint32_t kEmptyScript = 0x3F5CDB8A;     // empty_script
constexpr uint32_t kId = 0x40C698AF;              // id
constexpr uint32_t kType = 0x7321A8D6;            // type
constexpr uint32_t kName = 0xA1DC81F9;            // name
constexpr uint32_t kObjId = 0x0830ECAF;           // ObjId
constexpr uint32_t kStructure = 0x90FEC815;       // structure
constexpr uint32_t kTags = 0x90436BD9;            // tags
constexpr uint32_t kRemoveTags = 0x8225D629;      // remove_tags
constexpr uint32_t kEventHandlers = 0x475BF03C;   // event_handlers
constexpr uint32_t kReplaceHandlers = 0x3F93A1EF; // replace_handlers
constexpr uint32_t kParams = 0x7031F10C;          // params
constexpr uint32_t kPauseWithObject = 0x8757D0BB; // PauseWithObject
constexpr uint32_t kAll = 0xC4E78E22;             // all
constexpr uint32_t kClear = 0x1A4E0EF9;           // clear
constexpr uint32_t kNoSpawnerFile = 0x01917DF2;   // (name unknown): the spawned script keeps no spawner file

// The tracker's LaunchEvent (to `target`, -1: everyone; its answer isn't used).
void LaunchEvent(uint32_t type, uint32_t target, uint32_t source, CStruct* data, bool broadcast, float radius)
{
    Tracker()->LaunchEvent(type, target, source, data, broadcast, radius);
}

// The object's vtable: slot 1 SetProperties, 6 FlagAsDead.
void VirtualSetProperties(CObject* object, CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CObject*, CStruct*)>(object->m_vtable[1])(object, params);
}
void VirtualMarkAsDead(CObject* object) { reinterpret_cast<void(__thiscall*)(CObject*)>(object->m_vtable[6])(object); }

// How the original makes and frees a script: the pooled operator new and Construct; Destroy and the pooled
// operator delete.
CScript* NewScript()
{
    void* memory = CScript::operator new(sizeof(CScript));
    return memory ? static_cast<CScript*>(memory)->Construct() : nullptr;
}
void DeleteScript(CScript* script)
{
    script->Destroy();
    CScript::operator delete(script);
}
// `new CStruct` (the structure pool) / `delete`.
CStruct* NewEmptyStruct()
{
    CStruct* structure = Scripts::NewStruct();
    return structure ? structure->Init() : nullptr;
}
void FreeStruct(CStruct* structure)
{
    structure->Clear();
    Scripts::DeleteStruct(structure);
}
// A copy of `component` (from the component pool) added to `dest`.
void AddCopy(CStruct* dest, CComponent* component)
{
    CComponent* copy = Scripts::NewComponent();
    Scripts::DuplicateComponent(copy, component, 0);
    dest->AddComponent(copy);
}

} // namespace

// 0x00460920
void CObjectRef::DestroyBody() { Destruct(); }

// 0x00460AF0
CObjectRef* CObjectRef::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00460B20
void CObjectNode::DestroyBody() { UnlinkAs(g_objectNodeVtable); }

// 0x00460B40
CObjectNode* CObjectNode::Destroy(unsigned int flags)
{
    UnlinkAs(g_objectNodeVtable);
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00460B80
CObject* CObject::Construct()
{
    CSharedBase::Construct();
    m_vtable = kObjectVtable;
    m_listLink.vtable = g_objectNodeVtable;
    m_listLink.data = this;
    m_listLink.priority = 0;
    m_listLink.next = &m_listLink;
    m_listLink.prev = &m_listLink;
    m_ref.CSharedBase::Construct();
    m_ref.m_ownerObject = this;
    m_ref.m_vtable = kObjectRefVtable;
    m_id = kUnassignedId;
    m_type = kNoObjectType;
    m_tagTable = nullptr;
    m_runScript = nullptr;
    m_parkOwner = nullptr;
    m_objectFlags = 0;
    m_aliasCount = 0;
    m_visitedMask = 0;
    return this;
}

// 0x004615D0
void CObject::DestroyBody()
{
    m_vtable = kObjectVtable;
    if (m_tagTable)
        FreeStruct(m_tagTable); // (the pointer is left as it is)
    if (m_parkOwner)
        m_parkOwner->VirtualUnregisterObject(this);
    if (m_runScript) {
        m_runScript->ClearOnExitScripts();
        if (m_runScript)
            DeleteScript(m_runScript);
        m_runScript = nullptr;
    }
    Scripts::EndScriptsBoundToEntity(this);
    m_ref.DestroyBody();
    m_listLink.UnlinkAs(g_objectNodeVtable);
    Destruct();
}

// 0x00461900
CObject* CObject::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00460930
void CObject::DisposeWhenFree()
{
    if (!(m_objectFlags & OBJECT_FLAG_LOCKED))
        VirtualDestroy(1);
}

// 0x00461710
void CObject::SetProperties(CStruct* params)
{
    bool replace = params->HasBareFlag(kReplaceHandlers);
    CArray* handlers = nullptr;
    if (params->GetArray(kEventHandlers, &handlers, 0)) {
        CreateScriptIfNeeded();
        m_runScript->InstallEventTable(handlers, replace);
    }
    CStruct* tags = nullptr;
    if (params->ReadSubStruct(kTags, &tags, 0)) {
        CreateTagsIfNeeded();
        m_tagTable->AppendStructureIfAny(tags);
    }
    CArray* removeTags = nullptr;
    if (params->GetArray(kRemoveTags, &removeTags, 0))
        RemoveTags(removeTags);
}

// 0x004608F0
bool CObject::InvokeMethodByName(uint32_t name, CStruct* params, CScript* /*script*/)
{
    if (name == kSetProps)
        VirtualSetProperties(this, params);
    else if (name == kDie)
        VirtualMarkAsDead(this);
    return true;
}

// 0x00461140
bool CObject::PassEventToScript(CSignal* event)
{
    if (!m_runScript)
        return true;
    CSharedRef<CObject> self;
    self.Set(this);
    m_runScript->ReceiveEvent(event);
    return static_cast<bool>(self); // (cleared if the object was deleted meanwhile)
}

// 0x00460A50
void CObject::FlagAsDead()
{
    if (m_runScript)
        m_runScript->Block(0); // (until unblocked: for good)
    Scripts::EndScriptsBoundToEntity(this);
    m_objectFlags &= ~OBJECT_FLAG_LOCKED;
    CObjectHubBase* manager = m_parkOwner; // (unregistering clears m_parkOwner: the manager read first is used)
    manager->VirtualUnregisterObject(this);
    manager->VirtualKillObject(this);
    m_objectFlags |= OBJECT_FLAG_DEAD;
    m_ref.ClearSmartPointers();
}

// 0x00461290
void CObject::CreateScriptIfNeeded()
{
    if (m_runScript)
        return;
    MemSys::EnterHeapScope(MemSys::Manager::Get()->m_scriptHeap);
    CScript* script = NewScript();
    m_runScript = script;
    script->BindScript(kEmptyScript, nullptr, this);
    MemSys::LeaveHeapScope();
}

// 0x00461350
CScript* CObject::ReplaceScript(uint32_t name, CStruct* params)
{
    if (!m_runScript)
        m_runScript = NewScript();
    m_runScript->BindScript(name, params, this);
    m_runScript->HasAttachedScript(); // (the answer isn't used)
    return m_runScript;
}

// 0x00461500
int CObject::RunSubScript(uint32_t name, CStruct* params)
{
    if (!m_runScript) {
        CScript* script = NewScript();
        m_runScript = script;
        script->m_object.Set(this);
    }
    return m_runScript->Interrupt(name, params);
}

// 0x00461410
CScript* CObject::SpawnScript(uint32_t name, CStruct* params, uint32_t id, bool pauseWithObject)
{
    CScript* script = Scripts::SpawnScript(name, params, 0, nullptr, -1, id, false, false, false, pauseWithObject);
    script->m_object.Set(this);
    return script;
}

// 0x00461480
CScript* CObject::StartScriptTask(uint32_t name, int node, bool replicateFlag, bool permanent, CStruct* params)
{
    if (replicateFlag)
        Scripts::SendSpawnScriptMessage(name, nullptr, node, permanent);
    CScript* script = Scripts::SpawnScript(name, params, 0, nullptr, node, 0, false, false, false, false);
    script->m_object.Set(this);
    script->Update();
    return script;
}

// 0x004609F0
void CObject::PostOwnEvent(uint32_t type, CStruct* data) { LaunchEvent(type, m_id, m_id, data, false, -1.0f); }

// 0x00460A20
void CObject::BroadcastEvent(uint32_t type, CStruct* data, float /*radius*/)
{
    LaunchEvent(type, kUnassignedId, m_id, data, true, -1.0f);
}

// 0x00461930
void CObject::SetID(uint32_t id)
{
    if (m_id != kUnassignedId && m_parkOwner)
        m_parkOwner->VirtualReassignObjectId(this, id);
    m_id = id;
    CreateTagsIfNeeded();
    m_tagTable->PutNameKey(kId, id);
}

// 0x00461970
void CObject::SetType(uint32_t type)
{
    m_type = type;
    CreateTagsIfNeeded();
    m_tagTable->PutNameKey(kType, type);
}

// 0x004611E0
void CObject::CreateTagsIfNeeded()
{
    if (m_tagTable)
        return;
    MemSys::EnterHeapScope(MemSys::Manager::Get()->m_frontEndHeap);
    m_tagTable = NewEmptyStruct();
    MemSys::LeaveHeapScope();
}

// 0x004616B0
void CObject::StoreIntTag(uint32_t name, int value)
{
    CreateTagsIfNeeded();
    m_tagTable->PutInt(name, value);
}

// 0x004616D0
void CObject::WriteHashTag(uint32_t name, uint32_t value)
{
    CreateTagsIfNeeded();
    m_tagTable->PutNameKey(name, value);
}

// 0x004616F0
void CObject::SetTagsFromStruct(CStruct* tags)
{
    CreateTagsIfNeeded();
    m_tagTable->AppendStructureIfAny(tags);
}

// 0x00460C20
void CObject::RemoveTags(CArray* names)
{
    if (!m_tagTable)
        return;
    if (names) {
        for (uint32_t i = 0; i < names->m_size; ++i) {
            uint32_t name = names->GetElement(i);
            m_tagTable->RemoveComponent(name);
            m_tagTable->RemoveFlag(name);
        }
        if (!m_tagTable->IsEmpty())
            return;
    }
    if (m_tagTable)
        FreeStruct(m_tagTable);
    m_tagTable = nullptr;
}

// 0x00460CA0
bool CObject::GetTag(CStruct* params, CStruct* dest)
{
    if (!m_tagTable)
        return false;
    // The name is read into the argument's own slot: with no unnamed checksum, the pointer `params` is the name.
    uint32_t name = reinterpret_cast<uint32_t>(params);
    params->ReadHashValue(kUnnamed, &name, 1);
    CComponent* tag = m_tagTable->FindComponent(name);
    if (!tag)
        return false;
    AddCopy(dest, tag);
    return true;
}

// 0x00460D90
bool CObject::GetTags(CArray* names, CStruct* dest)
{
    if (!m_tagTable)
        return true;
    for (uint32_t i = names->m_size; i != 0;) {
        --i;
        if (CComponent* tag = m_tagTable->FindComponent(names->GetElement(i)))
            AddCopy(dest, tag);
    }
    return true;
}

// 0x004607A0
void CObject::ClearEventHandler(uint32_t type)
{
    if (m_runScript)
        m_runScript->ClearEventHandler(type);
}

// 0x004607B0
void CObject::RemoveTagFlag(uint32_t flag)
{
    if (m_tagTable)
        m_tagTable->RemoveFlag(flag);
}

// 0x004607C0
bool CObject::ReadIntTag(uint32_t name, int* value) { return m_tagTable ? m_tagTable->FetchIntValue(name, value, 0) : false; }

// 0x004607E0
bool CObject::ReadHashTag(uint32_t name, uint32_t* value)
{
    return m_tagTable ? m_tagTable->ReadHashValue(name, value, 0) : false;
}

// 0x00460800
bool CObject::ContainsTagFlag(uint32_t flag) { return m_tagTable ? m_tagTable->HasBareFlag(flag) : false; }

// 0x00460820
void CObject::CopyTagsTo(CStruct* dest)
{
    if (m_tagTable)
        dest->AppendStructureIfAny(m_tagTable);
}

// 0x00460960
uint32_t CObject::GetFlagMask(CStruct* params, CScript* /*script*/)
{
    uint32_t mask = 0;
    CArray* names = nullptr;
    int bit = 0;
    params->GetArray(kUnnamed, &names, 0);
    if (names) {
        for (uint32_t i = 0; i < names->m_size; ++i)
            mask |= 1u << (Scripts::FetchIntValue(names->GetElement(i)) & 31);
        return mask;
    }
    if (params->FetchIntValue(kUnnamed, &bit, 0))
        return 1u << (bit & 31);
    return mask;
}

// 0x00460840: the calling script lets go of the object (at every level; it stops waiting). True.
bool CObject::Obj_LeaveScript(CStruct* /*params*/, CScript* script)
{
    script->ObjectDestroyed(this);
    return true;
}

// 0x00460850: kill the scripts spawned on the object with the name `name` - else with the spawn id `id` - except
// the one calling. True.
bool CObject::Obj_KillSpawnedScript(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    if (params->ReadHashValue(kName, &name, 0)) {
        Scripts::EndChildScriptsOfEntityByName(this, name, script);
        return true;
    }
    uint32_t id = 0;
    if (params->ReadHashValue(kId, &id, 0))
        Scripts::StopScriptsOfObjectAndTag(this, id, script);
    return true;
}

// 0x004608D0
bool CObject::Obj_Visible(CStruct* /*params*/, CScript* /*script*/)
{
    m_objectFlags &= ~ENTITY_HIDDEN_BIT;
    return true;
}

// 0x004608E0
bool CObject::Obj_Invisible(CStruct* /*params*/, CScript* /*script*/)
{
    m_objectFlags |= ENTITY_HIDDEN_BIT;
    return true;
}

// 0x00460940: ObjId = the object's id, in the calling script's parameters.
bool CObject::Obj_GetId(CStruct* /*params*/, CScript* script)
{
    script->m_params->PutNameKey(kObjId, m_id);
    return true;
}

// 0x00460EB0: the tags into the calling script's parameters - as a new structure named by structure=, else
// added to them.
bool CObject::Obj_GetTags(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    if (params->ReadHashValue(kStructure, &name, 0)) {
        CStruct* copy = NewEmptyStruct();
        if (m_tagTable)
            copy->AppendStructureIfAny(m_tagTable);
        script->m_params->AppendStructRef(name, copy);
        return true;
    }
    if (m_tagTable)
        script->m_params->AppendStructureIfAny(m_tagTable);
    return true;
}

// 0x00460FB0: the tag named by the unnamed checksum, into the calling script's parameters.
bool CObject::Obj_GetSingleTag(CStruct* params, CScript* script) { return GetTag(params, script->m_params); }

// 0x00460FC0: the tags named in the unnamed array, into the calling script's parameters. With no array, the
// pointer `params` is taken for one [bug].
bool CObject::Obj_GetNamedTags(CStruct* params, CScript* script)
{
    auto* names = reinterpret_cast<CArray*>(params);
    params->GetArray(kUnnamed, &names, 1);
    return GetTags(names, script->m_params);
}

// 0x00460FF0: remove the tags named in tags= (else the unnamed array); with neither, every tag goes. True.
bool CObject::Obj_RemoveTags(CStruct* params, CScript* /*script*/)
{
    CArray* names = nullptr;
    if (!params->GetArray(kTags, &names, 0))
        params->GetArray(kUnnamed, &names, 0);
    RemoveTags(names);
    return true;
}

// 0x00461040: none of the flags is set (no flags named: false).
bool CObject::Obj_FlagNotSet(CStruct* params, CScript* script)
{
    uint32_t mask = GetFlagMask(params, script);
    if (!mask)
        return false;
    return (m_flags & mask) == 0;
}

// 0x00461070: clear the flags (all: every flag). No flags named: false.
bool CObject::Obj_ClearFlag(CStruct* params, CScript* script)
{
    if (params->HasBareFlag(kAll)) {
        m_flags = 0;
        return true;
    }
    uint32_t mask = GetFlagMask(params, script);
    if (!mask)
        return false;
    m_flags &= ~mask;
    return true;
}

// 0x004610C0: set the flags. No flags named: false.
bool CObject::Obj_SetFlag(CStruct* params, CScript* script)
{
    uint32_t mask = GetFlagMask(params, script);
    if (!mask)
        return false;
    m_flags |= mask;
    return true;
}

// 0x004610F0: any of the flags was set (clear: then they're cleared). No flags named: false.
bool CObject::Obj_FlagSet(CStruct* params, CScript* script)
{
    uint32_t flags = m_flags;
    uint32_t mask = GetFlagMask(params, script);
    if (!mask)
        return false;
    if (params->HasBareFlag(kClear))
        m_flags &= ~mask;
    return (flags & mask) != 0;
}

// 0x004617C0: add the parameters to the tags. True.
bool CObject::Obj_SetTags(CStruct* params, CScript* /*script*/)
{
    CreateTagsIfNeeded();
    m_tagTable->AppendStructureIfAny(params);
    return true;
}

// 0x004617E0: spawn the script named first (a bare name) on the object, with params=, id= (default: the
// calling script's spawn id) and PauseWithObject. Unless 0x01917DF2 is given, it gets the calling script's
// spawner file. True (also when the first parameter isn't a name: nothing is spawned).
bool CObject::Obj_SpawnScript(CStruct* params, CScript* script)
{
    CComponent* first = params->StepToEntry(nullptr);
    if (!first || first->Type() != Scripts::QBVAL_NAME)
        return true;
    uint32_t name = first->checksum;
    uint32_t id = Scripts::GetSpawnId(script);
    params->ReadHashValue(kId, &id, 0);
    CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    bool pauseWithObject = params->HasBareFlag(kPauseWithObject);
    CScript* spawned = SpawnScript(name, scriptParams, id, pauseWithObject);
    if (!params->HasBareFlag(kNoSpawnerFile))
        spawned->SetSpawnerFile(script);
    return true;
}

// 0x004618A0: the object's script switches to the unnamed name, with params=. True.
bool CObject::Obj_SwitchScript(CStruct* params, CScript* /*script*/)
{
    uint32_t name = 0;
    if (params->ReadHashValue(kUnnamed, &name, 0)) {
        CStruct* scriptParams = nullptr;
        params->ReadSubStruct(kParams, &scriptParams, 0);
        ReplaceScript(name, scriptParams);
    }
    return true;
}

// 0x00460AD0
CObject* FindObject(uint32_t id) { return Tracker()->LookUpObject(id); }

// 0x00460AC0
void Unlock(CObject* object, void* /*data*/) { object->m_objectFlags &= ~OBJECT_FLAG_LOCKED; }

// 0x00460AA0
void DisposeWhenFree(CObject* object, void* /*data*/)
{
    if (!(object->m_objectFlags & OBJECT_FLAG_LOCKED))
        object->VirtualDestroy(1);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kObjectRefVtable_slots[] = {&GameObjects::CObjectRef::Destroy};
void** const kObjectRefVtable = reinterpret_cast<void**>(kObjectRefVtable_slots);
static VtableSlot kObjectVtable_slots[] = {&GameObjects::CObject::Destroy,
         &GameObjects::CObject::SetProperties,
         static_cast<bool (GameObjects::CObject::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObject::InvokeMethodByName),
         static_cast<bool (GameObjects::CObject::*)(uint32_t, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObject::InvokeMethodByName),
         Stubs::IgnoreArgument,
         &GameObjects::CObject::PassEventToScript,
         &GameObjects::CObject::FlagAsDead,
         Stubs::ReturnTrue};
void** const kObjectVtable = reinterpret_cast<void**>(kObjectVtable_slots);

} // namespace
} // namespace Obj

namespace GameObjects {

static VtableSlot g_objectNodeVtable_slots[] = {&GameObjects::CObjectNode::Destroy};
void** const g_objectNodeVtable = reinterpret_cast<void**>(g_objectNodeVtable_slots);

} // namespace Obj

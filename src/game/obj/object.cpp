// Obj::CObject: the base of the game's objects - id, type, tags, flags, its own script, and the member functions
// scripts call on any object (object.h).

#include "game/obj/object.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

using Script::CArray;
using Script::CComponent;
using Script::CScript;
using Script::CStruct;

namespace {

void** const kObjectVtable = reinterpret_cast<void**>(0x006477E8);
void** const kObjectRefVtable = reinterpret_cast<void**>(0x006477E0);
void** const kObjectNodeVtable = reinterpret_cast<void**>(0x006477E4);

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

// Not rewritten yet (wt/trk): CRefCounted's constructor (vtable 0x006478E4, no pointers), its destructor (the
// vtable back, then the pointers cleared) and the clearing of its pointers (each smart pointer to it set to null).
void ConstructRefCounted(CRefCounted* object)
{
    GameFunction<CRefCounted*(__thiscall*)(CRefCounted*)>(0x004637B0)(object); // -> wt/trk
}
void DestroyRefCounted(CRefCounted* object)
{
    GameFunction<void(__thiscall*)(CRefCounted*)>(0x00463870)(object); // -> wt/trk
}
void ClearSmartPointers(CRefCounted* object)
{
    GameFunction<void(__thiscall*)(CRefCounted*)>(0x00463820)(object); // -> wt/trk
}

// The object tracker (0x006F0FC0, wt/trk): the object with an id, and LaunchEvent (to `target`, -1: everyone).
void* Tracker() { return GameVariable<void*>(0x006F0FC0); }
CObject* TrackerGetObject(uint32_t id)
{
    return GameFunction<CObject*(__thiscall*)(void*, uint32_t)>(0x00462120)(Tracker(), id); // -> wt/trk
}
void LaunchEvent(uint32_t type, uint32_t target, uint32_t source, CStruct* data, bool broadcast, float radius)
{
    GameFunction<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, CStruct*, bool, float)>(0x00462F00)(
        Tracker(), type, target, source, data, broadcast, radius); // -> wt/trk
}

// The object's vtable: slot 1 SetProperties, 6 MarkAsDead.
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
    CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
void FreeStruct(CStruct* structure)
{
    structure->Clear();
    Script::DeleteStruct(structure);
}
// A copy of `component` (from the component pool) added to `dest`.
void AddCopy(CStruct* dest, CComponent* component)
{
    CComponent* copy = Script::NewComponent();
    Script::CopyComponent(copy, component, 0);
    dest->AddComponent(copy);
}

} // namespace

// 0x00460920
void CObjectRef::DestroyBody() { DestroyRefCounted(this); }

// 0x00460AF0
CObjectRef* CObjectRef::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00460B20
void CObjectNode::DestroyBody() { UnlinkAs(kObjectNodeVtable); }

// 0x00460B40
CObjectNode* CObjectNode::Destroy(unsigned int flags)
{
    UnlinkAs(kObjectNodeVtable);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00460B80
CObject* CObject::Construct()
{
    ConstructRefCounted(this);
    m_vtable = kObjectVtable;
    m_node.vtable = kObjectNodeVtable;
    m_node.data = this;
    m_node.priority = 0;
    m_node.next = &m_node;
    m_node.prev = &m_node;
    ConstructRefCounted(&m_ref);
    m_ref.mp_object = this;
    m_ref.m_vtable = kObjectRefVtable;
    m_id = kNoObjectId;
    m_type = kNoObjectType;
    mp_tags = nullptr;
    m_script = nullptr;
    mp_manager = nullptr;
    m_objectFlags = 0;
    unknown3C = 0;
    m_visitedMask = 0;
    return this;
}

// 0x004615D0
void CObject::DestroyBody()
{
    m_vtable = kObjectVtable;
    if (mp_tags)
        FreeStruct(mp_tags); // (the pointer is left as it is)
    if (mp_manager)
        mp_manager->VirtualUnregisterObject(this);
    if (m_script) {
        m_script->ClearOnExitScripts();
        if (m_script)
            DeleteScript(m_script);
        m_script = nullptr;
    }
    Script::StopAllScriptsUsingThisObject(this);
    m_ref.DestroyBody();
    m_node.UnlinkAs(kObjectNodeVtable);
    DestroyRefCounted(this);
}

// 0x00461900
CObject* CObject::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00460930
void CObject::DestroyIfUnlocked()
{
    if (!(m_objectFlags & OBJECT_FLAG_LOCKED))
        VirtualDestroy(1);
}

// 0x00461710
void CObject::SetProperties(CStruct* params)
{
    bool replace = params->ContainsFlag(kReplaceHandlers);
    CArray* handlers = nullptr;
    if (params->GetArray(kEventHandlers, &handlers, 0)) {
        CreateScriptIfNeeded();
        m_script->SetEventHandlers(handlers, replace);
    }
    CStruct* tags = nullptr;
    if (params->GetStructure(kTags, &tags, 0)) {
        CreateTagsIfNeeded();
        mp_tags->AppendStructureIfAny(tags);
    }
    CArray* removeTags = nullptr;
    if (params->GetArray(kRemoveTags, &removeTags, 0))
        RemoveTags(removeTags);
}

// 0x004608F0
bool CObject::CallMemberFunction(uint32_t name, CStruct* params, CScript* /*script*/)
{
    if (name == kSetProps)
        VirtualSetProperties(this, params);
    else if (name == kDie)
        VirtualMarkAsDead(this);
    return true;
}

// 0x00461140
bool CObject::PassEventToScript(CEvent* event)
{
    if (!m_script)
        return true;
    CSmtPtr<CObject> self;
    self.Set(this);
    m_script->ReceiveEvent(event);
    return static_cast<bool>(self); // (cleared if the object was deleted meanwhile)
}

// 0x00460A50
void CObject::MarkAsDead()
{
    if (m_script)
        m_script->Block(0); // (until unblocked: for good)
    Script::StopAllScriptsUsingThisObject(this);
    m_objectFlags &= ~OBJECT_FLAG_LOCKED;
    CBaseManager* manager = mp_manager; // (unregistering clears mp_manager: the manager read first is used)
    manager->VirtualUnregisterObject(this);
    manager->VirtualKillObject(this);
    m_objectFlags |= OBJECT_FLAG_DEAD;
    ClearSmartPointers(&m_ref);
}

// 0x00461290
void CObject::CreateScriptIfNeeded()
{
    if (m_script)
        return;
    Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
    CScript* script = NewScript();
    m_script = script;
    script->SetScript(kEmptyScript, nullptr, this);
    Mem::PopContext();
}

// 0x00461350
CScript* CObject::SwitchScript(uint32_t name, CStruct* params)
{
    if (!m_script)
        m_script = NewScript();
    m_script->SetScript(name, params, this);
    m_script->GotScript(); // (the answer isn't used)
    return m_script;
}

// 0x00461500
int CObject::CallScript(uint32_t name, CStruct* params)
{
    if (!m_script) {
        CScript* script = NewScript();
        m_script = script;
        script->m_object.Set(this);
    }
    return m_script->Interrupt(name, params);
}

// 0x00461410
CScript* CObject::SpawnScript(uint32_t name, CStruct* params, uint32_t id, bool pauseWithObject)
{
    CScript* script = Script::SpawnScript(name, params, 0, nullptr, -1, id, false, false, false, pauseWithObject);
    script->m_object.Set(this);
    return script;
}

// 0x00461480
CScript* CObject::SpawnAndRunScript(uint32_t name, int node, bool netEnabled, bool permanent, CStruct* params)
{
    if (netEnabled)
        Script::SendSpawnScriptMessage(name, nullptr, node, permanent);
    CScript* script = Script::SpawnScript(name, params, 0, nullptr, node, 0, false, false, false, false);
    script->m_object.Set(this);
    script->Update();
    return script;
}

// 0x004609F0
void CObject::SelfEvent(uint32_t type, CStruct* data) { LaunchEvent(type, m_id, m_id, data, false, -1.0f); }

// 0x00460A20
void CObject::BroadcastEvent(uint32_t type, CStruct* data, float /*radius*/)
{
    LaunchEvent(type, kNoObjectId, m_id, data, true, -1.0f);
}

// 0x00461930
void CObject::SetID(uint32_t id)
{
    if (m_id != kNoObjectId && mp_manager)
        mp_manager->VirtualReassignObjectId(this, id);
    m_id = id;
    CreateTagsIfNeeded();
    mp_tags->AddChecksum(kId, id);
}

// 0x00461970
void CObject::SetType(uint32_t type)
{
    m_type = type;
    CreateTagsIfNeeded();
    mp_tags->AddChecksum(kType, type);
}

// 0x004611E0
void CObject::CreateTagsIfNeeded()
{
    if (mp_tags)
        return;
    Mem::PushContext(Mem::Manager::Get()->m_frontEndHeap);
    mp_tags = NewEmptyStruct();
    Mem::PopContext();
}

// 0x004616B0
void CObject::SetIntegerTag(uint32_t name, int value)
{
    CreateTagsIfNeeded();
    mp_tags->AddInteger(name, value);
}

// 0x004616D0
void CObject::SetChecksumTag(uint32_t name, uint32_t value)
{
    CreateTagsIfNeeded();
    mp_tags->AddChecksum(name, value);
}

// 0x004616F0
void CObject::SetTagsFromStruct(CStruct* tags)
{
    CreateTagsIfNeeded();
    mp_tags->AppendStructureIfAny(tags);
}

// 0x00460C20
void CObject::RemoveTags(CArray* names)
{
    if (!mp_tags)
        return;
    if (names) {
        for (uint32_t i = 0; i < names->m_size; ++i) {
            uint32_t name = names->GetElement(i);
            mp_tags->RemoveComponent(name);
            mp_tags->RemoveFlag(name);
        }
        if (!mp_tags->IsEmpty())
            return;
    }
    if (mp_tags)
        FreeStruct(mp_tags);
    mp_tags = nullptr;
}

// 0x00460CA0
bool CObject::GetTag(CStruct* params, CStruct* dest)
{
    if (!mp_tags)
        return false;
    // The name is read into the argument's own slot: with no unnamed checksum, the pointer `params` is the name.
    uint32_t name = reinterpret_cast<uint32_t>(params);
    params->GetChecksum(kUnnamed, &name, 1);
    CComponent* tag = mp_tags->FindComponent(name);
    if (!tag)
        return false;
    AddCopy(dest, tag);
    return true;
}

// 0x00460D90
bool CObject::GetTags(CArray* names, CStruct* dest)
{
    if (!mp_tags)
        return true;
    for (uint32_t i = names->m_size; i != 0;) {
        --i;
        if (CComponent* tag = mp_tags->FindComponent(names->GetElement(i)))
            AddCopy(dest, tag);
    }
    return true;
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
            mask |= 1u << (Script::GetInteger(names->GetElement(i)) & 31);
        return mask;
    }
    if (params->GetInteger(kUnnamed, &bit, 0))
        return 1u << (bit & 31);
    return mask;
}

// 0x00460850: kill the scripts spawned on the object with the name `name` - else with the spawn id `id` - except
// the one calling. True.
bool CObject::Obj_KillSpawnedScript(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    if (params->GetChecksum(kName, &name, 0)) {
        Script::KillSpawnedScriptsWithObjectAndName(this, name, script);
        return true;
    }
    uint32_t id = 0;
    if (params->GetChecksum(kId, &id, 0))
        Script::KillSpawnedScriptsWithObjectAndId(this, id, script);
    return true;
}

// 0x004608D0
bool CObject::Obj_Visible(CStruct* /*params*/, CScript* /*script*/)
{
    m_objectFlags &= ~OBJECT_FLAG_INVISIBLE;
    return true;
}

// 0x004608E0
bool CObject::Obj_Invisible(CStruct* /*params*/, CScript* /*script*/)
{
    m_objectFlags |= OBJECT_FLAG_INVISIBLE;
    return true;
}

// 0x00460940: ObjId = the object's id, in the calling script's parameters.
bool CObject::Obj_GetId(CStruct* /*params*/, CScript* script)
{
    script->m_params->AddChecksum(kObjId, m_id);
    return true;
}

// 0x00460EB0: the tags into the calling script's parameters - as a new structure named by structure=, else
// added to them.
bool CObject::Obj_GetTags(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    if (params->GetChecksum(kStructure, &name, 0)) {
        CStruct* copy = NewEmptyStruct();
        if (mp_tags)
            copy->AppendStructureIfAny(mp_tags);
        script->m_params->AddStructurePointer(name, copy);
        return true;
    }
    if (mp_tags)
        script->m_params->AppendStructureIfAny(mp_tags);
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
    if (params->ContainsFlag(kAll)) {
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
    if (params->ContainsFlag(kClear))
        m_flags &= ~mask;
    return (flags & mask) != 0;
}

// 0x004617C0: add the parameters to the tags. True.
bool CObject::Obj_SetTags(CStruct* params, CScript* /*script*/)
{
    CreateTagsIfNeeded();
    mp_tags->AppendStructureIfAny(params);
    return true;
}

// 0x004617E0: spawn the script named first (a bare name) on the object, with params=, id= (default: the
// calling script's spawn id) and PauseWithObject. Unless 0x01917DF2 is given, it gets the calling script's
// spawner file. True (also when the first parameter isn't a name: nothing is spawned).
bool CObject::Obj_SpawnScript(CStruct* params, CScript* script)
{
    CComponent* first = params->GetNextComponent(nullptr);
    if (!first || first->Type() != Script::ESYMBOLTYPE_NAME)
        return true;
    uint32_t name = first->checksum;
    uint32_t id = Script::GetSpawnId(script);
    params->GetChecksum(kId, &id, 0);
    CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    bool pauseWithObject = params->ContainsFlag(kPauseWithObject);
    CScript* spawned = SpawnScript(name, scriptParams, id, pauseWithObject);
    if (!params->ContainsFlag(kNoSpawnerFile))
        spawned->SetSpawnerFile(script);
    return true;
}

// 0x004618A0: the object's script switches to the unnamed name, with params=. True.
bool CObject::Obj_SwitchScript(CStruct* params, CScript* /*script*/)
{
    uint32_t name = 0;
    if (params->GetChecksum(kUnnamed, &name, 0)) {
        CStruct* scriptParams = nullptr;
        params->GetStructure(kParams, &scriptParams, 0);
        SwitchScript(name, scriptParams);
    }
    return true;
}

// 0x00460AD0
CObject* FindObject(uint32_t id) { return TrackerGetObject(id); }

// 0x00460AC0
void Unlock(CObject* object, void* /*data*/) { object->m_objectFlags &= ~OBJECT_FLAG_LOCKED; }

// 0x00460AA0
void DestroyIfUnlocked(CObject* object, void* /*data*/)
{
    if (!(object->m_objectFlags & OBJECT_FLAG_LOCKED))
        object->VirtualDestroy(1);
}

namespace {
// What GetComponent sees of a component.
struct ComponentLink {
    void** m_vtable;
    uint32_t m_type;
    uint32_t unknown8[2];
    CBaseComponent* mp_next;
};
} // namespace

// 0x0045DB90
CBaseComponent* CCompositeObject::GetComponent(uint32_t type)
{
    for (CBaseComponent* component = mp_components; component;
         component = reinterpret_cast<ComponentLink*>(component)->mp_next) {
        if (reinterpret_cast<ComponentLink*>(component)->m_type == type)
            return component;
    }
    return nullptr;
}

} // namespace Obj

REPLACE_SOURCE_ONLY(0x00460920, &Obj::CObjectRef::DestroyBody); // a jump to 0x00463870
REPLACE(0x00460AF0, &Obj::CObjectRef::Destroy);
REPLACE(0x00460B20, &Obj::CObjectNode::DestroyBody);
REPLACE(0x00460B40, &Obj::CObjectNode::Destroy);
REPLACE(0x00460B80, &Obj::CObject::Construct);
REPLACE(0x004615D0, &Obj::CObject::DestroyBody);
REPLACE(0x00461900, &Obj::CObject::Destroy);
REPLACE(0x00460930, &Obj::CObject::DestroyIfUnlocked);
REPLACE(0x00461710, &Obj::CObject::SetProperties);
REPLACE(0x004608F0, &Obj::CObject::CallMemberFunction);
REPLACE(0x00461140, &Obj::CObject::PassEventToScript);
REPLACE(0x00460A50, &Obj::CObject::MarkAsDead);
REPLACE(0x00461290, &Obj::CObject::CreateScriptIfNeeded);
REPLACE(0x00461350, &Obj::CObject::SwitchScript);
REPLACE(0x00461500, &Obj::CObject::CallScript);
REPLACE(0x00461410, &Obj::CObject::SpawnScript);
REPLACE(0x00461480, &Obj::CObject::SpawnAndRunScript);
REPLACE(0x004609F0, &Obj::CObject::SelfEvent);
REPLACE(0x00460A20, &Obj::CObject::BroadcastEvent);
REPLACE(0x00461930, &Obj::CObject::SetID);
REPLACE(0x00461970, &Obj::CObject::SetType);
REPLACE(0x004611E0, &Obj::CObject::CreateTagsIfNeeded);
REPLACE(0x004616B0, &Obj::CObject::SetIntegerTag);
REPLACE(0x004616D0, &Obj::CObject::SetChecksumTag);
REPLACE(0x004616F0, &Obj::CObject::SetTagsFromStruct);
REPLACE(0x00460C20, &Obj::CObject::RemoveTags);
REPLACE(0x00460CA0, &Obj::CObject::GetTag);
REPLACE(0x00460D90, &Obj::CObject::GetTags);
REPLACE(0x00460960, &Obj::CObject::GetFlagMask);
REPLACE(0x00460850, &Obj::CObject::Obj_KillSpawnedScript);
REPLACE(0x004608D0, &Obj::CObject::Obj_Visible);
REPLACE(0x004608E0, &Obj::CObject::Obj_Invisible);
REPLACE(0x00460940, &Obj::CObject::Obj_GetId);
REPLACE(0x00460EB0, &Obj::CObject::Obj_GetTags);
REPLACE(0x00460FB0, &Obj::CObject::Obj_GetSingleTag);
REPLACE(0x00460FC0, &Obj::CObject::Obj_GetNamedTags);
REPLACE(0x00460FF0, &Obj::CObject::Obj_RemoveTags);
REPLACE(0x00461040, &Obj::CObject::Obj_FlagNotSet);
REPLACE(0x00461070, &Obj::CObject::Obj_ClearFlag);
REPLACE(0x004610C0, &Obj::CObject::Obj_SetFlag);
REPLACE(0x004610F0, &Obj::CObject::Obj_FlagSet);
REPLACE(0x004617C0, &Obj::CObject::Obj_SetTags);
REPLACE(0x004617E0, &Obj::CObject::Obj_SpawnScript);
REPLACE(0x004618A0, &Obj::CObject::Obj_SwitchScript);
REPLACE(0x00460AD0, Obj::FindObject);
REPLACE(0x00460AC0, Obj::Unlock);
REPLACE(0x00460AA0, Obj::DestroyIfUnlocked);
REPLACE(0x0045DB90, &Obj::CCompositeObject::GetComponent);

// The cutscene destroy list's commands and the std::list constructor (see cutscene_list_cfuncs.h).

#include "game/obj/cutscene_list_cfuncs.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/movie_cfuncs.h"
#include "game/obj/pager_message.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/startup_init.h"

namespace ScriptCommands {

bool g_cutsceneListFlushing;
PointerList g_cutsceneDestroyList;

namespace {

using Scripts::CStruct;
using Node = PointerList::Node;

constexpr uint32_t kName = 0xA1DC81F9;             // "name"
constexpr uint32_t kDestroyFunc = 0x589B2282;      // "destroy_func"
constexpr uint32_t kDestroyParams = 0x9C2E3A57;    // "destroy_params"
constexpr uint32_t kParams = 0x7031F10C;           // "params"
constexpr uint32_t kId = 0x40C698AF;               // "id"
constexpr uint32_t kCallback = 0x86068BD9;         // "callback"
constexpr uint32_t kCallbackParams = 0x0DD85BDA;   // "CallbackParams"
constexpr uint32_t kNetEnabled = 0x20209C31;       // "NetEnabled"
constexpr uint32_t kPermanent = 0x23627FD7;        // "permanent"
constexpr uint32_t kNotSessionSpecific = 0x2EBFF4BC; // "NotSessionSpecific"
constexpr uint32_t kKillSpawnedScript = 0xD7034230; // "cutscene_kill_spawned_script"

PointerList& FreeNodeChain() { return g_cutsceneDestroyList; }
// Set while the list is flushed (RemoveFromCutsceneDestroyList does nothing then).
bool& Flushing() { return g_cutsceneListFlushing; }

CutsceneDestroyEntry* EntryOf(Node* node) { return static_cast<CutsceneDestroyEntry*>(node->value); }

// `delete entry` (inlined): its parameters deleted, then the entry freed.
void DeleteEntry(CutsceneDestroyEntry* entry)
{
    if (!entry)
        return;
    if (CStruct* params = entry->m_params) {
        params->Clear();
        Scripts::DeleteStruct(params);
        entry->m_params = nullptr;
    }
    MemSys::Manager::Get()->Free(entry);
}

// A new structure (`new CStruct`), deleted with Clear and DeleteStruct.
CStruct* NewParams()
{
    CStruct* memory = Scripts::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteParams(CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

} // namespace

// 0x005B4650
PointerList* PointerList::Construct()
{
    m_size = 0;
    auto* node = static_cast<Node*>(MemSys::New(sizeof(Node))); // (operator new inlined)
    if (node) {
        node->next = node;
        node->prev = node;
    }
    m_head = node;
    return this;
}

// 0x005B5320: AddToCutsceneDestroyList name=<object> destroy_func=<script> [destroy_params={...}]: an entry (with its
// own copy of the parameters) added at the end of the list. Always true.
bool __cdecl CFunc_AddToCutsceneDestroyList(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    uint32_t destroyScript;
    params->ReadHashValue(kDestroyFunc, &destroyScript, 1);
    CStruct* destroyParams = nullptr;
    params->ReadSubStruct(kDestroyParams, &destroyParams, 0);
    // (the original walks the list to its end first, finding nothing)
    void* memory = MemSys::AllocZeroed(sizeof(CutsceneDestroyEntry));
    auto* entry = memory ? static_cast<CutsceneDestroyEntry*>(memory)->Construct() : nullptr;
    entry->m_name = name;
    entry->m_destroyScript = destroyScript;
    if (destroyParams)
        entry->m_params->AppendStructureIfAny(destroyParams);

    // push_back
    PointerList& list = FreeNodeChain();
    Node* head = list.m_head;
    auto* node = static_cast<Node*>(MemSys::New(sizeof(Node))); // (operator new inlined)
    if (node) {
        node->value = entry;
        node->next = head;
        node->prev = head->prev;
    }
    ++list.m_size;
    head->prev = node;
    node->prev->next = node;
    return true;
}

// 0x005B5450: RemoveFromCutsceneDestroyList name=<object>: the object's entry deleted and taken out of the list (not
// run). False while the list is being flushed or without such an entry.
bool __cdecl CFunc_RemoveFromCutsceneDestroyList(Scripts::CStruct* params, Scripts::CScript*)
{
    if (Flushing())
        return false;
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    PointerList& list = FreeNodeChain();
    Node* head = list.m_head;
    Node* node = head ? head->next : nullptr;
    for (; node != head; node = node->next)
        if (EntryOf(node)->m_name == name)
            break;
    if (node == head)
        return false;
    DeleteEntry(EntryOf(node));
    if (node != list.m_head) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        MemSys::Manager::Get()->Free(node);
        --list.m_size;
    }
    return true;
}

// 0x005B5510: Cutscene_SpawnScript <script> [params={...}] [id=<id>] [callback=<script>] [CallbackParams={...}]
// [NetEnabled] [permanent] [NotSessionSpecific=n]: the script spawned as SpawnScript does (the id defaulting to the
// calling script's .qb file - its symbol's +8), and an entry added to the cutscene destroy list that kills it
// (cutscene_kill_spawned_script with its unique id). The spawned script isn't checked for null. Always true.
bool __cdecl CFunc_Cutscene_SpawnScript(Scripts::CStruct* params, Scripts::CScript* script)
{
    Scripts::DumpToLog(params, 0);
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    uint32_t id = Scripts::FindSymbol(script->m_scriptTitle)->unknown8;
    params->ReadHashValue(kId, &id, 0);
    uint32_t callback = 0;
    params->ReadHashValue(kCallback, &callback, 0);
    CStruct* callbackParams = nullptr;
    params->ReadSubStruct(kCallbackParams, &callbackParams, 0);
    bool replicateFlag = params->HasBareFlag(kNetEnabled);
    bool permanent = params->HasBareFlag(kPermanent);
    int notSessionSpecific = 0;
    params->FetchIntValue(kNotSessionSpecific, &notSessionSpecific, 0);
    Scripts::CScript* spawned = Scripts::SpawnScript(name, scriptParams, callback, callbackParams, script->m_listLink, id,
                                                   replicateFlag, permanent, notSessionSpecific != 0, false);
    uint32_t uniqueId = spawned->m_uniqueId;

    CStruct* entry = NewParams();
    entry->PutNameKey(kName, uniqueId);
    entry->PutNameKey(kDestroyFunc, kKillSpawnedScript);
    CStruct* destroyParams = NewParams();
    destroyParams->PutNameKey(kId, uniqueId);
    entry->PutSubStruct(kDestroyParams, destroyParams);
    DeleteParams(destroyParams);
    CFunc_AddToCutsceneDestroyList(entry, nullptr);
    DeleteParams(entry);
    return true;
}

// 0x005B5BE0: FlushCutsceneDestroyList: each entry's destroy script run (with its parameters, on no object) and the
// entry deleted, then the list emptied (std::list::erase, 0x004ECC30). Always true.
bool __cdecl CFunc_FlushCutsceneDestroyList(Scripts::CStruct*, Scripts::CScript*)
{
    PointerList& list = FreeNodeChain();
    Flushing() = true;
    Node* head = list.m_head;
    for (Node* node = head ? head->next : nullptr; node != head; node = node->next) {
        CutsceneDestroyEntry* entry = EntryOf(node);
        Scripts::LaunchScript(entry->m_destroyScript, entry->m_params, nullptr, false, nullptr);
        DeleteEntry(EntryOf(node));
    }
    Node* result;
    reinterpret_cast<GameObjects::PointerList*>(&list)->Erase(
        reinterpret_cast<GameObjects::PointerListNode**>(&result),
        reinterpret_cast<GameObjects::PointerListNode*>(list.m_head ? list.m_head->next : nullptr),
        reinterpret_cast<GameObjects::PointerListNode*>(list.m_head));
    Flushing() = false;
    return true;
}

} // namespace ScriptCommands

namespace ScriptCommands {

// 0x00644A00 (a static initialiser): the cutscene destroy list, empty.
void ConstructCutsceneDestroyList() { g_cutsceneDestroyList.Construct(); }

} // namespace ScriptCommands

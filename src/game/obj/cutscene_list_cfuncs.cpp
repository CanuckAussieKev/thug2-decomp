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

namespace CFuncs {

bool g_cutsceneListFlushing;
PointerList g_cutsceneDestroyList;

namespace {

using Script::CStruct;
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

PointerList& DestroyList() { return g_cutsceneDestroyList; }
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
        Script::DeleteStruct(params);
        entry->m_params = nullptr;
    }
    Mem::Manager::Get()->Free(entry);
}

// A new structure (`new CStruct`), deleted with Clear and DeleteStruct.
CStruct* NewParams()
{
    CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteParams(CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

} // namespace

// 0x005B4650
PointerList* PointerList::Construct()
{
    m_size = 0;
    auto* node = static_cast<Node*>(Mem::New(sizeof(Node))); // (operator new inlined)
    if (node) {
        node->next = node;
        node->prev = node;
    }
    m_head = node;
    return this;
}

// 0x005B5320: AddToCutsceneDestroyList name=<object> destroy_func=<script> [destroy_params={...}]: an entry (with its
// own copy of the parameters) added at the end of the list. Always true.
bool __cdecl CFunc_AddToCutsceneDestroyList(Script::CStruct* params, Script::CScript*)
{
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    uint32_t destroyScript;
    params->GetChecksum(kDestroyFunc, &destroyScript, 1);
    CStruct* destroyParams = nullptr;
    params->GetStructure(kDestroyParams, &destroyParams, 0);
    // (the original walks the list to its end first, finding nothing)
    void* memory = Mem::Calloc(sizeof(CutsceneDestroyEntry));
    auto* entry = memory ? static_cast<CutsceneDestroyEntry*>(memory)->Construct() : nullptr;
    entry->m_name = name;
    entry->m_destroyScript = destroyScript;
    if (destroyParams)
        entry->m_params->AppendStructureIfAny(destroyParams);

    // push_back
    PointerList& list = DestroyList();
    Node* head = list.m_head;
    auto* node = static_cast<Node*>(Mem::New(sizeof(Node))); // (operator new inlined)
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
bool __cdecl CFunc_RemoveFromCutsceneDestroyList(Script::CStruct* params, Script::CScript*)
{
    if (Flushing())
        return false;
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    PointerList& list = DestroyList();
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
        Mem::Manager::Get()->Free(node);
        --list.m_size;
    }
    return true;
}

// 0x005B5510: Cutscene_SpawnScript <script> [params={...}] [id=<id>] [callback=<script>] [CallbackParams={...}]
// [NetEnabled] [permanent] [NotSessionSpecific=n]: the script spawned as SpawnScript does (the id defaulting to the
// calling script's .qb file - its symbol's +8), and an entry added to the cutscene destroy list that kills it
// (cutscene_kill_spawned_script with its unique id). The spawned script isn't checked for null. Always true.
bool __cdecl CFunc_Cutscene_SpawnScript(Script::CStruct* params, Script::CScript* script)
{
    Script::PrintContents(params, 0);
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    uint32_t id = Script::FindSymbol(script->m_scriptName)->unknown8;
    params->GetChecksum(kId, &id, 0);
    uint32_t callback = 0;
    params->GetChecksum(kCallback, &callback, 0);
    CStruct* callbackParams = nullptr;
    params->GetStructure(kCallbackParams, &callbackParams, 0);
    bool netEnabled = params->ContainsFlag(kNetEnabled);
    bool permanent = params->ContainsFlag(kPermanent);
    int notSessionSpecific = 0;
    params->GetInteger(kNotSessionSpecific, &notSessionSpecific, 0);
    Script::CScript* spawned = Script::SpawnScript(name, scriptParams, callback, callbackParams, script->m_node, id,
                                                   netEnabled, permanent, notSessionSpecific != 0, false);
    uint32_t uniqueId = spawned->m_uniqueId;

    CStruct* entry = NewParams();
    entry->AddChecksum(kName, uniqueId);
    entry->AddChecksum(kDestroyFunc, kKillSpawnedScript);
    CStruct* destroyParams = NewParams();
    destroyParams->AddChecksum(kId, uniqueId);
    entry->AddStructure(kDestroyParams, destroyParams);
    DeleteParams(destroyParams);
    CFunc_AddToCutsceneDestroyList(entry, nullptr);
    DeleteParams(entry);
    return true;
}

// 0x005B5BE0: FlushCutsceneDestroyList: each entry's destroy script run (with its parameters, on no object) and the
// entry deleted, then the list emptied (std::list::erase, 0x004ECC30). Always true.
bool __cdecl CFunc_FlushCutsceneDestroyList(Script::CStruct*, Script::CScript*)
{
    PointerList& list = DestroyList();
    Flushing() = true;
    Node* head = list.m_head;
    for (Node* node = head ? head->next : nullptr; node != head; node = node->next) {
        CutsceneDestroyEntry* entry = EntryOf(node);
        Script::RunScript(entry->m_destroyScript, entry->m_params, nullptr, false, nullptr);
        DeleteEntry(EntryOf(node));
    }
    Node* result;
    reinterpret_cast<Obj::PointerList*>(&list)->Erase(
        reinterpret_cast<Obj::PointerListNode**>(&result),
        reinterpret_cast<Obj::PointerListNode*>(list.m_head ? list.m_head->next : nullptr),
        reinterpret_cast<Obj::PointerListNode*>(list.m_head));
    Flushing() = false;
    return true;
}

} // namespace CFuncs

namespace CFuncs {

// 0x00644A00 (a static initialiser): the cutscene destroy list, empty.
void ConstructCutsceneDestroyList() { g_cutsceneDestroyList.Construct(); }

} // namespace CFuncs

// The cutscene destroy list's commands and the std::list constructor (see cutscene_list_cfuncs.h).

#include "engine/objects/cutscene_list_cfuncs.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/movie_cfuncs.h"
#include "engine/objects/pager_message.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sys/startup_init.h"

namespace commands {

bool g_cutscene_list_flushing;
PointerList g_cutscene_destroy_list;

namespace {

using script::Struct;
using Elem = PointerList::Elem;

constexpr uint32_t kName = 0xA1DC81F9;               // "name"
constexpr uint32_t kDestroyFunc = 0x589B2282;        // "destroy_func"
constexpr uint32_t kDestroyParams = 0x9C2E3A57;      // "destroy_params"
constexpr uint32_t kParams = 0x7031F10C;             // "params"
constexpr uint32_t kId = 0x40C698AF;                 // "id"
constexpr uint32_t kCallback = 0x86068BD9;           // "callback"
constexpr uint32_t kCallbackParams = 0x0DD85BDA;     // "CallbackParams"
constexpr uint32_t kOnlineFlag = 0x20209C31;         // "online_mode_flag"
constexpr uint32_t kPermanent = 0x23627FD7;          // "permanent"
constexpr uint32_t kNotSessionSpecific = 0x2EBFF4BC; // "NotSessionSpecific"
constexpr uint32_t kKillSpawnedScript = 0xD7034230;  // "cutscene_kill_spawned_script"

PointerList& free_node_chain() { return g_cutscene_destroy_list; }
// Set while the list is flushed (RemoveFromCutsceneDestroyList does nothing then).
bool& flushing() { return g_cutscene_list_flushing; }

CutsceneDestroyEntry* entry_of(Elem* node) { return static_cast<CutsceneDestroyEntry*>(node->value); }

// `delete entry` (inlined): its parameters deleted, then the entry freed.
void delete_entry(CutsceneDestroyEntry* entry)
{
    if (!entry)
        return;
    if (Struct* params = entry->args_) {
        params->get_clear();
        script::delete_struct(params);
        entry->args_ = nullptr;
    }
    memory::Hub::get_pull()->dealloc(entry);
}

// A new structure (`new CStruct`), deleted with Clear and delete_struct.
Struct* new_params()
{
    Struct* memory = script::spawn_param_bag();
    return memory ? memory->prepare_all() : nullptr;
}
void delete_params(Struct* params)
{
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

} // namespace

// 0x005B4650
PointerList* PointerList::make_new()
{
    extent_ = 0;
    auto* node = static_cast<Elem*>(memory::alloc(sizeof(Elem))); // (operator new inlined)
    if (node) {
        node->next = node;
        node->prev = node;
    }
    head_ = node;
    return this;
}

// 0x005B5320: AddToCutsceneDestroyList name=<object> destroy_func=<script> [destroy_params={...}]: an entry (with its
// own copy of the parameters) added at the end of the list. Always true.
bool __cdecl cfunc_add_to_cutscene_destroy_list(script::Struct* params, script::Script*)
{
    uint32_t name;
    params->read_hash_value(kName, &name, 1);
    uint32_t destroy_script;
    params->read_hash_value(kDestroyFunc, &destroy_script, 1);
    Struct* destroy_params = nullptr;
    params->read_sub_struct(kDestroyParams, &destroy_params, 0);
    // (the original walks the list to its end first, finding nothing)
    void* memory = memory::alloc_zeroed(sizeof(CutsceneDestroyEntry));
    auto* entry = memory ? new (memory) CutsceneDestroyEntry : nullptr;
    entry->label_ = name;
    entry->destroy_script_ = destroy_script;
    if (destroy_params)
        entry->args_->append_structure_if_any(destroy_params);

    // push_back
    PointerList& list = free_node_chain();
    Elem* head = list.head_;
    auto* node = static_cast<Elem*>(memory::alloc(sizeof(Elem))); // (operator new inlined)
    if (node) {
        node->value = entry;
        node->next = head;
        node->prev = head->prev;
    }
    ++list.extent_;
    head->prev = node;
    node->prev->next = node;
    return true;
}

// 0x005B5450: RemoveFromCutsceneDestroyList name=<object>: the object's entry deleted and taken out of the list (not
// run). False while the list is being flushed or without such an entry.
bool __cdecl cfunc_remove_from_cutscene_destroy_list(script::Struct* params, script::Script*)
{
    if (flushing())
        return false;
    uint32_t name;
    params->read_hash_value(kName, &name, 1);
    PointerList& list = free_node_chain();
    Elem* head = list.head_;
    Elem* node = head ? head->next : nullptr;
    for (; node != head; node = node->next)
        if (entry_of(node)->label_ == name)
            break;
    if (node == head)
        return false;
    delete_entry(entry_of(node));
    if (node != list.head_) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        memory::Hub::get_pull()->dealloc(node);
        --list.extent_;
    }
    return true;
}

// 0x005B5510: Cutscene_SpawnScript <script> [params={...}] [id=<id>] [callback=<script>] [CallbackParams={...}]
// [online_mode_flag] [permanent] [NotSessionSpecific=n]: the script spawned as SpawnScript does (the id defaulting to the
// calling script's .qb file - its symbol's +8), and an entry added to the cutscene destroy list that kills it
// (cutscene_kill_spawned_script with its unique id). The spawned script isn't checked for null. Always true.
bool __cdecl cfunc_cutscene_spawn_script(script::Struct* params, script::Script* script)
{
    script::dump_to_log(params, 0);
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    uint32_t id = script::find_symbol(script->script_title_)->unknown8;
    params->read_hash_value(kId, &id, 0);
    uint32_t callback = 0;
    params->read_hash_value(kCallback, &callback, 0);
    Struct* callback_params = nullptr;
    params->read_sub_struct(kCallbackParams, &callback_params, 0);
    bool replicate_flag = params->has_bare_flag(kOnlineFlag);
    bool permanent = params->has_bare_flag(kPermanent);
    int not_session_specific = 0;
    params->fetch_int_value(kNotSessionSpecific, &not_session_specific, 0);
    script::Script* spawned = script::spawn_script(name, script_params, callback, callback_params, script->list_link_,
                                                   id, replicate_flag, permanent, not_session_specific != 0, false);
    uint32_t unique_id = spawned->unique_id_;

    Struct* entry = new_params();
    entry->put_name_key(kName, unique_id);
    entry->put_name_key(kDestroyFunc, kKillSpawnedScript);
    Struct* destroy_params = new_params();
    destroy_params->put_name_key(kId, unique_id);
    entry->put_sub_struct(kDestroyParams, destroy_params);
    delete_params(destroy_params);
    cfunc_add_to_cutscene_destroy_list(entry, nullptr);
    delete_params(entry);
    return true;
}

// 0x005B5BE0: FlushCutsceneDestroyList: each entry's destroy script run (with its parameters, on no object) and the
// entry deleted, then the list emptied (std::list::erase, 0x004ECC30). Always true.
bool __cdecl cfunc_flush_cutscene_destroy_list(script::Struct*, script::Script*)
{
    PointerList& list = free_node_chain();
    flushing() = true;
    Elem* head = list.head_;
    for (Elem* node = head ? head->next : nullptr; node != head; node = node->next) {
        CutsceneDestroyEntry* entry = entry_of(node);
        script::launch_script(entry->destroy_script_, entry->args_, nullptr, false, nullptr);
        delete_entry(entry_of(node));
    }
    Elem* result;
    reinterpret_cast<objects::PointerList*>(&list)->get_erase(
        reinterpret_cast<objects::PointerListNode**>(&result),
        reinterpret_cast<objects::PointerListNode*>(list.head_ ? list.head_->next : nullptr),
        reinterpret_cast<objects::PointerListNode*>(list.head_));
    flushing() = false;
    return true;
}

} // namespace commands

namespace commands {

// 0x00644A00 (a static initialiser): the cutscene destroy list, empty.
void construct_cutscene_destroy_list() { g_cutscene_destroy_list.make_new(); }

} // namespace commands

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
#include "engine/core/no_destroy.h"
namespace commands {

namespace {
// NOT ORIGINAL grouping - cutscene_list_cfuncs.cpp's state (the original's globals). Made on first use, never destroyed
// (cutscene_list_cfuncs_state()).
struct CutsceneListCfuncsState {
    bool cutscene_list_flushing_{};
    CutsceneDestroyList cutscene_destroy_list_{};
};

CutsceneListCfuncsState& cutscene_list_cfuncs_state()
{
    static core::NoDestroy<CutsceneListCfuncsState> state;
    return state.get();
}

using script::Struct;
using Elem = CutsceneDestroyList::Node;

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

CutsceneDestroyList& free_node_chain() { return cutscene_list_cfuncs_state().cutscene_destroy_list_; }
// Set while the list is flushed (RemoveFromCutsceneDestroyList does nothing then).
bool& flushing() { return cutscene_list_cfuncs_state().cutscene_list_flushing_; }

CutsceneDestroyEntry* entry_of(const Elem* node) { return node->value; }

// `delete entry` (inlined): its parameters deleted, then the entry freed.
void delete_entry(CutsceneDestroyEntry* entry)
{
    if (!entry)
        return;
    if (Struct* params = entry->args()) {
        params->get_clear();
        script::delete_struct(params);
        entry->set_args(nullptr);
    }
    memory::Hub::get_pull()->dealloc(entry);
}

// A new structure (`new CStruct`), deleted with Clear and delete_struct.

} // namespace

// 0x005B5320: AddToCutsceneDestroyList name=<object> destroy_func=<script> [destroy_params={...}]: an entry (with its
// own copy of the parameters) added at the end of the list. Always true.
bool cfunc_add_to_cutscene_destroy_list(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    uint32_t destroy_script = 0;
    params->read_hash_value(kDestroyFunc, &destroy_script, 1);
    Struct* destroy_params = nullptr;
    params->read_sub_struct(kDestroyParams, &destroy_params, 0);
    // (the original walks the list to its end first, finding nothing)
    void* memory = memory::alloc_zeroed(sizeof(CutsceneDestroyEntry));
    auto* entry = memory ? memory::construct_in<CutsceneDestroyEntry>(memory) : nullptr;
    entry->set_label(name);
    entry->set_destroy_script(destroy_script);
    if (destroy_params)
        entry->args()->append_structure_if_any(destroy_params);

    // push_back
    CutsceneDestroyList& list = free_node_chain();
    Elem* head = list.head();
    auto* node = static_cast<Elem*>(memory::alloc(sizeof(Elem))); // (operator new inlined)
    if (node) {
        node->value = entry;
        node->next = head;
        node->prev = head->prev;
    }
    ++list.size();
    head->prev = node;
    node->prev->next = node;
    return true;
}

// 0x005B5450: RemoveFromCutsceneDestroyList name=<object>: the object's entry deleted and taken out of the list (not
// run). False while the list is being flushed or without such an entry.
bool cfunc_remove_from_cutscene_destroy_list(script::Struct* params, script::Script*)
{
    if (flushing())
        return false;
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    CutsceneDestroyList& list = free_node_chain();
    const Elem* head = list.head();
    Elem* node = head ? head->next : nullptr;
    for (; node != head; node = node->next)
        if (entry_of(node)->label() == name)
            break;
    if (node == head)
        return false;
    delete_entry(entry_of(node));
    if (node != list.head()) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        memory::Hub::get_pull()->dealloc(node);
        --list.size();
    }
    return true;
}

// 0x005B5510: Cutscene_SpawnScript <script> [params={...}] [id=<id>] [callback=<script>] [CallbackParams={...}]
// [online_mode_flag] [permanent] [NotSessionSpecific=n]: the script spawned as SpawnScript does (the id defaulting to the
// calling script's .qb file - its symbol's +8), and an entry added to the cutscene destroy list that kills it
// (cutscene_kill_spawned_script with its unique id). The spawned script isn't checked for null. Always true.
bool cfunc_cutscene_spawn_script(script::Struct* params, script::Script* script)
{
    script::dump_to_log(params, 0);
    uint32_t name = 0;
    params->read_hash_value(0u, &name, 0);
    Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    uint32_t id = script::find_symbol(script->script_title())->unknown8();
    params->read_hash_value(kId, &id, 0);
    uint32_t callback = 0;
    params->read_hash_value(kCallback, &callback, 0);
    Struct* callback_params = nullptr;
    params->read_sub_struct(kCallbackParams, &callback_params, 0);
    const bool replicate_flag = params->has_bare_flag(kOnlineFlag);
    const bool permanent = params->has_bare_flag(kPermanent);
    int not_session_specific = 0;
    params->fetch_int_value(kNotSessionSpecific, &not_session_specific, 0);
    const script::Script* spawned =
        script::spawn_script(name, script_params, callback, callback_params, script->list_link(), id, replicate_flag,
                             permanent, not_session_specific != 0, false);
    const uint32_t unique_id = spawned->unique_id();

    script::StructPtr entry = script::make_struct();
    entry->put_name_key(kName, unique_id);
    entry->put_name_key(kDestroyFunc, kKillSpawnedScript);
    script::StructPtr destroy_params = script::make_struct();
    destroy_params->put_name_key(kId, unique_id);
    entry->put_sub_struct(kDestroyParams, destroy_params.get());
    destroy_params.reset();
    cfunc_add_to_cutscene_destroy_list(entry.get(), nullptr);
    entry.reset();
    return true;
}

// 0x005B5BE0: FlushCutsceneDestroyList: each entry's destroy script run (with its parameters, on no object) and the
// entry deleted, then the list emptied (std::list::erase, 0x004ECC30). Always true.
bool cfunc_flush_cutscene_destroy_list(script::Struct*, script::Script*)
{
    CutsceneDestroyList& list = free_node_chain();
    flushing() = true;
    const Elem* head = list.head();
    for (const Elem* node = head ? head->next : nullptr; node != head; node = node->next) {
        const CutsceneDestroyEntry* entry = entry_of(node);
        script::launch_script(entry->destroy_script(), entry->args(), nullptr, false, nullptr);
        delete_entry(entry_of(node));
    }
    list.erase(list.first(), list.head());
    flushing() = false;
    return true;
}

} // namespace commands

namespace commands {

// 0x00644A00 (a static initialiser): the cutscene destroy list, empty.
void construct_cutscene_destroy_list() { cutscene_list_cfuncs_state().cutscene_destroy_list_.make(); }

} // namespace commands

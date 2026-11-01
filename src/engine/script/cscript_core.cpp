// CScript's life: construction and destruction, the pooled new / delete, stopping (reset_script_state), setting the
// script to run, and the small accessors. See cscript.h for the class and docs/script-runtime.md.

#include "engine/script/cscript.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/objects/signal.h"
#include "engine/objects/tracker.h"
#include "engine/script/cache.h"
#include "engine/script/token_reader.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"

namespace script {

Script* g_running_script;
Script* g_script_list_head;
bool g_one_per_frame_released;
int g_script_count;
uint32_t g_next_script_unique_id;
bool g_updating_spawned_scripts;
bool g_delete_spawned_scripts_pending;
memory::PoolPair g_script_pools;

namespace {

// The CScript pool pair (300 items, made by the script system's start-up).
memory::PoolPair& script_pools() { return g_script_pools; }

// A new empty structure from the structure pool (null if there's no room).
Struct* new_empty_struct()
{
    Struct* structure = spawn_param_bag();
    return structure ? memory::construct_in<script::Struct>(structure) : nullptr;
}
// `delete structure` (its destructor is Clear).
void free_struct(Struct* structure)
{
    structure->get_clear();
    delete_struct(structure);
}

} // namespace

// 0x004738E0
Script* Script::make_new()
{
    entity_.preceding_ = nullptr;
    entity_.following_ = nullptr;
    entity_.owner_object_ = nullptr;
    new (&shared_base_) objects::SharedBase;
    // Then all of it is zeroed - shared_base_'s vptr too (an original quirk, kept: nothing calls through it).
    // NOLINTNEXTLINE(bugprone-undefined-memory-manipulation): the original's whole-object clear / copy
    std::memset(this, 0, sizeof(Script));
    following_ = script_list_head();
    if (script_list_head())
        script_list_head()->prior_entry_ = this;
    unique_id_ = next_script_unique_id();
    ++script_count();
    ++next_script_unique_id();
    list_link_ = 0;
    signal_routes_ = nullptr;
    frame_records_ = inline_return_addresses_;
    on_exception_script_ = 0;
    on_exit_script_ = 0;
    loops_ = &inline_loop_;
    script_list_head() = this;
    return this;
}

// 0x00474420
void Script::teardown()
{
    reset_script_state();
    report_stopped(); // (inlined in the original)
    if (callback_params_)
        free_struct(callback_params_);

    if (!prior_entry_)
        script_list_head() = following_;
    if (following_)
        following_->prior_entry_ = prior_entry_;
    if (prior_entry_)
        prior_entry_->following_ = following_;

    forget_stored_randoms_of(reinterpret_cast<uint32_t>(this));
    if (objects::SignalRouteTable* table = signal_routes_) {
        if (table->in_use_) {
            // Its handlers are being run: it is marked dead and freed afterwards (by forward_trigger).
            table->alive_ = false;
        } else {
            table->~SignalRouteTable();
            memory::Hub::get_pull()->dealloc(table);
        }
    }
    --script_count();
    shared_base_.clear_smart_pointers(); // (its destructor's body; the vptr was zeroed above)
    entity_.get_release();
}

// 0x00449970
void* Script::operator new(size_t size) { return script_pools().carve(static_cast<unsigned int>(size)); }

// 0x004499B0
void Script::operator delete(void* script) { script_pools().dealloc(script); }

Script* Script::create()
{
    void* memory = operator new(sizeof(Script));
    return memory ? static_cast<Script*>(memory)->make_new() : nullptr;
}

void Script::destroy(Script* script)
{
    if (!script)
        return;
    script->teardown();
    operator delete(script);
}

// 0x00473A80
void Script::reset_script_state()
{
    run_on_exit_scripts();
    if (code_copy_) {
        memory::free_malloc(code_copy_);
        code_copy_ = nullptr;
    }
    ScriptStore* cache = script_store_instance();
    // (released even when the code was a private copy or given to bind_script, which took no use of the cached
    // code: if that script is cached, a use someone else took is given back - an original quirk, kept)
    if (pc_)
        cache->release_script(script_title_);
    pc_ = nullptr;
    if (args_) {
        free_struct(args_);
        args_ = nullptr;
    }
    if (stmt_args_) {
        free_struct(stmt_args_);
        stmt_args_ = nullptr;
    }
    for (int i = 0; i < call_depth_; ++i) {
        cache->release_script(frame_records_[i].script_id_);
        if (Struct* params = frame_records_[i].arg_struct_) {
            free_struct(params);
            frame_records_[i].arg_struct_ = nullptr;
        }
    }
    call_depth_ = 0;
    if (frame_records_ != inline_return_addresses_)
        memory::free_malloc(frame_records_);
    frame_records_ = inline_return_addresses_;
    innermost_repeat_ = nullptr;
    if (loops_ != &inline_loop_)
        memory::free_malloc(loops_);
    loops_ = &inline_loop_;
    suspend_frames_left_ = 0;
    suspend_reason_ = kSuspendOnNothing;
    blocking_module_ = nullptr;
    suspend_arg_ = 0;
    entity_.get_release();
    script_title_ = 0;
    flag_bits_ &= ~(kScriptRunInterrupted | kScriptRunWaitUpdated);
}

// 0x00473880
void Script::run_on_exit_scripts()
{
    if (const uint32_t name = on_exit_script_) {
        on_exit_script_ = 0;
        preempt(name, nullptr);
    }
    // From the top level down; the depth is read once (the interrupts push and pop levels of their own).
    for (int i = call_depth_; i; --i) {
        uint32_t& on_exit = frame_records_[i - 1].on_exit_script_ptr_;
        if (const uint32_t name = on_exit) {
            on_exit = 0;
            preempt(name, nullptr);
        }
    }
}

// 0x00473010
void Script::report_stopped()
{
    if (signal_routes_) {
        signal_routes_->unregister_with_tracker(this);
        signal_routes_->remove_group(objects::kAllGroups);
    }
    if (spawn_flags_ & kScriptFlagTrackerBlocked)
        objects::get_tracker()->forget_script(this);
}

// 0x004739D0
Struct* Script::copy_params_if_owned(Struct* params)
{
    if (!params)
        return nullptr;
    if ((stmt_args_ && stmt_args_->ref_tally(params)) || (args_ && args_->ref_tally(params))) {
        Struct* copy = new_empty_struct();
        copy->append_structure_if_any(params); // (no check for a full pool)
        return copy;
    }
    return nullptr;
}

// 0x00474540
void Script::bind_script(uint32_t name, const uint8_t* code, Struct* params, objects::Entity* object)
{
    // reset_script_state frees this script's parameters: keep what `params` needs.
    Struct* copy = copy_params_if_owned(params);
    reset_script_state();
    stmt_args_ = new_empty_struct();
    script_title_ = name;
    entity_.assign_value(object);
    args_ = new_empty_struct();
    pc_ = code ? code : script_store_instance()->fetch_script(name);
    if (!pc_) {
        // No such script.
        if (copy)
            free_struct(copy);
        reset_script_state();
        report_stopped();
        return;
    }
    // The first line: the script's default parameters.
    pc_ = parse_arguments(args_, pc_, nullptr);
    if (copy) {
        args_->append_structure_if_any(copy);
        free_struct(copy);
    } else {
        args_->append_structure_if_any(params);
    }
}

// 0x004747A0
void Script::bind_script(uint32_t name, Struct* params, objects::Entity* object)
{
    bind_script(name, nullptr, params, object);
}

// 0x004747C0
void Script::bind_script(const char* name, Struct* params, objects::Entity* object)
{
    bind_script(checksums::hash_from_text(name), nullptr, params, object);
}

// 0x00474720
void Script::set_script_copy(ParamScript* script, Struct* params, objects::Entity* object)
{
    auto size = static_cast<uint32_t>(skip_to_end_of_script(script->code_stream_) - script->code_stream_);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    auto* copy = static_cast<uint8_t*>(memory::alloc_block(size));
    memory::Hub::get_pull()->leave_heap_scope();
    const uint8_t* source = script->code_stream_;
    for (uint32_t i = 0; i < size; ++i)
        copy[i] = source[i];
    bind_script(script->own_name_hash_, copy, params, object);
    code_copy_ = copy; // (after bind_script: its reset_script_state would free it)
}

// 0x004747F0
void Script::relaunch()
{
    Struct* params = new_empty_struct();
    uint32_t name = 0;
    objects::Entity* object = nullptr;
    if (call_depth_) {
        // The bottom level: the script it was started with.
        params->get_assign(frame_records_[0].arg_struct_);
        object = frame_records_[0].owner_entity_;
        name = frame_records_[0].script_id_;
    } else {
        params->get_assign(args_);
        object = entity_.get_pull();
        name = script_title_;
    }
    bind_script(name, nullptr, params, object);
    if (params)
        free_struct(params);
}

// 0x00472C90
bool Script::all_scripts_still_defined()
{
    if (call_depth_ == 0)
        return code_copy_ || find_symbol(script_title_);
    if (!find_symbol(script_title_))
        return false;
    // (the bottom level's script isn't looked for when it runs a private copy)
    for (int i = code_copy_ ? 1 : 0; i < call_depth_; ++i) {
        if (!find_symbol(frame_records_[i].script_id_))
            return false;
    }
    return true;
}

// 0x00472940
bool Script::has_attached_script() { return pc_ != nullptr; }

// 0x00472F90
void Script::set_spawner_file(Script* spawner)
{
    const uint32_t name = spawner->call_depth_ ? spawner->frame_records_[0].script_id_ : spawner->script_title_;
    spawner_file_ = find_symbol(name)->unknown8; // (no check that it is still defined)
}

// 0x00473BE0
void Script::object_destroyed(objects::Entity* object)
{
    if (!object)
        return;
    if (entity_.get_pull() == object)
        entity_.get_release();
    for (int i = 0; i < call_depth_; ++i) {
        if (frame_records_[i].owner_entity_ == object)
            frame_records_[i].owner_entity_ = nullptr;
    }
    suspend_reason_ = kSuspendOnNothing; // (whatever it was waiting for)
}

// 0x00472ED0 / 0x00472EE0 / 0x00472EF0
void Script::set_on_exception_script(uint32_t name) { on_exception_script_ = name; }
uint32_t Script::get_on_exception_script() { return on_exception_script_; }
void Script::set_on_exit_script(uint32_t name) { on_exit_script_ = name; }

// 0x00472F00
void Script::clear_on_exit_scripts()
{
    on_exit_script_ = 0;
    for (int i = call_depth_; i; --i)
        frame_records_[i - 1].on_exit_script_ptr_ = 0;
}

// 0x004734B0
Script* find_script_with_unique_id(uint32_t id)
{
    for (Script* script = script_list_head(); script; script = script->following_) {
        if (script->unique_id_ == id)
            return script;
    }
    return nullptr;
}

} // namespace script

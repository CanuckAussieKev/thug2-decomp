// CScript's life: construction and destruction, the pooled new / delete, stopping (reset_script_state), setting the
// script to run, and the small accessors. See cscript.h for the class and docs/script-runtime.md.

#include "engine/script/cscript.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>

#include "base/core/checksums.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/slab_bank.h"
#include "engine/objects/signal.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/tracker.h"
#include "engine/script/cache.h"
#include "engine/script/token_reader.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "engine/script/param_struct.h"
#include "engine/objects/entity.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include <gsl/pointers>
#include "base/core/raw_view.h"

namespace script {

namespace {

// NOT ORIGINAL grouping - the scripts' list and counters (the original's globals). Made on first use, never destroyed
// (script_run_state()).
struct ScriptRunState {
    Script* running_script_{};
    Script* script_list_head_{};
    bool one_per_frame_released_{};
    int script_count_{};
    uint32_t next_script_unique_id_{};
    bool updating_spawned_scripts_{};
    bool delete_spawned_scripts_pending_{};
};

ScriptRunState& script_run_state()
{
    static core::NoDestroy<ScriptRunState> state;
    return state.get();
}

} // namespace

Script*& script_list_head() { return script_run_state().script_list_head_; }
Script*& current_script_var() { return script_run_state().running_script_; }
int& script_count() { return script_run_state().script_count_; }
uint32_t& next_script_unique_id() { return script_run_state().next_script_unique_id_; }
bool& updating_spawned_scripts() { return script_run_state().updating_spawned_scripts_; }
bool& delete_spawned_scripts_pending() { return script_run_state().delete_spawned_scripts_pending_; }
bool& one_per_frame_released() { return script_run_state().one_per_frame_released_; }

namespace {

// The CScript pool pair (300 items, made by the script system's start-up).


// A new empty structure from the structure pool (null if there's no room).
// `delete structure` (its destructor is Clear).

} // namespace

// 0x004738E0
Script* Script::make_new()
{
    entity_.preceding_ = nullptr;
    entity_.following_ = nullptr;
    entity_.target_ = nullptr;
    new (&shared_base_) objects::SharedBase;
    // Then all of it is zeroed - shared_base_'s vptr too (an original quirk, kept: nothing calls through it).
    raw::zero_object(this);
    following_ = script_list_head();
    if (script_list_head()) {
        script_list_head()->prior_entry_ = this;
    }
    unique_id_ = next_script_unique_id();
    ++script_count();
    ++next_script_unique_id();
    list_link_ = 0;
    signal_routes_ = nullptr;
    frame_records_ = inline_return_addresses_.data();
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
    if (callback_params_) {
        memory::destroy_then_clear(callback_params_);
    }

    if (!prior_entry_) {
        script_list_head() = following_;
    }
    if (following_) {
        following_->prior_entry_ = prior_entry_;
    }
    if (prior_entry_) {
        prior_entry_->following_ = following_;
    }

    forget_stored_randoms_of(this);
    if (objects::SignalRouteTable* table = signal_routes_) {
        if (table->in_use() != 0) {
            // Its handlers are being run: it is marked dead and freed afterwards (by forward_trigger).
            table->set_alive(false);
        } else {
            table->~SignalRouteTable();
            memory::Hub::dealloc(table);
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
    if (!script) {
        return;
    }
    script->teardown();
    operator delete(script);
}

// 0x00473A80
void Script::reset_script_state()
{
    run_on_exit_scripts();
    if (code_copy_) {
        forget_code_block(code_copy_);
        memory::free_malloc(code_copy_);
        code_copy_ = nullptr;
    }
    ScriptStore* cache = script_store_instance();
    // (released even when the code was a private copy or given to bind_script, which took no use of the cached
    // code: if that script is cached, a use someone else took is given back - an original quirk, kept)
    if (pc_) {
        cache->release_script(script_title_);
    }
    pc_ = nullptr;
    if (args_) {
        memory::destroy_then_clear(args_);
    }
    if (stmt_args_) {
        memory::destroy_then_clear(stmt_args_);
    }
    for (int i = 0; i < call_depth_; ++i) {
        cache->release_script(core::at(call_stack(), i).script_id_);
        if (Struct* params = core::at(call_stack(), i).arg_struct_) {
            free_struct(memory::owned_element(params));
            core::at(call_stack(), i).arg_struct_ = nullptr;
        }
    }
    call_depth_ = 0;
    if (frame_records_ != inline_return_addresses_.data()) {
        memory::free_malloc(frame_records_);
    }
    frame_records_ = inline_return_addresses_.data();
    innermost_repeat_ = nullptr;
    if (loops_ != &inline_loop_) {
        memory::free_malloc(loops_);
    }
    loops_ = &inline_loop_;
    suspend_frames_left_ = 0;
    suspend_reason_ = BlockReason::kNothing;
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
    for (int i = call_depth_; i != 0; --i) {
        uint32_t& on_exit = core::at(call_stack(), i - 1).on_exit_script_ptr_;
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
    if ((spawn_flags_ & kScriptFlagTrackerBlocked) != 0) {
        objects::get_tracker()->forget_script(this);
    }
}

// 0x004739D0
gsl::owner<Struct*> Script::copy_params_if_owned(Struct* params)
{
    if (!params) {
        return nullptr;
    }
    if ((stmt_args_ && stmt_args_->ref_tally(params)) || (args_ && args_->ref_tally(params))) {
        gsl::owner<Struct*> copy = script::new_struct();
        copy->append_structure_if_any(params); // (no check for a full pool)
        return copy;
    }
    return nullptr;
}

// 0x00474540
void Script::bind_script(uint32_t name, const uint8_t* code, Struct* params, objects::Entity* object)
{
    // reset_script_state frees this script's parameters: keep what `params` needs.
    gsl::owner<Struct*> copy = copy_params_if_owned(params);
    reset_script_state();
    stmt_args_ = script::make_struct();
    script_title_ = name;
    entity_.assign_value(object);
    args_ = script::make_struct();
    pc_ = code ? code : script_store_instance()->fetch_script(name);
    if (!pc_) {
        // No such script.
        if (copy) {
            free_struct(copy);
        }
        reset_script_state();
        report_stopped();
        return;
    }
    // The first line: the script's default parameters.
    pc_ = parse_arguments(args_.get(), tokens_at(pc_), nullptr).address();
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
    const Tokens start = tokens_at(script->code_stream_);
    const std::span<const uint8_t> source = code_between(start, skip_to_end_of_script(start));
    const auto size = static_cast<uint32_t>(source.size());
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap());
    auto* block = static_cast<uint8_t*>(memory::alloc_block(size));
    memory::Hub::get_pull()->leave_heap_scope();
    const std::span<uint8_t> copy(block, size);
    note_code_block(copy);
    std::ranges::copy(source, copy.begin());
    bind_script(script->own_name_hash_, block, params, object);
    code_copy_ = block; // (after bind_script: its reset_script_state would free it)
}

// 0x004747F0
void Script::relaunch()
{
    gsl::owner<Struct*> params = script::new_struct();
    uint32_t name = 0;
    objects::Entity* object = nullptr;
    if (call_depth_ != 0) {
        // The bottom level: the script it was started with.
        params->get_assign(core::at(call_stack(), 0).arg_struct_);
        object = core::at(call_stack(), 0).owner_entity_;
        name = core::at(call_stack(), 0).script_id_;
    } else {
        params->get_assign(args_.get());
        object = entity_.get_pull();
        name = script_title_;
    }
    bind_script(name, nullptr, params, object);
    if (params) {
        free_struct(params);
    }
}

// 0x00472C90
bool Script::all_scripts_still_defined()
{
    if (call_depth_ == 0) {
        return (code_copy_ != nullptr) || (find_symbol(script_title_) != nullptr);
    }
    if (!find_symbol(script_title_)) {
        return false;
    }
    // (the bottom level's script isn't looked for when it runs a private copy)
    for (int i = code_copy_ ? 1 : 0; i < call_depth_; ++i) {
        if (find_symbol(core::at(call_stack(), i).script_id_) == nullptr) {
            return false;
        }
    }
    return true;
}

// 0x00472940
bool Script::has_attached_script() { return pc_ != nullptr; }

// 0x00472F90
void Script::set_spawner_file(Script* spawner)
{
    const uint32_t name =
        (spawner->call_depth_ != 0) ? core::at(spawner->call_stack(), 0).script_id_ : spawner->script_title_;
    spawner_file_ = find_symbol(name)->unknown8(); // (no check that it is still defined)
}

// 0x00473BE0
void Script::object_destroyed(objects::Entity* object)
{
    if (!object) {
        return;
    }
    if (entity_.get_pull() == object) {
        entity_.get_release();
    }
    for (int i = 0; i < call_depth_; ++i) {
        if (core::at(call_stack(), i).owner_entity_ == object) {
            core::at(call_stack(), i).owner_entity_ = nullptr;
        }
    }
    suspend_reason_ = BlockReason::kNothing; // (whatever it was waiting for)
}

// 0x00472ED0 / 0x00472EE0 / 0x00472EF0
void Script::set_on_exception_script(uint32_t name) { on_exception_script_ = name; }
uint32_t Script::get_on_exception_script() const { return on_exception_script_; }
void Script::set_on_exit_script(uint32_t name) { on_exit_script_ = name; }

// 0x00472F00
void Script::clear_on_exit_scripts()
{
    on_exit_script_ = 0;
    for (int i = call_depth_; i != 0; --i) {
        core::at(call_stack(), i - 1).on_exit_script_ptr_ = 0;
    }
}

// 0x004734B0
Script* find_script_with_unique_id(uint32_t id)
{
    for (Script* script = script_list_head(); script; script = script->following()) {
        if (script->unique_id() == id) {
            return script;
        }
    }
    return nullptr;
}

} // namespace script

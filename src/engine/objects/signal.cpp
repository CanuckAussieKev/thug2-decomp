// Events, event listeners and the scripts' event handler tables (0x0045F7F0-0x00460792). See signal.h and
// docs/script-runtime.md section 11 (how an event reaches a script's handlers).

#include <cstdint>

#include "engine/objects/signal.h"

#include <cstddef>
#include <span>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/entity.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace objects {

namespace {
constexpr uint32_t kController = 0xB30D9965; // Controller

using script::Script;
using script::Struct;

// Handler structure components (add_handlers): `params`, `group`, `exception`.
constexpr uint32_t kParamsName = 0x7031F10C;
constexpr uint32_t kGroupName = 0x923FBB3A;
constexpr uint32_t kExceptionFlag = 0x80367192;

// The tracker (0x006F0FC0, tracker.h): which scripts listen for which event types, the event listeners, the
// event log.
void tracker_add_listener(uint32_t type, Script* script) { get_tracker()->add_listener(type, script); }
void tracker_remove_listener(uint32_t type, Script* script) { get_tracker()->remove_listener(type, script); }
void tracker_register_listener(SignalReceiver* listener) { get_tracker()->subscribe_receiver(listener); }
void tracker_unregister_listener(SignalReceiver* listener) { get_tracker()->remove_event_listener(listener); }
void tracker_log_event_handled(Signal* event, uint32_t how, uint32_t script)
{
    get_tracker()->log_handled(event, how, script);
}
void tracker_log_event_read(Signal* event, uint32_t how, uint32_t script)
{
    get_tracker()->log_read(event, how, script);
}

// `new CStruct` / `new CStruct(*source)` (null if the pool is full) and `delete structure`.
Struct* new_struct_copy(Struct* source)
{
    gsl::owner<Struct*> structure = script::spawn_param_bag();
    return structure ? structure->init_copy(source) : nullptr;
}
// An entry's parameters go (and the pointer is cleared).
void free_params(SignalRouteTable::TableRow& entry)
{
    if (entry.params) {
        free_struct(memory::owned_element(entry.params)); // (the row's copy)
        entry.params = nullptr;
    }
}

} // namespace

// 0x0045F7F0
Signal::Signal(uint32_t type, uint32_t source, uint32_t target, Struct* data)
    : kind_tag_(type), from_where_(source), aim_point_(target), payload_(data)
{
}

// 0x0045F980
void Signal::set_event_consumed(uint32_t how, uint32_t script)
{
    tracker_log_event_read(this, how, script);
    flag_bits_ |= kFlagRead;
}

// 0x0045F9B0
void Signal::set_event_handled(uint32_t how, uint32_t script)
{
    tracker_log_event_handled(this, how, script);
    flag_bits_ |= kFlagHandled;
}

// 0x0045F9E0
int event_controller(Signal* event)
{
    int controller = 0;
    if (event->payload())
        event->payload()->fetch_int_value(kController, &controller, 0); // "controller"
    return controller;
}

// 0x00460150
void Signal::teardown()
{
    if ((flag_bits_ & kFlagOwnsData) && payload_)
        free_struct(memory::owned_element(payload_)); // (kFlagOwnsData: the event's)
}

// 0x0045FAC0
SignalReceiver::~SignalReceiver()
{
    tracker_unregister_listener(this);
    owner_object_.get_release();
}

// 0x0045FB30
void SignalReceiver::enter_wait_board(Entity* object)
{
    owner_object_.assign_value(object);
    tracker_register_listener(this);
}

// 0x0045F820
SignalRouteTable::SignalRouteTable() = default;

// 0x0045FB70
SignalRouteTable::~SignalRouteTable()
{
    if (!entries_)
        return;
    for (int i = 0; i < num_; ++i) {
        if (Struct* params = core::at(entries(), i).params)
            free_struct(memory::owned_element(params)); // (the row's copy)
    }
    memory::Hub::get_pull()->dealloc(entries_);
}

// 0x0045F840
void SignalRouteTable::mark_group(uint32_t group)
{
    for (int i = 0; i < num_; ++i) {
        TableRow& entry = core::at(entries(), i);
        if (entry.group == group && entry.script != kNullScript)
            entry.flags |= TableRow::kFlagMarked;
    }
}

// 0x0045F880
int SignalRouteTable::list_wait_table(SignalRouteTable* table)
{
    debug::printf("====================================\n");
    debug::printf("Event Handler Table:\n");
    if (!table) {
        debug::printf("  Emtpy\n");
    } else {
        for (int i = 0; i < table->num_; ++i) {
            const TableRow& entry = core::at(table->entries(), i);
            debug::printf("------------------------------------\n");
            debug::printf("  Type     : %s\n", script::find_checksum_name_or_unknown(entry.type));
            debug::printf("  Script   : %s\n", script::find_checksum_name_or_unknown(entry.script));
            debug::printf("  Group    : %s\n", script::find_checksum_name_or_unknown(entry.group));
            debug::printf("  Exception: %s\n", (entry.flags & TableRow::kFlagReplacesRunning) ? "yes" : "no");
            if (entry.params) {
                debug::printf("  Params   :\n");
                script::dump_to_log(entry.params, 13);
            }
        }
    }
    return debug::printf("====================================\n");
}

// 0x0045FA10
void SignalRouteTable::enter_wait_board(Script* script)
{
    for (int i = 0; i < num_; ++i) {
        const TableRow& entry = core::at(entries(), i);
        if (entry.type && entry.script && entry.script != kNullScript)
            tracker_add_listener(entry.type, script);
    }
}

// 0x0045FA60 (removed entries included, as long as they still have a type)
void SignalRouteTable::unregister_with_tracker(Script* script)
{
    for (int i = 0; i < num_; ++i) {
        if (const uint32_t type = core::at(entries(), i).type)
            tracker_remove_listener(type, script);
    }
}

// 0x0045FBC0
void SignalRouteTable::append_listener(Script* script, ListenerKey listener_key,
                                       bool exception, Struct* params)
{
    const auto type = listener_key.type;
    const auto script_name = listener_key.script_name_;
    const auto group = listener_key.group;
    bool tell_tracker = true;
    TableRow* entry = nullptr;
    if (!entries_) {
        num_ = 1;
        entries_ = static_cast<TableRow*>(memory::alloc(sizeof(TableRow)));
        entry = &core::at(entries(), 0);
    } else {
        // The entry of that type (live or removed by group): reused; the tracker already knows a live one.
        for (int i = 0; i < num_; ++i) {
            if (core::at(entries(), i).type == type) {
                entry = &core::at(entries(), i);
                if (entry->params)
                    free_struct(memory::owned_element(entry->params)); // (the row's copy; the pointer is overwritten below)
                tell_tracker = entry->script == kNullScript;
                break;
            }
        }
        // Else the first removed entry.
        if (!entry) {
            for (int i = 0; i < num_; ++i) {
                if (core::at(entries(), i).script == kNullScript) {
                    entry = &core::at(entries(), i);
                    break;
                }
            }
        }
        // Else a bigger array (on the current heap), the new entry at the end.
        if (!entry) {
            const std::span<TableRow> old = entries();
            entries_ = static_cast<TableRow*>(memory::alloc((num_ + 1) * sizeof(TableRow)));
            ++num_;
            const std::span<TableRow> grown = entries();
            for (std::size_t i = 0; i < old.size(); ++i)
                core::at(grown, i) = core::at(old, i);
            memory::Hub::get_pull()->dealloc(old.data());
            entry = &grown.back();
        }
    }
    entry->flags = static_cast<uint8_t>((entry->flags & ~(TableRow::kFlagReplacesRunning | TableRow::kFlagMarked)) |
                                        (static_cast<uint8_t>(exception) & TableRow::kFlagReplacesRunning));
    entry->group = group;
    entry->params = params ? new_struct_copy(params) : nullptr;
    entry->script = script_name;
    entry->type = type;
    altered_ = true;
    if (tell_tracker)
        tracker_add_listener(type, script);
}

// 0x0045FDF0
void SignalRouteTable::remove_type(uint32_t type)
{
    for (int i = 0; i < num_; ++i) {
        TableRow& entry = core::at(entries(), i);
        if (entry.type == type) {
            entry.type = 0;
            entry.script = kNullScript;
            free_params(entry);
        }
    }
}

// 0x0045FE60
void SignalRouteTable::remove_group(uint32_t group)
{
    for (int i = 0; i < num_; ++i) {
        TableRow& entry = core::at(entries(), i);
        if (entry.group == group || group == kAllGroups) {
            entry.script = kNullScript;
            free_params(entry);
        }
    }
}

// 0x004600E0 (already removed entries are told to the tracker again)
void SignalRouteTable::remove_group(Script* script, uint32_t group)
{
    const std::span<TableRow> rows = entries();
    for (int i = 0; i < num_; ++i) {
        TableRow& entry = core::at(rows, i);
        if (entry.group == group || group == kAllGroups) {
            if (entry.type)
                tracker_remove_listener(entry.type, script);
            entry.script = kNullScript;
            free_params(entry);
        }
    }
}

// 0x0045FED0 (the mark stays)
void SignalRouteTable::remove_marked(Script* script)
{
    for (int i = 0; i < num_; ++i) {
        TableRow& entry = core::at(entries(), i);
        if (entry.flags & TableRow::kFlagMarked) {
            if (entry.type)
                tracker_remove_listener(entry.type, script);
            entry.script = kNullScript;
            free_params(entry);
        }
    }
}

// 0x0045FF50
void SignalRouteTable::compact()
{
    if (!entries_ || num_ <= 0)
        return;
    int removed = 0;
    for (int i = 0; i < num_; ++i) {
        if (core::at(entries(), i).script == kNullScript)
            ++removed;
    }
    if (!removed)
        return;
    const int live = num_ - removed;
    altered_ = true;
    if (live == 0) {
        for (int i = 0; i < num_; ++i)
            free_params(core::at(entries(), i));
        memory::Hub::get_pull()->dealloc(entries_);
        entries_ = nullptr;
        num_ = 0;
        return;
    }
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->front_end_heap());
    const std::span<TableRow> kept(static_cast<TableRow*>(memory::alloc(live * sizeof(TableRow))), static_cast<std::size_t>(live));
    memory::Hub::get_pull()->leave_heap_scope();
    std::size_t out = 0;
    for (int i = 0; i < num_; ++i) {
        TableRow& entry = core::at(entries(), i);
        if (entry.script != kNullScript) {
            core::at(kept, out) = entry;
            ++out;
        } else {
            free_params(entry);
        }
    }
    memory::Hub::get_pull()->dealloc(entries_);
    num_ = live;
    entries_ = kept.data();
}

// 0x004601A0
void SignalRouteTable::add_handlers(script::Array* handlers, bool replace)
{
    const int count = static_cast<int>(handlers->extent());
    if (replace && count == 0) {
        // An empty array: every handler goes at once (the tracker isn't told; the caller has unregistered).
        if (!entries_)
            return;
        for (int i = 0; i < num_; ++i) {
            if (Struct* params = core::at(entries(), i).params)
                free_struct(memory::owned_element(params)); // (the row's copy)
        }
        memory::Hub::get_pull()->dealloc(entries_);
        entries_ = nullptr;
        num_ = 0;
        altered_ = true;
        return;
    }
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->front_end_heap());
    const std::span<TableRow> made(static_cast<TableRow*>(memory::alloc((num_ + count) * sizeof(TableRow))),
                                   static_cast<std::size_t>(num_ + count));
    int used = 0;
    for (int i = 0; i < count; ++i) {
        auto* handler = handlers->struct_at(static_cast<uint32_t>(i));
        script::Component* type = handler->step_to_entry(nullptr);
        const script::Component* script = handler->step_to_entry(type);
        if (replace)
            remove_type(type->value.word());
        TableRow& entry = core::at(made, used++);
        Struct* params = nullptr;
        entry.params = handler->read_sub_struct(kParamsName, &params, 0) ? new_struct_copy(params) : nullptr;
        if (!handler->read_hash_value(kGroupName, &entry.group, 0))
            entry.group = kDefaultGroup;
        entry.flags = static_cast<uint8_t>((entry.flags & ~(TableRow::kFlagReplacesRunning | TableRow::kFlagMarked)) |
                                           (handler->has_bare_flag(kExceptionFlag) & TableRow::kFlagReplacesRunning));
        entry.type = type->value.word();
        entry.script = script->value.word();
        altered_ = true;
    }
    // The old entries after the new ones.
    if (entries_) {
        for (int i = 0; i < num_; ++i)
            core::at(made, used++) = core::at(entries(), i);
    }
    memory::Hub::get_pull()->dealloc(entries_);
    entries_ = made.data();
    num_ = used;
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00460470
void SignalRouteTable::forward_trigger(Signal* event, Script* script)
{
    const bool was_changed = altered_;
    altered_ = false;
    ++in_use_;
    // (Read once: if a handler adds entries the array moves, altered_ is set and the loop stops after it.)
    const std::span<const TableRow> rows = entries();
    for (int i = 0; i < num_; ++i) {
        const TableRow* entry = &core::at(rows, i);
        if (entry->type == event->kind_tag() && entry->script != kNullScript) {
            // The event's data is added over the handler's own parameters, in a structure of its own.
            gsl::owner<Struct*> merged = nullptr;
            Struct* params = nullptr;
            if (event->payload()) {
                merged = script::new_struct();
                if (entry->params)
                    merged->merge_structure(entry->params);
                merged->merge_structure(event->payload());
                params = merged;
            } else {
                params = entry->params;
            }

            if (entry->flags & TableRow::kFlagReplacesRunning) {
                // An exception: first the script's OnExceptionRun script (once) as an interrupt, given an
                // unnamed checksum 1 (the exception bit); then the handler replaces what the script runs.
                if (script->get_on_exception_script()) {
                    gsl::owner<Struct*> exception_params = script::new_struct();
                    exception_params->add_component_of_type(0, script::kQbvalName,
                                                            entry->flags & TableRow::kFlagReplacesRunning);
                    const uint32_t on_exception = script->get_on_exception_script();
                    script->set_on_exception_script(0);
                    script->preempt(on_exception, exception_params);
                    if (exception_params)
                        free_struct(exception_params);
                }
                // (The entry may have been removed by the OnExceptionRun script - or its array freed [bug].)
                if (entry->script != kNullScript) {
                    script->bind_script(entry->script, params, script->entity().get_pull());
                    script->tick_frame();
                }
            } else if (const Entity* object = script->entity().get_pull();
                       object && (object->object_flags() & objects::kObjectFlagSpawnHandlers)) {
                // A new spawned script on the object, run at once (a C function as handler returns no script:
                // null is used [bug: crash]).
                Script* spawned =
                    script::spawn_script(entry->script, params);
                spawned->entity().assign_value(script->entity().get_pull());
                spawned->tick_frame();
            } else {
                script->preempt(entry->script, params);
            }

            if (merged)
                free_struct(merged);
            // (entry->script is read again: a stale entry if the handler changed the table [bug, harmless].)
            event->set_event_handled(0, entry->script);
        }
        if (!alive_) {
            // The script was destroyed by a handler: the last forward_trigger out frees the table.
            if (--in_use_ == 0) {
                this->~SignalRouteTable();
                memory::Hub::get_pull()->dealloc(this);
            }
            return;
        }
        if (altered_) {
            --in_use_;
            return;
        }
    }
    altered_ = was_changed;
    --in_use_;
}

} // namespace objects

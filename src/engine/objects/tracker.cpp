// The object tracker: objects by id and alias, the scripts listening for each event type, WaitForEvent, the
// event log, and LaunchEvent - how an event is delivered (docs/script-runtime.md, section 11). See tracker.h.

#include "engine/objects/tracker.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/lists/key_map.h"
#include "engine/maths/vec4.h"
#include "engine/objects/signal.h"
#include "engine/objects/smtptr.h"
#include "frontend/screen_element_manager.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/objects/compositemgr.h"
#include "engine/script/script_startup.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "platform/all_ones.h"
#include <gsl/pointers>
#include <gsl/assert>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/objects/objects_shared.h"

namespace objects {

objects::EventWaitBoard* get_tracker() { return objects_shared().tracker().get(); }

memory::PoolPair& script_list_pools()
{
    static core::NoDestroy<memory::PoolPair> script_list_pools;
    return script_list_pools.get();
}

namespace {
constexpr int kHashSlots = 8;
constexpr std::size_t kAutoNameBytes = 64;
constexpr int kMaxAutoId = 1000000;

// Vtables.

// Names.
constexpr uint32_t kId = 0x40C698AF;         // id
constexpr uint32_t kAlias = 0x1E93946B;      // alias
constexpr uint32_t kType = 0x7321A8D6;       // type
constexpr uint32_t kTarget = 0xB990D003;     // target
constexpr uint32_t kSource = 0xA075808C;     // source
constexpr uint32_t kData = 0x520C0C9C;       // data
constexpr uint32_t kBroadcast = 0x640E830A;  // broadcast
constexpr uint32_t kRadius = 0xC48391A5;     // radius
constexpr uint32_t kScripts = 0x22E168C1;    // scripts
constexpr uint32_t kScriptName = 0x6166F3AD; // script_name
constexpr uint32_t kEventNames = 0x08114F90; // the global array of event names (its name isn't known)

constexpr int kLogSize = 0x400;
constexpr int kWaiters = 40;

// The pool pair of the listener lists (ScriptList).

// --- the objects ---
uint32_t& current_object_id(Entity* object) { return object->ident(); }
// The tracker's aliases of it (compared as a signed count by the original).
bool has_aliases(const Entity* object) { return static_cast<int>(object->alias_count()) > 0; }
bool has_position(const Entity* object) { return (object->object_flags() & kObjectFlagHasPosition) != 0; }
// The position: GameEntity's at_point_ (+0x4C, just after Entity's; read on any object).
const maths::Vec4& get_position(Entity* object) { return core::down_cast<GameEntity>(object)->at_point(); }
// Take the properties in `params` (SetObjectProps); an event targeted at it.
void change_traits(Entity* object, script::Struct* params) { object->change_traits(params); }
void forward_trigger(Entity* object, Signal* event) { object->pass_event_to_script(event); }
// The object with this id (the object manager 0x006F0FB8's look_up_entity).
Entity* find_object_by_id(uint32_t id)
{
    return static_cast<ObjectHubBase*>(objects::object_hub_slot().get())->look_up_entity(id);
}
// 0x004AC120: the object id named by `name` in `params` (the screen element manager 0x00701440; also takes names
// of screen elements and aliases). False if there is none.
bool get_object_id(script::Struct* params, uint32_t name, uint32_t* id)
{
    return frontend::UiTree::sole()->resolve_id(params, name, id);
}

// 0x004A1090: give an event listener the event (the subclass's filter).
void listener_receive(SignalReceiver* listener, Signal* event) { listener->receive(event); }

// A new hash table of 2^bits buckets (null if there's no memory).
gsl::owner<lists::KeyMap*> new_hash_table(int bits)
{
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    return memory ? memory::construct_in<lists::KeyMap>(memory, bits) : nullptr;
}
void delete_hash_table(gsl::owner<lists::KeyMap*> table)
{
    if (table) {
        table->~KeyMap();
        memory::dealloc(table);
    }
}

void push_script_heap() { memory::enter_heap_scope(memory::Hub::get_pull()->script_heap()); }

// Delete an empty listener list (its key `type` already gone from the table... or about to be).
void delete_script_list(ScriptList* list)
{
    list->uncouple(); // (the head's destructor, inlined)
    ScriptList::operator delete(list);
}

} // namespace

// --- the list of scripts ---------------------------------------------------------------------------------

// 0x00462530
void ScriptNode::uncouple()
{
    preceding_->following_ = following_;
    following_->preceding_ = preceding_;
    following_ = this;
    preceding_ = this;
}

// 0x00462620
void ScriptList::append_one(script::Script* script)
{
    for (const ScriptNode* node = following()->is_head() ? nullptr : following(); node;) {
        if (node->payload() == script)
            return;
        node = node->following();
        if (node->is_head())
            break;
    }
    push_script_heap();
    auto* node = static_cast<ScriptNode*>(memory::alloc_zeroed(sizeof(ScriptNode)));
    if (node) {
        node->set_payload(script);
        node->set_rank(0);
        node->set_following(node);
        node->set_preceding(node);
    }
    memory::leave_heap_scope();
    // At the end (no null check).
    Expects(node != nullptr);
    node->set_preceding(preceding());
    node->set_following(this);
    preceding()->set_following(node);
    set_preceding(node);
}

// 0x004626B0
void ScriptList::drop_out(script::Script* script)
{
    for (ScriptNode* node = following()->is_head() ? nullptr : following(); node;) {
        if (node->payload() == script) {
            node->preceding()->set_following(node->following());
            node->following()->set_preceding(node->preceding());
            node->set_following(node);
            node->set_preceding(node);
            memory::dealloc(node); // (its destructor only unlinks it again)
            return;
        }
        node = node->following();
        if (node->is_head())
            return;
    }
}

void* ScriptList::operator new(size_t size) { return script_list_pools().carve(static_cast<unsigned int>(size)); }

// 0x004624D0
void ScriptList::operator delete(void* list) { script_list_pools().dealloc(list); }

// --- the event log ---------------------------------------------------------------------------------------

// 0x004627C0
void SignalTrace::add_row(const TraceRow& trace_row)
{
    const auto type = trace_row.type;
    const auto target = trace_row.target;
    const auto source = trace_row.source;
    const auto script = trace_row.script;
    const auto how = trace_row.how;
    const auto kind = trace_row.kind;
    if (!names_ptr_) {
        auto table = new_hash_table(kHashSlots);
        names_ptr_ = table;
        script::Array* names = script::fetch_list(kEventNames);
        for (int i = 0; i < ints::as_signed(names->extent()); ++i) {
            char* name = core::at(names_, i).data();
            const char* string = names->find_inline_text(i);
            std::memcpy(name, string, std::strlen(string) + 1);
            const uint32_t checksum = checksums::hash_from_text(names->find_inline_text(i));
            names_ptr_->store_entry(checksum, name);
        }
    }
    int last = ordinal_ - 1;
    if (last < 0)
        last = kLogSize - 1;
    if (kind == Kind::kFrame) {
        if (core::at(entries_, last).kind == Kind::kFrame) {
            ++core::at(entries_, last).type;
            return;
        }
    } else if (kind == Kind::kHandled) {
        --layer_;
    }
    TableRow& entry = core::at(entries_, ordinal_);
    entry.kind = kind;
    entry.type = kind == Kind::kFrame ? 0 : type;
    entry.target = target;
    entry.source = source;
    entry.script = script;
    entry.how = how;
    entry.depth = layer_;
    if (++ordinal_ == kLogSize) {
        ordinal_ = 0;
        has_looped_ = true;
    }
    if (kind == Kind::kLaunched)
        ++layer_;
    ++added_since_sweep_;
}

// 0x004620A0
void SignalTrace::echo_out(bool only_new, int /*count*/)
{
    constexpr const char* kLine = "========================================================\n"; // "=====...\n"
    int index = 0;
    int count = ordinal_;
    if (has_looped_) {
        index = ordinal_;
        count = kLogSize;
    }
    debug::printf(kLine);
    if (only_new)
        debug::printf("PRINTING EVENT LOG (ONLY NEW ENTRIES)\n\n");
    else
        debug::printf("PRINTING EVENT LOG\n\n");
    // Each entry from the oldest (their printing is compiled out of this build).
    for (; count > 0; --count) {
        if (++index == kLogSize)
            index = 0;
    }
    debug::printf(kLine);
    added_since_sweep_ = 0;
}

// 0x00462CB0
void SignalTrace::teardown() { delete_hash_table(memory::owned_element(names_ptr_)); }

// --- the tracker -----------------------------------------------------------------------------------------

// 0x00462CF0
EventWaitBoard::EventWaitBoard()
    : entity_list_(new_hash_table(kHashSlots)), aliases_(new_hash_table(4)), listeners_(new_hash_table(kHashSlots))
{
    log_.set_ordinal(0);
    log_.set_has_looped(false);
    log_.set_names_ptr(nullptr);
    log_.set_layer(0);
    log_.set_added_since_sweep(0);
    for (Waiter& waiter : waiters_)
        waiter.type = 0;
}

// 0x00462E40 (the listener table isn't freed - an original leak)
EventWaitBoard::~EventWaitBoard()
{
    delete_hash_table(entity_list_);
    log_.teardown();
}

// 0x00462120
Entity* EventWaitBoard::look_up_object(uint32_t id)
{
    if (auto* object = static_cast<Entity*>(entity_list_->fetch_entry(id)))
        return object;
    return static_cast<Entity*>(aliases_->fetch_entry(id, true));
}

// 0x00462190
Entity* EventWaitBoard::look_up_by_nickname(uint32_t alias)
{
    return static_cast<Entity*>(aliases_->fetch_entry(alias));
}

// 0x004621D0
uint32_t EventWaitBoard::fetch_distinct_id()
{
    uint32_t checksum = 0;
    for (bool again = true; again; again = entity_list_->fetch_entry(checksum)) {
        std::array<char, kAutoNameBytes> name{};
        text::format_to(name, "autoid%d", next_auto_id_++); // "autoid%d"
        if (next_auto_id_ >= kMaxAutoId)
            next_auto_id_ = 0;
        checksum = checksums::hash_from_text(name.data());
    }
    return checksum;
}

// 0x00462980
void EventWaitBoard::add_object(Entity* object)
{
    if (auto* aliased = static_cast<Entity*>(aliases_->fetch_entry(current_object_id(object)))) {
        if (has_aliases(aliased))
            --aliased->alias_count();
        aliases_->erase_entry(current_object_id(object));
    }
    entity_list_->store_entry(current_object_id(object), object);
    log_.add_row({.type = 0, .target = current_object_id(object), .source = kNoObjectId, .script = 0, .how = 0, .kind = SignalTrace::Kind::kObjectAdded});
}

// 0x00462A20
void EventWaitBoard::remove_object(Entity* object, uint32_t fresh_id, bool renamed)
{
    entity_list_->erase_entry(current_object_id(object));
    if (renamed) {
        for (Waiter& waiter : waiters_) {
            if (waiter.object_id_ == current_object_id(object))
                waiter.object_id_ = fresh_id;
        }
    } else {
        remove_aliases_of(object);
    }
    log_.add_row({.type = 0, .target = current_object_id(object), .source = kNoObjectId, .script = 0, .how = 0, .kind = SignalTrace::Kind::kObjectRemoved});
}

// 0x00462590: from the start of the table again after each one removed.
void EventWaitBoard::remove_aliases_of(Entity* object)
{
    lists::KeyMap* table = aliases_;
    for (;;) {
        table->set_iter_bucket(-1);
        table->set_iter_item(nullptr);
        if (!table->step_iterator() || !table->iter_item())
            return;
        uint32_t alias = table->iter_item()->key;
        auto* aliased = static_cast<Entity*>(table->iter_item()->value);
        for (;;) {
            if (!aliased)
                return;
            if (current_object_id(aliased) == current_object_id(object))
                break;
            if (!table->step_iterator() || !table->iter_item())
                return;
            alias = table->iter_item()->key;
            aliased = static_cast<Entity*>(table->iter_item()->value);
        }
        if (has_aliases(aliased))
            --aliased->alias_count();
        table->erase_entry(alias);
    }
}

// 0x00462A90 (`object` isn't checked for null)
void EventWaitBoard::register_nickname(uint32_t alias, Entity* object)
{
    if (auto* aliased = static_cast<Entity*>(aliases_->fetch_entry(alias))) {
        if (has_aliases(aliased))
            --aliased->alias_count();
        aliases_->erase_entry(alias);
    }
    ++object->alias_count();
    aliases_->store_entry(alias, object);
}

// 0x00462000
void EventWaitBoard::remove_event_listener(SignalReceiver* listener)
{
    SignalReceiver* previous = nullptr;
    for (SignalReceiver* other = event_listeners_; other; other = other->next_subscriber()) {
        if (other == listener) {
            if (previous)
                previous->set_next_subscriber(listener->next_subscriber());
            else
                event_listeners_ = listener->next_subscriber();
            break;
        }
        previous = other;
    }
    listener->set_on_board(false);
}

// 0x00463370
void EventWaitBoard::add_listener(uint32_t type, script::Script* script)
{
    push_script_heap();
    auto* list = static_cast<ScriptList*>(listeners_->fetch_entry(type));
    if (!list) {
        list = static_cast<ScriptList*>(ScriptList::operator new(sizeof(ScriptList)));
        if (list) {
            list->set_payload(platform::all_ones_pointer<script::Script>());
            list->set_rank(0);
            list->set_following(list);
            list->set_preceding(list);
        }
        listeners_->store_entry(type, list); // (null too, if there was no memory - then Add crashes)
    }
    Expects(list != nullptr);
    list->append_one(script);
    memory::leave_heap_scope();
}

// 0x00463450
void EventWaitBoard::remove_listener(uint32_t type, script::Script* script)
{
    auto* list = static_cast<ScriptList*>(listeners_->fetch_entry(type));
    if (!list)
        return;
    list->drop_out(script);
    if (list->is_vacant()) {
        listeners_->erase_entry(type);
        delete_script_list(list);
    }
}

// 0x00462B90: walking the table, one item ahead (the current one may be removed).
void EventWaitBoard::forget_script(script::Script* script)
{
    lists::KeyMap* table = listeners_;
    table->set_iter_bucket(-1);
    table->set_iter_item(nullptr);
    if (!table->step_iterator() || !table->iter_item())
        return;
    uint32_t type = table->iter_item()->key;
    auto* list = static_cast<ScriptList*>(table->iter_item()->value);
    uint32_t next_type = type;
    while (list) {
        ScriptList* next = nullptr;
        if (table->step_iterator() && table->iter_item()) {
            next_type = table->iter_item()->key;
            next = static_cast<ScriptList*>(table->iter_item()->value);
        }
        list->drop_out(script);
        if (list->is_vacant()) {
            table->erase_entry(type);
            delete_script_list(list);
        }
        list = next;
        type = next_type;
    }
}

// 0x00462260
void EventWaitBoard::wait_for_event(script::Script* script, uint32_t type)
{
    int slot = -1;
    for (int i = 0; i < kWaiters; ++i) {
        if (core::at(waiters_, i).type == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        // A slot whose script has gone.
        for (int i = 0; i < kWaiters; ++i) {
            if (!script::find_script_with_unique_id(core::at(waiters_, i).script_id_)) {
                slot = i;
                break;
            }
        }
        if (slot < 0)
            return;
    }
    Waiter& waiter = core::at(waiters_, slot);
    waiter.script_id_ = script->unique_id();
    waiter.object_id_ = script->entity() ? current_object_id(script->entity().get_pull()) : 0;
    waiter.type = type;
    script->gate_off(0);
}

// 0x00462F00
bool EventWaitBoard::launch_event(uint32_t type, uint32_t target, uint32_t source, script::Struct* data, bool broadcast,
                                  float radius)
{
    ++launch_depth_;
    log_.add_row({.type = type, .target = target, .source = source, .script = static_cast<uint32_t>(launched_from_script_), .how = 0, .kind = SignalTrace::Kind::kLaunched});
    launched_from_script_ = 0;
    Signal event(type, source, target, data);

    if (!broadcast) {
        // Every event listener, then the target: an object, or else the spawned script with that id.
        for (SignalReceiver* listener = event_listeners_; listener; listener = listener->next_subscriber()) {
            ++listener->busy();
            listener_receive(listener, &event);
            --listener->busy();
        }
        const uint32_t event_target = event.aim_point();
        if (event_target != kNoObjectId && !(event.flag_bits() & Signal::kFlagHandled)) {
            if (Entity* object = look_up_object(event_target)) {
                forward_trigger(object, &event);
            } else {
                script::Script* script = script::look_up_spawned_script(event_target);
                if (!script)
                    log_.echo_out(true, kLogSize);
                script->receive_event(&event); // (null when there's no such script - an original crash)
            }
        }
    } else if (auto* list = static_cast<ScriptList*>(listeners_->fetch_entry(type, true))) {
        // Every script listening for the type, in the order they started listening; with a radius (>= 0), only
        // those whose object is nearer the source than that (scripts without an object, or whose object has no
        // position, always get it).
        maths::Vec4 source_position{};
        if (radius >= 0.0f) {
            source_position = get_position(find_object_by_id(source)); // (not checked for null)
        }
        auto in_range = [&](script::Script* script) {
            if (!(radius >= 0.0f))
                return true;
            Entity* object = script->entity().get_pull();
            if (!object || !has_position(object))
                return true;
            const double distance = maths::get_distance_squared(source_position, get_position(object));
            return static_cast<double>(radius) * radius > distance;
        };
        int count = 0;
        const ScriptNode* first = list->following();
        if (!first->is_head()) {
            for (const ScriptNode* node = first; node;) {
                node = node->following();
                ++count;
                if (node->is_head())
                    break;
            }
        }
        if (count == 1) {
            script::Script* script = first->payload();
            if (in_range(script))
                script->receive_event(&event);
        } else {
            // Several (or none): first a smart pointer to each, so that one deleted by an earlier one's handler
            // is skipped.
            using Pointer = SharedRef<SharedBase>;
            auto* scripts_block = static_cast<script::Script**>(memory::alloc(static_cast<unsigned int>(count) * 4));
            const std::span<script::Script*> scripts(scripts_block, static_cast<std::size_t>(count));
            // (the original's new[]: its count before them - here the count is the local one)
            auto* pointers_block = static_cast<Pointer*>(memory::alloc(static_cast<unsigned int>(count) * sizeof(Pointer)));
            const std::span<Pointer> pointers(pointers_block, pointers_block ? static_cast<std::size_t>(count) : 0);
            for (Pointer& pointer : pointers)
                memory::construct_in<Pointer>(&pointer);
            const ScriptNode* node = list->following()->is_head() ? nullptr : list->following();
            for (int i = 0; node; ++i) {
                core::at(scripts, i) = node->payload();
                core::at(pointers, i).assign_value(&node->payload()->shared_base());
                node = node->following();
                if (node->is_head())
                    break;
            }
            for (int i = 0; i < count; ++i) {
                if (core::at(pointers, i) && in_range(core::at(scripts, i)))
                    core::at(scripts, i)->receive_event(&event);
            }
            if (pointers_block) {
                for (int i = count - 1; i >= 0; --i)
                    core::at(pointers, i).get_release(); // (the destructors, last first)
                memory::dealloc(pointers_block);
            }
            memory::dealloc(static_cast<void*>(scripts_block));
        }
    }

    // The first WaitForEvent waiter for this type whose object is the target (or that has no object, or the
    // event has no target or is broadcast) goes on; waiters whose script has gone are dropped on the way.
    for (Waiter& waiter : waiters_) {
        if (waiter.type == type &&
            (waiter.object_id_ == target || waiter.object_id_ == 0 || target == kNoObjectId || broadcast)) {
            waiter.type = 0;
            if (script::Script* script = script::find_script_with_unique_id(waiter.script_id_)) {
                script->stop_waiting();
                event.set_event_handled(2, script->script_title());
                break;
            }
        }
    }
    if (!(event.flag_bits() & Signal::kFlagHandled))
        log_.add_row({.type = event.kind_tag(), .target = event.aim_point(), .source = event.from_where(), .script = 0, .how = 3, .kind = SignalTrace::Kind::kHandled});
    --launch_depth_;
    const bool handled = (event.flag_bits() & Signal::kFlagHandled) != 0;
    event.teardown();
    return handled;
}

// 0x00462B10
void EventWaitBoard::log_handled(Signal* event, uint32_t how, uint32_t script)
{
    log_.add_row({.type = event->kind_tag(), .target = event->aim_point(), .source = event->from_where(), .script = script, .how = how, .kind = SignalTrace::Kind::kHandled});
}

// 0x00462B40
void EventWaitBoard::log_read(Signal* event, uint32_t how, uint32_t script)
{
    log_.add_row({.type = event->kind_tag(), .target = event->aim_point(), .source = event->from_where(), .script = script, .how = how, .kind = SignalTrace::Kind::kRead});
}

// 0x00462B70
void EventWaitBoard::log_frame() { log_.add_row({.type = 0, .target = 0, .source = 0, .script = 0, .how = 0, .kind = SignalTrace::Kind::kFrame}); }

// 0x00463730
EventWaitBoard* create_tracker(bool create)
{
    if (!objects_shared().tracker() && create)
        objects_shared().tracker() = std::make_unique<EventWaitBoard>(); // (null when the heap is full)
    ++objects_shared().tracker_references();
    return objects_shared().tracker().get();
}

// --- the script commands ---------------------------------------------------------------------------------

// 0x00462310: WaitForEvent type=<type>
bool cfunc_wait_for_event(script::Struct* params, script::Script* script)
{
    uint32_t type = 0; // (uninitialised in the original when there's no type)
    params->read_hash_value(kType, &type, 0);
    get_tracker()->wait_for_event(script, type);
    return true;
}

// 0x00462340: ObjectExists id=<object>
bool cfunc_object_exists(script::Struct* params, script::Script* /*script*/)
{
    uint32_t id = 0;
    if (!get_object_id(params, kId, &id))
        return false;
    return get_tracker()->look_up_object(id) != nullptr;
}

// 0x00462380: TerminateObjectsScripts id=<object> (scripts=[names] | script_name=<name>): stop the object's
// scripts that refer to these names (no name: 0).
bool cfunc_terminate_objects_scripts(script::Struct* params, script::Script* /*script*/)
{
    uint32_t id = 0;
    if (!get_object_id(params, kId, &id))
        return false;
    Entity* object = get_tracker()->look_up_object(id);
    script::Array* scripts = nullptr;
    if (params->fetch_list(kScripts, &scripts, 0)) {
        for (uint32_t i = 0; i < scripts->extent(); ++i)
            script::stop_scripts_using_object_that_refer_to(object, scripts->fetch_part(i));
    } else {
        uint32_t name = 0;
        params->read_hash_value(kScriptName, &name, 0);
        script::stop_scripts_using_object_that_refer_to(object, name);
    }
    return true;
}

// 0x00462440: SetObjectProps id=<object> ... (the object takes the rest; not checked for null)
bool cfunc_set_object_props(script::Struct* params, script::Script* /*script*/)
{
    uint32_t id = 0;
    if (!get_object_id(params, kId, &id))
        return false;
    change_traits(get_tracker()->look_up_object(id), params);
    return true;
}

// 0x00462480: PrintEventLog [<count>] (with a count: the whole header, not "only new entries")
bool cfunc_print_event_log(script::Struct* params, script::Script* /*script*/)
{
    bool only_new = true;
    int count = kLogSize;
    if (params->fetch_int_value(0u, &count, 0))
        only_new = false;
    get_tracker()->log().echo_out(only_new, count);
    return true;
}

// 0x00462C40: AssignAlias id=<object> alias=<name>
bool cfunc_assign_alias(script::Struct* params, script::Script* /*script*/)
{
    uint32_t id = 0;
    if (!get_object_id(params, kId, &id))
        return false;
    EventWaitBoard* tracker = get_tracker();
    Entity* object = tracker->look_up_object(id);
    uint32_t alias = 0; // (uninitialised in the original when there's no alias)
    params->read_hash_value(kAlias, &alias, 1);
    tracker->register_nickname(alias, object);
    return true;
}

// 0x004634D0: LaunchEvent / FireEvent type=<type> | type=[types] | type={types} [target=<id>] [source=<id>]
// [data={...}] [broadcast] [radius=<r>]. Target and source default to the script's object (none without one).
bool cfunc_launch_event(script::Struct* params, script::Script* script)
{
    auto own_object = [&]() {
        return script && script->entity() ? current_object_id(script->entity().get_pull()) : kNoObjectId;
    };
    uint32_t target = 0;
    if (!get_object_id(params, kTarget, &target))
        target = own_object();
    uint32_t source = 0;
    if (!get_object_id(params, kSource, &source))
        source = own_object();
    script::Struct* data = nullptr;
    params->read_sub_struct(kData, &data, 0);
    const bool broadcast = params->has_bare_flag(kBroadcast);
    float radius = -1.0f;
    params->fetch_float(kRadius, &radius, 0);
    EventWaitBoard* tracker = get_tracker();
    // Whether this launch comes from a script with a name (only for the log).
    auto from_script = [&]() { return script && script->script_title() ? 1 : 0; };

    uint32_t type = 0;
    if (params->read_hash_value(kType, &type, 0)) {
        tracker->set_launched_from_script(from_script());
        tracker->launch_event(type, target, source, data, broadcast, radius);
        return true;
    }
    script::Array* types = nullptr;
    if (params->fetch_list(kType, &types, 0)) {
        const uint32_t size = types->extent();
        for (uint32_t i = 0; i < size; ++i) {
            tracker->set_launched_from_script(from_script());
            tracker->launch_event(types->fetch_part(i), target, source, data, broadcast, radius);
        }
        return true;
    }
    script::Struct* structure = nullptr;
    if (params->read_sub_struct(kType, &structure, 0)) {
        for (script::Component* component = structure->step_to_entry(nullptr); component;
             component = structure->step_to_entry(component)) {
            tracker->set_launched_from_script(from_script());
            tracker->launch_event(component->value.word(), target, source, data, broadcast, radius);
        }
    }
    return true;
}

} // namespace objects

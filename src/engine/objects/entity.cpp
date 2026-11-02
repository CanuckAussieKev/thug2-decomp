// GameObjects::Entity: the base of the game's objects - id, type, tags, flags, its own script, and the member functions
// scripts call on any object (entity.h).

#include "engine/objects/entity.h"

#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>

namespace objects {

using script::Array;
using script::Component;
using script::Script;
using script::Struct;

namespace {

// Names (checksums) the object uses.
constexpr uint32_t kUnnamed = 0;                  // an unnamed component
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
constexpr uint32_t kReactionList = 0x475BF03C;    // event_handlers
constexpr uint32_t kReplaceHandlers = 0x3F93A1EF; // replace_handlers
constexpr uint32_t kParams = 0x7031F10C;          // params
constexpr uint32_t kPauseWithObject = 0x8757D0BB; // PauseWithObject
constexpr uint32_t kAll = 0xC4E78E22;             // all
constexpr uint32_t kClear = 0x1A4E0EF9;           // clear
constexpr uint32_t kNoSpawnerFile = 0x01917DF2;   // (name unknown): the spawned script keeps no spawner file

// The tracker's LaunchEvent (to `target`, -1: everyone; its answer isn't used).
void launch_event(uint32_t type, uint32_t target, uint32_t source, Struct* data, bool broadcast, float radius)
{
    get_tracker()->launch_event(type, target, source, data, broadcast, radius);
}

// `new CStruct` (the structure pool) / `delete`.
// A copy of `component` (from the component pool) added to `dest`.
void add_copy(Struct* dest, Component* component)
{
    gsl::owner<Component*> copy = script::new_component();
    script::duplicate_component(copy, component, 0);
    dest->attach_module(copy);
}

} // namespace

// 0x00460B80
Entity::Entity() { list_link_.make_node(this); }

// 0x004615D0
Entity::~Entity()
{
    if (tag_table_)
        free_struct(tag_table_); // (the pointer is left as it is)
    if (park_owner_)
        park_owner_->drop_entity_registration(this);
    if (own_script_) {
        own_script_->clear_on_exit_scripts();
        if (own_script_)
            Script::destroy(own_script_);
        own_script_ = nullptr;
    }
    script::end_scripts_bound_to_entity(this);
    ref_.clear_smart_pointers(); // (ref_'s destructor, in the original's order)
    list_link_.uncouple();       // (0x00460B20)
    // (then SharedBase's: the smart pointers to it cleared)
}

// 0x00460930
void Entity::dispose_when_free()
{
    if (!(object_flags_ & kObjectFlagLocked))
        delete this;
}

// 0x00461710
void Entity::change_traits(Struct* params)
{
    const bool replace = params->has_bare_flag(kReplaceHandlers);
    Array* handlers = nullptr;
    if (params->fetch_list(kReactionList, &handlers, 0)) {
        create_script_if_needed();
        own_script_->install_event_table(handlers, replace);
    }
    Struct* tags = nullptr;
    if (params->read_sub_struct(kTags, &tags, 0)) {
        create_tags_if_needed();
        tag_table_->append_structure_if_any(tags);
    }
    Array* remove_tags = nullptr;
    if (params->fetch_list(kRemoveTags, &remove_tags, 0))
        get_remove_tags(remove_tags);
}

// 0x004608F0
bool Entity::invoke_method_by_name(uint32_t name, Struct* params, Script* /*script*/)
{
    if (name == kSetProps)
        change_traits(params);
    else if (name == kDie)
        flag_as_dead();
    return true;
}

// 0x00461140
bool Entity::pass_event_to_script(Signal* event)
{
    if (!own_script_)
        return true;
    SharedRef<Entity> self;
    self.assign_value(this);
    own_script_->receive_event(event);
    return static_cast<bool>(self); // (cleared if the object was deleted meanwhile)
}

// 0x00460A50
void Entity::flag_as_dead()
{
    if (own_script_)
        own_script_->gate_off(0); // (until unblocked: for good)
    script::end_scripts_bound_to_entity(this);
    object_flags_ &= ~kObjectFlagLocked;
    ObjectHubBase* manager = park_owner_; // (unregistering clears park_owner_: the manager read first is used)
    manager->drop_entity_registration(this);
    manager->remove_entity(this);
    object_flags_ |= kObjectFlagDead;
    ref_.clear_smart_pointers();
}

// 0x00461290
void Entity::create_script_if_needed()
{
    if (own_script_)
        return;
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    Script* script = Script::create();
    own_script_ = script;
    script->bind_script(kEmptyScript, nullptr, this);
    memory::leave_heap_scope();
}

// 0x00461350
Script* Entity::replace_script(uint32_t name, Struct* params)
{
    if (!own_script_)
        own_script_ = Script::create();
    own_script_->bind_script(name, params, this);
    own_script_->has_attached_script(); // (the answer isn't used)
    return own_script_;
}

// 0x00461500
int Entity::run_sub_script(uint32_t name, Struct* params)
{
    if (!own_script_) {
        Script* script = Script::create();
        own_script_ = script;
        script->entity().assign_value(this);
    }
    return own_script_->preempt(name, params);
}

// 0x00461410
Script* Entity::spawn_script(uint32_t name, Struct* params, uint32_t id, bool pause_with_object)
{
    Script* script = script::spawn_script(name, params, 0, nullptr, -1, id, false, false, false, pause_with_object);
    script->entity().assign_value(this);
    return script;
}

// 0x00461480
Script* Entity::start_script_task(uint32_t name, int node, bool replicate_flag, bool permanent, Struct* params)
{
    if (replicate_flag)
        script::send_spawn_script_message(name, nullptr, node, permanent);
    Script* script = script::spawn_script(name, params, 0, nullptr, node, 0, false, false, false, false);
    script->entity().assign_value(this);
    script->tick_frame();
    return script;
}

// 0x004609F0
void Entity::post_own_event(uint32_t type, Struct* data) { launch_event(type, ident_, ident_, data, false, -1.0f); }

// 0x00460A20
void Entity::announce(uint32_t type, Struct* data, float /*radius*/)
{
    launch_event(type, kUnassignedId, ident_, data, true, -1.0f);
}

// 0x00461930
void Entity::change_serial(uint32_t id)
{
    if (ident_ != kUnassignedId && park_owner_)
        park_owner_->reassign_object_id(this, id);
    ident_ = id;
    create_tags_if_needed();
    tag_table_->put_name_key(kId, id);
}

// 0x00461970
void Entity::change_species(uint32_t type)
{
    kind_tag_ = type;
    create_tags_if_needed();
    tag_table_->put_name_key(kType, type);
}

// 0x004611E0
void Entity::create_tags_if_needed()
{
    if (tag_table_)
        return;
    memory::enter_heap_scope(memory::Hub::get_pull()->front_end_heap());
    tag_table_ = script::new_struct();
    memory::leave_heap_scope();
}

// 0x004616B0
void Entity::store_int_tag(uint32_t name, int value)
{
    create_tags_if_needed();
    tag_table_->put_int(name, value);
}

// 0x004616D0
void Entity::write_hash_tag(uint32_t name, uint32_t value)
{
    create_tags_if_needed();
    tag_table_->put_name_key(name, value);
}

// 0x004616F0
void Entity::set_tags_from_struct(Struct* tags)
{
    create_tags_if_needed();
    tag_table_->append_structure_if_any(tags);
}

// 0x00460C20
void Entity::get_remove_tags(Array* names)
{
    if (!tag_table_)
        return;
    if (names) {
        for (uint32_t i = 0; i < names->extent_; ++i) {
            const uint32_t name = names->fetch_part(i);
            tag_table_->erase_module(name);
            tag_table_->remove_flag(name);
        }
        if (!tag_table_->is_vacant())
            return;
    }
    if (tag_table_)
        free_struct(tag_table_);
    tag_table_ = nullptr;
}

// 0x00460CA0
bool Entity::get_tag(Struct* params, Struct* dest)
{
    if (!tag_table_)
        return false;
    // The name is read into the argument's own slot: with no unnamed checksum, the pointer `params` is the name.
    uint32_t name = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    params->read_hash_value(kUnnamed, &name, 1);
    Component* tag = tag_table_->find_component(name);
    if (!tag)
        return false;
    add_copy(dest, tag);
    return true;
}

// 0x00460D90
bool Entity::fetch_labels(Array* names, Struct* dest)
{
    if (!tag_table_)
        return true;
    for (uint32_t i = names->extent_; i != 0;) {
        --i;
        if (Component* tag = tag_table_->find_component(names->fetch_part(i)))
            add_copy(dest, tag);
    }
    return true;
}

// 0x004607A0
void Entity::clear_event_handler(uint32_t type)
{
    if (own_script_)
        own_script_->clear_event_handler(type);
}

// 0x004607B0
void Entity::remove_tag_flag(uint32_t flag)
{
    if (tag_table_)
        tag_table_->remove_flag(flag);
}

// 0x004607C0
bool Entity::read_int_tag(uint32_t name, int* value)
{
    return tag_table_ ? tag_table_->fetch_int_value(name, value, 0) : false;
}

// 0x004607E0
bool Entity::read_hash_tag(uint32_t name, uint32_t* value)
{
    return tag_table_ ? tag_table_->read_hash_value(name, value, 0) : false;
}

// 0x00460800
bool Entity::contains_tag_flag(uint32_t flag) const { return tag_table_ ? tag_table_->has_bare_flag(flag) : false; }

// 0x00460820
void Entity::copy_tags_to(Struct* dest)
{
    if (tag_table_)
        dest->append_structure_if_any(tag_table_);
}

// 0x00460960
uint32_t Entity::get_flag_mask(Struct* params, Script* /*script*/)
{
    uint32_t mask = 0;
    Array* names = nullptr;
    int bit = 0;
    params->fetch_list(kUnnamed, &names, 0);
    if (names) {
        for (uint32_t i = 0; i < names->extent_; ++i)
            mask |= 1u << (script::fetch_int_value(names->fetch_part(i)) & 31);
        return mask;
    }
    if (params->fetch_int_value(kUnnamed, &bit, 0))
        return 1u << (bit & 31);
    return mask;
}

// 0x00460840: the calling script lets go of the object (at every level; it stops waiting). True.
bool Entity::obj_leave_script(Struct* /*params*/, Script* script)
{
    script->object_destroyed(this);
    return true;
}

// 0x00460850: kill the scripts spawned on the object with the name `name` - else with the spawn id `id` - except
// the one calling. True.
bool Entity::obj_kill_spawned_script(Struct* params, Script* script)
{
    uint32_t name = 0;
    if (params->read_hash_value(kName, &name, 0)) {
        script::end_child_scripts_of_entity_by_name(this, name, script);
        return true;
    }
    uint32_t id = 0;
    if (params->read_hash_value(kId, &id, 0))
        script::stop_scripts_of_object_and_tag(this, id, script);
    return true;
}

// 0x004608D0
bool Entity::obj_visible(Struct* /*params*/, Script* /*script*/)
{
    object_flags_ &= ~kEntityHiddenBit;
    return true;
}

// 0x004608E0
bool Entity::obj_invisible(Struct* /*params*/, Script* /*script*/)
{
    object_flags_ |= kEntityHiddenBit;
    return true;
}

// 0x00460940: ObjId = the object's id, in the calling script's parameters.
bool Entity::obj_get_id(Struct* /*params*/, Script* script)
{
    script->args()->put_name_key(kObjId, ident_);
    return true;
}

// 0x00460EB0: the tags into the calling script's parameters - as a new structure named by structure=, else
// added to them.
bool Entity::obj_get_tags(Struct* params, Script* script)
{
    uint32_t name = 0;
    if (params->read_hash_value(kStructure, &name, 0)) {
        gsl::owner<Struct*> copy = script::new_struct();
        if (tag_table_)
            copy->append_structure_if_any(tag_table_);
        script->args()->append_struct_ref(name, copy);
        return true;
    }
    if (tag_table_)
        script->args()->append_structure_if_any(tag_table_);
    return true;
}

// 0x00460FB0: the tag named by the unnamed checksum, into the calling script's parameters.
bool Entity::obj_get_single_tag(Struct* params, Script* script) { return get_tag(params, script->args().get()); }

// 0x00460FC0: the tags named in the unnamed array, into the calling script's parameters. With no array, the
// pointer `params` is taken for one [bug].
bool Entity::obj_get_named_tags(Struct* params, Script* script)
{
    Array* names = nullptr; // (the original: the structure's own pointer read as the array [bug])
    params->fetch_list(kUnnamed, &names, 1);
    return fetch_labels(names, script->args().get());
}

// 0x00460FF0: remove the tags named in tags= (else the unnamed array); with neither, every tag goes. True.
bool Entity::obj_remove_tags(Struct* params, Script* /*script*/)
{
    Array* names = nullptr;
    if (!params->fetch_list(kTags, &names, 0))
        params->fetch_list(kUnnamed, &names, 0);
    get_remove_tags(names);
    return true;
}

// 0x00461040: none of the flags is set (no flags named: false).
bool Entity::obj_flag_not_set(Struct* params, Script* script)
{
    const uint32_t mask = get_flag_mask(params, script);
    if (!mask)
        return false;
    return (flag_bits_ & mask) == 0;
}

// 0x00461070: clear the flags (all: every flag). No flags named: false.
bool Entity::obj_clear_flag(Struct* params, Script* script)
{
    if (params->has_bare_flag(kAll)) {
        flag_bits_ = 0;
        return true;
    }
    const uint32_t mask = get_flag_mask(params, script);
    if (!mask)
        return false;
    flag_bits_ &= ~mask;
    return true;
}

// 0x004610C0: set the flags. No flags named: false.
bool Entity::obj_set_flag(Struct* params, Script* script)
{
    const uint32_t mask = get_flag_mask(params, script);
    if (!mask)
        return false;
    flag_bits_ |= mask;
    return true;
}

// 0x004610F0: any of the flags was set (clear: then they're cleared). No flags named: false.
bool Entity::obj_flag_set(Struct* params, Script* script)
{
    const uint32_t flags = flag_bits_;
    const uint32_t mask = get_flag_mask(params, script);
    if (!mask)
        return false;
    if (params->has_bare_flag(kClear))
        flag_bits_ &= ~mask;
    return (flags & mask) != 0;
}

// 0x004617C0: add the parameters to the tags. True.
bool Entity::obj_set_tags(Struct* params, Script* /*script*/)
{
    create_tags_if_needed();
    tag_table_->append_structure_if_any(params);
    return true;
}

// 0x004617E0: spawn the script named first (a bare name) on the object, with params=, id= (default: the
// calling script's spawn id) and PauseWithObject. Unless 0x01917DF2 is given, it gets the calling script's
// spawner file. True (also when the first parameter isn't a name: nothing is spawned).
bool Entity::obj_spawn_script(Struct* params, Script* script)
{
    Component* first = params->step_to_entry(nullptr);
    if (!first || first->kind_tag() != script::kQbvalName)
        return true;
    const uint32_t name = first->value.word();
    uint32_t id = script::get_spawn_id(script);
    params->read_hash_value(kId, &id, 0);
    Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    const bool pause_with_object = params->has_bare_flag(kPauseWithObject);
    Script* spawned = spawn_script(name, script_params, id, pause_with_object);
    if (!params->has_bare_flag(kNoSpawnerFile))
        spawned->set_spawner_file(script);
    return true;
}

// 0x004618A0: the object's script switches to the unnamed name, with params=. True.
bool Entity::obj_switch_script(Struct* params, Script* /*script*/)
{
    uint32_t name = 0;
    if (params->read_hash_value(kUnnamed, &name, 0)) {
        Struct* script_params = nullptr;
        params->read_sub_struct(kParams, &script_params, 0);
        replace_script(name, script_params);
    }
    return true;
}

// 0x00460AD0
Entity* find_object(uint32_t id) { return get_tracker()->look_up_object(id); }

// 0x00460AC0
void get_unlock(Entity* object, void* /*data*/) { object->object_flags() &= ~kObjectFlagLocked; }

// 0x00460AA0
void dispose_when_free(Entity* object, void* /*data*/)
{
    if (!(object->object_flags() & kObjectFlagLocked)) {
        gsl::owner<decltype(object)> owned = memory::owned_element(object); // (the manager's)
        delete owned;
    }
}

} // namespace objects

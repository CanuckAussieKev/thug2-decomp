#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/smtptr.h"

// GameObjects::Entity, the base of every game object (0x4C bytes; GameEntity and its components build on it), and
// the managers that own objects (ObjectHubBase / MiscHub, entity_hub.h).
//
// An object has an id (a name checksum, the name scripts write before ':' - `id:Obj_Func`) and a type, both also
// kept as tags; at most one script of its own (own_script_, not spawned: whoever updates the object runs it); tags (a
// structure scripts read and write); flags scripts set and test (Obj_SetFlag...). Scripts find an object by its id
// through the object tracker (0x006F0FC0, wt/trk's code): find_object.
//
// From the disassembly of 0x004607A0-0x00461900 (methods: __thiscall, RET n = 4 * arguments; find_object and the
// manager walk's callbacks: __cdecl). The member functions scripts call (Obj_SetFlag...) are entries of the member
// function table at 0x00683550 (0x18 bytes each: the name, the component type 0x57525413 - "the object itself" - and
// a member function pointer, filled in by 0x00642940); 0x0045D850 (invoke_method) calls them as
// `bool (Entity::*)(CStruct* params, CScript* script)`.
namespace script {
class Array;
class Script;
class Struct;
struct MemberFunctionEntry;
} // namespace script

namespace objects {

class ObjectModule;
class ObjectHubBase;
class Entity;
struct Signal;

// A second smart-pointer target in each object (SharedBase, vtable 0x006477E0, +0x08 the object): the pointers to
// it are cleared as soon as the object is marked dead (flag_as_dead), the object's own (SharedBase) ones only when
// it is deleted.
class ObjectRef final : public SharedBase {
public:
    explicit ObjectRef(Entity* owner) : owner_object_(owner) {}
    // 0x00460920 (0x00460AF0 the deleting one): SharedBase's (a jump to 0x00463870).
    ~ObjectRef() override = default;

    Entity* owner_object_; // +0x08
};

// object_flags_ (+0x08)
constexpr uint32_t kObjectFlagDead = 0x1;              // flag_as_dead: waiting in the manager's dead list to be deleted
constexpr uint32_t kObjectFlagLocked = 0x2;            // not deleted by the manager walks that delete unlocked objects
constexpr uint32_t kEntityHiddenBit = 0x4;             // obj_invisible / Obj_Visible
constexpr uint32_t kObjectFlagHasPosition = 0x20;      // the tracker's distance checks use its position (+0x4C)
constexpr uint32_t kObjectFlagSpawnHandlers = 0x10000; // its event handlers run as new spawned scripts on it

// No id yet (0x0064780C: the manager gives it a unique one when registered) / no type (0x00647810).
constexpr uint32_t kUnassignedId = 0xFFFFFFFF;
constexpr uint32_t kNoObjectType = 0;

class Entity : public SharedBase {
public:
    // 0x00460B80: vtable 0x006477E8, no id, no type, no manager, no script, no tags. flag_bits_ (+0x24) is not
    // initialised.
    Entity();
    // 0x004615D0 (0x00461900 the deleting one): tags freed, unregistered from the manager, its script's on-exit
    // scripts dropped and the script deleted, every script using the object stopped, then the smart pointers to it
    // cleared.
    ~Entity() override;

    // --- the virtuals (vtable 0x006477E8: 0 the destructor, then these in the original's order; its slot 2 -
    // invoke_method - and slot 4 - empty - are the same in every object's table: a plain method / left out) ---
    // 0x00461710 (slot 1, the member function SetProps): event_handlers (with replace_handlers), tags (added) and
    // remove_tags from `params`.
    virtual void change_traits(script::Struct* params);
    // 0x004608F0 (slot 3): a member function by name: SetProps (0x6C63C7C5) runs change_traits, Die (0xC6870028)
    // flag_as_dead. Always true - also for names it doesn't know.
    virtual bool invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);
    // 0x00461140 (slot 5): `event` for its script (receive_event; none: nothing). Returns whether the object
    // still exists afterwards (a smart pointer to it is held across the call).
    virtual bool pass_event_to_script(Signal* event);
    // 0x00460A50 (slot 6): blocks its script for good, stops every script using it, unlocks it, unregisters it
    // and gives it to the manager's dead list (remove_entity); dead; the pointers to ref_ are cleared.
    virtual void flag_as_dead();
    // 0x005B9760 (slot 7): whether its PAUSE_WITH_OBJECT scripts may run (true: an Entity isn't paused).
    virtual bool should_update_scripts() { return true; }

    // 0x0045D850 (slot 2): a member function table entry run on the object: its member function pointer (on `this`
    // moved by the entry's adjustment), or, with none, invoke_method_by_name by the entry's name. Returns what that
    // returns.
    bool invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script);
    // 0x00460930: delete itself unless it is locked (no callers; the walks use the free dispose_when_free).
    void dispose_when_free();

    // --- script ---
    // 0x00461290: a script of its own if it has none (on the script heap), running empty_script (0x3F5CDB8A).
    void create_script_if_needed();
    // 0x00461350: its script (made if needed, from the current heap) runs `name` instead, with `params`.
    // Returns the script.
    script::Script* replace_script(uint32_t name, script::Struct* params);
    // 0x00461500: `name` runs now as an interrupt of its script (made if needed, from the current heap).
    int run_sub_script(uint32_t name, script::Struct* params);
    // 0x00461410: spawn `name` on the object (spawn id `id`). A C function name gives no script: crash.
    script::Script* spawn_script(uint32_t name, script::Struct* params, uint32_t id, bool pause_with_object);
    // 0x00461480: spawn `name` on the object and Update it once at once (`replicate_flag`: the other players are
    // told first, with no object).
    script::Script* start_script_task(uint32_t name, int node, bool replicate_flag, bool permanent,
                                      script::Struct* params);

    // --- events (the tracker 0x006F0FC0's LaunchEvent 0x00462F00) ---
    // 0x004609F0: an event of `type` from the object to itself.
    void post_own_event(uint32_t type, script::Struct* data);
    // 0x00460A20: an event of `type` from the object to every listener. `radius` isn't used (-1).
    void announce(uint32_t type, script::Struct* data, float radius);

    // --- id, type, tags ---
    // 0x00461930: the new id (the manager is told first if it had one), also as the tag id.
    void change_serial(uint32_t id);
    // 0x00461970: the type, also as the tag type.
    void change_species(uint32_t type);
    // 0x004611E0: an empty tag structure if it has none (on the front-end heap).
    void create_tags_if_needed();
    // 0x004616B0 / 0x004616D0: a tag as an integer / a checksum. 0x004616F0: add `tags`' components.
    void store_int_tag(uint32_t name, int value);
    void write_hash_tag(uint32_t name, uint32_t value);
    void set_tags_from_struct(script::Struct* tags);
    // 0x00460C20: remove the tags named in `names` (components and flags); then, if none are left - or straight
    // away when `names` is null - the tag structure is freed.
    void get_remove_tags(script::Array* names);
    // 0x00460CA0: a copy of the tag named by `params`' unnamed checksum, added to `dest`. False if there are no
    // tags or no such tag.
    bool get_tag(script::Struct* params, script::Struct* dest);
    // 0x00460D90: copies of the tags named in `names` (the last name first) added to `dest`. True.
    bool fetch_labels(script::Array* names, script::Struct* dest);
    // 0x004607B0: remove the flag `flag` from the tags (none: nothing).
    void remove_tag_flag(uint32_t flag);
    // 0x004607C0 / 0x004607E0: the tag `name` as an integer / a checksum (no tags, or none such: false).
    bool read_int_tag(uint32_t name, int* value);
    bool read_hash_tag(uint32_t name, uint32_t* value);
    // 0x00460800: whether the tags hold the flag `flag` (no tags: false).
    bool contains_tag_flag(uint32_t flag);
    // 0x00460820: the tags added to `dest` (no tags: nothing).
    void copy_tags_to(script::Struct* dest);
    // 0x004607A0: its script's handler for events of `type` goes (no script: nothing).
    void clear_event_handler(uint32_t type);
    // 0x00460960: the flag bits `params` names: its unnamed array of names (each one's global integer is a bit
    // number), else 1 << its unnamed integer, else 0. (`this` and `script` aren't used.)
    uint32_t get_flag_mask(script::Struct* params, script::Script* script);

    // --- the member functions scripts call on the object (the table at 0x00683550) ---
    bool obj_leave_script(script::Struct* params, script::Script* script);        // 0x00460840 (0xB3C262EC)
    bool obj_kill_spawned_script(script::Struct* params, script::Script* script); // 0x00460850
    bool obj_visible(script::Struct* params, script::Script* script);             // 0x004608D0
    bool obj_invisible(script::Struct* params, script::Script* script);           // 0x004608E0
    bool obj_get_id(script::Struct* params, script::Script* script);              // 0x00460940
    bool obj_get_tags(script::Struct* params, script::Script* script);            // 0x00460EB0 (fetch_labels)
    bool obj_get_single_tag(script::Struct* params, script::Script* script);      // 0x00460FB0 (GetSingleTag)
    bool obj_get_named_tags(script::Struct* params, script::Script* script);      // 0x00460FC0 (0xEEEB7220)
    bool obj_remove_tags(script::Struct* params, script::Script* script);         // 0x00460FF0 (RemoveTags)
    bool obj_flag_not_set(script::Struct* params, script::Script* script);        // 0x00461040
    bool obj_clear_flag(script::Struct* params, script::Script* script);          // 0x00461070
    bool obj_set_flag(script::Struct* params, script::Script* script);            // 0x004610C0
    bool obj_flag_set(script::Struct* params, script::Script* script);            // 0x004610F0
    bool obj_set_tags(script::Struct* params, script::Script* script);            // 0x004617C0 (SetTags)
    bool obj_spawn_script(script::Struct* params, script::Script* script);        // 0x004617E0
    bool obj_switch_script(script::Struct* params, script::Script* script);       // 0x004618A0

    // --- fields (after SharedBase's vptr and smart pointer list) ---
    uint32_t object_flags_ = 0;            // +0x08 OBJECT_FLAG_*
    ObjectHubBase* park_owner_ = nullptr;  // +0x0C the manager it is registered with
    script::Script* own_script_ = nullptr; // +0x10 its own script (made on demand)
    uint32_t ident_ = kUnassignedId;       // +0x14 kUnassignedId until registered
    uint32_t kind_tag_ = kNoObjectType;    // +0x18
    uint32_t visited_mask_ = 0;            // +0x1C ObjectHubBase::for_each's bits (one per walk in progress)
    script::Struct* tag_table_ = nullptr;  // +0x20 made on demand (front-end heap)
    uint32_t flag_bits_;                   // +0x24 the script flags (Obj_SetFlag...; never initialised here)
    // +0x28 in the manager's list (or its dead list; lists::Elem<Entity>: data = the object, priority = the list's
    // sort key, the highest first)
    memory::ListNode list_link_;
    uint32_t alias_count_ = 0; // +0x3C the tracker's aliases of it (tracker.cpp)
    ObjectRef ref_{this};      // +0x40
};

// The object with the id `id` (the tracker 0x006F0FC0's 0x00462120), or null.
// 0x00460AD0 (cdecl).
Entity* find_object(uint32_t id);

// The manager walk's callbacks (for_each, cdecl; `data` unused).
// 0x00460AC0: unlock the object.
void get_unlock(Entity* object, void* data);
// 0x00460AA0: delete the object unless it is locked.
void dispose_when_free(Entity* object, void* data);

class GameEntity;

// One of a composite object's components (GameObjects::ObjectModule, vtable 0x00647618 in the base; composite.cpp). A
// component belongs to one object (entity_) and is linked into its list (+0x10) and into the composite object
// manager's list of the components of its type (+0x14; 0x006F0FB8, wt/coim's 0x0045F3E0 / 0x0045F420).
//
// The virtuals are the original's slots in order (0 the destructor): 1 Update, 2 configure_from_data, 3 Finalize,
// 5 reapply_from_data, 6 a script waiting on it (cscript_wait.cpp), 7 hold_off, 8 conceal, 9 Teleport,
// 10 fetch_diagnostics, 11 a member function table entry, 12 a member function by name. Slot 4 (its object is being
// marked dead) is the same empty stub in every component's table: a plain method. The components with more slots
// (13-15: the model, the collision, the peds, the node lists) have them as plain methods of their own.
//
// Components are made from the current heap, zeroed (the original's makers), and deleted back to it. Not a
// memory::HeapObject: the scripts' member function tables name component methods through code_address, which an
// empty base would turn into the multiple-inheritance member pointer form.
class ObjectModule {
public:
    static void* operator new(std::size_t size) noexcept
    {
        return memory::alloc_zeroed(static_cast<unsigned int>(size));
    }
    static void operator delete(void* memory);
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void*, void*) noexcept {}

    // 0x0045D730: no flags (the rest is the zeroed memory it is made in).
    ObjectModule() {}
    // 0x0045D740 (0x0045D780 the deleting one): if a script waited on it, every script lets go of it
    // (component_destroyed_all_scripts).
    virtual ~ObjectModule();

    ObjectModule(const ObjectModule&) = delete;
    ObjectModule& operator=(const ObjectModule&) = delete;

    // 0x0051B970 (slot 1, Update - pure in the base's table; this body is the one every component with nothing to
    // update shares): suspends itself (hold_off), so it isn't updated again.
    virtual void tick_frame();
    // (slot 2 - pure in the base's table; the components without settings share an empty stub) Set up from `params`.
    virtual void configure_from_data(script::Struct* /*params*/) {}
    // (slot 3, Finalize) Once the object has all its components: nothing here.
    virtual void seal() {}
    // (slot 4, the same empty stub in every table) Its object is being marked dead.
    void object_marked_dead() {}
    // 0x004E3F90 (slot 5, a tail jump to slot 2): set up again from `params`.
    virtual void reapply_from_data(script::Struct* params) { configure_from_data(params); }
    // (slot 6, cscript_wait.cpp: a script waiting on the component with `arg` asks it whether to stop waiting) Nothing
    // here.
    virtual void step_wait_state(script::Script* /*script*/, uint32_t /*arg*/) {}
    // 0x0045D760 (slot 7): suspended or not (a suspended component isn't updated).
    virtual void hold_off(bool suspend);
    // (slot 8) Its object hidden or shown: nothing here.
    virtual void conceal(bool /*hide*/) {}
    // (slot 9, Teleport) Its object was moved without passing in between: nothing here.
    virtual void jump_to() {}
    // (slot 10) Its debug details added to `info`: nothing here.
    virtual void fetch_diagnostics(script::Struct* /*info*/) {}
    // 0x0045D720 (slot 11): a member function table entry, run by its name (invoke_method_by_name) - the base ignores
    // the entry's member function pointer.
    virtual int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script);
    // 0x0051ADC0 (slot 12, the base's and every component's without member functions of its own): a member function
    // by name: 0 = false, 1 = true, 2 = not one of its own.
    virtual int invoke_method_by_name(uint32_t /*name*/, script::Struct* /*params*/, script::Script* /*script*/)
    {
        return 2;
    }

    // 0x0042BB20 (slot 11 of most components, shared): the entry's member function pointer (on `this` moved by the
    // entry's adjustment), or, with none, invoke_method_by_name by the entry's name. Returns what that returns.
    int dispatch_script_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script);
    // 0x0041D700 (slot 11 of the flexible particle and animation blender components, shared): as
    // dispatch_script_method, but the member function is given the entry's name too.
    int dispatch_named_script_method(script::MemberFunctionEntry* entry, script::Struct* params,
                                     script::Script* script);

    uint32_t kind_tag_;          // +0x04 its component type (a checksum)
    uint32_t flag_bits_{0};      // +0x08 COMPONENT_FLAG_*
    GameEntity* entity_;         // +0x0C
    ObjectModule* following_;    // +0x10 the next of its object's components
    ObjectModule* next_of_type_; // +0x14 the next of the manager's components of its type
};
static_assert(offsetof(ObjectModule, kind_tag_) == 0x04 && offsetof(ObjectModule, flag_bits_) == 0x08 &&
                  offsetof(ObjectModule, entity_) == 0x0C && offsetof(ObjectModule, following_) == 0x10 &&
                  offsetof(ObjectModule, next_of_type_) == 0x14 && sizeof(ObjectModule) == 0x18,
              "layout must match the original (the components' fields start at 0x18)");

// ObjectModule::flag_bits_ (+0x08)
constexpr uint32_t kComponentFlagSuspended = 0x1;
constexpr uint32_t kComponentFlagScriptWaiting = 0x2; // a script waited on it (set_wait_on_component)

// GameEntity::composite_flags_ (+0x104)
constexpr uint32_t kCompositeFlagPaused = 0x1;    // not updated at all; its PAUSE_WITH_OBJECT scripts wait too
constexpr uint32_t kCompositeFlagSuspended = 0x2; // its script and own update skipped (components still run)
constexpr uint32_t kCompositeFlagHidden = 0x4;
constexpr uint32_t kCompositeFlagTeleported = 0x8;  // until the end of its next update
constexpr uint32_t kCompositeFlagFinalized = 0x20;  // Finalize (which also sets 0x40)
constexpr uint32_t kCompositeFlagFinalized2 = 0x40; // (set with 0x20; meaning unknown)
constexpr uint32_t kCompositeFlagDestroying = 0x80; // its destructor is running

// An object made of components (GameObjects::GameEntity, vtable 0x00647650; composite.cpp): skaters, peds, cars,
// level objects. It has a position, an orientation, a velocity and a display matrix; each frame its update runs
// its own script, then its own update (own_update, for subclasses), then its components'.
//
// The original's vtable (11 slots): Entity's, with 0 Destroy, 3 invoke_method_by_name (components first), 6
// flag_as_dead (components told first), 7 should_update_scripts (not paused), and three more: +0x20 Finalize (seal),
// +0x24 CreateComponentFromStructure, +0x28 its own update (empty here). Its subclasses (the crown 0x0064EC44, the
// skater 0x0064EF48, the viewer object 0x0064F138) keep slots 5, 8 and 9 as they are.
class GameEntity : public Entity {
public:
    // 0x0045E510: at the origin (w = 1), identity matrices, no velocity, no components, no flags;
    // kObjectFlagHasPosition. (old_pos_ isn't initialised.)
    GameEntity();
    // 0x0045D890 (0x0045E620 the deleting one): DESTROYING, each component (first to last) taken off the list, out
    // of the manager's and deleted, then Entity's.
    ~GameEntity() override;

    // 0x0045D920: one frame: unless paused, its script's Update and its own update (own_update) - both skipped
    // while suspended, and nothing more if that killed the object - then the Update of each component that isn't
    // suspended (stopping if the object dies); then at_point_.w = 1 and TELEPORTED cleared.
    void tick_frame();
    // 0x0045D990: hidden or not; when that changes, each component's conceal(hide).
    void conceal(bool hide);
    // 0x0045D9E0: suspended or not; when that changes, each component's hold_off(suspend).
    void hold_off(bool suspend);
    // 0x0045DA30: `component` added at the end of the list, its object set, and registered with the composite
    // object manager.
    void attach_module(ObjectModule* component);
    // 0x0045DA90: create_component_from_structure for each structure in `components`, with `extra`.
    void build_modules_from_list(script::Array* components, script::Struct* extra);
    // 0x0045DAD0: name= (the id), the position and orientation (0x0045C050 / 0x0045C0B0), `permanent` (locked).
    void configure_from_data(script::Struct* params);
    // 0x0045DB30 (the original's slot 8, Finalize - the same in every table): every component's Finalize,
    // FINALIZED, then every component's Teleport.
    void seal();
    // 0x0045DB90: its component of `type`, or null.
    ObjectModule* fetch_module(uint32_t type);
    // 0x0045E450: a member function by name: each component's (+0x30) in turn until one says 0 (false) or 1
    // (true), then Entity's.
    bool invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x0045D7C0: each component's +0x10, then Entity::flag_as_dead.
    void flag_as_dead() override;
    // 0x0045E4C0: TELEPORTED; with `teleport_components`, each component's Teleport too.
    void mark_jumped_ahead(bool teleport_components);
    // 0x0045E500: whether its PAUSE_WITH_OBJECT scripts may run (it isn't paused).
    bool should_update_scripts() override;
    // 0x0045EE20 (the original's slot 9, the same in every table): a component of the type `params` names
    // (component=), made by the composite object manager, added, and initialised from `params` (with `extra`, from a
    // merged copy).
    void create_component_from_structure(script::Struct* params, script::Struct* extra);

    // --- the member functions scripts call (the table at 0x00683268, component type 0x0ADA4E74) ---
    // (Names in brackets are the checksums of names not recovered; the C++ names say what they do.)
    bool obj_get_position(script::Struct* params, script::Script* script);           // 0x0045DBC0
    bool obj_print_details(script::Struct* params, script::Script* script);          // 0x0045D7F0
    bool obj_create_component(script::Struct* params, script::Script* script);       // 0x0045D810 [0x406998A5]
    bool obj_component_exists(script::Struct* params, script::Script* script);       // 0x0045DBF0 [0x2BFFEAA3]
    bool obj_set_position(script::Struct* params, script::Script* script);           // 0x0045D830
    bool obj_force_update(script::Struct* params, script::Script* script);           // 0x0045DC40
    bool obj_get_velocity(script::Struct* params, script::Script* script);           // 0x0045DC50
    bool get_speed(script::Struct* params, script::Script* script);                  // 0x0045DC80
    bool obj_hide(script::Struct* params, script::Script* script);                   // 0x0045DCD0 (Hide)
    bool obj_unhide(script::Struct* params, script::Script* script);                 // 0x0045DD20 (Unhide)
    bool obj_is_hidden(script::Struct* params, script::Script* script);              // 0x0045DD70 (is_concealed)
    bool obj_move_local(script::Struct* params, script::Script* script);             // 0x0045DD80 [0x10C1C887]
    bool obj_suspend(script::Struct* params, script::Script* script);                // 0x0045DED0 (Suspend)
    bool obj_unsuspend(script::Struct* params, script::Script* script);              // 0x0045DF20 (Unsuspend)
    bool obj_get_distance_to_object(script::Struct* params, script::Script* script); // 0x0045E0B0
    bool obj_moving_backward(script::Struct* params, script::Script* script);        // 0x0045E260 [0xF8CFD515]
    bool obj_speed_near(script::Struct* params, script::Script* script);             // 0x0045E2A0 [0x7DCC5FB9]
    bool obj_speed_above(script::Struct* params, script::Script* script);            // 0x0045E310 [0xE5DF66D7]
    bool obj_speed_below(script::Struct* params, script::Script* script);            // 0x0045E380 [0xDD468509]
    bool obj_get_type(script::Struct* params, script::Script* script);               // 0x0045E3F0
    bool obj_has_component(script::Struct* params, script::Script* script);          // 0x0045E410
    bool obj_pause(script::Struct* params, script::Script* script);                  // 0x0045E9B0 (Pause)
    bool obj_unpause(script::Struct* params, script::Script* script);                // 0x0045E9D0 (Unpause)
    // 0x0045E9E0 (Obj_GetCollision): whether the collision feeler hits anything along a line from its position
    // + up * height= + at * distance= + right * shift= (feet; height 3), length= (feet, 3) along right (side),
    // down (down) or at.
    bool obj_get_collision(script::Struct* params, script::Script* script);
    bool obj_get_orientation(script::Struct* params, script::Script* script);           // 0x0045DF70
    bool obj_get_dist_to_node(script::Struct* params, script::Script* script);          // 0x0045E020
    bool obj_get_orientation_to_object(script::Struct* params, script::Script* script); // 0x0045E140
    bool obj_set_orientation(script::Struct* params, script::Script* script);           // 0x0045E650
    bool obj_get_quat(script::Struct* params, script::Script* script);                  // 0x0045E940
    bool obj_nearest_outside_angle(script::Struct* params, script::Script* script);     // 0x0045EF30 [0x37185D8A]
    // 0x0045ED30: the nearest other object of `type` in its manager's list (the first one met if none is nearer
    // than 5322672), or null.
    GameEntity* find_nearest_object_of_type(uint32_t type);

    // (the original's slot 10, 0x005480A0 here) Its own update, each frame before its components': nothing here.
    virtual void own_update() {}

    maths::Vec4 at_point_;                     // +0x4C (w = 1 after every update)
    maths::Vec4 old_pos_;                      // +0x5C (not initialised here; the skater's code sets it)
    maths::Mat4 basis_frame_;                  // +0x6C its orientation (rows: right, up, at, and a fourth)
    maths::Vec4 vel_;                          // +0xAC
    uint8_t is_local_skater_ = 0;              // +0xBC set on this machine's skaters (not remote ones)
    maths::Mat4 display_matrix_;               // +0xC0
    ObjectModule* attached_modules_ = nullptr; // +0x100
    uint32_t composite_flags_ = 0;             // +0x104 COMPOSITE_FLAG_*
};

} // namespace objects

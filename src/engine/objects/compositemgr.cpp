// The composite object manager and the COIM heap (compositemgr.h).

#include "engine/objects/compositemgr.h"
#include "engine/objects/pager_message.h"

#include <cstdint>
#include <memory>
#include <new>

#include "engine/core/arena_alloc.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/sys/mlp.h"
#include "engine/assets/resource_registry.h"
#include "engine/objects/entity_hub.h"
#include "frontend/screen_element_manager.h"
#include "engine/objects/cutscene_list_cfuncs.h"
#include "engine/sys/startup_init.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/checked_index.h"
#include "engine/lists/pointer_ring.h"
#include "engine/core/no_destroy.h"

namespace objects {
namespace {
constexpr int kKilobyteShift = 10;
constexpr uint32_t kObjGetPosition = 0xE90AAD2D; // Obj_GetPosition
constexpr uint32_t kCompositeobject = 0x0ADA4E74; // compositeobject
constexpr uint32_t kObjPrintDetails = 0xC2404947; // Obj_PrintDetails
constexpr uint32_t kCreateComponentFromStructure = 0x406998A5; // CreateComponentFromStructure
constexpr uint32_t kContainsComponent = 0x2BFFEAA3; // ContainsComponent
constexpr uint32_t kObjSetPosition = 0xF7251A64; // Obj_SetPosition
constexpr uint32_t kObjSetOrientation = 0xC6F3BAA5; // Obj_SetOrientation
constexpr uint32_t kObjGetQuat = 0x77403AFC; // Obj_GetQuat
constexpr uint32_t kObjForceUpdate = 0xC1BFF0F3; // Obj_ForceUpdate
constexpr uint32_t kObjGetVelocity = 0x11FE9F71; // Obj_GetVelocity
constexpr uint32_t kGetSpeed = 0xC0CAAC4A; // GetSpeed
constexpr uint32_t kHide = 0x5B6634D4; // Hide
constexpr uint32_t kUnHide = 0xB60D1F35; // UnHide
constexpr uint32_t kIsHidden = 0xB16619AE; // IsHidden
constexpr uint32_t kMove = 0x10C1C887; // Move
constexpr uint32_t kSuspend = 0xCE0CA665; // Suspend
constexpr uint32_t kUnsuspend = 0xCA3C59A6; // Unsuspend
constexpr uint32_t kPause = 0x28656D12; // Pause
constexpr uint32_t kUnPause = 0xFF85D4D7; // UnPause
constexpr uint32_t kObjGetCollision = 0x0168B09C; // Obj_GetCollision
constexpr uint32_t kObjGetOrientation = 0xB9ED09D2; // Obj_GetOrientation
constexpr uint32_t kObjGetDistToNode = 0xD96C0A20; // Obj_GetDistToNode
constexpr uint32_t kObjGetDistanceToObject = 0x4AF57BBB; // Obj_GetDistanceToObject
constexpr uint32_t kObjGetOrientationToObject = 0x2A26FFA2; // Obj_GetOrientationToObject
constexpr uint32_t kBackwards = 0xF8CFD515; // Backwards
constexpr uint32_t kSpeedEquals = 0x7DCC5FB9; // SpeedEquals
constexpr uint32_t kSpeedGreaterThan = 0xE5DF66D7; // SpeedGreaterThan
constexpr uint32_t kSpeedLessThan = 0xDD468509; // SpeedLessThan
constexpr uint32_t kObjAngleToNearestSkaterGreaterThan = 0x37185D8A; // Obj_AngleToNearestSkaterGreaterThan
constexpr uint32_t kObjGetType = 0x70ED3048; // Obj_GetType
constexpr uint32_t kObjHasComponent = 0x33CAC5FF; // Obj_HasComponent
constexpr uint32_t kGetTags = 0x3611C136; // GetTags
constexpr uint32_t kObject = 0x57525413; // Object
constexpr uint32_t kGetSingleTag = 0x62C11CB9; // GetSingleTag
constexpr uint32_t kGetNamedTags = 0xEEEB7220; // text unknown (obj_get_named_tags)
constexpr uint32_t kSetTags = 0xA58079EB; // SetTags
constexpr uint32_t kRemoveTags = 0x0C7CD27A; // RemoveTags
constexpr uint32_t kDisassociateFromObject = 0xB3C262EC; // DisassociateFromObject
constexpr uint32_t kObjGetID = 0x500EB224; // Obj_GetID
constexpr uint32_t kObjFlagSet = 0x4BABC987; // Obj_FlagSet
constexpr uint32_t kObjFlagNotSet = 0x53EBEE03; // Obj_FlagNotSet
constexpr uint32_t kObjClearFlag = 0x6C2B67F9; // Obj_ClearFlag
constexpr uint32_t kObjSetFlag = 0xBE563426; // Obj_SetFlag
constexpr uint32_t kObjKillSpawnedScript = 0xFBD89CD5; // Obj_KillSpawnedScript
constexpr uint32_t kObjSpawnScript = 0x23A4E5C2; // Obj_SpawnScript
constexpr uint32_t kObjSwitchScript = 0x714937C7; // Obj_SwitchScript
constexpr uint32_t kObjVisible = 0x20D125D7; // Obj_Visible
constexpr uint32_t kObjInvisible = 0x3578E5A9; // Obj_Invisible
constexpr uint32_t kCoimUnusedInteger = 0xBA2C7CCB; // text unknown (a script integer read when the heap is made; the result is not used)
constexpr uint32_t kCoimInteger0 = 0xB5527B89; // text unknown (a script integer the manager keeps (the first))
constexpr uint32_t kCoimInteger1 = 0xD84C26CF; // text unknown (a script integer the manager keeps (the second))
} // namespace

std::array<script::MemberFunctionEntry, kCompositeObjectMemberFunctionCount>& composite_object_member_functions()
{
    static std::array<script::MemberFunctionEntry, kCompositeObjectMemberFunctionCount> composite_object_member_functions{{
        {.callsign_ = kObjGetPosition, .owner_kind_ = kCompositeobject}, {.callsign_ = kObjPrintDetails, .owner_kind_ = kCompositeobject}, {.callsign_ = kCreateComponentFromStructure, .owner_kind_ = kCompositeobject}, {.callsign_ = kContainsComponent, .owner_kind_ = kCompositeobject},
        {.callsign_ = kObjSetPosition, .owner_kind_ = kCompositeobject}, {.callsign_ = kObjSetOrientation, .owner_kind_ = kCompositeobject}, {.callsign_ = kObjGetQuat, .owner_kind_ = kCompositeobject}, {.callsign_ = kObjForceUpdate, .owner_kind_ = kCompositeobject},
        {.callsign_ = kObjGetVelocity, .owner_kind_ = kCompositeobject}, {.callsign_ = kGetSpeed, .owner_kind_ = kCompositeobject}, {.callsign_ = kHide, .owner_kind_ = kCompositeobject}, {.callsign_ = kUnHide, .owner_kind_ = kCompositeobject},
        {.callsign_ = kIsHidden, .owner_kind_ = kCompositeobject}, {.callsign_ = kMove, .owner_kind_ = kCompositeobject}, {.callsign_ = kSuspend, .owner_kind_ = kCompositeobject}, {.callsign_ = kUnsuspend, .owner_kind_ = kCompositeobject},
        {.callsign_ = kPause, .owner_kind_ = kCompositeobject}, {.callsign_ = kUnPause, .owner_kind_ = kCompositeobject}, {.callsign_ = kObjGetCollision, .owner_kind_ = kCompositeobject}, {.callsign_ = kObjGetOrientation, .owner_kind_ = kCompositeobject},
        {.callsign_ = kObjGetDistToNode, .owner_kind_ = kCompositeobject}, {.callsign_ = kObjGetDistanceToObject, .owner_kind_ = kCompositeobject}, {.callsign_ = kObjGetOrientationToObject, .owner_kind_ = kCompositeobject}, {.callsign_ = kBackwards, .owner_kind_ = kCompositeobject},
        {.callsign_ = kSpeedEquals, .owner_kind_ = kCompositeobject}, {.callsign_ = kSpeedGreaterThan, .owner_kind_ = kCompositeobject}, {.callsign_ = kSpeedLessThan, .owner_kind_ = kCompositeobject}, {.callsign_ = kObjAngleToNearestSkaterGreaterThan, .owner_kind_ = kCompositeobject},
        {.callsign_ = kObjGetType, .owner_kind_ = kCompositeobject}, {.callsign_ = kObjHasComponent, .owner_kind_ = kCompositeobject}, {.callsign_ = 0, .owner_kind_ = kCompositeobject}}};
    return composite_object_member_functions;
}

std::array<script::MemberFunctionEntry, kObjectMemberFunctionCount>& object_member_functions()
{
    static std::array<script::MemberFunctionEntry, kObjectMemberFunctionCount> object_member_functions{{
        {.callsign_ = kGetTags, .owner_kind_ = kObject}, {.callsign_ = kGetSingleTag, .owner_kind_ = kObject}, {.callsign_ = kGetNamedTags, .owner_kind_ = kObject}, {.callsign_ = kSetTags, .owner_kind_ = kObject},
        {.callsign_ = kRemoveTags, .owner_kind_ = kObject}, {.callsign_ = kDisassociateFromObject, .owner_kind_ = kObject}, {.callsign_ = kObjGetID, .owner_kind_ = kObject}, {.callsign_ = kObjFlagSet, .owner_kind_ = kObject},
        {.callsign_ = kObjFlagNotSet, .owner_kind_ = kObject}, {.callsign_ = kObjClearFlag, .owner_kind_ = kObject}, {.callsign_ = kObjSetFlag, .owner_kind_ = kObject}, {.callsign_ = kObjKillSpawnedScript, .owner_kind_ = kObject},
        {.callsign_ = kObjSpawnScript, .owner_kind_ = kObject}, {.callsign_ = kObjSwitchScript, .owner_kind_ = kObject}, {.callsign_ = kObjVisible, .owner_kind_ = kObject}, {.callsign_ = kObjInvisible, .owner_kind_ = kObject},
        {.callsign_ = 0, .owner_kind_ = kObject}}};
    return object_member_functions;
}

namespace {
constexpr int kCompositeMgrBlockHeaderSize{16};
// NOT ORIGINAL grouping - compositemgr.cpp's state (the original's globals). Made on first use, never destroyed
// (compositemgr_state()).
struct CompositemgrState {
    int coim_integer0_{4};
    int coim_integer1_{2};
    int coim_a_{};
    int coim_b_{};
    int coim_c_{};
    std::unique_ptr<memory::Zone> coim_region_;
    memory::Arena* coim_heap_{};
    // (one list per registered type: up to kMaxComponentTypes, the manager pointer follows at 0x006F0FB8)
    std::array<ObjectModule*, objects::EntityHub::kMaxComponentTypes> component_list_{};
    uint32_t composite_mgr_references_{};
};

CompositemgrState& compositemgr_state()
{
    static core::NoDestroy<CompositemgrState> state;
    return state.get();
}

} // namespace
namespace {

constexpr uint32_t kCompositeObjectType = 0xCDEB9229;
constexpr uint32_t kObjectMemberFunctionType = 0x57525413;    // the table at 0x00683550 (Entity's)
constexpr uint32_t kCompositeMemberFunctionType = 0x0ADA4E74; // the table at 0x00683268 (GameEntity's)

uint32_t& ref_tally() { return compositemgr_state().composite_mgr_references_; }

// The components of each type (index = the type's index in components_), linked through their +0x14.
ObjectModule*& component_list(uint32_t index) { return core::at(compositemgr_state().component_list_, index); }
// A component's type and its link to the next of its type.
uint32_t type_of(ObjectModule* component) { return component->kind_tag(); }
ObjectModule*& next_of_type(ObjectModule* component) { return component->next_of_type(); }

// A composite object's flags.
uint32_t& composite_flags(GameEntity* object) { return object->composite_flags(); }

// GameEntity's code (composite.cpp).

void update_composite_object(GameEntity* object) { object->tick_frame(); }
void configure_from_data(GameEntity* object, script::Struct* node_data) { object->configure_from_data(node_data); }
void build_modules_from_list(GameEntity* object, script::Array* components, script::Struct* node_data)
{
    object->build_modules_from_list(components, node_data);
}
void seal(GameEntity* object) { object->seal(); }

// The task system (Tsk): a task's constructor (priority), its removal from its list, and the main loop's logic task
// list's add.
void get_remove_task(tasks::SchedulerTask* task) { task->drop_out(); }
void register_logic_task(tasks::SchedulerTask* task) { assets::get_main_loop_manager()->logic_tasks().queue_job(task); }

// The node after `node`, or null at the head; a node's object (null for none).
memory::ListNode* get_next_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next();
    return next->data() == memory::ListNode::head_marker() ? nullptr : next;
}
GameEntity* object_of(memory::ListNode* node) { return node ? static_cast<GameEntity*>(node->data()) : nullptr; }

// The walk Update and Pause share (ObjectHubBase::for_each's, with the work inline): `visit` once for each object,
// starting again from the front (past the objects done) whenever the list changes.
template <typename Visit>
void walk_objects(ObjectHubBase* manager, Visit visit)
{
    uint32_t bit = 1;
    while (manager->walking() & bit) {
        bit <<= 1;
    }
    manager->walking() |= bit;
    for (memory::ListNode* node = get_next_node(&manager->entities()); node && node->data();
         node = get_next_node(node)) {
        static_cast<Entity*>(node->data())->visited_mask() &= ~bit;
    }
    for (bool again = true; again; again = manager->restart_walks() & bit) {
        manager->restart_walks() &= ~bit;
        memory::ListNode* node = get_next_node(&manager->entities());
        GameEntity* object = object_of(node);
        while (object) {
            memory::ListNode* next_node = get_next_node(node);
            GameEntity* next = object_of(next_node);
            if (!(object->visited_mask() & bit)) {
                object->visited_mask() |= bit;
                visit(object);
                if (manager->restart_walks() & bit) {
                    break;
                }
            }
            node = next_node;
            object = next;
        }
    }
    manager->walking() &= ~bit;
}

} // namespace

std::unique_ptr<objects::EntityHub>& object_hub_slot()
{
    static core::NoDestroy<std::unique_ptr<objects::EntityHub>> composite_registry;
    return composite_registry.get();
}

// 0x0045F6E0
EntityHub::EntityHub()
{
    update_job_ = std::make_unique<tasks::Task<EntityHub>>(&composite_object_manager_task, this, kLogicTaskPriority);
    register_logic_task(update_job_.get());
    script::register_member_function_table(object_member_functions());
    script::register_member_function_table(composite_object_member_functions());
}

// 0x0045F200
EntityHub::~EntityHub()
{
    if (update_job_) {
        get_remove_task(update_job_.get());
        update_job_.reset();
    }
}

// 0x0045F270
void EntityHub::enrol_module(uint32_t type, ComponentCreateFunction create, ComponentRegisterFunction register_function)
{
    core::at(components_, component_types_).type = type;
    core::at(components_, component_types_).create = create;
    ++component_types_;
    if (register_function) {
        register_function();
    }
}

// 0x0045F2A0
ObjectModule* EntityHub::spawn_module(uint32_t type)
{
    for (uint32_t i = 0; i < component_types_; ++i) {
        if (core::at(components_, i).type == type) {
            return core::at(components_, i).create();
        }
    }
    return nullptr;
}

// 0x0045F2D0
ObjectModule* EntityHub::find_first_module_of_kind(uint32_t type)
{
    for (uint32_t i = 0; i < component_types_; ++i) {
        if (core::at(components_, i).type == type) {
            return component_list(i);
        }
    }
    return nullptr;
}

// 0x0045F3E0
void EntityHub::attach_module_of_kind(ObjectModule* component)
{
    const uint32_t type = type_of(component);
    for (uint32_t i = 0; i < component_types_; ++i) {
        if (core::at(components_, i).type == type) {
            next_of_type(component) = component_list(i);
            component_list(i) = component;
            return;
        }
    }
}

// 0x0045F420. (A type registered twice: the lists of each of its entries are searched, in order.)
void EntityHub::detach_module_of_kind(ObjectModule* component)
{
    const uint32_t type = type_of(component);
    for (uint32_t i = 0; i < component_types_; ++i) {
        if (core::at(components_, i).type != type) {
            continue;
        }
        ObjectModule* previous = component_list(i);
        if (previous == component) {
            component_list(i) = next_of_type(component);
            next_of_type(component) = nullptr;
            return;
        }
        for (; previous; previous = next_of_type(previous)) {
            if (next_of_type(previous) == component) {
                next_of_type(previous) = next_of_type(component);
                next_of_type(component) = nullptr;
                return;
            }
        }
    }
}

// 0x0045F330
gsl::owner<GameEntity*> EntityHub::create_composite_object()
{
    gsl::owner<objects::GameEntity*> object{memory::make_owned<GameEntity>()};
    object->change_species(kCompositeObjectType); // (no memory: a crash, as the original)
    enrol_subject(object);
    return object;
}

// 0x0045F3A0
GameEntity* EntityHub::spawn_entity_from_node(script::Array* components, script::Struct* node_data, bool finalize)
{
    gsl::owner<GameEntity*> object = create_composite_object();
    configure_from_data(object, node_data);
    build_modules_from_list(object, components, node_data);
    if (finalize) {
        seal(object);
    }
    GameEntity* const made = object; // (the manager's lists hold it)
    return made;
}

// 0x0045F4A0
void EntityHub::tick_frame()
{
    walk_objects(this, [](GameEntity* object) { update_composite_object(object); });
}

// 0x0045F5D0
void EntityHub::get_freeze(bool pause)
{
    walk_objects(this, [pause](GameEntity* object) {
        if (pause) {
            composite_flags(object) |= kCompositeFlagPaused;
        } else {
            composite_flags(object) &= ~kCompositeFlagPaused;
        }
    });
}

// 0x0045F6D0
void composite_object_manager_task(tasks::Task<EntityHub>* task) { task->content()->tick_frame(); }

// 0x0045F7A0
EntityHub* create_composite_object_manager(bool create)
{
    if (!object_hub_slot() && create) {
        object_hub_slot() = std::make_unique<EntityHub>(); // (zeroed, from the current heap: the component count starts at 0)
    }
    ++ref_tally();
    return object_hub_slot().get();
}

// 0x0045F1E0
void release_composite_object_manager()
{
    uint32_t& references = ref_tally();
    if (references > 1) {
        --references;
        return;
    }
    // (not deleted: left as it is, the original's leak)
    [[maybe_unused]] const EntityHub* const left = object_hub_slot().release();
    references = 0;
}

// --- the COIM heap ---

namespace {

// An object of the COIM instance list (nothing in this build adds one; the original deleted them through their slot
// 1).
class CoimInstance {
public:
    virtual ~CoimInstance() = default;
};

// The instance list (a std::list of objects): its sentinel and its size, one object (the original's 0x006F0DB0 and
// the count after it - not original: they were two globals here, and the list's code wrote the size past the head's
// global).
lists::PointerRing<CoimInstance>& instance_list()
{
    static core::NoDestroy<lists::PointerRing<CoimInstance>> instance_list;
    return instance_list.get();
}
using MaterializeNode = lists::PointerRing<CoimInstance>::Node;
MaterializeNode* instance_list_head() { return instance_list().head(); }
int& instance_count() { return instance_list().size(); }
// std::list::erase(first, last) (0x004ECC30; the result iterator in `result`).
void erase_instances(MaterializeNode** result, MaterializeNode* first, MaterializeNode* last)
{
    *result = instance_list().erase(first, last);
}

std::unique_ptr<memory::Zone>& coim_region() { return compositemgr_state().coim_region_; }
memory::Arena*& coim_heap() { return compositemgr_state().coim_heap_; }
int& coim_a() { return compositemgr_state().coim_a_; }
int& coim_b() { return compositemgr_state().coim_b_; }
int& coim_c() { return compositemgr_state().coim_c_; }
int& coim_integer0() { return compositemgr_state().coim_integer0_; } // (4 until InitCOIM)
int& coim_integer1() { return compositemgr_state().coim_integer1_; } // (2 until InitCOIM)

} // namespace

// 0x0045EFD0
void destroy_coim_heap()
{
    debug::printf("Destroying COIM heap\n");
    if (instance_count() != 0) {
        MaterializeNode* head = instance_list_head();
        MaterializeNode* node = head ? head->next : nullptr;
        if (node != head) {
            for (bool again = true; again; again = node != instance_list_head()) {
                if (node->value) {
                    gsl::owner<decltype(node->value)> owned = memory::owned_element(node->value); // (the table's)
                    delete owned;
                    node->value = nullptr;
                }
                MaterializeNode* next = node->next;
                if (node != instance_list_head()) {
                    node->prev->next = node->next;
                    node->next->prev = node->prev;
                    memory::Hub::dealloc(node);
                    --instance_count();
                }
                node = next;
            }
        }
        head = instance_list_head();
        MaterializeNode* result = nullptr;
        erase_instances(&result, head ? head->next : nullptr, head);
    }
    if (coim_heap()) {
        memory::Hub::get_pull()->delete_heap(coim_heap());
        memory::destroy_then_clear(coim_region());
        coim_heap() = nullptr;
        coim_region() = nullptr;
    }
}

// 0x0045F0B0
bool create_coim_heap(uint32_t size)
{
    destroy_coim_heap();
    if (size != 0U) {
        coim_region() = std::make_unique<memory::MemoryArena>(size);
        coim_heap() = memory::Hub::get_pull()->add_heap(coim_region().get(), 1, "COIMHeap");
        coim_heap()->set_unknown64(true);
        coim_heap()->set_unknown68(coim_b() - kCompositeMgrBlockHeaderSize); // (16: a block header)
        script::fetch_int_value(kCoimUnusedInteger);                                    // (the result isn't used)
    }
    return false;
}

// 0x0045F160
void init_coim(int a, uint32_t heap_size, int b, int value_c)
{
    coim_a() = a;
    coim_b() = b;
    coim_integer0() = script::fetch_int_value(kCoimInteger0);
    coim_integer1() = script::fetch_int_value(kCoimInteger1);
    coim_c() = value_c;
    create_coim_heap(heap_size);
    debug::printf("Initializing CCompositeObjectInstanceMgr at %dk\n", heap_size >> kKilobyteShift);
}

// 0x0045F1C0
void resize_coim_heap(uint32_t size)
{
    if (size != 0U) {
        create_coim_heap(size);
    } else {
        destroy_coim_heap();
    }
}

} // namespace objects

namespace objects {
namespace {} // namespace
} // namespace objects

namespace objects {

namespace {

// A list (std::list<T*>) nothing uses but its static initialiser and exit function: an empty head node.
lists::PointerRing<void>& spare_object_list()
{
    static core::NoDestroy<lists::PointerRing<void>> spare_object_list;
    return spare_object_list.get();
}

} // namespace

// 0x006409F0 (a static initialiser)
void construct_spare_object_list() { spare_object_list().make(); }

// 0x00640A10 (a static initialiser): the instance list (its head and the count after it), empty.
void construct_instance_list() { instance_list().make(); }

} // namespace objects

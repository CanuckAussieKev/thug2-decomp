// The composite object manager and the COIM heap (compositemgr.h).

#include "engine/objects/compositemgr.h"
#include "engine/objects/pager_message.h"

#include <cstdint>
#include <new>

#include "engine/core/arena_alloc.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/sys/mlp.h"
#include "engine/assets/resource_registry.h"
#include "engine/core/code_address.h"
#include "engine/objects/entity_hub.h"
#include "frontend/screen_element_manager.h"
#include "engine/objects/cutscene_list_cfuncs.h"
#include "engine/sys/startup_init.h"

namespace objects {

uint32_t g_composite_object_member_functions[186]{
    0xE90AAD2D, 0x0ADA4E74, 0, 0, 0, 0, 0xC2404947, 0x0ADA4E74, 0, 0, 0, 0, 0x406998A5, 0x0ADA4E74, 0, 0, 0, 0,
    0x2BFFEAA3, 0x0ADA4E74, 0, 0, 0, 0, 0xF7251A64, 0x0ADA4E74, 0, 0, 0, 0, 0xC6F3BAA5, 0x0ADA4E74, 0, 0, 0, 0,
    0x77403AFC, 0x0ADA4E74, 0, 0, 0, 0, 0xC1BFF0F3, 0x0ADA4E74, 0, 0, 0, 0, 0x11FE9F71, 0x0ADA4E74, 0, 0, 0, 0,
    0xC0CAAC4A, 0x0ADA4E74, 0, 0, 0, 0, 0x5B6634D4, 0x0ADA4E74, 0, 0, 0, 0, 0xB60D1F35, 0x0ADA4E74, 0, 0, 0, 0,
    0xB16619AE, 0x0ADA4E74, 0, 0, 0, 0, 0x10C1C887, 0x0ADA4E74, 0, 0, 0, 0, 0xCE0CA665, 0x0ADA4E74, 0, 0, 0, 0,
    0xCA3C59A6, 0x0ADA4E74, 0, 0, 0, 0, 0x28656D12, 0x0ADA4E74, 0, 0, 0, 0, 0xFF85D4D7, 0x0ADA4E74, 0, 0, 0, 0,
    0x0168B09C, 0x0ADA4E74, 0, 0, 0, 0, 0xB9ED09D2, 0x0ADA4E74, 0, 0, 0, 0, 0xD96C0A20, 0x0ADA4E74, 0, 0, 0, 0,
    0x4AF57BBB, 0x0ADA4E74, 0, 0, 0, 0, 0x2A26FFA2, 0x0ADA4E74, 0, 0, 0, 0, 0xF8CFD515, 0x0ADA4E74, 0, 0, 0, 0,
    0x7DCC5FB9, 0x0ADA4E74, 0, 0, 0, 0, 0xE5DF66D7, 0x0ADA4E74, 0, 0, 0, 0, 0xDD468509, 0x0ADA4E74, 0, 0, 0, 0,
    0x37185D8A, 0x0ADA4E74, 0, 0, 0, 0, 0x70ED3048, 0x0ADA4E74, 0, 0, 0, 0, 0x33CAC5FF, 0x0ADA4E74, 0, 0, 0, 0,
    0,          0x0ADA4E74, 0, 0, 0, 0};

uint32_t g_object_member_functions[102]{
    0x3611C136, 0x57525413, 0, 0, 0, 0, 0x62C11CB9, 0x57525413, 0, 0, 0, 0, 0xEEEB7220, 0x57525413, 0, 0, 0, 0,
    0xA58079EB, 0x57525413, 0, 0, 0, 0, 0x0C7CD27A, 0x57525413, 0, 0, 0, 0, 0xB3C262EC, 0x57525413, 0, 0, 0, 0,
    0x500EB224, 0x57525413, 0, 0, 0, 0, 0x4BABC987, 0x57525413, 0, 0, 0, 0, 0x53EBEE03, 0x57525413, 0, 0, 0, 0,
    0x6C2B67F9, 0x57525413, 0, 0, 0, 0, 0xBE563426, 0x57525413, 0, 0, 0, 0, 0xFBD89CD5, 0x57525413, 0, 0, 0, 0,
    0x23A4E5C2, 0x57525413, 0, 0, 0, 0, 0x714937C7, 0x57525413, 0, 0, 0, 0, 0x20D125D7, 0x57525413, 0, 0, 0, 0,
    0x3578E5A9, 0x57525413, 0, 0, 0, 0, 0,          0x57525413, 0, 0, 0, 0};

int g_composite_mgr_block_header_size{16};
int g_coim_integer0{4};
int g_coim_integer1{2};
int g_coim_a;
int g_coim_b;
int g_coim_c;
memory::Zone* g_coim_region;
memory::Arena* g_coim_heap;
uint32_t g_instance_count;
// (one list per registered type: up to kMaxComponentTypes, the manager pointer follows at 0x006F0FB8)
ObjectModule* g_component_list[objects::EntityHub::kMaxComponentTypes];
objects::EntityHub* g_composite_registry;
uint32_t g_composite_mgr_references;

namespace {

constexpr uint32_t kCompositeObjectType = 0xCDEB9229;
constexpr uint32_t kObjectMemberFunctionType = 0x57525413;    // the table at 0x00683550 (Entity's)
constexpr uint32_t kCompositeMemberFunctionType = 0x0ADA4E74; // the table at 0x00683268 (GameEntity's)

uint32_t& ref_tally() { return g_composite_mgr_references; }

// The components of each type (index = the type's index in components_), linked through their +0x14.
ObjectModule*& component_list(uint32_t index) { return g_component_list[index]; }
// A component's type and its link to the next of its type.
uint32_t type_of(ObjectModule* component) { return component->kind_tag_; }
ObjectModule*& next_of_type(ObjectModule* component) { return component->next_of_type_; }

// A composite object's flags.
uint32_t& composite_flags(GameEntity* object) { return object->composite_flags_; }

// GameEntity's code (composite.cpp).
GameEntity* construct_composite_object(void* memory) { return static_cast<GameEntity*>(memory)->make_new(); }
void update_composite_object(GameEntity* object) { object->tick_frame(); }
void configure_from_data(GameEntity* object, script::Struct* node_data) { object->configure_from_data(node_data); }
void build_modules_from_list(GameEntity* object, script::Array* components, script::Struct* node_data)
{
    object->build_modules_from_list(components, node_data);
}
void seal(GameEntity* object) // its vtable +0x20
{
    reinterpret_cast<void(__thiscall*)(GameEntity*)>(object->vtable_[0x20 / 4])(object);
}

// The task system (Tsk): a task's constructor (priority), its removal from its list, and the main loop's logic task
// list's add.
void get_remove_task(tasks::SchedulerTask* task) { task->drop_out(); }
void register_logic_task(tasks::SchedulerTask* task) { assets::g_main_loop_manager->logic_tasks_.queue_job(task); }

// The node after `node`, or null at the head; a node's object (null for none).
memory::ListNode* get_next_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}
GameEntity* object_of(memory::ListNode* node) { return node ? static_cast<GameEntity*>(node->data) : nullptr; }

// The walk Update and Pause share (ObjectHubBase::for_each's, with the work inline): `visit` once for each object,
// starting again from the front (past the objects done) whenever the list changes.
template <typename Visit>
void walk_objects(ObjectHubBase* manager, Visit visit)
{
    uint32_t bit = 1;
    while (manager->walking_ & bit)
        bit <<= 1;
    manager->walking_ |= bit;
    for (memory::ListNode* node = get_next_node(&manager->entities_); node && node->data; node = get_next_node(node))
        static_cast<Entity*>(node->data)->visited_mask_ &= ~bit;
    do {
        manager->restart_walks_ &= ~bit;
        memory::ListNode* node = get_next_node(&manager->entities_);
        GameEntity* object = object_of(node);
        while (object) {
            memory::ListNode* next_node = get_next_node(node);
            GameEntity* next = object_of(next_node);
            if (!(object->visited_mask_ & bit)) {
                object->visited_mask_ |= bit;
                visit(object);
                if (manager->restart_walks_ & bit)
                    break;
            }
            node = next_node;
            object = next;
        }
    } while (manager->restart_walks_ & bit);
    manager->walking_ &= ~bit;
}

} // namespace

EntityHub*& object_hub_slot() { return g_composite_registry; }

// 0x0045F6E0
EntityHub::EntityHub()
{
    update_job_ = new tasks::Task<EntityHub>(&composite_object_manager_task, this, kLogicTaskPriority);
    register_logic_task(update_job_);
    script::register_member_function_table(reinterpret_cast<void*>(kObjectMemberFunctionType),
                                           g_object_member_functions, 0x18);
    script::register_member_function_table(reinterpret_cast<void*>(kCompositeMemberFunctionType),
                                           g_composite_object_member_functions, 0x18);
}

// 0x0045F200
EntityHub::~EntityHub()
{
    if (update_job_) {
        get_remove_task(update_job_);
        delete update_job_;
    }
}

// 0x0045F270
void EntityHub::enrol_module(uint32_t type, ComponentCreateFunction create, ComponentRegisterFunction register_function)
{
    components_[component_types_].type = type;
    components_[component_types_].create = create;
    ++component_types_;
    if (register_function)
        register_function();
}

// 0x0045F2A0
ObjectModule* EntityHub::spawn_module(uint32_t type)
{
    for (uint32_t i = 0; i < component_types_; ++i) {
        if (components_[i].type == type)
            return components_[i].create();
    }
    return nullptr;
}

// 0x0045F2D0
ObjectModule* EntityHub::find_first_module_of_kind(uint32_t type)
{
    for (uint32_t i = 0; i < component_types_; ++i) {
        if (components_[i].type == type)
            return component_list(i);
    }
    return nullptr;
}

// 0x0045F3E0
void EntityHub::attach_module_of_kind(ObjectModule* component)
{
    uint32_t type = type_of(component);
    for (uint32_t i = 0; i < component_types_; ++i) {
        if (components_[i].type == type) {
            next_of_type(component) = component_list(i);
            component_list(i) = component;
            return;
        }
    }
}

// 0x0045F420. (A type registered twice: the lists of each of its entries are searched, in order.)
void EntityHub::detach_module_of_kind(ObjectModule* component)
{
    uint32_t type = type_of(component);
    for (uint32_t i = 0; i < component_types_; ++i) {
        if (components_[i].type != type)
            continue;
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
GameEntity* EntityHub::create_composite_object()
{
    void* memory = memory::alloc_zeroed(0x108);
    GameEntity* object = memory ? construct_composite_object(memory) : nullptr;
    object->change_species(kCompositeObjectType);
    enrol_subject(object);
    return object;
}

// 0x0045F3A0
GameEntity* EntityHub::spawn_entity_from_node(script::Array* components, script::Struct* node_data, bool finalize)
{
    GameEntity* object = create_composite_object();
    configure_from_data(object, node_data);
    build_modules_from_list(object, components, node_data);
    if (finalize)
        seal(object);
    return object;
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
        if (pause)
            composite_flags(object) |= kCompositeFlagPaused;
        else
            composite_flags(object) &= ~kCompositeFlagPaused;
    });
}

// 0x0045F6D0
void __cdecl composite_object_manager_task(tasks::Task<EntityHub>* task) { task->content_->tick_frame(); }

// 0x0045F7A0
EntityHub* create_composite_object_manager(bool create)
{
    if (!object_hub_slot() && create) {
        object_hub_slot() = new EntityHub; // (zeroed, from the current heap: the component count starts at 0)
    }
    ++ref_tally();
    return object_hub_slot();
}

// 0x0045F1E0
void release_composite_object_manager()
{
    uint32_t& references = ref_tally();
    if (references > 1) {
        --references;
        return;
    }
    object_hub_slot() = nullptr; // (not deleted)
    references = 0;
}

// --- the COIM heap ---

namespace {

// The instance list (a std::list of objects with a virtual deleting destructor in slot 1): its head node and size.
struct MaterializeNode {
    void* object; // the object (its vtable first)
    MaterializeNode* next;
    MaterializeNode* prev;
};
MaterializeNode* g_instance_list_head;
MaterializeNode*& instance_list_head() { return g_instance_list_head; }
uint32_t& instance_count() { return g_instance_count; }
// std::list::erase(first, last) (0x004ECC30; the result iterator in `result`).
void erase_instances(MaterializeNode** result, MaterializeNode* first, MaterializeNode* last)
{
    reinterpret_cast<PointerList*>(&g_instance_list_head)
        ->get_erase(reinterpret_cast<PointerListNode**>(result), reinterpret_cast<PointerListNode*>(first),
                    reinterpret_cast<PointerListNode*>(last));
}

memory::Zone*& coim_region() { return g_coim_region; }
memory::Arena*& coim_heap() { return g_coim_heap; }
int& coim_a() { return g_coim_a; }
int& coim_b() { return g_coim_b; }
int& coim_c() { return g_coim_c; }
int& coim_integer0() { return g_coim_integer0; } // (4 until InitCOIM)
int& coim_integer1() { return g_coim_integer1; } // (2 until InitCOIM)

} // namespace

// 0x0045EFD0
void destroy_coim_heap()
{
    debug::printf("Destroying COIM heap\n");
    if (instance_count()) {
        MaterializeNode* head = instance_list_head();
        MaterializeNode* node = head ? head->next : nullptr;
        if (node != head) {
            do {
                if (node->object) {
                    void** vtable = *static_cast<void***>(node->object); // (slot 1: its deleting destructor)
                    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[1])(node->object, 1);
                    node->object = nullptr;
                }
                MaterializeNode* next = node->next;
                if (node != instance_list_head()) {
                    node->prev->next = node->next;
                    node->next->prev = node->prev;
                    memory::Hub::get_pull()->dealloc(node);
                    --instance_count();
                }
                node = next;
            } while (node != instance_list_head());
        }
        head = instance_list_head();
        MaterializeNode* result;
        erase_instances(&result, head ? head->next : nullptr, head);
    }
    if (coim_heap()) {
        memory::Hub::get_pull()->delete_heap(coim_heap());
        delete coim_region();
        coim_heap() = nullptr;
        coim_region() = nullptr;
    }
}

// 0x0045F0B0
bool create_coim_heap(uint32_t size)
{
    destroy_coim_heap();
    if (size) {
        coim_region() = new memory::MemoryArena(size);
        coim_heap() = memory::Hub::get_pull()->add_heap(coim_region(), 1, "COIMHeap");
        coim_heap()->unknown64_ = true;
        coim_heap()->unknown68_ = coim_b() - g_composite_mgr_block_header_size; // (16: a block header)
        script::fetch_int_value(0xBA2C7CCB);                                    // (the result isn't used)
    }
    return false;
}

// 0x0045F160
void init_coim(int a, uint32_t heap_size, int b, int c)
{
    coim_a() = a;
    coim_b() = b;
    coim_integer0() = script::fetch_int_value(0xB5527B89);
    coim_integer1() = script::fetch_int_value(0xD84C26CF);
    coim_c() = c;
    create_coim_heap(heap_size);
    debug::printf("Initializing CCompositeObjectInstanceMgr at %dk\n", heap_size >> 10);
}

// 0x0045F1C0
void resize_coim_heap(uint32_t size)
{
    if (size)
        create_coim_heap(size);
    else
        destroy_coim_heap();
}

} // namespace objects

namespace objects {
namespace {} // namespace
} // namespace objects

namespace objects {

namespace {

// A list (std::list<T*>) nothing uses but its static initialiser and exit function: an empty head node.
commands::PointerList g_spare_object_list;

} // namespace

// 0x006409F0 (a static initialiser)
void construct_spare_object_list() { g_spare_object_list.make_new(); }

// 0x00640A10 (a static initialiser): the instance list (its head and the count after it), empty.
void construct_instance_list() { reinterpret_cast<commands::PointerList*>(&g_instance_list_head)->make_new(); }

} // namespace objects

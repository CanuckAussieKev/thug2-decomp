#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include <gsl/pointers>
#include <array>

// GameObjects::EntityHub, the manager of every composite object (the singleton 0x006F0FB8, references
// 0x006F0FBC): the component types (who makes each one), the list of all components of each type, making composite
// objects, and the logic task that updates them all once a frame. Also the COIM heap (the remains of a
// "CCompositeObjectInstanceMgr": a heap of its own, sized by the script command InitCOIM / ResizeCOIMHeap).
// From the disassembly of 0x0045EFD0-0x0045F7A0 (methods __thiscall, RET n = 4 * arguments; the rest __cdecl).
namespace objects {

class ObjectModule;

// A component type's maker: returns a new component (no arguments, cdecl).
using ComponentCreateFunction = ObjectModule* (*)();
// Called once when the type is registered (its member functions for scripts...).
using ComponentRegisterFunction = void (*)();

// (GameEntity's +0x104 bits: COMPOSITE_FLAG_* in entity.h.)

class EntityHub : public MiscHub {
public:
    EntityHub(const EntityHub&) = delete;
    EntityHub& operator=(const EntityHub&) = delete;
    static constexpr int kMaxComponentTypes = 0x80;
    static constexpr int kLogicTaskPriority = -900; // the main loop's logic tasks (the front end's: -2000)

    // 0x0045F6E0: a MiscHub (vtable 0x006476D4), its logic task (priority -900) on the main loop's logic task list,
    // and the member functions of objects (0x00683550) and of composite objects (0x00683268) registered for
    // scripts. The component count isn't set: the instance is made zeroed (0x0045F7A0).
    EntityHub();
    // 0x0045F200 (0x0045F300 the deleting one): the logic task off its list and deleted, then MiscHub's.
    ~EntityHub() override;

    // 0x0045F270: a component type: `create` makes one; `register_function` (if any) is called at once. (No check
    // against kMaxComponentTypes.)
    void enrol_module(uint32_t type, ComponentCreateFunction create, ComponentRegisterFunction register_function);
    // 0x0045F2A0: a new component of `type` (its maker's result), or null for a type not registered.
    ObjectModule* spawn_module(uint32_t type);
    // 0x0045F2D0: the first component of `type` (the newest; each one's +0x14 is the next), or null.
    ObjectModule* find_first_module_of_kind(uint32_t type);
    // 0x0045F3E0: `component` goes to the front of its type's list (its +0x04 type; not registered: nothing).
    void attach_module_of_kind(ObjectModule* component);
    // 0x0045F420: `component` out of its type's list (its +0x14 cleared); not in it: nothing.
    void detach_module_of_kind(ObjectModule* component);

    // 0x0045F330: a new composite object (0x108 bytes, GameEntity's constructor 0x0045E510), of type
    // 0xCDEB9229, registered with this manager. Out of memory: crash (change_species on null).
    gsl::owner<GameEntity*> create_composite_object();
    // 0x0045F3A0: a new composite object set up from `node_data` (0x0045DAD0), with the components `components`
    // names (0x0045DA90, each with `node_data`), then, with `finalize`, its vtable +0x20 (Finalize).
    GameEntity* spawn_entity_from_node(script::Array* components, script::Struct* node_data, bool finalize);

    // 0x0045F4A0: every object's update (GameEntity 0x0045D920), in list order (node priority, highest
    // first), each once even if updates register or unregister objects (the walk starts again from the front,
    // skipping those done - as ObjectHubBase::for_each).
    void tick_frame();
    // 0x0045F5D0: kCompositeFlagPaused set (or cleared) on every object it has now - the same walk.
    void get_freeze(bool pause);

    std::unique_ptr<tasks::Task<EntityHub>> update_job_; // +0x40 running composite_object_manager_task
    uint32_t component_types_{};                         // +0x44
    struct ComponentType {
        uint32_t type;
        ComponentCreateFunction create;
    };
    std::array<ComponentType, kMaxComponentTypes> components_{}; // +0x48
};

// 0x0045F6D0: the logic task's code: Update on its data (the manager).
void composite_object_manager_task(tasks::Task<EntityHub>* task);

// The singleton (0x006F0FB8).
std::unique_ptr<EntityHub>& object_hub_slot();
// 0x0045F1E0: drop a reference; the last one only forgets the instance (it isn't deleted).
void release_composite_object_manager();
// 0x0045F7A0: the first call with `create` makes it (zeroed, 0x448 bytes); every call takes a reference and returns
// it.
EntityHub* create_composite_object_manager(bool create);

// --- the COIM heap ---
// 0x0045EFD0: "Destroying COIM heap": the instance list (0x006F0DB0, never filled in this build) emptied, each
// entry's object deleted; then the heap removed from the memory manager and its region deleted.
void destroy_coim_heap();
// 0x0045F0B0: the heap destroyed, then, if `size`, made again: "COIMHeap" (bottom-up) over a region of `size` bytes.
// Returns false.
bool create_coim_heap(uint32_t size);
// 0x0045F160 (InitCOIM's work, its parameters 0xBB9A31B5, 0x083FDB95, 0xB0847D54 and the float 0x03D6DE47
// truncated): `a`, `b` and `c` kept (0x006F0D8C, 0x006F0D90 - `b` less a block header becomes the heap's +0x68 -
// and 0x006F0D98), the script integers 0xB5527B89 and 0xD84C26CF read into 0x0067D804 / 0x0067D808, the heap made
// `heapSize` bytes. Nothing else reads any of them.
void init_coim(int a, uint32_t heap_size, int b, int c);
// 0x0045F1C0 (ResizeCOIMHeap's work): the heap made again `size` bytes, or destroyed for 0.
void resize_coim_heap(uint32_t size);

extern float g_pi;
} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
std::array<script::MemberFunctionEntry, 31>& composite_object_member_functions();
std::array<script::MemberFunctionEntry, 17>& object_member_functions();
} // namespace objects

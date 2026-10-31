#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"
#include "game/obj/objman.h"

// Obj::CCompositeObjectManager, the manager of every composite object (the singleton 0x006F0FB8, references
// 0x006F0FBC): the component types (who makes each one), the list of all components of each type, making composite
// objects, and the logic task that updates them all once a frame. Also the COIM heap (the remains of a
// "CCompositeObjectInstanceMgr": a heap of its own, sized by the script command InitCOIM / ResizeCOIMHeap).
// From the disassembly of 0x0045EFD0-0x0045F7A0 (methods __thiscall, RET n = 4 * arguments; the rest __cdecl).
namespace Obj {

class CBaseComponent;

// A component type's maker: returns a new component (no arguments, cdecl).
using ComponentCreateFunction = CBaseComponent*(__cdecl*)();
// Called once when the type is registered (its member functions for scripts...).
using ComponentRegisterFunction = void(__cdecl*)();

// (CCompositeObject's +0x104 bits: COMPOSITE_FLAG_* in object.h.)

class CCompositeObjectManager : public CGeneralManager {
public:
    static constexpr int kMaxComponentTypes = 0x80;
    static constexpr int kLogicTaskPriority = -900; // the main loop's logic tasks (the front end's: -2000)

    // 0x0045F6E0: the constructor (returns this): a CGeneralManager (vtable 0x006476D4), its logic task (priority
    // -900) on the main loop's logic task list, and the member functions of objects (0x00683550) and of composite
    // objects (0x00683268) registered for scripts. The component count isn't set: the instance is made zeroed
    // (0x0045F7A0).
    CCompositeObjectManager* Construct();
    // 0x0045F200: the destructor body: the logic task off its list and deleted, then CGeneralManager's.
    void DestroyBody();
    // 0x0045F300: the scalar deleting destructor (vtable slot 0).
    CCompositeObjectManager* Destroy(unsigned int flags);

    // 0x0045F270: a component type: `create` makes one; `registerFunction` (if any) is called at once. (No check
    // against kMaxComponentTypes.)
    void RegisterComponent(uint32_t type, ComponentCreateFunction create, ComponentRegisterFunction registerFunction);
    // 0x0045F2A0: a new component of `type` (its maker's result), or null for a type not registered.
    CBaseComponent* CreateComponent(uint32_t type);
    // 0x0045F2D0: the first component of `type` (the newest; each one's +0x14 is the next), or null.
    CBaseComponent* GetFirstComponentByType(uint32_t type);
    // 0x0045F3E0: `component` goes to the front of its type's list (its +0x04 type; not registered: nothing).
    void AddComponentByType(CBaseComponent* component);
    // 0x0045F420: `component` out of its type's list (its +0x14 cleared); not in it: nothing.
    void RemoveComponentByType(CBaseComponent* component);

    // 0x0045F330: a new composite object (0x108 bytes, CCompositeObject's constructor 0x0045E510), of type
    // 0xCDEB9229, registered with this manager (vtable slot 1). Out of memory: crash (SetType on null).
    CCompositeObject* CreateCompositeObject();
    // 0x0045F3A0: a new composite object set up from `nodeData` (0x0045DAD0), with the components `components`
    // names (0x0045DA90, each with `nodeData`), then, with `finalize`, its vtable +0x20 (Finalize).
    CCompositeObject* CreateCompositeObjectFromNode(Script::CArray* components, Script::CStruct* nodeData,
                                                    bool finalize);

    // 0x0045F4A0: every object's update (CCompositeObject 0x0045D920), in list order (node priority, highest
    // first), each once even if updates register or unregister objects (the walk starts again from the front,
    // skipping those done - as CBaseManager::ForEach).
    void Update();
    // 0x0045F5D0: COMPOSITE_FLAG_PAUSED set (or cleared) on every object it has now - the same walk.
    void Pause(bool pause);

    void* m_logicTask;        // +0x40 a Tsk::Task (vtable 0x006476F0) running UpdateTask
    uint32_t m_componentTypes; // +0x44
    struct ComponentType {
        uint32_t type;
        ComponentCreateFunction create;
    };
    ComponentType m_components[kMaxComponentTypes]; // +0x48
};
static_assert(offsetof(CCompositeObjectManager, m_logicTask) == 0x40 &&
                  offsetof(CCompositeObjectManager, m_componentTypes) == 0x44 &&
                  offsetof(CCompositeObjectManager, m_components) == 0x48 && sizeof(CCompositeObjectManager) == 0x448,
              "layout must match the original (0x0045F7A0 allocates 0x448 bytes)");

// 0x0045F6D0: the logic task's code: Update on its data (the manager).
void __cdecl CompositeObjectManagerTask(void* task);

// The singleton (0x006F0FB8).
CCompositeObjectManager*& CompositeObjectManager();
// 0x0045F1E0: drop a reference; the last one only forgets the instance (it isn't deleted).
void ReleaseCompositeObjectManager();
// 0x0045F7A0: the first call with `create` makes it (zeroed, 0x448 bytes); every call takes a reference and returns
// it.
CCompositeObjectManager* CreateCompositeObjectManager(bool create);

// --- the COIM heap ---
// 0x0045EFD0: "Destroying COIM heap": the instance list (0x006F0DB0, never filled in this build) emptied, each
// entry's object deleted; then the heap removed from the memory manager and its region deleted.
void DestroyCoimHeap();
// 0x0045F0B0: the heap destroyed, then, if `size`, made again: "COIMHeap" (bottom-up) over a region of `size` bytes.
// Returns false.
bool CreateCoimHeap(uint32_t size);
// 0x0045F160 (InitCOIM's work, its parameters 0xBB9A31B5, 0x083FDB95, 0xB0847D54 and the float 0x03D6DE47
// truncated): `a`, `b` and `c` kept (0x006F0D8C, 0x006F0D90 - `b` less a block header becomes the heap's +0x68 -
// and 0x006F0D98), the script integers 0xB5527B89 and 0xD84C26CF read into 0x0067D804 / 0x0067D808, the heap made
// `heapSize` bytes. Nothing else reads any of them.
void InitCoim(int a, uint32_t heapSize, int b, int c);
// 0x0045F1C0 (ResizeCOIMHeap's work): the heap made again `size` bytes, or destroyed for 0.
void ResizeCoimHeap(uint32_t size);

extern Obj::CCompositeObjectManager* g_compositeObjectManager;

extern float g_pi;
} // namespace Obj

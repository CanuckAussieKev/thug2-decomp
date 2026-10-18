#pragma once

#include <cstddef>
#include <cstdint>

#include "game/sys/module.h"

// The viewer module (Mdl::Viewer: the debug viewer for looking at models and levels, driven from a PC tool over
// the network, and the pad's viewer buttons). Its constructor and most of its code follow in 0x00577300-; what is
// here (0x00576CB0-0x005772E0) is its destructor and stop callback, its input handlers, the viewer object it owns,
// and a few helpers.
namespace Inp {
struct Data;
}
namespace Obj {
class CCompositeObject;
}
namespace Mth {
class Vector;
}
namespace Script {
class CScript;
}

namespace Mdl {

struct Viewer;

// Inp::Handler<Viewer> (0x2C bytes, vtable 0x0064D7D8 - the template's, shared with the front end's handlers).
struct ViewerInputHandler {
    uint8_t m_node[0x18];            // the handler list node (0x00406890, with the priority)
    Inp::Data* m_controlData;        // +0x18: the controller's data this frame
    uint8_t* m_device;               // +0x1C
    int m_index;                     // +0x20: the controller (the script integer 0x702247A5)
    void(__cdecl* m_code)(ViewerInputHandler*);
    Viewer* m_data;

    // 0x00577270: Inp::Handler<T>'s vtable slot 0 (every T's): the code, given the handler.
    void Handle();
    // 0x00577280: Inp::Handler<T>'s destructor body: Inp::BaseHandler's vtable (0x00646090), then the list node's
    // destructor (0x00406930).
    void DestroyBody();
    // 0x00577290: a jump to the list node's destructor (0x00406930).
    void DestroyNode();
};
static_assert(sizeof(ViewerInputHandler) == 0x2C && offsetof(ViewerInputHandler, m_code) == 0x24);

struct Viewer : Module {
    // The commands the pad gives (m_commands bits).
    enum : uint32_t {
        kCommandShoulderA = 0x2,
        kCommandShoulderB = 0x4,
    };

    ViewerInputHandler* m_inputHandler; // +0x14 (HandleInput, priority 0)
    ViewerInputHandler* m_shiftHandler; // +0x18 (HandleShift, priority 1000)
    void* m_task1C;                     // +0x1C a Tsk::Task<Viewer> (0x00577870)
    void* m_task20;                     // +0x20 a Tsk::Task<Viewer> (0x00577380)
    float m_rightX;                     // +0x24 the sticks (raw, -128..127)
    float m_rightY;                     // +0x28
    uint8_t m_unknown2C[8];
    float m_leftX;                      // +0x34
    float m_leftY;                      // +0x38
    uint8_t m_unknown3C[8];
    uint32_t m_commands;                // +0x44 the buttons' commands (bits; AddCommands)
    uint32_t m_shiftMakes;              // +0x48 the buttons pressed with select held
    int m_mode;                         // +0x4C 0: object mode, 1: ...
    void* m_netHandler;                 // +0x50 the PC tool's message handler (vtable; +0x0C a list node)
    void* m_object;                     // +0x54 the viewer object (0x128 bytes, 0x0058ADB0)

    // 0x00576DC0: the destructor body: the handlers, tasks and net handler deleted, the singleton cleared, then
    // Module's. 0x00577230: the scalar deleting destructor (vtable slot 0).
    void DestroyBody();
    Viewer* Destroy(uint8_t flags);
    // 0x00576D70: v_stop_cb (vtable slot 2): the viewer object deleted, the handlers and tasks off their lists,
    // the net handler's node off its list.
    void StopCallback();
    // 0x00576D40: the viewer object (if any) shut down (0x005897B0) and deleted.
    void RemoveViewerObject();
    // 0x00577170: a new viewer object (in the debug heap, or the bottom-up one), set up from `data`.
    void CreateViewerObject(void* data);
    // 0x00576E60
    void* GetViewerObject();
    // 0x00577260: commands added (OR).
    void AddCommands(uint32_t commands);
};
static_assert(offsetof(Viewer, m_inputHandler) == 0x14 && offsetof(Viewer, m_commands) == 0x44 &&
                  offsetof(Viewer, m_object) == 0x54,
              "layout must match the original");

// 0x00576F00: the input handler's code: the sticks into the viewer, and (if viewer_buttons_enabled) the buttons
// as commands and the User Viewer scripts; only in a view mode (SetViewMode).
void __cdecl HandleViewerInput(ViewerInputHandler* handler);
// 0x00576EB0: the shift handler's code: with select held, the buttons pressed noted, and (select_shift) the
// controller's buttons and sticks hidden from the other handlers.
void __cdecl HandleViewerShift(ViewerInputHandler* handler);
// 0x00577140: the PC tool's "remove the object" message: the viewer object deleted. Returns 1.
int __cdecl HandleRemoveViewerObject(void** context);
// 0x00576E70 / 0x00576E80 / 0x00576E90: the script commands SetViewMode's, SetMovementVelocity's (400 to start
// with) and SetRotateVelocity's (120) values. 0x00576EA0: the singleton (0x007CE4A8).
void SetViewMode(int mode);
void SetMovementVelocity(float velocity);
void SetRotateVelocity(float velocity);
Viewer* GetViewer();
// 0x00576CB0: spawn script `name` if there is one (null if not).
Script::CScript* SpawnScriptIfExists(uint32_t name);
// 0x00576CF0: a script file reloaded by the PC tool: runnow.qb (or ..\runnow.qb) noted (0x007CE4A0), then the
// file loaded (0x005A5960). Returns 1.
int __cdecl ReloadScriptFile(const char** name);
// 0x005772E0: `object` moved by `offset` along its own axes (its matrix's rotation).
void MoveObjectLocal(Obj::CCompositeObject* object, const Mth::Vector* offset);

} // namespace Mdl

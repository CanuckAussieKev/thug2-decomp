#pragma once

#include <cstddef>
#include <cstdint>

#include "game/sys/module.h"

// The viewer module (Mdl::Viewer: the debug viewer for looking at models and levels, driven from a PC tool over
// the network, and the pad's viewer buttons) - 0x00576CB0-0x00577F20: its constructor, start and stop callbacks and
// destructor, its input handlers and tasks (the free camera, the Select combos), the PC tool's messages, the viewer
// object it owns, and a few helpers.
namespace Inp {
struct Data;
}
namespace Net {
struct MsgHandlerContext;
}
namespace Script {
class CStruct;
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

// Tsk::Task<Viewer> (0x20 bytes, vtable 0x006476F0): a node of the main loop's task list.
struct ViewerTask {
    uint8_t m_node[0x18]; // the task list node (0x00406890 constructs it, with the priority)
    void(__cdecl* m_code)(ViewerTask*);
    Viewer* m_data;
};
static_assert(sizeof(ViewerTask) == 0x20 && offsetof(ViewerTask, m_code) == 0x18);

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

    // 0x00577F20: the constructor (only CFunc_LaunchViewer 0x005A54E0 makes one): the module, no commands; unless
    // the debug checks are on (0x00684FA8 - they are in this exe), the PC tool's app server "THUG2 Viewer" (port
    // 10000); the free camera task and the input handler, the speeds (move 400, turn 120), the Select combo task
    // and the shift handler (priority 1000) - the handlers on the controller the script integer 0x702247A5 names;
    // the singleton. Returns this.
    Viewer* Construct();
    // 0x00577DA0: v_start_cb (vtable slot 1), in the front-end heap: the PC tool's message handlers (unless the
    // debug checks are on), the tasks onto the main loop's list, the handlers onto their controller's.
    void StartCallback();
    // 0x00577740: with ResetCameraToViewerObject set, the camera object viewer_cam put back: identity orientation,
    // at ViewerObjPos (`params`) raised by (0, 35, 100) - (0, 35, 100) itself without one. Returns true.
    bool PlaceCamera(Script::CStruct* params);
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
// 0x00577870: the free camera task (once a frame): the frame's time; the mode toggled (command 0x4000, only to the
// object mode when there is a viewer object); the sticks' dead zone (+-50 of the raw values, then scaled); then on
// viewer_cam: in the camera mode the left stick turns it (up / down about its own x axis, left / right about the
// world's y), in the object mode it goes to the viewer object (0x0058A2E0); the right stick moves it along its own
// axes, commands 0x2 / 0x4 up / down; unless the script integer 0xD8C91578 is set, commands 0x10000 / 0x20000
// roll it. Then the camera object unpaused, command 0x1000 the next of six settings (0x004A0710 gets the current
// one, 0x004A0AD0 sets one: a table at 0x00649CC8), the commands cleared. (Without viewer_cam the original carries
// on with a null object: the moves and the unpausing write through it - a crash.)
void __cdecl FreeCameraTask(ViewerTask* task);
// 0x00577380: the Select combo task: with select_shift set, the UserSelect script of the first button pressed
// with Select held (by the shift handler: m_shiftMakes) - in the order Triangle, Square, Circle, Start, X, L1, L2,
// R1, R2, Up, Down, Left, Right - spawned if there is one.
void __cdecl SelectComboTask(ViewerTask* task);
// The PC tool's messages (cdecl handlers, each returns 1; context->m_Msg is the message):
// 0x00577CD0 (0x20): ReloadScene with scene = the message (a string).
int __cdecl HandleReloadScene(Net::MsgHandlerContext* context);
// 0x005774C0 (0x2A): AddToScene with scene = the message, Add = the string at +0x80.
int __cdecl HandleAddToScene(Net::MsgHandlerContext* context);
// 0x005775A0 (0x23): a new viewer object from modelname = the message, skeletonName = the checksum at +0x84. (The
// viewer is taken from the message's first word too - as the original reads it: the handler's data, the viewer, is
// at +0x18.)
int __cdecl HandleCreateViewerObject(Net::MsgHandlerContext* context);
// 0x00577670 (0x2B): the script named by the message's first word run with the structure written after it.
int __cdecl HandleRunScript(Net::MsgHandlerContext* context);
// 0x00577140: the PC tool's "remove the object" message (0x24): the viewer object deleted (the viewer from the
// message's first word, like 0x005775A0). Returns 1.
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

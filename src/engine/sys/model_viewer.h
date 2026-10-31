#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/pad_input/input_hub.h"
#include "engine/sys/game_module.h"

// The viewer module (modules::Viewer: the debug viewer for looking at models and levels, driven from a PC tool over
// the network, and the pad's viewer buttons) - 0x00576CB0-0x00577F20: its constructor, start and stop callbacks and
// destructor, its input handlers and tasks (the free camera, the Select combos), the PC tool's messages, the viewer
// object it owns, and a few helpers.
namespace net {
struct InboundMsgCtx;
}
namespace script {
class Struct;
}
namespace objects {
class GameEntity;
}
namespace maths {
class Vec4;
}
namespace script {
class Script;
}

namespace modules {

struct Viewer;

// The viewer's input handlers and tasks.
using ViewerInputHandler = pad_input::Responder<Viewer>;
using ViewerTask = tasks::Task<Viewer>;

struct Viewer final : public PartUnit {
    // The commands the pad gives (actions_ bits).
    enum : uint32_t {
        kCommandShoulderA = 0x2,
        kCommandShoulderB = 0x4,
    };

    ViewerInputHandler* pad_listener_;  // +0x14 (process_pad_input, priority 0)
    ViewerInputHandler* shift_handler_; // +0x18 (HandleShift, priority 1000)
    ViewerTask* task_1c_;               // +0x1C free_camera_task (0x00577870)
    ViewerTask* task20_;                // +0x20 select_combo_task (0x00577380)
    float pad_rx_;                      // +0x24 the sticks (raw, -128..127)
    float pad_ry_;                      // +0x28
    uint8_t unknown2c_[8];
    float pad_lx_; // +0x34
    float pad_ly_; // +0x38
    uint8_t unknown3c_[8];
    uint32_t actions_;     // +0x44 the buttons' commands (bits; add_commands)
    uint32_t shift_makes_; // +0x48 the buttons pressed with select held
    int mode_sel_;         // +0x4C 0: object mode, 1: ...
    void* net_handler_;    // +0x50 the PC tool's message handler (vtable; +0x0C a list node)
    void* entity_;         // +0x54 the viewer object (0x128 bytes, 0x0058ADB0)

    // 0x00577F20: the constructor (only cfunc_launch_viewer 0x005A54E0 makes one): the module, no commands; unless
    // the debug checks are on (0x00684FA8 - they are in this exe), the PC tool's app server "THUG2 Viewer" (port
    // 10000); the free camera task and the input handler, the speeds (move 400, turn 120), the Select combo task
    // and the shift handler (priority 1000) - the handlers on the controller the script integer 0x702247A5 names;
    // the singleton.
    Viewer();
    // 0x00577DA0: in the front-end heap: the PC tool's message handlers (unless the debug checks are on), the tasks
    // onto the main loop's list, the handlers onto their controller's.
    void start_callback() override;
    // 0x00577740: with ResetCameraToViewerObject set, the camera object viewer_cam put back: identity orientation,
    // at ViewerObjPos (`params`) raised by (0, 35, 100) - (0, 35, 100) itself without one. Returns true.
    bool place_camera(script::Struct* params);
    // 0x00576DC0 / 0x00577230: the handlers, tasks and net handler deleted, the singleton cleared.
    ~Viewer() override;
    // 0x00576D70: the viewer object deleted, the handlers and tasks off their lists, the net handler's node off its
    // list.
    void stop_callback() override;
    // 0x00576D40: the viewer object (if any) shut down (0x005897B0) and deleted.
    void drop_viewer_entity();
    // 0x00577170: a new viewer object (in the debug heap, or the bottom-up one), set up from `data`.
    void create_viewer_object(void* data);
    // 0x00576E60
    void* get_viewer_entity();
    // 0x00577260: commands added (OR).
    void add_commands(uint32_t commands);
};
static_assert(offsetof(Viewer, pad_listener_) == 0x14 && offsetof(Viewer, actions_) == 0x44 &&
                  offsetof(Viewer, entity_) == 0x54,
              "layout must match the original");

// 0x00576F00: the input handler's code: the sticks into the viewer, and (if viewer_buttons_enabled) the buttons
// as commands and the User Viewer scripts; only in a view mode (change_view_mode).
void __cdecl handle_viewer_input(ViewerInputHandler* handler);
// 0x00576EB0: the shift handler's code: with select held, the buttons pressed noted, and (select_shift) the
// controller's buttons and sticks hidden from the other handlers.
void __cdecl handle_viewer_shift(ViewerInputHandler* handler);
// 0x00577870: the free camera task (once a frame): the frame's time; the mode toggled (command 0x4000, only to the
// object mode when there is a viewer object); the sticks' dead zone (+-50 of the raw values, then scaled); then on
// viewer_cam: in the camera mode the left stick turns it (up / down about its own x axis, left / right about the
// world's y), in the object mode it goes to the viewer object (0x0058A2E0); the right stick moves it along its own
// axes, commands 0x2 / 0x4 up / down; unless the script integer 0xD8C91578 is set, commands 0x10000 / 0x20000
// roll it. Then the camera object unpaused, command 0x1000 the next of six settings (0x004A0710 gets the current
// one, 0x004A0AD0 sets one: a table at 0x00649CC8), the commands cleared. (Without viewer_cam the original carries
// on with a null object: the moves and the unpausing write through it - a crash.)
void __cdecl free_camera_task(ViewerTask* task);
// 0x00577380: the Select combo task: with select_shift set, the UserSelect script of the first button pressed
// with Select held (by the shift handler: shift_makes_) - in the order Triangle, Square, Circle, Start, X, L1, L2,
// R1, R2, Up, Down, Left, Right - spawned if there is one.
void __cdecl select_combo_task(ViewerTask* task);
// The PC tool's messages (cdecl handlers, each returns 1; context->body_ is the message):
// 0x00577CD0 (0x20): ReloadScene with scene = the message (a string).
int __cdecl handle_reload_scene(net::InboundMsgCtx* context);
// 0x005774C0 (0x2A): AddToScene with scene = the message, Add = the string at +0x80.
int __cdecl handle_add_to_scene(net::InboundMsgCtx* context);
// 0x005775A0 (0x23): a new viewer object from modelname = the message, skeletonName = the checksum at +0x84. (The
// viewer is taken from the message's first word too - as the original reads it: the handler's data, the viewer, is
// at +0x18.)
int __cdecl handle_create_viewer_object(net::InboundMsgCtx* context);
// 0x00577670 (0x2B): the script named by the message's first word run with the structure written after it.
int __cdecl on_remote_script_msg(net::InboundMsgCtx* context);
// 0x00577140: the PC tool's "remove the object" message (0x24): the viewer object deleted (the viewer from the
// message's first word, like 0x005775A0). Returns 1.
int __cdecl handle_remove_viewer_object(void** context);
// 0x00576E70 / 0x00576E80 / 0x00576E90: the script commands change_view_mode's, SetMovementVelocity's (400 to start
// with) and SetRotateVelocity's (120) values. 0x00576EA0: the singleton (0x007CE4A8).
void change_view_mode(int mode);
void set_movement_velocity(float velocity);
void set_rotate_velocity(float velocity);
Viewer* get_live_viewer();
// 0x00576CB0: spawn script `name` if there is one (null if not).
script::Script* spawn_script_if_exists(uint32_t name);
// 0x00576CF0: a script file reloaded by the PC tool: runnow.qb (or ..\runnow.qb) noted (0x007CE4A0), then the
// file loaded (0x005A5960). Returns 1.
int __cdecl reload_script_file(const char** name);
// 0x005772E0: `object` moved by `offset` along its own axes (its matrix's rotation).
void move_object_local(objects::GameEntity* object, const maths::Vec4* offset);

} // namespace modules

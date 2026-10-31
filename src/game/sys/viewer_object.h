#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/task_list.h"
#include "game/obj/entity.h"

// The model viewer's object (0x128 bytes, vtable 0x0064F138: a composite object with a model and animations, made by
// Viewer::CreateViewerObject with the viewer's net handler - constructor 0x0058ADB0). Its code is 0x005897B0-0x0058AFB7.
// The PC tool drives it with messages (play an animation, its speed, a frame step, reload an animation or a script)
// and the pad with the commands its input handler sets (m_actions, done once a frame by OwnUpdate).
namespace PadInput {
struct Payload;
}
namespace NetLink {
struct AppHost;
struct InboundMsgCtx;
} // namespace Net
namespace Scripts {
class CStruct;
}

namespace GameObjects {

class CPreviewEntity;

// PadInput::Responder<CPreviewEntity> (0x2C bytes, vtable 0x0064D7D8 - the template's, like GameModules::ViewerInputHandler).
// The node is a task's (0x00406890, priority 0), on the controller's stack of handler lists.
struct ViewerObjectInputHandler : TaskList::SchedulerTask {
    PadInput::Payload* m_controlData;        // +0x18: the controller's data this frame
    uint8_t* m_dev;               // +0x1C
    int m_ordinal;                     // +0x20: the controller (0)
    void(__cdecl* m_codeValue)(ViewerObjectInputHandler*);
    CPreviewEntity* m_content;
};
static_assert(sizeof(ViewerObjectInputHandler) == 0x2C && offsetof(ViewerObjectInputHandler, m_codeValue) == 0x24);

class CPreviewEntity : public CGameEntity {
public:
    // The commands (m_actions bits) the input handler sets and OwnUpdate carries out.
    enum : uint32_t {
        kCommandFaster = 0x40,          // the speed up by 0.25 (Square... - makes 0x4)
        kCommandSlower = 0x80,          // down by 0.25 (makes 0x1)
        kCommandStepForward = 0x100,    // a frame on (Right, or held and auto-repeated)
        kCommandStepBack = 0x200,       // a frame back (Left)
        kCommandTogglePanel = 0x1000,   // the info panel on / off (makes 0x10)
        kCommandCameraUp = 0x2000,      // to the viewer: its command 0x2 (buttons 0x8 without 0x2)
        kCommandCameraDown = 0x4000,    // its command 0x4 (buttons 0x2 without 0x8)
        kCommandRollLeft = 0x8000,      // its command 0x10000 (no animations; buttons 0x20)
        kCommandRollRight = 0x10000,    // its command 0x20000 (no animations; buttons 0x80)
        kCommandToggleMode = 0x20000,   // its command 0x4000 (makes 0x200)
    };

    // 0x0058ADB0: the constructor: a composite object (vtable 0x0064F138, no commands) with the base components, type
    // OBJECT_TYPE_COMPOSITE; its input handler on controller 0's list; speed 1 (none saved), the panel shown, not
    // paused, no skeleton loaded, identity orientation; with a net handler (`app`), its messages 0x25-0x29 and 0x2C.
    // Returns this.
    CPreviewEntity* MakeNew(NetLink::AppHost* app);
    // 0x00589910: set up from `params`: registered, its motion and node components; then, with the debug heap or the
    // bottom-up heap pushed: for the model names model0-3 and modelname / model, a CAS artist's (cas_artist) cached
    // copies of their .skin, .col, .mdl and .tex files dropped (so edited files are read again); the skeleton
    // (skeletonFileName as skeletonName) loaded if it isn't and remembered; placed at ViewerObjPos (or the origin)
    // with no rotation; viewerobj_add_components, Finalize, viewerobj_init_model; the idle animation played.
    void ReadIn(Scripts::CStruct* params);
    // 0x005897B0: shut down - the script kill_viewer_object_panel run. (The object isn't used.)
    void PowerDown();
    // 0x005897D0: animation `anim` played from the start (to its own length, looping, blend 0.3, speed 1), if the object
    // has an animation component that knows it.
    void PlayAnimation(uint32_t anim);
    // 0x00589860: the destructor body - its input handler taken off its list, the skeleton it loaded (if any) unloaded
    // and forgotten, the handler deleted, then CGameEntity's. 0x0058A2B0: the scalar deleting destructor (vtable
    // slot 0).
    void DestroyBody();
    CPreviewEntity* Teardown(unsigned int flags);
    // 0x00589FD0: with an animation component, animation `file` loaded again as `anim` (its anim set's) - the asset
    // (anim set + anim, or the file name's checksum with no anim) unloaded first if it's there - and played.
    void ReloadAnimation(const char* file, uint32_t anim);
    // 0x0058A2E0: the orientation turned by `z` about z, then `x` about x (from identity; radians).
    void ChangeSpin(float z, float x);
    // 0x0058A360: the animation speed (clamped to 0..1; NaN kept) given to the animation component (its first channel
    // and its Speed member function), not paused.
    void ScaleAnimRate(float speed);
    // 0x0058A4A0: paused (the speed saved and 0), then the animation one frame (1/60 s) on, or back (reversed for
    // the step, then forward again).
    void StepAnimation(bool forward);
    // 0x0058A720: the board's wheels (bones 2-5) spun by max_wheel_speed x the speed (the angle kept in 0x007CE590)
    // and every bone but the first put under the first - with a model, a skeleton and more than 5 bones. With no
    // skeleton the counter 0x0067F384 moves on instead (0-31).
    void SpinWheels();
    // 0x0058A8B0: with an animation component that knows its animation and the panel on: draw_viewer_object_panel
    // with its four lines (name, speed, time, frame); then, with a model that has bones, SpinWheels.
    void UpdatePanel();
    // 0x0058AB30 (vtable slot 10, its own update): the commands carried out, the panel updated, the commands cleared.
    void OwnUpdate();

    ViewerObjectInputHandler* m_task; // +0x108: its input handler (0x0058A060)
    uint32_t m_unknown10C;
    uint32_t m_actions;     // +0x110
    NetLink::AppHost* m_ownerApp;         // +0x114: the PC tool's (the viewer's net handler)
    float m_speed;           // +0x118: the animation speed (0-1)
    float m_savedSpeed;      // +0x11C: the speed while paused
    uint32_t m_asset;        // +0x120 (the skeleton asset it loaded, by checksum)
    bool m_isHalted;           // +0x124: stepped frame by frame (until a speed is set)
    bool m_panelShown;       // +0x125
    uint8_t pad126[2];
};
static_assert(offsetof(CPreviewEntity, m_task) == 0x108 && offsetof(CPreviewEntity, m_actions) == 0x110 &&
                  offsetof(CPreviewEntity, m_speed) == 0x118 && offsetof(CPreviewEntity, m_asset) == 0x120 &&
                  offsetof(CPreviewEntity, m_isHalted) == 0x124 && sizeof(CPreviewEntity) == 0x128,
              "layout must match the original");

// 0x0058A060: the input handler's code: the controller's buttons as commands - square (makes 0x10) the panel, Left /
// Right (makes 0x8000 / 0x2000) a frame back / on, held: again after ViewerAutoRepeatTime frames, then every
// ViewerAutoRepeatSpeed (the count kept in 0x007CE588, a function static); makes 0x200 the viewer's mode; buttons 0x8 /
// 0x2 (one alone) the camera up / down; makes 0x4 / 0x1 faster / slower; with no animation component buttons 0x20 /
// 0x80 roll.
void __cdecl HandleViewerObjectInput(ViewerObjectInputHandler* handler);
// The PC tool's messages to the object (cdecl handlers; context->m_body is the message, m_content the object; each
// returns 1): 0x00589840 (0x25) PlayAnimation(the first word); 0x0058ACA0 (0x26) ScaleAnimRate(the first word, a
// float); 0x0058ACC0 (0x27) StepAnimation(the first byte); 0x0058A1F0 (0x28) ReloadAnimation(the message, the word at
// +0x80); 0x0058ACE0 (0x29) ReloadSkaterCamAnim (0x005ADEC0) with the message and the checksum at +0x80 (unnamed);
// 0x0058A210 (0x2C) scripts\animview\<message>.qb loaded and the script named by the message spawned on the object.
int __cdecl HandlePlayAnimation(NetLink::InboundMsgCtx* context);
int __cdecl OnClipRateMsg(NetLink::InboundMsgCtx* context);
int __cdecl HandleStepAnimation(NetLink::InboundMsgCtx* context);
int __cdecl HandleReloadAnimation(NetLink::InboundMsgCtx* context);
int __cdecl HandleReloadCamAnimation(NetLink::InboundMsgCtx* context);
int __cdecl HandleRunAnimViewScript(NetLink::InboundMsgCtx* context);

} // namespace Obj

#pragma once

#include <cstddef>
#include <cstdint>

#include <gsl/pointers>
#include "engine/pad_input/input_hub.h"
#include "engine/objects/entity.h"

// The model viewer's object (0x128 bytes, vtable 0x0064F138: a composite object with a model and animations, made by
// Viewer::create_viewer_object with the viewer's net handler - constructor 0x0058ADB0). Its code is 0x005897B0-0x0058AFB7.
// The PC tool drives it with messages (play an animation, its speed, a frame step, reload an animation or a script)
// and the pad with the commands its input handler sets (actions_, done once a frame by own_update).
namespace net {
struct AppHost;
struct InboundMsgCtx;
} // namespace net
namespace script {
class Struct;
}

namespace objects {

class PreviewEntity;

// Its input handler (priority 0, on controller 0's stack of handler lists).
using ViewerObjectInputHandler = pad_input::Responder<PreviewEntity>;

class PreviewEntity final : public GameEntity {
public:
    PreviewEntity(const PreviewEntity&) = delete;
    PreviewEntity& operator=(const PreviewEntity&) = delete;
    // The commands (actions_ bits) the input handler sets and own_update carries out.
    static constexpr uint32_t kCommandFaster = 0x40;        // the speed up by 0.25 (Square... - makes 0x4)
    static constexpr uint32_t kCommandSlower = 0x80;        // down by 0.25 (makes 0x1)
    static constexpr uint32_t kCommandStepForward = 0x100;  // a frame on (Right, or held and auto-repeated)
    static constexpr uint32_t kCommandStepBack = 0x200;     // a frame back (Left)
    static constexpr uint32_t kCommandTogglePanel = 0x1000; // the info panel on / off (makes 0x10)
    static constexpr uint32_t kCommandCameraUp = 0x2000;    // to the viewer: its command 0x2 (buttons 0x8 without 0x2)
    static constexpr uint32_t kCommandCameraDown = 0x4000;  // its command 0x4 (buttons 0x2 without 0x8)
    static constexpr uint32_t kCommandRollLeft = 0x8000;    // its command 0x10000 (no animations; buttons 0x20)
    static constexpr uint32_t kCommandRollRight = 0x10000;  // its command 0x20000 (no animations; buttons 0x80)
    static constexpr uint32_t kCommandToggleMode = 0x20000; // its command 0x4000 (makes 0x200)

    // 0x0058ADB0: a composite object (vtable 0x0064F138, no commands) with the base components, type
    // OBJECT_TYPE_COMPOSITE; its input handler on controller 0's list; speed 1 (none saved), the panel shown, not
    // paused, no skeleton loaded, identity orientation; with a net handler (`app`), its messages 0x25-0x29 and 0x2C.
    explicit PreviewEntity(net::AppHost* app);
    // 0x00589910: set up from `params`: registered, its motion and node components; then, with the debug heap or the
    // bottom-up heap pushed: for the model names model0-3 and modelname / model, a CAS artist's (cas_artist) cached
    // copies of their .skin, .col, .mdl and .tex files dropped (so edited files are read again); the skeleton
    // (skeletonFileName as skeletonName) loaded if it isn't and remembered; placed at ViewerObjPos (or the origin)
    // with no rotation; viewerobj_add_components, Finalize, viewerobj_init_model; the idle animation played.
    void read_in(script::Struct* params);
    // 0x005897B0: shut down - the script kill_viewer_object_panel run. (The object isn't used.)
    void power_down();
    // 0x005897D0: animation `anim` played from the start (to its own length, looping, blend 0.3, speed 1), if the object
    // has an animation component that knows it.
    void play_animation(uint32_t anim);
    // 0x00589860 (0x0058A2B0 the deleting one): its input handler taken off its list, the skeleton it loaded (if any)
    // unloaded and forgotten, the handler deleted, then GameEntity's.
    ~PreviewEntity() override;
    // 0x00589FD0: with an animation component, animation `file` loaded again as `anim` (its anim set's) - the asset
    // (anim set + anim, or the file name's checksum with no anim) unloaded first if it's there - and played.
    void reload_animation(const char* file, uint32_t anim);
    // 0x0058A2E0: the orientation turned by `z` about z, then `x` about x (from identity; radians).
    void change_spin(float z, float x);
    // 0x0058A360: the animation speed (clamped to 0..1; NaN kept) given to the animation component (its first channel
    // and its Speed member function), not paused.
    void scale_anim_rate(float speed);
    // 0x0058A4A0: paused (the speed saved and 0), then the animation one frame (1/60 s) on, or back (reversed for
    // the step, then forward again).
    void step_animation(bool forward);
    // 0x0058A720: the board's wheels (bones 2-5) spun by max_wheel_speed x the speed (the angle kept in 0x007CE590)
    // and every bone but the first put under the first - with a model, a skeleton and more than 5 bones. With no
    // skeleton the counter 0x0067F384 moves on instead (0-31).
    void spin_wheels();
    // 0x0058A8B0: with an animation component that knows its animation and the panel on: draw_viewer_object_panel
    // with its four lines (name, speed, time, frame); then, with a model that has bones, spin_wheels.
    void update_panel();
    // 0x0058AB30 (its own update): the commands carried out, the panel updated, the commands cleared.
    void own_update() override;

    gsl::owner<ViewerObjectInputHandler*> task_; // +0x108: its input handler (0x0058A060)
    uint32_t unknown10c_{};
    uint32_t actions_{0};     // +0x110
    net::AppHost* owner_app_; // +0x114: the PC tool's (the viewer's net handler)
    float speed_;             // +0x118: the animation speed (0-1)
    float saved_speed_;       // +0x11C: the speed while paused
    uint32_t asset_;          // +0x120 (the skeleton asset it loaded, by checksum)
    bool is_halted_;          // +0x124: stepped frame by frame (until a speed is set)
    bool panel_shown_;        // +0x125
};

// 0x0058A060: the input handler's code: the controller's buttons as commands - square (makes 0x10) the panel, Left /
// Right (makes 0x8000 / 0x2000) a frame back / on, held: again after ViewerAutoRepeatTime frames, then every
// ViewerAutoRepeatSpeed (the count kept in 0x007CE588, a function static); makes 0x200 the viewer's mode; buttons 0x8 /
// 0x2 (one alone) the camera up / down; makes 0x4 / 0x1 faster / slower; with no animation component buttons 0x20 /
// 0x80 roll.
void handle_viewer_object_input(ViewerObjectInputHandler* handler);
// The PC tool's messages to the object (cdecl handlers; context->body_ is the message, content_ the object; each
// returns 1): 0x00589840 (0x25) play_animation(the first word); 0x0058ACA0 (0x26) scale_anim_rate(the first word, a
// float); 0x0058ACC0 (0x27) step_animation(the first byte); 0x0058A1F0 (0x28) reload_animation(the message, the word at
// +0x80); 0x0058ACE0 (0x29) ReloadSkaterCamAnim (0x005ADEC0) with the message and the checksum at +0x80 (unnamed);
// 0x0058A210 (0x2C) scripts\animview\<message>.qb loaded and the script named by the message spawned on the object.
int handle_play_animation(net::InboundMsgCtx* context);
int on_clip_rate_msg(net::InboundMsgCtx* context);
int handle_step_animation(net::InboundMsgCtx* context);
int handle_reload_animation(net::InboundMsgCtx* context);
int handle_reload_cam_animation(net::InboundMsgCtx* context);
int handle_run_anim_view_script(net::InboundMsgCtx* context);

} // namespace objects

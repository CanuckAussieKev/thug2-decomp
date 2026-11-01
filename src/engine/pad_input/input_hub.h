#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "engine/core/task_list.h"
#include "engine/core/owner.h"

namespace sio {
class InputPort;
}

// The input manager (the singleton 0x0044E7F0 makes): a server per controller that
// turns its Sio device's PS2-format control data into button bits and analogue values once a frame and passes
// them to the server's input handlers (pad_input::Responder<T>, e.g. the front end's). A server can also play back a
// recording (one PadFrameRecord per frame) or record what it reads.
namespace pad_input {

// One frame of a recording (0x20 bytes).
struct PadFrameRecord {
    uint8_t is_usable_;
    uint8_t incident_[25]; // Payload::incident_
    uint8_t pad1a_[2];
    uint32_t buttons_;
};
static_assert(sizeof(PadFrameRecord) == 0x20 && offsetof(PadFrameRecord, buttons_) == 0x1C);

// What the handlers get (0x34 bytes): the analogue values and the button bits (PS2 order: bit 12 up, 13 right,
// 14 down, 15 left ...; the device's extra byte above bit 16).
struct Payload {
    // The analogue values: the sticks (right X, right Y, left X, left Y: 0-255, 0x80 centred), then a pressure
    // per button (0 or 0xFF from a digital pad), and copies of the sticks before the dead zone at 0x12-0x15.
    static constexpr int kRightX = 0;
    static constexpr int kRightY = 1;
    static constexpr int kLeftX = 2;
    static constexpr int kLeftY = 3;
    static constexpr int kPadRight = 4;
    static constexpr int kPadLeft = 5;
    static constexpr int kPadUp = 6;
    static constexpr int kPadDown = 7;
    static constexpr int kRawRightX = 0x12;
    static constexpr int kRawRightY = 0x13;
    static constexpr int kRawLeftX = 0x14;
    static constexpr int kRawLeftY = 0x15;
    static constexpr int kQueueCapacity = 25;
    static constexpr uint32_t kReleaseEdge = 0x1000;
    static constexpr uint32_t kButtonRight = 0x2000;
    static constexpr uint32_t kPressEdge = 0x4000;
    static constexpr uint32_t kButtonLeft = 0x8000;

    uint8_t incident_[kQueueCapacity];
    uint8_t pad19[3];
    uint32_t buttons_;  // +0x1C: down
    uint32_t makes_;    // +0x20: pressed this frame
    uint32_t breaks_;   // +0x24: released this frame
    uint32_t changes_;  // +0x28
    uint32_t ongoing_;  // +0x2C: the buttons read this frame
    uint32_t previous_; // +0x30: and last frame

    // 0x0044E840: the buttons in `mask` read as up (held, pressed and released).
    void block_button_input(uint32_t mask);
    // 0x0044E860: the analogue values in `mask` (bit n: value n) neutral - the sticks centred, pressures 0.
    void block_stick_input(uint32_t mask);
    // 0x0044E8C0: each button's pressure from its bit (0xFF down, 0), by the table at 0x0067D730 ({mask, value}
    // pairs, ended by a mask that isn't positive).
    void digital_as_stick();
    // 0x0044E900: the raw sticks kept at 0x12-0x15; then each stick axis within 50 of the centre centred, and a
    // raw stick only when both its axes are.
    void apply_deadzone_rules();
    // 0x0044EA30: with no d-pad button down, the left stick works the d-pad: its direction's button bit and
    // pressure (the pressures as the original computes them: up's grows towards the centre).
    void pad_follows_stick();
};
static_assert(sizeof(Payload) == 0x34 && offsetof(Payload, buttons_) == 0x1C && offsetof(Payload, previous_) == 0x30);

// An input handler (0x24 bytes; the base's vtable 0x00646090): a task in a controller's handler list, run once a
// frame with that controller's data.
class InputHandler : public tasks::SchedulerTask {
public:
    // 0x00406890's task, on `controller`.
    InputHandler(int priority, int controller) : SchedulerTask(priority), ordinal_(controller) {}

    Payload* control_data_ = nullptr; // +0x18: the controller's data this frame (set when it's added to a list)
    sio::InputPort* dev_ = nullptr;   // +0x1C: the controller's device
    int ordinal_;                     // +0x20: the controller
};

// pad_input::Responder<Owner> (0x2C bytes; every instance shared the vtable 0x0064D7D8): a handler running a cdecl
// function given the handler, for the object it works for.
template <typename Owner>
class Responder final : public InputHandler {
public:
    using Code = void (*)(Responder* handler);

    Responder(Code code, Owner* owner, int priority, int controller = 0)
        : InputHandler(priority, controller), code_value_(code), content_(owner)
    {}

    // 0x00577270 (slot 0 of every Responder<T>): the code, given the handler.
    void run() override { code_value_(this); }

    Code code_value_; // +0x24
    Owner* content_;  // +0x28
};
static_assert(sizeof(Responder<void>) == 0x2C, "the original's size");

// A controller's server (0x84 bytes).
struct HostEnd {
    sio::InputPort* dev_;
    tasks::PileUp handlers_;        // +0x04: the handler list (0x00406E80 makes it, 0x004070A0 runs it)
    uint8_t pad38_[0x08];           // +0x38: (the original list's two table words: the host keeps its 0x84 bytes)
    Payload content_;               // +0x40
    PadFrameRecord* data_in_;       // +0x74: the recording played back (null: none)
    PadFrameRecord* in_buf_limit_;  // +0x78
    PadFrameRecord* data_out_;      // +0x7C: the recording made (null: none)
    PadFrameRecord* out_buf_limit_; // +0x80

    // 0x0044E9E0: the handler list and everything cleared (but dev_, makes_ and breaks_).
    HostEnd();
    // 0x0044EAA0: a frame - the data from the recording played back (if any), else from the device (if it has
    // a pad: nothing at all when it has none), recorded if recording; the buttons noted for the device's index
    // (0x00696D48 / 50 / 58); then the handlers run.
    void poll_input_sinks();
    // 0x0044E520: the destructor: the handler list destroyed (0x004070E0) - the implicit one.
};
static_assert(sizeof(HostEnd) == 0x84 && offsetof(HostEnd, handlers_) == 0x04 && offsetof(HostEnd, content_) == 0x40 &&
              offsetof(HostEnd, out_buf_limit_) == 0x80);

struct Hub {
    static constexpr int kNumServers = 2;

    std::unique_ptr<tasks::Task<Hub>> task_; // priority 2000, running the servers (service_task)
    HostEnd hosts_[kNumServers];             // +0x04
    bool stick_replaces_pad_;                // +0x10C (0x0044E610 sets, 0x0044E620 gets)
    bool unknown10d_;
    uint8_t pad10e_[2];
    int unknown110_[2]; // +0x110 (0x0044E630 / 0x0044E640 get and set them)

    // 0x0044E700: the servers, the task, each server's device (by index), the stick working the d-pad on.
    Hub();
    // 0x0044E530: the destructor: the task deleted (its virtual scalar deleting destructor), the servers destroyed
    // (the last first).
    ~Hub();
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;
    // 0x0044E5D0 / 0x0044E5F0: server `index`'s device (if any) stops and refuses vibration / allows it again.
    void stop_rumble(int index);
    void enable_actuators(int index);
    // 0x0044E610 / 0x0044E620
    void change_pad_follows_stick(bool on) { stick_replaces_pad_ = on; }
    bool pad_follows_stick() { return stick_replaces_pad_; }
    // 0x0044E630 / 0x0044E640: unknown110_[`player`] (the controller a player uses: Construct gives player 0
    // controller 0, player 1 none).
    int get_player_controller(int player) { return unknown110_[player]; }
    void set_player_controller(int player, int controller) { unknown110_[player] = controller; }
    // 0x0044E660: the task's code (`task` its tasks::Task<Manager>: the manager at +0x1C): for each server, a
    // recording played back or made that reached its end stops; then the server's frame (poll_input_sinks).
    static void service_task(tasks::Task<Hub>* task);
};
static_assert(sizeof(Hub) == 0x118 && offsetof(Hub, stick_replaces_pad_) == 0x10C);

// The singleton (0x00696D40, references 0x00696D44). 0x0044E7F0: the first call with `create` makes it, every
// call takes a reference and returns it (null if there is none).
Hub* create_manager(bool create);
// 0x0044E6B0: drops a reference; the last one destroys (0x0044E530) and frees the manager.
void release_manager();

// The button table (0x0067D6D8: 22 checksums, entries 1-21 the buttons - up, down, ..., the debounce times are
// indexed the same way). 0x0044E590: entry `index`'s checksum. 0x0044E5A0: the index of `checksum` (0 if it's
// none of them).
uint32_t button_hash(int index);
int glyph_for_button_tag(uint32_t checksum);

extern memory::owner<pad_input::Hub*> g_input_manager;

} // namespace pad_input

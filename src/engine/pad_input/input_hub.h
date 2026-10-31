#pragma once

#include <cstddef>
#include <cstdint>

namespace Sio {
class InputPort;
}

// The input manager (Neversoft's PadInput::Hub, the singleton 0x0044E7F0 makes): a server per controller that
// turns its Sio device's PS2-format control data into button bits and analogue values once a frame and passes
// them to the server's input handlers (PadInput::Responder<T>, e.g. the front end's). A server can also play back a
// recording (one PadFrameRecord per frame) or record what it reads.
namespace PadInput {

// One frame of a recording (0x20 bytes).
struct PadFrameRecord {
    uint8_t m_isUsable;
    uint8_t m_incident[25]; // Payload::m_incident
    uint8_t pad1A[2];
    uint32_t m_buttons;
};
static_assert(sizeof(PadFrameRecord) == 0x20 && offsetof(PadFrameRecord, m_buttons) == 0x1C);

// What the handlers get (0x34 bytes): the analogue values and the button bits (PS2 order: bit 12 up, 13 right,
// 14 down, 15 left ...; the device's extra byte above bit 16).
struct Payload {
    // The analogue values: the sticks (right X, right Y, left X, left Y: 0-255, 0x80 centred), then a pressure
    // per button (0 or 0xFF from a digital pad), and copies of the sticks before the dead zone at 0x12-0x15.
    enum : int {
        kRightX = 0, kRightY = 1, kLeftX = 2, kLeftY = 3,
        kPadRight = 4, kPadLeft = 5, kPadUp = 6, kPadDown = 7,
        kRawRightX = 0x12, kRawRightY = 0x13, kRawLeftX = 0x14, kRawLeftY = 0x15,
        kQueueCapacity = 25,
    };
    enum : uint32_t { kReleaseEdge = 0x1000, kButtonRight = 0x2000, kPressEdge = 0x4000, kButtonLeft = 0x8000 };

    uint8_t m_incident[kQueueCapacity];
    uint8_t pad19[3];
    uint32_t m_buttons; // +0x1C: down
    uint32_t m_makes;   // +0x20: pressed this frame
    uint32_t m_breaks;  // +0x24: released this frame
    uint32_t m_changes; // +0x28
    uint32_t m_ongoing; // +0x2C: the buttons read this frame
    uint32_t m_previous; // +0x30: and last frame

    // 0x0044E840: the buttons in `mask` read as up (held, pressed and released).
    void BlockButtonInput(uint32_t mask);
    // 0x0044E860: the analogue values in `mask` (bit n: value n) neutral - the sticks centred, pressures 0.
    void BlockStickInput(uint32_t mask);
    // 0x0044E8C0: each button's pressure from its bit (0xFF down, 0), by the table at 0x0067D730 ({mask, value}
    // pairs, ended by a mask that isn't positive).
    void DigitalAsStick();
    // 0x0044E900: the raw sticks kept at 0x12-0x15; then each stick axis within 50 of the centre centred, and a
    // raw stick only when both its axes are.
    void ApplyDeadzoneRules();
    // 0x0044EA30: with no d-pad button down, the left stick works the d-pad: its direction's button bit and
    // pressure (the pressures as the original computes them: up's grows towards the centre).
    void PadFollowsStick();
};
static_assert(sizeof(Payload) == 0x34 && offsetof(Payload, m_buttons) == 0x1C && offsetof(Payload, m_previous) == 0x30);

// A controller's server (0x84 bytes).
struct HostEnd {
    Sio::InputPort* m_dev;
    uint8_t m_handlers[0x3C];    // +0x04: the handler list (Scheduling::LinkChain: 0x00406E80 makes it, 0x004070A0 runs it)
    Payload m_content;                 // +0x40
    PadFrameRecord* m_dataIn;      // +0x74: the recording played back (null: none)
    PadFrameRecord* m_inBufLimit;   // +0x78
    PadFrameRecord* m_dataOut;     // +0x7C: the recording made (null: none)
    PadFrameRecord* m_outBufLimit;  // +0x80

    // 0x0044E9E0: the handler list and everything cleared (but m_dev, m_makes and m_breaks). Returns this.
    HostEnd* MakeNew();
    // 0x0044EAA0: a frame - the data from the recording played back (if any), else from the device (if it has
    // a pad: nothing at all when it has none), recorded if recording; the buttons noted for the device's index
    // (0x00696D48 / 50 / 58); then the handlers run.
    void PollInputSinks();
    // 0x0044E520: the destructor: the handler list destroyed (0x004070E0).
    void Destruct();
};
static_assert(sizeof(HostEnd) == 0x84 && offsetof(HostEnd, m_content) == 0x40 && offsetof(HostEnd, m_outBufLimit) == 0x80);

struct Hub {
    enum { kNumServers = 2 };

    void* m_task;                     // Scheduling::Task<Manager> (priority 2000) running the servers (0x0044E660)
    HostEnd m_hosts[kNumServers];    // +0x04
    bool m_stickReplacesPad;      // +0x10C (0x0044E610 sets, 0x0044E620 gets)
    bool m_unknown10D;
    uint8_t pad10E[2];
    int m_unknown110[2];              // +0x110 (0x0044E630 / 0x0044E640 get and set them)

    // 0x0044E700: the servers, the task, each server's device (by index), the stick working the d-pad on.
    // Returns this.
    Hub* MakeNew();
    // 0x0044E530: the destructor: the task deleted (its virtual scalar deleting destructor), the servers destroyed
    // (the last first).
    void Destruct();
    // 0x0044E5D0 / 0x0044E5F0: server `index`'s device (if any) stops and refuses vibration / allows it again.
    void StopRumble(int index);
    void EnableActuators(int index);
    // 0x0044E610 / 0x0044E620
    void ChangePadFollowsStick(bool on) { m_stickReplacesPad = on; }
    bool PadFollowsStick() { return m_stickReplacesPad; }
    // 0x0044E630 / 0x0044E640: m_unknown110[`player`] (the controller a player uses: Construct gives player 0
    // controller 0, player 1 none).
    int GetPlayerController(int player) { return m_unknown110[player]; }
    void SetPlayerController(int player, int controller) { m_unknown110[player] = controller; }
    // 0x0044E660: the task's code (`task` its Scheduling::Task<Manager>: the manager at +0x1C): for each server, a
    // recording played back or made that reached its end stops; then the server's frame (PollInputSinks).
    static void __cdecl ServiceTask(void* task);
};
static_assert(sizeof(Hub) == 0x118 && offsetof(Hub, m_stickReplacesPad) == 0x10C);

// The singleton (0x00696D40, references 0x00696D44). 0x0044E7F0: the first call with `create` makes it, every
// call takes a reference and returns it (null if there is none).
Hub* CreateManager(bool create);
// 0x0044E6B0: drops a reference; the last one destroys (0x0044E530) and frees the manager.
void ReleaseManager();

// The button table (0x0067D6D8: 22 checksums, entries 1-21 the buttons - up, down, ..., the debounce times are
// indexed the same way). 0x0044E590: entry `index`'s checksum. 0x0044E5A0: the index of `checksum` (0 if it's
// none of them).
uint32_t __cdecl ButtonHash(int index);
int __cdecl GlyphForButtonTag(uint32_t checksum);

extern PadInput::Hub* g_inputManager;

} // namespace Inp

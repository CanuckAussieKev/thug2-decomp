#pragma once

namespace input {
class Source;
} // namespace input

// The DirectInput input source (input::Source, engine/pad_input/input_source.h; app/ adds it first): the original's
// DirectInput keyboard read once a frame (the frame's half of 0x005BDD60), the joystick a port's pad is looked for on
// (os_layer::read_joystick), whether DirectInput is made (init_input's first test) and its state cleared at start-up,
// and the original's pad read (os_layer::read_pad, its modes and keys, get_key_down) for sio::InputPort::pull_data.
// Since PartyMod's input nothing makes DirectInput or its devices, so each read finds none - kept as the original
// did it, and the one place <dinput.h> is seen (tools/input_headers.test.ts).
namespace input::dinput {

Source& keyboard();

} // namespace input::dinput

#pragma once

// NOT ORIGINAL - the x87 precision the original's game ran its maths at. The original made its Direct3D 9 device
// without D3DCREATE_FPU_PRESERVE (0x004D7C80), so Direct3D 9 loaded its own control word into the x87 of the thread
// that made the device - the game's thread - at CreateDevice and again at every Reset: 0x007F, 24-bit precision,
// round to nearest, every exception masked. Its float and double maths (a double sqrt's FSQRT too) ran at 24 bits from
// there on; until the device was made it ran at the C runtime's 53. The game itself never sets the control word.
// The renderers call use_device_fpu_precision where the device is made and reset, so the game's maths runs as the
// original's whatever the renderer (Vulkan leaves the x87 alone; Direct3D 9 has already loaded the same word: no
// change). Other threads keep the C runtime's default, as the original's did. 64-bit builds do their float maths in
// SSE, which has no precision to set: nothing happens there.
namespace maths {

// The x87 control word as the original's game thread had it after its device was made (32-bit x86; elsewhere
// nothing).
void use_device_fpu_precision();

// The x87 control word as it is now, to be put back by restore_fpu_word (32-bit x86; elsewhere 0 and nothing). The
// original's libraries were all loaded at its start, before the device; a DLL we load later sets the C runtime's
// default word in its start-up code (a MinGW-built DLL's: 53 bits) on the thread that loads it - the loader keeps the word
// around it.
[[nodiscard]] unsigned int fpu_word();
void restore_fpu_word(unsigned int word);

} // namespace maths

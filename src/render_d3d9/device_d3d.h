#pragma once

// NOT ORIGINAL split - the Direct3D 9 device's Direct3D side (device.h), for the renderer's files that talk to
// Direct3D: the device made, the sound device callback that runs alongside, and the present parameters.
#include <windows.h>

#include <d3d9.h>

#include "base/maths/rect.h"

namespace render {

// 0x004D7C80: create the device at width x height (into 0x007D69E0) - hardware vertex processing if
// `hardware_vertices` (else software), 2x multisampling if `multisample` - and note the adapter (a Radeon 8500;
// an Intel 915G with Realtek audio). Returns the HRESULT.
HRESULT create_device(maths::PixelSize size, bool hardware_vertices, bool multisample);
// 0x004D7C20: DirectSoundEnumerateA's callback - keeps each device's description (0x00786AC8). Returns TRUE.
BOOL CALLBACK sound_device_found(GUID* guid, const char* description, const char* module, void* context);

// The present parameters the device was made with (device.cpp's record; the original's global).
D3DPRESENT_PARAMETERS& present_parameters();

} // namespace render

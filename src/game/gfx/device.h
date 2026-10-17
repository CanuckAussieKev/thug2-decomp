#pragma once

#include <cstdint>

// The Direct3D 9 device (Nx engine, PC).
namespace Nx {

// 0x004D8650: create Direct3D and the device - full screen (or windowed, PartyMod's Windowed setting) at the
// resolution setting's size, trying smaller ones and fewer features until it works (exits if nothing does) -
// and set up the 640x480 -> back buffer conversion. With PartyMod's changes: its resolution in place of
// 640x480, the window left alone (not maximised, moved or cursor-hidden - PartyMod's SDL window does that).
void InitDirect3D();

// 0x0044F870: the device's SetRenderState (a D3DRENDERSTATETYPE and its value). Returns the HRESULT.
long __cdecl SetRenderState(int state, uint32_t value);
// 0x0044F890: the device's Clear (rectangles or the whole viewport; D3DCLEAR_* flags). Returns the HRESULT.
long __cdecl ClearDevice(uint32_t rectCount, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil);

} // namespace Nx

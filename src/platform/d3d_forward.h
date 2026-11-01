#pragma once

// NOT ORIGINAL - the Direct3D 9 interfaces the engine's headers name (pointers only), declared without <d3d9.h> and
// <windows.h> (whose macros - near, far, min... - the rest of the game must not see). The .cpp files that call them
// include <d3d9.h>.
// NOLINTBEGIN(cppcoreguidelines-virtual-class-destructor): COM interfaces: released, never deleted
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct IDirect3DTexture9;
struct IDirect3DSurface9;
// NOLINTEND(cppcoreguidelines-virtual-class-destructor)

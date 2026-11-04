#pragma once

#include <d3d9.h>

#include <cstdint>

#include "base/core/pointer_cell.h"

// NOT ORIGINAL - Dear ImGui's Direct3D 9 backend names a texture by its IDirect3DTexture9* as a 64-bit id (ImTextureID):
// the pointer is the id (the level editor's UI backend, editor/level_editor_ui_backend.cpp).
namespace os_layer {

inline uint64_t imgui_texture_id(IDirect3DTexture9* texture) { return static_cast<uint64_t>(cell::address(texture)); }
inline IDirect3DTexture9* imgui_texture_of(uint64_t id) { return cell::pointer<IDirect3DTexture9>(static_cast<uintptr_t>(id)); }

} // namespace os_layer

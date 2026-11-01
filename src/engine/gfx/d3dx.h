// The D3DX9 maths and utility calls the game makes, behind our own functions (the renderer is to move to OpenGL
// one day: these are the only places that talk to D3DX).
//
// Microsoft's D3DX9 (d3dx9_43.dll, from the Microsoft.DXSDK.D3DX NuGet package), through the standard API only - MinGW-w64's
// d3dx9 headers and libd3dx9_43.a offer the same calls. Matrices are 16 floats, row-major (D3DXMATRIX / maths::Mat4);
// the viewing functions are the right-handed ones (what the original's 2004 D3DX9 computes).
#pragma once

#include <cstdint>

// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor): a COM interface: released, never deleted
struct IDirect3DSurface9;

namespace d3dx {

// (defined in d3dx.cpp, which includes d3dx9.h)
void vec3_normalize(float* out, const float* v);
void vec3_transform(float* out, const float* v, const void* matrix);        // out: 4 floats, (v, 1) * matrix
void vec3_transform_coord(float* out, const float* v, const void* matrix);  // out: 3 floats, divided by w
void vec3_transform_normal(float* out, const float* v, const void* matrix); // out: 3 floats, no translation
void vec4_transform(float* out, const float* v, const void* matrix);        // out, v: 4 floats
void matrix_multiply(void* out, const void* a, const void* b);              // out may alias a or b
void matrix_transpose(void* out, const void* m);
void matrix_inverse(void* out, float* determinant, const void* m);
void matrix_scaling(void* out, float x, float y, float z);
void matrix_rotation_y(void* out, float angle);
void matrix_look_at_rh(void* out, const float* eye, const float* at, const float* up);
void matrix_perspective_rh(void* out, float width, float height, float near_z, float far_z);
void matrix_ortho_rh(void* out, float width, float height, float near_z, float far_z);
long save_surface_to_jpg(const char* file, IDirect3DSurface9* surface); // the HRESULT
// `source`: ps_1_x assembler text. `shader`, `errors`: receive an ID3DXBuffer* each (COM: [2] Release,
// [3] GetBufferPointer); `errors` may be null.
long assemble_shader(const char* source, uint32_t length, void** shader, void** errors);

} // namespace d3dx

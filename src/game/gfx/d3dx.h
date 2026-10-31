// The D3DX9 maths and utility calls the game makes, behind our own functions (the renderer is to move to OpenGL
// one day: these are the only places that talk to D3DX).
//
// Microsoft's D3DX9 (d3dx9_43.dll, from the Microsoft.DXSDK.D3DX NuGet package), through the standard API only - MinGW-w64's
// d3dx9 headers and libd3dx9_43.a offer the same calls. Matrices are 16 floats, row-major (D3DXMATRIX / Mth::Matrix);
// the viewing functions are the right-handed ones (what the original's 2004 D3DX9 computes).
#pragma once

#include <cstdint>

struct IDirect3DSurface9;

namespace D3dx {

// (defined in d3dx.cpp, which includes d3dx9.h)
void Vec3Normalize(float* out, const float* v);
void Vec3Transform(float* out, const float* v, const void* matrix);       // out: 4 floats, (v, 1) * matrix
void Vec3TransformCoord(float* out, const float* v, const void* matrix);  // out: 3 floats, divided by w
void Vec3TransformNormal(float* out, const float* v, const void* matrix); // out: 3 floats, no translation
void Vec4Transform(float* out, const float* v, const void* matrix);       // out, v: 4 floats
void MatrixMultiply(void* out, const void* a, const void* b);             // out may alias a or b
void MatrixTranspose(void* out, const void* m);
void MatrixInverse(void* out, float* determinant, const void* m);
void MatrixScaling(void* out, float x, float y, float z);
void MatrixRotationY(void* out, float angle);
void MatrixLookAtRH(void* out, const float* eye, const float* at, const float* up);
void MatrixPerspectiveRH(void* out, float width, float height, float nearZ, float farZ);
void MatrixOrthoRH(void* out, float width, float height, float nearZ, float farZ);
long SaveSurfaceToJpg(const char* file, IDirect3DSurface9* surface); // the HRESULT
// `source`: ps_1_x assembler text. `shader`, `errors`: receive an ID3DXBuffer* each (COM: [2] Release,
// [3] GetBufferPointer); `errors` may be null.
long AssembleShader(const char* source, uint32_t length, void** shader, void** errors);

} // namespace D3dx

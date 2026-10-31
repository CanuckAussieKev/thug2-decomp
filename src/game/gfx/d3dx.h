// The D3DX9 maths and utility calls the game makes, behind our own functions (the renderer is to move to OpenGL
// one day: these are the only places that talk to D3DX).
//
// Hooked build: the exe's own D3DX9 (a 2004 version linked into it), called by address. Standalone build: Microsoft's
// D3DX9 (d3dx9_43.dll, from the Microsoft.DXSDK.D3DX NuGet package), through the standard API only - MinGW-w64's
// d3dx9 headers and libd3dx9_43.a offer the same calls. Matrices are 16 floats, row-major (D3DXMATRIX / Mth::Matrix);
// the viewing functions are the right-handed ones (what the exe's versions compute).
#pragma once

#include <cstdint>

#include "hook/game.h"

struct IDirect3DSurface9;

namespace D3dx {

#ifdef THUG2_STANDALONE
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
#else
inline void Vec3Normalize(float* out, const float* v)
{
    GameFunction<void*(__stdcall*)(float*, const float*)>(0x005BEF2D)(out, v);
}
inline void Vec3Transform(float* out, const float* v, const void* matrix)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const void*)>(0x005BF19F)(out, v, matrix);
}
inline void Vec3TransformCoord(float* out, const float* v, const void* matrix)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const void*)>(0x005BF2F5)(out, v, matrix);
}
inline void Vec3TransformNormal(float* out, const float* v, const void* matrix)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const void*)>(0x005BF31F)(out, v, matrix);
}
inline void Vec4Transform(float* out, const float* v, const void* matrix)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const void*)>(0x005BF833)(out, v, matrix);
}
inline void MatrixMultiply(void* out, const void* a, const void* b)
{
    GameFunction<void*(__stdcall*)(void*, const void*, const void*)>(0x005BFA8C)(out, a, b);
}
inline void MatrixTranspose(void* out, const void* m)
{
    GameFunction<void*(__stdcall*)(void*, const void*)>(0x005BFCD5)(out, m);
}
inline void MatrixInverse(void* out, float* determinant, const void* m)
{
    GameFunction<void*(__stdcall*)(void*, float*, const void*)>(0x005BFF98)(out, determinant, m);
}
inline void MatrixScaling(void* out, float x, float y, float z)
{
    GameFunction<void*(__stdcall*)(void*, float, float, float)>(0x005C036B)(out, x, y, z);
}
inline void MatrixRotationY(void* out, float angle)
{
    GameFunction<void*(__stdcall*)(void*, float)>(0x005C0509)(out, angle);
}
inline void MatrixLookAtRH(void* out, const float* eye, const float* at, const float* up)
{
    GameFunction<void*(__stdcall*)(void*, const float*, const float*, const float*)>(0x005C0985)(out, eye, at, up);
}
inline void MatrixPerspectiveRH(void* out, float width, float height, float nearZ, float farZ)
{
    GameFunction<void*(__stdcall*)(void*, float, float, float, float)>(0x005C0ACD)(out, width, height, nearZ, farZ);
}
inline void MatrixOrthoRH(void* out, float width, float height, float nearZ, float farZ)
{
    GameFunction<void*(__stdcall*)(void*, float, float, float, float)>(0x005C0B39)(out, width, height, nearZ, farZ);
}
inline long SaveSurfaceToJpg(const char* file, IDirect3DSurface9* surface)
{
    // (D3DXSaveSurfaceToFileA; format 1 = D3DXIFF_JPG, no palette, whole surface)
    return GameFunction<long(__stdcall*)(const char*, int, IDirect3DSurface9*, const void*, const void*)>(0x005C2F76)(
        file, 1, surface, nullptr, nullptr);
}
inline long AssembleShader(const char* source, uint32_t length, void** shader, void** errors)
{
    return GameFunction<long(__stdcall*)(const char*, uint32_t, const void*, void*, uint32_t, void**, void**)>(
        0x005C2FBD)(source, length, nullptr, nullptr, 0, shader, errors);
}
#endif

} // namespace D3dx

// The D3DX9 calls the game makes (see d3dx.h). Only the standalone build has code here: Microsoft's D3DX9 through its
// standard API. The hooked build calls the exe's own copy (inline in the header).
#include "game/gfx/d3dx.h"


#include <windows.h>

#include <d3d9.h>
#include <d3dx9.h>

namespace D3dx {

// (Mth::Matrix and the callers' float arrays are 16 floats, 4-byte aligned, like D3DXMATRIX: the casts below are
// layout-exact. d3dx9_43 may differ from the exe's 2004 D3DX9 in the last bit of a float.)
static_assert(sizeof(D3DXMATRIX) == 64 && sizeof(D3DXVECTOR3) == 12 && sizeof(D3DXVECTOR4) == 16,
              "the D3DX types must be plain floats");

static D3DXMATRIX* M(void* p) { return static_cast<D3DXMATRIX*>(p); }
static const D3DXMATRIX* M(const void* p) { return static_cast<const D3DXMATRIX*>(p); }
static D3DXVECTOR3* V3(float* p) { return reinterpret_cast<D3DXVECTOR3*>(p); }
static const D3DXVECTOR3* V3(const float* p) { return reinterpret_cast<const D3DXVECTOR3*>(p); }
static D3DXVECTOR4* V4(float* p) { return reinterpret_cast<D3DXVECTOR4*>(p); }
static const D3DXVECTOR4* V4(const float* p) { return reinterpret_cast<const D3DXVECTOR4*>(p); }

void Vec3Normalize(float* out, const float* v) { D3DXVec3Normalize(V3(out), V3(v)); }
void Vec3Transform(float* out, const float* v, const void* matrix) { D3DXVec3Transform(V4(out), V3(v), M(matrix)); }
void Vec3TransformCoord(float* out, const float* v, const void* matrix)
{
    D3DXVec3TransformCoord(V3(out), V3(v), M(matrix));
}
void Vec3TransformNormal(float* out, const float* v, const void* matrix)
{
    D3DXVec3TransformNormal(V3(out), V3(v), M(matrix));
}
void Vec4Transform(float* out, const float* v, const void* matrix) { D3DXVec4Transform(V4(out), V4(v), M(matrix)); }
void MatrixMultiply(void* out, const void* a, const void* b) { D3DXMatrixMultiply(M(out), M(a), M(b)); }
void MatrixTranspose(void* out, const void* m) { D3DXMatrixTranspose(M(out), M(m)); }
void MatrixInverse(void* out, float* determinant, const void* m) { D3DXMatrixInverse(M(out), determinant, M(m)); }
void MatrixScaling(void* out, float x, float y, float z) { D3DXMatrixScaling(M(out), x, y, z); }
void MatrixRotationY(void* out, float angle) { D3DXMatrixRotationY(M(out), angle); }
void MatrixLookAtRH(void* out, const float* eye, const float* at, const float* up)
{
    D3DXMatrixLookAtRH(M(out), V3(eye), V3(at), V3(up));
}
void MatrixPerspectiveRH(void* out, float width, float height, float nearZ, float farZ)
{
    D3DXMatrixPerspectiveRH(M(out), width, height, nearZ, farZ);
}
void MatrixOrthoRH(void* out, float width, float height, float nearZ, float farZ)
{
    D3DXMatrixOrthoRH(M(out), width, height, nearZ, farZ);
}

long SaveSurfaceToJpg(const char* file, IDirect3DSurface9* surface)
{
    return D3DXSaveSurfaceToFileA(file, D3DXIFF_JPG, surface, nullptr, nullptr);
}

long AssembleShader(const char* source, uint32_t length, void** shader, void** errors)
{
    // (D3DXAssembleShader's ID3DXBuffer is a COM object with the same vtable the callers use)
    return D3DXAssembleShader(source, length, nullptr, nullptr, 0, reinterpret_cast<LPD3DXBUFFER*>(shader),
                              reinterpret_cast<LPD3DXBUFFER*>(errors));
}

} // namespace D3dx


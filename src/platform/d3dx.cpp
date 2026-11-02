// The D3DX9 calls the game makes (see d3dx.h): Microsoft's D3DX9 through its standard API.
#include "platform/d3dx.h"
#include "engine/core/raw_view.h"

#include <windows.h>

#include <d3d9.h>
#include <d3dx9.h>

#include <cstdint>

namespace d3dx {

// (maths::Mat4 and the callers' float arrays are 16 floats, 4-byte aligned, like D3DXMATRIX: the casts below are
// layout-exact. d3dx9_43 may differ from the exe's 2004 D3DX9 in the last bit of a float.)
static_assert(sizeof(D3DXMATRIX) == 64 && sizeof(D3DXVECTOR3) == 12 && sizeof(D3DXVECTOR4) == 16,
              "the D3DX types must be plain floats");

namespace {
D3DXMATRIX* get_m(void* p) { return static_cast<D3DXMATRIX*>(p); }
const D3DXMATRIX* get_m(const void* p) { return static_cast<const D3DXMATRIX*>(p); }
D3DXVECTOR3* v3(float* p) { return raw::as<D3DXVECTOR3>(p); }
const D3DXVECTOR3* v3(const float* p) { return raw::as<const D3DXVECTOR3>(p); }
D3DXVECTOR4* v4(float* p) { return raw::as<D3DXVECTOR4>(p); }
const D3DXVECTOR4* v4(const float* p) { return raw::as<const D3DXVECTOR4>(p); }
} // namespace

void vec3_normalize(float* out, const float* v) { D3DXVec3Normalize(v3(out), v3(v)); }
void vec3_transform(float* out, const float* v, const void* matrix)
{
    D3DXVec3Transform(v4(out), v3(v), get_m(matrix));
}
void vec3_transform_coord(float* out, const float* v, const void* matrix)
{
    D3DXVec3TransformCoord(v3(out), v3(v), get_m(matrix));
}
void vec3_transform_normal(float* out, const float* v, const void* matrix)
{
    D3DXVec3TransformNormal(v3(out), v3(v), get_m(matrix));
}
void vec4_transform(float* out, const float* v, const void* matrix)
{
    D3DXVec4Transform(v4(out), v4(v), get_m(matrix));
}
void matrix_multiply(void* out, const void* a, const void* b) { D3DXMatrixMultiply(get_m(out), get_m(a), get_m(b)); }
void matrix_transpose(void* out, const void* m) { D3DXMatrixTranspose(get_m(out), get_m(m)); }
void matrix_inverse(void* out, float* determinant, const void* m)
{
    D3DXMatrixInverse(get_m(out), determinant, get_m(m));
}
void matrix_scaling(void* out, float x, float y, float z) { D3DXMatrixScaling(get_m(out), x, y, z); }
void matrix_rotation_y(void* out, float angle) { D3DXMatrixRotationY(get_m(out), angle); }
void matrix_look_at_rh(void* out, const float* eye, const float* at, const float* up)
{
    D3DXMatrixLookAtRH(get_m(out), v3(eye), v3(at), v3(up));
}
void matrix_perspective_rh(void* out, float width, float height, float near_z, float far_z)
{
    D3DXMatrixPerspectiveRH(get_m(out), width, height, near_z, far_z);
}
void matrix_ortho_rh(void* out, float width, float height, float near_z, float far_z)
{
    D3DXMatrixOrthoRH(get_m(out), width, height, near_z, far_z);
}

long save_surface_to_jpg(const char* file, IDirect3DSurface9* surface)
{
    return D3DXSaveSurfaceToFileA(file, D3DXIFF_JPG, surface, nullptr, nullptr);
}

long assemble_shader(const char* source, uint32_t length, void** shader, void** errors)
{
    // (D3DXAssembleShader's ID3DXBuffer is a COM object with the same vtable the callers use)
    return D3DXAssembleShader(source, length, nullptr, nullptr, 0, raw::as<LPD3DXBUFFER>(shader),
                              raw::as<LPD3DXBUFFER>(errors));
}
void* buffer_pointer(void* buffer) { return static_cast<ID3DXBuffer*>(buffer)->GetBufferPointer(); }
void release_buffer(void* buffer) { static_cast<ID3DXBuffer*>(buffer)->Release(); }

} // namespace d3dx

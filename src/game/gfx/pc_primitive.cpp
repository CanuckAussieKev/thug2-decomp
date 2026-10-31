// The PC primitive drawer: see pc_primitive.h.

#include "game/gfx/pc_primitive.h"

#include <windows.h>

#include <d3d9.h>

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_primitive.h"
#include "game/gfx/material.h"
#include "game/gfx/pc_flexible_particles.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/gfx/simple_material.h"
#include "game/sys/platform_d3d.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/gfx/shader_slots.h"
#include "game/gfx/device.h"
#include "game/sys/crt.h"

namespace Nx {

// The game's buffer for the vertices of a primitive drawn without one of its own.
GAME_GLOBAL(uint8_t[0x100], g_primitiveVertexBuffer, 0x00705620);

namespace {

extern void** const kVtable;

IDirect3DDevice9* Device() { return g_d3dDevice; }
IDirect3DIndexBuffer9* QuadIndices() { return g_quadIndices; }
uint32_t& DrawCalls() { return reinterpret_cast<uint32_t&>(g_drawCalls); }
uint32_t& DrawnTriangles() { return reinterpret_cast<uint32_t&>(g_drawnTriangles); }
int Rand() { return Crt::Rand(); } // (the game's C runtime's)

template <typename T>
T& At(uint8_t* base, int offset)
{
    return *reinterpret_cast<T*>(base + offset);
}

// RGBA with red and blue swapped (D3D's BGRA).
uint32_t Swapped(uint32_t rgba) { return (rgba & 0xFF00FF00u) | (rgba & 0xFF) << 16 | (rgba >> 16 & 0xFF); }

} // namespace

// 0x004C5230
CPcPrimitive* CPcPrimitive::Construct(int own)
{
    reinterpret_cast<CPrimitive*>(this)->CPrimitive::Construct(own);
    vtable = reinterpret_cast<void**>(kVtable);
    reinterpret_cast<sMaterial*>(material)->Construct();
    uint32_t made = flags | 1;
    vertexFormat = 2;
    vertices = nullptr;
    vertexCount = 0;
    colour = 0x80808080;
    flags = made;
    At<uint32_t>(material, 0x20) = 0;
    int r = Rand();
    At<int>(material, 0x00) = r * Rand(); // (a sort key)
    At<uint32_t>(material, 0x08) = 1;
    At<uint32_t>(material, 0x30) = 0;
    material[0x0D] = 1;
    material[0x11] = 4;
    At<uint32_t>(material, 0x80) = BlendModeFromName(0x54628ED7);
    At<float>(material, 0x40) = At<float>(material, 0x44) = At<float>(material, 0x48) = 0.5f;
    At<uint32_t>(material, 0x90) = 0;
    At<uint32_t>(material, 0xE0) = 0;
    material[0x0E] = 0;
    material[0x10] = 1;
    return this;
}

// 0x004C5A20
void CPcPrimitive::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kVtable);
    if (ownsVertices == 1 && vertices)
        Mem::Manager::Get()->Free(vertices);
    reinterpret_cast<sMaterial*>(material)->DestroyBody();
    reinterpret_cast<CPrimitive*>(this)->CPrimitive::DestroyBody();
}

// 0x004C5ED0
CPcPrimitive* CPcPrimitive::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C5A90
void CPcPrimitive::SetTexture(const uint8_t* texture, int, int)
{
    uint32_t key = texture[8] & 1 ? **reinterpret_cast<uint32_t* const*>(texture + 0x10)
                                  : *reinterpret_cast<const uint32_t*>(texture + 0x10);
    At<uint32_t>(material, 0x20) = key ? 4 : 0;
    int r = Rand();
    At<int>(material, 0x00) = r * Rand();
    At<uint32_t>(material, 0x08) = 1;
    At<uint32_t>(material, 0x30) = key;
    material[0x0D] = 1;
    material[0x11] = 4;
    At<uint32_t>(material, 0x80) = BlendModeFromName(blendMode);
    At<float>(material, 0x40) = At<float>(material, 0x44) = At<float>(material, 0x48) = 0.5f;
    material[0x10] = 1;
    At<uint32_t>(material, 0x90) = 0;
    At<uint32_t>(material, 0xE0) = 0;
    material[0x0E] = 0;
}

// 0x004C5310
void CPcPrimitive::SetColour(uint32_t rgba) { colour = rgba; }

// 0x004C5F00
void CPcPrimitive::Init(uint32_t layoutFlags, int primitiveType, int count)
{
    vertexFormat = 2;
    if (layoutFlags & 1)
        vertexFormat = 0x42;
    if (layoutFlags & 2)
        vertexFormat |= 0x100;
    if (layoutFlags & 4)
        vertexFormat |= 0x10;
    if (ownsVertices == 1) {
        int size = 0;
        switch (vertexFormat) {
        case 0x2: size = 0xC; break;
        case 0x42: size = 0x10; break;
        case 0x102: size = 0x14; break;
        case 0x142: size = 0x18; break;
        case 0x152: size = 0x24; break;
        default: break;
        }
        if (size) {
            // (the 0x102 layout's allocation is Mem::New's inline; the others through it)
            vertices = static_cast<uint8_t*>(Mem::New(count * size));
            stride = size;
        }
    }
    vertexCount = count;
    flags = (flags & ~1u) | 2;
    layout = layoutFlags;
    type = primitiveType;
}

// 0x004C5320
void CPcPrimitive::Ready()
{
    if (!(vertexFormat & 0x100) || At<uint32_t>(material, 0x30) != 0)
        flags = (flags & ~2u) | 4;
}

// 0x004C5370
void CPcPrimitive::Render()
{
    At<uint32_t>(material, 0x80) = BlendModeFromName(blendMode);
    reinterpret_cast<sMaterial*>(material)->Submit(); // (its set-up)
    IDirect3DVertexBuffer9* buffer = nullptr;
    uint32_t firstVertex = 0, room = 0;
    SetVertexFormat(vertexFormat);
    SetPixelShader(g_pixelShaders[kPixelShader964], nullptr);
    if (type != 0 && type != 2 && type != 4)
        return;
    void* out = Platform::LockDynamicVertices(vertexCount, stride, &buffer, &firstVertex, &room);
    std::memcpy(out, vertices, static_cast<uint32_t>(vertexCount * stride));
    Platform::UnlockDynamicVertices(vertexCount, stride);
    Device()->SetStreamSource(0, buffer, 0, stride);
    int primitives;
    if (type == 0) {
        primitives = vertexCount / 2;
        Device()->DrawPrimitive(D3DPT_LINELIST, firstVertex, primitives);
    } else if (type == 2) {
        primitives = vertexCount / 3;
        Device()->DrawPrimitive(D3DPT_TRIANGLELIST, firstVertex, primitives);
    } else {
        Device()->SetIndices(QuadIndices());
        primitives = vertexCount / 2;
        Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, vertexCount, 0,
                                       primitives);
    }
    ++DrawCalls();
    DrawnTriangles() += primitives;
}

// 0x004C55D0
void CPcPrimitive::Draw(const float* positions, const uint32_t* colours, const float* uvs, const float* normals)
{
    int size = 0;
    switch (vertexFormat) {
    case 0x2: size = 0xC; break;
    case 0x42: size = 0x10; break;
    case 0x102: size = 0x14; break;
    case 0x142: size = 0x18; break;
    case 0x152: size = 0x24; break;
    default: break;
    }
    if (size) {
        stride = size;
        vertices = g_primitiveVertexBuffer; // (a buffer of the game's)
    }
    using Vec3 = void(__thiscall*)(CPcPrimitive*, int, float, float, float);
    for (int i = 0; i < vertexCount; ++i) {
        reinterpret_cast<Vec3>(vtable[7])(this, i, positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]);
        if (colours)
            reinterpret_cast<void(__thiscall*)(CPcPrimitive*, int, uint32_t)>(vtable[15])(this, i, colours[i]);
        if (uvs)
            reinterpret_cast<void(__thiscall*)(CPcPrimitive*, int, float, float)>(vtable[16])(this, i, uvs[2 * i],
                                                                                            uvs[2 * i + 1]);
        if (normals)
            reinterpret_cast<Vec3>(vtable[11])(this, i, normals[3 * i], normals[3 * i + 1], normals[3 * i + 2]);
    }
    reinterpret_cast<void(__thiscall*)(CPcPrimitive*)>(vtable[5])(this);
}

// 0x004C5B20
void CPcPrimitive::SetPosition(int index, float x, float y, float z)
{
    At<float>(vertices, stride * index) = x;
    At<float>(vertices, stride * index + 4) = y;
    At<float>(vertices, stride * index + 8) = z;
}
// 0x004C5B60
void CPcPrimitive::SetX(int index, float x) { At<float>(vertices, stride * index) = x; }
// 0x004C5B80
void CPcPrimitive::SetY(int index, float y) { At<float>(vertices, stride * index + 4) = y; }
// 0x004C5BA0
void CPcPrimitive::SetZ(int index, float z) { At<float>(vertices, stride * index + 8) = z; }

// 0x004C5BC0
void CPcPrimitive::SetNormal(int index, float x, float y, float z)
{
    At<float>(vertices, index * 0x24 + 0xC) = x;
    At<float>(vertices, index * 0x24 + 0x10) = y;
    At<float>(vertices, index * 0x24 + 0x14) = z;
}
// 0x004C5BF0
void CPcPrimitive::SetNormalX(int index, float x) { At<float>(vertices, index * 0x24 + 0xC) = x; }
// 0x004C5C10
void CPcPrimitive::SetNormalY(int index, float y) { At<float>(vertices, index * 0x24 + 0x10) = y; }
// 0x004C5C30
void CPcPrimitive::SetNormalZ(int index, float z) { At<float>(vertices, index * 0x24 + 0x14) = z; }

// 0x004C56E0
void CPcPrimitive::SetVertexColour(int index, uint32_t rgba)
{
    uint32_t bgra = Swapped(rgba);
    if (vertexFormat == 0x42)
        At<uint32_t>(vertices, index * 0x10 + 0xC) = bgra;
    else if (vertexFormat == 0x142)
        At<uint32_t>(vertices, index * 0x18 + 0xC) = (bgra & 0xFFFFFF) | 0x80000000;
    else if (vertexFormat == 0x152)
        At<uint32_t>(vertices, index * 0x24 + 0x18) = bgra;
}

// 0x004C5760
void CPcPrimitive::SetUV(int index, float u, float v)
{
    if (vertexFormat == 0x142) {
        At<float>(vertices, index * 0x18 + 0x10) = u;
        At<float>(vertices, index * 0x18 + 0x14) = v;
    } else if (vertexFormat == 0x152) {
        At<float>(vertices, index * 0x24 + 0x1C) = u;
        At<float>(vertices, index * 0x24 + 0x20) = v;
    }
}
// 0x004C57C0
void CPcPrimitive::SetU(int index, float u)
{
    if (vertexFormat == 0x142)
        At<float>(vertices, index * 0x18 + 0x10) = u;
    else if (vertexFormat == 0x152)
        At<float>(vertices, index * 0x24 + 0x1C) = u;
}
// 0x004C5800
void CPcPrimitive::SetV(int index, float v)
{
    if (vertexFormat == 0x142)
        At<float>(vertices, index * 0x18 + 0x14) = v;
    else if (vertexFormat == 0x152)
        At<float>(vertices, index * 0x24 + 0x20) = v;
}

// 0x004C5C50
float CPcPrimitive::GetX(int index) { return At<float>(vertices, stride * index); }
// 0x004C5C70
float CPcPrimitive::GetY(int index) { return At<float>(vertices, stride * index + 4); }
// 0x004C5C90
float CPcPrimitive::GetZ(int index) { return At<float>(vertices, stride * index + 8); }
// 0x004C5CB0
float CPcPrimitive::GetNormalX(int index) { return At<float>(vertices, index * 0x24 + 0xC); }
// 0x004C5CD0
float CPcPrimitive::GetNormalY(int index) { return At<float>(vertices, index * 0x24 + 0x10); }
// 0x004C5CF0
float CPcPrimitive::GetNormalZ(int index) { return At<float>(vertices, index * 0x24 + 0x14); }

// 0x004C5840
uint32_t CPcPrimitive::GetVertexColour(int index)
{
    if (vertexFormat == 0x42)
        return At<uint32_t>(vertices, index * 0x10 + 0xC);
    if (vertexFormat == 0x142)
        return At<uint32_t>(vertices, index * 0x18 + 0xC);
    if (vertexFormat == 0x152)
        return At<uint32_t>(vertices, index * 0x24 + 0x18);
    return 0;
}
// 0x004C5890
float CPcPrimitive::GetU(int index)
{
    if (vertexFormat == 0x142)
        return At<float>(vertices, index * 0x18 + 0x10);
    if (vertexFormat == 0x152)
        return At<float>(vertices, index * 0x24 + 0x1C);
    return 0.0f;
}
// 0x004C58D0
float CPcPrimitive::GetV(int index)
{
    if (vertexFormat == 0x142)
        return At<float>(vertices, index * 0x18 + 0x14);
    if (vertexFormat == 0x152)
        return At<float>(vertices, index * 0x24 + 0x20);
    return 0.0f;
}

// 0x004C5D10
void CPcPrimitive::Translate(float x, float y, float z)
{
    for (int i = 0; i < vertexCount; ++i) {
        At<float>(vertices, stride * i) = x + At<float>(vertices, stride * i);
        At<float>(vertices, stride * i + 4) = y + At<float>(vertices, stride * i + 4);
        At<float>(vertices, stride * i + 8) = z + At<float>(vertices, stride * i + 8);
    }
}
// 0x004C5D80
void CPcPrimitive::TranslateX(float x)
{
    for (int i = 0; i < vertexCount; ++i)
        At<float>(vertices, stride * i) = x + At<float>(vertices, stride * i);
}
// 0x004C5DC0
void CPcPrimitive::TranslateY(float y)
{
    for (int i = 0; i < vertexCount; ++i)
        At<float>(vertices, stride * i + 4) = y + At<float>(vertices, stride * i + 4);
}
// 0x004C5E00
void CPcPrimitive::TranslateZ(float z)
{
    for (int i = 0; i < vertexCount; ++i)
        At<float>(vertices, stride * i + 8) = z + At<float>(vertices, stride * i + 8);
}

// 0x004C5910
void CPcPrimitive::ScrollUV(float u, float v)
{
    for (int i = 0; i < vertexCount; ++i) {
        if (vertexFormat == 0x142) {
            At<float>(vertices, i * 0x18 + 0x10) = u + At<float>(vertices, i * 0x18 + 0x10);
            At<float>(vertices, i * 0x18 + 0x14) = v + At<float>(vertices, i * 0x18 + 0x14);
        } else if (vertexFormat == 0x152) {
            At<float>(vertices, i * 0x24 + 0x1C) = u + At<float>(vertices, i * 0x24 + 0x1C);
            At<float>(vertices, i * 0x24 + 0x20) = v + At<float>(vertices, i * 0x24 + 0x20);
        }
    }
}
// 0x004C5980
void CPcPrimitive::ScrollU(float u)
{
    for (int i = 0; i < vertexCount; ++i) {
        if (vertexFormat == 0x142)
            At<float>(vertices, i * 0x18 + 0x10) = u + At<float>(vertices, i * 0x18 + 0x10);
        else if (vertexFormat == 0x152)
            At<float>(vertices, i * 0x24 + 0x1C) = u + At<float>(vertices, i * 0x24 + 0x1C);
    }
}
// 0x004C59D0
void CPcPrimitive::ScrollV(float v)
{
    for (int i = 0; i < vertexCount; ++i) {
        if (vertexFormat == 0x142)
            At<float>(vertices, i * 0x18 + 0x14) = v + At<float>(vertices, i * 0x18 + 0x14);
        else if (vertexFormat == 0x152)
            At<float>(vertices, i * 0x24 + 0x20) = v + At<float>(vertices, i * 0x24 + 0x20);
    }
}

// 0x004C5340
int CPcPrimitive::Compare(const CPcPrimitive* other)
{
    if (type < other->type)
        return 1;
    return type <= other->type ? 0 : -1;
}

// 0x004C5E40
void __cdecl SetPrimitiveDepthTest(int state, int value)
{
    DWORD function = D3DCMP_LESSEQUAL;
    if (state == 1) {
        switch (value) {
        case 1: function = D3DCMP_NEVER; break;
        case 2: function = D3DCMP_ALWAYS; break;
        case 3: function = D3DCMP_LESSEQUAL; break;
        case 4: function = D3DCMP_LESS; break;
        default: break;
        }
    }
    Device()->SetRenderState(D3DRS_ZFUNC, function);
}

namespace {
GAME_VTABLE(kVtable, 0x0064A628, &Nx::CPcPrimitive::Destroy, &Nx::CPcPrimitive::SetTexture,
            &Nx::CPcPrimitive::SetColour, &Nx::CPcPrimitive::Init, &Nx::CPcPrimitive::Ready,
            &Nx::CPcPrimitive::Render, &Nx::CPcPrimitive::Draw, &Nx::CPcPrimitive::SetPosition,
            &Nx::CPcPrimitive::SetX, &Nx::CPcPrimitive::SetY, &Nx::CPcPrimitive::SetZ, &Nx::CPcPrimitive::SetNormal,
            &Nx::CPcPrimitive::SetNormalX, &Nx::CPcPrimitive::SetNormalY, &Nx::CPcPrimitive::SetNormalZ,
            &Nx::CPcPrimitive::SetVertexColour, &Nx::CPcPrimitive::SetUV, &Nx::CPcPrimitive::SetU,
            &Nx::CPcPrimitive::SetV, &Nx::CPcPrimitive::GetX, &Nx::CPcPrimitive::GetY, &Nx::CPcPrimitive::GetZ,
            &Nx::CPcPrimitive::GetNormalX, &Nx::CPcPrimitive::GetNormalY, &Nx::CPcPrimitive::GetNormalZ,
            &Nx::CPcPrimitive::GetVertexColour, &Nx::CPcPrimitive::GetU, &Nx::CPcPrimitive::GetV,
            &Nx::CPcPrimitive::Translate, &Nx::CPcPrimitive::TranslateX, &Nx::CPcPrimitive::TranslateY,
            &Nx::CPcPrimitive::TranslateZ, &Nx::CPcPrimitive::ScrollUV, &Nx::CPcPrimitive::ScrollU,
            &Nx::CPcPrimitive::ScrollV, &Nx::CPcPrimitive::Compare);
} // namespace

} // namespace Nx

REPLACE(0x004C5230, &Nx::CPcPrimitive::Construct);
REPLACE(0x004C5A20, &Nx::CPcPrimitive::DestroyBody);
REPLACE(0x004C5ED0, &Nx::CPcPrimitive::Destroy);
REPLACE(0x004C5A90, &Nx::CPcPrimitive::SetTexture);
REPLACE(0x004C5310, &Nx::CPcPrimitive::SetColour);
REPLACE(0x004C5F00, &Nx::CPcPrimitive::Init);
REPLACE(0x004C5320, &Nx::CPcPrimitive::Ready);
REPLACE(0x004C5370, &Nx::CPcPrimitive::Render);
REPLACE(0x004C55D0, &Nx::CPcPrimitive::Draw);
REPLACE(0x004C5B20, &Nx::CPcPrimitive::SetPosition);
REPLACE(0x004C5B60, &Nx::CPcPrimitive::SetX);
REPLACE(0x004C5B80, &Nx::CPcPrimitive::SetY);
REPLACE(0x004C5BA0, &Nx::CPcPrimitive::SetZ);
REPLACE(0x004C5BC0, &Nx::CPcPrimitive::SetNormal);
REPLACE(0x004C5BF0, &Nx::CPcPrimitive::SetNormalX);
REPLACE(0x004C5C10, &Nx::CPcPrimitive::SetNormalY);
REPLACE(0x004C5C30, &Nx::CPcPrimitive::SetNormalZ);
REPLACE(0x004C56E0, &Nx::CPcPrimitive::SetVertexColour);
REPLACE(0x004C5760, &Nx::CPcPrimitive::SetUV);
REPLACE(0x004C57C0, &Nx::CPcPrimitive::SetU);
REPLACE(0x004C5800, &Nx::CPcPrimitive::SetV);
REPLACE(0x004C5C50, &Nx::CPcPrimitive::GetX);
REPLACE(0x004C5C70, &Nx::CPcPrimitive::GetY);
REPLACE(0x004C5C90, &Nx::CPcPrimitive::GetZ);
REPLACE(0x004C5CB0, &Nx::CPcPrimitive::GetNormalX);
REPLACE(0x004C5CD0, &Nx::CPcPrimitive::GetNormalY);
REPLACE(0x004C5CF0, &Nx::CPcPrimitive::GetNormalZ);
REPLACE(0x004C5840, &Nx::CPcPrimitive::GetVertexColour);
REPLACE(0x004C5890, &Nx::CPcPrimitive::GetU);
REPLACE(0x004C58D0, &Nx::CPcPrimitive::GetV);
REPLACE(0x004C5D10, &Nx::CPcPrimitive::Translate);
REPLACE(0x004C5D80, &Nx::CPcPrimitive::TranslateX);
REPLACE(0x004C5DC0, &Nx::CPcPrimitive::TranslateY);
REPLACE(0x004C5E00, &Nx::CPcPrimitive::TranslateZ);
REPLACE(0x004C5910, &Nx::CPcPrimitive::ScrollUV);
REPLACE(0x004C5980, &Nx::CPcPrimitive::ScrollU);
REPLACE(0x004C59D0, &Nx::CPcPrimitive::ScrollV);
REPLACE(0x004C5340, &Nx::CPcPrimitive::Compare);
REPLACE(0x004C5E40, Nx::SetPrimitiveDepthTest);

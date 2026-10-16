// The per-frame 3D render: the camera's matrices and the motion blur pass.

#include "game/gfx/render.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// --- the renderer's state (not rewritten yet; cdecl) ---------------------------------------------------------

// A render state by the renderer's own numbering (1-0x24), cached.
void SetRenderState(int state, int value) { GameFunction<void(__cdecl*)(int, int)>(0x004DAB60)(state, value); }
void SetTexture(int stage, void* texture, int unknown) { GameFunction<void(__cdecl*)(int, void*, int)>(0x004DA730)(stage, texture, unknown); }
void SetBlendMode(int mode) { GameFunction<void(__cdecl*)(int)>(0x004DA760)(mode); }
void SetVertexFormat(uint32_t fvf) { GameFunction<void(__cdecl*)(uint32_t)>(0x004D9DB0)(fvf); }
void SetVertexShader(int shader, int unknown) { GameFunction<void(__cdecl*)(int, int)>(0x004DAA80)(shader, unknown); }
// Fog on or off (the request is kept in 0x007004C8; the device's D3DRS_FOGENABLE follows).
void SetFog(bool on) { GameFunction<void(__cdecl*)(int)>(0x00490DE0)(on); }

// --- the D3D9 device (0x007D69E0) and its objects, through their vtables ----------------------------------------

void* Device() { return GameVariable<void*>(0x007D69E0); }
template <typename F>
F Method(void* object, int offset) { return reinterpret_cast<F>((*reinterpret_cast<void***>(object))[offset / 4]); }

void SetTransform(int type, const Mth::Matrix* m) { Method<long(__stdcall*)(void*, int, const void*)>(Device(), 0xB0)(Device(), type, m); }
void SetRenderTarget(void* surface) { Method<long(__stdcall*)(void*, int, void*)>(Device(), 0x94)(Device(), 0, surface); }
void SetDepthStencilSurface(void* surface) { Method<long(__stdcall*)(void*, void*)>(Device(), 0x9C)(Device(), surface); }
void SetTextureStageState(int stage, int type, int value)
{
    Method<long(__stdcall*)(void*, int, int, int)>(Device(), 0x10C)(Device(), stage, type, value);
}
uint32_t GetSamplerState(int sampler, int type)
{
    uint32_t value;
    Method<long(__stdcall*)(void*, int, int, uint32_t*)>(Device(), 0x110)(Device(), sampler, type, &value);
    return value;
}
void SetSamplerState(int sampler, int type, uint32_t value)
{
    Method<long(__stdcall*)(void*, int, int, uint32_t)>(Device(), 0x114)(Device(), sampler, type, value);
}
void ClearTarget(uint32_t color)
{
    Method<long(__stdcall*)(void*, uint32_t, const void*, uint32_t, uint32_t, float, uint32_t)>(Device(), 0xAC)(
        Device(), 0, nullptr, 1 /* D3DCLEAR_TARGET */, color, 1.0f, 0);
}
void DrawPrimitiveUP(int type, unsigned int count, const void* vertices, unsigned int stride)
{
    Method<long(__stdcall*)(void*, int, unsigned int, const void*, unsigned int)>(Device(), 0x14C)(Device(), type, count,
                                                                                               vertices, stride);
}

// --- D3DX (linked library code; stdcall) ----------------------------------------------------------------------

void MatrixLookAt(Mth::Matrix* out, const float* eye, const float* at, const float* up)
{
    GameFunction<void*(__stdcall*)(void*, const float*, const float*, const float*)>(0x005C0985)(out, eye, at, up);
}
void MatrixPerspective(Mth::Matrix* out, float width, float height, float nearZ, float farZ)
{
    GameFunction<void*(__stdcall*)(void*, float, float, float, float)>(0x005C0ACD)(out, width, height, nearZ, farZ);
}
void MatrixScaling(Mth::Matrix* out, float x, float y, float z)
{
    GameFunction<void*(__stdcall*)(void*, float, float, float)>(0x005C036B)(out, x, y, z);
}
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(void*, const void*, const void*)>(0x005BFA8C)(out, a, b);
}
void MatrixRotationY(Mth::Matrix* out, float angle)
{
    GameFunction<void*(__stdcall*)(void*, float)>(0x005C0509)(out, angle);
}

// --- x87 instructions the original uses, so results round exactly as there ---------------------------------------

// FPTAN of `angle` (unrounded), times `factor`, rounded once to float.
float TanTimes(double angle, float factor)
{
    float result;
    __asm {
        fld angle
        fptan
        fstp st(0)
        fmul factor
        fstp result
    }
    return result;
}
// FPTAN rounded once to float.
float Tan(float angle)
{
    float result;
    __asm {
        fld angle
        fptan
        fstp st(0)
        fstp result
    }
    return result;
}
// FPATAN: atan2(y, x) - pi/2 (the pi/2 a float), rounded once to float.
float Atan2LessQuarterTurn(float y, float x)
{
    const float quarterTurn = 1.5707963705062866f;
    float result;
    __asm {
        fld y
        fld x
        fpatan
        fsub quarterTurn
        fstp result
    }
    return result;
}

float FloatFromBits(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// The camera's state (globals).
struct Camera {
    float nearZ;      // 0x00786CA8
    float farZ;       // 0x00786CAC
    float angle;      // 0x00786CB0: horizontal field of view, degrees
    float nearWidth;  // 0x00786CB4: the view's size at the near plane
    float nearHeight; // 0x00786CB8
    uint8_t unknown14[0x08];
    float position[3]; // 0x00786CC4
    uint8_t unknown28[0x0C];
    float backward[3]; // 0x00786CDC: the orientation's row 2
    float up[3];       // 0x00786CE8: row 1
};
static_assert(offsetof(Camera, position) == 0x1C, "0x00786CC4");
static_assert(offsetof(Camera, backward) == 0x34, "0x00786CDC");

// The view's half-angle shape, for culling (0x00786E18).
struct ViewShape {
    float tanX;  // tan(half the horizontal angle)
    float tanY;  // -tanX / (aspect / 1.1): the vertical one, negated
    float sinX;  // sin and cos of the two half-angles
    float sinY;
    float cosX;
    float cosY;
};

} // namespace

// 0x004DB200
void SetCamera(const Mth::Matrix* orientation, const Mth::Vector* position, float angle, float aspect, float zoom,
               bool infinite)
{
    auto& camera = GameVariable<Camera>(0x00786CA8);
    auto& world = GameVariable<Mth::Matrix>(0x00786B90);
    auto& view = GameVariable<Mth::Matrix>(0x00786BD0);
    auto& projection = GameVariable<Mth::Matrix>(0x00786C10);

    camera.position[0] = position->x;
    camera.position[1] = position->y;
    camera.position[2] = position->z;
    camera.backward[0] = orientation->row[2].x;
    camera.backward[1] = orientation->row[2].y;
    camera.backward[2] = orientation->row[2].z;
    camera.up[0] = orientation->row[1].x;
    camera.up[1] = orientation->row[1].y;
    camera.up[2] = orientation->row[1].z;
    float target[3] = {camera.position[0] - camera.backward[0], camera.position[1] - camera.backward[1],
                       camera.position[2] - camera.backward[2]};
    world = Mth::Matrix{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    MatrixLookAt(&view, camera.position, target, camera.up);

    camera.nearZ = 2.0f;
    // PartyMod (its float at 0x004DB35D): the far plane 96000 - further, for large levels' backgrounds (e.g.
    // Skatopia). The original: camera.farZ = 32000.0f;
    camera.farZ = 96000.0f;
    camera.angle = angle;
    if (GameVariable<bool>(0x007D643A)) // distance clipping (setting 0x007D6444: 100-595)
        camera.farZ = static_cast<float>(static_cast<double>(GameVariable<uint32_t>(0x007D6444)) * 18.0);

    // (degrees to half the angle in radians: the tangent takes the product unrounded, later uses a float)
    double halfAngle = static_cast<double>(angle) * 0.008726646f;
    float width = TanTimes(halfAngle, 4.0f);
    float heightFactor = static_cast<float>(1.0 / (static_cast<double>(aspect) * 0.90909094f));
    float height = heightFactor * width;
    MatrixPerspective(&projection, width, height, camera.nearZ, camera.farZ);
    if (zoom != 1.0f) {
        Mth::Matrix scale;
        MatrixScaling(&scale, zoom, zoom, 1.0f);
        MatrixMultiply(&projection, &projection, &scale);
    }
    camera.nearHeight = height;
    camera.nearWidth = width;
    if (infinite) {
        projection.row[2].z = -0.999999f;
        projection.row[3].z = 1e-6f;
    }
    SetTransform(0x100, &world); // D3DTS_WORLD
    SetTransform(2, &view);      // D3DTS_VIEW
    SetTransform(3, &projection);

    auto& shape = GameVariable<ViewShape>(0x00786E18);
    shape.tanX = Tan(static_cast<float>(halfAngle));
    shape.tanY = -(heightFactor * shape.tanX);
    double tanX2 = static_cast<double>(shape.tanX) * shape.tanX;
    double tanY2 = static_cast<double>(shape.tanY) * shape.tanY;
    shape.sinX = static_cast<float>(1.0 / std::sqrt(1.0 / tanX2 + 1.0));
    shape.sinY = static_cast<float>(1.0 / std::sqrt(1.0 / tanY2 + 1.0));
    shape.cosX = static_cast<float>(1.0 / std::sqrt(tanX2 + 1.0));
    shape.cosY = static_cast<float>(1.0 / std::sqrt(tanY2 + 1.0));

    // The camera's heading: a rotation about the vertical axis (0x00786C50).
    MatrixRotationY(&GameVariable<Mth::Matrix>(0x00786C50),
                    Atan2LessQuarterTurn(-camera.backward[2], -camera.backward[0]));
    // The view frustum (0x00775B80) follows the new view.
    GameFunction<void(__thiscall*)(void*)>(0x004CDAA0)(&GameVariable<uint8_t>(0x00775B80));
}

namespace {

struct ScreenVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};
static_assert(sizeof(ScreenVertex) == 0x1C, "the draw's stride");

} // namespace

// 0x004DF390
void DrawScreenBlur()
{
    if (!GameVariable<bool>(0x007CC244)) // blur available
        return;
    SetFog(false);
    SetRenderState(0x15, 0);
    SetRenderState(0x16, 0);
    SetRenderState(0x10, 1);
    // Linear filtering on all four samplers (put back at the end).
    uint32_t minFilter[4], magFilter[4];
    for (int i = 0; i < 4; ++i) {
        minFilter[i] = GetSamplerState(i, 6); // D3DSAMP_MINFILTER
        if (minFilter[i] != 2)                // D3DTEXF_LINEAR
            SetSamplerState(i, 6, 2);
        magFilter[i] = GetSamplerState(i, 5); // D3DSAMP_MAGFILTER
        if (magFilter[i] != 2)
            SetSamplerState(i, 5, 2);
    }
    if (GameVariable<void*>(0x00705720))
        GameFunction<void(__cdecl*)()>(0x004DEB70)();
    SetRenderState(4, 0x10001);
    SetRenderState(1, 0);
    SetRenderState(2, 0);
    SetRenderState(0x12, 0);
    SetRenderState(0x11, 1);

    // Two textures take turns: the one just drawn into (the last frame's blur) and the other.
    int current = GameVariable<int>(0x00786D68);
    void* const* textures = &GameVariable<void*>(0x00786D1C);
    void* texture = textures[current];
    void* other = textures[(current + 1) % 2];
    uint32_t desc[8]; // D3DSURFACE_DESC
    Method<long(__stdcall*)(void*, unsigned int, uint32_t*)>(texture, 0x44)(texture, 0, desc); // GetLevelDesc
    uint32_t textureWidth = desc[6], textureHeight = desc[7];

    // The quad covers the screen; the texture is drawn shrunk towards its centre by the blur amount.
    uint32_t amount = GameVariable<uint32_t>(0x00787328);
    float inset = static_cast<float>((1.0 - static_cast<double>(amount) * (1.0f / 255)) * 0.5);
    double insetU = inset / static_cast<double>(textureWidth); // (the u's are rounded from this, unrounded)
    float insetV = static_cast<float>(inset / static_cast<double>(textureHeight));
    float screenWidth = static_cast<float>(GameVariable<uint32_t>(0x00786A7C));
    float screenHeight = static_cast<float>(GameVariable<uint32_t>(0x00786A80));
    uint32_t color = 0x80808080;
    ScreenVertex quad[4] = {
        {0, 0, 0, 1, color, static_cast<float>(insetU), insetV},
        {screenWidth, 0, 0, 1, color, static_cast<float>(1.0 - insetU), insetV},
        {0, screenHeight, 0, 1, color, static_cast<float>(insetU), 1.0f - insetV},
        {screenWidth, screenHeight, 0, 1, color, static_cast<float>(1.0 - insetU), 1.0f - insetV},
    };
    // PartyMod (its JMP at 0x004DF63A): no letterbox adjustment - PartyMod's letterbox is a viewport. The
    // original, when letterboxed (0x00786CBE), moved the quad in by an eighth of the back buffer's height:
    //   float bar = static_cast<float>(GameVariable<int>(0x00786D70) / 8);
    //   quad[0].y = quad[1].y = bar;
    //   quad[0].v = quad[1].v = insetV + bar;   (sic: a pixel count added to a texture coordinate)
    //   quad[2].y -= bar; quad[3].y -= bar; quad[2].v -= bar; quad[3].v -= bar;
    if (amount) {
        SetBlendMode(GameVariable<uint32_t>(0x0078732C) > 2 ? 5 : 0);
        color = amount << 24 | 0x808080;
        for (ScreenVertex& v : quad)
            v.color = color;
    } else {
        SetBlendMode(0);
    }

    // Draw into `texture`: the other texture (if the blur level, 0x0078732C, is above 2)...
    void* surface = nullptr;
    Method<long(__stdcall*)(void*, unsigned int, void**)>(texture, 0x48)(texture, 0, &surface); // GetSurfaceLevel
    void* depth = GameVariable<void*>(0x00786D64);
    SetRenderTarget(surface);
    SetDepthStencilSurface(depth);
    Method<unsigned long(__stdcall*)(void*)>(surface, 8)(surface); // Release
    SetVertexFormat(0x144);                                       // XYZRHW | DIFFUSE | TEX1
    SetVertexShader(0, 0);
    SetTextureStageState(0, 1, 2); // colour op: select arg 1
    SetTextureStageState(0, 2, 2); // colour arg 1: texture
    SetTextureStageState(0, 4, 1); // alpha op: disable
    if (GameVariable<uint32_t>(0x0078732C) > 2) {
        SetRenderState(0x11, 1);
        SetTexture(0, other, 0);
        DrawPrimitiveUP(5 /* triangle strip */, 2, quad, sizeof(ScreenVertex));
        ++GameVariable<int>(0x00786EBC); // draw calls
        GameVariable<int>(0x00786EC0) += 2; // triangles
    }
    // ...then `texture` onto the back buffer, cleared first to the fog colour (with fog and distance clipping
    // on) or the background colour.
    SetRenderTarget(GameVariable<void*>(0x00786D10));
    SetDepthStencilSurface(GameVariable<void*>(0x00786D14));
    bool fogColour = GameVariable<bool>(0x007D6436) && GameVariable<bool>(0x007D643A);
    ClearTarget(fogColour ? GameVariable<uint32_t>(0x0078733C) : GameVariable<uint32_t>(0x00786CC0));
    SetRenderState(0x11, 0);
    SetTexture(0, texture, 0);
    DrawPrimitiveUP(5, 2, quad, sizeof(ScreenVertex));
    SetRenderState(0x11, 1);
    ++GameVariable<int>(0x00786EBC);
    GameVariable<int>(0x00786EC0) += 2;
    for (int i = 0; i < 4; ++i) {
        SetSamplerState(i, 6, minFilter[i]);
        SetSamplerState(i, 5, magFilter[i]);
    }
    GameVariable<int>(0x00786D68) = (GameVariable<int>(0x00786D68) + 1) % 2;
    SetFog(true);
}

} // namespace Nx

REPLACE(0x004DB200, Nx::SetCamera);
REPLACE(0x004DF390, Nx::DrawScreenBlur);

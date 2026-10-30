// The per-frame 3D render: the camera's matrices and the motion blur pass, with PartyMod's widescreen
// display region and black bars (contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).

#include "game/gfx/render.h"
#include "game/gfx/device.h"
#include "game/gfx/pc_scene.h"
#include "game/gfx/settings.h"
#include "game/gfx/view_mode.h"
#include "game/gfx/wireframe.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/front/level_editor.h"
#include "game/front/new_frontend.h"
#include "game/front/test_view.h"
#include "game/gfx/camera.h"
#include "game/gfx/draw2d.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_imposters.h"
#include "game/gfx/engine_primitive.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/geom.h"
#include "game/gfx/glow.h"
#include "game/gfx/graphics_options.h"
#include "game/gfx/lens_flare.h"
#include "game/gfx/mesh.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/particles.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/pc_texture_projection.h"
#include "game/gfx/render_state.h"
#include "game/gfx/scene_render.h"
#include "game/gfx/screen.h"
#include "game/gfx/screen_fx.h"
#include "game/gfx/shadow.h"
#include "game/gfx/texture_splats.h"
#include "game/gfx/viewport.h"
#include "game/gfx/viewport_list.h"
#include "game/sys/screenshot.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Nx {

GAME_GLOBAL(DeviceViewport, g_deviceViewport, 0x00786C90);
GAME_GLOBAL(RenderCamera, g_camera, 0x00786CA8);
GAME_GLOBAL(ViewShape, g_viewShape, 0x00786E18);

GAME_GLOBAL(bool, g_radeon8500, 0x00786AB5);
GAME_GLOBAL(int, g_framesRendered, 0x00786AC4);
GAME_GLOBAL(Mth::Matrix, g_worldMatrix, 0x00786B90);
GAME_GLOBAL(Mth::Matrix, g_viewMatrix, 0x00786BD0);
GAME_GLOBAL(Mth::Matrix, g_projectionMatrix, 0x00786C10);
GAME_GLOBAL(Mth::Matrix, g_headingMatrix, 0x00786C50);
GAME_GLOBAL(int, g_frameTime, 0x00786D00);
GAME_GLOBAL(int, g_screenCopyIndex, 0x00786D68);
GAME_GLOBAL(int, g_backBufferWidth, 0x00786D6C);
GAME_GLOBAL(int, g_backBufferHeight, 0x00786D70);
GAME_GLOBAL(int, g_drawCalls, 0x00786EBC);
GAME_GLOBAL(int, g_drawnTriangles, 0x00786EC0);
GAME_GLOBAL(float, g_fogStart, 0x00787340);
GAME_GLOBAL(float, g_fogEnd, 0x00787344);

namespace {

// --- the renderer's state (render_state.h) ---------------------------------------------------------------------

// A render state by the renderer's own numbering (1-0x24), cached.
void SetRenderState(int state, int value) { SetState(state, static_cast<uint32_t>(value)); }
// Fog on or off (the request is kept in 0x007004C8; the device's D3DRS_FOGENABLE follows).
void SetFog(bool on) { Engine::EnableFog(on); }

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



} // namespace

// --- PartyMod's display region and black bars ---------------------------------------------------------------

void SetDisplayRegion()
{
    // the 640x480 layout's size on the back buffer (SetLetterbox), centred
    auto width = static_cast<uint32_t>(GameVariable<float>(0x00786D80) * 640.0f);
    auto height = static_cast<uint32_t>(GameVariable<float>(0x00786D84) * 480.0f);
    // (the device viewport, 0x00786C90: x, y, width, height)
    g_deviceViewport.x = (reinterpret_cast<uint32_t&>(g_backBufferWidth) - width) / 2;
    g_deviceViewport.width = width;
    g_deviceViewport.y = (reinterpret_cast<uint32_t&>(g_backBufferHeight) - height) / 2;
    g_deviceViewport.height = height;
}

void Draw2DWithBlackBars()
{
    struct BarVertex {
        float x, y, z, w;
        uint32_t color;
        float u, v;
    };
    Draw2D(); // the 2D pass
    // (widescreen_menus: in the front end the 3D scene fills the sides, so no bars over it.)
    if (FrontEndWidescreen())
        return;

    uint32_t backBufferWidth = reinterpret_cast<uint32_t&>(g_backBufferWidth);
    uint32_t backBufferHeight = reinterpret_cast<uint32_t&>(g_backBufferHeight);
    float backBufferAspect = static_cast<float>(backBufferWidth) / static_cast<float>(backBufferHeight);
    constexpr uint32_t kBlack = 0xFF000000;
    if (backBufferAspect > DesiredAspectRatio()) {
        // bars left and right
        auto width = static_cast<uint32_t>(std::round(static_cast<double>(GameVariable<float>(0x00786D80) * 640.0f)));
        uint32_t barWidth = (backBufferWidth - width) / 2;
        SetTexture(0, nullptr);
        BarVertex bar[4] = {
            {-0.5f, -0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
            {-0.5f, static_cast<float>(backBufferHeight) + 0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
            {static_cast<float>(barWidth) + 0.5f, static_cast<float>(backBufferHeight) + 0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
            {static_cast<float>(barWidth) + 0.5f, -0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
        };
        DrawPrimitiveUP(6 /* D3DPT_TRIANGLEFAN */, 2, bar, sizeof(BarVertex));
        for (BarVertex& corner : bar)
            corner.x += static_cast<float>(width + barWidth);
        DrawPrimitiveUP(6, 2, bar, sizeof(BarVertex));
    } else if (backBufferAspect < DesiredAspectRatio()) {
        // bars top and bottom
        auto height = static_cast<uint32_t>(std::round(static_cast<double>(GameVariable<float>(0x00786D84) * 480.0f)));
        uint32_t barHeight = (backBufferHeight - height) / 2;
        SetTexture(0, nullptr);
        BarVertex bar[4] = {
            {-0.5f, -0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
            {-0.5f, static_cast<float>(barHeight) + 0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
            {static_cast<float>(backBufferWidth) + 0.5f, static_cast<float>(barHeight) + 0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
            {static_cast<float>(backBufferWidth) + 0.5f, -0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
        };
        DrawPrimitiveUP(6, 2, bar, sizeof(BarVertex));
        for (BarVertex& corner : bar)
            corner.y += static_cast<float>(height + barHeight);
        DrawPrimitiveUP(6, 2, bar, sizeof(BarVertex));
    }
}

// Not original - the level editor's ortho views (game/front/level_editor_camera.h): the near plane brought up for the
// editor's camera while RenderViewport draws the world with it (2, the original's, otherwise).
static float s_nearZ = 2.0f;

// 0x004DB200
void SetCamera(const Mth::Matrix* orientation, const Mth::Vector* position, float angle, float aspect, float zoom,
               bool infinite)
{
    auto& camera = g_camera;
    auto& world = g_worldMatrix;
    auto& view = g_viewMatrix;
    auto& projection = g_projectionMatrix;

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

    // (not original: s_nearZ - the original: camera.nearZ = 2.0f;)
    camera.nearZ = s_nearZ;
    // Originally from PartyMod Fixes (its float at 0x004DB35D): the far plane 96000 - further, for large levels' backgrounds (e.g.
    // Skatopia). The original: camera.farZ = 32000.0f;
    camera.farZ = 96000.0f;
    camera.angle = angle;
    if (g_distanceClipping) // distance clipping (setting 0x007D6444: 100-595)
        camera.farZ = static_cast<float>(static_cast<double>(GameVariable<uint32_t>(0x007D6444)) * 18.0);
    // NOT ORIGINAL - [Graphics] draw_distance_scale (game/gfx/graphics_options.h): the far plane (and with it the
    // distance clipping's cull distance, 0x00786CAC) further out.
    camera.farZ = GraphicsOptions::Scaled(camera.farZ, GraphicsOptions::Get().drawDistanceScale);

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

    auto& shape = g_viewShape;
    shape.tanX = Tan(static_cast<float>(halfAngle));
    shape.tanY = -(heightFactor * shape.tanX);
    double tanX2 = static_cast<double>(shape.tanX) * shape.tanX;
    double tanY2 = static_cast<double>(shape.tanY) * shape.tanY;
    shape.sinX = static_cast<float>(1.0 / std::sqrt(1.0 / tanX2 + 1.0));
    shape.sinY = static_cast<float>(1.0 / std::sqrt(1.0 / tanY2 + 1.0));
    shape.cosX = static_cast<float>(1.0 / std::sqrt(tanX2 + 1.0));
    shape.cosY = static_cast<float>(1.0 / std::sqrt(tanY2 + 1.0));

    // The camera's heading: a rotation about the vertical axis (0x00786C50).
    MatrixRotationY(&g_headingMatrix,
                    Atan2LessQuarterTurn(-camera.backward[2], -camera.backward[0]));
    // The view frustum (0x00775B80) follows the new view.
    reinterpret_cast<sBillboardManager*>(&GameVariable<uint8_t>(0x00775B80))->SetupAxes();
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
    if (!g_screenCopyInUse) // blur available
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
    if (g_bloomOn)
        Bloom();
    SetRenderState(4, 0x10001);
    SetRenderState(1, 0);
    SetRenderState(2, 0);
    SetRenderState(0x12, 0);
    SetRenderState(0x11, 1);

    // Two textures take turns: the one just drawn into (the last frame's blur) and the other.
    int current = g_screenCopyIndex;
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
    float screenHeight = static_cast<float>(g_presentParameters.BackBufferHeight);
    uint32_t color = 0x80808080;
    ScreenVertex quad[4] = {
        {0, 0, 0, 1, color, static_cast<float>(insetU), insetV},
        {screenWidth, 0, 0, 1, color, static_cast<float>(1.0 - insetU), insetV},
        {0, screenHeight, 0, 1, color, static_cast<float>(insetU), 1.0f - insetV},
        {screenWidth, screenHeight, 0, 1, color, static_cast<float>(1.0 - insetU), 1.0f - insetV},
    };
    // Originally from PartyMod Fixes (its JMP at 0x004DF63A): no letterbox adjustment - PartyMod's letterbox is a viewport. The
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
    // (NOT ORIGINAL: or the multisampled scene surface - [Graphics] msaa, game/gfx/graphics_options.h)
    if (!GraphicsOptions::SetSceneTarget(static_cast<IDirect3DTexture9*>(texture))) {
        SetRenderTarget(surface);
        SetDepthStencilSurface(depth);
    }
    Method<unsigned long(__stdcall*)(void*)>(surface, 8)(surface); // Release
    SetVertexFormat(0x144);                                       // XYZRHW | DIFFUSE | TEX1
    SetPixelShader(nullptr, nullptr); // (no pixel shader)
    SetTextureStageState(0, 1, 2); // colour op: select arg 1
    SetTextureStageState(0, 2, 2); // colour arg 1: texture
    SetTextureStageState(0, 4, 1); // alpha op: disable
    if (GameVariable<uint32_t>(0x0078732C) > 2) {
        SetRenderState(0x11, 1);
        SetTexture(0, other);
        DrawPrimitiveUP(5 /* triangle strip */, 2, quad, sizeof(ScreenVertex));
        ++g_drawCalls; // draw calls
        g_drawnTriangles += 2; // triangles
    }
    // ...then `texture` onto the back buffer, cleared first to the fog colour (with fog and distance clipping
    // on) or the background colour.
    // NOT ORIGINAL - [Graphics] msaa (game/gfx/graphics_options.h): the multisampled frame resolved into `texture`.
    GraphicsOptions::ResolveSceneTarget(static_cast<IDirect3DTexture9*>(texture));
    SetRenderTarget(GameVariable<void*>(0x00786D10));
    SetDepthStencilSurface(GameVariable<void*>(0x00786D14));
    bool fogColour = g_fogSetting && g_distanceClipping;
    ClearTarget(fogColour ? GameVariable<uint32_t>(0x0078733C) : GameVariable<uint32_t>(0x00786CC0));
    SetRenderState(0x11, 0);
    SetTexture(0, texture);
    DrawPrimitiveUP(5, 2, quad, sizeof(ScreenVertex));
    SetRenderState(0x11, 1);
    ++g_drawCalls;
    g_drawnTriangles += 2;
    for (int i = 0; i < 4; ++i) {
        SetSamplerState(i, 6, minFilter[i]);
        SetSamplerState(i, 5, magFilter[i]);
    }
    g_screenCopyIndex = (g_screenCopyIndex + 1) % 2;
    SetFog(true);
}

namespace {

// The screen's viewports (0x004A06D0 returns one by index): fractions of the back buffer, and the camera.
struct Viewport {
    uint32_t unknown00;
    float x, y, width, height; // +0x04
    uint32_t unknown14;
    void* camera;              // +0x18 (null: nothing to draw)
};
static_assert(offsetof(Viewport, camera) == 0x18, "viewport layout");


// A world scene's mesh group: flagged 0x100 once its meshes are prepared.
struct MeshGroup {
    uint32_t flags;
    uint8_t unknown04[0x18];
    void** meshes; // +0x1C
    int meshCount; // +0x20
};
static_assert(offsetof(MeshGroup, meshCount) == 0x20, "mesh group layout");

// A loaded world scene (only what the render uses).
struct Scene {
    uint8_t unknown000[0x84];
    uint32_t name;          // +0x84: the checksum of its name
    void* texDict;          // +0x88
    uint8_t unknown08C;
    bool sky;               // +0x8D: drawn in the sky pass
    uint8_t unknown08E[0x1F2];
    MeshGroup* group;       // +0x280
};
static_assert(offsetof(Scene, group) == 0x280 && offsetof(Scene, sky) == 0x8D && offsetof(Scene, name) == 0x84,
              "scene layout");

// The list of loaded scenes (0x006FA7EC): ends at a node whose scene is -1.
struct SceneNode {
    uint32_t unknown0;
    Scene* scene;    // +0x04
    uint32_t unknown8;
    SceneNode* next; // +0x0C
};

bool IsEnd(const SceneNode* node) { return reinterpret_cast<intptr_t>(node->scene) == -1; }

// Calls fn(scene) for every scene in the list (the next node is taken before the call).
template <typename F>
void ForEachScene(F fn)
{
    SceneNode* node = GameVariable<SceneNode*>(0x006FA7EC);
    if (IsEnd(node) || !node) // (sic: the original reads the node before testing it)
        return;
    while (node) {
        SceneNode* next = IsEnd(node->next) ? nullptr : node->next;
        fn(node->scene);
        node = next;
    }
}

DeviceViewport& TheViewport() { return g_deviceViewport; }
void SetViewport() { Method<long(__stdcall*)(void*, const void*)>(Device(), 0xBC)(Device(), &TheViewport()); }
// The device's render state directly (not the renderer's cached one).
void SetDeviceRenderState(int state, uint32_t value)
{
    Method<long(__stdcall*)(void*, int, uint32_t)>(Device(), 0xE4)(Device(), state, value);
}

void FullScreenViewport()
{
    TheViewport() = {0, 0, reinterpret_cast<uint32_t&>(g_backBufferWidth), reinterpret_cast<uint32_t&>(g_backBufferHeight), 0.0f, 1.0f};
}

// The camera (a game object).
float CameraAngle(void* camera) { return static_cast<Gfx::Camera*>(camera)->GetAdjustedHFOV(); }
const Mth::Vector* CameraPosition(void* camera) { return static_cast<Gfx::Camera*>(camera)->GetPos(); }
const Mth::Matrix* CameraMatrix(void* camera) { return static_cast<Gfx::Camera*>(camera)->GetMatrix(); }

void DrawSceneGroup(Scene* scene, int pass, int viewport)
{
    RenderScene(reinterpret_cast<sScene*>(scene->group), static_cast<uint32_t>(pass), viewport);
}
void SetPass(int pass) { RenderInstances(static_cast<uint32_t>(pass), 0); }
// No world transform (SetWorldTransform's null: the original passes 0).
void ClearWorldTransform() { SetWorldTransform(nullptr); }

void RenderViewport(int index)
{
    const Viewport* view = reinterpret_cast<const Viewport*>(GetActiveViewport(index));
    void* camera = view->camera;
    if (!camera)
        return;
    // Not original - the level editor (game/front/level_editor.h): while it runs, the first viewport's camera is put
    // where the editor flies.
    // Original behaviour: the camera is drawn where the game put it.
    Front::LevelEditor::PlaceCamera(static_cast<Gfx::Camera*>(camera));
    // Not original - the automation channel's camera lock (game/front/test_view.h): viewport 0's camera held at a pose.
    // Original behaviour: the camera is drawn where the game put it.
    Front::TestView::PlaceCamera(static_cast<Gfx::Camera*>(camera), index);
    GameVariable<int>(0x006FA7B8) = index; // the viewport being drawn
    double width = g_backBufferWidth;
    double height = static_cast<float>(g_backBufferHeight);
    DeviceViewport& vp = TheViewport();
    vp.x = static_cast<int>(width * view->x);
    vp.y = static_cast<int>(height * view->y);
    vp.width = static_cast<int>(width * view->width);
    vp.height = static_cast<int>(height * view->height);
    vp.minZ = 0.0f;
    vp.maxZ = 1.0f;
    if (g_camera.letterboxActive && !FrontEndWidescreen()) {
        // Originally from PartyMod Fixes (its call at 0x004B2E65): the viewport fitted to the aspect wanted. The original squashed the
        // viewport to 3/4 of the screen's height:
        //   int screenHeight = GameVariable<int>(0x00786D70);
        //   vp.y += screenHeight / 8;
        //   vp.height -= screenHeight / 4;
        // (widescreen_menus: in the front end the 3D view keeps the whole screen; the 2D below stays fitted.)
        SetDisplayRegion();
    }
    SetViewport();
    ClearWorldTransform();
    float aspect = reinterpret_cast<CViewport*>(const_cast<Viewport*>(view))->GetAspectRatio();

    // Not original - the level editor's ortho views (game/front/level_editor_camera.h): its camera's field of view
    // narrowed and the near plane brought up for the world's passes (the sky's keeps the near plane).
    // Original behaviour: the camera's own angle, the near plane at 2.
    auto* editorCamera = static_cast<Gfx::Camera*>(camera);
    float editorNear = Front::LevelEditor::NearPlane(static_cast<Gfx::Camera*>(camera));

    // The world: every scene but the sky.
    {
        float angle = Front::LevelEditor::ViewAngle(editorCamera, CameraAngle(camera));
        s_nearZ = editorNear;
        const Mth::Vector* position = CameraPosition(camera);
        SetCamera(CameraMatrix(camera), position, angle, aspect, 1.0f, false);
        // Not original - the level editor: the view's projection noted (its mouse ray and outlines).
        // Original behaviour: nothing.
        Front::LevelEditor::NoteView(static_cast<Gfx::Camera*>(camera), angle, aspect, &vp.x);
        // Not original - the screenshot key's sidecar (game/sys/screenshot.h): the camera noted.
        const Mth::Matrix* frame = CameraMatrix(camera);
        const float at[3] = {position->x, position->y, position->z};
        const float forward[3] = {-frame->row[2].x, -frame->row[2].y, -frame->row[2].z};
        Sys::Screenshot::NoteView(index, at, forward);
    }
    ForEachScene([&](Scene* scene) {
        if (scene->sky)
            return;
        BuildOccluders(CameraPosition(camera), index);
        SetRenderState(1, 1);
        SetRenderState(2, 1);
        MeshGroup* group = scene->group;
        if (!(group->flags & 0x100)) {
            group->flags |= 0x100;
            for (int i = 0; i < group->meshCount; ++i)
                reinterpret_cast<sMesh*>(group->meshes[i])->BuildCollision();
        }
        DrawSceneGroup(scene, 0x55, index);
    });
    SetRenderState(1, 0);
    DrawTextureSplats();
    DrawProjectionBatches();
    SetRenderState(1, 1);
    static_cast<CImposterManager*>(GameVariable<void*>(0x006FA7BC))->Render();
    SetPass(1);
    ClearWorldTransform();

    // The sky: from the origin, far plane at infinity, fog over 20-21 (the fog range put back after).
    {
        Mth::Vector origin = {0.0f, 0.0f, 0.0f, 1.0f};
        float angle = Front::LevelEditor::ViewAngle(editorCamera, CameraAngle(camera));
        s_nearZ = 2.0f;
        SetCamera(CameraMatrix(camera), &origin, angle, aspect, 1.0f, true);
    }
    float& fogStart = g_fogStart;
    float& fogEnd = g_fogEnd;
    float savedFogEnd = fogEnd, savedFogStart = fogStart;
    fogStart = 20.0f;
    fogEnd = 21.0f;
    SetDeviceRenderState(0x24, reinterpret_cast<uint32_t&>(g_fogStart)); // D3DRS_FOGSTART
    SetDeviceRenderState(0x25, reinterpret_cast<uint32_t&>(g_fogEnd)); // D3DRS_FOGEND
    bool fog = g_fogEnabled != 0;
    SetFog(false);
    SetRenderState(1, 0);
    SetRenderState(2, 1);
    ForEachScene([&](Scene* scene) {
        if (!scene->sky)
            return;
        // (with distance clipping and fog both on, there's no sky)
        if (g_distanceClipping && g_fogSetting)
            return;
        uint32_t minFilter = GetSamplerState(0, 6), mipFilter = GetSamplerState(0, 7);
        SetSamplerState(0, 6, 2); // linear
        SetSamplerState(0, 7, 0); // no mipmaps
        DrawSceneGroup(scene, 3, index);
        SetSamplerState(0, 7, mipFilter);
        SetSamplerState(0, 6, minFilter);
    });
    SetRenderState(1, 1);
    SetRenderState(1, 0);
    if (g_radeon8500)
        Sun::Render(static_cast<Gfx::Camera*>(camera));
    SetRenderState(1, 1);
    SetRenderState(2, 1);
    fogStart = savedFogStart;
    fogEnd = savedFogEnd;
    SetDeviceRenderState(0x24, reinterpret_cast<uint32_t&>(g_fogStart));
    SetDeviceRenderState(0x25, reinterpret_cast<uint32_t&>(g_fogEnd));
    SetFog(fog);

    // The world again, for the later passes.
    {
        float angle = Front::LevelEditor::ViewAngle(editorCamera, CameraAngle(camera));
        const Mth::Vector* position = CameraPosition(camera);
        s_nearZ = editorNear;
        SetCamera(CameraMatrix(camera), position, angle, aspect, 1.0f, false);
        s_nearZ = 2.0f;
    }
    SetPass(0x82);
    SetRenderState(1, 1);
    SetRenderState(2, 1);
    ForEachScene([&](Scene* scene) {
        if (!scene->sky)
            DrawSceneGroup(scene, 0x46, index);
    });
    SetPass(0x102);
    IssueOcclusionQueries();
    SetRenderState(1, 0);
    static_cast<CFlexibleParticleManager*>(GameVariable<void*>(0x006FA7C0))->RenderSystems(index > 0);
    static_cast<CParticleManager*>(GameVariable<void*>(0x006FA7C4))->RenderParticles();
    {
        // (the weather's slot 3, through the jump 0x004A10A0)
        void* weather = GameVariable<void*>(0x006FA7C8);
        reinterpret_cast<void(__thiscall*)(void*)>((*static_cast<void***>(weather))[3])(weather);
    }
    SetRenderState(1, 1);
    Engine::DrawShatters();
    SetRenderState(1, 0);
    if (!g_radeon8500)
        Sun::Render(static_cast<Gfx::Camera*>(camera));
    SetRenderState(1, 1);
    SetRenderState(2, 1);
    RenderPrimitives();
    SetRenderState(1, 1);
    SetRenderState(2, 1);
    UpdateGlows(false);
    Engine::DrawFlashes(index, 0);
}

} // namespace

// 0x004B2D80
void RenderWorld()
{
    // Not original - the level editor (game/front/level_editor.h): its frame (flying, picking, dragging) - it runs
    // while the game is paused.
    // Original behaviour: nothing.
    Front::LevelEditor::Update();
    ++g_framesRendered; // frames rendered
    if (!GameVariable<bool>(0x00786D04)) { // (nothing while the device is lost)
        g_frameTime = static_cast<int>(Tmr::GetTime());
        SetRenderState(1, 1);
        SetRenderState(2, 1);
        RenderShadowTextures();
        BeginScreenEffects();
        int count = GameVariable<int>(0x00701378);
        // Not original - the wireframe render mode (game/gfx/wireframe.h): the 3D passes in wireframe when asked.
        // Original behaviour: the render mode (0x006FA734) isn't read.
        Wireframe::BeginWorld();
        ViewMode::BeginWorld(); // (not original either: the editor's view modes - game/gfx/view_mode.h)
        for (int i = 0; i < count; ++i)
            RenderViewport(i);
        ViewMode::EndWorld();
        Wireframe::EndWorld();

        // Full-screen effects and the 2D on top.
        FullScreenViewport();
        if (g_camera.letterboxActive) {
            // Originally from PartyMod Fixes (its call at 0x004B33CA): fitted to the aspect wanted. The original:
            //   int screenHeight = GameVariable<int>(0x00786D70);
            //   TheViewport().y = screenHeight / 8;
            //   TheViewport().height = screenHeight - screenHeight / 4;
            SetDisplayRegion();
        }
        SetViewport();
        CopyScreenQuarter();
        DrawScreenBlur();
        bool fog = g_fogEnabled != 0;
        SetFog(false);
        // Originally from PartyMod Fixes (its call at 0x004B341D): the 2D pass (0x004DFB90 in the original), then black bars over what's
        // drawn outside the fitted area.
        Draw2DWithBlackBars();
        // Not original - the level editor: the hovered and selected objects outlined over everything.
        // Original behaviour: nothing more is drawn.
        Front::LevelEditor::Draw();
        // Not original either - the new front end ([features] new_frontend, game/front/new_frontend.h): over
        // everything while it's up in the front end.
        Front::NewFrontend::Draw();
        SetFog(fog);
    }
    FullScreenViewport();
    SetViewport();
}

} // namespace Nx

REPLACE(0x004DB200, static_cast<void (*)(const Mth::Matrix*, const Mth::Vector*, float, float, float, bool)>(Nx::SetCamera));
REPLACE(0x004DF390, Nx::DrawScreenBlur);
REPLACE(0x004B2D80, Nx::RenderWorld);

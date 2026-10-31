// The per-frame 3D render: the camera's matrices and the motion blur pass, with PartyMod's widescreen
// display region and black bars (contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).

#include "game/gfx/draw_frame.h"
#include "game/gfx/d3dx.h"
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
#include "game/gfx/view_cam.h"
#include "game/gfx/draw2d.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_imposters.h"
#include "game/gfx/engine_primitive.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/geom.h"
#include "game/gfx/glow.h"
#include "game/gfx/graphics_options.h"
#include "game/gfx/lens_flare.h"
#include "game/gfx/sub_mesh.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/particle_sets.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/pc_texture_projection.h"
#include "game/gfx/render_state.h"
#include "game/gfx/scene_render.h"
#include "game/gfx/screen.h"
#include "game/gfx/screen_fx.h"
#include "game/gfx/shadow_base.h"
#include "game/gfx/texture_splats.h"
#include "game/gfx/viewport.h"
#include "game/gfx/viewport_list.h"
#include "game/sys/screenshot.h"
#include "game/sys/stopwatch.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/engine.h"
#include "game/obj/particle_component.h"
#include "game/skate/skate_module.h"

namespace Engine3D {

DeviceViewport g_deviceViewport;
RenderCamera g_camera;
ViewShape g_viewShape;

bool g_radeon8500;
int g_framesRendered;
Maths::Mat4 g_worldMatrix;
Maths::Mat4 g_viewMatrix;
Maths::Mat4 g_projectionMatrix;
Maths::Mat4 g_headingMatrix;
int g_frameTime;
int g_screenCopyIndex;
int g_backBufferWidth;
int g_backBufferHeight;
int g_drawCalls;
int g_drawnTriangles;
float g_fogBeginDistance;
float g_fogEnd;

namespace {

// --- the renderer's state (render_state.h) ---------------------------------------------------------------------

// A render state by the renderer's own numbering (1-0x24), cached.
void SetRenderState(int state, int value) { ChangePhase(state, static_cast<uint32_t>(value)); }
// Fog on or off (the request is kept in 0x007004C8; the device's D3DRS_FOGENABLE follows).
void ChangeFog(bool on) { Engine::EnableFog(on); }

// --- the D3D9 device (0x007D69E0) and its objects, through their vtables ----------------------------------------

void* InputPort() { return reinterpret_cast<void*>(g_d3dDevice); }
template <typename F>
F Method(void* object, int offset) { return reinterpret_cast<F>((*reinterpret_cast<void***>(object))[offset / 4]); }

void SetTransform(int type, const Maths::Mat4* m) { Method<long(__stdcall*)(void*, int, const void*)>(InputPort(), 0xB0)(InputPort(), type, m); }
void SetRenderTarget(void* surface) { Method<long(__stdcall*)(void*, int, void*)>(InputPort(), 0x94)(InputPort(), 0, surface); }
void SetDepthStencilSurface(void* surface) { Method<long(__stdcall*)(void*, void*)>(InputPort(), 0x9C)(InputPort(), surface); }
void SetTextureStageState(int stage, int type, int value)
{
    Method<long(__stdcall*)(void*, int, int, int)>(InputPort(), 0x10C)(InputPort(), stage, type, value);
}
uint32_t GetSamplerState(int sampler, int type)
{
    uint32_t value;
    Method<long(__stdcall*)(void*, int, int, uint32_t*)>(InputPort(), 0x110)(InputPort(), sampler, type, &value);
    return value;
}
void SetSamplerState(int sampler, int type, uint32_t value)
{
    Method<long(__stdcall*)(void*, int, int, uint32_t)>(InputPort(), 0x114)(InputPort(), sampler, type, value);
}
void ClearTarget(uint32_t color)
{
    Method<long(__stdcall*)(void*, uint32_t, const void*, uint32_t, uint32_t, float, uint32_t)>(InputPort(), 0xAC)(
        InputPort(), 0, nullptr, 1 /* D3DCLEAR_TARGET */, color, 1.0f, 0);
}
void DrawPrimitiveUP(int type, unsigned int count, const void* vertices, unsigned int stride)
{
    Method<long(__stdcall*)(void*, int, unsigned int, const void*, unsigned int)>(InputPort(), 0x14C)(InputPort(), type, count,
                                                                                               vertices, stride);
}

// --- D3DX (linked library code; stdcall) ----------------------------------------------------------------------

void MatrixLookAt(Maths::Mat4* out, const float* eye, const float* at, const float* up)
{
    D3dx::MatrixLookAtRH(out, eye, at, up);
}
void MatrixPerspective(Maths::Mat4* out, float width, float height, float nearZ, float farZ)
{
    D3dx::MatrixPerspectiveRH(out, width, height, nearZ, farZ);
}
void MatrixScaling(Maths::Mat4* out, float x, float y, float z)
{
    D3dx::MatrixScaling(out, x, y, z);
}
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}
void MatrixRotationY(Maths::Mat4* out, float angle)
{
    D3dx::MatrixRotationY(out, angle);
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
    auto width = static_cast<uint32_t>(g_displayFactorX * 640.0f);
    auto height = static_cast<uint32_t>(g_displayFactorY * 480.0f);
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
        auto width = static_cast<uint32_t>(std::round(static_cast<double>(g_displayFactorX * 640.0f)));
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
        auto height = static_cast<uint32_t>(std::round(static_cast<double>(g_displayFactorY * 480.0f)));
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
// editor's camera while DrawViewport draws the world with it (2, the original's, otherwise).
static float s_nearZ = 2.0f;

// 0x004DB200
void ChooseCamera(const Maths::Mat4* orientation, const Maths::Vec4* position, float angle, float aspect, float zoom,
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
    world = Maths::Mat4{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    MatrixLookAt(&view, camera.position, target, camera.up);

    // (not original: s_nearZ - the original: camera.nearZ = 2.0f;)
    camera.nearZ = s_nearZ;
    // Originally from PartyMod Fixes (its float at 0x004DB35D): the far plane 96000 - further, for large levels' backgrounds (e.g.
    // Skatopia). The original: camera.farZ = 32000.0f;
    camera.farZ = 96000.0f;
    camera.angle = angle;
    if (g_distanceClipping) // distance clipping (setting 0x007D6444: 100-595)
        camera.farZ = static_cast<float>(static_cast<double>(static_cast<uint32_t>(Gfx2D::g_drawDistanceSetting)) * 18.0);
    // NOT ORIGINAL - [Graphics] draw_distance_scale (game/gfx/graphics_options.h): the far plane (and with it the
    // distance clipping's cull distance, 0x00786CAC) further out.
    camera.farZ = GraphicsOptions::Scaled(camera.farZ, GraphicsOptions::Pull().drawDistanceScale);

    // (degrees to half the angle in radians: the tangent takes the product unrounded, later uses a float)
    double halfAngle = static_cast<double>(angle) * 0.008726646f;
    float width = TanTimes(halfAngle, 4.0f);
    float heightFactor = static_cast<float>(1.0 / (static_cast<double>(aspect) * 0.90909094f));
    float height = heightFactor * width;
    MatrixPerspective(&projection, width, height, camera.nearZ, camera.farZ);
    if (zoom != 1.0f) {
        Maths::Mat4 scale;
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
    g_billboardPool.SetupAxes();
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
    ChangeFog(false);
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
    void* const* textures = reinterpret_cast<void* const*>(g_screenTextures);
    void* texture = textures[current];
    void* other = textures[(current + 1) % 2];
    uint32_t desc[8]; // D3DSURFACE_DESC
    Method<long(__stdcall*)(void*, unsigned int, uint32_t*)>(texture, 0x44)(texture, 0, desc); // GetLevelDesc
    uint32_t texelsWide = desc[6], texelsHigh = desc[7];

    // The quad covers the screen; the texture is drawn shrunk towards its centre by the blur amount.
    uint32_t amount = g_screenBlur;
    float inset = static_cast<float>((1.0 - static_cast<double>(amount) * (1.0f / 255)) * 0.5);
    double insetU = inset / static_cast<double>(texelsWide); // (the u's are rounded from this, unrounded)
    float insetV = static_cast<float>(inset / static_cast<double>(texelsHigh));
    float screenWidth = static_cast<float>(g_presentParameters.BackBufferWidth);
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
    //   float bar = static_cast<float>(g_backBufferHeight / 8);
    //   quad[0].y = quad[1].y = bar;
    //   quad[0].v = quad[1].v = insetV + bar;   (sic: a pixel count added to a texture coordinate)
    //   quad[2].y -= bar; quad[3].y -= bar; quad[2].v -= bar; quad[3].v -= bar;
    if (amount) {
        ChooseBlendMode(g_screenBlurFrames > 2 ? 5 : 0);
        color = amount << 24 | 0x808080;
        for (ScreenVertex& v : quad)
            v.color = color;
    } else {
        ChooseBlendMode(0);
    }

    // Draw into `texture`: the other texture (if the blur level, 0x0078732C, is above 2)...
    void* surface = nullptr;
    Method<long(__stdcall*)(void*, unsigned int, void**)>(texture, 0x48)(texture, 0, &surface); // GetSurfaceLevel
    void* depth = reinterpret_cast<void*>(g_zBufferSurface);
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
    if (g_screenBlurFrames > 2) {
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
    SetRenderTarget(reinterpret_cast<void*>(g_backBuffer));
    SetDepthStencilSurface(reinterpret_cast<void*>(g_backDepth));
    bool fogColour = g_fogSetting && g_distanceClipping;
    ClearTarget(fogColour ? g_fogColour : g_camera.backgroundColour);
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
    ChangeFog(true);
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
    SceneNode* node = reinterpret_cast<SceneNode*>(g_sceneList.next);
    if (IsEnd(node) || !node) // (sic: the original reads the node before testing it)
        return;
    while (node) {
        SceneNode* next = IsEnd(node->next) ? nullptr : node->next;
        fn(node->scene);
        node = next;
    }
}

DeviceViewport& TheViewport() { return g_deviceViewport; }
void SetViewport() { Method<long(__stdcall*)(void*, const void*)>(InputPort(), 0xBC)(InputPort(), &TheViewport()); }
// The device's render state directly (not the renderer's cached one).
void SetDeviceRenderState(int state, uint32_t value)
{
    Method<long(__stdcall*)(void*, int, uint32_t)>(InputPort(), 0xE4)(InputPort(), state, value);
}

void FullScreenViewport()
{
    TheViewport() = {0, 0, reinterpret_cast<uint32_t&>(g_backBufferWidth), reinterpret_cast<uint32_t&>(g_backBufferHeight), 0.0f, 1.0f};
}

// The camera (a game object).
float CameraAngle(void* camera) { return static_cast<Gfx2D::ViewCam*>(camera)->FetchEffectiveHFov(); }
const Maths::Vec4* ViewerLocation(void* camera) { return static_cast<Gfx2D::ViewCam*>(camera)->FetchLocation(); }
const Maths::Mat4* CamOrientation(void* camera) { return static_cast<Gfx2D::ViewCam*>(camera)->FetchMat(); }

void DrawSceneGroup(Scene* scene, int pass, int viewport)
{
    DrawScene(reinterpret_cast<sceneRef*>(scene->group), static_cast<uint32_t>(pass), viewport);
}
void SetPass(int pass) { DrawInstanceBatch(static_cast<uint32_t>(pass), 0); }
// No world transform (SetWorldTransform's null: the original passes 0).
void ClearWorldTransform() { SetWorldTransform(nullptr); }

void DrawViewport(int index)
{
    const Viewport* view = reinterpret_cast<const Viewport*>(FetchCurrentView(index));
    void* camera = view->camera;
    if (!camera)
        return;
    // Not original - the level editor (game/front/level_editor.h): while it runs, the first viewport's camera is put
    // where the editor flies.
    // Original behaviour: the camera is drawn where the game put it.
    MenuSystem::LevelEditor::PlaceCamera(static_cast<Gfx2D::ViewCam*>(camera));
    // Not original - the automation channel's camera lock (game/front/test_view.h): viewport 0's camera held at a pose.
    // Original behaviour: the camera is drawn where the game put it.
    MenuSystem::TestView::PlaceCamera(static_cast<Gfx2D::ViewCam*>(camera), index);
    g_drawnViewport = index; // the viewport being drawn
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
        //   int screenHeight = g_backBufferHeight;
        //   vp.y += screenHeight / 8;
        //   vp.height -= screenHeight / 4;
        // (widescreen_menus: in the front end the 3D view keeps the whole screen; the 2D below stays fitted.)
        SetDisplayRegion();
    }
    SetViewport();
    ClearWorldTransform();
    float aspect = reinterpret_cast<CViewRegion*>(const_cast<Viewport*>(view))->FetchWidthOverHeight();

    // Not original - the level editor's ortho views (game/front/level_editor_camera.h): its camera's field of view
    // narrowed and the near plane brought up for the world's passes (the sky's keeps the near plane).
    // Original behaviour: the camera's own angle, the near plane at 2.
    auto* editorCamera = static_cast<Gfx2D::ViewCam*>(camera);
    float editorNear = MenuSystem::LevelEditor::NearPlane(static_cast<Gfx2D::ViewCam*>(camera));

    // The world: every scene but the sky.
    {
        float angle = MenuSystem::LevelEditor::AdjustViewAngle(editorCamera, CameraAngle(camera));
        s_nearZ = editorNear;
        const Maths::Vec4* position = ViewerLocation(camera);
        ChooseCamera(CamOrientation(camera), position, angle, aspect, 1.0f, false);
        // Not original - the level editor: the view's projection noted (its mouse ray and outlines).
        // Original behaviour: nothing.
        MenuSystem::LevelEditor::NoteView(static_cast<Gfx2D::ViewCam*>(camera), angle, aspect, &vp.x);
        // Not original - the screenshot key's sidecar (game/sys/screenshot.h): the camera noted.
        const Maths::Mat4* frame = CamOrientation(camera);
        const float at[3] = {position->x, position->y, position->z};
        const float forward[3] = {-frame->row[2].x, -frame->row[2].y, -frame->row[2].z};
        SystemCore::Screenshot::NoteView(index, at, forward);
    }
    ForEachScene([&](Scene* scene) {
        if (scene->sky)
            return;
        CollectOccluders(ViewerLocation(camera), index);
        SetRenderState(1, 1);
        SetRenderState(2, 1);
        MeshGroup* group = scene->group;
        if (!(group->flags & 0x100)) {
            group->flags |= 0x100;
            for (int i = 0; i < group->meshCount; ++i)
                reinterpret_cast<meshRef*>(group->meshes[i])->BuildCollision();
        }
        DrawSceneGroup(scene, 0x55, index);
    });
    SetRenderState(1, 0);
    DrawTextureSplats();
    DrawProjectionBatches();
    SetRenderState(1, 1);
    static_cast<CImposterManager*>(g_imposterManager)->Render();
    SetPass(1);
    ClearWorldTransform();

    // The sky: from the origin, far plane at infinity, fog over 20-21 (the fog range put back after).
    {
        Maths::Vec4 origin = {0.0f, 0.0f, 0.0f, 1.0f};
        float angle = MenuSystem::LevelEditor::AdjustViewAngle(editorCamera, CameraAngle(camera));
        s_nearZ = 2.0f;
        ChooseCamera(CamOrientation(camera), &origin, angle, aspect, 1.0f, true);
    }
    float& fogStart = g_fogBeginDistance;
    float& fogEnd = g_fogEnd;
    float savedFogEnd = fogEnd, savedFogStart = fogStart;
    fogStart = 20.0f;
    fogEnd = 21.0f;
    SetDeviceRenderState(0x24, reinterpret_cast<uint32_t&>(g_fogBeginDistance)); // D3DRS_FOGSTART
    SetDeviceRenderState(0x25, reinterpret_cast<uint32_t&>(g_fogEnd)); // D3DRS_FOGEND
    bool fog = g_fogActive != 0;
    ChangeFog(false);
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
        Sun::Render(static_cast<Gfx2D::ViewCam*>(camera));
    SetRenderState(1, 1);
    SetRenderState(2, 1);
    fogStart = savedFogStart;
    fogEnd = savedFogEnd;
    SetDeviceRenderState(0x24, reinterpret_cast<uint32_t&>(g_fogBeginDistance));
    SetDeviceRenderState(0x25, reinterpret_cast<uint32_t&>(g_fogEnd));
    ChangeFog(fog);

    // The world again, for the later passes.
    {
        float angle = MenuSystem::LevelEditor::AdjustViewAngle(editorCamera, CameraAngle(camera));
        const Maths::Vec4* position = ViewerLocation(camera);
        s_nearZ = editorNear;
        ChooseCamera(CamOrientation(camera), position, angle, aspect, 1.0f, false);
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
    static_cast<CFlexibleParticleManager*>(g_flexibleParticleManager)->RenderSystems(index > 0);
    static_cast<CEffectsDirector*>(reinterpret_cast<void*>(GameObjects::g_particleHub))->DrawParticles();
    {
        // (the weather's slot 3, through the jump 0x004A10A0)
        void* weather = g_weather;
        reinterpret_cast<void(__thiscall*)(void*)>((*static_cast<void***>(weather))[3])(weather);
    }
    SetRenderState(1, 1);
    Engine::DrawShatters();
    SetRenderState(1, 0);
    if (!g_radeon8500)
        Sun::Render(static_cast<Gfx2D::ViewCam*>(camera));
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
void DrawWorld()
{
    // Not original - the level editor (game/front/level_editor.h): its frame (flying, picking, dragging) - it runs
    // while the game is paused.
    // Original behaviour: nothing.
    MenuSystem::LevelEditor::TickFrame();
    ++g_framesRendered; // frames rendered
    if (!g_loadingScreenUp) { // (nothing while the device is lost)
        g_frameTime = static_cast<int>(FrameClock::GetTime());
        SetRenderState(1, 1);
        SetRenderState(2, 1);
        RenderShadowTextures();
        BeginScreenEffects();
        int count = GameModules::g_viewportCount;
        // Not original - the wireframe render mode (game/gfx/wireframe.h): the 3D passes in wireframe when asked.
        // Original behaviour: the render mode (0x006FA734) isn't read.
        Wireframe::BeginWorld();
        ViewMode::BeginWorld(); // (not original either: the editor's view modes - game/gfx/view_mode.h)
        for (int i = 0; i < count; ++i)
            DrawViewport(i);
        ViewMode::EndWorld();
        Wireframe::EndWorld();

        // Full-screen effects and the 2D on top.
        FullScreenViewport();
        if (g_camera.letterboxActive) {
            // Originally from PartyMod Fixes (its call at 0x004B33CA): fitted to the aspect wanted. The original:
            //   int screenHeight = g_backBufferHeight;
            //   TheViewport().y = screenHeight / 8;
            //   TheViewport().height = screenHeight - screenHeight / 4;
            SetDisplayRegion();
        }
        SetViewport();
        CopyScreenQuarter();
        DrawScreenBlur();
        bool fog = g_fogActive != 0;
        ChangeFog(false);
        // Originally from PartyMod Fixes (its call at 0x004B341D): the 2D pass (0x004DFB90 in the original), then black bars over what's
        // drawn outside the fitted area.
        Draw2DWithBlackBars();
        // Not original - the level editor: the hovered and selected objects outlined over everything.
        // Original behaviour: nothing more is drawn.
        MenuSystem::LevelEditor::Blit();
        // Not original either - the new front end ([features] new_frontend, game/front/new_frontend.h): over
        // everything while it's up in the front end.
        MenuSystem::NewFrontend::Blit();
        ChangeFog(fog);
    }
    FullScreenViewport();
    SetViewport();
}

} // namespace Nx

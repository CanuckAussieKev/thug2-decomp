// The per-frame 3D render: the camera's matrices and the motion blur pass, with PartyMod's widescreen
// display region and black bars (contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).

#include "engine/gfx/draw_frame.h"
#include "engine/gfx/device_d3d.h"
#include "gpu/d3d9_handles.h"
#include "platform/d3dx.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/view_mode.h"
#include "engine/gfx/wireframe.h"

#include <cstddef>
#include <bit>
#include <cmath>
#include <cstdint>
#include <array>

#include "engine/ext/extension.h"
#include "engine/memory/arena_alloc.h"
#include "base/maths/mat4.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/draw2d.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_imposters.h"
#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/glow.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/lens_flare.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/pc_texture_projection.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/scene_render.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/screen_fx.h"
#include "engine/gfx/shadow_base.h"
#include "engine/gfx/texture_splats.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/sys/stopwatch.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine.h"
#include "engine/objects/particle_component.h"
#include "engine/gfx/weather.h"
#include "base/core/checked_index.h"
#include "platform/d3d_matrix.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/d3d9_state.h"
#include "engine/gfx/effects_state.h"
#include "engine/gfx/view_setup.h"
#include "engine/gfx/engine_parts.h"
#include "base/maths/vec4.h"
#include "engine/gfx/graphics_options_d3d.h"

namespace render {

constexpr float kHalfPixel = 0.5F; // the bars' vertices sit on the pixel edges
constexpr int kTriangleFan = 6;    // D3DPT_TRIANGLEFAN
constexpr int kTriangleStrip = 5;  // D3DPT_TRIANGLESTRIP
constexpr float kDefaultNearZ = 2.0F;
constexpr float kFarZ = 96000.0F;
constexpr double kDrawDistanceUnit = 18.0; // the far plane for each step of the draw distance setting
constexpr float kNearPlaneDistance = 4.0F;
constexpr float kAspectScale = 0.90909094F; // 1 / 1.1
constexpr float kInfiniteDepthScale = 0.999999F;
constexpr float kInfiniteDepthOffset = 1e-6F;
constexpr int kTransformWorld = 0x100; // D3DTS_WORLD
// The renderer's states (RenderStateId) the blur sets.
constexpr RenderStateId kDepthBiasState = RenderStateId::kDepthBias;
constexpr RenderStateId kLightingState = RenderStateId::kLighting;
constexpr RenderStateId kCullModeState = RenderStateId::kCullMode;
constexpr RenderStateId kAlphaTestState = RenderStateId::kAlphaTest;
constexpr RenderStateId kAlphaBlendState = RenderStateId::kAlphaBlend;
// D3DSAMPLERSTATETYPE
constexpr int kSamplerMagFilter = 5;
constexpr int kSamplerMinFilter = 6;
constexpr int kSamplerMipFilter = 7;
constexpr int kColourByteMax = 255;
constexpr double kHalfFactor = 0.5;
constexpr uint32_t kNeutralColour = 0x80808080;
constexpr int kAlphaShift = 24;
constexpr int kNeutralRgb = 0x808080;
constexpr int kSceneCollisionBuiltFlag = 0x100;
constexpr int kMainScenePasses = 0x55;
constexpr int kSkyFreeScenePasses = 0x46;
constexpr float kNearFogBegin = 20.0F; // fog pushed in so it hides nothing while the blur is drawn
constexpr float kNearFogEnd = 21.0F;
constexpr int kFogStartState = 0x24; // D3DRS_FOGSTART
constexpr int kFogEndState = 0x25;   // D3DRS_FOGEND
constexpr int kOpaquePass = 0x82;
constexpr int kOcclusionPass = 0x102;

namespace {

// --- the renderer's state (render_state.h) ---------------------------------------------------------------------

// A render state by the renderer's own numbering (1-0x24), cached.
void set_render_state(RenderStateId state, int value) { change_phase(state, static_cast<uint32_t>(value)); }
// Fog on or off (the request is kept in 0x007004C8; the device's D3DRS_FOGENABLE follows).
void change_fog(bool on) { engine::enable_fog(on); }

// --- the D3D9 device (0x007D69E0) and its objects, through their vtables ----------------------------------------

void set_transform(int type, const maths::Mat4* matrix)
{
    static_assert(sizeof(maths::Mat4) == sizeof(D3DMATRIX));
    d3d9::state().d3d_device()->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(type), d3d::matrix(matrix->data()));
}
void set_render_target(void* surface) { d3d9::state().d3d_device()->SetRenderTarget(0, static_cast<IDirect3DSurface9*>(surface)); }
void set_depth_stencil_surface(void* surface)
{
    d3d9::state().d3d_device()->SetDepthStencilSurface(static_cast<IDirect3DSurface9*>(surface));
}
void set_texture_stage_state(int stage, int type, int value)
{
    d3d9::state().d3d_device()->SetTextureStageState(static_cast<DWORD>(stage), static_cast<D3DTEXTURESTAGESTATETYPE>(type),
                                       static_cast<DWORD>(value));
}
uint32_t get_sampler_state(int sampler, int type)
{
    DWORD value = 0;
    d3d9::state().d3d_device()->GetSamplerState(static_cast<DWORD>(sampler), static_cast<D3DSAMPLERSTATETYPE>(type), &value);
    return value;
}
void set_sampler_state(int sampler, int type, uint32_t value)
{
    d3d9::state().d3d_device()->SetSamplerState(static_cast<DWORD>(sampler), static_cast<D3DSAMPLERSTATETYPE>(type), value);
}
void clear_target(uint32_t color) { d3d9::state().d3d_device()->Clear(0, nullptr, D3DCLEAR_TARGET, color, 1.0F, 0); }
void draw_primitive_up(int type, unsigned int count, const void* vertices, unsigned int stride)
{
    d3d9::state().d3d_device()->DrawPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(type), count, vertices, stride);
}

// --- D3DX (linked library code; stdcall) ----------------------------------------------------------------------

void matrix_look_at(maths::Mat4* out, const float* eye, const float* at, const float* up)
{
    d3dx::matrix_look_at_rh(out, eye, at, up);
}
void matrix_perspective(maths::Mat4* out, float width, float height, float near_z, float far_z)
{
    d3dx::matrix_perspective_rh(out, width, height, near_z, far_z);
}
void matrix_scaling(maths::Mat4* out, float x, float y, float z) { d3dx::matrix_scaling(out, x, y, z); }
void matrix_rotation_y(maths::Mat4* out, float angle) { d3dx::matrix_rotation_y(out, angle); }

// atan2(y, x) - pi/2 (the pi/2 a float), rounded once to float.
float atan2_less_quarter_turn(float y, float x)
{
    constexpr float kQuarterTurn = 1.5707963705062866F;
    return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)) - kQuarterTurn);
}

constexpr float float_from_bits(uint32_t bits) { return std::bit_cast<float>(bits); }

} // namespace

// --- PartyMod's display region and black bars ---------------------------------------------------------------

void set_display_region()
{
    auto& gfx = renderer();
    // the 640x480 layout's size on the back buffer (set_letterbox), centred
    auto width = static_cast<uint32_t>(renderer().display_factor_x() * kLayoutWidth);
    auto height = static_cast<uint32_t>(renderer().display_factor_y() * kLayoutHeight);
    // (the device viewport, 0x00786C90: x, y, width, height)
    DeviceViewport viewport = gfx.device_viewport();
    viewport.x = (static_cast<uint32_t>(gfx.back_buffer_width()) - width) / 2;
    viewport.width = width;
    viewport.y = (static_cast<uint32_t>(gfx.back_buffer_height()) - height) / 2;
    viewport.height = height;
    gfx.set_device_viewport(viewport);
}

void draw_2d_with_black_bars()
{
    auto& gfx = renderer();
    struct BarVertex {
        float x, y, z, w;
        uint32_t color;
        float u, v;
    };
    draw_2d(); // the 2D pass
    // (widescreen_menus: in the front end the 3D scene fills the sides, so no bars over it.)
    if (get_front_end_widescreen()) {
        return;
    }

    const auto back_buffer_width = static_cast<uint32_t>(gfx.back_buffer_width());
    const auto back_buffer_height = static_cast<uint32_t>(gfx.back_buffer_height());
    const float back_buffer_aspect = static_cast<float>(back_buffer_width) / static_cast<float>(back_buffer_height);
    constexpr uint32_t kBlack = 0xFF000000;
    if (back_buffer_aspect > desired_aspect_ratio()) {
        // bars left and right
        auto width = static_cast<uint32_t>(std::round(static_cast<double>(renderer().display_factor_x() * kLayoutWidth)));
        const uint32_t bar_width = (back_buffer_width - width) / 2;
        set_texture(0, nullptr);
        std::array<BarVertex, 4> bar = {{
            {.x = -kHalfPixel, .y = -kHalfPixel, .z = 0.0F, .w = 1.0F, .color = kBlack, .u = 0.0F, .v = 0.0F},
            {.x = -kHalfPixel,
             .y = static_cast<float>(back_buffer_height) + kHalfPixel,
             .z = 0.0F,
             .w = 1.0F,
             .color = kBlack,
             .u = 0.0F,
             .v = 0.0F},
            {.x = static_cast<float>(bar_width) + kHalfPixel,
             .y = static_cast<float>(back_buffer_height) + kHalfPixel,
             .z = 0.0F,
             .w = 1.0F,
             .color = kBlack,
             .u = 0.0F,
             .v = 0.0F},
            {.x = static_cast<float>(bar_width) + kHalfPixel,
             .y = -kHalfPixel,
             .z = 0.0F,
             .w = 1.0F,
             .color = kBlack,
             .u = 0.0F,
             .v = 0.0F},
        }};
        draw_primitive_up(kTriangleFan /* D3DPT_TRIANGLEFAN */, 2, bar.data(), sizeof(BarVertex));
        for (BarVertex& corner : bar) {
            corner.x += static_cast<float>(width + bar_width);
        }
        draw_primitive_up(kTriangleFan, 2, bar.data(), sizeof(BarVertex));
    } else if (back_buffer_aspect < desired_aspect_ratio()) {
        // bars top and bottom
        auto height = static_cast<uint32_t>(std::round(static_cast<double>(renderer().display_factor_y() * kLayoutHeight)));
        const uint32_t bar_height = (back_buffer_height - height) / 2;
        set_texture(0, nullptr);
        std::array<BarVertex, 4> bar = {{
            {.x = -kHalfPixel, .y = -kHalfPixel, .z = 0.0F, .w = 1.0F, .color = kBlack, .u = 0.0F, .v = 0.0F},
            {.x = -kHalfPixel,
             .y = static_cast<float>(bar_height) + kHalfPixel,
             .z = 0.0F,
             .w = 1.0F,
             .color = kBlack,
             .u = 0.0F,
             .v = 0.0F},
            {.x = static_cast<float>(back_buffer_width) + kHalfPixel,
             .y = static_cast<float>(bar_height) + kHalfPixel,
             .z = 0.0F,
             .w = 1.0F,
             .color = kBlack,
             .u = 0.0F,
             .v = 0.0F},
            {.x = static_cast<float>(back_buffer_width) + kHalfPixel,
             .y = -kHalfPixel,
             .z = 0.0F,
             .w = 1.0F,
             .color = kBlack,
             .u = 0.0F,
             .v = 0.0F},
        }};
        draw_primitive_up(kTriangleFan, 2, bar.data(), sizeof(BarVertex));
        for (BarVertex& corner : bar) {
            corner.y += static_cast<float>(height + bar_height);
        }
        draw_primitive_up(kTriangleFan, 2, bar.data(), sizeof(BarVertex));
    }
}

namespace {
// Not original - the level editor's ortho views (editor/level_editor_camera.h): the near plane brought up for the
// editor's camera while draw_viewport draws the world with it (2, the original's, otherwise).
float& stored_near_z()
{
    static float near_z{kDefaultNearZ};
    return near_z;
}
} // namespace

// 0x004DB200
void choose_camera(const maths::Mat4* orientation, const maths::Vec4* position, CameraLens lens,
                   bool infinite)
{
    const auto angle = lens.angle;
    const auto aspect = lens.aspect;
    const auto zoom = lens.zoom;
    auto& gfx = renderer();
    // (worked out here, then stored: the renderer's camera, its three matrices and the view's shape)
    RenderCamera camera = gfx.render_camera();
    maths::Mat4 world;
    maths::Mat4 view;
    maths::Mat4 projection;

    core::at(camera.position, 0) = position->x;
    core::at(camera.position, 1) = position->y;
    core::at(camera.position, 2) = position->z;
    core::at(camera.backward, 0) = core::at(orientation->row, 2).x;
    core::at(camera.backward, 1) = core::at(orientation->row, 2).y;
    core::at(camera.backward, 2) = core::at(orientation->row, 2).z;
    core::at(camera.up, 0) = core::at(orientation->row, 1).x;
    core::at(camera.up, 1) = core::at(orientation->row, 1).y;
    core::at(camera.up, 2) = core::at(orientation->row, 1).z;
    std::array<float, 3> target = {{core::at(camera.position, 0) - core::at(camera.backward, 0), core::at(camera.position, 1) - core::at(camera.backward, 1),
                       core::at(camera.position, 2) - core::at(camera.backward, 2)}};
    world = maths::Mat4{{{{.x = 1, .y = 0, .z = 0, .w = 0}, {.x = 0, .y = 1, .z = 0, .w = 0}, {.x = 0, .y = 0, .z = 1, .w = 0}, {.x = 0, .y = 0, .z = 0, .w = 1}}}};
    matrix_look_at(&view, camera.position.data(), target.data(), camera.up.data());

    // (not original: stored_near_z() - the original: camera.nearZ = 2.0f;)
    camera.near_z_ = stored_near_z();
    // Originally from PartyMod Fixes (its float at 0x004DB35D): the far plane 96000 - further, for large levels' backgrounds (e.g.
    // Skatopia). The original: camera.farZ = 32000.0f;
    camera.far_z_ = kFarZ;
    camera.angle = angle;
    if (view_setup().distance_clipping()) { // distance clipping (setting 0x007D6444: 100-595)
        camera.far_z_ =
            static_cast<float>(static_cast<double>(static_cast<uint32_t>(gfx::draw_distance_setting())) * kDrawDistanceUnit);
    }
    // NOT ORIGINAL - [Graphics] draw_distance_scale (engine/gfx/graphics_options.h): the far plane (and with it the
    // distance clipping's cull distance, 0x00786CAC) further out.
    camera.far_z_ = graphics_options::get_scaled(camera.far_z_, graphics_options::get_pull().draw_distance_scale_);

    // (degrees to half the angle in radians: the tangent takes the product unrounded, later uses a float)
    const double half_angle = static_cast<double>(angle) * 0.008726646F;
    auto width = static_cast<float>(std::tan(half_angle) * kNearPlaneDistance);
    auto height_factor = static_cast<float>(1.0 / (static_cast<double>(aspect) * kAspectScale));
    const float height = height_factor * width;
    matrix_perspective(&projection, width, height, camera.near_z_, camera.far_z_);
    if (zoom != 1.0F) {
        maths::Mat4 scale;
        matrix_scaling(&scale, zoom, zoom, 1.0F);
        maths::matrix_multiply(&projection, &projection, &scale);
    }
    camera.near_height_ = height;
    camera.near_width_ = width;
    if (infinite) {
        core::at(projection.row, 2).z = -kInfiniteDepthScale;
        core::at(projection.row, 3).z = kInfiniteDepthOffset;
    }
    gfx.set_render_camera(camera);
    gfx.set_world_matrix(world);
    gfx.set_view_matrix(view);
    gfx.set_projection_matrix(projection);
    set_transform(kTransformWorld, &gfx.world_matrix()); // D3DTS_WORLD
    set_transform(2, &gfx.view_matrix());      // D3DTS_VIEW
    set_transform(3, &gfx.projection_matrix());

    ViewShape shape = gfx.view_shape();
    shape.tan_x_ = std::tan(static_cast<float>(half_angle));
    shape.tan_y_ = -(height_factor * shape.tan_x_);
    const double tan_x2 = static_cast<double>(shape.tan_x_) * shape.tan_x_;
    const double tan_y2 = static_cast<double>(shape.tan_y_) * shape.tan_y_;
    shape.sin_x_ = static_cast<float>(1.0 / std::sqrt((1.0 / tan_x2) + 1.0));
    shape.sin_y_ = static_cast<float>(1.0 / std::sqrt((1.0 / tan_y2) + 1.0));
    shape.cos_x_ = static_cast<float>(1.0 / std::sqrt(tan_x2 + 1.0));
    shape.cos_y_ = static_cast<float>(1.0 / std::sqrt(tan_y2 + 1.0));

    gfx.set_view_shape(shape);

    // The camera's heading: a rotation about the vertical axis (0x00786C50).
    maths::Mat4 heading;
    matrix_rotation_y(&heading, atan2_less_quarter_turn(-core::at(camera.backward, 2), -core::at(camera.backward, 0)));
    gfx.set_heading_matrix(heading);
    // The view frustum (0x00775B80) follows the new view.
    billboard_pool().setup_axes();
}

namespace {

struct ScreenVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};
constexpr std::size_t kScreenVertexBytes = 0x1C;

static_assert(sizeof(ScreenVertex) == kScreenVertexBytes, "the draw's stride");

} // namespace

// 0x004DF390
void draw_screen_blur()
{
    if (!effects_state().screen_copy_in_use()) { // blur available
        return;
    }
    change_fog(false);
    set_render_state(kDepthBiasState, 0);
    set_render_state(kLightingState, 0);
    set_render_state(kCullModeState, 1);
    // Linear filtering on all four samplers (put back at the end).
    std::array<uint32_t, 4> min_filter{};
    std::array<uint32_t, 4> mag_filter{};
    for (int i = 0; i < 4; ++i) {
        core::at(min_filter, i) = get_sampler_state(i, kSamplerMinFilter); // D3DSAMP_MINFILTER
        if (core::at(min_filter, i) != 2) {                                // D3DTEXF_LINEAR
            set_sampler_state(i, kSamplerMinFilter, 2);
        }
        core::at(mag_filter, i) = get_sampler_state(i, kSamplerMagFilter); // D3DSAMP_MAGFILTER
        if (core::at(mag_filter, i) != 2) {
            set_sampler_state(i, kSamplerMagFilter, 2);
        }
    }
    if (effects_state().bloom_on() != 0U) {
        get_bloom();
    }
    set_render_state(RenderStateId::kAddressMode0, kClampBothWays);
    set_render_state(RenderStateId::kZWrite, 0);
    set_render_state(RenderStateId::kZTest, 0);
    set_render_state(kAlphaTestState, 0);
    set_render_state(kAlphaBlendState, 1);

    // Two textures take turns: the one just drawn into (the last frame's blur) and the other.
    const int current = renderer().screen_copy_index();
    IDirect3DTexture9* texture = core::at(d3d9::state().screen_textures(), current);
    IDirect3DTexture9* other = core::at(d3d9::state().screen_textures(), (current + 1) % 2);
    D3DSURFACE_DESC desc;
    texture->GetLevelDesc(0, &desc);
    const uint32_t texels_wide = desc.Width;
    const uint32_t texels_high = desc.Height;

    // The quad covers the screen; the texture is drawn shrunk towards its centre by the blur amount.
    const uint32_t amount = effects_state().screen_blur();
    auto inset = static_cast<float>((1.0 - (static_cast<double>(amount) * (1.0F / kColourByteMax))) * kHalfFactor);
    const double inset_u = inset / static_cast<double>(texels_wide); // (the u's are rounded from this, unrounded)
    auto inset_v = static_cast<float>(inset / static_cast<double>(texels_high));
    auto screen_width = static_cast<float>(present_parameters().BackBufferWidth);
    auto screen_height = static_cast<float>(present_parameters().BackBufferHeight);
    uint32_t color = kNeutralColour;
    std::array<ScreenVertex, 4> quad = {{
        {.x = 0, .y = 0, .z = 0, .w = 1, .color = color, .u = static_cast<float>(inset_u), .v = inset_v},
        {.x = screen_width,
         .y = 0,
         .z = 0,
         .w = 1,
         .color = color,
         .u = static_cast<float>(1.0 - inset_u),
         .v = inset_v},
        {.x = 0,
         .y = screen_height,
         .z = 0,
         .w = 1,
         .color = color,
         .u = static_cast<float>(inset_u),
         .v = 1.0F - inset_v},
        {.x = screen_width,
         .y = screen_height,
         .z = 0,
         .w = 1,
         .color = color,
         .u = static_cast<float>(1.0 - inset_u),
         .v = 1.0F - inset_v},
    }};
    // Originally from PartyMod Fixes (its JMP at 0x004DF63A): no letterbox adjustment - PartyMod's letterbox is a viewport. The
    // original, when letterboxed (0x00786CBE), moved the quad in by an eighth of the back buffer's height:
    //   float bar = static_cast<float>(renderer().back_buffer_height() / 8);
    //   quad[0].y = quad[1].y = bar;
    //   quad[0].v = quad[1].v = inset_v + bar;   (sic: a pixel count added to a texture coordinate)
    //   quad[2].y -= bar; quad[3].y -= bar; quad[2].v -= bar; quad[3].v -= bar;
    if (amount != 0U) {
        choose_blend_mode(effects_state().screen_blur_frames() > 2 ? kBlendModeAlpha : 0);
        color = amount << kAlphaShift | kNeutralRgb;
        for (ScreenVertex& v : quad) {
            v.color = color;
        }
    } else {
        choose_blend_mode(0);
    }

    // Draw into `texture`: the other texture (if the blur level, 0x0078732C, is above 2)...
    IDirect3DSurface9* surface = nullptr;
    texture->GetSurfaceLevel(0, &surface);
    // (NOT ORIGINAL: or the multisampled scene surface - [Graphics] msaa, engine/gfx/graphics_options.h)
    if (!graphics_options::set_scene_target(d3d9::handle(texture))) {
        set_render_target(surface);
        set_depth_stencil_surface(d3d9::state().z_buffer_surface());
    }
    surface->Release();
    set_vertex_format(kScreenVertexFormat);                                          // XYZRHW | DIFFUSE | TEX1
    set_pixel_shader(nullptr, nullptr);                                // (no pixel shader)
    set_texture_stage_state(0, 1, 2);                                  // colour op: select arg 1
    set_texture_stage_state(0, 2, 2);                                  // colour arg 1: texture
    set_texture_stage_state(0, 4, 1);                                  // alpha op: disable
    if (effects_state().screen_blur_frames() > 2) {
        set_render_state(kAlphaBlendState, 1);
        set_texture(0, d3d9::handle(other));
        draw_primitive_up(kTriangleStrip /* triangle strip */, 2, quad.data(), sizeof(ScreenVertex));
        renderer().count_draw(2); // the frame's draw calls and triangles
    }
    // ...then `texture` onto the back buffer, cleared first to the fog colour (with fog and distance clipping
    // on) or the background colour.
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): the multisampled frame resolved into `texture`.
    graphics_options::resolve_scene_target(d3d9::handle(texture));
    set_render_target(d3d9::state().back_buffer());
    set_depth_stencil_surface(d3d9::state().back_depth());
    const bool fogged = view_setup().fog_setting() && view_setup().distance_clipping();
    clear_target(fogged ? engine_parts().fog_colour() : renderer().render_camera().background_colour_);
    set_render_state(kAlphaBlendState, 0);
    set_texture(0, d3d9::handle(texture));
    draw_primitive_up(kTriangleStrip, 2, quad.data(), sizeof(ScreenVertex));
    set_render_state(kAlphaBlendState, 1);
    renderer().count_draw(2);
    for (int i = 0; i < 4; ++i) {
        set_sampler_state(i, kSamplerMinFilter, core::at(min_filter, i));
        set_sampler_state(i, kSamplerMagFilter, core::at(mag_filter, i));
    }
    renderer().set_screen_copy_index((renderer().screen_copy_index() + 1) % 2);
    change_fog(true);
}

namespace {

// The list of loaded scenes (0x006FA7EC): ends at a node whose scene is -1.
// A node of the scene list (memory::ListNode): its scene, and whether it is the list's head.
using SceneNode = memory::ListNode;
PcScene* scene_of(const SceneNode* node) { return static_cast<PcScene*>(node->data()); }
bool is_end(const SceneNode* node) { return node->is_head(); }

// Calls fn(scene) for every scene in the list (the next node is taken before the call).
template <typename F>
void for_each_scene(F fn)
{
    auto* node = (engine_parts().scene_list().next());
    if (is_end(node) || !node) { // (sic: the original reads the node before testing it)
        return;
    }
    while (node) {
        SceneNode* next = is_end(node->next()) ? nullptr : node->next();
        fn(scene_of(node));
        node = next;
    }
}

void set_viewport()
{
    const DeviceViewport& vp = renderer().device_viewport();
    const D3DVIEWPORT9 viewport = {.X = vp.x, .Y = vp.y, .Width = vp.width, .Height = vp.height, .MinZ = vp.min_z_, .MaxZ = vp.max_z_};
    d3d9::state().d3d_device()->SetViewport(&viewport);
}
// The device's render state directly (not the renderer's cached one).
void set_device_render_state(int state, uint32_t value)
{
    d3d9::state().d3d_device()->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state), value);
}

void full_screen_viewport()
{
    auto& gfx = renderer();
    gfx.set_device_viewport({.x = 0,
                             .y = 0,
                             .width = static_cast<uint32_t>(gfx.back_buffer_width()),
                             .height = static_cast<uint32_t>(gfx.back_buffer_height()),
                             .min_z_ = 0.0F,
                             .max_z_ = 1.0F});
}

// The camera (a game object).
float camera_angle(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_effective_h_fov(); }
const maths::Vec4* viewer_location(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_location(); }
const maths::Mat4* cam_orientation(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_mat(); }

void draw_scene_group(PcScene* scene, int pass, int viewport)
{
    draw_scene(scene->meshes(), {.flags = static_cast<uint32_t>(pass), .viewport = viewport});
}
void set_pass(int pass) { draw_instance_batch({.flags = static_cast<uint32_t>(pass), .viewport = 0}); }
// No world transform (set_world_transform's null: the original passes 0).
void clear_world_transform() { set_world_transform(nullptr); }

void draw_viewport(int index)
{
    const ViewRegion* view = fetch_current_view(index);
    void* camera = view->viewer().get_pull();
    if (!camera) {
        return;
    }
    // Not original - the extensions (engine/ext/extension.h; the level editor: while it runs, the first viewport's
    // camera is put where the editor flies).
    // Original behaviour: the camera is drawn where the game put it.
    // (then the automation channel's camera lock - devtools/test_view.h: viewport 0's camera held at a pose)
    ext::place_camera(static_cast<gfx::ViewCam*>(camera), index);
    view_setup().set_drawn_viewport(index); // the viewport being drawn
    const double width = renderer().back_buffer_width();
    const double height = static_cast<float>(renderer().back_buffer_height());
    DeviceViewport vp = renderer().device_viewport();
    vp.x = static_cast<int>(width * view->pos_x());
    vp.y = static_cast<int>(height * view->y());
    vp.width = static_cast<int>(width * view->span());
    vp.height = static_cast<int>(height * view->level_y());
    vp.min_z_ = 0.0F;
    vp.max_z_ = 1.0F;
    renderer().set_device_viewport(vp);
    if ((renderer().render_camera().letterbox_active_ != 0U) && !get_front_end_widescreen()) {
        // Originally from PartyMod Fixes (its call at 0x004B2E65): the viewport fitted to the aspect wanted. The original squashed the
        // viewport to 3/4 of the screen's height:
        //   int screen_height = renderer().back_buffer_height();
        //   vp.y += screen_height / 8;
        //   vp.height -= screen_height / 4;
        // (widescreen_menus: in the front end the 3D view keeps the whole screen; the 2D below stays fitted.)
        set_display_region();
    }
    set_viewport();
    clear_world_transform();
    const float aspect = view->fetch_width_over_height();

    // Not original - the extensions (engine/ext/extension.h; the level editor's ortho views: its camera's field of
    // view narrowed and the near plane brought up for the world's passes - the sky's keeps the near plane).
    // Original behaviour: the camera's own angle, the near plane at 2.
    auto* view_camera = static_cast<gfx::ViewCam*>(camera);
    const float world_near = ext::near_plane(view_camera, kDefaultNearZ);

    // The world: every scene but the sky.
    {
        const float angle = ext::view_angle(view_camera, camera_angle(camera));
        stored_near_z() = world_near;
        const maths::Vec4* position = viewer_location(camera);
        choose_camera(cam_orientation(camera), position, {.angle = angle, .aspect = aspect, .zoom = 1.0F}, false);
        // Not original - the extensions (engine/ext/extension.h): the view's projection noted (the level editor's
        // mouse ray and outlines; the camera for the screenshot key's sidecar).
        // Original behaviour: nothing.
        const DeviceViewport& drawn = renderer().device_viewport();
        const maths::Mat4* frame = cam_orientation(camera);
        const ext::ViewNote note = {
            .camera = view_camera,
            .viewport = index,
            .angle = angle,
            .aspect = aspect,
            .rect = {{drawn.x, drawn.y, drawn.width, drawn.height}},
            .position = {{position->x, position->y, position->z}},
            .forward = {{-core::at(frame->row, 2).x, -core::at(frame->row, 2).y, -core::at(frame->row, 2).z}},
        };
        ext::view_drawn(note);
    }
    for_each_scene([&](PcScene* scene) {
        if (scene->is_sky()) {
            return;
        }
        collect_occluders(viewer_location(camera), index);
        set_render_state(RenderStateId::kZWrite, 1);
        set_render_state(RenderStateId::kZTest, 1);
        SceneRef* group = scene->meshes();
        if (!(group->flags() & kSceneCollisionBuiltFlag)) {
            group->flags() |= kSceneCollisionBuiltFlag;
            for (MeshRef* mesh : group->mesh_run()) {
                mesh->build_collision();
            }
        }
        draw_scene_group(scene, kMainScenePasses, index);
    });
    set_render_state(RenderStateId::kZWrite, 0);
    draw_texture_splats();
    draw_projection_batches();
    set_render_state(RenderStateId::kZWrite, 1);
    imposter_manager()->render();
    set_pass(1);
    clear_world_transform();

    // The sky: from the origin, far plane at infinity, fog over 20-21 (the fog range put back after).
    {
        const maths::Vec4 origin = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        const float angle = ext::view_angle(view_camera, camera_angle(camera));
        stored_near_z() = kDefaultNearZ;
        choose_camera(cam_orientation(camera), &origin, {.angle = angle, .aspect = aspect, .zoom = 1.0F}, true);
    }
    const float saved_fog_end = renderer().fog_end();
    const float saved_fog_start = renderer().fog_begin_distance();
    renderer().set_fog_begin_distance(kNearFogBegin);
    renderer().set_fog_end(kNearFogEnd);
    set_device_render_state(kFogStartState, std::bit_cast<uint32_t>(renderer().fog_begin_distance())); // D3DRS_FOGSTART
    set_device_render_state(kFogEndState, std::bit_cast<uint32_t>(renderer().fog_end()));            // D3DRS_FOGEND
    const bool fog = renderer().fog_active() != 0;
    change_fog(false);
    set_render_state(RenderStateId::kZWrite, 0);
    set_render_state(RenderStateId::kZTest, 1);
    for_each_scene([&](PcScene* scene) {
        if (!scene->is_sky()) {
            return;
        }
        // (with distance clipping and fog both on, there's no sky)
        if (view_setup().distance_clipping() && view_setup().fog_setting()) {
            return;
        }
        const uint32_t min_filter = get_sampler_state(0, 6);
        const uint32_t mip_filter = get_sampler_state(0, 7);
        set_sampler_state(0, kSamplerMinFilter, 2); // linear
        set_sampler_state(0, kSamplerMipFilter, 0); // no mipmaps
        draw_scene_group(scene, 3, index);
        set_sampler_state(0, kSamplerMipFilter, mip_filter);
        set_sampler_state(0, kSamplerMinFilter, min_filter);
    });
    set_render_state(RenderStateId::kZWrite, 1);
    set_render_state(RenderStateId::kZWrite, 0);
    if (renderer().radeon8500()) {
        sun::render(static_cast<gfx::ViewCam*>(camera));
    }
    set_render_state(RenderStateId::kZWrite, 1);
    set_render_state(RenderStateId::kZTest, 1);
    renderer().set_fog_begin_distance(saved_fog_start);
    renderer().set_fog_end(saved_fog_end);
    set_device_render_state(kFogStartState, std::bit_cast<uint32_t>(renderer().fog_begin_distance()));
    set_device_render_state(kFogEndState, std::bit_cast<uint32_t>(renderer().fog_end()));
    change_fog(fog);

    // The world again, for the later passes.
    {
        const float angle = ext::view_angle(view_camera, camera_angle(camera));
        const maths::Vec4* position = viewer_location(camera);
        stored_near_z() = world_near;
        choose_camera(cam_orientation(camera), position, {.angle = angle, .aspect = aspect, .zoom = 1.0F}, false);
        stored_near_z() = kDefaultNearZ;
    }
    set_pass(kOpaquePass);
    set_render_state(RenderStateId::kZWrite, 1);
    set_render_state(RenderStateId::kZTest, 1);
    for_each_scene([&](PcScene* scene) {
        if (!scene->is_sky()) {
            draw_scene_group(scene, kSkyFreeScenePasses, index);
        }
    });
    set_pass(kOcclusionPass);
    issue_occlusion_queries();
    set_render_state(RenderStateId::kZWrite, 0);
    flexible_particle_manager()->render_systems(index > 0);
    objects::particle_hub()->draw_particles();
    weather()->render(); // (through the jump 0x004A10A0)
    set_render_state(RenderStateId::kZWrite, 1);
    engine::draw_shatters();
    set_render_state(RenderStateId::kZWrite, 0);
    if (!renderer().radeon8500()) {
        sun::render(static_cast<gfx::ViewCam*>(camera));
    }
    set_render_state(RenderStateId::kZWrite, 1);
    set_render_state(RenderStateId::kZTest, 1);
    render_primitives();
    set_render_state(RenderStateId::kZWrite, 1);
    set_render_state(RenderStateId::kZTest, 1);
    update_glows(false);
    engine::draw_flashes(index, 0);
}

} // namespace

// 0x004B2D80
void draw_world()
{
    // Not original - the extensions (engine/ext/extension.h; the level editor: its frame - flying, picking,
    // dragging - which runs while the game is paused).
    // Original behaviour: nothing.
    ext::before_draw_world();
    renderer().count_frame(); // frames rendered
    if (!engine_parts().loading_screen_up()) { // (nothing while the device is lost)
        renderer().set_step_seconds(static_cast<int>(frame_clock::get_time()));
        set_render_state(RenderStateId::kZWrite, 1);
        set_render_state(RenderStateId::kZTest, 1);
        render_shadow_textures();
        begin_screen_effects();
        const int count = modules::viewport_count();
        // Not original - the wireframe render mode (engine/gfx/wireframe.h): the 3D passes in wireframe when asked.
        // Original behaviour: the render mode (0x006FA734) isn't read.
        wireframe::begin_world();
        view_mode::begin_world(); // (not original either: the editor's view modes - engine/gfx/view_mode.h)
        for (int i = 0; i < count; ++i) {
            draw_viewport(i);
        }
        view_mode::end_world();
        wireframe::end_world();

        // Full-screen effects and the 2D on top.
        full_screen_viewport();
        if (renderer().render_camera().letterbox_active_ != 0U) {
            // Originally from PartyMod Fixes (its call at 0x004B33CA): fitted to the aspect wanted. The original:
            //   int screen_height = renderer().back_buffer_height();
            //   renderer().device_viewport().y = screen_height / 8;
            //   renderer().device_viewport().height = screen_height - screen_height / 4;
            set_display_region();
        }
        set_viewport();
        copy_screen_quarter();
        draw_screen_blur();
        const bool fog = renderer().fog_active() != 0;
        change_fog(false);
        // Originally from PartyMod Fixes (its call at 0x004B341D): the 2D pass (0x004DFB90 in the original), then black bars over what's
        // drawn outside the fitted area.
        draw_2d_with_black_bars();
        // Not original - the extensions (engine/ext/extension.h; the level editor: the hovered and selected objects
        // outlined over everything; then the new front end - [features] new_frontend - over everything while it's up
        // in the front end).
        // Original behaviour: nothing more is drawn.
        ext::draw_over_frame();
        change_fog(fog);
    }
    full_screen_viewport();
    set_viewport();
}

} // namespace render

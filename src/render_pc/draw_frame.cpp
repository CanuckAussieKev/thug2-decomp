// The per-frame 3D render: the camera's matrices and the motion blur pass, with PartyMod's widescreen
// display region and black bars (contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).

#include "render_pc/draw_frame.h"
#include "base/maths/view_transforms.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/render_camera.h"
#include "render_pc/device.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "render_pc/pc_scene.h"
#include "render_pc/view_mode.h"
#include "engine/gfx/view_modes.h"
#include "render_pc/wireframe.h"

#include <algorithm>
#include <cstddef>
#include <bit>
#include <cmath>
#include <cstdint>
#include <array>

#include "engine/ext/extension.h"
#include "engine/memory/arena_alloc.h"
#include "base/maths/mat4.h"
#include "engine/gfx/view_cam.h"
#include "render_pc/depth_lines_draw.h"
#include "render_pc/ghost_draw.h"
#include "render_pc/outline_draw.h"
#include "render_pc/draw2d.h"
#include "render_pc/game_view_frame.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_imposters.h"
#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "render_pc/geom.h"
#include "render_pc/glow.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/lens_flare.h"
#include "render_pc/sub_mesh.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/particle_sets.h"
#include "render_pc/pc_billboards.h"
#include "render_pc/pc_texture_projection.h"
#include "render_pc/render_state.h"
#include "render_pc/scene_render.h"
#include "render_pc/rt_record.h"
#include "engine/gfx/screen.h"
#include "render_pc/screen_fx.h"
#include "render_pc/shadow_base.h"
#include "render_pc/texture_splats.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/sys/stopwatch.h"
#include "engine/gfx/engine_glue.h"
#include "render_pc/pc_engine.h"
#include "engine/gfx/engine.h"
#include "engine/objects/particle_component.h"
#include "engine/gfx/weather.h"
#include "base/core/checked_index.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/effects_state.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/view_setup.h"
#include "engine/gfx/engine_parts.h"
#include "base/maths/vec4.h"
#include "render_pc/graphics_options_d3d.h"
#include "base/maths/trig.h"

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
    static_assert(sizeof(maths::Mat4) == sizeof(GpuMatrix));
    gpu().set_transform(static_cast<GpuTransform>(type), matrix->data());
}
void set_render_target(GpuSurface* surface) { gpu().set_render_target(0, surface); }
void set_depth_stencil_surface(GpuSurface* surface) { gpu().set_depth_surface(surface); }
void set_texture_stage_state(int stage, int type, int value)
{
    gpu().set_stage_state(static_cast<uint32_t>(stage), static_cast<GpuStageState>(type), static_cast<uint32_t>(value));
}
uint32_t get_sampler_state(int sampler, int type)
{
    uint32_t value = 0;
    gpu().get_sampler_state(static_cast<uint32_t>(sampler), static_cast<GpuSamplerState>(type), &value);
    return value;
}
void set_sampler_state(int sampler, int type, uint32_t value)
{
    gpu().set_sampler_state(static_cast<uint32_t>(sampler), static_cast<GpuSamplerState>(type), value);
}
void clear_target(uint32_t color) { gpu().clear(0, nullptr, gpu_value::kClearTarget, color, 1.0F, 0); }
void draw_primitive_up(int type, unsigned int count, const void* vertices, unsigned int stride)
{
    gpu().draw_from_memory(static_cast<GpuPrimitive>(type), count, vertices, stride);
}

// --- the original's D3DX (linked library code; stdcall): base/maths/view_transforms.h -----------------------------

void matrix_look_at(maths::Mat4* out, const float* eye, const float* at, const float* up)
{
    maths::matrix_look_at_rh(out->data(), eye, at, up);
}
void matrix_perspective(maths::Mat4* out, float width, float height, float near_z, float far_z)
{
    maths::matrix_perspective_rh(out->floats(), width, height, near_z, far_z);
}
void matrix_scaling(maths::Mat4* out, float x, float y, float z) { maths::matrix_scaling(out->floats(), x, y, z); }
void matrix_rotation_y(maths::Mat4* out, float angle) { maths::matrix_rotation_y(out->floats(), angle); }

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
    // (the view's width at the near plane: kNearPlaneDistance is twice the original's near plane, 2. Not original: with
    // the editor's ortho views' near plane further out - stored_near_z() - the width there, so the field of view stays
    // the angle; at 2 the same value as the original's tan * 4.)
    auto width = static_cast<float>(std::tan(half_angle) * kNearPlaneDistance * (static_cast<double>(camera.near_z_) / kDefaultNearZ));
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
    shape.tan_x_ = maths::runtime32::tan(static_cast<float>(half_angle));
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
    GpuTexture* texture = screen_copy_texture(current);
    GpuTexture* other = screen_copy_texture((current + 1) % 2);
    GpuSize desc;
    gpu().texture_level_size(texture, 0, &desc);
    const uint32_t texels_wide = desc.width;
    const uint32_t texels_high = desc.height;

    // The quad covers the screen; the texture is drawn shrunk towards its centre by the blur amount.
    const uint32_t amount = effects_state().screen_blur();
    auto inset = static_cast<float>((1.0 - (static_cast<double>(amount) * (1.0F / kColourByteMax))) * kHalfFactor);
    const double inset_u = inset / static_cast<double>(texels_wide); // (the u's are rounded from this, unrounded)
    auto inset_v = static_cast<float>(inset / static_cast<double>(texels_high));
    auto screen_width = static_cast<float>(presented_width());
    auto screen_height = static_cast<float>(presented_height());
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
    GpuSurface* surface = nullptr;
    gpu().texture_surface(texture, 0, &surface);
    // (NOT ORIGINAL: or the multisampled scene surface - [Graphics] msaa, engine/gfx/graphics_options.h)
    if (!graphics_options::set_scene_target(texture)) {
        set_render_target(surface);
        set_depth_stencil_surface(scene_depth_surface());
    }
    gpu().release_surface(surface);
    set_vertex_format(kScreenVertexFormat);                                          // XYZRHW | DIFFUSE | TEX1
    set_pixel_shader(nullptr, nullptr);                                // (no pixel shader)
    set_texture_stage_state(0, 1, 2);                                  // colour op: select arg 1
    set_texture_stage_state(0, 2, 2);                                  // colour arg 1: texture
    set_texture_stage_state(0, 4, 1);                                  // alpha op: disable
    if (effects_state().screen_blur_frames() > 2) {
        set_render_state(kAlphaBlendState, 1);
        set_texture(0, other);
        draw_primitive_up(kTriangleStrip /* triangle strip */, 2, quad.data(), sizeof(ScreenVertex));
        renderer().count_draw(2); // the frame's draw calls and triangles
    }
    // ...then `texture` onto the back buffer, cleared first to the fog colour (with fog and distance clipping
    // on) or the background colour.
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): the multisampled frame resolved into `texture`.
    graphics_options::resolve_scene_target(texture);
    set_render_target(back_buffer_surface());
    set_depth_stencil_surface(back_depth_surface());
    const bool fogged = view_setup().fog_setting() && view_setup().distance_clipping();
    clear_target(fogged ? engine_parts().fog_colour() : renderer().render_camera().background_colour_);
    set_render_state(kAlphaBlendState, 0);
    set_texture(0, texture);
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
    gpu().set_viewport({.x = vp.x, .y = vp.y, .width = vp.width, .height = vp.height, .min_z = vp.min_z_, .max_z = vp.max_z_});
}
// The device's render state directly (not the renderer's cached one).
void set_device_render_state(int state, uint32_t value)
{
    gpu().set_render_state(static_cast<GpuRenderState>(state), value);
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

// Not original - a split view's pass (ext::begin_view, the level editor's split view): the part of the viewport's
// area it is drawn in (none: the whole of it).
struct PassArea {
    bool on = false;
    std::array<float, 4> rect = {{0.0F, 0.0F, 1.0F, 1.0F}};
};
PassArea& pass_area()
{
    static PassArea area;
    return area;
}

// `whole`'s part that a pass's area covers (its edges rounded to pixels, so neighbouring views share them).
DeviceViewport part_of(const DeviceViewport& whole, const std::array<float, 4>& rect)
{
    const auto edge = [](uint32_t from, uint32_t size, float part) {
        return from + static_cast<uint32_t>(std::lround(static_cast<double>(size) * std::clamp(static_cast<double>(part), 0.0, 1.0)));
    };
    DeviceViewport part = whole;
    const auto [left, top, width, height] = rect;
    part.x = edge(whole.x, whole.width, left);
    part.y = edge(whole.y, whole.height, top);
    part.width = std::max(1U, edge(whole.x, whole.width, left + width) - part.x);
    part.height = std::max(1U, edge(whole.y, whole.height, top + height) - part.y);
    return part;
}

void draw_scene_group(PcScene* scene, int pass, int viewport)
{
    draw_scene(scene->meshes(), {.flags = static_cast<uint32_t>(pass), .viewport = viewport});
}
void set_pass(int pass) { draw_instance_batch({.flags = static_cast<uint32_t>(pass), .viewport = 0}); }
// No world transform (set_world_transform's null: the original passes 0).
void clear_world_transform() { set_world_transform(nullptr); }

// Not original - the listeners of rt_scene.h told that viewport `index`'s opaque world is drawn: its camera (the world's
// view and projection, as set on the Gpu) and its rectangle.
// (the viewport as it is drawn now - its world camera, its rectangle - for the screen-space effects)
rt::ViewDrawn view_drawn(int index)
{
    const auto& gfx = renderer();
    rt::ViewDrawn view;
    view.camera.viewport = static_cast<uint32_t>(index);
    const auto copy_matrix = [](const maths::Mat4& from, std::array<std::array<float, 4>, 4>* to) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                core::at(core::at(*to, row), column) = core::at(from.floats(), (row * 4) + column);
            }
        }
    };
    copy_matrix(gfx.view_matrix(), &view.camera.view);
    copy_matrix(gfx.projection_matrix(), &view.camera.projection);
    view.camera.position = gfx.render_camera().position;
    const DeviceViewport& drawn = gfx.device_viewport();
    view.rect = {drawn.x, drawn.y, drawn.width, drawn.height};
    view.depth_range = {drawn.min_z_, drawn.max_z_};
    // (the level's fog: with distance clipping and [Graphics] Fog, the fog each mesh draws with - over the last quarter
    // before the far plane, pc_mesh_render.cpp; else what a script's SetFogDistance / SetFogColor / EnableFog told the
    // device - pc_fog.h)
    constexpr uint32_t kByte = 0xFF;
    constexpr uint32_t kRedShift = 16;
    constexpr uint32_t kGreenShift = 8;
    constexpr float kPerByte = 1.0F / 255.0F;
    constexpr double kNearFadeShare = 0.25; // (pc_mesh_render.cpp's)
    const uint32_t fog_colour = engine_parts().fog_colour(); // (0xRRGGBB)
    if (view_setup().distance_clipping() && view_setup().fog_setting()) {
        const float far_z = gfx.render_camera().far_z_;
        view.fog.on = true;
        view.fog.end = far_z;
        view.fog.start = static_cast<float>(
            far_z - ((static_cast<double>(far_z) - gfx.render_camera().near_z_) * kNearFadeShare));
    } else {
        view.fog.on = gfx.fog_active() != 0;
        view.fog.start = gfx.fog_begin_distance();
        view.fog.end = gfx.fog_end();
    }
    view.fog.colour = {static_cast<float>((fog_colour >> kRedShift) & kByte) * kPerByte,
                       static_cast<float>((fog_colour >> kGreenShift) & kByte) * kPerByte,
                       static_cast<float>(fog_colour & kByte) * kPerByte};
    return view;
}

void note_opaque_drawn(int index)
{
    if (rt::view_listened()) {
        rt::opaque_view_drawn(view_drawn(index));
    }
}

// Not original - the viewport's whole world drawn, before the 2D: volumetric fog, tone mapping (rt_scene.h
// ViewListener::world_drawn). Original behaviour: nothing.
void note_world_drawn(int index)
{
    if (rt::view_listened()) {
        rt::world_view_drawn(view_drawn(index));
    }
}

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
    // (then the automation channel's camera lock - game/rider/test_view.h: viewport 0's camera held at a pose)
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
    // Not original - a split view's pass: its part of the viewport, the aspect its own (the area's shape against the
    // whole's). Original behaviour: the whole viewport.
    float aspect_scale = 1.0F;
    if (pass_area().on) {
        const DeviceViewport whole = renderer().device_viewport();
        const DeviceViewport part = part_of(whole, pass_area().rect);
        renderer().set_device_viewport(part);
        aspect_scale = static_cast<float>((static_cast<double>(part.width) * whole.height) /
                                          (static_cast<double>(part.height) * std::max(1U, whole.width)));
    }
    set_viewport();
    clear_world_transform();
    const float aspect = view->fetch_width_over_height() * aspect_scale;

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
        // Not original - ray tracing's record (rt_record.h): the viewport's camera; the frame's first begins the
        // frame (the TLAS) before the world is drawn. Original behaviour: nothing.
        rt_record::camera_chosen(index);
    }
    rt_record::set_world_passes(true); // (not original: the world's and the objects' passes, for a check's count)
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
    rt_record::set_world_passes(false); // (not original: not the sky's)

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
    // Not original - ray tracing's screen-space effects (engine/gfx/rt_scene.h, R4: the Vulkan Gpu's sun shadows): the
    // opaque world, objects and sky drawn, the world's camera set again. Original behaviour: nothing.
    note_opaque_drawn(index);
    rt_record::set_world_passes(true); // (not original: the world's and the objects' passes again)
    set_pass(kOpaquePass);
    set_render_state(RenderStateId::kZWrite, 1);
    set_render_state(RenderStateId::kZTest, 1);
    for_each_scene([&](PcScene* scene) {
        if (!scene->is_sky()) {
            draw_scene_group(scene, kSkyFreeScenePasses, index);
        }
    });
    set_pass(kOcclusionPass);
    rt_record::set_world_passes(false);
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
    note_world_drawn(index);
}

// Not original - the split views (ext::view_count; the level editor's): the first viewport drawn once for each view,
// in its part of the frame and its view mode (the frame's mode put back after), the lines handed over (depth_lines.h)
// drawn in each, then dropped. The view's area is cleared by Collision only's begin_world, so it is set first.
void draw_views(int views)
{
    const view_mode::Mode frame_mode = view_mode::at_hand();
    for (int i = 0; i < views; ++i) {
        ext::ViewPass pass;
        if (!ext::begin_view(i, &pass)) {
            continue;
        }
        pass_area() = {.on = true, .rect = pass.rect};
        const auto mode = static_cast<view_mode::Mode>(
            std::clamp(pass.mode, 0, static_cast<int>(view_mode::Mode::kCount) - 1));
        view_mode::switch_for_view(mode);
        renderer().set_device_viewport(part_of(
            {.x = 0,
             .y = 0,
             .width = static_cast<uint32_t>(renderer().back_buffer_width()),
             .height = static_cast<uint32_t>(renderer().back_buffer_height()),
             .min_z_ = 0.0F,
             .max_z_ = 1.0F},
            pass.rect));
        set_viewport();
        const bool fog = renderer().fog_active() != 0;
        if (!pass.fog) {
            change_fog(false);
        }
        wireframe::begin_world();
        view_mode::begin_world();
        draw_viewport(0);
        ghost_draw::draw(true);
        view_mode::end_world();
        wireframe::end_world();
        outline_draw::draw(true);
        depth_lines_draw::draw(true);
        change_fog(fog);
        pass_area() = {};
    }
    ext::end_views();
    view_mode::switch_for_view(frame_mode);
    depth_lines_draw::drop();
    ghost_draw::drop();
    outline_draw::drop();
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
        // Not original - the game view (engine/gfx/game_view.h; the level editor's Scene panel): the frame drawn into
        // the picture asked for, at its size, instead of the window.
        // Original behaviour: the frame is drawn to the window.
        game_view_frame::begin();
        renderer().set_step_seconds(static_cast<int>(frame_clock::get_time()));
        // Not original - ray tracing's record (rt_record.h): the frame's instances and lights. Original behaviour:
        // nothing.
        rt_record::record_frame();
        set_render_state(RenderStateId::kZWrite, 1);
        set_render_state(RenderStateId::kZTest, 1);
        render_shadow_textures();
        begin_screen_effects();
        const int count = modules::viewport_count();
        // Not original - an extension's split views (the level editor's: draw_views). Original behaviour: each
        // viewport once.
        if (const int views = count > 0 ? ext::view_count() : 0; views > 0) {
            draw_views(views);
        } else {
            // Not original - the wireframe render mode (render_pc/wireframe.h): the 3D passes in wireframe when asked.
            // Original behaviour: the render mode (0x006FA734) isn't read.
            wireframe::begin_world();
            view_mode::begin_world(); // (not original either: the editor's view modes - render_pc/view_mode.h)
            for (int i = 0; i < count; ++i) {
                draw_viewport(i);
            }
            // Not original - the ghosts a UI handed over (engine/gfx/ghost_draws.h; the level editor's drag preview),
            // drawn over the world with the last viewport's camera. Original behaviour: nothing.
            ghost_draw::draw();
            view_mode::end_world();
            wireframe::end_world();
            // Not original - the outlines a UI handed over (engine/gfx/outline_draws.h; the level editor's selection and
            // hover), round what they mark, over the world. Original behaviour: nothing.
            outline_draw::draw();
            // Not original - the lines a UI handed over (engine/gfx/depth_lines.h; the level editor's rails), drawn
            // against the world's depth.
            // Original behaviour: nothing.
            depth_lines_draw::draw();
        }

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
        // Not original - the game view: the window drawn to again (what the extensions draw over the frame - the
        // level editor's UI, its Scene panel showing the picture - goes to the window).
        game_view_frame::end();
        // Not original - the extensions (engine/ext/extension.h; the level editor: the hovered and selected objects
        // outlined over everything; then the new front end - [features] new_frontend - over everything while it's up
        // in the front end).
        // Original behaviour: nothing more is drawn.
        ext::draw_over_frame();
        change_fog(fog);
    } else {
        depth_lines_draw::drop(); // (not original: lines handed over before the loading screen aren't drawn after it)
        ghost_draw::drop();       // (not original: the same for ghosts)
        outline_draw::drop();     // (not original: and outlines)
    }
    full_screen_viewport();
    set_viewport();
}

} // namespace render

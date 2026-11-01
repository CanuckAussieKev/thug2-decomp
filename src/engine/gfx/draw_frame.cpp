// The per-frame 3D render: the camera's matrices and the motion blur pass, with PartyMod's widescreen
// display region and black bars (contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).

#include "engine/gfx/draw_frame.h"
#include "engine/gfx/d3dx.h"
#include "engine/gfx/device.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/settings.h"
#include "engine/gfx/view_mode.h"
#include "engine/gfx/wireframe.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>

#include "editor/level_editor.h"
#include "frontend/new_frontend.h"
#include "frontend/test_view.h"
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
#include "platform/screenshot.h"
#include "platform/stopwatch.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/engine.h"
#include "engine/objects/particle_component.h"
#include "engine/gfx/weather.h"
#include "game/skate/skate_module.h"
#include "engine/core/bits_as.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace render {

DeviceViewport g_device_viewport;
RenderCamera g_camera;
ViewShape g_view_shape;

bool g_radeon8500;
int g_frames_rendered;
maths::Mat4 g_world_matrix;
maths::Mat4 g_view_matrix;
maths::Mat4 g_projection_matrix;
maths::Mat4 g_heading_matrix;
int g_step_seconds;
int g_screen_copy_index;
int g_back_buffer_width;
int g_back_buffer_height;
int g_draw_calls;
int g_drawn_triangles;
float g_fog_begin_distance;
float g_fog_end;

namespace {

// --- the renderer's state (render_state.h) ---------------------------------------------------------------------

// A render state by the renderer's own numbering (1-0x24), cached.
void set_render_state(int state, int value) { change_phase(state, static_cast<uint32_t>(value)); }
// Fog on or off (the request is kept in 0x007004C8; the device's D3DRS_FOGENABLE follows).
void change_fog(bool on) { engine::enable_fog(on); }

// --- the D3D9 device (0x007D69E0) and its objects, through their vtables ----------------------------------------

void* input_port() { return raw::as<void>(g_d3d_device); }

void set_transform(int type, const maths::Mat4* m)
{
    static_assert(sizeof(maths::Mat4) == sizeof(D3DMATRIX));
    g_d3d_device->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(type), raw::as<const D3DMATRIX>(m));
}
void set_render_target(void* surface) { g_d3d_device->SetRenderTarget(0, static_cast<IDirect3DSurface9*>(surface)); }
void set_depth_stencil_surface(void* surface)
{
    g_d3d_device->SetDepthStencilSurface(static_cast<IDirect3DSurface9*>(surface));
}
void set_texture_stage_state(int stage, int type, int value)
{
    g_d3d_device->SetTextureStageState(static_cast<DWORD>(stage), static_cast<D3DTEXTURESTAGESTATETYPE>(type),
                                       static_cast<DWORD>(value));
}
uint32_t get_sampler_state(int sampler, int type)
{
    DWORD value = 0;
    g_d3d_device->GetSamplerState(static_cast<DWORD>(sampler), static_cast<D3DSAMPLERSTATETYPE>(type), &value);
    return value;
}
void set_sampler_state(int sampler, int type, uint32_t value)
{
    g_d3d_device->SetSamplerState(static_cast<DWORD>(sampler), static_cast<D3DSAMPLERSTATETYPE>(type), value);
}
void clear_target(uint32_t color) { g_d3d_device->Clear(0, nullptr, D3DCLEAR_TARGET, color, 1.0f, 0); }
void draw_primitive_up(int type, unsigned int count, const void* vertices, unsigned int stride)
{
    g_d3d_device->DrawPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(type), count, vertices, stride);
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
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }
void matrix_rotation_y(maths::Mat4* out, float angle) { d3dx::matrix_rotation_y(out, angle); }

// atan2(y, x) - pi/2 (the pi/2 a float), rounded once to float.
float atan2_less_quarter_turn(float y, float x)
{
    constexpr float kQuarterTurn = 1.5707963705062866f;
    return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)) - kQuarterTurn);
}

constexpr float float_from_bits(uint32_t bits) { return std::bit_cast<float>(bits); }

} // namespace

// --- PartyMod's display region and black bars ---------------------------------------------------------------

void set_display_region()
{
    // the 640x480 layout's size on the back buffer (set_letterbox), centred
    auto width = static_cast<uint32_t>(g_display_factor_x * 640.0f);
    auto height = static_cast<uint32_t>(g_display_factor_y * 480.0f);
    // (the device viewport, 0x00786C90: x, y, width, height)
    g_device_viewport.x = (memory::bits_as<uint32_t>(g_back_buffer_width) - width) / 2;
    g_device_viewport.width = width;
    g_device_viewport.y = (memory::bits_as<uint32_t>(g_back_buffer_height) - height) / 2;
    g_device_viewport.height = height;
}

void draw_2d_with_black_bars()
{
    struct BarVertex {
        float x, y, z, w;
        uint32_t color;
        float u, v;
    };
    draw_2d(); // the 2D pass
    // (widescreen_menus: in the front end the 3D scene fills the sides, so no bars over it.)
    if (get_front_end_widescreen())
        return;

    const uint32_t back_buffer_width = memory::bits_as<uint32_t>(g_back_buffer_width);
    const uint32_t back_buffer_height = memory::bits_as<uint32_t>(g_back_buffer_height);
    const float back_buffer_aspect = static_cast<float>(back_buffer_width) / static_cast<float>(back_buffer_height);
    constexpr uint32_t kBlack = 0xFF000000;
    if (back_buffer_aspect > desired_aspect_ratio()) {
        // bars left and right
        auto width = static_cast<uint32_t>(std::round(static_cast<double>(g_display_factor_x * 640.0f)));
        const uint32_t bar_width = (back_buffer_width - width) / 2;
        set_texture(0, nullptr);
        std::array<BarVertex, 4> bar = {{
            {-0.5f, -0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
            {-0.5f, static_cast<float>(back_buffer_height) + 0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
            {static_cast<float>(bar_width) + 0.5f, static_cast<float>(back_buffer_height) + 0.5f, 0.0f, 1.0f, kBlack,
             0.0f, 0.0f},
            {static_cast<float>(bar_width) + 0.5f, -0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
        }};
        draw_primitive_up(6 /* D3DPT_TRIANGLEFAN */, 2, bar.data(), sizeof(BarVertex));
        for (BarVertex& corner : bar)
            corner.x += static_cast<float>(width + bar_width);
        draw_primitive_up(6, 2, bar.data(), sizeof(BarVertex));
    } else if (back_buffer_aspect < desired_aspect_ratio()) {
        // bars top and bottom
        auto height = static_cast<uint32_t>(std::round(static_cast<double>(g_display_factor_y * 480.0f)));
        const uint32_t bar_height = (back_buffer_height - height) / 2;
        set_texture(0, nullptr);
        std::array<BarVertex, 4> bar = {{
            {-0.5f, -0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
            {-0.5f, static_cast<float>(bar_height) + 0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
            {static_cast<float>(back_buffer_width) + 0.5f, static_cast<float>(bar_height) + 0.5f, 0.0f, 1.0f, kBlack,
             0.0f, 0.0f},
            {static_cast<float>(back_buffer_width) + 0.5f, -0.5f, 0.0f, 1.0f, kBlack, 0.0f, 0.0f},
        }};
        draw_primitive_up(6, 2, bar.data(), sizeof(BarVertex));
        for (BarVertex& corner : bar)
            corner.y += static_cast<float>(height + bar_height);
        draw_primitive_up(6, 2, bar.data(), sizeof(BarVertex));
    }
}

namespace {
// Not original - the level editor's ortho views (editor/level_editor_camera.h): the near plane brought up for the
// editor's camera while draw_viewport draws the world with it (2, the original's, otherwise).
float s_near_z = 2.0f;
} // namespace

// 0x004DB200
void choose_camera(const maths::Mat4* orientation, const maths::Vec4* position, float angle, float aspect, float zoom,
                   bool infinite)
{
    auto& camera = g_camera;
    auto& world = g_world_matrix;
    auto& view = g_view_matrix;
    auto& projection = g_projection_matrix;

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
    world = maths::Mat4{{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}}};
    matrix_look_at(&view, camera.position.data(), target.data(), camera.up.data());

    // (not original: s_near_z - the original: camera.nearZ = 2.0f;)
    camera.near_z_ = s_near_z;
    // Originally from PartyMod Fixes (its float at 0x004DB35D): the far plane 96000 - further, for large levels' backgrounds (e.g.
    // Skatopia). The original: camera.farZ = 32000.0f;
    camera.far_z_ = 96000.0f;
    camera.angle = angle;
    if (g_distance_clipping) // distance clipping (setting 0x007D6444: 100-595)
        camera.far_z_ =
            static_cast<float>(static_cast<double>(static_cast<uint32_t>(gfx::g_draw_distance_setting)) * 18.0);
    // NOT ORIGINAL - [Graphics] draw_distance_scale (engine/gfx/graphics_options.h): the far plane (and with it the
    // distance clipping's cull distance, 0x00786CAC) further out.
    camera.far_z_ = graphics_options::get_scaled(camera.far_z_, graphics_options::get_pull().draw_distance_scale_);

    // (degrees to half the angle in radians: the tangent takes the product unrounded, later uses a float)
    const double half_angle = static_cast<double>(angle) * 0.008726646f;
    auto width = static_cast<float>(std::tan(half_angle) * 4.0f);
    auto height_factor = static_cast<float>(1.0 / (static_cast<double>(aspect) * 0.90909094f));
    const float height = height_factor * width;
    matrix_perspective(&projection, width, height, camera.near_z_, camera.far_z_);
    if (zoom != 1.0f) {
        maths::Mat4 scale;
        matrix_scaling(&scale, zoom, zoom, 1.0f);
        matrix_multiply(&projection, &projection, &scale);
    }
    camera.near_height_ = height;
    camera.near_width_ = width;
    if (infinite) {
        core::at(projection.row, 2).z = -0.999999f;
        core::at(projection.row, 3).z = 1e-6f;
    }
    set_transform(0x100, &world); // D3DTS_WORLD
    set_transform(2, &view);      // D3DTS_VIEW
    set_transform(3, &projection);

    auto& shape = g_view_shape;
    shape.tan_x_ = std::tan(static_cast<float>(half_angle));
    shape.tan_y_ = -(height_factor * shape.tan_x_);
    const double tan_x2 = static_cast<double>(shape.tan_x_) * shape.tan_x_;
    const double tan_y2 = static_cast<double>(shape.tan_y_) * shape.tan_y_;
    shape.sin_x_ = static_cast<float>(1.0 / std::sqrt(1.0 / tan_x2 + 1.0));
    shape.sin_y_ = static_cast<float>(1.0 / std::sqrt(1.0 / tan_y2 + 1.0));
    shape.cos_x_ = static_cast<float>(1.0 / std::sqrt(tan_x2 + 1.0));
    shape.cos_y_ = static_cast<float>(1.0 / std::sqrt(tan_y2 + 1.0));

    // The camera's heading: a rotation about the vertical axis (0x00786C50).
    matrix_rotation_y(&g_heading_matrix, atan2_less_quarter_turn(-core::at(camera.backward, 2), -core::at(camera.backward, 0)));
    // The view frustum (0x00775B80) follows the new view.
    g_billboard_pool.setup_axes();
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
void draw_screen_blur()
{
    if (!g_screen_copy_in_use) // blur available
        return;
    change_fog(false);
    set_render_state(0x15, 0);
    set_render_state(0x16, 0);
    set_render_state(0x10, 1);
    // Linear filtering on all four samplers (put back at the end).
    std::array<uint32_t, 4> min_filter{};
    std::array<uint32_t, 4> mag_filter{};
    for (int i = 0; i < 4; ++i) {
        core::at(min_filter, i) = get_sampler_state(i, 6); // D3DSAMP_MINFILTER
        if (core::at(min_filter, i) != 2)                  // D3DTEXF_LINEAR
            set_sampler_state(i, 6, 2);
        core::at(mag_filter, i) = get_sampler_state(i, 5); // D3DSAMP_MAGFILTER
        if (core::at(mag_filter, i) != 2)
            set_sampler_state(i, 5, 2);
    }
    if (g_bloom_on)
        get_bloom();
    set_render_state(4, 0x10001);
    set_render_state(1, 0);
    set_render_state(2, 0);
    set_render_state(0x12, 0);
    set_render_state(0x11, 1);

    // Two textures take turns: the one just drawn into (the last frame's blur) and the other.
    const int current = g_screen_copy_index;
    IDirect3DTexture9* texture = core::at(g_screen_textures, current);
    IDirect3DTexture9* other = core::at(g_screen_textures, (current + 1) % 2);
    D3DSURFACE_DESC desc;
    texture->GetLevelDesc(0, &desc);
    const uint32_t texels_wide = desc.Width;
    const uint32_t texels_high = desc.Height;

    // The quad covers the screen; the texture is drawn shrunk towards its centre by the blur amount.
    const uint32_t amount = g_screen_blur;
    auto inset = static_cast<float>((1.0 - static_cast<double>(amount) * (1.0f / 255)) * 0.5);
    const double inset_u = inset / static_cast<double>(texels_wide); // (the u's are rounded from this, unrounded)
    auto inset_v = static_cast<float>(inset / static_cast<double>(texels_high));
    auto screen_width = static_cast<float>(g_present_parameters.BackBufferWidth);
    auto screen_height = static_cast<float>(g_present_parameters.BackBufferHeight);
    uint32_t color = 0x80808080;
    std::array<ScreenVertex, 4> quad = {{
        {0, 0, 0, 1, color, static_cast<float>(inset_u), inset_v},
        {screen_width, 0, 0, 1, color, static_cast<float>(1.0 - inset_u), inset_v},
        {0, screen_height, 0, 1, color, static_cast<float>(inset_u), 1.0f - inset_v},
        {screen_width, screen_height, 0, 1, color, static_cast<float>(1.0 - inset_u), 1.0f - inset_v},
    }};
    // Originally from PartyMod Fixes (its JMP at 0x004DF63A): no letterbox adjustment - PartyMod's letterbox is a viewport. The
    // original, when letterboxed (0x00786CBE), moved the quad in by an eighth of the back buffer's height:
    //   float bar = static_cast<float>(g_back_buffer_height / 8);
    //   quad[0].y = quad[1].y = bar;
    //   quad[0].v = quad[1].v = inset_v + bar;   (sic: a pixel count added to a texture coordinate)
    //   quad[2].y -= bar; quad[3].y -= bar; quad[2].v -= bar; quad[3].v -= bar;
    if (amount) {
        choose_blend_mode(g_screen_blur_frames > 2 ? 5 : 0);
        color = amount << 24 | 0x808080;
        for (ScreenVertex& v : quad)
            v.color = color;
    } else {
        choose_blend_mode(0);
    }

    // Draw into `texture`: the other texture (if the blur level, 0x0078732C, is above 2)...
    IDirect3DSurface9* surface = nullptr;
    texture->GetSurfaceLevel(0, &surface);
    // (NOT ORIGINAL: or the multisampled scene surface - [Graphics] msaa, engine/gfx/graphics_options.h)
    if (!graphics_options::set_scene_target(texture)) {
        set_render_target(surface);
        set_depth_stencil_surface(g_z_buffer_surface);
    }
    surface->Release();
    set_vertex_format(0x144);                                          // XYZRHW | DIFFUSE | TEX1
    set_pixel_shader(nullptr, nullptr);                                // (no pixel shader)
    set_texture_stage_state(0, 1, 2);                                  // colour op: select arg 1
    set_texture_stage_state(0, 2, 2);                                  // colour arg 1: texture
    set_texture_stage_state(0, 4, 1);                                  // alpha op: disable
    if (g_screen_blur_frames > 2) {
        set_render_state(0x11, 1);
        set_texture(0, other);
        draw_primitive_up(5 /* triangle strip */, 2, quad.data(), sizeof(ScreenVertex));
        ++g_draw_calls;         // draw calls
        g_drawn_triangles += 2; // triangles
    }
    // ...then `texture` onto the back buffer, cleared first to the fog colour (with fog and distance clipping
    // on) or the background colour.
    // NOT ORIGINAL - [Graphics] msaa (engine/gfx/graphics_options.h): the multisampled frame resolved into `texture`.
    graphics_options::resolve_scene_target(static_cast<IDirect3DTexture9*>(texture));
    set_render_target(raw::as<void>(g_back_buffer));
    set_depth_stencil_surface(raw::as<void>(g_back_depth));
    const bool fog_colour = g_fog_setting && g_distance_clipping;
    clear_target(fog_colour ? g_fog_colour : g_camera.background_colour_);
    set_render_state(0x11, 0);
    set_texture(0, texture);
    draw_primitive_up(5, 2, quad.data(), sizeof(ScreenVertex));
    set_render_state(0x11, 1);
    ++g_draw_calls;
    g_drawn_triangles += 2;
    for (int i = 0; i < 4; ++i) {
        set_sampler_state(i, 6, core::at(min_filter, i));
        set_sampler_state(i, 5, core::at(mag_filter, i));
    }
    g_screen_copy_index = (g_screen_copy_index + 1) % 2;
    change_fog(true);
}

namespace {

// The screen's viewports (0x004A06D0 returns one by index): fractions of the back buffer, and the camera.
struct Viewport {
    uint32_t unknown00;
    float x, y, width, height; // +0x04
    uint32_t unknown14;
    void* camera; // +0x18 (null: nothing to draw)
};
static_assert(offsetof(Viewport, camera) == 0x18, "viewport layout");

// A world scene's mesh group: flagged 0x100 once its meshes are prepared.
struct MeshGroup {
    uint32_t flags;
    std::array<uint8_t, 0x18> unknown04;
    void** meshes;   // +0x1C
    int mesh_count_; // +0x20
};
static_assert(offsetof(MeshGroup, mesh_count_) == 0x20, "mesh group layout");

// A loaded world scene (only what the render uses).
struct Scene {
    std::array<uint8_t, 0x84> unknown000;
    uint32_t name;   // +0x84: the checksum of its name
    void* tex_dict_; // +0x88
    uint8_t unknown08c_;
    bool sky; // +0x8D: drawn in the sky pass
    std::array<uint8_t, 0x1F2> unknown08e_;
    MeshGroup* group; // +0x280
};
static_assert(offsetof(Scene, group) == 0x280 && offsetof(Scene, sky) == 0x8D && offsetof(Scene, name) == 0x84,
              "scene layout");

// The list of loaded scenes (0x006FA7EC): ends at a node whose scene is -1.
struct SceneNode {
    Scene* scene; // +0x00 (memory::ListNode's data)
    int priority;
    SceneNode* next; // +0x08
};
static_assert(sizeof(SceneNode) == 0x0C, "memory::ListNode's data, priority and next");

bool is_end(const SceneNode* node) { return static_cast<intptr_t>(cell::address(node->scene)) == -1; }

// Calls fn(scene) for every scene in the list (the next node is taken before the call).
template <typename F>
void for_each_scene(F fn)
{
    auto* node = raw::as<SceneNode>(g_scene_list.next);
    if (is_end(node) || !node) // (sic: the original reads the node before testing it)
        return;
    while (node) {
        SceneNode* next = is_end(node->next) ? nullptr : node->next;
        fn(node->scene);
        node = next;
    }
}

DeviceViewport& the_viewport() { return g_device_viewport; }
void set_viewport()
{
    static_assert(sizeof(DeviceViewport) == sizeof(D3DVIEWPORT9));
    g_d3d_device->SetViewport(raw::as<const D3DVIEWPORT9>(&the_viewport()));
}
// The device's render state directly (not the renderer's cached one).
void set_device_render_state(int state, uint32_t value)
{
    g_d3d_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state), value);
}

void full_screen_viewport()
{
    the_viewport() = {
        0,    0,   memory::bits_as<uint32_t>(g_back_buffer_width), memory::bits_as<uint32_t>(g_back_buffer_height),
        0.0f, 1.0f};
}

// The camera (a game object).
float camera_angle(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_effective_h_fov(); }
const maths::Vec4* viewer_location(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_location(); }
const maths::Mat4* cam_orientation(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_mat(); }

void draw_scene_group(Scene* scene, int pass, int viewport)
{
    draw_scene(raw::as<SceneRef>(scene->group), static_cast<uint32_t>(pass), viewport);
}
void set_pass(int pass) { draw_instance_batch(static_cast<uint32_t>(pass), 0); }
// No world transform (set_world_transform's null: the original passes 0).
void clear_world_transform() { set_world_transform(nullptr); }

void draw_viewport(int index)
{
    const auto* view = raw::as<const Viewport>(fetch_current_view(index));
    void* camera = view->camera;
    if (!camera)
        return;
    // Not original - the level editor (editor/level_editor.h): while it runs, the first viewport's camera is put
    // where the editor flies.
    // Original behaviour: the camera is drawn where the game put it.
    frontend::level_editor::place_camera(static_cast<gfx::ViewCam*>(camera));
    // Not original - the automation channel's camera lock (frontend/test_view.h): viewport 0's camera held at a pose.
    // Original behaviour: the camera is drawn where the game put it.
    frontend::test_view::place_camera(static_cast<gfx::ViewCam*>(camera), index);
    g_drawn_viewport = index; // the viewport being drawn
    const double width = g_back_buffer_width;
    const double height = static_cast<float>(g_back_buffer_height);
    DeviceViewport& vp = the_viewport();
    vp.x = static_cast<int>(width * view->x);
    vp.y = static_cast<int>(height * view->y);
    vp.width = static_cast<int>(width * view->width);
    vp.height = static_cast<int>(height * view->height);
    vp.min_z_ = 0.0f;
    vp.max_z_ = 1.0f;
    if (g_camera.letterbox_active_ && !get_front_end_widescreen()) {
        // Originally from PartyMod Fixes (its call at 0x004B2E65): the viewport fitted to the aspect wanted. The original squashed the
        // viewport to 3/4 of the screen's height:
        //   int screen_height = g_back_buffer_height;
        //   vp.y += screen_height / 8;
        //   vp.height -= screen_height / 4;
        // (widescreen_menus: in the front end the 3D view keeps the whole screen; the 2D below stays fitted.)
        set_display_region();
    }
    set_viewport();
    clear_world_transform();
    const float aspect = raw::as<const ViewRegion>(view)->fetch_width_over_height();

    // Not original - the level editor's ortho views (editor/level_editor_camera.h): its camera's field of view
    // narrowed and the near plane brought up for the world's passes (the sky's keeps the near plane).
    // Original behaviour: the camera's own angle, the near plane at 2.
    auto* editor_camera = static_cast<gfx::ViewCam*>(camera);
    const float editor_near = frontend::level_editor::get_near_plane(static_cast<gfx::ViewCam*>(camera));

    // The world: every scene but the sky.
    {
        const float angle = frontend::level_editor::adjust_view_angle(editor_camera, camera_angle(camera));
        s_near_z = editor_near;
        const maths::Vec4* position = viewer_location(camera);
        choose_camera(cam_orientation(camera), position, angle, aspect, 1.0f, false);
        // Not original - the level editor: the view's projection noted (its mouse ray and outlines).
        // Original behaviour: nothing.
        frontend::level_editor::note_view(static_cast<gfx::ViewCam*>(camera), angle, aspect, std::array<uint32_t, 4>{vp.x, vp.y, vp.width, vp.height});
        // Not original - the screenshot key's sidecar (platform/screenshot.h): the camera noted.
        const maths::Mat4* frame = cam_orientation(camera);
        const std::array<float, 3> at = {{position->x, position->y, position->z}};
        const std::array<float, 3> forward = {{-core::at(frame->row, 2).x, -core::at(frame->row, 2).y, -core::at(frame->row, 2).z}};
        sys::screenshot::note_view(index, at, forward);
    }
    for_each_scene([&](Scene* scene) {
        if (scene->sky)
            return;
        collect_occluders(viewer_location(camera), index);
        set_render_state(1, 1);
        set_render_state(2, 1);
        MeshGroup* group = scene->group;
        if (!(group->flags & 0x100)) {
            group->flags |= 0x100;
            for (int i = 0; i < group->mesh_count_; ++i)
                static_cast<MeshRef*>(group->meshes[i])->build_collision();
        }
        draw_scene_group(scene, 0x55, index);
    });
    set_render_state(1, 0);
    draw_texture_splats();
    draw_projection_batches();
    set_render_state(1, 1);
    static_cast<ImposterManager*>(g_imposter_manager.get())->render();
    set_pass(1);
    clear_world_transform();

    // The sky: from the origin, far plane at infinity, fog over 20-21 (the fog range put back after).
    {
        const maths::Vec4 origin = {0.0f, 0.0f, 0.0f, 1.0f};
        const float angle = frontend::level_editor::adjust_view_angle(editor_camera, camera_angle(camera));
        s_near_z = 2.0f;
        choose_camera(cam_orientation(camera), &origin, angle, aspect, 1.0f, true);
    }
    float& fog_start = g_fog_begin_distance;
    float& fog_end = g_fog_end;
    const float saved_fog_end = fog_end;
    const float saved_fog_start = fog_start;
    fog_start = 20.0f;
    fog_end = 21.0f;
    set_device_render_state(0x24, memory::bits_as<uint32_t>(g_fog_begin_distance)); // D3DRS_FOGSTART
    set_device_render_state(0x25, memory::bits_as<uint32_t>(g_fog_end));            // D3DRS_FOGEND
    const bool fog = g_fog_active != 0;
    change_fog(false);
    set_render_state(1, 0);
    set_render_state(2, 1);
    for_each_scene([&](Scene* scene) {
        if (!scene->sky)
            return;
        // (with distance clipping and fog both on, there's no sky)
        if (g_distance_clipping && g_fog_setting)
            return;
        const uint32_t min_filter = get_sampler_state(0, 6);
        const uint32_t mip_filter = get_sampler_state(0, 7);
        set_sampler_state(0, 6, 2); // linear
        set_sampler_state(0, 7, 0); // no mipmaps
        draw_scene_group(scene, 3, index);
        set_sampler_state(0, 7, mip_filter);
        set_sampler_state(0, 6, min_filter);
    });
    set_render_state(1, 1);
    set_render_state(1, 0);
    if (g_radeon8500)
        sun::render(static_cast<gfx::ViewCam*>(camera));
    set_render_state(1, 1);
    set_render_state(2, 1);
    fog_start = saved_fog_start;
    fog_end = saved_fog_end;
    set_device_render_state(0x24, memory::bits_as<uint32_t>(g_fog_begin_distance));
    set_device_render_state(0x25, memory::bits_as<uint32_t>(g_fog_end));
    change_fog(fog);

    // The world again, for the later passes.
    {
        const float angle = frontend::level_editor::adjust_view_angle(editor_camera, camera_angle(camera));
        const maths::Vec4* position = viewer_location(camera);
        s_near_z = editor_near;
        choose_camera(cam_orientation(camera), position, angle, aspect, 1.0f, false);
        s_near_z = 2.0f;
    }
    set_pass(0x82);
    set_render_state(1, 1);
    set_render_state(2, 1);
    for_each_scene([&](Scene* scene) {
        if (!scene->sky)
            draw_scene_group(scene, 0x46, index);
    });
    set_pass(0x102);
    issue_occlusion_queries();
    set_render_state(1, 0);
    static_cast<FlexibleParticleManager*>(g_flexible_particle_manager.get())->render_systems(index > 0);
    objects::g_particle_hub->draw_particles();
    g_weather->render(); // (through the jump 0x004A10A0)
    set_render_state(1, 1);
    engine::draw_shatters();
    set_render_state(1, 0);
    if (!g_radeon8500)
        sun::render(static_cast<gfx::ViewCam*>(camera));
    set_render_state(1, 1);
    set_render_state(2, 1);
    render_primitives();
    set_render_state(1, 1);
    set_render_state(2, 1);
    update_glows(false);
    engine::draw_flashes(index, 0);
}

} // namespace

// 0x004B2D80
void draw_world()
{
    // Not original - the level editor (editor/level_editor.h): its frame (flying, picking, dragging) - it runs
    // while the game is paused.
    // Original behaviour: nothing.
    frontend::level_editor::tick_frame();
    ++g_frames_rendered;        // frames rendered
    if (!g_loading_screen_up) { // (nothing while the device is lost)
        g_step_seconds = static_cast<int>(frame_clock::get_time());
        set_render_state(1, 1);
        set_render_state(2, 1);
        render_shadow_textures();
        begin_screen_effects();
        const int count = modules::g_viewport_count;
        // Not original - the wireframe render mode (engine/gfx/wireframe.h): the 3D passes in wireframe when asked.
        // Original behaviour: the render mode (0x006FA734) isn't read.
        wireframe::begin_world();
        view_mode::begin_world(); // (not original either: the editor's view modes - engine/gfx/view_mode.h)
        for (int i = 0; i < count; ++i)
            draw_viewport(i);
        view_mode::end_world();
        wireframe::end_world();

        // Full-screen effects and the 2D on top.
        full_screen_viewport();
        if (g_camera.letterbox_active_) {
            // Originally from PartyMod Fixes (its call at 0x004B33CA): fitted to the aspect wanted. The original:
            //   int screen_height = g_back_buffer_height;
            //   the_viewport().y = screen_height / 8;
            //   the_viewport().height = screen_height - screen_height / 4;
            set_display_region();
        }
        set_viewport();
        copy_screen_quarter();
        draw_screen_blur();
        const bool fog = g_fog_active != 0;
        change_fog(false);
        // Originally from PartyMod Fixes (its call at 0x004B341D): the 2D pass (0x004DFB90 in the original), then black bars over what's
        // drawn outside the fitted area.
        draw_2d_with_black_bars();
        // Not original - the level editor: the hovered and selected objects outlined over everything.
        // Original behaviour: nothing more is drawn.
        frontend::level_editor::blit();
        // Not original either - the new front end ([features] new_frontend, frontend/new_frontend.h): over
        // everything while it's up in the front end.
        frontend::new_frontend::blit();
        change_fog(fog);
    }
    full_screen_viewport();
    set_viewport();
}

} // namespace render

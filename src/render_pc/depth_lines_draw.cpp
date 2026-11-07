// NOT ORIGINAL - the depth-tested lines drawn in the 3D view (depth_lines_draw.h).

#include "render_pc/depth_lines_draw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/gfx/depth_lines.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/render_camera.h"
#include "engine/gfx/renderer.h"
#include "render_pc/render_state.h"

namespace render::depth_lines_draw {

namespace {

constexpr float kPullPixels = 2.0F;    // a line is pulled toward the camera by what this many pixels span at its depth
constexpr float kPullUnits = 0.5F;     // and by this much more
constexpr float kNearMargin = 1.01F;   // a line is cut where it comes nearer than the near plane times this
constexpr float kFringePixels = 1.0F; // a line's smoothed edge, each side
constexpr float kSmallestSide = 1e-6F; // a line seen end on (its side direction this short) isn't drawn
constexpr std::size_t kLinesPerDraw = 1024; // (at most 18 vertices each: three bands of two triangles)
constexpr uint32_t kRgbBits = 0x00FFFFFFU;
constexpr uint32_t kVertexFormat = kFvfXyz | kFvfDiffuse;
constexpr std::size_t kVertexBytes = 16; // XYZ | DIFFUSE
constexpr float kHalf = 0.5F;

// A point.
struct P3 {
    float x{};
    float y{};
    float z{};
};
P3 operator+(P3 a, P3 b) { return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z}; }
P3 operator-(P3 a, P3 b) { return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z}; }
P3 operator*(P3 a, float scale) { return {.x = a.x * scale, .y = a.y * scale, .z = a.z * scale}; }
float dot(P3 a, P3 b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }
P3 cross(P3 a, P3 b)
{
    return {.x = (a.y * b.z) - (a.z * b.y), .y = (a.z * b.x) - (a.x * b.z), .z = (a.x * b.y) - (a.y * b.x)};
}
P3 point_of(const std::array<float, 3>& point)
{
    return {.x = core::at(point, 0), .y = core::at(point, 1), .z = core::at(point, 2)};
}

// The vertex format's vertex: a position and its colour.
struct Vertex {
    float x{};
    float y{};
    float z{};
    uint32_t colour{};
};
static_assert(sizeof(Vertex) == kVertexBytes, "XYZ | DIFFUSE");

// The camera the world was drawn with: its view and projection, where it is, its near plane and what one pixel spans
// at depth 1.
struct Camera {
    maths::Mat4 view{};
    maths::Mat4 projection{};
    P3 eye;
    float near_z{};
    float pixel{};
};

// A point's depth: its clip-space w (the distance along the view, for the perspective the game uses).
float depth_of(const Camera& camera, P3 point)
{
    const maths::Vec4& x_row = core::at(camera.view.row, 0);
    const maths::Vec4& y_row = core::at(camera.view.row, 1);
    const maths::Vec4& z_row = core::at(camera.view.row, 2);
    const maths::Vec4& t_row = core::at(camera.view.row, 3);
    const maths::Vec4 seen = {.x = (point.x * x_row.x) + (point.y * y_row.x) + (point.z * z_row.x) + t_row.x,
                              .y = (point.x * x_row.y) + (point.y * y_row.y) + (point.z * z_row.y) + t_row.y,
                              .z = (point.x * x_row.z) + (point.y * y_row.z) + (point.z * z_row.z) + t_row.z,
                              .w = (point.x * x_row.w) + (point.y * y_row.w) + (point.z * z_row.w) + t_row.w};
    const auto& projection = camera.projection.row;
    return (seen.x * core::at(projection, 0).w) + (seen.y * core::at(projection, 1).w) +
           (seen.z * core::at(projection, 2).w) + (seen.w * core::at(projection, 3).w);
}

// A point moved toward the eye by `distance`.
P3 pulled(const Camera& camera, P3 point, float distance)
{
    const P3 to_eye = camera.eye - point;
    const float length = std::sqrt(dot(to_eye, to_eye));
    return length > distance ? point + (to_eye * (distance / length)) : point;
}

std::vector<Vertex>& vertex_store()
{
    static core::NoDestroy<std::vector<Vertex>> vertices;
    return vertices.get();
}

// A line's triangles added (none when it is wholly behind the near plane or seen end on).
void add_line(const Camera& camera, const depth_lines::Line& line, std::vector<Vertex>* out)
{
    P3 start = point_of(line.from);
    P3 end = point_of(line.to);
    float depth_a = depth_of(camera, start);
    float depth_b = depth_of(camera, end);
    const float near_z = camera.near_z * kNearMargin;
    if (depth_a < near_z && depth_b < near_z) {
        return;
    }
    if (depth_a < near_z) {
        start = start + ((end - start) * ((near_z - depth_a) / (depth_b - depth_a)));
        depth_a = near_z;
    } else if (depth_b < near_z) {
        end = end + ((start - end) * ((near_z - depth_b) / (depth_a - depth_b)));
        depth_b = near_z;
    }
    start = pulled(camera, start, kPullUnits + (kPullPixels * depth_a * camera.pixel));
    end = pulled(camera, end, kPullUnits + (kPullPixels * depth_b * camera.pixel));
    P3 side = cross(end - start, ((start + end) * kHalf) - camera.eye);
    const float side_length = std::sqrt(dot(side, side));
    if (side_length < kSmallestSide) {
        return;
    }
    side = side * (1.0F / side_length);
    // Across the strip: a solid core `width` less a pixel wide, and a pixel's fringe each side fading to nothing (the
    // edges smoothed as ImGui smooths its lines).
    const float inner = std::fmax(line.width - kFringePixels, 0.0F) * kHalf;
    const std::array<float, 4> across = {-(inner + kFringePixels), -inner, inner, inner + kFringePixels};
    const uint32_t clear = line.argb & kRgbBits;
    const std::array<uint32_t, 4> colours = {clear, line.argb, line.argb, clear};
    std::array<Vertex, 4> at_a{};
    std::array<Vertex, 4> at_b{};
    for (std::size_t i = 0; i < across.size(); ++i) {
        const P3 point_a = start + (side * (core::at(across, i) * depth_a * camera.pixel));
        const P3 point_b = end + (side * (core::at(across, i) * depth_b * camera.pixel));
        core::at(at_a, i) = {.x = point_a.x, .y = point_a.y, .z = point_a.z, .colour = core::at(colours, i)};
        core::at(at_b, i) = {.x = point_b.x, .y = point_b.y, .z = point_b.z, .colour = core::at(colours, i)};
    }
    for (std::size_t band = 0; band + 1 < across.size(); ++band) {
        if (band == 1 && inner <= 0.0F) {
            continue; // (no core: a line a pixel wide is its fringes)
        }
        for (const Vertex& vertex : {core::at(at_a, band), core::at(at_a, band + 1), core::at(at_b, band + 1),
                                     core::at(at_a, band), core::at(at_b, band + 1), core::at(at_b, band)}) {
            out->push_back(vertex);
        }
    }
}

// The depth test each kind of line is drawn with.
uint32_t compare_for(depth_lines::Depth depth)
{
    switch (depth) {
    case depth_lines::Depth::kBehind:
        return gpu_value::kCompareGreater;
    case depth_lines::Depth::kInFront:
        return gpu_value::kCompareLessEqual;
    case depth_lines::Depth::kAlways:
        break;
    }
    return gpu_value::kCompareAlways;
}

// The triangles of `lines` (all of one kind) drawn.
void draw_run(const Camera& camera, std::span<const depth_lines::Line> lines)
{
    std::vector<Vertex>& vertices = vertex_store();
    gpu().set_render_state(GpuRenderState::kZFunc, compare_for(lines.front().depth));
    for (std::size_t first = 0; first < lines.size(); first += kLinesPerDraw) {
        vertices.clear();
        for (const depth_lines::Line& line : lines.subspan(first, std::min(kLinesPerDraw, lines.size() - first))) {
            add_line(camera, line, &vertices);
        }
        if (!vertices.empty()) {
            const auto triangles = static_cast<uint32_t>(vertices.size() / 3);
            gpu().draw_from_memory(GpuPrimitive::kTriangleList, triangles, vertices.data(), sizeof(Vertex));
            renderer().count_draw(triangles);
        }
    }
}

// The device's state the lines change directly (not through the renderer's cache), put back after.
struct DirectState {
    uint32_t z_func{};
    uint32_t fill_mode{};
    std::array<uint32_t, 4> stages{};
    std::array<maths::Mat4, 3> transforms{};
};
constexpr std::array<GpuStageState, 4> kStageStates = {GpuStageState::kColourOp, GpuStageState::kColourArg1,
                                                        GpuStageState::kAlphaOp, GpuStageState::kAlphaArg1};
constexpr std::array<uint32_t, 4> kStageValues = {gpu_value::kOpSelectArg1, gpu_value::kArgDiffuse,
                                                  gpu_value::kOpSelectArg1, gpu_value::kArgDiffuse};
constexpr std::array<GpuTransform, 3> kTransforms = {GpuTransform::kWorld, GpuTransform::kView,
                                                      GpuTransform::kProjection};

DirectState save_direct_state()
{
    DirectState saved;
    gpu().get_render_state(GpuRenderState::kZFunc, &saved.z_func);
    gpu().get_render_state(GpuRenderState::kFillMode, &saved.fill_mode);
    for (std::size_t i = 0; i < kStageStates.size(); ++i) {
        gpu().get_stage_state(0, core::at(kStageStates, i), &core::at(saved.stages, i));
    }
    for (std::size_t i = 0; i < kTransforms.size(); ++i) {
        gpu().get_transform(core::at(kTransforms, i), core::at(saved.transforms, i).data());
    }
    return saved;
}

void restore_direct_state(const DirectState& saved)
{
    gpu().set_render_state(GpuRenderState::kZFunc, saved.z_func);
    gpu().set_render_state(GpuRenderState::kFillMode, saved.fill_mode);
    for (std::size_t i = 0; i < kStageStates.size(); ++i) {
        gpu().set_stage_state(0, core::at(kStageStates, i), core::at(saved.stages, i));
    }
    for (std::size_t i = 0; i < kTransforms.size(); ++i) {
        gpu().set_transform(core::at(kTransforms, i), core::at(saved.transforms, i).data());
    }
}

// The renderer's switches the lines change through its cache (the device has what the cache has), put back after.
struct CachedSwitches {
    uint32_t alpha_blend{};
    uint32_t alpha_test{};
    uint32_t z_write{};
    uint32_t cull_mode{};
    uint32_t lighting{};
    uint32_t fog{};
};

CachedSwitches save_switches()
{
    const auto& gfx = renderer();
    CachedSwitches saved{.z_write = gfx.z_write(), .cull_mode = gfx.cull_mode(), .fog = gfx.fog_active()};
    gpu().get_render_state(GpuRenderState::kAlphaBlendEnable, &saved.alpha_blend);
    gpu().get_render_state(GpuRenderState::kAlphaTestEnable, &saved.alpha_test);
    gpu().get_render_state(GpuRenderState::kLighting, &saved.lighting);
    return saved;
}

void restore_switches(const CachedSwitches& saved)
{
    change_phase(RenderStateId::kAlphaBlend, saved.alpha_blend);
    change_phase(RenderStateId::kAlphaTest, saved.alpha_test);
    change_phase(RenderStateId::kZWrite, saved.z_write);
    change_phase(RenderStateId::kCullMode, saved.cull_mode);
    change_phase(RenderStateId::kLighting, saved.lighting);
    change_phase(RenderStateId::kFog, saved.fog);
}

} // namespace

void draw()
{
    const std::span<const depth_lines::Line> lines = depth_lines::pending();
    const auto& gfx = renderer();
    const auto height = static_cast<float>(gfx.device_viewport().height);
    const float vertical_scale = std::fabs(core::at(gfx.projection_matrix().row, 1).y);
    if (lines.empty() || height <= 0.0F || vertical_scale <= 0.0F) {
        depth_lines::clear();
        return;
    }
    const RenderCamera& lens = gfx.render_camera();
    const Camera camera{.view = gfx.view_matrix(),
                        .projection = gfx.projection_matrix(),
                        .eye = point_of(lens.position),
                        .near_z = lens.near_z_,
                        .pixel = 2.0F / (vertical_scale * height)};

    // The renderer's own (cached) state: no texture, no pixel shader, coloured vertices, alpha-blended, no alpha test,
    // depth not written, both sides, unlit, no fog - the switches put back after (the texture, shader, format and blend
    // mode are set by every pass that draws).
    const CachedSwitches switches = save_switches();
    set_texture(0, nullptr);
    set_pixel_shader(nullptr, nullptr);
    set_vertex_format(kVertexFormat);
    choose_blend_mode(kBlendModeAlpha);
    change_phase(RenderStateId::kAlphaBlend, 1);
    change_phase(RenderStateId::kAlphaTest, 0);
    change_phase(RenderStateId::kZWrite, 0);
    change_phase(RenderStateId::kCullMode, gpu_value::kCullNone);
    change_phase(RenderStateId::kLighting, 0);
    change_phase(RenderStateId::kFog, 0);
    // What the cache doesn't know of: the depth test's comparison, the fill (wireframe's), stage 0 taking the vertex
    // colour, the world (the lines are in world space) and the camera's view and projection.
    const DirectState saved = save_direct_state();
    gpu().set_render_state(GpuRenderState::kFillMode, gpu_value::kFillSolid);
    for (std::size_t i = 0; i < kStageStates.size(); ++i) {
        gpu().set_stage_state(0, core::at(kStageStates, i), core::at(kStageValues, i));
    }
    const maths::Mat4 identity{{{{.x = 1, .y = 0, .z = 0, .w = 0},
                                 {.x = 0, .y = 1, .z = 0, .w = 0},
                                 {.x = 0, .y = 0, .z = 1, .w = 0},
                                 {.x = 0, .y = 0, .z = 0, .w = 1}}}};
    gpu().set_transform(GpuTransform::kWorld, identity.data());
    gpu().set_transform(GpuTransform::kView, camera.view.data());
    gpu().set_transform(GpuTransform::kProjection, camera.projection.data());

    // (pending keeps them in Depth's order: a run of each kind)
    std::size_t first = 0;
    while (first < lines.size()) {
        std::size_t end = first + 1;
        while (end < lines.size() && core::at(lines, end).depth == core::at(lines, first).depth) {
            ++end;
        }
        draw_run(camera, lines.subspan(first, end - first));
        first = end;
    }

    restore_direct_state(saved);
    restore_switches(switches);
    depth_lines::clear();
}

void drop() { depth_lines::clear(); }

} // namespace render::depth_lines_draw

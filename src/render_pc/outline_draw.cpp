// NOT ORIGINAL - the outlines a UI handed over drawn in the 3D view (outline_draw.h).

#include "render_pc/outline_draw.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "base/core/checked_index.h"
#include "base/core/down_cast.h"
#include "base/core/enum_ops.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/outline_draws.h"
#include "engine/gfx/renderer.h"
#include "render_pc/geom.h"
#include "render_pc/pc_fog.h"
#include "render_pc/render_state.h"
#include "render_pc/sub_mesh.h"

namespace render::outline_draw {

namespace {

constexpr uint32_t kMeshShownFlag = 0x40;     // a mesh's flags: drawn (as the world pass's)
constexpr uint32_t kMeshKeptOutFlags = 0x210; // the mesh flags that keep a mesh out of every view
constexpr uint32_t kMeshScanLimit = 4096;     // the meshes looked at, at most (the editor's limit)
constexpr uint32_t kNotCulledFlag = 8;        // draw_one_instance's flag: not culled
constexpr uint32_t kAlphaMask = 0xFF000000U;
constexpr uint32_t kLeastAlpha = 0x40000000U; // (times 4 in the edge pass: still "covered")
constexpr uint32_t kColourWriteAll = 0xF;
constexpr uint32_t kWhite = 0xFFFFFFFFU;
// The shown pass nudged towards the camera: a constant (in the depth's 0-1) and by the slope, so the world's own depth
// of the same triangles never hides them (the two passes' positions may round apart).
constexpr float kShownDepthBias = -0.000005F;
constexpr float kShownSlopeBias = -1.0F;
constexpr float kHalfPixel = 0.5F;

// The stroke: the offsets within two pixels (a disc of radius 2).
struct Offset {
    int8_t dx;
    int8_t dy;
};
constexpr std::array<Offset, 12> kStrokeOffsets{{{.dx = 1, .dy = 0},
                                                 {.dx = -1, .dy = 0},
                                                 {.dx = 0, .dy = 1},
                                                 {.dx = 0, .dy = -1},
                                                 {.dx = 1, .dy = 1},
                                                 {.dx = -1, .dy = 1},
                                                 {.dx = 1, .dy = -1},
                                                 {.dx = -1, .dy = -1},
                                                 {.dx = 2, .dy = 0},
                                                 {.dx = -2, .dy = 0},
                                                 {.dx = 0, .dy = 2},
                                                 {.dx = 0, .dy = -2}}};

// The states set on the Gpu itself during the passes (not through the renderer's cache): put back as they were.
constexpr std::array<GpuRenderState, 5> kKeptRenderStates{GpuRenderState::kFillMode, GpuRenderState::kColourWriteEnable,
                                                          GpuRenderState::kTextureFactor, GpuRenderState::kDepthBias,
                                                          GpuRenderState::kSlopeScaleDepthBias};
constexpr std::array<GpuStageState, 8> kKeptStageStates{
    GpuStageState::kColourOp,      GpuStageState::kColourArg1,
    GpuStageState::kColourArg2,    GpuStageState::kAlphaOp,
    GpuStageState::kAlphaArg1,     GpuStageState::kAlphaArg2,
    GpuStageState::kTexCoordIndex, GpuStageState::kTextureTransformFlags};
constexpr uint32_t kKeptStages = 3;

struct Kept {
    std::array<uint32_t, kKeptRenderStates.size()> render{};
    std::array<std::array<uint32_t, kKeptStageStates.size()>, kKeptStages> stages{};
    GpuVertexShader* vertex_shader = nullptr;
    GpuViewport viewport{};
};

// The mask (default pool) and the flat colour's pass.
struct OutlineState {
    GpuTexture* mask = nullptr;
    GpuSurface* mask_surface = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    bool flat = false;
    bool shown_pass = false;
    uint32_t colour = 0;
};

OutlineState& outline_state()
{
    static core::NoDestroy<OutlineState> state;
    return state.get();
}

const PcGeom* pc_geom(const Geom* geom)
{
    return (geom != nullptr && geom->kind() == GeomKind::kPc) ? core::down_cast<const PcGeom>(geom) : nullptr;
}

Kept keep_states()
{
    const GpuStateBlock& block = gpu().state_block();
    Kept kept;
    for (std::size_t i = 0; i < kKeptRenderStates.size(); ++i) {
        core::at(kept.render, i) = block.render_state(core::at(kKeptRenderStates, i));
    }
    for (uint32_t stage = 0; stage < kKeptStages; ++stage) {
        for (std::size_t i = 0; i < kKeptStageStates.size(); ++i) {
            core::at(core::at(kept.stages, stage), i) = block.stage_state(stage, core::at(kKeptStageStates, i));
        }
    }
    kept.vertex_shader = block.vertex_shader;
    kept.viewport = block.viewport;
    return kept;
}

void put_back(const Kept& kept)
{
    for (std::size_t i = 0; i < kKeptRenderStates.size(); ++i) {
        gpu().set_render_state(core::at(kKeptRenderStates, i), core::at(kept.render, i));
    }
    for (uint32_t stage = 0; stage < kKeptStages; ++stage) {
        for (std::size_t i = 0; i < kKeptStageStates.size(); ++i) {
            gpu().set_stage_state(stage, core::at(kKeptStageStates, i), core::at(core::at(kept.stages, stage), i));
        }
    }
    gpu().set_vertex_shader(kept.vertex_shader);
}

// No pixel shader (the renderer's record of it too, so the next shader is sent again).
void no_pixel_shader()
{
    if (renderer().pixel_shader_locked() != nullptr) {
        return;
    }
    gpu().set_pixel_shader(nullptr);
    renderer().set_current_pixel_shader(nullptr);
}

void stage(uint32_t index, uint32_t colour_op, uint32_t colour_arg, uint32_t alpha_op,
           std::pair<uint32_t, uint32_t> alpha_args)
{
    gpu().set_stage_state(index, GpuStageState::kColourOp, colour_op);
    gpu().set_stage_state(index, GpuStageState::kColourArg1, colour_arg);
    gpu().set_stage_state(index, GpuStageState::kAlphaOp, alpha_op);
    gpu().set_stage_state(index, GpuStageState::kAlphaArg1, alpha_args.first);
    gpu().set_stage_state(index, GpuStageState::kAlphaArg2, alpha_args.second);
}

void release_mask()
{
    OutlineState& state = outline_state();
    if (state.mask_surface != nullptr) {
        gpu().release_surface(state.mask_surface);
    }
    if (state.mask != nullptr) {
        gpu().release_texture(state.mask);
    }
    state.mask_surface = nullptr;
    state.mask = nullptr;
    state.width = 0;
    state.height = 0;
}

// The mask at the target's size (made again when it changed).
bool make_mask(uint32_t width, uint32_t height)
{
    OutlineState& state = outline_state();
    if (state.mask != nullptr && state.width == width && state.height == height) {
        return true;
    }
    release_mask();
    if (gpu().create_texture({.width = width,
                              .height = height,
                              .levels = 1,
                              .usage = gpu_value::kUsageRenderTarget,
                              .format = gpu_value::kFormatA8R8G8B8,
                              .pool = gpu_value::kPoolDefault},
                             &state.mask) != gpu_value::kResultOk ||
        state.mask == nullptr || gpu().texture_surface(state.mask, 0, &state.mask_surface) != gpu_value::kResultOk ||
        state.mask_surface == nullptr) {
        release_mask();
        return false;
    }
    state.width = width;
    state.height = height;
    return true;
}

// The target and depth drawn into now, if a mask can be drawn with that depth (neither multisampled, the depth at
// least the target's size). The handles are retained: released by the caller.
struct Targets {
    GpuSurface* colour = nullptr;
    GpuSurface* depth = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};
bool usable_targets(Targets* targets)
{
    gpu().render_target(0, &targets->colour);
    gpu().depth_surface(&targets->depth);
    if (targets->colour == nullptr) {
        return false;
    }
    GpuSurfaceDesc colour{};
    gpu().surface_desc(targets->colour, &colour);
    targets->width = colour.width;
    targets->height = colour.height;
    if (colour.samples != 0 || colour.width == 0 || colour.height == 0) {
        return false;
    }
    if (targets->depth != nullptr) {
        GpuSurfaceDesc depth{};
        gpu().surface_desc(targets->depth, &depth);
        if (depth.samples != 0 || depth.width < colour.width || depth.height < colour.height) {
            return false;
        }
    }
    return true;
}
void release_targets(const Targets& targets)
{
    if (targets.colour != nullptr) {
        gpu().release_surface(targets.colour);
    }
    if (targets.depth != nullptr) {
        gpu().release_surface(targets.depth);
    }
}

// One geometry in the flat colour: its instance's way (where it is, its bones) or the world pass's (its shown meshes).
void draw_flat(Geom* geom)
{
    const PcGeom* pc = pc_geom(geom);
    if (pc == nullptr) {
        return;
    }
    if (Singleton* instance = pc->instance()) {
        draw_one_instance(instance, kNotCulledFlag);
        return;
    }
    if (pc->meshes() == nullptr) {
        return;
    }
    gpu().set_transform(GpuTransform::kWorld, renderer().world_matrix().data());
    set_world_transform(nullptr);
    const std::span<MeshRef*> meshes = pc->mesh_run();
    for (uint32_t i = 0; i < meshes.size() && i < kMeshScanLimit; ++i) {
        MeshRef* mesh = core::at(meshes, i);
        if (mesh == nullptr || (mesh->flags() & kMeshShownFlag) == 0U || (mesh->flags() & kMeshKeptOutFlags) != 0U) {
            continue;
        }
        mesh->render();
    }
}

uint32_t with_least_alpha(uint32_t colour)
{
    return std::max(colour & kAlphaMask, kLeastAlpha) | (colour & ~kAlphaMask);
}

// The mask: cleared in the view, the hidden parts first, then the shown (so a shown colour wins where both are).
void draw_mask(std::span<const outline_draws::Outline> outlines)
{
    OutlineState& state = outline_state();
    gpu().clear(0, nullptr, gpu_value::kClearTarget, 0, 1.0F, 0);
    state.flat = true;
    for (const bool shown : {false, true}) {
        state.shown_pass = shown;
        for (const outline_draws::Outline& outline : outlines) {
            const uint32_t colour = shown ? outline.colour : outline.hidden_colour;
            if ((colour & kAlphaMask) == 0U) {
                continue;
            }
            state.colour = with_least_alpha(colour);
            draw_flat(outline.geom);
        }
    }
    state.flat = false;
}

// The stroke over the target: a quad over the view for each offset.
void draw_stroke(const GpuViewport& view)
{
    const OutlineState& state = outline_state();
    no_pixel_shader();
    gpu().set_vertex_shader(nullptr);
    set_texture(0, state.mask);
    set_texture(1, state.mask);
    for (int sampler = 0; sampler < 2; ++sampler) {
        change_phase(address_mode_state(sampler), kClampBothWays);
        change_phase(static_cast<RenderStateId>(core::to_underlying(RenderStateId::kFilter0) + sampler), 0);
    }
    // stage 0: the mask here - its alpha times 4 ("covered"); stage 1: the mask at the offset - its colour, its alpha
    // times "not covered here"
    stage(0, gpu_value::kOpSelectArg1, gpu_value::kArgTexture, gpu_value::kOpModulate4x,
          {gpu_value::kArgTexture, gpu_value::kArgTextureFactor});
    stage(1, gpu_value::kOpSelectArg1, gpu_value::kArgTexture, gpu_value::kOpModulate,
          {gpu_value::kArgTexture, gpu_value::kArgCurrent | gpu_value::kArgComplement});
    gpu().set_stage_state(0, GpuStageState::kTexCoordIndex, 0);
    gpu().set_stage_state(1, GpuStageState::kTexCoordIndex, 1);
    gpu().set_stage_state(0, GpuStageState::kTextureTransformFlags, gpu_value::kTextureTransformDisable);
    gpu().set_stage_state(1, GpuStageState::kTextureTransformFlags, gpu_value::kTextureTransformDisable);
    gpu().set_stage_state(2, GpuStageState::kColourOp, gpu_value::kOpDisable);
    gpu().set_stage_state(2, GpuStageState::kAlphaOp, gpu_value::kOpDisable);
    gpu().set_render_state(GpuRenderState::kTextureFactor, kWhite);
    gpu().set_render_state(GpuRenderState::kFillMode, gpu_value::kFillSolid);
    gpu().set_render_state(GpuRenderState::kColourWriteEnable, kColourWriteAll);
    gpu().set_render_state(GpuRenderState::kDepthBias, 0);
    gpu().set_render_state(GpuRenderState::kSlopeScaleDepthBias, 0);
    change_phase(RenderStateId::kZWrite, 0);
    change_phase(RenderStateId::kZTest, 0);
    change_phase(RenderStateId::kPhaseAlphaTest, 0);
    change_phase(RenderStateId::kCullMode, gpu_value::kCullNone);
    change_phase(RenderStateId::kSpecular, 0);
    change_phase(RenderStateId::kAlphaBlend, 1);
    choose_blend_mode(kBlendModeAlpha);
    set_vertex_format(kFvfXyzRhw | kFvfDiffuse | kFvfTex2);

    struct StrokeVertex {
        float x, y, z, rhw;
        uint32_t colour;
        float u0, v0, u1, v1;
    };
    const auto width = static_cast<float>(state.width);
    const auto height = static_cast<float>(state.height);
    const auto left = static_cast<float>(view.x);
    const auto top = static_cast<float>(view.y);
    const auto right = static_cast<float>(view.x + view.width);
    const auto bottom = static_cast<float>(view.y + view.height);
    for (const Offset& offset : kStrokeOffsets) {
        const float du = static_cast<float>(offset.dx) / width;
        const float dv = static_cast<float>(offset.dy) / height;
        auto corner = [&](float x, float y) {
            return StrokeVertex{.x = x - kHalfPixel,
                                .y = y - kHalfPixel,
                                .z = 0.0F,
                                .rhw = 1.0F,
                                .colour = kWhite,
                                .u0 = x / width,
                                .v0 = y / height,
                                .u1 = (x / width) + du,
                                .v1 = (y / height) + dv};
        };
        const std::array<StrokeVertex, 4> quad{
            {corner(left, top), corner(right, top), corner(left, bottom), corner(right, bottom)}};
        gpu().draw_from_memory(GpuPrimitive::kTriangleStrip, 2, quad.data(), sizeof(StrokeVertex));
        renderer().count_draw(2);
    }
    set_texture(0, nullptr);
    set_texture(1, nullptr);
}

} // namespace

void draw(bool keep)
{
    const std::span<const outline_draws::Outline> outlines = outline_draws::pending();
    if (outlines.empty()) {
        return;
    }
    Targets targets;
    const bool usable = usable_targets(&targets) && make_mask(targets.width, targets.height);
    outline_draws::note_drawn(usable);
    if (usable) {
        const Kept kept = keep_states();
        GpuMatrix world{};
        gpu().get_transform(GpuTransform::kWorld, world.data());
        const bool fog = renderer().fog_active() != 0U;
        toggle_fog(false);
        gpu().set_render_target(0, outline_state().mask_surface);
        gpu().set_viewport(kept.viewport); // (a new target 0 takes the whole of it)
        draw_mask(outlines);
        gpu().set_render_target(0, targets.colour);
        gpu().set_viewport(kept.viewport);
        draw_stroke(kept.viewport);
        put_back(kept);
        gpu().set_transform(GpuTransform::kWorld, world.data());
        set_world_transform(nullptr);
        toggle_fog(fog);
    }
    release_targets(targets);
    if (!keep) {
        outline_draws::clear();
    }
}

void drop() { outline_draws::clear(); }

void release() { release_mask(); }

bool flat_on() { return outline_state().flat; }

void flat_states()
{
    const OutlineState& state = outline_state();
    no_pixel_shader();
    stage(0, gpu_value::kOpSelectArg1, gpu_value::kArgTextureFactor, gpu_value::kOpSelectArg1,
          {gpu_value::kArgTextureFactor, gpu_value::kArgTextureFactor});
    gpu().set_stage_state(1, GpuStageState::kColourOp, gpu_value::kOpDisable);
    gpu().set_stage_state(1, GpuStageState::kAlphaOp, gpu_value::kOpDisable);
    gpu().set_render_state(GpuRenderState::kTextureFactor, state.colour);
    gpu().set_render_state(GpuRenderState::kFillMode, gpu_value::kFillSolid);
    gpu().set_render_state(GpuRenderState::kColourWriteEnable, kColourWriteAll);
    gpu().set_render_state(GpuRenderState::kDepthBias,
                           state.shown_pass ? std::bit_cast<uint32_t>(kShownDepthBias) : 0U);
    gpu().set_render_state(GpuRenderState::kSlopeScaleDepthBias,
                           state.shown_pass ? std::bit_cast<uint32_t>(kShownSlopeBias) : 0U);
    change_phase(RenderStateId::kZWrite, 0);
    change_phase(RenderStateId::kZTest, state.shown_pass ? 1 : 0);
    change_phase(RenderStateId::kPhaseAlphaTest, 0);
    change_phase(RenderStateId::kCullMode, gpu_value::kCullNone);
    change_phase(RenderStateId::kSpecular, 0);
    change_phase(RenderStateId::kAlphaBlend, 0);
}

} // namespace render::outline_draw

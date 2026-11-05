// The device states for the engine's list of 3D primitives (0x0048E370 draws it between these two), and three thin
// device calls.

#include <cstdint>

#include "render_d3d9/primitive_states.h"

#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/mesh_formats.h"
#include "render_d3d9/render_state.h"

namespace render {

// 0x004B60B0
void set_primitive_states()
{
    choose_blend_mode(kBlendModePrimitive);
    set_vertex_format(kFvfXyz | kFvfTex1);
    set_pixel_shader(nullptr, nullptr);
    gpu().set_stage_state(0, GpuStageState::kColourOp, gpu_value::kOpSelectArg1);
    gpu().set_stage_state(0, GpuStageState::kAlphaOp, gpu_value::kOpSelectArg1);
    change_phase(RenderStateId::kAddressMode0, kClampBothWays); // (clamped both ways)
    change_phase(RenderStateId::kPhaseAlphaTest, 1);
    change_phase(RenderStateId::kZWrite, 1);
    change_phase(RenderStateId::kZTest, 1);
    change_phase(RenderStateId::kCullMode, gpu_value::kCullNone);
    gpu().set_stage_state(0, GpuStageState::kTextureTransformFlags, gpu_value::kTextureTransformDisable);
    gpu().set_stage_state(0, GpuStageState::kTexCoordIndex, 0);
}

// 0x004B5FF0
void reset_primitive_states()
{
    set_texture(0, nullptr); // (the original passes a third 0)
    change_phase(RenderStateId::kCullMode, gpu_value::kCullClockwise);
    gpu().set_stage_state(0, GpuStageState::kColourOp, gpu_value::kOpModulate);
    gpu().set_stage_state(0, GpuStageState::kAlphaOp, gpu_value::kOpModulate);
}

// 0x004B6030
void set_texture_stage_state(uint32_t stage, uint32_t type, uint32_t value)
{
    gpu().set_stage_state(stage, static_cast<GpuStageState>(type), value);
}

// 0x004B6050
void set_transform(uint32_t type, const void* matrix)
{
    gpu().set_transform(static_cast<GpuTransform>(type), static_cast<const float*>(matrix));
}

// 0x004B6070
long set_render_target_and_depth(RenderTargets targets)
{
    auto* const surface = targets.surface;
    auto* const depth = targets.depth;
    long result = 0;
    if (gpu().set_render_target(0, static_cast<GpuSurface*>(surface)) != 0) {
        result = gpu_value::kResultInvalidCall;
    }
    if (gpu().set_depth_surface(static_cast<GpuSurface*>(depth)) != 0) {
        return gpu_value::kResultInvalidCall;
    }
    return result;
}

} // namespace render

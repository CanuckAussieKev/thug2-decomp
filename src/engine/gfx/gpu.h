#pragma once

// NOT ORIGINAL - render::Gpu: the low interface under the PC rendering layer (docs/design/vulkan.md, "A Vulkan
// backend: the shape"). The PC platform layer (src/render_pc: the scenes, meshes, materials and their passes,
// the particles, the screen effects, the 2D) calls it where it called the Direct3D 9 device; each graphics API
// implements it once (Direct3D 9: gpu_d3d9/gpu_d3d9.h; Vulkan later). It is never per vertex: a call per state
// change, per draw batch, per resource made, locked or released, per clear, per frame.
//
// - Numbers: states, values, formats, pools, usages, locks and vertex formats are Direct3D 9's numbers (the
//   original's), named below; Direct3D 9's implementation passes them on unchanged, another API translates them.
// - Resources are explicit: made, locked / unlocked, released through the Gpu, held as the engine's handles
//   (engine/gfx/gpu_handles.h).
// - The state block (GpuStateBlock): every state the Gpu was given, as Direct3D 9 values - the render states, the
//   texture stages, the samplers, the textures, the vertex format, the pixel shader and its constants, the material,
//   the lights, the transforms, the viewport. The Gpu records each one as it passes (the non-virtual setters below),
//   so an API without Direct3D 9's fixed-function pipeline derives its shaders and pipelines from the block at draw
//   time. Like Direct3D 9's device, the stages are sticky: a draw sees what the last calls left.
// - One virtual call per operation, where the device call was (the setters record into the block first, a few stores).
// - Results are HRESULT-style: 0 is success, the Direct3D 9 implementation returns the device's own result.
//
// Installed by app/ with the device (render_device.h); not installed, a call dereferences null.

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/gfx/gpu_handles.h"

namespace render {

// A render state (D3DRENDERSTATETYPE's numbers; Direct3D 9's all fit a byte - and a transform's two).
enum class GpuRenderState : uint8_t {
    kZEnable = 7,
    kFillMode = 8,
    kZWriteEnable = 14,
    kAlphaTestEnable = 15,
    kSrcBlend = 19,
    kDestBlend = 20,
    kCullMode = 22,
    kZFunc = 23,
    kAlphaRef = 24,
    kAlphaFunc = 25,
    kDitherEnable = 26,
    kAlphaBlendEnable = 27,
    kFogEnable = 28,
    kSpecularEnable = 29,
    kFogColour = 34,
    kFogTableMode = 35,
    kFogStart = 36,
    kFogEnd = 37,
    kFogDensity = 38,
    kRangeFogEnable = 48,
    kStencilEnable = 52,
    kTextureFactor = 60,
    kLighting = 137,
    kAmbient = 139,
    kFogVertexMode = 140,
    kColourVertex = 141,
    kLocalViewer = 142,
    kNormalizeNormals = 143,
    kDiffuseMaterialSource = 145,
    kSpecularMaterialSource = 146,
    kAmbientMaterialSource = 147,
    kEmissiveMaterialSource = 148,
    kPointSize = 154,
    kPointSizeMin = 155,
    kPointSpriteEnable = 156,
    kPointScaleEnable = 157,
    kPointScaleA = 158,
    kPointScaleB = 159,
    kPointScaleC = 160,
    kMultisampleAntialias = 161,
    kMultisampleMask = 162,
    kPointSizeMax = 166,
    kColourWriteEnable = 168,
    kBlendOp = 171,
    kScissorTestEnable = 174,
    kBlendFactor = 193,
    kDepthBias = 195,
    kSeparateAlphaBlendEnable = 206,
};
// A texture stage's state (D3DTEXTURESTAGESTATETYPE).
enum class GpuStageState : uint8_t {
    kColourOp = 1,
    kColourArg1 = 2,
    kColourArg2 = 3,
    kAlphaOp = 4,
    kAlphaArg1 = 5,
    kAlphaArg2 = 6,
    kBumpMatrix00 = 7,
    kBumpMatrix01 = 8,
    kBumpMatrix10 = 9,
    kBumpMatrix11 = 10,
    kTexCoordIndex = 11,
    kTextureTransformFlags = 24,
    kColourArg0 = 26,
    kAlphaArg0 = 27,
    kResultArg = 28,
    kConstant = 32,
};
// A sampler's state (D3DSAMPLERSTATETYPE).
enum class GpuSamplerState : uint8_t {
    kAddressU = 1,
    kAddressV = 2,
    kAddressW = 3,
    kBorderColour = 4,
    kMagFilter = 5,
    kMinFilter = 6,
    kMipFilter = 7,
    kMipLodBias = 8,
    kMaxMipLevel = 9,
    kMaxAnisotropy = 10,
};
// A transform (D3DTRANSFORMSTATETYPE): the texture stages' are kTexture0 + stage.
enum class GpuTransform : uint16_t {
    kView = 2,
    kProjection = 3,
    kTexture0 = 16,
    kWorld = 256,
};
// What a draw's vertices make (D3DPRIMITIVETYPE).
enum class GpuPrimitive : uint8_t {
    kPointList = 1,
    kLineList = 2,
    kLineStrip = 3,
    kTriangleList = 4,
    kTriangleStrip = 5,
    kTriangleFan = 6,
};

// The values the states take (Direct3D 9's numbers).
namespace gpu_value {
// a texture stage's operation (D3DTEXTUREOP)
inline constexpr uint32_t kOpDisable = 1;
inline constexpr uint32_t kOpSelectArg1 = 2;
inline constexpr uint32_t kOpSelectArg2 = 3;
inline constexpr uint32_t kOpModulate = 4;
inline constexpr uint32_t kOpModulate2x = 5;
inline constexpr uint32_t kOpModulate4x = 6;
// a texture stage's argument (D3DTA_*): a source, with the two modifier bits
inline constexpr uint32_t kArgDiffuse = 0;
inline constexpr uint32_t kArgCurrent = 1;
inline constexpr uint32_t kArgTexture = 2;
inline constexpr uint32_t kArgTextureFactor = 3;
inline constexpr uint32_t kArgTemp = 5;
inline constexpr uint32_t kArgConstant = 6;
inline constexpr uint32_t kArgComplement = 0x10;
inline constexpr uint32_t kArgAlphaReplicate = 0x20;
// the texture coordinate index's generation mode (the high half): the camera-space reflection vector
inline constexpr uint32_t kTexGenCameraReflection = 0x30000;
// the texture transform flags (D3DTEXTURETRANSFORMFLAGS)
inline constexpr uint32_t kTextureTransformDisable = 0;
inline constexpr uint32_t kTextureTransformCount2 = 2;
inline constexpr uint32_t kTextureTransformProjected = 0x100;
// blend factors (D3DBLEND) and operations (D3DBLENDOP)
inline constexpr uint32_t kBlendZero = 1;
inline constexpr uint32_t kBlendOne = 2;
inline constexpr uint32_t kBlendSrcColour = 3;
inline constexpr uint32_t kBlendSrcAlpha = 5;
inline constexpr uint32_t kBlendInvSrcAlpha = 6;
inline constexpr uint32_t kBlendDestColour = 9;
inline constexpr uint32_t kBlendFactor = 14;
inline constexpr uint32_t kBlendInvFactor = 15;
inline constexpr uint32_t kBlendOpAdd = 1;
inline constexpr uint32_t kBlendOpRevSubtract = 3;
// comparisons (D3DCMPFUNC)
inline constexpr uint32_t kCompareNever = 1;
inline constexpr uint32_t kCompareLess = 2;
inline constexpr uint32_t kCompareLessEqual = 4;
inline constexpr uint32_t kCompareGreaterEqual = 7;
inline constexpr uint32_t kCompareAlways = 8;
// culling (D3DCULL), fill (D3DFILLMODE)
inline constexpr uint32_t kCullNone = 1;
inline constexpr uint32_t kCullClockwise = 2;
inline constexpr uint32_t kCullCounterClockwise = 3;
inline constexpr uint32_t kFillWireframe = 2;
inline constexpr uint32_t kFillSolid = 3;
// fog modes (D3DFOGMODE)
inline constexpr uint32_t kFogNone = 0;
inline constexpr uint32_t kFogLinear = 3;
// a lit colour's source (D3DMATERIALCOLORSOURCE)
inline constexpr uint32_t kSourceMaterial = 0;
inline constexpr uint32_t kSourceColour1 = 1;
inline constexpr uint32_t kSourceColour2 = 2;
// sampler filters (D3DTEXTUREFILTERTYPE) and addressing (D3DTEXTUREADDRESS)
inline constexpr uint32_t kFilterNone = 0;
inline constexpr uint32_t kFilterPoint = 1;
inline constexpr uint32_t kFilterLinear = 2;
inline constexpr uint32_t kFilterAnisotropic = 3;
inline constexpr uint32_t kAddressWrap = 1;
inline constexpr uint32_t kAddressClamp = 3;
inline constexpr uint32_t kAddressBorder = 4;
// what a clear clears (D3DCLEAR_*)
inline constexpr uint32_t kClearTarget = 1;
inline constexpr uint32_t kClearDepth = 2;
inline constexpr uint32_t kClearStencil = 4;
// a light's kind (D3DLIGHTTYPE)
inline constexpr uint32_t kLightDirectional = 3;
// a buffer's usage (D3DUSAGE_*), its pool (D3DPOOL_*), an index buffer's format (D3DFMT_INDEX16) and a lock's flags
// (D3DLOCK_*)
inline constexpr uint32_t kUsageWriteOnly = 0x8;
inline constexpr uint32_t kUsageDynamic = 0x200;
inline constexpr uint32_t kPoolDefault = 0;
inline constexpr uint32_t kFormatIndex16 = 101;
inline constexpr uint32_t kLockReadOnly = 0x10;
inline constexpr uint32_t kLockDiscard = 0x2000;
inline constexpr uint32_t kLockNoOverwrite = 0x1000;
// a texture's or surface's format (D3DFORMAT): colour, paletted (P8: 8-bit indices through a 256-colour palette, which
// has no Vulkan format - a Vulkan Gpu expands it to RGBA at upload), compressed (the four characters, little-endian) and
// depth
inline constexpr uint32_t kFormatA8R8G8B8 = 21;
inline constexpr uint32_t kFormatX8R8G8B8 = 22;
inline constexpr uint32_t kFormatR5G6B5 = 23;
inline constexpr uint32_t kFormatX1R5G5B5 = 24;
inline constexpr uint32_t kFormatA1R5G5B5 = 25;
inline constexpr uint32_t kFormatP8 = 41;
inline constexpr uint32_t kFormatD16 = 80;
inline constexpr uint32_t kFormatD24X8 = 77;
inline constexpr uint32_t kFormatD24S8 = 75;
inline constexpr uint32_t kFormatDxt1 = 0x31545844;
inline constexpr uint32_t kFormatDxt3 = 0x33545844;
inline constexpr uint32_t kFormatDxt5 = 0x35545844;
// a resource's pool: in video memory (lost with the device), managed (kept in system memory too), system memory only,
// scratch (never given to the device); a texture drawn into
inline constexpr uint32_t kPoolManaged = 1;
inline constexpr uint32_t kPoolSystemMemory = 2;
inline constexpr uint32_t kPoolScratch = 3;
inline constexpr uint32_t kUsageRenderTarget = 1;
// a call's result: success, and the answer "not yet" (S_FALSE: a query whose count is not ready)
inline constexpr long kResultOk = 0;
inline constexpr long kResultNotReady = 1;
// a call's result: the device is lost (D3DERR_DEVICELOST: test_cooperative_level), and a thing not found
// (D3DERR_NOTFOUND: a query that was never begun)
inline constexpr long kResultDeviceLost = static_cast<long>(0x88760868U);
inline constexpr long kResultNotFound = static_cast<long>(0x88760866U);
// a call's result: an argument the API refused (D3DERR_INVALIDCALL)
inline constexpr long kResultInvalidCall = static_cast<long>(0x8876086CU);
} // namespace gpu_value

// A colour as four floats (D3DCOLORVALUE: red, green, blue, alpha).
using GpuColour = std::array<float, 4>;
// The fixed-function material (D3DMATERIAL9's layout).
struct GpuMaterial {
    GpuColour diffuse{};
    GpuColour ambient{};
    GpuColour specular{};
    GpuColour emissive{};
    float power{};
};
// A fixed-function light (D3DLIGHT9's layout).
struct GpuLight {
    uint32_t type{};
    GpuColour diffuse{};
    GpuColour specular{};
    GpuColour ambient{};
    std::array<float, 3> position{};
    std::array<float, 3> direction{};
    float range{};
    float falloff{};
    float attenuation0{};
    float attenuation1{};
    float attenuation2{};
    float theta{};
    float phi{};
};
// The viewport (D3DVIEWPORT9's layout): pixels, and the depth range.
struct GpuViewport {
    uint32_t x{};
    uint32_t y{};
    uint32_t width{};
    uint32_t height{};
    float min_z{};
    float max_z{};
};
// A rectangle in pixels, right / bottom exclusive (RECT's and D3DRECT's layout).
struct GpuRect {
    int32_t left{};
    int32_t top{};
    int32_t right{};
    int32_t bottom{};
};
// A texture level's size in pixels.
struct GpuSize {
    uint32_t width{};
    uint32_t height{};
};
// A texture level's size and format.
struct GpuLevelDesc {
    uint32_t width{};
    uint32_t height{};
    uint32_t format{};
};
// A locked texture level (D3DLOCKED_RECT's layout): the bytes between rows and the first byte.
struct GpuLockedRect {
    int32_t pitch{};
    void* bits{};
};
// A texture to make (CreateTexture's arguments: Direct3D 9's usage, format and pool numbers; levels 0: the whole chain
// of smaller levels down to 1x1).
struct GpuTextureDesc {
    uint32_t width{};
    uint32_t height{};
    uint32_t levels{};
    uint32_t usage{};
    uint32_t format{};
    uint32_t pool{};
};
// A surface to draw into (CreateRenderTarget's / CreateDepthStencilSurface's arguments).
struct GpuSurfaceDesc {
    uint32_t width{};
    uint32_t height{};
    uint32_t format{};
    uint32_t samples{}; // D3DMULTISAMPLE_TYPE: 0 none, else the sample count
    uint32_t quality{};
    bool lockable_or_discard{}; // a render target: lockable; a depth surface: its contents may be discarded
};
// The gamma ramp (D3DGAMMARAMP's layout: 256 steps of red, green, blue).
inline constexpr std::size_t kGpuGammaSteps = 256;
using GpuGammaRamp = std::array<std::array<uint16_t, kGpuGammaSteps>, 3>;

inline constexpr std::size_t kGpuRenderStateCount = 210; // D3DRS_ numbers 0-209
inline constexpr std::size_t kGpuStageCount = 8;
inline constexpr std::size_t kGpuStageStateCount = 33; // D3DTSS_ numbers 0-32
inline constexpr std::size_t kGpuSamplerCount = 16;
inline constexpr std::size_t kGpuSamplerStateCount = 14; // D3DSAMP_ numbers 0-13
inline constexpr std::size_t kGpuPixelConstantCount = 8; // ps.1.1's c0-c7
inline constexpr std::size_t kGpuVertexConstantCount = 8; // the vertex shader constants kept: c0-c7 (the billboards' c0-c6)
inline constexpr std::size_t kGpuLightCount = 8;
inline constexpr std::size_t kGpuMatrixFloats = 16;
inline constexpr std::size_t kGpuTransformCount = 3 + kGpuStageCount; // world, view, projection, the stages'

using GpuMatrix = std::array<float, kGpuMatrixFloats>;

// The state block: what the Gpu was last given (Direct3D 9's values), starting from Direct3D 9's defaults.
struct GpuStateBlock {
    std::array<uint32_t, kGpuRenderStateCount> render_states{};
    std::array<std::array<uint32_t, kGpuStageStateCount>, kGpuStageCount> stage_states{};
    std::array<std::array<uint32_t, kGpuSamplerStateCount>, kGpuSamplerCount> sampler_states{};
    std::array<GpuTexture*, kGpuSamplerCount> textures{};
    uint32_t vertex_format{}; // D3DFVF bits
    GpuPixelShader* pixel_shader{};
    std::array<std::array<float, 4>, kGpuPixelConstantCount> pixel_constants{};
    GpuVertexShader* vertex_shader{};
    std::array<std::array<float, 4>, kGpuVertexConstantCount> vertex_constants{};
    GpuMaterial material{};
    std::array<GpuLight, kGpuLightCount> lights{};
    uint32_t lights_enabled{}; // bit n: light n on
    std::array<GpuMatrix, kGpuTransformCount> transforms{}; // world, view, projection, texture 0-7
    GpuViewport viewport{};

    // Direct3D 9's defaults for a new or reset device (its documentation's tables).
    void reset();
    [[nodiscard]] uint32_t render_state(GpuRenderState state) const;
    [[nodiscard]] uint32_t stage_state(uint32_t stage, GpuStageState state) const;
    [[nodiscard]] uint32_t sampler_state(uint32_t sampler, GpuSamplerState state) const;
    // The lights switched on.
    [[nodiscard]] int enabled_light_count() const;

    // Recording (Gpu's setters); numbers outside the block are passed on but not kept.
    void record_render_state(GpuRenderState state, uint32_t value);
    void record_stage_state(uint32_t stage, GpuStageState state, uint32_t value);
    void record_sampler_state(uint32_t sampler, GpuSamplerState state, uint32_t value);
    void record_texture(uint32_t stage, GpuTexture* texture);
    void record_pixel_constants(uint32_t first, const float* values, uint32_t count);
    void record_vertex_constants(uint32_t first, const float* values, uint32_t count);
    void record_light(uint32_t index, const GpuLight& light);
    void record_light_enabled(uint32_t index, bool on);
    void record_transform(GpuTransform transform, const float* matrix);
};

// The device's state saved (Gpu::save_state), to be put back (restore_state) and released: the state block as it was and
// the API's own record of it (Direct3D 9: a state block object).
struct GpuSavedState {
    GpuStateBlock block;
    void* api{};
};

class Gpu {
public:
    Gpu(const Gpu&) = delete;
    Gpu& operator=(const Gpu&) = delete;
    Gpu(Gpu&&) = delete;
    Gpu& operator=(Gpu&&) = delete;

    // The state block (what this Gpu was given), and its reset to a new device's defaults.
    [[nodiscard]] const GpuStateBlock& state_block() const { return block_; }
    void reset_state_block() { block_.reset(); }

    // Every state saved, and put back later (Direct3D 9's whole-device state block). The state block follows. Release the
    // saved state when done.
    long save_state(GpuSavedState* saved)
    {
        saved->block = block_;
        return send_save_state(&saved->api);
    }
    long restore_state(const GpuSavedState& saved)
    {
        block_ = saved.block;
        return send_restore_state(saved.api);
    }
    uint32_t release_saved_state(GpuSavedState* saved) { return send_release_saved_state(saved->api); }

    // States (recorded, then passed on).
    long set_render_state(GpuRenderState state, uint32_t value)
    {
        block_.record_render_state(state, value);
        return send_render_state(state, value);
    }
    long set_stage_state(uint32_t stage, GpuStageState state, uint32_t value)
    {
        block_.record_stage_state(stage, state, value);
        return send_stage_state(stage, state, value);
    }
    long set_sampler_state(uint32_t sampler, GpuSamplerState state, uint32_t value)
    {
        block_.record_sampler_state(sampler, state, value);
        return send_sampler_state(sampler, state, value);
    }
    long set_texture(uint32_t stage, GpuTexture* texture)
    {
        block_.record_texture(stage, texture);
        return send_texture(stage, texture);
    }
    long set_vertex_format(uint32_t fvf)
    {
        block_.vertex_format = fvf;
        return send_vertex_format(fvf);
    }
    long set_pixel_shader(GpuPixelShader* shader)
    {
        block_.pixel_shader = shader;
        return send_pixel_shader(shader);
    }
    // `count` four-float constants from constant `first`.
    long set_pixel_constants(uint32_t first, const float* values, uint32_t count)
    {
        block_.record_pixel_constants(first, values, count);
        return send_pixel_constants(first, values, count);
    }
    // The vertex shader (null: the fixed-function pipeline) and its constants, like the pixel shader's.
    long set_vertex_shader(GpuVertexShader* shader)
    {
        block_.vertex_shader = shader;
        return send_vertex_shader(shader);
    }
    long set_vertex_constants(uint32_t first, const float* values, uint32_t count)
    {
        block_.record_vertex_constants(first, values, count);
        return send_vertex_constants(first, values, count);
    }
    long set_material(const GpuMaterial& material)
    {
        block_.material = material;
        return send_material(material);
    }
    long set_light(uint32_t index, const GpuLight& light)
    {
        block_.record_light(index, light);
        return send_light(index, light);
    }
    long enable_light(uint32_t index, bool on)
    {
        block_.record_light_enabled(index, on);
        return send_light_enabled(index, on);
    }
    // A 4x4 matrix, rows of four floats (D3DMATRIX's layout).
    long set_transform(GpuTransform transform, const float* matrix)
    {
        block_.record_transform(transform, matrix);
        return send_transform(transform, matrix);
    }
    long set_viewport(const GpuViewport& viewport)
    {
        block_.viewport = viewport;
        return send_viewport(viewport);
    }
    // What the API holds now (Direct3D 9: the device's Get* calls).
    virtual long get_render_state(GpuRenderState state, uint32_t* value) = 0;
    virtual long get_stage_state(uint32_t stage, GpuStageState state, uint32_t* value) = 0;
    virtual long get_sampler_state(uint32_t sampler, GpuSamplerState state, uint32_t* value) = 0;
    virtual long get_transform(GpuTransform transform, float* matrix) = 0;
    virtual long get_viewport(GpuViewport* viewport) = 0;
    // The vertex format, the shaders and the index buffer the API holds. The shaders and the index buffer come back
    // with a reference the caller releases (Direct3D 9 adds one; there is no release for a shader yet - a caller that
    // gives one back unreleased leaks it, as the original did).
    virtual long get_vertex_format(uint32_t* fvf) = 0;
    virtual long get_pixel_shader(GpuPixelShader** shader) = 0;
    virtual long get_vertex_shader(GpuVertexShader** shader) = 0;
    virtual long get_indices(GpuIndexBuffer** buffer) = 0;

    // Textures: made (into *texture), a level locked / unlocked, a level's surface (a reference the caller releases),
    // released.
    virtual long create_texture(const GpuTextureDesc& desc, GpuTexture** texture) = 0;
    virtual long lock_texture(GpuTexture* texture, uint32_t level, GpuLockedRect* locked, const GpuRect* area,
                              uint32_t flags) = 0;
    virtual long unlock_texture(GpuTexture* texture, uint32_t level) = 0;
    virtual long texture_surface(GpuTexture* texture, uint32_t level, GpuSurface** surface) = 0;
    // A texture level's size (Direct3D 9: GetLevelDesc).
    virtual long texture_level_size(GpuTexture* texture, uint32_t level, GpuSize* size) = 0;
    // A texture level's size and format (GetLevelDesc), and how many levels it has (GetLevelCount).
    virtual long texture_level_desc(GpuTexture* texture, uint32_t level, GpuLevelDesc* desc) = 0;
    virtual uint32_t texture_level_count(GpuTexture* texture) = 0;
    virtual uint32_t release_texture(GpuTexture* texture) = 0;
    // Vertex and index buffers (bytes; Direct3D 9's usage and pool; an index buffer's format: 16 or 32-bit indices).
    virtual long create_vertex_buffer(uint32_t bytes, uint32_t usage, uint32_t fvf, uint32_t pool,
                                      GpuVertexBuffer** buffer) = 0;
    virtual long lock_vertex_buffer(GpuVertexBuffer* buffer, uint32_t offset, uint32_t bytes, void** data,
                                    uint32_t flags) = 0;
    virtual long unlock_vertex_buffer(GpuVertexBuffer* buffer) = 0;
    virtual uint32_t release_vertex_buffer(GpuVertexBuffer* buffer) = 0;
    // One more reference to a vertex buffer (a share of an arena's holds it; released with release_vertex_buffer);
    // returns the count.
    virtual uint32_t retain_vertex_buffer(GpuVertexBuffer* buffer) = 0;
    virtual long create_index_buffer(uint32_t bytes, uint32_t usage, uint32_t format, uint32_t pool,
                                     GpuIndexBuffer** buffer) = 0;
    virtual long lock_index_buffer(GpuIndexBuffer* buffer, uint32_t offset, uint32_t bytes, void** data,
                                   uint32_t flags) = 0;
    virtual long unlock_index_buffer(GpuIndexBuffer* buffer) = 0;
    virtual uint32_t release_index_buffer(GpuIndexBuffer* buffer) = 0;
    // Surfaces drawn into: render targets and depth surfaces made, set (index 0 is the main target), read back
    // (references the caller releases), copied (StretchRect's filter), a render target read into a system-memory
    // surface, released.
    //
    // For an API with render passes (Vulkan): a pass is not an operation here - the pass is inferred. set_render_target /
    // set_depth_surface name the attachments the next draws and clears go into; the pass on the old attachments ends
    // (and a new one opens, lazily, at the first draw or clear) at the next set_render_target / set_depth_surface /
    // copy_surface / read_render_target / present, or when a texture that is an attachment of the open pass is bound
    // with set_texture (it is then sampled: the pass ends and the image changes layout to shader-read; it goes
    // back to attachment when it is set as the target again). A screen copy is therefore both: a texture the passes
    // read and a surface (texture_surface, level 0) set as the target; the surface the caller holds between
    // texture_surface and release_surface is only a name for that image. clear() at the start of a pass is the pass's
    // clear (load-op clear), anywhere else a clear inside it. A depth surface of null leaves the pass without depth.
    virtual long create_render_target(const GpuSurfaceDesc& desc, GpuSurface** surface) = 0;
    virtual long create_depth_surface(const GpuSurfaceDesc& desc, GpuSurface** surface) = 0;
    virtual long create_offscreen_surface(uint32_t width, uint32_t height, uint32_t format, uint32_t pool,
                                          GpuSurface** surface) = 0;
    virtual long set_render_target(uint32_t index, GpuSurface* surface) = 0;
    virtual long set_depth_surface(GpuSurface* surface) = 0;
    virtual long render_target(uint32_t index, GpuSurface** surface) = 0;
    virtual long depth_surface(GpuSurface** surface) = 0;
    virtual long copy_surface(GpuSurface* source, const GpuRect* source_area, GpuSurface* target,
                              const GpuRect* target_area, uint32_t filter) = 0;
    virtual long read_render_target(GpuSurface* target, GpuSurface* system_copy) = 0;
    virtual uint32_t release_surface(GpuSurface* surface) = 0;
    // A surface's size, format and sample count (GetDesc; a reference's description, not the surface's contents).
    virtual long surface_desc(GpuSurface* surface, GpuSurfaceDesc* desc) = 0;
    // A surface's pixels locked for reading or writing / unlocked (a system-memory copy, a texture's level).
    virtual long lock_surface(GpuSurface* surface, GpuLockedRect* locked, const GpuRect* area, uint32_t flags) = 0;
    virtual long unlock_surface(GpuSurface* surface) = 0;
    // The swap chain's back buffer (a reference the caller releases), the size the display is showing (the desktop's
    // when windowed) and the screen as shown, read into a system-memory surface of that size (GetFrontBufferData: the
    // front buffer; a Vulkan Gpu reads back the last presented image).
    virtual long back_buffer(GpuSurface** surface) = 0;
    virtual long display_size(GpuSize* size) = 0;
    virtual long read_front_buffer(GpuSurface* system_copy) = 0;
    // A pixel shader made from its ps.1.1 assembler text, `length` characters (Direct3D 9 assembles it with D3DX and makes
    // the shader from the tokens; another API keeps the text or the program it stands for). The result is the assembly's
    // when that failed (no shader made), else the creation's.
    virtual long create_pixel_shader(const char* source, uint32_t length, GpuPixelShader** shader) = 0;
    virtual uint32_t release_pixel_shader(GpuPixelShader* shader) = 0;
    // Occlusion queries: made, begun / ended around the draws counted, their pixel count asked (S_FALSE, 1, while not
    // ready), released. For an API with query pools: create_occlusion_query takes a slot of the frame's pool,
    // begin_query / end_query bracket the draws (inside one pass), the result is read back a frame or more later
    // (query_result answers 1, not ready, until then) and release_query returns the slot.
    virtual long create_occlusion_query(GpuQuery** query) = 0;
    virtual long begin_query(GpuQuery* query) = 0;
    virtual long end_query(GpuQuery* query) = 0;
    virtual long query_result(GpuQuery* query, void* data, uint32_t bytes, uint32_t flags) = 0;
    virtual uint32_t release_query(GpuQuery* query) = 0;

    // Draws: the buffers they read, then a batch - from the buffers (indexed or not) or from memory (the vertices,
    // and indices, copied for this draw).
    virtual long set_stream_source(uint32_t stream, GpuVertexBuffer* buffer, uint32_t offset, uint32_t stride) = 0;
    virtual long set_indices(GpuIndexBuffer* buffer) = 0;
    // The vertex buffer a stream holds now, with its offset and stride (a reference the caller releases, or null).
    virtual long get_stream_source(uint32_t stream, GpuVertexBuffer** buffer, uint32_t* offset, uint32_t* stride) = 0;
    virtual long draw(GpuPrimitive primitive, uint32_t first_vertex, uint32_t count) = 0;
    virtual long draw_indexed(GpuPrimitive primitive, int32_t base_vertex, uint32_t min_index, uint32_t vertex_count,
                              uint32_t first_index, uint32_t count) = 0;
    virtual long draw_from_memory(GpuPrimitive primitive, uint32_t count, const void* vertices, uint32_t stride) = 0;
    virtual long draw_indexed_from_memory(GpuPrimitive primitive, uint32_t min_index, uint32_t vertex_count,
                                          uint32_t count, const void* indices, uint32_t index_format,
                                          const void* vertices, uint32_t stride) = 0;

    // The frame: a scene begun / ended, the target cleared (rectangles or the whole viewport; gpu_value::kClear*),
    // shown, the gamma ramp, whether the device can draw (0) or is lost.
    virtual long begin_scene() = 0;
    virtual long end_scene() = 0;
    virtual long clear(uint32_t rect_count, const GpuRect* rects, uint32_t flags, uint32_t colour, float z,
                       uint32_t stencil) = 0;
    virtual long present() = 0;
    virtual void set_gamma_ramp(const GpuGammaRamp& ramp) = 0;
    virtual long test_cooperative_level() = 0;

protected:
    Gpu() = default;
    ~Gpu() = default;

    // The API's side of each setter above.
    virtual long send_render_state(GpuRenderState state, uint32_t value) = 0;
    virtual long send_stage_state(uint32_t stage, GpuStageState state, uint32_t value) = 0;
    virtual long send_sampler_state(uint32_t sampler, GpuSamplerState state, uint32_t value) = 0;
    virtual long send_texture(uint32_t stage, GpuTexture* texture) = 0;
    virtual long send_vertex_format(uint32_t fvf) = 0;
    virtual long send_pixel_shader(GpuPixelShader* shader) = 0;
    virtual long send_pixel_constants(uint32_t first, const float* values, uint32_t count) = 0;
    virtual long send_vertex_shader(GpuVertexShader* shader) = 0;
    virtual long send_vertex_constants(uint32_t first, const float* values, uint32_t count) = 0;
    virtual long send_material(const GpuMaterial& material) = 0;
    virtual long send_light(uint32_t index, const GpuLight& light) = 0;
    virtual long send_light_enabled(uint32_t index, bool on) = 0;
    virtual long send_transform(GpuTransform transform, const float* matrix) = 0;
    virtual long send_viewport(const GpuViewport& viewport) = 0;
    virtual long send_save_state(void** api) = 0;
    virtual long send_restore_state(void* api) = 0;
    virtual uint32_t send_release_saved_state(void* api) = 0;

public:
    // A surface's pixels written to a JPEG file (the screen shot script command's front buffer: Direct3D 9 saves it with
    // D3DX, another API reads the pixels back and encodes them itself). (Declared last: the operations above keep their
    // places in the table of virtual functions.)
    virtual long save_surface_jpeg(const char* file, GpuSurface* surface) = 0;

private:
    GpuStateBlock block_;
};

// The Gpu app/ installed (with the device).
Gpu& gpu();
void install_gpu(Gpu& gpu);

} // namespace render

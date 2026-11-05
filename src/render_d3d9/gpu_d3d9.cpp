// NOT ORIGINAL - Direct3D 9's render::Gpu (gpu_d3d9.h): every operation is one device call (or one call on the
// object it names), with the arguments as they come - the numbers are Direct3D 9's own. The state block's recording
// is the interface's (engine/gfx/gpu.h); nothing here skips, reorders or adds a call.

#include "render_d3d9/gpu_d3d9.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstddef>
#include <cstdint>

#include "base/core/enum_ops.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/mesh_formats.h"
#include "render_d3d9/d3d9_handles.h"
#include "render_d3d9/d3d9_state.h"

namespace render::d3d9 {

namespace {

// Gpu's numbers are Direct3D 9's.
static_assert(core::to_underlying(GpuRenderState::kZEnable) == D3DRS_ZENABLE);
static_assert(core::to_underlying(GpuRenderState::kFillMode) == D3DRS_FILLMODE);
static_assert(core::to_underlying(GpuRenderState::kZWriteEnable) == D3DRS_ZWRITEENABLE);
static_assert(core::to_underlying(GpuRenderState::kAlphaTestEnable) == D3DRS_ALPHATESTENABLE);
static_assert(core::to_underlying(GpuRenderState::kSrcBlend) == D3DRS_SRCBLEND);
static_assert(core::to_underlying(GpuRenderState::kDestBlend) == D3DRS_DESTBLEND);
static_assert(core::to_underlying(GpuRenderState::kCullMode) == D3DRS_CULLMODE);
static_assert(core::to_underlying(GpuRenderState::kZFunc) == D3DRS_ZFUNC);
static_assert(core::to_underlying(GpuRenderState::kAlphaRef) == D3DRS_ALPHAREF);
static_assert(core::to_underlying(GpuRenderState::kAlphaFunc) == D3DRS_ALPHAFUNC);
static_assert(core::to_underlying(GpuRenderState::kDitherEnable) == D3DRS_DITHERENABLE);
static_assert(core::to_underlying(GpuRenderState::kAlphaBlendEnable) == D3DRS_ALPHABLENDENABLE);
static_assert(core::to_underlying(GpuRenderState::kFogEnable) == D3DRS_FOGENABLE);
static_assert(core::to_underlying(GpuRenderState::kSpecularEnable) == D3DRS_SPECULARENABLE);
static_assert(core::to_underlying(GpuRenderState::kFogColour) == D3DRS_FOGCOLOR);
static_assert(core::to_underlying(GpuRenderState::kFogTableMode) == D3DRS_FOGTABLEMODE);
static_assert(core::to_underlying(GpuRenderState::kFogStart) == D3DRS_FOGSTART);
static_assert(core::to_underlying(GpuRenderState::kFogEnd) == D3DRS_FOGEND);
static_assert(core::to_underlying(GpuRenderState::kFogDensity) == D3DRS_FOGDENSITY);
static_assert(core::to_underlying(GpuRenderState::kRangeFogEnable) == D3DRS_RANGEFOGENABLE);
static_assert(core::to_underlying(GpuRenderState::kStencilEnable) == D3DRS_STENCILENABLE);
static_assert(core::to_underlying(GpuRenderState::kTextureFactor) == D3DRS_TEXTUREFACTOR);
static_assert(core::to_underlying(GpuRenderState::kLighting) == D3DRS_LIGHTING);
static_assert(core::to_underlying(GpuRenderState::kAmbient) == D3DRS_AMBIENT);
static_assert(core::to_underlying(GpuRenderState::kFogVertexMode) == D3DRS_FOGVERTEXMODE);
static_assert(core::to_underlying(GpuRenderState::kColourVertex) == D3DRS_COLORVERTEX);
static_assert(core::to_underlying(GpuRenderState::kLocalViewer) == D3DRS_LOCALVIEWER);
static_assert(core::to_underlying(GpuRenderState::kNormalizeNormals) == D3DRS_NORMALIZENORMALS);
static_assert(core::to_underlying(GpuRenderState::kDiffuseMaterialSource) == D3DRS_DIFFUSEMATERIALSOURCE);
static_assert(core::to_underlying(GpuRenderState::kSpecularMaterialSource) == D3DRS_SPECULARMATERIALSOURCE);
static_assert(core::to_underlying(GpuRenderState::kAmbientMaterialSource) == D3DRS_AMBIENTMATERIALSOURCE);
static_assert(core::to_underlying(GpuRenderState::kEmissiveMaterialSource) == D3DRS_EMISSIVEMATERIALSOURCE);
static_assert(core::to_underlying(GpuRenderState::kPointSize) == D3DRS_POINTSIZE);
static_assert(core::to_underlying(GpuRenderState::kPointSizeMin) == D3DRS_POINTSIZE_MIN);
static_assert(core::to_underlying(GpuRenderState::kPointSpriteEnable) == D3DRS_POINTSPRITEENABLE);
static_assert(core::to_underlying(GpuRenderState::kPointScaleEnable) == D3DRS_POINTSCALEENABLE);
static_assert(core::to_underlying(GpuRenderState::kPointScaleA) == D3DRS_POINTSCALE_A);
static_assert(core::to_underlying(GpuRenderState::kPointScaleB) == D3DRS_POINTSCALE_B);
static_assert(core::to_underlying(GpuRenderState::kPointScaleC) == D3DRS_POINTSCALE_C);
static_assert(core::to_underlying(GpuRenderState::kMultisampleAntialias) == D3DRS_MULTISAMPLEANTIALIAS);
static_assert(core::to_underlying(GpuRenderState::kMultisampleMask) == D3DRS_MULTISAMPLEMASK);
static_assert(core::to_underlying(GpuRenderState::kPointSizeMax) == D3DRS_POINTSIZE_MAX);
static_assert(core::to_underlying(GpuRenderState::kColourWriteEnable) == D3DRS_COLORWRITEENABLE);
static_assert(core::to_underlying(GpuRenderState::kBlendOp) == D3DRS_BLENDOP);
static_assert(core::to_underlying(GpuRenderState::kScissorTestEnable) == D3DRS_SCISSORTESTENABLE);
static_assert(core::to_underlying(GpuRenderState::kBlendFactor) == D3DRS_BLENDFACTOR);
static_assert(core::to_underlying(GpuRenderState::kDepthBias) == D3DRS_DEPTHBIAS);
static_assert(core::to_underlying(GpuRenderState::kSeparateAlphaBlendEnable) == D3DRS_SEPARATEALPHABLENDENABLE);
static_assert(kGpuRenderStateCount == D3DRS_BLENDOPALPHA + 1);
static_assert(core::to_underlying(GpuStageState::kColourOp) == D3DTSS_COLOROP);
static_assert(core::to_underlying(GpuStageState::kColourArg1) == D3DTSS_COLORARG1);
static_assert(core::to_underlying(GpuStageState::kColourArg2) == D3DTSS_COLORARG2);
static_assert(core::to_underlying(GpuStageState::kAlphaOp) == D3DTSS_ALPHAOP);
static_assert(core::to_underlying(GpuStageState::kAlphaArg1) == D3DTSS_ALPHAARG1);
static_assert(core::to_underlying(GpuStageState::kAlphaArg2) == D3DTSS_ALPHAARG2);
static_assert(core::to_underlying(GpuStageState::kBumpMatrix00) == D3DTSS_BUMPENVMAT00);
static_assert(core::to_underlying(GpuStageState::kBumpMatrix01) == D3DTSS_BUMPENVMAT01);
static_assert(core::to_underlying(GpuStageState::kBumpMatrix10) == D3DTSS_BUMPENVMAT10);
static_assert(core::to_underlying(GpuStageState::kBumpMatrix11) == D3DTSS_BUMPENVMAT11);
static_assert(core::to_underlying(GpuStageState::kTexCoordIndex) == D3DTSS_TEXCOORDINDEX);
static_assert(core::to_underlying(GpuStageState::kTextureTransformFlags) == D3DTSS_TEXTURETRANSFORMFLAGS);
static_assert(core::to_underlying(GpuStageState::kColourArg0) == D3DTSS_COLORARG0);
static_assert(core::to_underlying(GpuStageState::kAlphaArg0) == D3DTSS_ALPHAARG0);
static_assert(core::to_underlying(GpuStageState::kResultArg) == D3DTSS_RESULTARG);
static_assert(core::to_underlying(GpuStageState::kConstant) == D3DTSS_CONSTANT);
static_assert(kGpuStageStateCount == D3DTSS_CONSTANT + 1);
static_assert(core::to_underlying(GpuSamplerState::kAddressU) == D3DSAMP_ADDRESSU);
static_assert(core::to_underlying(GpuSamplerState::kAddressV) == D3DSAMP_ADDRESSV);
static_assert(core::to_underlying(GpuSamplerState::kAddressW) == D3DSAMP_ADDRESSW);
static_assert(core::to_underlying(GpuSamplerState::kBorderColour) == D3DSAMP_BORDERCOLOR);
static_assert(core::to_underlying(GpuSamplerState::kMagFilter) == D3DSAMP_MAGFILTER);
static_assert(core::to_underlying(GpuSamplerState::kMinFilter) == D3DSAMP_MINFILTER);
static_assert(core::to_underlying(GpuSamplerState::kMipFilter) == D3DSAMP_MIPFILTER);
static_assert(core::to_underlying(GpuSamplerState::kMipLodBias) == D3DSAMP_MIPMAPLODBIAS);
static_assert(core::to_underlying(GpuSamplerState::kMaxMipLevel) == D3DSAMP_MAXMIPLEVEL);
static_assert(core::to_underlying(GpuSamplerState::kMaxAnisotropy) == D3DSAMP_MAXANISOTROPY);
static_assert(kGpuSamplerStateCount == D3DSAMP_DMAPOFFSET + 1);
static_assert(core::to_underlying(GpuTransform::kView) == D3DTS_VIEW);
static_assert(core::to_underlying(GpuTransform::kProjection) == D3DTS_PROJECTION);
static_assert(core::to_underlying(GpuTransform::kTexture0) == D3DTS_TEXTURE0);
static_assert(core::to_underlying(GpuTransform::kWorld) == D3DTS_WORLD);
static_assert(core::to_underlying(GpuPrimitive::kPointList) == D3DPT_POINTLIST);
static_assert(core::to_underlying(GpuPrimitive::kLineList) == D3DPT_LINELIST);
static_assert(core::to_underlying(GpuPrimitive::kLineStrip) == D3DPT_LINESTRIP);
static_assert(core::to_underlying(GpuPrimitive::kTriangleList) == D3DPT_TRIANGLELIST);
static_assert(core::to_underlying(GpuPrimitive::kTriangleStrip) == D3DPT_TRIANGLESTRIP);
static_assert(core::to_underlying(GpuPrimitive::kTriangleFan) == D3DPT_TRIANGLEFAN);
static_assert(gpu_value::kOpDisable == D3DTOP_DISABLE && gpu_value::kOpSelectArg1 == D3DTOP_SELECTARG1 &&
              gpu_value::kOpSelectArg2 == D3DTOP_SELECTARG2 && gpu_value::kOpModulate == D3DTOP_MODULATE &&
              gpu_value::kOpModulate2x == D3DTOP_MODULATE2X && gpu_value::kOpModulate4x == D3DTOP_MODULATE4X);
static_assert(gpu_value::kArgDiffuse == D3DTA_DIFFUSE && gpu_value::kArgCurrent == D3DTA_CURRENT &&
              gpu_value::kArgTexture == D3DTA_TEXTURE && gpu_value::kArgTextureFactor == D3DTA_TFACTOR &&
              gpu_value::kArgTemp == D3DTA_TEMP && gpu_value::kArgConstant == D3DTA_CONSTANT &&
              gpu_value::kArgComplement == D3DTA_COMPLEMENT && gpu_value::kArgAlphaReplicate == D3DTA_ALPHAREPLICATE);
static_assert(gpu_value::kTexGenCameraReflection == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
static_assert(gpu_value::kTextureTransformDisable == D3DTTFF_DISABLE &&
              gpu_value::kTextureTransformCount2 == D3DTTFF_COUNT2 &&
              gpu_value::kTextureTransformProjected == D3DTTFF_PROJECTED);
static_assert(gpu_value::kBlendZero == D3DBLEND_ZERO && gpu_value::kBlendOne == D3DBLEND_ONE &&
              gpu_value::kBlendSrcColour == D3DBLEND_SRCCOLOR && gpu_value::kBlendSrcAlpha == D3DBLEND_SRCALPHA &&
              gpu_value::kBlendInvSrcAlpha == D3DBLEND_INVSRCALPHA &&
              gpu_value::kBlendDestColour == D3DBLEND_DESTCOLOR && gpu_value::kBlendFactor == D3DBLEND_BLENDFACTOR &&
              gpu_value::kBlendInvFactor == D3DBLEND_INVBLENDFACTOR);
static_assert(gpu_value::kBlendOpAdd == D3DBLENDOP_ADD && gpu_value::kBlendOpRevSubtract == D3DBLENDOP_REVSUBTRACT);
static_assert(gpu_value::kCompareNever == D3DCMP_NEVER && gpu_value::kCompareLess == D3DCMP_LESS &&
              gpu_value::kCompareLessEqual == D3DCMP_LESSEQUAL &&
              gpu_value::kCompareGreaterEqual == D3DCMP_GREATEREQUAL && gpu_value::kCompareAlways == D3DCMP_ALWAYS);
static_assert(gpu_value::kCullNone == D3DCULL_NONE && gpu_value::kCullClockwise == D3DCULL_CW &&
              gpu_value::kCullCounterClockwise == D3DCULL_CCW);
static_assert(gpu_value::kFillWireframe == D3DFILL_WIREFRAME && gpu_value::kFillSolid == D3DFILL_SOLID);
static_assert(gpu_value::kFogNone == D3DFOG_NONE && gpu_value::kFogLinear == D3DFOG_LINEAR);
static_assert(gpu_value::kSourceMaterial == D3DMCS_MATERIAL && gpu_value::kSourceColour1 == D3DMCS_COLOR1 &&
              gpu_value::kSourceColour2 == D3DMCS_COLOR2);
static_assert(gpu_value::kFilterNone == D3DTEXF_NONE && gpu_value::kFilterPoint == D3DTEXF_POINT &&
              gpu_value::kFilterLinear == D3DTEXF_LINEAR && gpu_value::kFilterAnisotropic == D3DTEXF_ANISOTROPIC);
static_assert(gpu_value::kAddressWrap == D3DTADDRESS_WRAP && gpu_value::kAddressClamp == D3DTADDRESS_CLAMP &&
              gpu_value::kAddressBorder == D3DTADDRESS_BORDER);
static_assert(gpu_value::kClearTarget == D3DCLEAR_TARGET && gpu_value::kClearDepth == D3DCLEAR_ZBUFFER &&
              gpu_value::kClearStencil == D3DCLEAR_STENCIL);
static_assert(gpu_value::kUsageWriteOnly == D3DUSAGE_WRITEONLY && gpu_value::kUsageDynamic == D3DUSAGE_DYNAMIC &&
              gpu_value::kPoolDefault == D3DPOOL_DEFAULT && gpu_value::kFormatIndex16 == D3DFMT_INDEX16 &&
              gpu_value::kLockReadOnly == D3DLOCK_READONLY && gpu_value::kLockDiscard == D3DLOCK_DISCARD);
static_assert(kFvfXyz == D3DFVF_XYZ && kFvfNormal == D3DFVF_NORMAL && kFvfDiffuse == D3DFVF_DIFFUSE &&
              kFvfSpecular == D3DFVF_SPECULAR && kFvfTex1 == D3DFVF_TEX1 && kFvfTex2 == D3DFVF_TEX2);
static_assert(gpu_value::kLightDirectional == D3DLIGHT_DIRECTIONAL);
static_assert(gpu_value::kResultInvalidCall == D3DERR_INVALIDCALL);

// ... and its structures Direct3D 9's layouts.
static_assert(sizeof(GpuMaterial) == sizeof(D3DMATERIAL9));
static_assert(offsetof(GpuMaterial, emissive) == offsetof(D3DMATERIAL9, Emissive));
static_assert(offsetof(GpuMaterial, power) == offsetof(D3DMATERIAL9, Power));
static_assert(sizeof(GpuLight) == sizeof(D3DLIGHT9));
static_assert(offsetof(GpuLight, position) == offsetof(D3DLIGHT9, Position));
static_assert(offsetof(GpuLight, direction) == offsetof(D3DLIGHT9, Direction));
static_assert(offsetof(GpuLight, phi) == offsetof(D3DLIGHT9, Phi));
static_assert(sizeof(GpuViewport) == sizeof(D3DVIEWPORT9));
static_assert(offsetof(GpuViewport, min_z) == offsetof(D3DVIEWPORT9, MinZ));
static_assert(sizeof(GpuRect) == sizeof(RECT) && sizeof(GpuRect) == sizeof(D3DRECT));
static_assert(offsetof(GpuRect, bottom) == offsetof(RECT, bottom) && offsetof(GpuRect, bottom) == offsetof(D3DRECT, y2));
static_assert(sizeof(GpuLockedRect) == sizeof(D3DLOCKED_RECT));
static_assert(offsetof(GpuLockedRect, bits) == offsetof(D3DLOCKED_RECT, pBits));
static_assert(sizeof(GpuGammaRamp) == sizeof(D3DGAMMARAMP));
static_assert(sizeof(GpuMatrix) == sizeof(D3DMATRIX));
static_assert(sizeof(DWORD) == sizeof(uint32_t));

// The device every operation goes to (the same one the renderer's own calls use).
IDirect3DDevice9* d3d()
{
    return state().d3d_device();
}

// A structure of Gpu's as Direct3D 9's (the same layout: a copy of the bytes).
D3DMATERIAL9 d3d_value(const GpuMaterial& material) { return std::bit_cast<D3DMATERIAL9>(material); }
D3DLIGHT9 d3d_value(const GpuLight& light) { return std::bit_cast<D3DLIGHT9>(light); }
D3DVIEWPORT9 d3d_value(const GpuViewport& viewport) { return std::bit_cast<D3DVIEWPORT9>(viewport); }
D3DGAMMARAMP d3d_value(const GpuGammaRamp& ramp) { return std::bit_cast<D3DGAMMARAMP>(ramp); }

// A pointer to one of Gpu's structures as the pointer type the device takes (the same layout, above: the same
// address, never read as the other type's object).
template <typename To, typename From>
To* same_layout(From* pointer) noexcept
{
    return detail::same_address<To>(pointer);
}
template <typename To, typename From>
const To* same_layout(const From* pointer) noexcept
{
    const void* const address = pointer;
    return static_cast<const To*>(address);
}
const RECT* d3d_rect(const GpuRect* rect) { return same_layout<RECT>(rect); }
const D3DRECT* d3d_clear_rects(const GpuRect* rects) { return same_layout<D3DRECT>(rects); }
D3DLOCKED_RECT* d3d_locked(GpuLockedRect* locked) { return same_layout<D3DLOCKED_RECT>(locked); }
const D3DMATRIX* d3d_matrix(const float* matrix) { return same_layout<D3DMATRIX>(matrix); }
D3DMATRIX* d3d_matrix(float* matrix) { return same_layout<D3DMATRIX>(matrix); }
D3DVIEWPORT9* d3d_viewport(GpuViewport* viewport) { return same_layout<D3DVIEWPORT9>(viewport); }
const DWORD* d3d_tokens(const uint32_t* tokens) { return same_layout<DWORD>(tokens); }
DWORD* d3d_dword(uint32_t* value) { return same_layout<DWORD>(value); } // (DWORD: unsigned long, uint32_t's size)

D3DRENDERSTATETYPE d3d_state(GpuRenderState state) { return static_cast<D3DRENDERSTATETYPE>(state); }
D3DTEXTURESTAGESTATETYPE d3d_state(GpuStageState state) { return static_cast<D3DTEXTURESTAGESTATETYPE>(state); }
D3DSAMPLERSTATETYPE d3d_state(GpuSamplerState state) { return static_cast<D3DSAMPLERSTATETYPE>(state); }
D3DTRANSFORMSTATETYPE d3d_state(GpuTransform transform) { return static_cast<D3DTRANSFORMSTATETYPE>(transform); }
D3DPRIMITIVETYPE d3d_primitive(GpuPrimitive primitive) { return static_cast<D3DPRIMITIVETYPE>(primitive); }

class D3d9Gpu final : public Gpu {
public:
    constexpr D3d9Gpu() = default;
    D3d9Gpu(const D3d9Gpu&) = delete;
    D3d9Gpu& operator=(const D3d9Gpu&) = delete;
    D3d9Gpu(D3d9Gpu&&) = delete;
    D3d9Gpu& operator=(D3d9Gpu&&) = delete;
    ~D3d9Gpu() = default;

    long get_render_state(GpuRenderState state, uint32_t* value) override
    {
        return d3d()->GetRenderState(d3d_state(state), d3d_dword(value));
    }
    long get_stage_state(uint32_t stage, GpuStageState state, uint32_t* value) override
    {
        return d3d()->GetTextureStageState(stage, d3d_state(state), d3d_dword(value));
    }
    long get_sampler_state(uint32_t sampler, GpuSamplerState state, uint32_t* value) override
    {
        return d3d()->GetSamplerState(sampler, d3d_state(state), d3d_dword(value));
    }
    long get_transform(GpuTransform transform, float* matrix) override
    {
        return d3d()->GetTransform(d3d_state(transform), d3d_matrix(matrix));
    }
    long get_viewport(GpuViewport* viewport) override { return d3d()->GetViewport(d3d_viewport(viewport)); }
    long get_vertex_format(uint32_t* fvf) override { return d3d()->GetFVF(d3d_dword(fvf)); }
    long get_pixel_shader(GpuPixelShader** shader) override { return d3d()->GetPixelShader(out(shader)); }
    long get_vertex_shader(GpuVertexShader** shader) override { return d3d()->GetVertexShader(out(shader)); }
    long get_indices(GpuIndexBuffer** buffer) override { return d3d()->GetIndices(out(buffer)); }

    long create_texture(const GpuTextureDesc& desc, GpuTexture** texture) override
    {
        return d3d()->CreateTexture(desc.width, desc.height, desc.levels, desc.usage, static_cast<D3DFORMAT>(desc.format),
                                    static_cast<D3DPOOL>(desc.pool), out(texture), nullptr);
    }
    long lock_texture(GpuTexture* texture, uint32_t level, GpuLockedRect* locked, const GpuRect* area,
                      uint32_t flags) override
    {
        return object(texture)->LockRect(level, d3d_locked(locked), d3d_rect(area), flags);
    }
    long unlock_texture(GpuTexture* texture, uint32_t level) override { return object(texture)->UnlockRect(level); }
    long texture_surface(GpuTexture* texture, uint32_t level, GpuSurface** surface) override
    {
        return object(texture)->GetSurfaceLevel(level, out(surface));
    }
    uint32_t release_texture(GpuTexture* texture) override { return object(texture)->Release(); }
    long create_vertex_buffer(uint32_t bytes, uint32_t usage, uint32_t fvf, uint32_t pool,
                              GpuVertexBuffer** buffer) override
    {
        return d3d()->CreateVertexBuffer(bytes, usage, fvf, static_cast<D3DPOOL>(pool), out(buffer), nullptr);
    }
    long lock_vertex_buffer(GpuVertexBuffer* buffer, uint32_t offset, uint32_t bytes, void** data,
                            uint32_t flags) override
    {
        return object(buffer)->Lock(offset, bytes, data, flags);
    }
    long unlock_vertex_buffer(GpuVertexBuffer* buffer) override { return object(buffer)->Unlock(); }
    uint32_t release_vertex_buffer(GpuVertexBuffer* buffer) override { return object(buffer)->Release(); }
    uint32_t retain_vertex_buffer(GpuVertexBuffer* buffer) override { return object(buffer)->AddRef(); }
    long create_index_buffer(uint32_t bytes, uint32_t usage, uint32_t format, uint32_t pool,
                             GpuIndexBuffer** buffer) override
    {
        return d3d()->CreateIndexBuffer(bytes, usage, static_cast<D3DFORMAT>(format), static_cast<D3DPOOL>(pool),
                                        out(buffer), nullptr);
    }
    long lock_index_buffer(GpuIndexBuffer* buffer, uint32_t offset, uint32_t bytes, void** data,
                           uint32_t flags) override
    {
        return object(buffer)->Lock(offset, bytes, data, flags);
    }
    long unlock_index_buffer(GpuIndexBuffer* buffer) override { return object(buffer)->Unlock(); }
    uint32_t release_index_buffer(GpuIndexBuffer* buffer) override { return object(buffer)->Release(); }

    long create_render_target(const GpuSurfaceDesc& desc, GpuSurface** surface) override
    {
        return d3d()->CreateRenderTarget(desc.width, desc.height, static_cast<D3DFORMAT>(desc.format),
                                         static_cast<D3DMULTISAMPLE_TYPE>(desc.samples), desc.quality,
                                         desc.lockable_or_discard ? TRUE : FALSE, out(surface), nullptr);
    }
    long create_depth_surface(const GpuSurfaceDesc& desc, GpuSurface** surface) override
    {
        return d3d()->CreateDepthStencilSurface(desc.width, desc.height, static_cast<D3DFORMAT>(desc.format),
                                                static_cast<D3DMULTISAMPLE_TYPE>(desc.samples), desc.quality,
                                                desc.lockable_or_discard ? TRUE : FALSE, out(surface), nullptr);
    }
    long create_offscreen_surface(uint32_t width, uint32_t height, uint32_t format, uint32_t pool,
                                  GpuSurface** surface) override
    {
        return d3d()->CreateOffscreenPlainSurface(width, height, static_cast<D3DFORMAT>(format),
                                                  static_cast<D3DPOOL>(pool), out(surface), nullptr);
    }
    long set_render_target(uint32_t index, GpuSurface* surface) override
    {
        return d3d()->SetRenderTarget(index, object(surface));
    }
    long set_depth_surface(GpuSurface* surface) override { return d3d()->SetDepthStencilSurface(object(surface)); }
    long render_target(uint32_t index, GpuSurface** surface) override
    {
        return d3d()->GetRenderTarget(index, out(surface));
    }
    long depth_surface(GpuSurface** surface) override { return d3d()->GetDepthStencilSurface(out(surface)); }
    long copy_surface(GpuSurface* source, const GpuRect* source_area, GpuSurface* target, const GpuRect* target_area,
                      uint32_t filter) override
    {
        return d3d()->StretchRect(object(source), d3d_rect(source_area), object(target), d3d_rect(target_area),
                                  static_cast<D3DTEXTUREFILTERTYPE>(filter));
    }
    long read_render_target(GpuSurface* target, GpuSurface* system_copy) override
    {
        return d3d()->GetRenderTargetData(object(target), object(system_copy));
    }
    uint32_t release_surface(GpuSurface* surface) override { return object(surface)->Release(); }

    long create_pixel_shader(const uint32_t* tokens, GpuPixelShader** shader) override
    {
        return d3d()->CreatePixelShader(d3d_tokens(tokens), out(shader));
    }

    long create_occlusion_query(GpuQuery** query) override
    {
        return d3d()->CreateQuery(D3DQUERYTYPE_OCCLUSION, out(query));
    }
    long begin_query(GpuQuery* query) override { return object(query)->Issue(D3DISSUE_BEGIN); }
    long end_query(GpuQuery* query) override { return object(query)->Issue(D3DISSUE_END); }
    long query_result(GpuQuery* query, void* data, uint32_t bytes, uint32_t flags) override
    {
        return object(query)->GetData(data, bytes, flags);
    }
    uint32_t release_query(GpuQuery* query) override { return object(query)->Release(); }

    long set_stream_source(uint32_t stream, GpuVertexBuffer* buffer, uint32_t offset, uint32_t stride) override
    {
        return d3d()->SetStreamSource(stream, object(buffer), offset, stride);
    }
    long set_indices(GpuIndexBuffer* buffer) override { return d3d()->SetIndices(object(buffer)); }
    long get_stream_source(uint32_t stream, GpuVertexBuffer** buffer, uint32_t* offset, uint32_t* stride) override
    {
        return d3d()->GetStreamSource(stream, out(buffer), offset, stride);
    }
    long draw(GpuPrimitive primitive, uint32_t first_vertex, uint32_t count) override
    {
        return d3d()->DrawPrimitive(d3d_primitive(primitive), first_vertex, count);
    }
    long draw_indexed(GpuPrimitive primitive, int32_t base_vertex, uint32_t min_index, uint32_t vertex_count,
                      uint32_t first_index, uint32_t count) override
    {
        return d3d()->DrawIndexedPrimitive(d3d_primitive(primitive), base_vertex, min_index, vertex_count, first_index,
                                           count);
    }
    long draw_from_memory(GpuPrimitive primitive, uint32_t count, const void* vertices, uint32_t stride) override
    {
        return d3d()->DrawPrimitiveUP(d3d_primitive(primitive), count, vertices, stride);
    }
    long draw_indexed_from_memory(GpuPrimitive primitive, uint32_t min_index, uint32_t vertex_count, uint32_t count,
                                  const void* indices, uint32_t index_format, const void* vertices,
                                  uint32_t stride) override
    {
        return d3d()->DrawIndexedPrimitiveUP(d3d_primitive(primitive), min_index, vertex_count, count, indices,
                                             static_cast<D3DFORMAT>(index_format), vertices, stride);
    }

    long begin_scene() override { return d3d()->BeginScene(); }
    long end_scene() override { return d3d()->EndScene(); }
    long clear(uint32_t rect_count, const GpuRect* rects, uint32_t flags, uint32_t colour, float z,
               uint32_t stencil) override
    {
        return d3d()->Clear(rect_count, d3d_clear_rects(rects), flags, colour, z, stencil);
    }
    long present() override { return d3d()->Present(nullptr, nullptr, nullptr, nullptr); }
    void set_gamma_ramp(const GpuGammaRamp& ramp) override
    {
        const D3DGAMMARAMP gamma = d3d_value(ramp);
        d3d()->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &gamma);
    }
    long test_cooperative_level() override { return d3d()->TestCooperativeLevel(); }

protected:
    long send_render_state(GpuRenderState state, uint32_t value) override
    {
        return d3d()->SetRenderState(d3d_state(state), value);
    }
    long send_stage_state(uint32_t stage, GpuStageState state, uint32_t value) override
    {
        return d3d()->SetTextureStageState(stage, d3d_state(state), value);
    }
    long send_sampler_state(uint32_t sampler, GpuSamplerState state, uint32_t value) override
    {
        return d3d()->SetSamplerState(sampler, d3d_state(state), value);
    }
    long send_texture(uint32_t stage, GpuTexture* texture) override { return d3d()->SetTexture(stage, object(texture)); }
    long send_vertex_format(uint32_t fvf) override { return d3d()->SetFVF(fvf); }
    long send_pixel_shader(GpuPixelShader* shader) override { return d3d()->SetPixelShader(object(shader)); }
    long send_pixel_constants(uint32_t first, const float* values, uint32_t count) override
    {
        return d3d()->SetPixelShaderConstantF(first, values, count);
    }
    long send_vertex_shader(GpuVertexShader* shader) override { return d3d()->SetVertexShader(object(shader)); }
    long send_vertex_constants(uint32_t first, const float* values, uint32_t count) override
    {
        return d3d()->SetVertexShaderConstantF(first, values, count);
    }
    long send_material(const GpuMaterial& material) override
    {
        const D3DMATERIAL9 value = d3d_value(material);
        return d3d()->SetMaterial(&value);
    }
    long send_light(uint32_t index, const GpuLight& light) override
    {
        const D3DLIGHT9 value = d3d_value(light);
        return d3d()->SetLight(index, &value);
    }
    long send_light_enabled(uint32_t index, bool on) override { return d3d()->LightEnable(index, on ? TRUE : FALSE); }
    long send_transform(GpuTransform transform, const float* matrix) override
    {
        return d3d()->SetTransform(d3d_state(transform), d3d_matrix(matrix));
    }
    long send_viewport(const GpuViewport& viewport) override
    {
        const D3DVIEWPORT9 value = d3d_value(viewport);
        return d3d()->SetViewport(&value);
    }
};

} // namespace

Gpu& gpu()
{
    static core::NoDestroy<D3d9Gpu> objects;
    return objects.get();
}

} // namespace render::d3d9

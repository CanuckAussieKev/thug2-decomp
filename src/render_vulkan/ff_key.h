#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

// NOT ORIGINAL - the key of the Vulkan backend's fixed-function emulation (docs/design/vulkan.md "Fixed-function
// emulation in Vulkan"): the part of Direct3D 9's fixed-function state that changes a shader's code - the vertex format,
// lighting, fog, texture coordinate generation and transforms, point sprites, the texture stages' operations, alpha test,
// and which of the game's ps.1.1 pixel shaders replaces the stages. ff_shaders.h turns a key into GLSL;
// glsl_compiler.h compiles it to SPIR-V. Values the key doesn't hold (matrices, colours, the fog's distances, the alpha
// reference, the texture factor, the pixel shader constants) are the uniforms of ff_uniforms.h, so one shader serves
// every draw of its key.
//
// Every enumerator has Direct3D 9's own number (D3DTOP_, D3DTA_, D3DFOG_, D3DCMP_, D3DMCS_, D3DTSS_TCI_ >> 16,
// D3DFVF_), so a state block that keeps Direct3D 9's values converts field by field.
//
// The key is two halves, one per shader: VertexKey makes the vertex shader, FragmentKey the fragment shader; a pipeline
// takes both. normalised() clears what a half doesn't read (the stages after the first disabled one, the arguments an
// operation doesn't use, the lighting's fields with lighting off...), so states that draw alike share a shader. Both
// halves are plain bytes with no padding: they are compared and hashed as bytes.
namespace render::vulkan {

// D3DTEXTUREOP: a texture stage's colour or alpha operation.
enum class StageOp : uint8_t {
    kNone = 0, // (not a Direct3D value: the field unused)
    kDisable = 1,
    kSelectArg1 = 2,
    kSelectArg2 = 3,
    kModulate = 4,
    kModulate2x = 5,
    kModulate4x = 6,
    kAdd = 7,
    kAddSigned = 8,
    kAddSigned2x = 9,
    kSubtract = 10,
    kAddSmooth = 11,
    kBlendDiffuseAlpha = 12,
    kBlendTextureAlpha = 13,
    kBlendFactorAlpha = 14,
    kBlendTextureAlphaPm = 15,
    kBlendCurrentAlpha = 16,
    kPremodulate = 17,
    kModulateAlphaAddColour = 18,
    kModulateColourAddAlpha = 19,
    kModulateInvAlphaAddColour = 20,
    kModulateInvColourAddAlpha = 21,
    kBumpEnvMap = 22,
    kBumpEnvMapLuminance = 23,
    kDotProduct3 = 24,
    kMultiplyAdd = 25,
    kLerp = 26,
};

// D3DTA_: a stage argument - its source in the low three bits, and two modifiers.
enum class ArgSource : uint8_t {
    kDiffuse = 0,
    kCurrent = 1,
    kTexture = 2,
    kFactor = 3, // the texture factor (D3DRS_TEXTUREFACTOR)
    kSpecular = 4,
    kTemp = 5,
    kConstant = 6, // a stage's own constant (D3DTSS_CONSTANT): not emulated
};
constexpr uint8_t kArgSourceMask = 0x07U;
constexpr uint8_t kArgComplement = 0x10U;     // 1 - the value
constexpr uint8_t kArgAlphaReplicate = 0x20U; // the value's alpha in all four channels

[[nodiscard]] constexpr ArgSource arg_source(uint8_t arg) { return static_cast<ArgSource>(arg & kArgSourceMask); }
[[nodiscard]] constexpr uint8_t arg(ArgSource source, uint8_t modifiers = 0)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(source) | modifiers);
}

// D3DTSS_TCI_ (the high half of D3DTSS_TEXCOORDINDEX, shifted down): where a stage's texture coordinates come from.
enum class TexGen : uint8_t {
    kPassThrough = 0,          // the vertex's coordinate set (the low half of TEXCOORDINDEX)
    kCameraSpaceNormal = 1,    // the normal in camera space
    kCameraSpacePosition = 2,  // the position in camera space
    kCameraSpaceReflection = 3, // the reflection vector in camera space (the game's environment maps)
    kSphereMap = 4,            // not emulated
};

// D3DFOGMODE.
enum class FogMode : uint8_t {
    kNone = 0,
    kExp = 1,
    kExp2 = 2,
    kLinear = 3,
};

// D3DCMPFUNC: the alpha test's function. kOff (not a Direct3D value): no alpha test.
enum class CompareFunc : uint8_t {
    kOff = 0,
    kNever = 1,
    kLess = 2,
    kEqual = 3,
    kLessEqual = 4,
    kGreater = 5,
    kNotEqual = 6,
    kGreaterEqual = 7,
    kAlways = 8,
};

// D3DMATERIALCOLORSOURCE: where a lit colour's material colour comes from (with D3DRS_COLORVERTEX on).
enum class ColourSource : uint8_t {
    kMaterial = 0,
    kColour1 = 1, // the vertex's diffuse colour
    kColour2 = 2, // the vertex's specular colour
};

// What replaces the texture stages: nothing (the stages run), or one of the game's ps.1.1 pixel shaders
// (render_pc/pixel_shaders.cpp), each ported to GLSL by ff_shaders.cpp.
enum class PixelProgram : uint8_t {
    kStages = 0,                    // the fixed-function texture stages
    kTextureVertexX4 = 1,           // texture x pass colour x vertex colour x4, vertex alpha x texture alpha x2
    kVertexFactor = 2,              // vertex colour x pass colour x2, vertex alpha
    kBloomCutoff = 3,               // the bloom's cut-off: the texel where it is brighter than c0, else black
    kVertexAlphaTextureAlpha = 4,   // vertex alpha x texture alpha x2, in all four channels
    kConstantAlphaTextureAlpha = 5, // c4's alpha x texture alpha x2, in all four channels
    kTextureConstantAlpha = 6,      // texture x pass colour x vertex colour x4, c4's alpha x texture alpha x2
    kMaterial = 7,                  // a material's generated shader: FragmentKey's material_* fields
};

// The vertex format's bits the key reads (D3DFVF_).
constexpr uint32_t kFvfXyz = 0x002U;
constexpr uint32_t kFvfXyzRhw = 0x004U;
constexpr uint32_t kFvfPositionMask = 0x00EU;
constexpr uint32_t kFvfNormal = 0x010U;
constexpr uint32_t kFvfDiffuse = 0x040U;
constexpr uint32_t kFvfSpecular = 0x080U;
constexpr uint32_t kFvfTexCountShift = 8;
constexpr uint32_t kFvfTexCountMask = 0xF00U;
constexpr uint32_t kFvfTexSizeShift = 16; // two bits a set from here: 0 two floats, 1 three, 2 four, 3 one
constexpr uint32_t kFvfKnownBits = 0xFFFF0FDEU;

// The stages the key holds: the game uses up to four textures with its pixel shaders, and up to two stages without.
constexpr std::size_t kStageCount = 4;
// The directional lights the vertex shader can light with (Direct3D 9's limit for a hardware device).
constexpr std::size_t kMaxLights = 8;
// The texture coordinate sets a vertex can have.
constexpr std::size_t kMaxTexSets = 8;

// A stage's texture coordinates: which, and their transform (D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS).
struct TexCoordKey {
    uint8_t set = 0; // the vertex's coordinate set (kPassThrough)
    TexGen gen = TexGen::kPassThrough;
    uint8_t count = 0;      // D3DTTFF_COUNTn: the texture matrix is applied and n outputs kept; 0: not transformed
    bool projected = false; // D3DTTFF_PROJECTED: the coordinates are divided by their last one per pixel

    bool operator==(const TexCoordKey&) const = default;
};

// The vertex shader's half.
struct VertexKey {
    uint32_t fvf = kFvfXyz;
    bool lighting = false;          // D3DRS_LIGHTING
    bool colour_vertex = false;     // D3DRS_COLORVERTEX: the material sources below are used
    bool specular = false;          // D3DRS_SPECULARENABLE: lights' specular colour computed
    bool local_viewer = false;      // D3DRS_LOCALVIEWER: specular and reflection vectors from the real eye direction
    bool normalize_normals = false; // D3DRS_NORMALIZENORMALS
    uint8_t light_count = 0;        // the enabled lights, all directional, 0 - kMaxLights
    ColourSource diffuse_source = ColourSource::kColour1;
    ColourSource ambient_source = ColourSource::kMaterial;
    ColourSource specular_source = ColourSource::kColour2;
    ColourSource emissive_source = ColourSource::kMaterial;
    FogMode fog = FogMode::kNone; // vertex fog (D3DRS_FOGVERTEXMODE, with fog on and no table fog)
    bool range_fog = false;       // D3DRS_RANGEFOGENABLE: vertex fog by distance, not depth
    bool points = false;          // a point list: the point size is written
    bool point_scale = false;     // D3DRS_POINTSCALEENABLE: the size scaled by distance
    uint8_t coord_mask = 0;       // the stages whose texture coordinates the fragment shader reads (bit n: stage n)
    uint8_t spare = 0;
    std::array<TexCoordKey, kStageCount> coords{};

    bool operator==(const VertexKey&) const = default;
};

// A texture stage's operations and arguments (D3DTSS_COLOROP... D3DTSS_ALPHAARG0).
struct StageKey {
    StageOp colour_op = StageOp::kDisable;
    uint8_t colour_arg0 = 0;
    uint8_t colour_arg1 = 0;
    uint8_t colour_arg2 = 0;
    StageOp alpha_op = StageOp::kDisable;
    uint8_t alpha_arg0 = 0;
    uint8_t alpha_arg1 = 0;
    uint8_t alpha_arg2 = 0;

    bool operator==(const StageKey&) const = default;
};

// The fragment shader's half.
struct FragmentKey {
    PixelProgram program = PixelProgram::kStages;
    uint8_t material_passes = 0;      // kMaterial: the passes, 2-4
    uint8_t material_alpha_flags = 0; // kMaterial: bit n - pass n takes its alpha from its texture alone
    CompareFunc alpha_test = CompareFunc::kOff;
    std::array<uint8_t, 4> material_modes{}; // kMaterial: each pass's blend mode (engine/gfx/mesh_formats.h)
    bool fog = false;                        // D3DRS_FOGENABLE
    FogMode table_fog = FogMode::kNone;      // D3DRS_FOGTABLEMODE: per-pixel fog (wins over vertex fog)
    bool specular_add = false;               // kStages with D3DRS_SPECULARENABLE: the specular colour added after them
    bool point_sprite = false;               // D3DRS_POINTSPRITEENABLE: every stage's coordinates are the point's
    uint8_t projected_mask = 0;              // the stages whose coordinates are projected (TexCoordKey::projected)
    std::array<uint8_t, 3> spare{};
    std::array<StageKey, kStageCount> stages{};

    bool operator==(const FragmentKey&) const = default;
};

// A draw's shaders.
struct ShaderKey {
    VertexKey vertex{};
    FragmentKey fragment{};

    bool operator==(const ShaderKey&) const = default;
};

static_assert(std::has_unique_object_representations_v<VertexKey>, "VertexKey is hashed as bytes");
static_assert(std::has_unique_object_representations_v<FragmentKey>, "FragmentKey is hashed as bytes");
constexpr std::size_t kVertexKeyBytes = 36;
constexpr std::size_t kFragmentKeyBytes = 48;
static_assert(sizeof(VertexKey) == kVertexKeyBytes && sizeof(FragmentKey) == kFragmentKeyBytes, "the keys stay compact");

// The texture coordinate sets of `fvf` (0-8).
[[nodiscard]] constexpr uint32_t fvf_tex_count(uint32_t fvf) { return (fvf & kFvfTexCountMask) >> kFvfTexCountShift; }
// The floats of set `set` of `fvf` (1-4).
[[nodiscard]] uint32_t fvf_tex_size(uint32_t fvf, uint32_t set);
// Whether `fvf` is pre-transformed (screen-space positions with a reciprocal w).
[[nodiscard]] constexpr bool fvf_pre_transformed(uint32_t fvf) { return (fvf & kFvfPositionMask) == kFvfXyzRhw; }

// The textures a fragment key samples (bit n: stage n's texture, t<n>).
[[nodiscard]] uint32_t sampled_stages(const FragmentKey& key);
// Whether an operation reads arg0 / arg1 / arg2.
[[nodiscard]] bool op_reads_arg0(StageOp op);
[[nodiscard]] bool op_reads_arg1(StageOp op);
[[nodiscard]] bool op_reads_arg2(StageOp op);

// The key with what its shaders don't read cleared (and the vertex half's coordinate mask and the fragment half's
// projected mask matched to each other): equal normalised keys make the same shaders.
[[nodiscard]] ShaderKey normalised(const ShaderKey& key);

// Whether the generator emulates everything the key asks for; false names what it doesn't in `why`: bump mapping,
// premodulation, D3DTA_CONSTANT, sphere maps, more lights or stages than it has, vertex formats it doesn't read.
[[nodiscard]] bool supported(const ShaderKey& key, std::string* why);

// 64-bit FNV-1a hashes of the halves' bytes.
[[nodiscard]] uint64_t hash_of(const VertexKey& key);
[[nodiscard]] uint64_t hash_of(const FragmentKey& key);

// One line naming the key's fields, for logs and the check's report.
[[nodiscard]] std::string describe(const VertexKey& key);
[[nodiscard]] std::string describe(const FragmentKey& key);

struct VertexKeyHash {
    std::size_t operator()(const VertexKey& key) const { return static_cast<std::size_t>(hash_of(key)); }
};
struct FragmentKeyHash {
    std::size_t operator()(const FragmentKey& key) const { return static_cast<std::size_t>(hash_of(key)); }
};

} // namespace render::vulkan

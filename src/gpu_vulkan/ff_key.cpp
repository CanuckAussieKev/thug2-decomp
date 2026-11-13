// NOT ORIGINAL - the fixed-function emulation's key: normalisation, the supported subset, hashes and descriptions
// (ff_key.h).
#include "gpu_vulkan/ff_key.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

#include "base/core/checked_index.h"
#include "base/core/enum_ops.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001B3ULL;
constexpr uint32_t kTexSizeMask = 3;
constexpr uint32_t kTexSizeOne = 3; // D3DFVF_TEXTUREFORMAT1
constexpr uint32_t kFvfPointSize = 0x020U;
constexpr uint32_t kFvfSizeTwo = 2;
constexpr uint32_t kHexDigitBits = 4;
constexpr uint32_t kHexDigitMask = 0xFU;
constexpr uint32_t kHexDigits = 8;
constexpr std::array<char, 17> kHex = {"0123456789abcdef"};

template <typename Key>
uint64_t fnv1a(const Key& key)
{
    const auto bytes = std::bit_cast<std::array<std::byte, sizeof(Key)>>(key);
    uint64_t hash = kFnvOffset;
    for (const std::byte value : bytes) {
        hash = (hash ^ static_cast<uint64_t>(value)) * kFnvPrime;
    }
    return hash;
}

uint32_t bit(std::size_t index) { return 1U << index; }

bool reads_texture(uint8_t value) { return arg_source(value) == ArgSource::kTexture; }

// Whether stage `stage`'s operations read its texture.
bool stage_samples(const StageKey& stage)
{
    const auto reads = [](StageOp op, uint8_t a0, uint8_t a1, uint8_t a2) {
        if (op == StageOp::kBlendTextureAlpha || op == StageOp::kBlendTextureAlphaPm) {
            return true;
        }
        return (op_reads_arg0(op) && reads_texture(a0)) || (op_reads_arg1(op) && reads_texture(a1)) ||
               (op_reads_arg2(op) && reads_texture(a2));
    };
    return reads(stage.colour_op, stage.colour_arg0, stage.colour_arg1, stage.colour_arg2) ||
           reads(stage.alpha_op, stage.alpha_arg0, stage.alpha_arg1, stage.alpha_arg2);
}

// At stage 0 CURRENT is the diffuse colour: the same argument.
uint8_t stage_arg(uint8_t value, std::size_t stage)
{
    if (stage == 0 && arg_source(value) == ArgSource::kCurrent) {
        return arg(ArgSource::kDiffuse, static_cast<uint8_t>(value & ~kArgSourceMask));
    }
    return value;
}

// One operation and its arguments with what it doesn't read cleared. An alpha argument's alpha replicate changes
// nothing (it is already the alpha): cleared too.
void normalise_op(StageOp& op, std::array<uint8_t*, 3> args, std::size_t stage, bool alpha)
{
    std::array<bool, 3> reads = {op_reads_arg0(op), op_reads_arg1(op), op_reads_arg2(op)};
    for (std::size_t i = 0; i < args.size(); ++i) {
        uint8_t& value = *core::at(args, i);
        if (!core::at(reads, i)) {
            value = 0;
            continue;
        }
        value = stage_arg(value, stage);
        if (alpha) {
            value = static_cast<uint8_t>(value & ~kArgAlphaReplicate);
        }
    }
}

void normalise_stages(FragmentKey& key)
{
    bool disabled = false;
    for (std::size_t index = 0; index < kStageCount; ++index) {
        StageKey& stage = core::at(key.stages, index);
        if (disabled || stage.colour_op == StageOp::kDisable || stage.colour_op == StageOp::kNone) {
            disabled = true;
            stage = StageKey{};
            continue;
        }
        // Alpha disabled under an enabled colour: the alpha goes through unchanged (CURRENT's alpha).
        if (stage.alpha_op == StageOp::kDisable || stage.alpha_op == StageOp::kNone) {
            stage.alpha_op = StageOp::kSelectArg1;
            stage.alpha_arg1 = arg(ArgSource::kCurrent);
        }
        normalise_op(stage.colour_op, {&stage.colour_arg0, &stage.colour_arg1, &stage.colour_arg2}, index, false);
        normalise_op(stage.alpha_op, {&stage.alpha_arg0, &stage.alpha_arg1, &stage.alpha_arg2}, index, true);
        // A dot product in the colour writes the alpha too: the alpha operation isn't read.
        if (stage.colour_op == StageOp::kDotProduct3) {
            stage.alpha_op = StageOp::kSelectArg1;
            stage.alpha_arg0 = 0;
            stage.alpha_arg1 = arg(ArgSource::kCurrent);
            stage.alpha_arg2 = 0;
        }
    }
}

// (NOT ORIGINAL - rt_pbr) The programs the per-pixel lighting can take: those whose last line multiplies by v0.
bool takes_surface(const FragmentKey& key)
{
    return key.unlit == 0 && (key.program == PixelProgram::kMaterial || key.program == PixelProgram::kTextureVertexX4 ||
                              key.program == PixelProgram::kTextureConstantAlpha);
}

void normalise_fragment(FragmentKey& key, bool points)
{
    if (!takes_surface(key) || points || (key.surface & kSurfaceLighting) == 0U) {
        key.surface = 0;
    }
    key.surface = static_cast<uint8_t>(key.surface & kSurfaceKnown);
    if (key.program != PixelProgram::kStages) {
        key.stages = {};
        key.specular_add = false;
    } else {
        key.unlit = 0; // (a twin is always of a ps.1.1 program)
        normalise_stages(key);
    }
    if (key.program == PixelProgram::kMaterial) {
        const uint32_t passes = key.material_passes;
        key.material_alpha_flags = static_cast<uint8_t>(key.material_alpha_flags & ((1U << passes) - 1U));
        for (std::size_t pass = 0; pass < key.material_modes.size(); ++pass) {
            if (pass == 0 || pass >= passes) {
                core::at(key.material_modes, pass) = 0; // (pass 0's mode is the blend's, not the shader's)
            }
        }
    } else {
        key.material_passes = 0;
        key.material_alpha_flags = 0;
        key.material_modes = {};
    }
    if (!key.fog) {
        key.table_fog = FogMode::kNone;
    }
    if (key.alpha_test == CompareFunc::kAlways) {
        key.alpha_test = CompareFunc::kOff;
    }
    if (!points) {
        key.point_sprite = false;
    }
}

bool uses_gen(const VertexKey& key, TexGen gen)
{
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        if ((key.coord_mask & bit(stage)) != 0U && core::at(key.coords, stage).gen == gen) {
            return true;
        }
    }
    return false;
}

// A material source as the vertex format makes it: a vertex colour the format doesn't have is the material's.
ColourSource source_for(ColourSource source, uint32_t fvf)
{
    if ((source == ColourSource::kColour1 && (fvf & kFvfDiffuse) == 0U) ||
        (source == ColourSource::kColour2 && (fvf & kFvfSpecular) == 0U)) {
        return ColourSource::kMaterial;
    }
    return source;
}

void normalise_lighting(VertexKey& key)
{
    const bool reflection = uses_gen(key, TexGen::kCameraSpaceReflection);
    const bool normal_gen = reflection || uses_gen(key, TexGen::kCameraSpaceNormal);
    if (!key.lighting) {
        key.colour_vertex = false;
        key.specular = false;
        key.light_count = 0;
    }
    if (!key.lighting || !key.colour_vertex) {
        key.diffuse_source = ColourSource::kMaterial;
        key.ambient_source = ColourSource::kMaterial;
        key.specular_source = ColourSource::kMaterial;
        key.emissive_source = ColourSource::kMaterial;
    } else {
        key.diffuse_source = source_for(key.diffuse_source, key.fvf);
        key.ambient_source = source_for(key.ambient_source, key.fvf);
        key.specular_source = source_for(key.specular_source, key.fvf);
        key.emissive_source = source_for(key.emissive_source, key.fvf);
    }
    if (!key.specular) {
        key.specular_source = ColourSource::kMaterial;
    }
    if (!(key.lighting && key.specular) && !reflection) {
        key.local_viewer = false;
    }
    if (!key.lighting && !normal_gen) {
        key.normalize_normals = false;
    }
}

void normalise_vertex(VertexKey& key)
{
    key.spare = {};
    const bool pre_transformed = fvf_pre_transformed(key.fvf);
    if (pre_transformed) {
        key.motion = 0;
        key.retro = 0;
    }
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        TexCoordKey& coords = core::at(key.coords, stage);
        if ((key.coord_mask & bit(stage)) == 0U) {
            coords = TexCoordKey{};
        } else if (pre_transformed) {
            // Pre-transformed vertices: their own coordinates, untransformed (only the projection is kept).
            coords.gen = TexGen::kPassThrough;
            coords.count = 0;
        }
    }
    if (pre_transformed) {
        key.lighting = false;
        key.range_fog = false;
        key.point_scale = false;
        if (key.fog != FogMode::kNone) {
            key.fog = FogMode::kLinear; // (the factor is the specular alpha: the mode isn't read)
        }
    }
    normalise_lighting(key);
    if (key.fog == FogMode::kNone) {
        key.range_fog = false;
    }
    if (!key.points) {
        key.point_scale = false;
    }
}

void append_hex(std::string& text, uint32_t value)
{
    text += "0x";
    bool started = false;
    for (uint32_t digit = kHexDigits; digit-- > 0;) {
        const uint32_t nibble = (value >> (digit * kHexDigitBits)) & kHexDigitMask;
        if (nibble != 0U || started || digit == 0) {
            text += core::at(kHex, nibble);
            started = true;
        }
    }
}

const char* fog_name(FogMode mode)
{
    switch (mode) {
    case FogMode::kNone:
        return "none";
    case FogMode::kExp:
        return "exp";
    case FogMode::kExp2:
        return "exp2";
    case FogMode::kLinear:
        return "linear";
    }
    return "?";
}

void append_arg(std::string& text, uint8_t value)
{
    static constexpr std::array<const char*, 8> kNames = {"diffuse", "current", "texture", "factor",
                                                          "specular", "temp", "constant", "?"};
    if ((value & kArgComplement) != 0U) {
        text += '1';
        text += '-';
    }
    text += core::at(kNames, value & kArgSourceMask);
    if ((value & kArgAlphaReplicate) != 0U) {
        text += ".a";
    }
}

void append_op(std::string& text, StageOp op, std::array<uint8_t, 3> args)
{
    text += std::to_string(core::to_underlying(op));
    text += '(';
    bool first = true;
    const std::array<bool, 3> reads = {op_reads_arg0(op), op_reads_arg1(op), op_reads_arg2(op)};
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (!core::at(reads, i)) {
            continue;
        }
        if (!first) {
            text += ',';
        }
        first = false;
        append_arg(text, core::at(args, i));
    }
    text += ')';
}

} // namespace

uint32_t fvf_tex_size(uint32_t fvf, uint32_t set)
{
    const uint32_t code = (fvf >> (kFvfTexSizeShift + (set * 2))) & kTexSizeMask;
    return code == kTexSizeOne ? 1U : code + kFvfSizeTwo;
}

bool op_reads_arg0(StageOp op) { return op == StageOp::kMultiplyAdd || op == StageOp::kLerp; }

bool op_reads_arg1(StageOp op)
{
    return op != StageOp::kNone && op != StageOp::kDisable && op != StageOp::kSelectArg2;
}

bool op_reads_arg2(StageOp op)
{
    return op != StageOp::kNone && op != StageOp::kDisable && op != StageOp::kSelectArg1;
}

uint32_t sampled_stages(const FragmentKey& key)
{
    switch (key.program) {
    case PixelProgram::kStages: {
        uint32_t mask = 0;
        for (std::size_t stage = 0; stage < kStageCount; ++stage) {
            const StageKey& ops = core::at(key.stages, stage);
            if (ops.colour_op == StageOp::kDisable || ops.colour_op == StageOp::kNone) {
                break;
            }
            if (stage_samples(ops)) {
                mask |= bit(stage);
            }
        }
        return mask;
    }
    case PixelProgram::kVertexFactor:
        return 0;
    case PixelProgram::kMaterial:
        return (1U << key.material_passes) - 1U;
    case PixelProgram::kTextureVertexX4:
    case PixelProgram::kBloomCutoff:
    case PixelProgram::kVertexAlphaTextureAlpha:
    case PixelProgram::kConstantAlphaTextureAlpha:
    case PixelProgram::kTextureConstantAlpha:
        return 1;
    }
    return 0;
}

ShaderKey normalised(const ShaderKey& key)
{
    ShaderKey result = key;
    normalise_fragment(result.fragment, result.vertex.points);
    const uint32_t sampled = sampled_stages(result.fragment);
    result.vertex.coord_mask = result.fragment.point_sprite ? 0 : static_cast<uint8_t>(sampled);
    normalise_vertex(result.vertex);
    uint8_t projected = 0;
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        if ((result.vertex.coord_mask & bit(stage)) != 0U && core::at(result.vertex.coords, stage).projected) {
            projected = static_cast<uint8_t>(projected | bit(stage));
        }
    }
    result.fragment.projected_mask = projected;
    result.fragment.motion = result.vertex.motion; // (the fragment half reads what the vertex half hands it)
    // (NOT ORIGINAL - rt_pbr) the per-pixel lighting needs camera-space positions (not pre-transformed vertices) and
    // stage 0's coordinates; the vertex half writes its inputs exactly when the fragment half reads them
    if (fvf_pre_transformed(result.vertex.fvf) || (result.vertex.coord_mask & 1U) == 0U ||
        result.fragment.point_sprite) {
        result.fragment.surface = 0;
    }
    if (!result.vertex.lighting) {
        result.fragment.surface = static_cast<uint8_t>(result.fragment.surface & ~kSurfaceLit);
    }
    result.vertex.surface = result.fragment.surface != 0 ? 1 : 0;
    return result;
}

bool supported(const ShaderKey& key, std::string* why)
{
    const auto refuse = [why](const char* reason) {
        if (why) {
            *why = reason;
        }
        return false;
    };
    const uint32_t position = key.vertex.fvf & kFvfPositionMask;
    if (position != kFvfXyz && position != kFvfXyzRhw) {
        return refuse("a position other than XYZ or XYZRHW (blend weights)");
    }
    if ((key.vertex.fvf & ~kFvfKnownBits) != 0U || (key.vertex.fvf & kFvfPointSize) != 0U) {
        return refuse("vertex format bits the generator doesn't read (a point size per vertex...)");
    }
    if (key.vertex.light_count > kMaxLights) {
        return refuse("more lights than kMaxLights");
    }
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        const TexCoordKey& coords = core::at(key.vertex.coords, stage);
        if (coords.gen == TexGen::kSphereMap || static_cast<uint8_t>(coords.gen) > static_cast<uint8_t>(TexGen::kSphereMap)) {
            return refuse("sphere-map (or unknown) texture coordinate generation");
        }
        if (coords.set >= kMaxTexSets || coords.count > 4) {
            return refuse("a texture coordinate set or count out of range");
        }
        const StageKey& ops = core::at(key.fragment.stages, stage);
        for (const StageOp op : {ops.colour_op, ops.alpha_op}) {
            if (op == StageOp::kPremodulate || op == StageOp::kBumpEnvMap || op == StageOp::kBumpEnvMapLuminance ||
                static_cast<uint8_t>(op) > static_cast<uint8_t>(StageOp::kLerp)) {
                return refuse("premodulation, bump mapping or an unknown stage operation");
            }
        }
        for (const uint8_t value : {ops.colour_arg0, ops.colour_arg1, ops.colour_arg2, ops.alpha_arg0, ops.alpha_arg1,
                                    ops.alpha_arg2}) {
            if (arg_source(value) == ArgSource::kConstant || arg_source(value) > ArgSource::kConstant) {
                return refuse("a stage's own constant (D3DTA_CONSTANT)");
            }
        }
    }
    if (key.fragment.program == PixelProgram::kMaterial &&
        (key.fragment.material_passes < 1 || key.fragment.material_passes > 4)) {
        return refuse("a material with no passes or more than four");
    }
    if (static_cast<uint8_t>(key.fragment.program) > static_cast<uint8_t>(PixelProgram::kMaterial)) {
        return refuse("an unknown pixel program");
    }
    return true;
}

uint64_t hash_of(const VertexKey& key) { return fnv1a(key); }
uint64_t hash_of(const FragmentKey& key) { return fnv1a(key); }

std::string describe(const VertexKey& key)
{
    std::string text = "fvf=";
    append_hex(text, key.fvf);
    if (key.lighting) {
        text += " lit(" + std::to_string(key.light_count);
        if (key.colour_vertex) {
            text += ",colour_vertex:" + std::to_string(core::to_underlying(key.diffuse_source)) +
                    std::to_string(core::to_underlying(key.ambient_source)) +
                    std::to_string(core::to_underlying(key.specular_source)) +
                    std::to_string(core::to_underlying(key.emissive_source));
        }
        text += key.specular ? ",specular" : "";
        text += key.normalize_normals ? ",normalize" : "";
        text += ')';
    }
    text += key.local_viewer ? " local_viewer" : "";
    if (key.fog != FogMode::kNone) {
        text += std::string(" fog=") + fog_name(key.fog) + (key.range_fog ? ",range" : "");
    }
    if (key.points) {
        text += key.point_scale ? " points(scaled)" : " points";
    }
    text += key.motion != 0U ? " motion" : "";
    text += key.surface != 0 ? " surface" : "";
    text += key.retro != 0U ? " retro" : "";
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        if ((key.coord_mask & bit(stage)) == 0U) {
            continue;
        }
        const TexCoordKey& coords = core::at(key.coords, stage);
        text += " t" + std::to_string(stage) + "=set" + std::to_string(coords.set);
        text += "/gen" + std::to_string(core::to_underlying(coords.gen));
        if (coords.count != 0) {
            text += "/count" + std::to_string(coords.count);
        }
        text += coords.projected ? "/projected" : "";
    }
    return text;
}

std::string describe(const FragmentKey& key)
{
    std::string text = "program=" + std::to_string(core::to_underlying(key.program));
    if (key.program == PixelProgram::kMaterial) {
        text += " passes=" + std::to_string(key.material_passes) + " modes=";
        for (std::size_t pass = 1; pass < key.material_passes; ++pass) {
            text += std::to_string(core::at(key.material_modes, pass)) + (pass + 1 < key.material_passes ? "," : "");
        }
        text += " alpha_flags=" + std::to_string(key.material_alpha_flags);
    }
    for (std::size_t stage = 0; stage < kStageCount && key.program == PixelProgram::kStages; ++stage) {
        const StageKey& ops = core::at(key.stages, stage);
        if (ops.colour_op == StageOp::kDisable) {
            break;
        }
        text += " s" + std::to_string(stage) + "=";
        append_op(text, ops.colour_op, {ops.colour_arg0, ops.colour_arg1, ops.colour_arg2});
        text += '/';
        append_op(text, ops.alpha_op, {ops.alpha_arg0, ops.alpha_arg1, ops.alpha_arg2});
    }
    text += key.specular_add ? " +specular" : "";
    if (key.fog) {
        text += std::string(" fog") + (key.table_fog != FogMode::kNone ? std::string("(table ") + fog_name(key.table_fog) + ")" : "");
    }
    if (key.alpha_test != CompareFunc::kOff) {
        text += " alpha_test=" + std::to_string(core::to_underlying(key.alpha_test));
    }
    text += key.point_sprite ? " point_sprite" : "";
    text += key.motion != 0U ? " motion" : "";
    if (key.surface != 0) {
        text += " surface=" + std::to_string(key.surface);
    }
    if (key.projected_mask != 0U) {
        text += " projected=" + std::to_string(key.projected_mask);
    }
    return text;
}

} // namespace render::vulkan

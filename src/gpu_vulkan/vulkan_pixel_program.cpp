// NOT ORIGINAL - the game's ps.1.1 shaders recognised from their text (vulkan_pixel_program.h).
#include "gpu_vulkan/vulkan_pixel_program.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/mesh_formats.h"
#include "gpu_vulkan/ff_key.h"

namespace render::vulkan {

namespace {

constexpr std::size_t kMostPasses = 4;
constexpr uint32_t kLastMode = 0xD; // the generator's modes with a blend line: 1 - 0xD

// The text's lines, each without its comment and with its spaces and tabs squeezed out (the generator writes both).
std::vector<std::string> lines_of(std::string_view source)
{
    std::vector<std::string> lines;
    std::string line;
    bool comment = false;
    for (const char ch : source) {
        if (ch == '\n') {
            if (!line.empty()) {
                lines.push_back(line);
            }
            line.clear();
            comment = false;
        } else if (ch == ';') {
            comment = true;
        } else if (!comment && ch != ' ' && ch != '\t' && ch != '\r') {
            line.push_back(ch);
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// One line with its spaces and tabs squeezed out.
std::string squeezed_line(std::string_view line)
{
    std::string kept;
    for (const char ch : line) {
        if (ch != ' ' && ch != '	') {
            kept.push_back(ch);
        }
    }
    return kept;
}

std::string squeezed(std::string_view text)
{
    std::string joined;
    for (const std::string& line : lines_of(text)) {
        joined += line;
        joined += '\n';
    }
    return joined;
}

// The six fixed shaders (render_pc/pixel_shaders.cpp's sources, without their comments).
struct Fixed {
    std::string_view source;
    PixelProgram program;
};
constexpr std::array<Fixed, 6> kFixed{{
    {.source = "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4 r0.rgb,v0,r0\n",
     .program = PixelProgram::kTextureVertexX4},
    {.source = "ps.1.1\nmul_x2 r0.rgb,v0,c0\n+mov r0.a, v0.a\n", .program = PixelProgram::kVertexFactor},
    {.source = "ps.1.1\ndef c1, 1, 1, 1, 1\ndef c2, 0, 0, 0, 0\ntex t0\nsub r0,t0,c0\ndp3 r1,r0,c1\n"
               "add r0.a,r1.a,c0.a\ncnd r0,r0.a,t0,c2\n",
     .program = PixelProgram::kBloomCutoff},
    {.source = "ps.1.1\ntex t0\nmul_x2 r0,v0.a,t0.a\n", .program = PixelProgram::kVertexAlphaTextureAlpha},
    {.source = "ps.1.1\ntex t0\nmul_x2 r0,c4.a,t0.a\n", .program = PixelProgram::kConstantAlphaTextureAlpha},
    {.source = "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\nmul_x4 r0.rgb,v0,r0\n+mul_x2 r0.a,c4.a,t0.a\n",
     .program = PixelProgram::kTextureConstantAlpha},
}};

// A pass after the first: its number, the register its colour is in, the previous pass's register.
struct PassRegisters {
    uint32_t pass = 0;
    const char* reg = "r1";
    const char* previous = "r0";
};

// The blend line the generator writes for `mode` blending a pass over r0 (generated_pixel_shader's switch); none for
// the modes without one.
std::optional<std::string> blend_line(uint32_t mode, const PassRegisters& at)
{
    const uint32_t pass = at.pass;
    const char* const reg = at.reg;
    const char* const previous = at.previous;
    switch (mode) {
    case 1:
        return text::format("mad r0.rgb,%s,%s.a,r0", reg, reg);
    case 2:
        return text::format("mad r0.rgb,%s,c%u.a,r0", reg, pass);
    case 3:
        return text::format("mad r0.rgb,%s,-%s.a,r0", reg, reg);
    case 4:
        return text::format("mad r0.rgb,-%s,c%u.a,r0", reg, pass);
    case kBlendModeAlpha:
        return text::format("lrp r0.rgb,%s.a,%s,r0", reg, reg);
    case kBlendModeFixBlend:
        return text::format("lrp r0.rgb,c%u.a,%s,r0", pass, reg);
    case kBlendModeModulate:
        return text::format("mul r0.rgb,r0,%s.a", reg);
    case kBlendModeFixModulate:
        return text::format("mul r0.rgb,r0,c%u.a", pass);
    case kBlendModeBrighten:
        return text::format("mad r0.rgb,r0,%s.a,r0", reg);
    case kBlendModeFixBrighten:
        return text::format("mad r0.rgb,r0,c%u.a,r0", pass);
    case kBlendModeVertexTexAlpha:
        return text::format("mul v1.rgb,v1,%s.a", reg);
    case kBlendModeLerpPrevious:
        return text::format("lrp r0.rgb,%s.a,%s,r0", previous, reg);
    case kBlendModeLerpFromPrevious:
        return text::format("lrp r0.rgb,%s.a,r0,%s", previous, reg);
    default:
        return std::nullopt;
    }
}

// A material's generated shader (generated_pixel_shader): its passes' textures, the first pass into r0, each further
// pass into r1 and blended over r0 by its mode, then r0 times the vertex colour.
std::optional<ShaderProgram> material_program(const std::vector<std::string>& lines)
{
    std::size_t at = 0;
    const auto next_is = [&lines, &at](std::string_view expected) {
        if (at < lines.size() && core::at(lines, at) == expected) {
            ++at;
            return true;
        }
        return false;
    };
    if (!next_is("ps.1.1")) {
        return std::nullopt;
    }
    uint32_t passes = 0;
    while (passes < kMostPasses && next_is(text::format("text%u", passes))) {
        ++passes;
    }
    if (passes < 2) {
        return std::nullopt;
    }
    ShaderProgram made{.known = true, .program = PixelProgram::kMaterial, .material_passes = static_cast<uint8_t>(passes)};
    if (next_is("movr0.a,t0.a")) {
        made.material_alpha_flags = 1;
    } else if (!next_is("mul_x2r0.a,v0.a,t0.a")) { // (else the vertex alpha times the texture's)
        return std::nullopt;
    }
    if (!next_is("+mul_x2r0.rgb,c0,t0")) {
        return std::nullopt;
    }
    const char* previous = "r0";
    for (uint32_t pass = 1; pass < passes; ++pass) {
        if (!next_is(text::format("mul_x2r1.rgb,c%u,t%u", pass, pass))) {
            return std::nullopt;
        }
        bool texture_alpha = false;
        if (next_is(text::format("+movr1.a,t%u", pass))) {
            texture_alpha = true;
        } else if (!next_is(text::format("+mul_x2r1.a,v0.a,t%u.a", pass)) &&
                   !next_is(text::format("+mul_x2_satr1.a,v0.a,t%u.a", pass))) {
            // (no alpha line: a mode that blends by the previous pass's alpha, 0xC / 0xD)
        }
        uint32_t mode = 0; // (a mode without a blend line: nothing blended)
        for (uint32_t candidate = 1; candidate <= kLastMode; ++candidate) {
            const std::optional<std::string> line =
                blend_line(candidate, {.pass = pass, .reg = "r1", .previous = previous});
            if (line && next_is(squeezed_line(*line))) {
                mode = candidate;
                break;
            }
        }
        if (texture_alpha && mode != kBlendModeLerpPrevious && mode != kBlendModeLerpFromPrevious) {
            made.material_alpha_flags = static_cast<uint8_t>(made.material_alpha_flags | (1U << pass));
        }
        core::at(made.material_modes, pass) = static_cast<uint8_t>(mode);
        previous = "r1";
    }
    if (!next_is("mul_x2r0.rgb,r0,v0") || at != lines.size()) {
        return std::nullopt;
    }
    return made;
}

} // namespace

ShaderProgram classify_pixel_shader(std::string_view source)
{
    const std::string text = squeezed(source);
    for (const Fixed& fixed : kFixed) {
        if (text == squeezed(fixed.source)) {
            return {.known = true, .program = fixed.program};
        }
    }
    const std::optional<ShaderProgram> material = material_program(lines_of(source));
    return material ? *material : ShaderProgram{};
}

void apply_program(const ShaderProgram& program, FragmentKey* key)
{
    key->program = program.program;
    key->material_passes = program.material_passes;
    key->material_alpha_flags = program.material_alpha_flags;
    key->material_modes = program.material_modes;
}

} // namespace render::vulkan

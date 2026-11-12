// NOT ORIGINAL - GLSL to SPIR-V for the ray-tracing work (vulkan_rt_glsl.h).
#include "gpu_vulkan/vulkan_rt_glsl.h"

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <string>
#include <string_view>

#include "gpu_vulkan/glsl_compiler.h"

namespace render::vulkan {

namespace {

constexpr int kGlslVersion = 460;
constexpr int kGlslInputVersion = 100; // (glslang's number for the Vulkan GLSL dialect's input)

EShLanguage language_of(RtShaderStage stage)
{
    switch (stage) {
    case RtShaderStage::kVertex:
        return EShLangVertex;
    case RtShaderStage::kFragment:
        return EShLangFragment;
    case RtShaderStage::kCompute:
    default:
        return EShLangCompute;
    }
}

} // namespace

CompiledShader compile_rt_glsl(RtShaderStage stage, std::string_view source)
{
    // (glslang's process-wide tables: InitializeProcess counts its callers, so this and glsl_compiler.cpp's both hold
    // them for the process)
    static const bool kInitialised = glslang::InitializeProcess();
    static_cast<void>(kInitialised);
    CompiledShader result;
    const EShLanguage language = language_of(stage);
    glslang::TShader shader(language);
    const std::string text(source);
    const char* strings = text.c_str();
    shader.setStrings(&strings, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, kGlslInputVersion);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), kGlslVersion, false, messages)) {
        result.log = std::string(shader.getInfoLog()) + shader.getInfoDebugLog();
        return result;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        result.log = std::string(program.getInfoLog()) + program.getInfoDebugLog();
        return result;
    }
    spv::SpvBuildLogger logger;
    glslang::SpvOptions options;
    options.validate = false;
    glslang::GlslangToSpv(*program.getIntermediate(language), result.spirv, &logger, &options);
    result.log = std::string(shader.getInfoLog()) + logger.getAllMessages();
    result.ok = !result.spirv.empty();
    return result;
}

} // namespace render::vulkan

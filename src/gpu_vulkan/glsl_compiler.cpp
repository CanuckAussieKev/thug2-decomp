// NOT ORIGINAL - GLSL to SPIR-V through glslang, and the fixed-function emulation's shader library (glsl_compiler.h).
#include "gpu_vulkan/glsl_compiler.h"

#include <SPIRV/GlslangToSpv.h>
#include <cstdint>
#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_shaders.h"

namespace render::vulkan {

namespace {

constexpr int kGlslVersion = 450;
constexpr int kGlslInputVersion = 100; // (glslang's number for the Vulkan GLSL dialect's input)

// glslang's process-wide tables, made once (and left for the process: FinalizeProcess at exit would race the other
// static destructors).
void initialise_glslang()
{
    static const bool kInitialised = glslang::InitializeProcess();
    static_cast<void>(kInitialised);
}

EShLanguage language_of(ShaderStage stage)
{
    return stage == ShaderStage::kVertex ? EShLangVertex : EShLangFragment;
}

const std::vector<uint32_t>* found_or_compiled(std::unique_ptr<CompiledShader>& entry, ShaderStage stage,
                                               const std::string& source, std::string* error)
{
    if (!entry) {
        entry = std::make_unique<CompiledShader>(compile_glsl(stage, source));
        if (!entry->ok) {
            entry->log += "\n" + source; // (the source with the messages: they name its lines)
        }
    }
    if (entry->ok) {
        return &entry->spirv;
    }
    if (error) {
        *error = entry->log;
    }
    return nullptr;
}

} // namespace

CompiledShader compile_glsl(ShaderStage stage, std::string_view source)
{
    initialise_glslang();
    CompiledShader result;
    const EShLanguage language = language_of(stage);
    glslang::TShader shader(language);
    const std::string text(source);
    const char* strings = text.c_str();
    shader.setStrings(&strings, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, kGlslInputVersion);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
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
    options.validate = false; // (src/tests/ff_shader_check.cpp validates with SPIRV-Tools)
    glslang::GlslangToSpv(*program.getIntermediate(language), result.spirv, &logger, &options);
    result.log = std::string(shader.getInfoLog()) + logger.getAllMessages();
    result.ok = !result.spirv.empty();
    return result;
}

const std::vector<uint32_t>* ShaderLibrary::vertex(const VertexKey& key, std::string* error)
{
    Entry& entry = vertex_shaders_[key];
    return found_or_compiled(entry, ShaderStage::kVertex, entry ? std::string() : vertex_shader_glsl(key), error);
}

const std::vector<uint32_t>* ShaderLibrary::fragment(const FragmentKey& key, std::string* error)
{
    Entry& entry = fragment_shaders_[key];
    return found_or_compiled(entry, ShaderStage::kFragment, entry ? std::string() : fragment_shader_glsl(key), error);
}

} // namespace render::vulkan

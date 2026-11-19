// NOT ORIGINAL - GLSL to SPIR-V for the ray-tracing work (vulkan_rt_glsl.h).
#include "gpu_vulkan/vulkan_rt_glsl.h"

#include <SPIRV/GlslangToSpv.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <array>
#include <iterator>
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

// (round OM) SPIR-V's words: the header's five, then instructions - each word count << 16 | opcode.
constexpr std::size_t kSpirvHeaderWords = 5;
constexpr uint32_t kOpExtension = 10;
constexpr uint32_t kOpCapability = 17;
constexpr uint32_t kCapabilityRayQuery = 4472;              // RayQueryKHR
constexpr uint32_t kCapabilityOpacityMicromap = 5381;       // RayTracingOpacityMicromapEXT
constexpr std::string_view kMicromapExtension = "SPV_EXT_opacity_micromap";
constexpr uint32_t kWordShift = 16;
constexpr uint32_t kOpcodeMask = 0xFFFF;

std::atomic<bool>& declared_flag()
{
    static std::atomic<bool> flag{false};
    return flag;
}

// The extension's OpExtension: its name, nul-terminated, padded to words.
std::vector<uint32_t> extension_instruction()
{
    const std::size_t words = (kMicromapExtension.size() / sizeof(uint32_t)) + 1;
    std::vector<uint32_t> made(1 + words, 0);
    made.front() = (static_cast<uint32_t>(made.size()) << kWordShift) | kOpExtension;
    std::memcpy(std::next(made.data()), kMicromapExtension.data(), kMicromapExtension.size());
    return made;
}

} // namespace

void set_micromaps_declared(bool declared) { declared_flag().store(declared); }

bool micromaps_declared() { return declared_flag().load(); }

void spirv_micromaps_declared(std::vector<uint32_t>* words, bool declared)
{
    std::vector<uint32_t>& module = *words;
    if (module.size() <= kSpirvHeaderWords) {
        return;
    }
    const std::vector<uint32_t> extension = extension_instruction();
    bool ray_query = false;
    std::size_t capability_at = 0; // (the instruction holding the micromap capability; 0: none)
    std::size_t extension_at = 0;
    std::size_t after_capabilities = kSpirvHeaderWords;
    for (std::size_t at = kSpirvHeaderWords; at < module.size();) {
        const uint32_t count = module[at] >> kWordShift;
        const uint32_t opcode = module[at] & kOpcodeMask;
        if (count == 0 || at + count > module.size()) {
            return; // (not a module this can read)
        }
        if (opcode == kOpCapability) {
            ray_query = ray_query || module[at + 1] == kCapabilityRayQuery;
            capability_at = module[at + 1] == kCapabilityOpacityMicromap ? at : capability_at;
            after_capabilities = at + count;
        } else if (opcode == kOpExtension) {
            if (count == extension.size() && std::equal(extension.begin(), extension.end(), module.begin() + static_cast<std::ptrdiff_t>(at))) {
                extension_at = at;
            }
        } else {
            break; // (the capabilities and extensions come first)
        }
        at += count;
    }
    const bool has = capability_at != 0;
    if (declared && ray_query && !has) {
        // (the capability after the last one, the extension right after it: still before every other OpExtension's
        // section ends)
        const std::array<uint32_t, 2> capability{(2U << kWordShift) | kOpCapability, kCapabilityOpacityMicromap};
        std::vector<uint32_t> inserted(capability.begin(), capability.end());
        if (extension_at == 0) {
            inserted.insert(inserted.end(), extension.begin(), extension.end());
        }
        module.insert(module.begin() + static_cast<std::ptrdiff_t>(after_capabilities), inserted.begin(), inserted.end());
    } else if (!declared && has) {
        if (extension_at != 0) {
            module.erase(module.begin() + static_cast<std::ptrdiff_t>(extension_at),
                         module.begin() + static_cast<std::ptrdiff_t>(extension_at + extension.size()));
        }
        module.erase(module.begin() + static_cast<std::ptrdiff_t>(capability_at),
                     module.begin() + static_cast<std::ptrdiff_t>(capability_at + 2));
    }
}

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
    if (result.ok) {
        spirv_micromaps_declared(&result.spirv, micromaps_declared()); // (round OM)
    }
    return result;
}

} // namespace render::vulkan

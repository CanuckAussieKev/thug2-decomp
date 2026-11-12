// NOT ORIGINAL - thug2_vulkan_rt_materials_check.exe: ray tracing's material table (gpu_vulkan/vulkan_rt_materials.h)
// tried on this machine. It opens the Vulkan Gpu with ray tracing asked for, makes textures through render::Gpu in every
// format the game makes (A8R8G8B8, X8R8G8B8, R5G6B5, X1R5G5B5, A1R5G5B5, P8, DXT1, DXT3, DXT5), registers materials
// with them, and has a compute shader read the table through its GLSL (material_glsl()) at known UVs; what it reads is
// compared with the texels worked out here from the bytes written (exact for the uncompressed formats with nearest
// sampling, within a few 255ths for the block-compressed ones, whose interpolation differs between GPUs):
// - every texel of every format; the material's colour; the address modes (wrap, mirror, clamp, border) outside 0..1;
//   linear filtering; untextured materials, material 0 and ids past the rows; the alpha test helper;
// - a render target as a material's texture (shown white);
// - a texture released while materials use it (they read white, the slot is white), the slot handed out again once
//   the frames that may read it have finished, and the new texture read through it;
// - thousands of materials over a hundred textures (the records' buffer grown, a new set), half of them removed;
// - a second table with textures[] at the device's limit, its last slot used;
// - the validation layer's messages counted (none expected), everything released, the device shut down.
//   thug2_vulkan_rt_materials_check [--device N] [--display N] [--no-validation]
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan ray tracing for this process.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "tests/no_error_dialogs.h"

namespace {

using render::GpuLockedRect;
using render::GpuTexture;
using render::vulkan::Backend;
using render::vulkan::MaterialTable;
namespace value = render::gpu_value;
namespace rt = render::rt;

using Rgba = std::array<float, 4>;

constexpr int kWindowWidth = 320;
constexpr int kWindowHeight = 240;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kSize = 8; // the textures' width and height (two 4x4 blocks each way)
constexpr uint32_t kSmall = 4;
constexpr uint32_t kGroupSize = 64;
constexpr float kByte = 255.0F;
constexpr float kExact = 0.75F / kByte;     // uncompressed, nearest (16-bit formats native or widened)
constexpr float kBlockTolerance = 4.0F / kByte; // block-compressed: the GPUs' interpolation
constexpr float kLinearTolerance = 2.0F / kByte;
constexpr uint32_t kManyTextures = 100;
constexpr uint32_t kManyMaterials = 4000;
constexpr uint32_t kFirstRows = 1024;
constexpr uint32_t kAddressMirror = 2;
constexpr float kHalf = 0.5F;
constexpr float kThird = 1.0F / 3.0F;
constexpr uint32_t kByteShift = 8;
constexpr uint32_t kOpaqueByte = 255;
// the 16-bit formats' channels
constexpr uint32_t kMask5 = 0x1F;
constexpr uint32_t kMask6 = 0x3F;
constexpr uint32_t kGreenAt = 5;
constexpr uint32_t kRedAt565 = 11;
constexpr uint32_t kRedAt1555 = 10;
constexpr uint32_t kAlphaBit1555 = 0x8000;
constexpr float kTop5 = 31.0F;
constexpr float kTop6 = 63.0F;
// the block-compressed formats
constexpr uint32_t kBlockSide = 4;
constexpr std::size_t kBc1BlockBytes = 8;
constexpr std::size_t kBc23BlockBytes = 16;
constexpr std::size_t kBc3IndexBytes = 6;
constexpr uint32_t kBc3IndexBits = 3;
constexpr uint32_t kBc3IndexMask = 7;
constexpr uint32_t kBc3ZeroIndex = 6; // (the six-step mode's 0 and 1)
constexpr uint32_t kBc3OneIndex = 7;
constexpr float kBc3EightSteps = 7.0F;
constexpr float kBc3SixSteps = 5.0F;
constexpr uint32_t kNibbleMask = 0xF;
constexpr float kTop4 = 15.0F;
// the seeds of the textures
constexpr uint32_t kSamplingSeed = 77;
constexpr uint32_t kReleasedSeed = 501;
constexpr uint32_t kReusingSeed = 502;
constexpr uint32_t kManySeeds = 1000;
constexpr uint32_t kLimitSeeds = 2000;
constexpr uint32_t kLimitRows = 16;
constexpr uint32_t kTopSlots = 3;
constexpr int kMismatchesShown = 4;
constexpr std::size_t kQueryBytes = 16;
constexpr std::size_t kAnswerBytes = 32;
constexpr uint32_t kShades = 256;
constexpr Rgba kWhite{1.0F, 1.0F, 1.0F, 1.0F};
constexpr Rgba kTint{0.5F, 0.25F, 1.0F, 0.75F};
constexpr Rgba kGrey{0.5F, 0.5F, 0.5F, 1.0F};
constexpr Rgba kFlat{0.2F, 0.4F, 0.6F, 0.8F};
constexpr std::array<float, 2> kSomewhere{0.3F, 0.7F};
constexpr uint32_t kFarRow = 1U << 30U;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--no-validation") {
            options.validation = false;
        } else if (argument == "--device" && has_value) {
            options.device = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--display" && has_value) {
            options.display = text::to_int(core::at(arguments, ++i));
        }
    }
    return options;
}

int& validation_messages()
{
    static int count = 0;
    return count;
}

void print_line(const std::string& text)
{
    if (text.starts_with("validation ")) {
        ++validation_messages();
    }
    text::print("  %s\n", text.c_str());
}

struct Tally {
    int passed = 0;
    int failed = 0;
    void check(bool ok, const std::string& what)
    {
        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
        text::print("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    }
};

// --- bytes and the texels they stand for (written out from the formats' definitions) ----------------------------

// What makes one texture's bytes differ from another's.
struct Seed {
    uint32_t value = 0;
};

// A hash of a seed and a position (a few multiplications and shifts).
uint32_t mix(Seed seed, uint32_t index)
{
    constexpr uint32_t kSeedFactor = 0x9E3779B1U;
    constexpr uint32_t kIndexFactor = 0x85EBCA77U;
    constexpr uint32_t kMixFactor = 0x2C1B3C6DU;
    constexpr uint32_t kFirstShift = 15;
    constexpr uint32_t kSecondShift = 12;
    uint32_t value = (seed.value * kSeedFactor) ^ (index * kIndexFactor);
    value ^= value >> kFirstShift;
    value *= kMixFactor;
    value ^= value >> kSecondShift;
    return value;
}

std::vector<uint8_t> seeded_bytes(Seed seed, std::size_t count)
{
    std::vector<uint8_t> bytes(count);
    for (std::size_t i = 0; i < count; ++i) {
        core::at(bytes, i) = static_cast<uint8_t>(mix(seed, static_cast<uint32_t>(i)) >> kByteShift);
    }
    return bytes;
}

uint32_t word16(std::span<const uint8_t> bytes, std::size_t at)
{
    return core::at(bytes, at) | (static_cast<uint32_t>(core::at(bytes, at + 1)) << kByteShift);
}

Rgba from_bytes(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha)
{
    return {static_cast<float>(red) / kByte, static_cast<float>(green) / kByte, static_cast<float>(blue) / kByte,
            static_cast<float>(alpha) / kByte};
}

// A 5:6:5 colour as unorm floats.
Rgba colour_565(uint32_t word)
{
    return {static_cast<float>((word >> kRedAt565) & kMask5) / kTop5,
            static_cast<float>((word >> kGreenAt) & kMask6) / kTop6, static_cast<float>(word & kMask5) / kTop5, 1.0F};
}

// A 4x4 block's colours (BC1's half: the two endpoints and the indices), texel (column, row) of it; `three`: BC1's
// three-colour mode allowed (c0 <= c1), with index 3 transparent black.
Rgba block_colour(std::span<const uint8_t> block, uint32_t column, uint32_t row, bool three)
{
    const uint32_t c0 = word16(block, 0);
    const uint32_t c1 = word16(block, 2);
    const Rgba first = colour_565(c0);
    const Rgba second = colour_565(c1);
    uint32_t indices = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        indices |= static_cast<uint32_t>(core::at(block, 4 + i)) << (kByteShift * i);
    }
    const uint32_t index = (indices >> (2U * ((row * kBlockSide) + column))) & 3U;
    Rgba out{};
    const bool four = c0 > c1 || !three;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const float end0 = core::at(first, channel);
        const float end1 = core::at(second, channel);
        switch (index) {
        case 0:
            core::at(out, channel) = end0;
            break;
        case 1:
            core::at(out, channel) = end1;
            break;
        case 2:
            core::at(out, channel) = four ? end0 + ((end1 - end0) * kThird) : (end0 + end1) * kHalf;
            break;
        default:
            core::at(out, channel) = four ? end1 + ((end0 - end1) * kThird) : 0.0F;
            break;
        }
    }
    core::at(out, 3) = (!four && index == 3) ? 0.0F : 1.0F;
    return out;
}

// BC3's alpha block, texel (column, row).
float block_alpha_bc3(std::span<const uint8_t> block, uint32_t column, uint32_t row)
{
    const float a0 = static_cast<float>(core::at(block, 0)) / kByte;
    const float a1 = static_cast<float>(core::at(block, 1)) / kByte;
    uint64_t bits = 0;
    for (std::size_t i = 0; i < kBc3IndexBytes; ++i) {
        bits |= static_cast<uint64_t>(core::at(block, 2 + i)) << (kByteShift * i);
    }
    const auto index =
        static_cast<uint32_t>((bits >> (kBc3IndexBits * ((row * kBlockSide) + column))) & kBc3IndexMask);
    if (index == 0) {
        return a0;
    }
    if (index == 1) {
        return a1;
    }
    if (a0 > a1) {
        const auto step = static_cast<float>(index - 1);
        return (((kBc3EightSteps - step) * a0) + (step * a1)) / kBc3EightSteps;
    }
    if (index == kBc3ZeroIndex) {
        return 0.0F;
    }
    if (index == kBc3OneIndex) {
        return 1.0F;
    }
    const auto step = static_cast<float>(index - 1);
    return (((kBc3SixSteps - step) * a0) + (step * a1)) / kBc3SixSteps;
}

// Where a texel is in a level.
struct Texel {
    uint32_t column = 0;
    uint32_t row = 0;
};

// A texel of a level of `format` (`width` wide) whose Direct3D 9 bytes are `bytes` (tightly packed rows).
Rgba expected_texel(uint32_t format, std::span<const uint8_t> bytes, uint32_t width, Texel at)
{
    const std::size_t pixel = (static_cast<std::size_t>(at.row) * width) + at.column;
    switch (format) {
    case value::kFormatA8R8G8B8:
    case value::kFormatX8R8G8B8: {
        const std::size_t first = pixel * 4;
        const uint32_t alpha = format == value::kFormatX8R8G8B8 ? kOpaqueByte : core::at(bytes, first + 3);
        return from_bytes(core::at(bytes, first + 2), core::at(bytes, first + 1), core::at(bytes, first), alpha);
    }
    case value::kFormatR5G6B5:
        return colour_565(word16(bytes, pixel * 2));
    case value::kFormatX1R5G5B5:
    case value::kFormatA1R5G5B5: {
        const uint32_t word = word16(bytes, pixel * 2);
        const bool has_alpha = format == value::kFormatA1R5G5B5;
        return {static_cast<float>((word >> kRedAt1555) & kMask5) / kTop5,
                static_cast<float>((word >> kGreenAt) & kMask5) / kTop5, static_cast<float>(word & kMask5) / kTop5,
                (!has_alpha || (word & kAlphaBit1555) != 0) ? 1.0F : 0.0F};
    }
    case value::kFormatP8: {
        // (the backend's palette: a grey ramp, opaque - vulkan_formats.h)
        const uint32_t index = core::at(bytes, pixel);
        return from_bytes(index, index, index, kOpaqueByte);
    }
    default:
        break;
    }
    // the block-compressed formats: blocks of 8 (DXT1) or 16 bytes in rows of width / 4
    const bool dxt1 = format == value::kFormatDxt1;
    const std::size_t block_bytes = dxt1 ? kBc1BlockBytes : kBc23BlockBytes;
    const std::size_t block =
        ((static_cast<std::size_t>(at.row / kBlockSide) * (width / kBlockSide)) + (at.column / kBlockSide)) *
        block_bytes;
    const std::span<const uint8_t> whole = bytes.subspan(block, block_bytes);
    const uint32_t column = at.column % kBlockSide;
    const uint32_t row = at.row % kBlockSide;
    if (dxt1) {
        return block_colour(whole, column, row, true);
    }
    Rgba colour = block_colour(whole.subspan(kBc1BlockBytes), column, row, false);
    if (format == value::kFormatDxt3) {
        const uint32_t texel = (row * kBlockSide) + column;
        const uint32_t nibble = (core::at(whole, texel / 2) >> (kBlockSide * (texel % 2))) & kNibbleMask;
        core::at(colour, 3) = static_cast<float>(nibble) / kTop4;
    } else {
        core::at(colour, 3) = block_alpha_bc3(whole, column, row);
    }
    return colour;
}

const char* format_name(uint32_t format)
{
    switch (format) {
    case value::kFormatA8R8G8B8:
        return "A8R8G8B8";
    case value::kFormatX8R8G8B8:
        return "X8R8G8B8";
    case value::kFormatR5G6B5:
        return "R5G6B5";
    case value::kFormatX1R5G5B5:
        return "X1R5G5B5";
    case value::kFormatA1R5G5B5:
        return "A1R5G5B5";
    case value::kFormatP8:
        return "P8";
    case value::kFormatDxt1:
        return "DXT1";
    case value::kFormatDxt3:
        return "DXT3";
    case value::kFormatDxt5:
        return "DXT5";
    default:
        return "other";
    }
}

// --- textures ----------------------------------------------------------------------------------------------------

struct MadeTexture {
    GpuTexture* texture = nullptr;
    uint32_t format = 0;
    uint32_t width = 0;
    std::vector<uint8_t> bytes; // level 0, Direct3D 9's layout, tightly packed
};

// What a texture is made as.
struct TextureSpec {
    uint32_t format = value::kFormatA8R8G8B8;
    uint32_t size = kSize; // width and height
    Seed seed;
    bool target = false; // a render target (nothing written)
};

// A one-level texture, its level filled with seeded bytes through a lock.
MadeTexture make_texture(render::Gpu& gpu, const TextureSpec& spec)
{
    MadeTexture made;
    made.format = spec.format;
    made.width = spec.size;
    const uint32_t usage = spec.target ? value::kUsageRenderTarget : 0;
    const uint32_t pool = spec.target ? value::kPoolDefault : value::kPoolManaged;
    if (gpu.create_texture(
            {.width = spec.size, .height = spec.size, .levels = 1, .usage = usage, .format = spec.format, .pool = pool},
            &made.texture) != value::kResultOk) {
        made.texture = nullptr;
        return made;
    }
    if (spec.target) {
        return made;
    }
    const render::vulkan::LevelLayout layout =
        render::vulkan::d3d_level_layout(spec.format, render::vulkan::Extent{.width = spec.size, .height = spec.size});
    made.bytes = seeded_bytes(spec.seed, layout.bytes());
    GpuLockedRect locked;
    if (gpu.lock_texture(made.texture, 0, &locked, nullptr, 0) == value::kResultOk) {
        const std::span<uint8_t> into(static_cast<uint8_t*>(locked.bits),
                                      (static_cast<std::size_t>(layout.rows - 1) * static_cast<uint32_t>(locked.pitch)) +
                                          layout.pitch);
        for (uint32_t row = 0; row < layout.rows; ++row) {
            std::copy_n(made.bytes.begin() + (static_cast<std::ptrdiff_t>(row) * layout.pitch), layout.pitch,
                        into.begin() + (static_cast<std::ptrdiff_t>(row) * locked.pitch));
        }
        gpu.unlock_texture(made.texture, 0);
    }
    return made;
}

Rgba texel_of(const MadeTexture& made, uint32_t column, uint32_t row)
{
    return expected_texel(made.format, made.bytes, made.width, Texel{.column = column, .row = row});
}

float tolerance_of(uint32_t format)
{
    return format == value::kFormatDxt1 || format == value::kFormatDxt3 || format == value::kFormatDxt5 ? kBlockTolerance
                                                                                                          : kExact;
}

// --- the probe: a compute shader that reads the table ------------------------------------------------------------

struct Query {
    uint32_t material = 0;
    uint32_t pad = 0;
    std::array<float, 2> uv{};
};
struct Answer {
    Rgba sample{};
    uint32_t alpha_passes = 0;
    std::array<uint32_t, 3> pad{};
};
static_assert(sizeof(Query) == kQueryBytes && sizeof(Answer) == kAnswerBytes);

constexpr std::string_view kProbeHeader = "#version 460\n#define THUG2_MATERIAL_SET 1\n";
constexpr std::string_view kProbeBody = R"(
layout(local_size_x = 64) in;
struct Query { uint material; uint pad; vec2 uv; };
struct Answer { vec4 sample_value; uint alpha_passes; uint pad0; uint pad1; uint pad2; };
layout(set = 0, binding = 0, std430) readonly buffer Queries { Query queries[]; };
layout(set = 0, binding = 1, std430) writeonly buffer Answers { Answer answers[]; };
layout(push_constant) uniform Count { uint count; } probe;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= probe.count) {
        return;
    }
    Query q = queries[i];
    answers[i].sample_value = material_sample(q.material, q.uv);
    answers[i].alpha_passes = material_alpha_passes_at(q.material, q.uv) ? 1u : 0u;
}
)";

class Probe {
public:
    Probe() = default;
    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;
    Probe(Probe&&) = delete;
    Probe& operator=(Probe&&) = delete;
    ~Probe() = default;

    bool init(VkDevice device, VkDescriptorSetLayout table_layout)
    {
        device_ = device;
        const std::string source = std::string(kProbeHeader) + std::string(render::vulkan::material_glsl()) +
                                   std::string(kProbeBody);
        const render::vulkan::CompiledShader compiled =
            render::vulkan::compile_rt_glsl(render::vulkan::RtShaderStage::kCompute, source);
        if (!compiled.ok) {
            text::print("  the probe's shader: %s\n", compiled.log.c_str());
            return false;
        }
        const VkShaderModuleCreateInfo module_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = 0,
                                                   .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                                   .pCode = compiled.spirv.data()};
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &module_info, nullptr, &module) != VK_SUCCESS) {
            return false;
        }
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
            VkDescriptorSetLayoutBinding{.binding = 0,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         .descriptorCount = 1,
                                         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                         .pImmutableSamplers = nullptr},
            VkDescriptorSetLayoutBinding{.binding = 1,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         .descriptorCount = 1,
                                         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                         .pImmutableSamplers = nullptr}};
        const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                       .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                       .pBindings = bindings.data()};
        bool ok = vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own_layout_) == VK_SUCCESS;
        const std::array<VkDescriptorSetLayout, 2> sets{own_layout_, table_layout};
        const VkPushConstantRange range{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 4};
        const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .setLayoutCount = static_cast<uint32_t>(sets.size()),
                                                     .pSetLayouts = sets.data(),
                                                     .pushConstantRangeCount = 1,
                                                     .pPushConstantRanges = &range};
        ok = ok && vkCreatePipelineLayout(device, &layout_info, nullptr, &layout_) == VK_SUCCESS;
        const VkComputePipelineCreateInfo pipeline_info{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                      .pNext = nullptr,
                      .flags = 0,
                      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                      .module = module,
                      .pName = "main",
                      .pSpecializationInfo = nullptr},
            .layout = layout_,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1};
        ok = ok && vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_) == VK_SUCCESS;
        vkDestroyShaderModule(device, module, nullptr);
        return ok;
    }

    void shutdown()
    {
        if (device_ == nullptr) {
            return;
        }
        vkDestroyPipeline(device_, pipeline_, nullptr);
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, own_layout_, nullptr);
        device_ = nullptr;
    }

    // The table's changes made visible, the queries run, the answers read back (the backend's lock taken here).
    bool run(MaterialTable& table, std::span<const Query> queries, std::vector<Answer>* answers)
    {
        Backend& state = render::vulkan::backend();
        const std::scoped_lock lock(state.mutex);
        if (!table.make_visible(state)) {
            return false;
        }
        render::vulkan::DeviceBuffer in;
        render::vulkan::DeviceBuffer out;
        const VkDeviceSize in_bytes = std::max<VkDeviceSize>(queries.size_bytes(), sizeof(Query));
        const VkDeviceSize out_bytes = std::max<VkDeviceSize>(queries.size() * sizeof(Answer), sizeof(Answer));
        bool ok = state.memory.create_buffer(in_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                             render::vulkan::BufferPlace::kUpload, &in) &&
                  state.memory.create_buffer(out_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                             render::vulkan::BufferPlace::kReadBack, &out);
        if (ok) {
            std::memcpy(in.mapped, queries.data(), queries.size_bytes());
            state.memory.flush(in, 0, in_bytes);
            VkCommandBuffer commands = render::vulkan::outside_pass(state);
            const std::array<VkDescriptorBufferInfo, 2> buffers{
                VkDescriptorBufferInfo{.buffer = in.buffer, .offset = 0, .range = VK_WHOLE_SIZE},
                VkDescriptorBufferInfo{.buffer = out.buffer, .offset = 0, .range = VK_WHOLE_SIZE}};
            std::array<VkWriteDescriptorSet, 2> writes{};
            for (uint32_t i = 0; i < 2; ++i) {
                core::at(writes, i) = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                       .pNext = nullptr,
                                       .dstSet = VK_NULL_HANDLE,
                                       .dstBinding = i,
                                       .dstArrayElement = 0,
                                       .descriptorCount = 1,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                       .pImageInfo = nullptr,
                                       .pBufferInfo = &core::at(buffers, i),
                                       .pTexelBufferView = nullptr};
            }
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
            vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0,
                                      static_cast<uint32_t>(writes.size()), writes.data());
            const VkDescriptorSet table_set = table.descriptor_set();
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 1, 1, &table_set, 0, nullptr);
            const auto count = static_cast<uint32_t>(queries.size());
            vkCmdPushConstants(commands, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(count), &count);
            vkCmdDispatch(commands, (count + kGroupSize - 1) / kGroupSize, 1, 1);
            const VkMemoryBarrier to_host{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                          .pNext = nullptr,
                                          .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                          .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                                 &to_host, 0, nullptr, 0, nullptr);
            ok = render::vulkan::finish_work(state);
        }
        if (ok) {
            vmaInvalidateAllocation(state.memory.allocator(), out.allocation, 0, VK_WHOLE_SIZE);
            answers->resize(queries.size());
            std::memcpy(answers->data(), out.mapped, queries.size() * sizeof(Answer));
        }
        state.memory.destroy_buffer(&in);
        state.memory.destroy_buffer(&out);
        return ok;
    }

private:
    VkDevice device_ = nullptr;
    VkDescriptorSetLayout own_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

// --- the checks ----------------------------------------------------------------------------------------------------

// What a query should read: a colour within a tolerance, and whether the alpha test passes (-1: not checked).
struct Expectation {
    Rgba colour{};
    float tolerance = kExact;
    int alpha_passes = -1;
};

struct Batch {
    std::vector<Query> queries;
    std::vector<Expectation> expected;
    void add(rt::MaterialId material, float u, float v, const Expectation& expectation)
    {
        queries.push_back({.material = material, .pad = 0, .uv = {u, v}});
        expected.push_back(expectation);
    }
};

Rgba times(const Rgba& texel, const Rgba& colour)
{
    Rgba out{};
    for (std::size_t channel = 0; channel < 4; ++channel) {
        core::at(out, channel) = core::at(texel, channel) * core::at(colour, channel);
    }
    return out;
}

Rgba mean(const Rgba& first, const Rgba& second)
{
    Rgba out{};
    for (std::size_t channel = 0; channel < 4; ++channel) {
        core::at(out, channel) = (core::at(first, channel) + core::at(second, channel)) * kHalf;
    }
    return out;
}

// The batch run and compared; the first mismatches printed.
bool run_batch(Probe& probe, MaterialTable& table, const Batch& batch, const std::string& what, Tally* tally)
{
    std::vector<Answer> answers;
    if (!probe.run(table, batch.queries, &answers)) {
        tally->check(false, what + " (the probe didn't run)");
        return false;
    }
    int wrong = 0;
    for (std::size_t i = 0; i < answers.size(); ++i) {
        const Answer& got = core::at(answers, i);
        const Expectation& want = core::at(batch.expected, i);
        bool same = true;
        for (std::size_t channel = 0; channel < 4; ++channel) {
            same = same &&
                   std::fabs(core::at(got.sample, channel) - core::at(want.colour, channel)) <= want.tolerance;
        }
        if (want.alpha_passes >= 0) {
            same = same && std::cmp_equal(got.alpha_passes, want.alpha_passes);
        }
        if (!same && ++wrong <= kMismatchesShown) {
            const Query& query = core::at(batch.queries, i);
            text::print("    material %u at (%.4f, %.4f): read (%.4f %.4f %.4f %.4f) alpha %u, expected (%.4f %.4f %.4f "
                        "%.4f) alpha %d\n",
                        query.material, static_cast<double>(core::at(query.uv, 0)),
                        static_cast<double>(core::at(query.uv, 1)), static_cast<double>(core::at(got.sample, 0)),
                        static_cast<double>(core::at(got.sample, 1)), static_cast<double>(core::at(got.sample, 2)),
                        static_cast<double>(core::at(got.sample, 3)), got.alpha_passes,
                        static_cast<double>(core::at(want.colour, 0)), static_cast<double>(core::at(want.colour, 1)),
                        static_cast<double>(core::at(want.colour, 2)), static_cast<double>(core::at(want.colour, 3)),
                        want.alpha_passes);
        }
    }
    const bool ok = wrong == 0;
    tally->check(ok, text::format("%s (%u reads, %d wrong)", what.c_str(), static_cast<uint32_t>(answers.size()), wrong));
    return ok;
}

float centre(uint32_t texel, uint32_t size) { return (static_cast<float>(texel) + kHalf) / static_cast<float>(size); }

rt::Material point_material(GpuTexture* texture)
{
    rt::Material material;
    material.texture = texture;
    material.filter = value::kFilterPoint;
    return material;
}

void release(render::Gpu& gpu, MadeTexture* made)
{
    if (made->texture != nullptr) {
        gpu.release_texture(made->texture);
        made->texture = nullptr;
    }
}

// Every texel of every format, the colour, untextured materials, material 0 and ids past the rows.
void check_formats(render::Gpu& gpu, Probe& probe, MaterialTable& table, Tally* tally)
{
    constexpr std::array<uint32_t, 9> kFormats{value::kFormatA8R8G8B8, value::kFormatX8R8G8B8, value::kFormatR5G6B5,
                                               value::kFormatX1R5G5B5, value::kFormatA1R5G5B5, value::kFormatP8,
                                               value::kFormatDxt1,     value::kFormatDxt3,     value::kFormatDxt5};
    for (const uint32_t format : kFormats) {
        MadeTexture made = make_texture(gpu, {.format = format, .seed = Seed{format}});
        if (made.texture == nullptr) {
            tally->check(false, text::format("%s made", format_name(format)));
            continue;
        }
        const rt::MaterialId plain = rt::next_material_id();
        table.set(plain, point_material(made.texture));
        rt::Material tinted = point_material(made.texture);
        tinted.colour = kTint;
        const rt::MaterialId coloured = rt::next_material_id();
        table.set(coloured, tinted);
        Batch batch;
        for (uint32_t row = 0; row < kSize; ++row) {
            for (uint32_t column = 0; column < kSize; ++column) {
                const Rgba texel = texel_of(made, column, row);
                batch.add(plain, centre(column, kSize), centre(row, kSize),
                          {.colour = texel, .tolerance = tolerance_of(format)});
                batch.add(coloured, centre(column, kSize), centre(row, kSize),
                          {.colour = times(texel, tinted.colour), .tolerance = tolerance_of(format)});
            }
        }
        run_batch(probe, table, batch,
                  text::format("%s: every texel, plain and times the colour (slot %u)", format_name(format),
                               table.slot_of(made.texture)),
                  tally);
        table.remove(plain);
        table.remove(coloured);
        release(gpu, &made);
    }
    // untextured, material 0, an id past every row
    rt::Material untextured;
    untextured.colour = kFlat;
    const rt::MaterialId flat = rt::next_material_id();
    table.set(flat, untextured);
    Batch batch;
    batch.add(flat, core::at(kSomewhere, 0), core::at(kSomewhere, 1), {.colour = untextured.colour});
    batch.add(0, kHalf, kHalf, {.colour = kWhite});
    batch.add(kFarRow, kHalf, kHalf, {.colour = kWhite});
    run_batch(probe, table, batch, "untextured: its colour; material 0 and an id past the rows: white", tally);
    table.remove(flat);
}

// The address modes outside 0..1, linear filtering, the alpha test.
void check_sampling(render::Gpu& gpu, Probe& probe, MaterialTable& table, Tally* tally)
{
    MadeTexture made = make_texture(gpu, {.seed = Seed{kSamplingSeed}});
    const std::array<uint32_t, 4> modes{value::kAddressWrap, kAddressMirror, value::kAddressClamp, value::kAddressBorder};
    const std::array<const char*, 4> names{"wrap", "mirror", "clamp", "border"};
    // the texel an address mode reads at texel coordinate t (-1: the border)
    const auto address = [](uint32_t mode, int texel) {
        const int size = static_cast<int>(kSize);
        switch (mode) {
        case kAddressMirror: {
            const int folded = ((texel % (2 * size)) + (2 * size)) % (2 * size);
            return folded < size ? folded : (2 * size) - 1 - folded;
        }
        case value::kAddressClamp:
            return std::clamp(texel, 0, size - 1);
        case value::kAddressBorder:
            return texel < 0 || texel >= size ? -1 : texel;
        default:
            return ((texel % size) + size) % size;
        }
    };
    for (std::size_t mode = 0; mode < modes.size(); ++mode) {
        rt::Material material = point_material(made.texture);
        material.address_u = core::at(modes, mode);
        material.address_v = core::at(modes, (mode + 1) % modes.size());
        const rt::MaterialId id = rt::next_material_id();
        table.set(id, material);
        Batch batch;
        for (int ty = -3 * static_cast<int>(kSize) / 2; ty < 3 * static_cast<int>(kSize); ty += 3) {
            for (int tx = -2 * static_cast<int>(kSize); tx < 3 * static_cast<int>(kSize); tx += 2) {
                const int column = address(material.address_u, tx);
                const int row = address(material.address_v, ty);
                const Rgba want = column < 0 || row < 0 ? Rgba{}
                                                        : texel_of(made, static_cast<uint32_t>(column),
                                                                   static_cast<uint32_t>(row));
                batch.add(id, (static_cast<float>(tx) + kHalf) / static_cast<float>(kSize),
                          (static_cast<float>(ty) + kHalf) / static_cast<float>(kSize), {.colour = want});
            }
        }
        run_batch(probe, table, batch,
                  text::format("address u %s, v %s outside 0..1", core::at(names, mode),
                               core::at(names, (mode + 1) % modes.size())),
                  tally);
        table.remove(id);
    }
    // linear: a texel's centre reads it; halfway between two texels reads their mean
    rt::Material linear = point_material(made.texture);
    linear.filter = value::kFilterLinear;
    const rt::MaterialId smooth = rt::next_material_id();
    table.set(smooth, linear);
    Batch blended;
    for (uint32_t row = 0; row < kSize; ++row) {
        for (uint32_t column = 0; column + 1 < kSize; ++column) {
            const Rgba left = texel_of(made, column, row);
            const Rgba right = texel_of(made, column + 1, row);
            blended.add(smooth, centre(column, kSize), centre(row, kSize),
                        {.colour = left, .tolerance = kLinearTolerance});
            blended.add(smooth, static_cast<float>(column + 1) / static_cast<float>(kSize), centre(row, kSize),
                        {.colour = mean(left, right), .tolerance = kLinearTolerance});
        }
    }
    run_batch(probe, table, blended, "linear: texel centres, and halfway between texels", tally);
    table.remove(smooth);
    // the alpha test: GREATEREQUAL against the 8-bit reference; a material without it always passes
    constexpr uint32_t kReference = 128;
    rt::Material tested = point_material(made.texture);
    tested.flags = rt::kAlphaTested;
    tested.alpha_ref = static_cast<float>(kReference) / kByte;
    const rt::MaterialId cutout = rt::next_material_id();
    table.set(cutout, tested);
    const rt::MaterialId solid = rt::next_material_id();
    table.set(solid, point_material(made.texture));
    Batch alpha;
    int passing = 0;
    for (uint32_t row = 0; row < kSize; ++row) {
        for (uint32_t column = 0; column < kSize; ++column) {
            const Rgba texel = texel_of(made, column, row);
            const bool passes = std::lround(core::at(texel, 3) * kByte) >= static_cast<long>(kReference);
            passing += passes ? 1 : 0;
            alpha.add(cutout, centre(column, kSize), centre(row, kSize),
                      {.colour = texel, .alpha_passes = passes ? 1 : 0});
            alpha.add(solid, centre(column, kSize), centre(row, kSize), {.colour = texel, .alpha_passes = 1});
        }
    }
    run_batch(probe, table, alpha,
              text::format("the alpha test at %u / 255 (%d of %u texels pass); untested: all", kReference, passing,
                           kSize * kSize),
              tally);
    table.remove(cutout);
    table.remove(solid);
    release(gpu, &made);
    // a render target as a texture: white (its layout changes within the frame)
    MadeTexture target = make_texture(gpu, {.target = true});
    const rt::MaterialId drawn = rt::next_material_id();
    table.set(drawn, point_material(target.texture));
    Batch drawn_batch;
    drawn_batch.add(drawn, kHalf, kHalf, {.colour = kWhite});
    run_batch(probe, table, drawn_batch, "a render target as a material's texture: white", tally);
    table.remove(drawn);
    release(gpu, &target);
}

// A texture released while materials use it; its slot handed out again later.
void check_release(render::Gpu& gpu, Probe& probe, MaterialTable& table, Tally* tally)
{
    MadeTexture first = make_texture(gpu, {.size = kSmall, .seed = Seed{kReleasedSeed}});
    rt::Material tinted = point_material(first.texture);
    tinted.colour = kGrey;
    const rt::MaterialId user = rt::next_material_id();
    table.set(user, tinted);
    const rt::MaterialId other = rt::next_material_id();
    table.set(other, point_material(first.texture));
    Batch before;
    before.add(user, centre(1, kSmall), centre(2, kSmall), {.colour = times(texel_of(first, 1, 2), tinted.colour)});
    run_batch(probe, table, before, "a texture's material before it is released", tally);
    const uint32_t slot = table.slot_of(first.texture);
    const uint32_t used_before = table.slots_in_use();
    release(gpu, &first);
    tally->check(table.slots_in_use() == used_before - 1 && table.record(user).texture == 0 &&
                     table.record(other).texture == 0,
                 text::format("released: slot %u freed, its two materials on slot 0", slot));
    Batch after;
    after.add(user, centre(1, kSmall), centre(2, kSmall), {.colour = tinted.colour});
    after.add(other, centre(3, kSmall), centre(0, kSmall), {.colour = kWhite});
    run_batch(probe, table, after, "released while used: its materials read white", tally);
    // (the run finished every submission; the next make_visible hands the slot out again)
    run_batch(probe, table, after, "and again a frame later", tally);
    MadeTexture second = make_texture(gpu, {.size = kSmall, .seed = Seed{kReusingSeed}});
    const rt::MaterialId next = rt::next_material_id();
    table.set(next, point_material(second.texture));
    tally->check(table.slot_of(second.texture) == slot,
                 text::format("the slot handed out again: %u (the new texture's %u)", slot,
                              table.slot_of(second.texture)));
    Batch reused;
    for (uint32_t row = 0; row < kSmall; ++row) {
        for (uint32_t column = 0; column < kSmall; ++column) {
            reused.add(next, centre(column, kSmall), centre(row, kSmall), {.colour = texel_of(second, column, row)});
        }
    }
    reused.add(user, kHalf, kHalf, {.colour = tinted.colour});
    run_batch(probe, table, reused, "the new texture read through the reused slot; the old materials still white", tally);
    table.remove(user);
    table.remove(other);
    table.remove(next);
    release(gpu, &second);
}

// Thousands of materials over a hundred textures: the rows grow; then half removed.
void check_many(render::Gpu& gpu, Probe& probe, MaterialTable& table, Tally* tally)
{
    std::vector<MadeTexture> textures;
    textures.reserve(kManyTextures);
    for (uint32_t i = 0; i < kManyTextures; ++i) {
        textures.push_back(make_texture(gpu, {.size = kSmall, .seed = Seed{kManySeeds + i}}));
    }
    const VkDescriptorSet set_before = table.descriptor_set();
    const uint32_t rows_before = table.rows();
    struct Placed {
        rt::MaterialId id = 0;
        uint32_t texture = 0;
        Rgba colour{};
    };
    std::vector<Placed> placed;
    placed.reserve(kManyMaterials);
    for (uint32_t i = 0; i < kManyMaterials; ++i) {
        rt::Material material = point_material(core::at(textures, i % kManyTextures).texture);
        const auto shade = static_cast<float>(i % kShades) / kByte;
        material.colour = {shade, 1.0F - shade, 1.0F, 1.0F};
        const rt::MaterialId id = rt::next_material_id();
        table.set(id, material);
        placed.push_back({.id = id, .texture = i % kManyTextures, .colour = material.colour});
    }
    Batch batch;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        const Placed& one = core::at(placed, i);
        const auto column = static_cast<uint32_t>(i % kSmall);
        const auto row = static_cast<uint32_t>((i / kSmall) % kSmall);
        batch.add(one.id, centre(column, kSmall), centre(row, kSmall),
                  {.colour = times(texel_of(core::at(textures, one.texture), column, row), one.colour),
                   .tolerance = 1.0F / kByte});
    }
    run_batch(probe, table, batch,
              text::format("%u materials over %u textures", kManyMaterials, kManyTextures), tally);
    tally->check(table.rows() > rows_before && table.descriptor_set() != set_before,
                 text::format("the rows grew from %u to %u, a new descriptor set", rows_before, table.rows()));
    for (std::size_t i = 0; i < placed.size(); i += 2) {
        table.remove(core::at(placed, i).id);
        core::at(batch.expected, i).colour = kWhite;
    }
    run_batch(probe, table, batch, "half of them removed: those read white", tally);
    for (std::size_t i = 1; i < placed.size(); i += 2) {
        table.remove(core::at(placed, i).id);
    }
    for (MadeTexture& made : textures) {
        release(gpu, &made);
    }
    tally->check(table.slots_in_use() == 0, "every texture released: no slot in use");
}

// A second table with textures[] at the device's limit, its last slot used.
void check_limit(render::Gpu& gpu, Tally* tally)
{
    Backend& state = render::vulkan::backend();
    const uint32_t limit = MaterialTable::device_slot_limit(state.context.physical_device());
    MaterialTable big;
    bool made = false;
    {
        const std::scoped_lock lock(state.mutex);
        made = big.init(state, {.texture_slots = limit, .first_rows = kLimitRows, .first_slot = limit - kTopSlots});
    }
    tally->check(made && big.texture_slots() == limit,
                 text::format("a table with %u texture slots (the device's limit less %u for other sets, at most %u)",
                              limit, MaterialTable::kReservedSampledImages, MaterialTable::kMostTextureSlots));
    if (!made) {
        return;
    }
    Probe probe;
    if (!probe.init(state.context.device(), big.layout())) {
        tally->check(false, "the probe for it made");
    } else {
        std::vector<MadeTexture> textures;
        Batch batch;
        textures.reserve(kTopSlots);
        for (uint32_t i = 0; i < kTopSlots; ++i) {
            textures.push_back(make_texture(gpu, {.size = kSmall, .seed = Seed{kLimitSeeds + i}}));
            const rt::MaterialId id = rt::next_material_id();
            big.set(id, point_material(textures.back().texture));
            for (uint32_t row = 0; row < kSmall; ++row) {
                for (uint32_t column = 0; column < kSmall; ++column) {
                    batch.add(id, centre(column, kSmall), centre(row, kSmall),
                              {.colour = texel_of(textures.back(), column, row)});
                }
            }
        }
        const uint32_t last = big.slot_of(textures.back().texture);
        tally->check(last == limit - 1, text::format("the last texture in the last slot (%u)", last));
        run_batch(probe, big, batch, "three textures read through the top slots", tally);
        {
            const std::scoped_lock lock(state.mutex);
            for (const MadeTexture& one : textures) {
                big.texture_released(state, render::vulkan::object(one.texture));
            }
        }
        for (MadeTexture& one : textures) {
            release(gpu, &one);
        }
        {
            const std::scoped_lock lock(state.mutex);
            render::vulkan::finish_work(state);
        }
        probe.shutdown();
    }
    const std::scoped_lock lock(state.mutex);
    render::vulkan::finish_work(state);
    big.shutdown(state);
}

int window_position(int display)
{
    int displays = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&displays);
    int chosen = display;
    if (chosen <= 0) {
        chosen = displays > 1 ? 2 : 1;
    }
    int position = static_cast<int>(SDL_WINDOWPOS_CENTERED);
    if (ids != nullptr && chosen <= displays) {
        const std::span<const SDL_DisplayID> listed(ids, static_cast<size_t>(displays));
        position = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(core::at(listed, chosen - 1)));
    }
    SDL_free(ids);
    return position;
}

int run(const Options& options, SDL_Window* window)
{
    render::vulkan::set_context_options(
        {.validation = options.validation, .device_index = options.device, .on_message = print_line, .ray_tracing = true},
        window);
    render::GpuContext& context = render::vulkan::gpu_context();
    render::Gpu& gpu = render::vulkan::gpu();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return render::vulkan::loader_version() == 0 ? 2 : 1;
    }
    Backend& state = render::vulkan::backend();
    text::print("validation layer: %s\n", state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowWidth,
                       .height = kWindowHeight,
                       .windowed = true,
                       .vsync = true,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24X8});
    if (context.create_device({.width = kWindowWidth,
                               .height = kWindowHeight,
                               .hardware_vertices = true,
                               .samples = 0,
                               .sample_quality = 0}) != value::kResultOk) {
        text::print("create_device failed\n");
        context.shutdown();
        return 1;
    }
    context.mark_ready();
    text::print("device: %s\n", context.adapter().description.data());
    if (!state.context.ray_tracing_enabled()) {
        text::print("this device has no ray tracing (the extensions and features asked for): nothing to check\n");
        context.shutdown();
        return 2;
    }
    text::print("formats: R5G6B5 %s, A1R5G5B5 %s\n", state.formats.r5g6b5 ? "native" : "widened",
                state.formats.a1r5g5b5 ? "native" : "widened");
    Tally tally;
    MaterialTable& table = state.materials;
    tally.check(table.ready() && table.texture_slots() > 1,
                text::format("the backend's table made: %u texture slots", table.texture_slots()));
    if (table.ready()) {
        Probe probe;
        if (!probe.init(state.context.device(), table.layout())) {
            tally.check(false, "the probe's compute pipeline made");
        } else {
            check_formats(gpu, probe, table, &tally);
            check_sampling(gpu, probe, table, &tally);
            check_release(gpu, probe, table, &tally);
            check_many(gpu, probe, table, &tally);
            probe.shutdown();
        }
        check_limit(gpu, &tally);
    }
    tally.check(state.textures.size() == 0, "every texture released");
    context.shutdown();
    tally.check(!context.device_made() && !table.ready(), "the device shut down, the table with it");
    tally.check(!options.validation || !state.context.validation_on() || validation_messages() == 0,
                text::format("validation messages: %d", validation_messages()));
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_rt_materials_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    if (!render::vulkan::open_loader()) {
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window = SDL_CreateWindow("thug2 Vulkan ray-tracing materials check", kWindowWidth, kWindowHeight,
                                          SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        text::print("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    SDL_SetWindowPosition(window, position, position);
    SDL_ShowWindow(window);
    int result = 1;
    try {
        result = run(options, window);
    } catch (const std::exception& error) {
        text::print("the check stopped: %s\n", error.what());
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}

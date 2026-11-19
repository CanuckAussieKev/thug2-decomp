// NOT ORIGINAL - (round OM) opacity micromaps for the ray-traced scene (vulkan_rt_micromaps.h).
#include "gpu_vulkan/vulkan_rt_micromaps.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_rt_shadows.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

namespace rt = render::rt;

constexpr uint32_t kLeastLevel = 2;        // 16 micro-triangles: a triangle's states fill a word
constexpr uint32_t kMostLevel = 6;         // 4096 micro-triangles: a kilobyte a triangle
constexpr VkDeviceSize kMostMeshBytes = 8ULL * 1024 * 1024;    // a mesh's states at most (its level lowered to fit)
constexpr uint64_t kMostBytes = 512ULL * 1024 * 1024;          // every micromap's states and storage at most
constexpr VkDeviceSize kInputAlignment = 256;                  // the build's data and triangle array addresses
constexpr uint32_t kGroup = 64;                                // the bake's local size
constexpr uint32_t kMostGroups = 65535;                        // a dispatch's groups at most (the least maximum)
constexpr uint32_t kSampledTriangles = 4096;                   // the level's median from at most this many
constexpr uint32_t kTriangleCorners = 3;
constexpr uint32_t kFloatBytes = 4;
constexpr uint32_t kAlphaShift = 24;
constexpr uint64_t kHashBasis = 14695981039346656037ULL; // (FNV-1a)
constexpr uint64_t kHashPrime = 1099511628211ULL;
constexpr double kMegabyte = 1024.0 * 1024.0;
constexpr std::size_t kTallyWords = kMicromapTally;
// (each bake's decided / unknown micro-triangles, read back: a micromap that decides less than this share of its
// micro-triangles costs the traversal more than it saves - soft alpha everywhere - and is dropped)
constexpr uint32_t kVerdictSlots = 8192;
constexpr std::size_t kTallyBytes = (kTallyWords + (2 * std::size_t{kVerdictSlots})) * sizeof(uint32_t);
constexpr double kLeastDecided = 0.25;

// The bake: one invocation a micro-triangle. Its corners' barycentrics from its place in the level's grid, its index
// from the specification's curve at its centre; its state from the drawn alpha's range over it (vulkan_rt_micromaps.h).
constexpr const char* kBakeShader = R"(
layout(local_size_x = 64) in;
layout(buffer_reference, buffer_reference_align = 4) buffer MicroStates { uint words[]; };
layout(push_constant) uniform Bake {
    uvec2 states;      // the data's address (a triangle's states from triangle x 4^level x 2 bits)
    uint job;          // the mesh's row of the geometry records
    uint material;
    uint level;
    uint mode;
    uint first;        // the dispatch's first triangle
    uint count;        // its triangles
    uvec2 tally;       // the states counted: 4 words a mode (MicroTally), then 2 a bake (decided, unknown)
    uint verdict;      // the bake's pair (its word: 16 + 2 x this)
} bake;
layout(buffer_reference, buffer_reference_align = 4) buffer MicroTally { uint counts[]; };
const uint MODE_CUTOUT = 1u;
const uint MODE_SEEN_SOLID = 2u;
const uint MODE_SEEN_BLENDED = 3u;
const uint STATE_TRANSPARENT = 0u;
const uint STATE_OPAQUE = 1u;
const uint STATE_UNKNOWN_TRANSPARENT = 2u;
const uint STATE_UNKNOWN_OPAQUE = 3u;
const float BAKE_ALPHA_MARGIN = 1.0 / 1024.0;
const int BAKE_MOST_TEXELS = 1024;
const uint BAKE_TEXTURE_ALPHA = 128u; // (rt::kTextureAlpha)
const uint BAKE_GLASS = 2u;           // (rt::kReflectGlass)

// VK_EXT_opacity_micromap's reference BarycentricsToSpaceFillingCurveIndex.
uint micro_index(float u, float v, uint level)
{
    u = clamp(u, 0.0, 1.0);
    v = clamp(v, 0.0, 1.0);
    uint side = 1u << level;
    float fu = u * float(side);
    float fv = v * float(side);
    uint iu = uint(fu);
    uint iv = uint(fv);
    float uf = fu - float(iu);
    float vf = fv - float(iv);
    if (iu >= side) iu = side - 1u;
    if (iv >= side) iv = side - 1u;
    uint iuv = iu + iv;
    if (iuv >= side) iu -= iuv - side + 1u;
    uint iw = ~(iu + iv);
    if (uf + vf >= 1.0 && iuv < side - 1u) --iw;
    uint b0 = ~(iu ^ iw);
    b0 &= side - 1u;
    uint t = (iu ^ iv) & b0;
    uint f = t;
    f ^= f >> 1u;
    f ^= f >> 2u;
    f ^= f >> 4u;
    f ^= f >> 8u;
    uint b1 = ((f ^ iu) & ~b0) | t;
    b0 = (b0 | (b0 << 8u)) & 0x00ff00ffu;
    b0 = (b0 | (b0 << 4u)) & 0x0f0f0f0fu;
    b0 = (b0 | (b0 << 2u)) & 0x33333333u;
    b0 = (b0 | (b0 << 1u)) & 0x55555555u;
    b1 = (b1 | (b1 << 8u)) & 0x00ff00ffu;
    b1 = (b1 | (b1 << 4u)) & 0x0f0f0f0fu;
    b1 = (b1 | (b1 << 2u)) & 0x33333333u;
    b1 = (b1 | (b1 << 1u)) & 0x55555555u;
    return b0 | (b1 << 1u);
}

// The alpha test as the rays take it (vulkan_rt_scene.cpp record_alpha_passes).
bool bake_passes(Material m, float alpha)
{
    if ((m.flags & MATERIAL_ALPHA_TESTED) == 0u) {
        return true;
    }
    return uint(round(clamp(alpha, 0.0, 1.0) * 255.0)) >= uint(round(clamp(m.alpha_ref, 0.0, 1.0) * 255.0));
}

// A texel coordinate through an address mode (the material table's order: wrap, mirror, clamp, border); -1: the border.
int bake_address(int x, int size, uint mode)
{
    if (mode == 0u) {
        return ((x % size) + size) % size;
    }
    if (mode == 1u) {
        int period = 2 * size;
        int m = ((x % period) + period) % period;
        return m < size ? m : period - 1 - m;
    }
    if (mode == 2u) {
        return clamp(x, 0, size - 1);
    }
    return x < 0 || x >= size ? -1 : x;
}

void main()
{
    uint per = 1u << (2u * bake.level);
    uint invocation = gl_GlobalInvocationID.x;
    if (invocation >= bake.count * per) {
        return;
    }
    uint triangle = bake.first + invocation / per;
    uint cell = invocation % per;
    uint side = 1u << bake.level;
    uint a = cell / side;
    uint b = cell % side;
    float step = 1.0 / float(side);
    vec2 c[3];
    if (a + b <= side - 1u) { // (upright)
        c[0] = vec2(a, b) * step;
        c[1] = vec2(a + 1u, b) * step;
        c[2] = vec2(a, b + 1u) * step;
    } else {                  // (turned round)
        uint i = side - 1u - a;
        uint j = side - 1u - b;
        c[0] = vec2(i + 1u, j) * step;
        c[1] = vec2(i, j + 1u) * step;
        c[2] = vec2(i + 1u, j + 1u) * step;
    }
    vec2 centre = (c[0] + c[1] + c[2]) * (1.0 / 3.0);
    uint index = micro_index(centre.x, centre.y, bake.level);

    uint state = bake.mode == MODE_CUTOUT ? STATE_UNKNOWN_OPAQUE : STATE_UNKNOWN_TRANSPARENT;
    GeometryRecord g = geometry_record(bake.job);
    uvec3 corners;
    Material m = material(bake.material);
    bool usable = geometry_triangle(g, triangle, corners);
    uint blend = m.blend & 0xFFFFFFu;
    if (usable && bake.mode == MODE_SEEN_BLENDED) {
        // (hit_blend: a coverage surface - not glass, not a filter or an added one - read through set 0)
        bool coverage = blend == 5u || blend == 6u || blend == 0u || blend == 11u || blend == 12u || blend == 13u;
        uint sets = (g.flags >> 8) & 15u;
        usable = coverage && m.reflection_class != BAKE_GLASS && (m.uv_set == 0u || m.uv_set >= sets);
    }
    if (usable && (m.flags & MATERIAL_ALPHA_TESTED) == 0u && bake.mode != MODE_SEEN_BLENDED) {
        state = bake.mode == MODE_CUTOUT ? STATE_OPAQUE : state; // (no test: every point kept)
        usable = false;
    }
    if (usable) {
        // the corners' UVs (set 0) and vertex alphas, then the micro-triangle's
        vec2 uv[3];
        float alpha[3];
        for (uint k = 0u; k < 3u; ++k) {
            uv[k] = vec2(0.0);
            if ((g.flags & GEOMETRY_UV) != 0u) {
                uv[k] = vec2(geometry_float(g, corners[k], g.uv_offset), geometry_float(g, corners[k], g.uv_offset + 4u));
            }
            alpha[k] = 1.0;
            if ((g.flags & GEOMETRY_COLOUR) != 0u) {
                alpha[k] = float(geometry_word(g.vertex_address, g.vertex_offset + corners[k] * g.stride + g.colour_offset) >> 24) / 255.0;
            }
        }
        uint slot = m.texture_slot;
        uint chosen = min(m.sampler_index, 31u);
        ivec2 size = max(textureSize(sampler2D(material_textures[nonuniformEXT(slot)], material_samplers[nonuniformEXT(chosen)]), 0), ivec2(1));
        vec2 low = vec2(3.0e38);
        vec2 high = vec2(-3.0e38);
        float least_vertex = 3.0e38; // (the vertex alpha's range over the micro-triangle)
        float most_vertex = -3.0e38;
        for (uint k = 0u; k < 3u; ++k) {
            float w0 = 1.0 - c[k].x - c[k].y;
            vec2 at = uv[0] * w0 + uv[1] * c[k].x + uv[2] * c[k].y;
            vec2 texel = at * vec2(size) - 0.5;
            low = min(low, texel);
            high = max(high, texel);
            float va = alpha[0] * w0 + alpha[1] * c[k].x + alpha[2] * c[k].y;
            least_vertex = min(least_vertex, va);
            most_vertex = max(most_vertex, va);
        }
        bool finite = all(lessThan(abs(low), vec2(1.0e7))) && all(lessThan(abs(high), vec2(1.0e7)));
        ivec2 first = ivec2(floor(low)) - 1;
        ivec2 last = ivec2(floor(high)) + 2;
        ivec2 span = last - first + 1;
        if (finite && span.x * span.y <= BAKE_MOST_TEXELS) {
            uint u_mode = (chosen >> 2) & 3u;
            uint v_mode = chosen & 3u;
            float least = 1.0;
            float most = 0.0;
            for (int y = first.y; y <= last.y; ++y) {
                int ty = bake_address(y, size.y, v_mode);
                for (int x = first.x; x <= last.x; ++x) {
                    int tx = bake_address(x, size.x, u_mode);
                    float t = 0.0; // (the border: transparent black)
                    if (tx >= 0 && ty >= 0) {
                        t = texelFetch(sampler2D(material_textures[nonuniformEXT(slot)], material_samplers[nonuniformEXT(chosen)]), ivec2(tx, ty), 0).a;
                    }
                    least = min(least, t);
                    most = max(most, t);
                }
            }
            // the alpha the test compares: with 128's vertex alpha too (the bounce and reflection rays' test takes it -
            // material_alpha_passes_at)
            float drawn_least = least;
            float drawn_most = most;
            bool covered = least >= 1.0; // (a blend 5 surface drawn wholly opaque: its coverage 1 everywhere)
            if ((m.flags & BAKE_TEXTURE_ALPHA) == 0u) {
                drawn_least = clamp(2.0 * min(least_vertex, 0.5) * least, 0.0, 1.0);
                drawn_most = clamp(2.0 * max(most_vertex, 0.5) * most, 0.0, 1.0);
                covered = 2.0 * least_vertex * least >= 1.0 + BAKE_ALPHA_MARGIN;
            }
            bool kept = bake_passes(m, drawn_least - BAKE_ALPHA_MARGIN);
            if (!bake_passes(m, drawn_most + BAKE_ALPHA_MARGIN)) {
                state = STATE_TRANSPARENT;
            } else if (bake.mode == MODE_CUTOUT && kept) {
                state = STATE_OPAQUE;
            } else if (bake.mode == MODE_SEEN_BLENDED && kept &&
                       ((blend == 5u && covered) || blend == 0u || blend == 11u || blend == 12u || blend == 13u ||
                        (blend == 6u && m.colour.a >= 1.0))) {
                // (hit_blend's coverage 1 everywhere: no light through it - a commit; vulkan_rt_scene.h seen_through
                // takes a committed blended hit as 0)
                state = STATE_OPAQUE;
            }
        }
    }
    atomicAdd(MicroTally(bake.tally).counts[bake.mode * 4u + state], 1u);
    atomicAdd(MicroTally(bake.tally).counts[16u + 2u * bake.verdict + (state >= STATE_UNKNOWN_TRANSPARENT ? 1u : 0u)], 1u);
    if (state != STATE_TRANSPARENT) {
        uint bit = (triangle * per + index) * 2u;
        atomicOr(MicroStates(bake.states).words[bit >> 5], state << (bit & 31u));
    }
}
)";

struct BakePass {
    bool tried = false;
    VkDescriptorSetLayout rows_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct BakeConstants {
    std::array<uint32_t, 2> states{};
    uint32_t job = 0;
    uint32_t material = 0;
    uint32_t level = 0;
    uint32_t mode = 0;
    uint32_t first = 0;
    uint32_t count = 0;
    std::array<uint32_t, 2> tally{};
    uint32_t verdict = 0;
};

struct Micromaps {
    bool on = false;
    uint32_t most_level = 0;
    VkDevice device = nullptr;
    BakePass pass;
    MicromapStats stats;
    bool budget_logged = false;
    DeviceBuffer tally; // (the bakes' states counted - read back, MicromapStats::states - and each bake's verdict)
    VkDeviceAddress tally_address = 0;
    uint32_t next_verdict = 0;
};

Micromaps& micromaps()
{
    static core::NoDestroy<Micromaps> made;
    return made.get();
}

VkDeviceAddress buffer_address(VkDevice device, VkBuffer buffer)
{
    const VkBufferDeviceAddressInfo info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = buffer};
    return vkGetBufferDeviceAddress(device, &info);
}

VkDeviceAddress aligned(VkDeviceAddress value, VkDeviceSize alignment)
{
    return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

uint32_t mode_number(MicromapMode mode) { return static_cast<uint32_t>(mode); }

// A triangle's states in bytes at `level` (2 bits a micro-triangle).
VkDeviceSize triangle_bytes(uint32_t level) { return (VkDeviceSize{1} << (2U * level)) / 4; }

void destroy_pass(Micromaps& made)
{
    BakePass& pass = made.pass;
    if (pass.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(made.device, pass.pipeline, nullptr);
    }
    if (pass.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(made.device, pass.layout, nullptr);
    }
    if (pass.rows_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(made.device, pass.rows_layout, nullptr);
    }
    pass = BakePass{};
}

bool make_pass(Backend& state, Micromaps& made)
{
    BakePass& pass = made.pass;
    if (pass.tried) {
        return pass.pipeline != VK_NULL_HANDLE;
    }
    pass.tried = true;
    if (!state.materials.ready()) {
        return false;
    }
    std::string source = "#version 460\n#define THUG2_MATERIAL_SET 0\n";
    source += material_glsl();
    source += "#define THUG2_GEOMETRY_SET 1\n#define THUG2_GEOMETRY_BINDING 0\n";
    source += scene_geometry_glsl();
    source += kBakeShader;
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, source);
    if (!compiled.ok) {
        get_log("vulkan: ray tracing: the opacity micromaps' bake doesn't compile: %s", compiled.log.c_str());
        return false;
    }
    const VkDescriptorSetLayoutBinding rows{.binding = 0,
                                            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                            .descriptorCount = 1,
                                            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                            .pImmutableSamplers = nullptr};
    const VkDescriptorSetLayoutCreateInfo rows_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                    .pNext = nullptr,
                                                    .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                    .bindingCount = 1,
                                                    .pBindings = &rows};
    if (vkCreateDescriptorSetLayout(made.device, &rows_info, nullptr, &pass.rows_layout) != VK_SUCCESS) {
        return false;
    }
    const std::array<VkDescriptorSetLayout, 2> sets{state.materials.layout(), pass.rows_layout};
    const VkPushConstantRange push{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(BakeConstants)};
    const VkPipelineLayoutCreateInfo layout{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = 0,
                                           .setLayoutCount = static_cast<uint32_t>(sets.size()),
                                           .pSetLayouts = sets.data(),
                                           .pushConstantRangeCount = 1,
                                           .pPushConstantRanges = &push};
    if (vkCreatePipelineLayout(made.device, &layout, nullptr, &pass.layout) != VK_SUCCESS) {
        return false;
    }
    const VkShaderModuleCreateInfo module_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                               .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(made.device, &module_info, nullptr, &module) != VK_SUCCESS) {
        return false;
    }
    const VkComputePipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = 0,
                                           .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                     .module = module,
                                                     .pName = "main",
                                                     .pSpecializationInfo = nullptr},
                                           .layout = pass.layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    const VkResult result = vkCreateComputePipelines(made.device, VK_NULL_HANDLE, 1, &info, nullptr, &pass.pipeline);
    vkDestroyShaderModule(made.device, module, nullptr);
    if (result != VK_SUCCESS) {
        pass.pipeline = VK_NULL_HANDLE;
        return false;
    }
    if (made.tally.buffer == VK_NULL_HANDLE) {
        if (!state.memory.create_buffer(kTallyBytes,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                        BufferPlace::kReadBack, &made.tally) ||
            made.tally.mapped == nullptr) {
            state.memory.destroy_buffer(&made.tally);
            vkDestroyPipeline(made.device, pass.pipeline, nullptr);
            pass.pipeline = VK_NULL_HANDLE;
            return false;
        }
        state.memory.name_buffer(made.tally, "the opacity micromaps' state counts");
        std::memset(made.tally.mapped, 0, kTallyBytes);
        state.memory.flush(made.tally, 0, kTallyBytes);
        made.tally_address = buffer_address(made.device, made.tally.buffer);
    }
    return true;
}

// Every command before it - writes of any kind - seen by every command after it (the micromap stages have no
// synchronization 1 flags of their own: VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT is in the all-commands stage).
void full_barrier(VkCommandBuffer commands)
{
    const VkMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier,
                         0, nullptr, 0, nullptr);
}

VkMicromapBuildInfoEXT build_info(const MeshMicromap& micromap)
{
    VkMicromapBuildInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT;
    info.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    info.flags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    info.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    info.dstMicromap = micromap.handle;
    info.usageCountsCount = 1;
    info.pUsageCounts = &micromap.usage;
    info.data.deviceAddress = micromap.data_address;
    info.triangleArray.deviceAddress = micromap.triangles_address;
    info.triangleArrayStride = sizeof(VkMicromapTriangleEXT);
    return info;
}

void count_live(Micromaps& made, MeshMicromap& micromap, bool adding)
{
    if (micromap.counted == adding) {
        return;
    }
    micromap.counted = adding;
    const uint64_t bytes = micromap.data.bytes + micromap.storage.bytes + micromap.triangles.bytes + micromap.indices.bytes;
    const uint64_t micro = uint64_t{micromap.count} << (2U * micromap.level);
    if (adding) {
        ++made.stats.meshes;
        made.stats.bytes += bytes;
        made.stats.micro_triangles += micro;
    } else {
        made.stats.meshes -= std::min<uint32_t>(made.stats.meshes, 1);
        made.stats.bytes -= std::min(made.stats.bytes, bytes);
        made.stats.micro_triangles -= std::min(made.stats.micro_triangles, micro);
    }
}

} // namespace

void micromaps_create(Backend& state)
{
    Micromaps& made = micromaps();
    destroy_pass(made);
    made.device = state.context.device();
    made.on = state.context.opacity_micromaps_enabled();
    made.most_level = std::min(state.context.opacity_micromap_levels(), kMostLevel);
    made.stats = MicromapStats{};
    made.budget_logged = false;
    set_micromaps_declared(made.on);
    if (made.on) {
        get_log("vulkan: ray tracing: opacity micromaps on (VK_EXT_opacity_micromap, 4-state, levels %u - %u)",
                kLeastLevel, made.most_level);
    } else {
        get_log("vulkan: ray tracing: no opacity micromaps (%s)",
                state.options.opacity_micromaps ? "the device doesn't offer VK_EXT_opacity_micromap"
                                                 : "[debug] rt_opacity_micromaps=0");
    }
}

void micromaps_release(Backend& state)
{
    Micromaps& made = micromaps();
    state.memory.destroy_buffer(&made.tally); // (the device idle: release_device_objects waited for it)
    made.tally_address = 0;
    if (made.device != nullptr) {
        destroy_pass(made);
    }
    made.on = false;
    made.device = nullptr;
    set_micromaps_declared(false);
}

bool micromaps_on() { return micromaps().on; }

MicromapMode micromap_mode(const rt::Instance& instance, uint32_t mask, bool forced_opaque)
{
    if (!micromaps().on || forced_opaque || (instance.flags & (rt::kObject | rt::kAddsLight)) != 0 ||
        instance.material == 0) {
        return MicromapMode::kNone;
    }
    const bool blended = (instance.flags & rt::kTransparent) != 0;
    if (mask == kMaskNoShadow) {
        return blended ? MicromapMode::kSeenBlended : MicromapMode::kSeenSolid;
    }
    if (mask == kMaskAlphaTested || mask == kMaskWorld) {
        return blended ? MicromapMode::kSeenBlended : MicromapMode::kCutout;
    }
    return MicromapMode::kNone;
}

MicromapKey micromap_key(Backend& state, rt::MaterialId material, MicromapMode mode)
{
    const MaterialRecord record = state.materials.record(material);
    const MaterialTable::TextureInfo texture = state.materials.texture_info(material);
    return {.material = material,
            .mode = mode,
            .texture_slot = record.texture,
            .sampler = record.sampler,
            .flags = record.flags,
            .blend = record.blend,
            .uv_set = record.uv_set,
            .reflection_class = record.reflection_class,
            .alpha_ref = record.alpha_ref,
            .texture_uploads = texture.uploads,
            .alpha_hash = 0};
}

uint64_t micromap_alpha_hash(std::span<const uint8_t> vertices, const rt::Geometry& geometry, uint32_t vertex_count)
{
    if (geometry.colour_format != rt::kColourA8R8G8B8) {
        return 0;
    }
    uint64_t hash = kHashBasis;
    constexpr uint32_t kAlphaByte = kAlphaShift / 8;
    for (uint32_t vertex = 0; vertex < vertex_count; ++vertex) {
        const std::size_t at = std::size_t{geometry.vertex_offset} + (std::size_t{vertex} * geometry.vertex_stride) +
                               geometry.colour_offset + kAlphaByte;
        if (at >= vertices.size()) {
            break;
        }
        hash = (hash ^ core::at(vertices, at)) * kHashPrime;
    }
    return hash;
}

uint32_t micromap_level(std::span<const uint8_t> vertices, const rt::Geometry& geometry, std::span<const uint32_t> corners,
                        uint32_t width, uint32_t height)
{
    const Micromaps& made = micromaps();
    const auto triangles = static_cast<uint32_t>(corners.size() / kTriangleCorners);
    if (triangles == 0) {
        return kLeastLevel;
    }
    const auto uv_at = [&](uint32_t vertex, std::array<float, 2>* uv) {
        const std::size_t at =
            std::size_t{geometry.vertex_offset} + (std::size_t{vertex} * geometry.vertex_stride) + geometry.uv_offset;
        if (geometry.uv_format != rt::kUvFloat2 || at + (2 * kFloatBytes) > vertices.size()) {
            return false;
        }
        std::memcpy(uv->data(), &core::at(vertices, at), 2 * kFloatBytes);
        return true;
    };
    std::vector<float> areas;
    const uint32_t stride = std::max<uint32_t>(1, triangles / kSampledTriangles);
    for (uint32_t triangle = 0; triangle < triangles; triangle += stride) {
        std::array<std::array<float, 2>, kTriangleCorners> uv{};
        bool read = true;
        for (uint32_t k = 0; k < kTriangleCorners; ++k) {
            read = read && uv_at(core::at(corners, (std::size_t{triangle} * kTriangleCorners) + k), &core::at(uv, k));
        }
        if (!read) {
            continue;
        }
        const auto [a, b, c] = uv;
        const float du1 = (std::get<0>(b) - std::get<0>(a)) * static_cast<float>(width);
        const float dv1 = (std::get<1>(b) - std::get<1>(a)) * static_cast<float>(height);
        const float du2 = (std::get<0>(c) - std::get<0>(a)) * static_cast<float>(width);
        const float dv2 = (std::get<1>(c) - std::get<1>(a)) * static_cast<float>(height);
        const float area = 0.5F * std::abs((du1 * dv2) - (du2 * dv1));
        if (std::isfinite(area)) {
            areas.push_back(area);
        }
    }
    uint32_t level = kLeastLevel;
    if (!areas.empty()) {
        const auto middle = areas.begin() + static_cast<std::ptrdiff_t>(areas.size() / 2);
        std::ranges::nth_element(areas, middle);
        // (4^level micro-triangles at least the texels the triangle covers: one a texel or smaller)
        const double texels = std::max(1.0, static_cast<double>(*middle));
        level = static_cast<uint32_t>(std::ceil(std::log2(texels) / 2.0));
    }
    level = std::clamp(level, kLeastLevel, std::max(kLeastLevel, made.most_level));
    while (level > kLeastLevel && triangle_bytes(level) * triangles > kMostMeshBytes) {
        --level;
    }
    return level;
}

void micromap_retire(Backend& state, MeshMicromap* micromap)
{
    Micromaps& made = micromaps();
    count_live(made, *micromap, false);
    if (micromap->handle != VK_NULL_HANDLE || micromap->storage.buffer != VK_NULL_HANDLE) {
        if (state.draws_made) {
            state.frames.retire(micromap->handle, micromap->storage);
        } else {
            if (micromap->handle != VK_NULL_HANDLE) {
                vkDestroyMicromapEXT(state.context.device(), micromap->handle, nullptr);
            }
            state.memory.destroy_buffer(&micromap->storage);
        }
    }
    retire_buffer(state, &micromap->data);
    retire_buffer(state, &micromap->triangles);
    retire_buffer(state, &micromap->indices);
    *micromap = MeshMicromap{};
}

bool micromap_prepare(Backend& state, MeshMicromap* micromap, const MicromapKey& key, uint32_t triangles,
                      uint32_t level)
{
    Micromaps& made = micromaps();
    micromap_retire(state, micromap);
    if (!made.on || triangles == 0) {
        return false;
    }
    const VkDeviceSize per = triangle_bytes(level);
    const VkDeviceSize data_bytes = per * triangles;
    if (made.stats.bytes + (data_bytes * 2) > kMostBytes) {
        if (!made.budget_logged) {
            made.budget_logged = true;
            get_log("vulkan: ray tracing: the opacity micromaps reached %.0f MB - no more made",
                    static_cast<double>(kMostBytes) / kMegabyte);
        }
        return false;
    }
    const VkDevice device = state.context.device();
    MeshMicromap made_now;
    made_now.key = key;
    made_now.level = level;
    made_now.count = triangles;
    made_now.usage = {.count = triangles,
                      .subdivisionLevel = level,
                      .format = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT};
    const auto fail = [&](const char* what) {
        get_log("vulkan: ray tracing: no memory for an opacity micromap's %s (%u triangles, level %u)", what, triangles,
                level);
        micromap_retire(state, &made_now);
        return false;
    };
    // the states (written by the bake), the triangles' records and the BLAS's indices
    if (!state.memory.create_buffer(data_bytes + kInputAlignment,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                        VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    BufferPlace::kDevice, &made_now.data)) {
        return fail("states");
    }
    state.memory.name_buffer(made_now.data, "an opacity micromap's states");
    made_now.data_address = aligned(buffer_address(device, made_now.data.buffer), kInputAlignment);
    const VkDeviceSize triangle_records = VkDeviceSize{triangles} * sizeof(VkMicromapTriangleEXT);
    if (!state.memory.create_buffer(triangle_records + kInputAlignment,
                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                        VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT,
                                    BufferPlace::kUpload, &made_now.triangles) ||
        made_now.triangles.mapped == nullptr) {
        return fail("triangles");
    }
    state.memory.name_buffer(made_now.triangles, "an opacity micromap's triangles");
    const VkDeviceAddress triangles_start = buffer_address(device, made_now.triangles.buffer);
    made_now.triangles_address = aligned(triangles_start, kInputAlignment);
    {
        std::vector<VkMicromapTriangleEXT> records(triangles);
        for (uint32_t i = 0; i < triangles; ++i) {
            core::at(records, i) = {.dataOffset = static_cast<uint32_t>(per * i),
                                    .subdivisionLevel = static_cast<uint16_t>(level),
                                    .format = static_cast<uint16_t>(VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT)};
        }
        const auto skip = static_cast<std::size_t>(made_now.triangles_address - triangles_start);
        const std::span<uint8_t> into(static_cast<uint8_t*>(made_now.triangles.mapped),
                                      static_cast<std::size_t>(made_now.triangles.bytes));
        std::memcpy(into.subspan(skip, static_cast<std::size_t>(triangle_records)).data(), records.data(),
                    static_cast<std::size_t>(triangle_records));
        state.memory.flush(made_now.triangles, skip, triangle_records);
    }
    const VkDeviceSize index_bytes = VkDeviceSize{triangles} * sizeof(uint32_t);
    if (!state.memory.create_buffer(index_bytes,
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    BufferPlace::kUpload, &made_now.indices) ||
        made_now.indices.mapped == nullptr) {
        return fail("indices");
    }
    state.memory.name_buffer(made_now.indices, "an opacity micromap's indices");
    {
        std::vector<uint32_t> indices(triangles);
        for (uint32_t i = 0; i < triangles; ++i) {
            core::at(indices, i) = i;
        }
        std::memcpy(made_now.indices.mapped, indices.data(), static_cast<std::size_t>(index_bytes));
        state.memory.flush(made_now.indices, 0, index_bytes);
    }
    made_now.indices_address = buffer_address(device, made_now.indices.buffer);
    // the micromap's own storage
    VkMicromapBuildInfoEXT info = build_info(made_now);
    VkMicromapBuildSizesInfoEXT sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT;
    vkGetMicromapBuildSizesEXT(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &sizes);
    if (!state.memory.create_buffer(sizes.micromapSize,
                                    VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    BufferPlace::kDevice, &made_now.storage)) {
        return fail("storage");
    }
    state.memory.name_buffer(made_now.storage, "an opacity micromap");
    const VkMicromapCreateInfoEXT create{.sType = VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT,
                                         .pNext = nullptr,
                                         .createFlags = 0,
                                         .buffer = made_now.storage.buffer,
                                         .offset = 0,
                                         .size = sizes.micromapSize,
                                         .type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT,
                                         .deviceAddress = 0};
    if (vkCreateMicromapEXT(device, &create, nullptr, &made_now.handle) != VK_SUCCESS) {
        made_now.handle = VK_NULL_HANDLE;
        return fail("object");
    }
    made_now.build_scratch = sizes.buildScratchSize;
    *micromap = made_now;
    count_live(made, *micromap, true);
    return true;
}

MicromapAttach micromap_attach(const MeshMicromap& micromap)
{
    MicromapAttach attach;
    attach.usage = micromap.usage;
    attach.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT;
    attach.triangles.indexType = VK_INDEX_TYPE_UINT32;
    attach.triangles.indexBuffer.deviceAddress = micromap.indices_address;
    attach.triangles.indexStride = sizeof(uint32_t);
    attach.triangles.baseTriangle = 0;
    attach.triangles.usageCountsCount = 1;
    attach.triangles.pUsageCounts = nullptr; // (pointed at `usage` where it is recorded: the attach is copied)
    attach.triangles.micromap = micromap.handle;
    return attach;
}

VkDeviceSize micromap_scratch(std::span<const MicromapBake> bakes, VkDeviceSize scratch_alignment)
{
    VkDeviceSize total = 0;
    for (const MicromapBake& bake : bakes) {
        total += aligned(bake.micromap->build_scratch, scratch_alignment);
    }
    return total;
}

bool record_micromap_bakes(Backend& state, VkCommandBuffer commands, std::span<MicromapBake> bakes,
                           VkDeviceAddress scratch, VkDeviceSize scratch_bytes, VkDeviceSize scratch_alignment)
{
    Micromaps& made = micromaps();
    if (bakes.empty()) {
        return true;
    }
    // the meshes' rows, read through the geometry records' GLSL
    DeviceBuffer rows;
    const VkDeviceSize row_bytes = bakes.size() * sizeof(GeometryRecord);
    bool baked = make_pass(state, made);
    if (baked && (!state.memory.create_buffer(row_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferPlace::kUpload, &rows) ||
                  rows.mapped == nullptr)) {
        state.memory.destroy_buffer(&rows);
        baked = false;
    }
    if (baked) {
        state.memory.name_buffer(rows, "the opacity micromaps' bake rows");
        const std::span<GeometryRecord> into(static_cast<GeometryRecord*>(rows.mapped), bakes.size());
        for (std::size_t i = 0; i < bakes.size(); ++i) {
            core::at(into, i) = core::at(bakes, i).geometry;
        }
        state.memory.flush(rows, 0, row_bytes);
    } else {
        get_log("vulkan: ray tracing: %zu opacity micromaps left unknown everywhere (no bake pass) - their rays test "
                "every hit as before",
                bakes.size());
    }
    // (the scratch's last users - an earlier submission's builds - done first)
    full_barrier(commands);
    // the states start transparent (0) - the bake ORs the others in; without the bake every state unknown (3: the
    // shader's test everywhere, as without a micromap)
    constexpr uint32_t kAllUnknown = 0xFFFFFFFFU;
    for (const MicromapBake& bake : bakes) {
        vkCmdFillBuffer(commands, bake.micromap->data.buffer, 0, VK_WHOLE_SIZE, baked ? 0U : kAllUnknown);
        if (baked) { // (its verdict's pair zeroed, the submission it is read after)
            bake.micromap->verdict_slot = made.next_verdict;
            bake.micromap->verdict_serial = state.frames.serial();
            made.next_verdict = (made.next_verdict + 1) % kVerdictSlots;
            const VkDeviceSize at = (kTallyWords + (2 * VkDeviceSize{bake.micromap->verdict_slot})) * sizeof(uint32_t);
            vkCmdFillBuffer(commands, made.tally.buffer, at, 2 * sizeof(uint32_t), 0);
        }
    }
    if (baked) {
        const VkMemoryBarrier filled{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                     .pNext = nullptr,
                                     .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                             &filled, 0, nullptr, 0, nullptr);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, made.pass.pipeline);
        const VkDescriptorSet materials = state.materials.descriptor_set();
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, made.pass.layout, 0, 1, &materials, 0,
                                nullptr);
        const VkDescriptorBufferInfo rows_info{.buffer = rows.buffer, .offset = 0, .range = row_bytes};
        const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                         .pNext = nullptr,
                                         .dstSet = VK_NULL_HANDLE,
                                         .dstBinding = 0,
                                         .dstArrayElement = 0,
                                         .descriptorCount = 1,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         .pImageInfo = nullptr,
                                         .pBufferInfo = &rows_info,
                                         .pTexelBufferView = nullptr};
        vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, made.pass.layout, 1, 1, &write);
        for (std::size_t i = 0; i < bakes.size(); ++i) {
            const MeshMicromap& micromap = *core::at(bakes, i).micromap;
            const uint32_t per = 1U << (2U * micromap.level);
            const uint32_t chunk = std::max<uint32_t>(1, (kMostGroups * kGroup) / per);
            for (uint32_t first = 0; first < micromap.count; first += chunk) {
                const uint32_t count = std::min(chunk, micromap.count - first);
                const BakeConstants constants{.states = {static_cast<uint32_t>(micromap.data_address),
                                                         static_cast<uint32_t>(micromap.data_address >> 32U)},
                                              .job = static_cast<uint32_t>(i),
                                              .material = micromap.key.material,
                                              .level = micromap.level,
                                              .mode = mode_number(micromap.key.mode),
                                              .first = first,
                                              .count = count,
                                              .tally = {static_cast<uint32_t>(made.tally_address),
                                                        static_cast<uint32_t>(made.tally_address >> 32U)},
                                              .verdict = micromap.verdict_slot};
                vkCmdPushConstants(commands, made.pass.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                                   &constants);
                const uint64_t invocations = uint64_t{count} * per;
                vkCmdDispatch(commands, static_cast<uint32_t>((invocations + kGroup - 1) / kGroup), 1, 1);
            }
            ++made.stats.bakes;
        }
    }
    full_barrier(commands);
    // the micromaps built, in batches whose scratch fits (a barrier between: the scratch is shared)
    std::size_t start = 0;
    while (start < bakes.size()) {
        std::vector<VkMicromapBuildInfoEXT> infos;
        VkDeviceSize offset = 0;
        std::size_t end = start;
        while (end < bakes.size()) {
            const MeshMicromap& micromap = *core::at(bakes, end).micromap;
            const VkDeviceSize needed = aligned(micromap.build_scratch, scratch_alignment);
            if (end > start && offset + needed > scratch_bytes) {
                break;
            }
            VkMicromapBuildInfoEXT info = build_info(micromap);
            info.scratchData.deviceAddress = scratch + offset;
            infos.push_back(info);
            offset += needed;
            ++end;
        }
        if (start > 0) {
            full_barrier(commands);
        }
        vkCmdBuildMicromapsEXT(commands, static_cast<uint32_t>(infos.size()), infos.data());
        start = end;
    }
    full_barrier(commands);
    retire_buffer(state, &rows);
    return baked;
}

MicromapVerdict micromap_verdict(Backend& state, MeshMicromap* micromap)
{
    Micromaps& made = micromaps();
    if (micromap->verdict_serial == 0 || made.tally.mapped == nullptr) {
        return MicromapVerdict::kKept;
    }
    if (state.frames.completed() < micromap->verdict_serial) {
        return MicromapVerdict::kWaiting;
    }
    micromap->verdict_serial = 0;
    state.memory.invalidate(made.tally);
    std::array<uint32_t, 2> pair{};
    const std::span<const uint8_t> words(static_cast<const uint8_t*>(made.tally.mapped), kTallyBytes);
    std::memcpy(pair.data(),
                words.subspan((kTallyWords + (2 * std::size_t{micromap->verdict_slot})) * sizeof(uint32_t), sizeof(pair))
                    .data(),
                sizeof(pair));
    const double total = static_cast<double>(pair[0]) + static_cast<double>(pair[1]);
    if (total > 0.0 && static_cast<double>(pair[0]) < kLeastDecided * total) {
        ++made.stats.dropped;
        return MicromapVerdict::kDropped;
    }
    return MicromapVerdict::kKept;
}

MicromapStats micromap_stats()
{
    Micromaps& made = micromaps();
    MicromapStats stats = made.stats;
    if (made.tally.mapped != nullptr) {
        // (the counts of the bakes the device has run: a read-back buffer, read as it is)
        std::array<uint32_t, kTallyWords> counts{};
        std::memcpy(counts.data(), made.tally.mapped, sizeof(counts));
        for (std::size_t i = 0; i < kTallyWords; ++i) {
            core::at(stats.states, i) = core::at(counts, i);
        }
    }
    return stats;
}

bool read_micromap_states(Backend& state, const MeshMicromap& micromap, std::vector<uint32_t>* words)
{
    words->clear();
    if (micromap.data.buffer == VK_NULL_HANDLE) {
        return false;
    }
    const VkDeviceSize skip = micromap.data_address - buffer_address(state.context.device(), micromap.data.buffer);
    const VkDeviceSize bytes = triangle_bytes(micromap.level) * micromap.count;
    std::vector<uint8_t> read;
    if (!state.memory.read_buffer(micromap.data, skip, bytes, &read)) {
        return false;
    }
    words->resize(read.size() / sizeof(uint32_t));
    std::memcpy(words->data(), read.data(), words->size() * sizeof(uint32_t));
    return true;
}

uint32_t micromap_index(float u, float v, uint32_t level)
{
    // (the bake shader's micro_index, the specification's reference)
    u = std::clamp(u, 0.0F, 1.0F);
    v = std::clamp(v, 0.0F, 1.0F);
    const uint32_t side = 1U << level;
    const float fu = u * static_cast<float>(side);
    const float fv = v * static_cast<float>(side);
    auto iu = static_cast<uint32_t>(fu);
    auto iv = static_cast<uint32_t>(fv);
    const float uf = fu - static_cast<float>(iu);
    const float vf = fv - static_cast<float>(iv);
    iu = std::min(iu, side - 1);
    iv = std::min(iv, side - 1);
    const uint32_t iuv = iu + iv;
    if (iuv >= side) {
        iu -= iuv - side + 1;
    }
    uint32_t iw = ~(iu + iv);
    if (uf + vf >= 1.0F && iuv < side - 1) {
        --iw;
    }
    uint32_t b0 = ~(iu ^ iw);
    b0 &= side - 1;
    const uint32_t t = (iu ^ iv) & b0;
    uint32_t f = t;
    f ^= f >> 1U;
    f ^= f >> 2U;
    f ^= f >> 4U;
    f ^= f >> 8U;
    uint32_t b1 = ((f ^ iu) & ~b0) | t;
    const auto spread = [](uint32_t x) {
        x = (x | (x << 8U)) & 0x00ff00ffU;
        x = (x | (x << 4U)) & 0x0f0f0f0fU;
        x = (x | (x << 2U)) & 0x33333333U;
        x = (x | (x << 1U)) & 0x55555555U;
        return x;
    };
    b0 = spread(b0);
    b1 = spread(b1);
    return b0 | (b1 << 1U);
}

} // namespace render::vulkan

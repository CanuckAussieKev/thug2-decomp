// NOT ORIGINAL - the Vulkan Gpu's pipelines and their caches (vulkan_pipelines.h).
#include "gpu_vulkan/vulkan_pipelines.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/layout_32bit.h"
#include "engine/file/disk.h"
#include "engine/gfx/rt_cel.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_shaders.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_draw_state.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The 64-bit build keeps its caches in files of its own: the pipeline cache is the driver's data, and a 32-bit and a
// 64-bit driver are different binaries whose caches may carry the same device UUID in their headers; the shader file
// goes with it, so that each build's pair stays together. The 32-bit build's names are unchanged.
constexpr const char* kShaderFile =
    core::kPointers32Bit ? "thug2-decomp-vulkan-shaders.bin" : "thug2-decomp-vulkan-shaders-x64.bin";
constexpr const char* kPipelineFile =
    core::kPointers32Bit ? "thug2-decomp-vulkan-pipelines.bin" : "thug2-decomp-vulkan-pipelines-x64.bin";
constexpr uint32_t kShaderFileMagic = 0x53324854U; // "TH2S"
constexpr uint32_t kShaderFileVersion = 2;
constexpr uint32_t kPipelineFileMagic = 0x50324854U; // "TH2P"
constexpr uint32_t kPipelineFileVersion = 1;
// The files' sizes are bounded (the freeze audit: they reached 90 + 44 MB): a save keeps the entries the newest runs
// used, up to these.
constexpr std::size_t kShaderCacheBytes = 24U << 20U;
constexpr std::size_t kPipelineCacheBytes = 40U << 20U;
constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001B3ULL;
constexpr double kSlowPipelineSeconds = 0.008; // a pipeline (its shaders compiled and made) slower than this: logged
constexpr std::size_t kMostSpirvWords = 1U << 20U; // (a sanity limit for the file's entries)

uint64_t fnv1a(std::span<const uint8_t> bytes)
{
    uint64_t hash = kFnvOffset;
    for (const uint8_t byte : bytes) {
        hash = (hash ^ byte) * kFnvPrime;
    }
    return hash;
}

uint64_t text_hash(const std::string& text)
{
    const std::span<const char> chars(text);
    uint64_t hash = kFnvOffset;
    for (const char ch : chars) {
        hash = (hash ^ static_cast<uint8_t>(ch)) * kFnvPrime;
    }
    return hash;
}

double seconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// A whole file's bytes (empty when it isn't there).
std::vector<uint8_t> read_file(const std::string& path)
{
    std::vector<uint8_t> bytes;
    fs::DiskStream* const in = fs::disk().open_stream(path.c_str(), "rb", fs::StreamSharing::kAll);
    if (in == nullptr) {
        return bytes;
    }
    if (fs::disk().stream_seek(in, 0, SEEK_END) == 0) {
        const long size = fs::disk().stream_tell(in);
        if (size > 0 && fs::disk().stream_seek(in, 0, SEEK_SET) == 0) {
            bytes.resize(static_cast<std::size_t>(size));
            const std::size_t got = fs::disk().stream_read(in, bytes.data(), 1, bytes.size());
            bytes.resize(got);
        }
    }
    fs::disk().stream_close(in);
    return bytes;
}

// (the caches' saver, off the drawing thread) `bytes` written beside `path` and moved over it: a reader - the next
// start - never sees half a file. (The disk layer's calls hold no state of their own: a thread may make them.)
bool write_file_replacing(const std::string& path, std::span<const uint8_t> bytes)
{
    const std::string temporary = path + ".saving";
    fs::DiskStream* const out = fs::disk().open_stream(temporary.c_str(), "wb", fs::StreamSharing::kAll);
    if (out == nullptr) {
        return false;
    }
    const std::size_t written = fs::disk().stream_write(out, bytes.data(), 1, bytes.size());
    if (fs::disk().stream_close(out) != 0 || written != bytes.size()) {
        fs::disk().remove(temporary.c_str());
        return false;
    }
    return fs::disk().replace(temporary.c_str(), path.c_str(), false);
}

// A little-endian reader over a file's bytes.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    template <typename T>
    bool read(T* value)
    {
        if (at_ + sizeof(T) > bytes_.size()) {
            return false;
        }
        std::memcpy(value, bytes_.subspan(at_, sizeof(T)).data(), sizeof(T));
        at_ += sizeof(T);
        return true;
    }
    bool read_words(std::size_t count, std::vector<uint32_t>* words)
    {
        const std::size_t size = count * sizeof(uint32_t);
        if (at_ + size > bytes_.size()) {
            return false;
        }
        words->resize(count);
        std::memcpy(words->data(), bytes_.subspan(at_, size).data(), size);
        at_ += size;
        return true;
    }

    bool read_bytes(std::size_t size, std::vector<uint8_t>* data)
    {
        if (at_ + size > bytes_.size()) {
            return false;
        }
        data->assign(bytes_.begin() + static_cast<std::ptrdiff_t>(at_),
                     bytes_.begin() + static_cast<std::ptrdiff_t>(at_ + size));
        at_ += size;
        return true;
    }

private:
    std::span<const uint8_t> bytes_;
    std::size_t at_ = 0;
};

template <typename T>
void append(std::vector<uint8_t>& out, const T& value)
{
    std::array<uint8_t, sizeof(T)> raw{};
    std::memcpy(raw.data(), &value, sizeof(T));
    out.insert(out.end(), raw.begin(), raw.end());
}

} // namespace

std::size_t PipelineKeyHash::operator()(const PipelineKey& key) const
{
    std::array<uint8_t, sizeof(PipelineKey)> raw{};
    std::memcpy(raw.data(), &key, sizeof(PipelineKey));
    return static_cast<std::size_t>(fnv1a(raw));
}

std::size_t PipelineKeyTableHash::operator()(const PipelineKey& key) const
{
    constexpr std::size_t kWords = (sizeof(PipelineKey) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
    std::array<uint64_t, kWords> words{};
    std::memcpy(words.data(), &key, sizeof(PipelineKey));
    constexpr uint64_t kMix = 0x9E3779B97F4A7C15ULL;
    constexpr unsigned kFold = 29;
    uint64_t hash = sizeof(PipelineKey);
    for (const uint64_t word : words) {
        hash = (hash ^ word) * kMix;
        hash ^= hash >> kFold;
    }
    return static_cast<std::size_t>(hash);
}

// (round TRAIN) The draws' set 0 bindings (push descriptors), the one definition the pipelines and the shader check
// (ff_shader_check) both lay out - the check had its own copy without the water's, the snow's, the paint's, the wet
// ground's and the emission map's bindings.
std::vector<VkDescriptorSetLayoutBinding> draw_set_bindings(bool ray_queries)
{
    constexpr std::size_t kWaterBindings = 5; // (NOT ORIGINAL - rt_water: kWaterUniformsBinding - kWaterWavesBinding)
    constexpr std::size_t kDetailBindings = 2; // (NOT ORIGINAL - rt_detail: kDetailUniformsBinding, kDetailMapBinding)
    constexpr std::size_t kSnowBindings = 2;   // (NOT ORIGINAL - snow: kSnowUniformsBinding, kSnowSkyBinding)
    constexpr std::size_t kPaintBindings = 3;  // (NOT ORIGINAL - paint: kPaintUniformsBinding - kPaintPlaceBinding)
    constexpr std::size_t kWetBindings = 2;    // (NOT ORIGINAL - rain: kWetUniformsBinding, kWetMapBinding)
    constexpr std::size_t kEmissionBindings = 1; // (NOT ORIGINAL - round SUBPIX: kEmissionMapBinding)
    std::array<VkDescriptorSetLayoutBinding, 2 + kStageCount + 4 + kWaterBindings + kDetailBindings + kSnowBindings +
                                                 kPaintBindings + kWetBindings + kEmissionBindings>
        bindings{};
    core::at(bindings, 0) = {.binding = kVertexUniformsBinding,
                             .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                             .pImmutableSamplers = nullptr};
    core::at(bindings, 1) = {.binding = kFragmentUniformsBinding,
                             .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                             .pImmutableSamplers = nullptr};
    for (uint32_t stage = 0; stage < kStageCount; ++stage) {
        core::at(bindings, 2 + stage) = {.binding = kFirstTextureBinding + stage,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                         .descriptorCount = 1,
                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                         .pImmutableSamplers = nullptr};
    }
    // (NOT ORIGINAL - rt_pbr, vulkan_surface.h) the per-pixel lighting's block and surface map: pushed only for the keys
    // that read them, so the other draws' shaders and descriptors are as before
    core::at(bindings, 2 + kStageCount) = {.binding = kSurfaceUniformsBinding,
                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                           .descriptorCount = 1,
                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                           .pImmutableSamplers = nullptr};
    core::at(bindings, 3 + kStageCount) = {.binding = kSurfaceMapBinding,
                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                           .descriptorCount = 1,
                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                           .pImmutableSamplers = nullptr};
    // (added by cel shading - vulkan_cel.h) its block, and with ray queries the frame's scene: pushed only for the keys
    // that read them. The scene is the array's last (left out of a layout without ray queries).
    constexpr std::size_t kCelBlockSlot = 4 + kStageCount;
    constexpr std::size_t kWaterFirstSlot = kCelBlockSlot + 1;
    constexpr std::size_t kDetailFirstSlot = kWaterFirstSlot + kWaterBindings;
    constexpr std::size_t kSnowFirstSlot = kDetailFirstSlot + kDetailBindings;
    constexpr std::size_t kPaintFirstSlot = kSnowFirstSlot + kSnowBindings;
    constexpr std::size_t kWetFirstSlot = kPaintFirstSlot + kPaintBindings;
    constexpr std::size_t kEmissionSlot = kWetFirstSlot + kWetBindings;
    constexpr std::size_t kCelSceneSlot = kEmissionSlot + kEmissionBindings;
    static_assert(kCelSceneSlot + 1 == bindings.size(), "the scene is the last binding");
    core::at(bindings, kCelBlockSlot) = {.binding = kCelUniformsBinding,
                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                           .descriptorCount = 1,
                                           .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           .pImmutableSamplers = nullptr};
    core::at(bindings, kCelSceneSlot) = {.binding = kCelSceneBinding,
                                           .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                                           .descriptorCount = 1,
                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                           .pImmutableSamplers = nullptr};
    // (NOT ORIGINAL - rt_water, vulkan_water.h) the water's block, the copy behind it, the sky and the ripples: pushed only
    // for water draws. The block and the depth behind are the vertex shader's too (rt_water_geometry: the moving surface).
    for (uint32_t water = 0; water < kWaterBindings; ++water) {
        const uint32_t binding = kWaterUniformsBinding + water;
        const bool both = binding == kWaterUniformsBinding || binding == kWaterDepthBinding;
        core::at(bindings, kWaterFirstSlot + water) = {
            .binding = binding,
            .descriptorType = water == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = both ? VkShaderStageFlags{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT}
                               : VkShaderStageFlags{VK_SHADER_STAGE_FRAGMENT_BIT},
            .pImmutableSamplers = nullptr};
    }
    static_assert(kWaterWavesBinding == kWaterUniformsBinding + kWaterBindings - 1, "the water's bindings follow each other");
    // (NOT ORIGINAL - rt_detail / rt_delight, vulkan_detail.h) the detail layer's block and texture: pushed only for the
    // keys that read them
    core::at(bindings, kDetailFirstSlot) = {.binding = kDetailUniformsBinding,
                                            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                            .descriptorCount = 1,
                                            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                            .pImmutableSamplers = nullptr};
    core::at(bindings, kDetailFirstSlot + 1) = {.binding = kDetailMapBinding,
                                                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                .descriptorCount = 1,
                                                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                .pImmutableSamplers = nullptr};
    // (NOT ORIGINAL - snow, vulkan_snow.h) the snow's block and the level's sky map: pushed only for the keys that read them
    core::at(bindings, kSnowFirstSlot) = {.binding = kSnowUniformsBinding,
                                          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                          .descriptorCount = 1,
                                          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                          .pImmutableSamplers = nullptr};
    core::at(bindings, kSnowFirstSlot + 1) = {.binding = kSnowSkyBinding,
                                              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                              .descriptorCount = 1,
                                              .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                              .pImmutableSamplers = nullptr};
    // (NOT ORIGINAL - paint, vulkan_paint.h) the paint's block, its coats and places: pushed only for the keys that read them
    for (uint32_t paint = 0; paint < kPaintBindings; ++paint) {
        core::at(bindings, kPaintFirstSlot + paint) = {
            .binding = kPaintUniformsBinding + paint,
            .descriptorType = paint == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr};
    }
    static_assert(kPaintPlaceBinding == kPaintUniformsBinding + kPaintBindings - 1, "the paint's bindings follow each other");
    // (NOT ORIGINAL - rain, vulkan_rain.h) the wet ground's block and the precipitation occlusion map: pushed only for the
    // keys that read them
    core::at(bindings, kWetFirstSlot) = {.binding = kWetUniformsBinding,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                         .descriptorCount = 1,
                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                         .pImmutableSamplers = nullptr};
    core::at(bindings, kWetFirstSlot + 1) = {.binding = kWetMapBinding,
                                             .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                             .descriptorCount = 1,
                                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                             .pImmutableSamplers = nullptr};
    // (NOT ORIGINAL - round SUBPIX) a relit surface's emission map: pushed only for the keys that read it (kSurfaceRelit)
    core::at(bindings, kEmissionSlot) = {.binding = kEmissionMapBinding,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                         .descriptorCount = 1,
                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                         .pImmutableSamplers = nullptr};
    return {bindings.begin(), bindings.end() - (ray_queries ? 0 : 1)};
}

// (round TRAIN) Set 1's bindings: the view's light inputs (ff_uniforms.h kLightInputsSet).
std::vector<VkDescriptorSetLayoutBinding> light_set_bindings()
{
    std::array<VkDescriptorSetLayoutBinding, kLightInputCount> light_bindings{};
    for (uint32_t light = 0; light < kLightInputCount; ++light) {
        core::at(light_bindings, light) = {.binding = light,
                                           .descriptorType = light == kLightGuidesBinding ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                                                          : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                           .descriptorCount = 1,
                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                           .pImmutableSamplers = nullptr};
    }
    return {light_bindings.begin(), light_bindings.end()};
}

bool Pipelines::init(VkDevice device, VkPhysicalDevice /*physical*/, const std::string& folder, bool ray_queries)
{
    device_ = device;
    folder_ = folder;
    ray_queries_ = ray_queries;
    const std::vector<VkDescriptorSetLayoutBinding> bindings = draw_set_bindings(ray_queries);
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    if (vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &set_layout_) != VK_SUCCESS) {
        return false;
    }
    // (NOT ORIGINAL - round V, vulkan_surface_light.h; round FL: a set of their own) the view's light inputs: set 1, one
    // descriptor set a view (vulkan_surface_light.cpp), bound for the keys that read them
    const std::vector<VkDescriptorSetLayoutBinding> light_bindings = light_set_bindings();
    const VkDescriptorSetLayoutCreateInfo light_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .bindingCount = kLightInputCount,
                                                     .pBindings = light_bindings.data()};
    if (vkCreateDescriptorSetLayout(device_, &light_info, nullptr, &light_set_layout_) != VK_SUCCESS) {
        return false;
    }
    static_assert(kLightInputsSet == 1, "the light inputs' set follows the draws' pushed set");
    const std::array<VkDescriptorSetLayout, 2> set_layouts{set_layout_, light_set_layout_};
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = static_cast<uint32_t>(set_layouts.size()),
                                                 .pSetLayouts = set_layouts.data(),
                                                 .pushConstantRangeCount = 0,
                                                 .pPushConstantRanges = nullptr};
    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        return false;
    }
    load(folder);
    return true;
}

void Pipelines::load(const std::string& folder)
{
    uint32_t newest = 0;
    if (!folder.empty()) {
        const std::vector<uint8_t> shader_bytes = read_file(folder + kShaderFile);
        Reader reader(shader_bytes);
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t generation = 0;
        uint32_t count = 0;
        if (reader.read(&magic) && reader.read(&version) && reader.read(&generation) && reader.read(&count) &&
            magic == kShaderFileMagic && version == kShaderFileVersion) {
            newest = std::max(newest, generation);
            for (uint32_t entry = 0; entry < count; ++entry) {
                uint64_t hash = 0;
                uint32_t used = 0;
                uint32_t words = 0;
                std::vector<uint32_t> spirv;
                if (!reader.read(&hash) || !reader.read(&used) || !reader.read(&words) || words > kMostSpirvWords ||
                    !reader.read_words(words, &spirv)) {
                    break;
                }
                spirv_.emplace(hash, SpirvEntry{.words = std::make_shared<const std::vector<uint32_t>>(std::move(spirv)),
                                                .used = used});
            }
        }
        // (a file of the driver's whole cache - before the blobs - isn't read: it has no entries to keep or drop)
        const std::vector<uint8_t> pipeline_bytes = read_file(folder + kPipelineFile);
        Reader blobs(pipeline_bytes);
        if (blobs.read(&magic) && blobs.read(&version) && blobs.read(&generation) && blobs.read(&count) &&
            magic == kPipelineFileMagic && version == kPipelineFileVersion) {
            newest = std::max(newest, generation);
            for (uint32_t entry = 0; entry < count; ++entry) {
                uint64_t identity = 0;
                uint32_t used = 0;
                uint32_t size = 0;
                std::vector<uint8_t> data;
                if (!blobs.read(&identity) || !blobs.read(&used) || !blobs.read(&size) ||
                    !blobs.read_bytes(size, &data)) {
                    break;
                }
                blobs_.emplace(identity, PipelineBlob{.data = std::make_shared<const std::vector<uint8_t>>(std::move(data)),
                                                      .used = used});
            }
        }
    }
    generation_ = newest + 1;
}

namespace {

// What a save writes: the entries as they were when it was asked for (the data shared, never copied).
struct Snapshot {
    uint32_t generation = 1;
    std::vector<std::pair<uint64_t, Pipelines::SpirvEntry>> shaders;
    std::vector<std::pair<uint64_t, Pipelines::PipelineBlob>> blobs;
};

// The newest entries (by `used`, this run's first) that fit in `cap` bytes.
template <typename Entry, typename Size>
void keep_newest(std::vector<std::pair<uint64_t, Entry>>* entries, std::size_t cap, Size size_of)
{
    std::ranges::stable_sort(*entries, [](const auto& left, const auto& right) { return left.second.used > right.second.used; });
    std::size_t total = 0;
    std::size_t keep = 0;
    for (const auto& [key, entry] : *entries) {
        total += size_of(entry);
        if (total > cap) {
            break;
        }
        ++keep;
    }
    entries->resize(keep);
}

// The caches written (off the drawing thread), each file beside its own and moved over it.
void write_caches(Snapshot snapshot, const std::string& folder)
{
    const auto began = std::chrono::steady_clock::now();
    std::size_t bytes = 0;
    std::size_t dropped = 0;
    {
        const std::size_t before = snapshot.shaders.size();
        keep_newest(&snapshot.shaders, kShaderCacheBytes,
                    [](const Pipelines::SpirvEntry& entry) { return entry.words->size() * sizeof(uint32_t); });
        dropped += before - snapshot.shaders.size();
        std::vector<uint8_t> file;
        append(file, kShaderFileMagic);
        append(file, kShaderFileVersion);
        append(file, snapshot.generation);
        append(file, static_cast<uint32_t>(snapshot.shaders.size()));
        for (const auto& [hash, entry] : snapshot.shaders) {
            append(file, hash);
            append(file, entry.used);
            append(file, static_cast<uint32_t>(entry.words->size()));
            const std::size_t at = file.size();
            file.resize(at + (entry.words->size() * sizeof(uint32_t)));
            std::memcpy(std::span(file).subspan(at).data(), entry.words->data(), entry.words->size() * sizeof(uint32_t));
        }
        if (write_file_replacing(folder + kShaderFile, file)) {
            bytes += file.size();
        }
    }
    {
        const std::size_t before = snapshot.blobs.size();
        keep_newest(&snapshot.blobs, kPipelineCacheBytes,
                    [](const Pipelines::PipelineBlob& entry) { return entry.data->size(); });
        dropped += before - snapshot.blobs.size();
        std::vector<uint8_t> file;
        append(file, kPipelineFileMagic);
        append(file, kPipelineFileVersion);
        append(file, snapshot.generation);
        append(file, static_cast<uint32_t>(snapshot.blobs.size()));
        for (const auto& [identity, entry] : snapshot.blobs) {
            append(file, identity);
            append(file, entry.used);
            append(file, static_cast<uint32_t>(entry.data->size()));
            file.insert(file.end(), entry.data->begin(), entry.data->end());
        }
        if (write_file_replacing(folder + kPipelineFile, file)) {
            bytes += file.size();
        }
    }
    get_log("vulkan: the shader and pipeline caches saved (%.1f MB, %zu entries dropped for the size) in %.0f ms, off the drawing thread",
            static_cast<double>(bytes) / (1024.0 * 1024.0), dropped, seconds_since(began) * 1000.0);
}

// (the freeze audit) The caches are saved off the drawing thread: a 128 MB write on it was a 9 s frame once (the disk
// busy, the file scanned). One save at a time - a save asked for meanwhile waits for the next time (still dirty); the
// entries are taken here as pointers (a few thousand), everything else is the saver's.
Snapshot take_snapshot(uint32_t generation, const std::unordered_map<uint64_t, Pipelines::SpirvEntry>& spirv,
                       const std::unordered_map<uint64_t, Pipelines::PipelineBlob>& blobs)
{
    Snapshot snapshot;
    snapshot.generation = generation;
    snapshot.shaders.reserve(spirv.size());
    for (const auto& [hash, entry] : spirv) {
        if (entry.words && !entry.words->empty()) {
            snapshot.shaders.emplace_back(hash, entry);
        }
    }
    snapshot.blobs.reserve(blobs.size());
    for (const auto& [identity, blob] : blobs) {
        if (blob.data && !blob.data->empty()) {
            snapshot.blobs.emplace_back(identity, blob);
        }
    }
    return snapshot;
}

} // namespace

void Pipelines::save()
{
    if (!dirty_ || folder_.empty() || device_ == nullptr || saving_.load()) {
        return;
    }
    dirty_ = false;
    if (saver_.joinable()) {
        saver_.join(); // (finished: saving_ is false)
    }
    saving_.store(true);
    saver_ = std::jthread([this, snapshot = take_snapshot(generation_, spirv_, blobs_)]() mutable {
        write_caches(std::move(snapshot), folder_);
        saving_.store(false);
    });
}

void Pipelines::save_now()
{
    if (saver_.joinable()) {
        saver_.join();
    }
    if (!dirty_ || folder_.empty() || device_ == nullptr) {
        return;
    }
    dirty_ = false;
    write_caches(take_snapshot(generation_, spirv_, blobs_), folder_);
}

void Pipelines::shutdown()
{
    if (device_ == nullptr) {
        return;
    }
    save_now(); // (a save under way finished first)
    for (const auto& [key, pipeline] : pipelines_) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline, nullptr);
        }
    }
    for (const auto& [key, module] : vertex_modules_) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, module, nullptr);
        }
    }
    for (const auto& [key, module] : fragment_modules_) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, module, nullptr);
        }
    }
    for (const auto& [key, pipeline] : screen_pipelines_) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline, nullptr);
        }
    }
    for (const auto& [key, module] : screen_modules_) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, module, nullptr);
        }
    }
    screen_pipelines_.clear();
    screen_modules_.clear();
    pipelines_.clear();
    vertex_modules_.clear();
    fragment_modules_.clear();
    spirv_.clear();
    blobs_.clear();
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
    }
    if (set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
    }
    if (light_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, light_set_layout_, nullptr);
    }
    layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    light_set_layout_ = VK_NULL_HANDLE;
    device_ = nullptr;
    stats_ = {};
}

VkShaderModule Pipelines::spirv_module(ShaderStage stage, const std::string& source, std::string* error, bool ray_query)
{
    const uint64_t hash = text_hash(source);
    auto kept = spirv_.find(hash);
    if (kept == spirv_.end()) {
        const auto start = std::chrono::steady_clock::now();
        CompiledShader compiled =
            ray_query ? compile_rt_glsl(RtShaderStage::kFragment, source) : compile_glsl(stage, source);
        stats_.compile_seconds += seconds_since(start);
        ++stats_.shaders_compiled;
        if (!compiled.ok) {
            if (error != nullptr) {
                *error = compiled.log + "\n" + source;
            }
            kept = spirv_.emplace(hash, SpirvEntry{}).first;
        } else {
            kept = spirv_
                       .emplace(hash, SpirvEntry{.words = std::make_shared<const std::vector<uint32_t>>(std::move(compiled.spirv)),
                                                 .used = generation_})
                       .first;
            dirty_ = true;
        }
    } else {
        ++stats_.shaders_from_disk;
    }
    if (kept->second.used != generation_) {
        kept->second.used = generation_; // (kept in the next save: this run used it; nothing is written for that alone)
    }
    VkShaderModule made = VK_NULL_HANDLE;
    if (kept->second.words && !kept->second.words->empty()) {
        // (round OM: a ray-query module kept on disk by an earlier run says it traverses opacity micromaps as this
        // device does - vulkan_rt_glsl.h)
        std::vector<uint32_t> declared;
        if (ray_query) {
            declared = *kept->second.words;
            spirv_micromaps_declared(&declared, micromaps_declared());
        }
        const std::vector<uint32_t>& words = ray_query ? declared : *kept->second.words;
        const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                            .pNext = nullptr,
                                            .flags = 0,
                                            .codeSize = words.size() * sizeof(uint32_t),
                                            .pCode = words.data()};
        if (vkCreateShaderModule(device_, &info, nullptr, &made) != VK_SUCCESS) {
            made = VK_NULL_HANDLE;
        }
    }
    return made;
}

VkShaderModule Pipelines::module(bool vertex, const ShaderKey& key, std::string* error)
{
    if (vertex) {
        const auto found = vertex_modules_.find(key.vertex);
        if (found != vertex_modules_.end()) {
            return found->second;
        }
    } else {
        const auto found = fragment_modules_.find(key.fragment);
        if (found != fragment_modules_.end()) {
            return found->second;
        }
    }
    const std::string source = vertex ? vertex_shader_glsl(key.vertex) : fragment_shader_glsl(key.fragment);
    // (added by cel shading) a fragment shader with ray queries needs SPIR-V 1.4 (the ray-tracing work's compiler);
    // none on a device without them
    const bool ray_query = !vertex && (key.fragment.surface & kSurfaceCelShadow) != 0U; // (round W: the snow's reach the map's)
    if (ray_query && !ray_queries_) {
        if (error != nullptr) {
            *error = "a ray query on a device without ray tracing";
        }
        fragment_modules_.emplace(key.fragment, VK_NULL_HANDLE);
        return VK_NULL_HANDLE;
    }
    const VkShaderModule made =
        spirv_module(vertex ? ShaderStage::kVertex : ShaderStage::kFragment, source, error, ray_query);
    if (vertex) {
        vertex_modules_.emplace(key.vertex, made);
    } else {
        fragment_modules_.emplace(key.fragment, made);
    }
    return made;
}

bool Pipelines::make_graphics(const VkGraphicsPipelineCreateInfo& info, uint64_t identity, VkPipeline* made)
{
    // (Vulkan 1.3's creation feedback - the draws need 1.3: whether the cache had it. Each pipeline has a cache of its
    // own, made from the blob kept for it and read back when it wasn't in it: the files hold exactly the pipelines the
    // runs use, each with the run that last did, and a save keeps the newest up to a size - the driver's one big cache
    // can't be pruned.)
    VkPipelineCreationFeedback whole{};
    VkPipelineCreationFeedbackCreateInfo feedback{.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO,
                                                  .pNext = info.pNext,
                                                  .pPipelineCreationFeedback = &whole,
                                                  .pipelineStageCreationFeedbackCount = 0,
                                                  .pPipelineStageCreationFeedbacks = nullptr};
    VkGraphicsPipelineCreateInfo chained = info;
    chained.pNext = &feedback;
    const auto start = std::chrono::steady_clock::now();
    const auto kept = blobs_.find(identity);
    const bool had_blob = kept != blobs_.end() && kept->second.data && !kept->second.data->empty();
    VkPipelineCacheCreateInfo cache_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
                                         .pNext = nullptr,
                                         .flags = 0,
                                         .initialDataSize = had_blob ? kept->second.data->size() : 0,
                                         .pInitialData = had_blob ? kept->second.data->data() : nullptr};
    VkPipelineCache cache = VK_NULL_HANDLE;
    if (vkCreatePipelineCache(device_, &cache_info, nullptr, &cache) != VK_SUCCESS) {
        cache = VK_NULL_HANDLE;
    }
    const bool ok = vkCreateGraphicsPipelines(device_, cache, 1, &chained, nullptr, made) == VK_SUCCESS;
    stats_.pipeline_seconds += seconds_since(start);
    if (!ok) {
        *made = VK_NULL_HANDLE;
    }
    if (ok && cache != VK_NULL_HANDLE) {
        const bool from_cache = had_blob && (whole.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT) != 0U &&
                                (whole.flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT) != 0U;
        if (from_cache) {
            kept->second.used = generation_;
        } else {
            std::size_t size = 0;
            std::vector<uint8_t> data;
            if (vkGetPipelineCacheData(device_, cache, &size, nullptr) == VK_SUCCESS && size > 0) {
                data.resize(size);
                if (vkGetPipelineCacheData(device_, cache, &size, data.data()) == VK_SUCCESS) {
                    data.resize(size);
                    blobs_[identity] = PipelineBlob{.data = std::make_shared<const std::vector<uint8_t>>(std::move(data)),
                                                    .used = generation_};
                    dirty_ = true;
                }
            }
        }
    }
    if (cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(device_, cache, nullptr);
    }
    return ok;
}

VkPipeline Pipelines::pipeline(const PipelineKey& key, std::string* error)
{
    const auto found = pipelines_.find(key);
    if (found != pipelines_.end()) {
        return found->second;
    }
    const auto began = std::chrono::steady_clock::now();
    const VkShaderModule vertex = module(true, key.shaders, error);
    const VkShaderModule fragment = module(false, key.shaders, error);
    VkPipeline made = VK_NULL_HANDLE;
    if (vertex != VK_NULL_HANDLE && fragment != VK_NULL_HANDLE) {
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vertex,
             .pName = "main",
             .pSpecializationInfo = nullptr},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = fragment,
             .pName = "main",
             .pSpecializationInfo = nullptr},
        }};
        const VertexLayout layout = vertex_layout(key.shaders.vertex.fvf);
        // (the upscaler's motion: binding 1 - the vertices of the frame before, the same layout - its position)
        const bool motion_input = key.shaders.vertex.motion != 0U;
        // (added by cel shading: the cel stream at binding 2 - three D3DCOLOR words a vertex, rt_cel.h)
        const bool cel_words = (key.shaders.vertex.surface & kVertexCelStream) != 0U;
        // (lighting round A: the relit light store's colour word at the same binding)
        const bool relit_colour = (key.shaders.vertex.surface & kVertexRelitColour) != 0U;
        const bool cel_input = cel_words || relit_colour;
        std::array<VkVertexInputBindingDescription, 3> bindings{{
            {.binding = 0, .stride = layout.bytes, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
            {.binding = 1, .stride = layout.bytes, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
            {.binding = kCelStreamBinding, .stride = rt::kCelStreamBytes, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
        }};
        uint32_t binding_count = motion_input ? 2U : 1U;
        if (cel_input) {
            if (!motion_input) {
                core::at(bindings, 1) = core::at(bindings, 2); // (the descriptions needn't be in order of binding)
            }
            ++binding_count;
        }
        std::array<VkVertexInputAttributeDescription, kMostAttributes + 7> attributes{};
        std::copy_n(layout.attributes.begin(), layout.count, attributes.begin());
        uint32_t attribute_count = layout.count;
        if (motion_input) {
            core::at(attributes, attribute_count++) = {
                .location = kPreviousPositionLocation, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0};
        }
        // (round L - the physical light store: the light words RGB9E5, read as uint and decoded in the shader; the tint
        // and the flags D3DCOLORs)
        if (cel_words) {
            for (uint32_t word = 0; word < rt::kCelStreamAttributes; ++word) {
                core::at(attributes, attribute_count++) = {.location = kCelLocation + word,
                                                           .binding = kCelStreamBinding,
                                                           .format = word == 1 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R32_UINT,
                                                           .offset = word * 4};
            }
            core::at(attributes, attribute_count++) = {.location = kCelEmitLocation,
                                                       .binding = kCelStreamBinding,
                                                       .format = VK_FORMAT_R32_UINT,
                                                       .offset = rt::kCelEmitWord * 4};
        }
        if (relit_colour) {
            core::at(attributes, attribute_count++) = {.location = kRelitColourLocation,
                                                       .binding = kCelStreamBinding,
                                                       .format = VK_FORMAT_R32_UINT,
                                                       .offset = rt::kCelColourWord * 4};
        }
        if (cel_input) {
            core::at(attributes, attribute_count++) = {.location = kCelFlagsLocation,
                                                       .binding = kCelStreamBinding,
                                                       .format = VK_FORMAT_B8G8R8A8_UNORM,
                                                       .offset = rt::kCelFlagsWord * 4};
        }
        const VkPipelineVertexInputStateCreateInfo input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = binding_count,
            .pVertexBindingDescriptions = bindings.data(),
            .vertexAttributeDescriptionCount = attribute_count,
            .pVertexAttributeDescriptions = attributes.data()};
        constexpr std::array<VkPrimitiveTopology, 3> kClassTopology{
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        const VkPipelineInputAssemblyStateCreateInfo assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = core::at(kClassTopology, std::min<std::size_t>(key.topology_class, 2)),
            .primitiveRestartEnable = VK_FALSE};
        const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                         .pNext = nullptr,
                                                         .flags = 0,
                                                         .viewportCount = 1,
                                                         .pViewports = nullptr,
                                                         .scissorCount = 1,
                                                         .pScissors = nullptr};
        const VkPipelineRasterizationStateCreateInfo raster{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = key.wireframe != 0 ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_TRUE,
            .depthBiasConstantFactor = 0.0F,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = 0.0F,
            .lineWidth = 1.0F};
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = static_cast<VkSampleCountFlagBits>(key.samples),
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE};
        const VkPipelineDepthStencilStateCreateInfo depth{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0F,
            .maxDepthBounds = 1.0F};
        const VkBlendFactor source = blend_factor(key.source_blend);
        const VkBlendFactor dest = blend_factor(key.dest_blend);
        const VkBlendOp operation = blend_op(key.blend_operation);
        // (round RG - PipelineKey::layer) into Ray Reconstruction's transparency layer the alpha is how much of what is
        // behind the layer hides: what the draw keeps of it (its destination factor) x the layer's, plus the rest of what
        // it hides (1 - kept: the source's alpha x one for an alpha blend; nothing for added light)
        const bool layer = key.layer != 0U && key.blend != 0U;
        const VkBlendFactor alpha_source =
            !layer ? source : (dest == VK_BLEND_FACTOR_ONE ? VK_BLEND_FACTOR_ZERO : VK_BLEND_FACTOR_ONE);
        const VkPipelineColorBlendAttachmentState blend_attachment{
            .blendEnable = key.blend != 0 ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = source,
            .dstColorBlendFactor = dest,
            .colorBlendOp = operation,
            .srcAlphaBlendFactor = alpha_source,
            .dstAlphaBlendFactor = dest,
            .alphaBlendOp = layer ? VK_BLEND_OP_ADD : operation,
            .colorWriteMask = key.write_mask};
        // (the upscaler's motion attachment, vulkan_motion.h: a draw that doesn't blend writes its motion (x, y) - what
        // is seen through a blended draw keeps its own -, a blended one only its share of the pixel (z, the largest);
        // a draw without the motion bit (pre-transformed) nothing)
        const bool motion_shader = key.motion != 0U && key.shaders.fragment.motion != 0U;
        VkColorComponentFlags motion_mask = 0;
        if (motion_shader && key.write_mask != 0U) {
            motion_mask = key.blend != 0U && key.motion != kMotionSurface ? VkColorComponentFlags{VK_COLOR_COMPONENT_B_BIT}
                                          : VkColorComponentFlags{VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT};
        }
        std::array<VkPipelineColorBlendAttachmentState, 2> blend_attachments{{
            blend_attachment,
            {.blendEnable = key.blend != 0U && key.motion != kMotionSurface ? VK_TRUE : VK_FALSE,
             .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
             .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
             .colorBlendOp = VK_BLEND_OP_MAX,
             .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
             .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
             .alphaBlendOp = VK_BLEND_OP_MAX,
             .colorWriteMask = motion_mask},
        }};
        // (round DECAL - PipelineKey::dbuffer) the decals' two inputs, each layer over the last as the decals' pass lays
        // its marks (vulkan_scatter.cpp): the base's weight a product, the layers' premultiplied values a sum over it
        if (key.dbuffer != 0U) {
            for (VkPipelineColorBlendAttachmentState& over : blend_attachments) {
                over = {.blendEnable = VK_TRUE,
                        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                        .dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                        .colorBlendOp = VK_BLEND_OP_ADD,
                        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                        .dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                        .alphaBlendOp = VK_BLEND_OP_ADD,
                        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT};
            }
        }
        const uint32_t attachment_count = key.motion != 0U || key.dbuffer != 0U ? 2U : 1U;
        const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                        .pNext = nullptr,
                                                        .flags = 0,
                                                        .logicOpEnable = VK_FALSE,
                                                        .logicOp = VK_LOGIC_OP_COPY,
                                                        .attachmentCount = attachment_count,
                                                        .pAttachments = blend_attachments.data(),
                                                        .blendConstants = {}};
        constexpr std::array<VkDynamicState, 11> kDynamic{
            VK_DYNAMIC_STATE_VIEWPORT,           VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_CULL_MODE,          VK_DYNAMIC_STATE_FRONT_FACE,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
            VK_DYNAMIC_STATE_DEPTH_BIAS,         VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE};
        const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = 0,
                                                       .dynamicStateCount = static_cast<uint32_t>(kDynamic.size()),
                                                       .pDynamicStates = kDynamic.data()};
        const std::array<VkFormat, 2> colour_formats{static_cast<VkFormat>(key.colour_format),
                                                     key.dbuffer != 0U ? static_cast<VkFormat>(key.colour_format) : kMotionFormat};
        const VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                      .pNext = nullptr,
                                                      .viewMask = 0,
                                                      .colorAttachmentCount = attachment_count,
                                                      .pColorAttachmentFormats = colour_formats.data(),
                                                      .depthAttachmentFormat = static_cast<VkFormat>(key.depth_format),
                                                      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED};
        const VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                .pNext = &rendering,
                                                .flags = 0,
                                                .stageCount = static_cast<uint32_t>(stages.size()),
                                                .pStages = stages.data(),
                                                .pVertexInputState = &input,
                                                .pInputAssemblyState = &assembly,
                                                .pTessellationState = nullptr,
                                                .pViewportState = &viewport,
                                                .pRasterizationState = &raster,
                                                .pMultisampleState = &multisample,
                                                .pDepthStencilState = &depth,
                                                .pColorBlendState = &blend,
                                                .pDynamicState = &dynamic,
                                                .layout = layout_,
                                                .renderPass = VK_NULL_HANDLE,
                                                .subpass = 0,
                                                .basePipelineHandle = VK_NULL_HANDLE,
                                                .basePipelineIndex = -1};
        if (!make_graphics(info, static_cast<uint64_t>(PipelineKeyHash{}(key)), &made) && error != nullptr && error->empty()) {
            *error = "vkCreateGraphicsPipelines refused it";
        }
    }
    if (made == VK_NULL_HANDLE) {
        ++stats_.refused;
    } else {
        ++stats_.pipelines;
    }
    // (a pipeline made while drawing stalls that frame: the slow ones named - their fragment key - with what they took)
    if (const double seconds = seconds_since(began); seconds > kSlowPipelineSeconds) {
        get_log("vulkan: a pipeline took %.0f ms to make (%s; %u made so far)", seconds * 1000.0,
                describe(key.shaders.fragment).c_str(), static_cast<unsigned>(stats_.pipelines));
    }
    pipelines_.emplace(key, made);
    return made;
}

namespace {

// The full-screen passes' vertex shader: the triangle (-1,-1) (3,-1) (-1,3), its texture coordinates 0 - 2 (0 - 1 over
// the target).
constexpr const char* kScreenPassVertex = R"(#version 450
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

} // namespace

VkPipeline Pipelines::screen_pass(VkFormat colour_format, const std::string& fragment, std::string* error)
{
    const uint64_t key = text_hash(fragment) ^ (static_cast<uint64_t>(colour_format) * kFnvPrime);
    const auto found = screen_pipelines_.find(key);
    if (found != screen_pipelines_.end()) {
        return found->second;
    }
    const auto module_of = [this, error](ShaderStage stage, const std::string& source) {
        const uint64_t hash = text_hash(source);
        const auto kept = screen_modules_.find(hash);
        if (kept != screen_modules_.end()) {
            return kept->second;
        }
        const VkShaderModule made = spirv_module(stage, source, error);
        screen_modules_.emplace(hash, made);
        return made;
    };
    const VkShaderModule vertex = module_of(ShaderStage::kVertex, kScreenPassVertex);
    const VkShaderModule pixel = module_of(ShaderStage::kFragment, fragment);
    VkPipeline made = VK_NULL_HANDLE;
    if (vertex != VK_NULL_HANDLE && pixel != VK_NULL_HANDLE) {
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vertex,
             .pName = "main",
             .pSpecializationInfo = nullptr},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = pixel,
             .pName = "main",
             .pSpecializationInfo = nullptr},
        }};
        const VkPipelineVertexInputStateCreateInfo input{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                         .pNext = nullptr,
                                                         .flags = 0,
                                                         .vertexBindingDescriptionCount = 0,
                                                         .pVertexBindingDescriptions = nullptr,
                                                         .vertexAttributeDescriptionCount = 0,
                                                         .pVertexAttributeDescriptions = nullptr};
        const VkPipelineInputAssemblyStateCreateInfo assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE};
        const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                         .pNext = nullptr,
                                                         .flags = 0,
                                                         .viewportCount = 1,
                                                         .pViewports = nullptr,
                                                         .scissorCount = 1,
                                                         .pScissors = nullptr};
        const VkPipelineRasterizationStateCreateInfo raster{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0F,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = 0.0F,
            .lineWidth = 1.0F};
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE};
        const VkPipelineColorBlendAttachmentState blend_attachment{
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT};
        const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                        .pNext = nullptr,
                                                        .flags = 0,
                                                        .logicOpEnable = VK_FALSE,
                                                        .logicOp = VK_LOGIC_OP_COPY,
                                                        .attachmentCount = 1,
                                                        .pAttachments = &blend_attachment,
                                                        .blendConstants = {}};
        constexpr std::array<VkDynamicState, 2> kDynamic{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = 0,
                                                       .dynamicStateCount = static_cast<uint32_t>(kDynamic.size()),
                                                       .pDynamicStates = kDynamic.data()};
        const VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                      .pNext = nullptr,
                                                      .viewMask = 0,
                                                      .colorAttachmentCount = 1,
                                                      .pColorAttachmentFormats = &colour_format,
                                                      .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
                                                      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED};
        const VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                .pNext = &rendering,
                                                .flags = 0,
                                                .stageCount = static_cast<uint32_t>(stages.size()),
                                                .pStages = stages.data(),
                                                .pVertexInputState = &input,
                                                .pInputAssemblyState = &assembly,
                                                .pTessellationState = nullptr,
                                                .pViewportState = &viewport,
                                                .pRasterizationState = &raster,
                                                .pMultisampleState = &multisample,
                                                .pDepthStencilState = nullptr,
                                                .pColorBlendState = &blend,
                                                .pDynamicState = &dynamic,
                                                .layout = layout_,
                                                .renderPass = VK_NULL_HANDLE,
                                                .subpass = 0,
                                                .basePipelineHandle = VK_NULL_HANDLE,
                                                .basePipelineIndex = -1};
        if (!make_graphics(info, key, &made) && error != nullptr && error->empty()) {
            *error = "vkCreateGraphicsPipelines refused it";
        }
    }
    if (made == VK_NULL_HANDLE) {
        ++stats_.refused;
    } else {
        ++stats_.pipelines;
    }
    screen_pipelines_.emplace(key, made);
    return made;
}

namespace {

// The UI's shaders (Pipelines::ui_pass): ImGui's vertices to the target through the vertex block's scale and offset;
// each pixel the vertex colour times the texture, as ImGui's own backends draw.
static_assert(kVertexUniformsBinding == 0 && kFirstTextureBinding == 2, "the UI's shaders name the bindings");
constexpr const char* kUiVertex = R"(#version 450
layout(set = 0, binding = 0, std140) uniform Ui { vec4 scale_offset; } ui;
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_colour;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_colour;
void main()
{
    v_uv = a_uv;
    v_colour = a_colour;
    gl_Position = vec4(a_position * ui.scale_offset.xy + ui.scale_offset.zw, 0.0, 1.0);
}
)";
constexpr const char* kUiFragment = R"(#version 450
layout(set = 0, binding = 2) uniform sampler2D t0;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_colour;
layout(location = 0) out vec4 o_colour;
void main()
{
    o_colour = v_colour * texture(t0, v_uv);
}
)";

} // namespace

VkPipeline Pipelines::ui_pass(VkFormat colour_format, VkFormat depth_format, uint32_t samples, std::string* error)
{
    const std::string vertex_source = kUiVertex;
    const std::string fragment_source = kUiFragment;
    const uint64_t key = (text_hash(fragment_source) ^ (static_cast<uint64_t>(colour_format) * kFnvPrime) ^
                          (static_cast<uint64_t>(depth_format) * kFnvPrime * kFnvPrime)) +
                         samples;
    const auto found = screen_pipelines_.find(key);
    if (found != screen_pipelines_.end()) {
        return found->second;
    }
    const auto module_of = [this, error](ShaderStage stage, const std::string& source) {
        const uint64_t hash = text_hash(source);
        const auto kept = screen_modules_.find(hash);
        if (kept != screen_modules_.end()) {
            return kept->second;
        }
        const VkShaderModule made = spirv_module(stage, source, error);
        screen_modules_.emplace(hash, made);
        return made;
    };
    const VkShaderModule vertex = module_of(ShaderStage::kVertex, vertex_source);
    const VkShaderModule pixel = module_of(ShaderStage::kFragment, fragment_source);
    VkPipeline made = VK_NULL_HANDLE;
    if (vertex != VK_NULL_HANDLE && pixel != VK_NULL_HANDLE) {
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vertex,
             .pName = "main",
             .pSpecializationInfo = nullptr},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .pNext = nullptr,
             .flags = 0,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = pixel,
             .pName = "main",
             .pSpecializationInfo = nullptr},
        }};
        const VkVertexInputBindingDescription binding{
            .binding = 0, .stride = kUiVertexBytes, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription, 3> attributes{{
            {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0},
            {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = kUiUvOffset},
            {.location = 2, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = kUiColourOffset},
        }};
        const VkPipelineVertexInputStateCreateInfo input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &binding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions = attributes.data()};
        const VkPipelineInputAssemblyStateCreateInfo assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE};
        const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                         .pNext = nullptr,
                                                         .flags = 0,
                                                         .viewportCount = 1,
                                                         .pViewports = nullptr,
                                                         .scissorCount = 1,
                                                         .pScissors = nullptr};
        const VkPipelineRasterizationStateCreateInfo raster{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0F,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = 0.0F,
            .lineWidth = 1.0F};
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = static_cast<VkSampleCountFlagBits>(samples),
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE};
        const VkPipelineDepthStencilStateCreateInfo depth{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_FALSE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_ALWAYS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0F,
            .maxDepthBounds = 1.0F};
        const VkPipelineColorBlendAttachmentState blend_attachment{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT};
        const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                        .pNext = nullptr,
                                                        .flags = 0,
                                                        .logicOpEnable = VK_FALSE,
                                                        .logicOp = VK_LOGIC_OP_COPY,
                                                        .attachmentCount = 1,
                                                        .pAttachments = &blend_attachment,
                                                        .blendConstants = {}};
        constexpr std::array<VkDynamicState, 2> kDynamic{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = 0,
                                                       .dynamicStateCount = static_cast<uint32_t>(kDynamic.size()),
                                                       .pDynamicStates = kDynamic.data()};
        const VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                      .pNext = nullptr,
                                                      .viewMask = 0,
                                                      .colorAttachmentCount = 1,
                                                      .pColorAttachmentFormats = &colour_format,
                                                      .depthAttachmentFormat = depth_format,
                                                      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED};
        const VkGraphicsPipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                .pNext = &rendering,
                                                .flags = 0,
                                                .stageCount = static_cast<uint32_t>(stages.size()),
                                                .pStages = stages.data(),
                                                .pVertexInputState = &input,
                                                .pInputAssemblyState = &assembly,
                                                .pTessellationState = nullptr,
                                                .pViewportState = &viewport,
                                                .pRasterizationState = &raster,
                                                .pMultisampleState = &multisample,
                                                .pDepthStencilState = &depth,
                                                .pColorBlendState = &blend,
                                                .pDynamicState = &dynamic,
                                                .layout = layout_,
                                                .renderPass = VK_NULL_HANDLE,
                                                .subpass = 0,
                                                .basePipelineHandle = VK_NULL_HANDLE,
                                                .basePipelineIndex = -1};
        if (!make_graphics(info, key, &made) && error != nullptr && error->empty()) {
            *error = "vkCreateGraphicsPipelines refused it";
        }
    }
    if (made == VK_NULL_HANDLE) {
        ++stats_.refused;
    } else {
        ++stats_.pipelines;
    }
    screen_pipelines_.emplace(key, made);
    return made;
}

} // namespace render::vulkan

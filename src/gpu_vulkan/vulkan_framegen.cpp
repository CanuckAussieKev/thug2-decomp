// NOT ORIGINAL - frame generation and low latency (vulkan_framegen.h; docs/design/vulkan.md "Frame generation"): the
// settings, the provider chosen, the HUD-less colour (a view listener, last at world_drawn), the UI image, and the
// frame's on / off, reset and counts.
#include "gpu_vulkan/vulkan_framegen.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"
#include "engine/sys/frame_blend.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr VkFormat kPictureFormat = VK_FORMAT_R8G8B8A8_UNORM; // (the HUD-less colour, the frame's, the UI: storage everywhere)
constexpr uint32_t kGroupSide = 8;
constexpr uint64_t kLogEvery = 3600;  // (frames between the log's lines on the frames shown)
constexpr uint32_t kFsrMultiplier = 2; // (FSR 3's frame generation makes one frame a drawn one)
constexpr uint64_t kReportGamma = 0x76C0'0001ULL;
constexpr uint64_t kReportFailed = 0x76C0'0002ULL;
constexpr uint64_t kReportFirst = 0x76C0'0003ULL;
constexpr uint64_t kReportTarget = 0x76C0'0004ULL;
constexpr uint32_t kPresentQueues = 2; // (FSR's present queue and its image-acquire queue)

// The UI image (vulkan_framegen.h step 4): where the frame's back buffer differs from its HUD-less colour the 2D is
// there - opaque, the frame's colour (premultiplied: Final = UI + (1 - a) HUD-less holds exactly); elsewhere clear.
constexpr const char* kUiSource = R"(#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba8) uniform readonly image2D frame_in;
layout(set = 0, binding = 1, rgba8) uniform readonly image2D hudless_in;
layout(set = 0, binding = 2, rgba8) uniform writeonly image2D ui_out;
layout(push_constant) uniform Pass {
    ivec2 size;
} pass;
void main()
{
    const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, pass.size))) {
        return;
    }
    const vec3 frame = imageLoad(frame_in, pixel).rgb;
    const vec3 hudless = imageLoad(hudless_in, pixel).rgb;
    const float ui = any(greaterThan(abs(frame - hudless), vec3(0.5 / 255.0))) ? 1.0 : 0.0;
    imageStore(ui_out, pixel, vec4(frame * ui, ui));
}
)";

struct UiConstants {
    std::array<int32_t, 2> size{};
};

struct FrameGenState {
    FrameGenSettings settings;
    std::unique_ptr<FrameGenProvider> provider;
    bool tried = false;
    uint32_t multiplier = 1;
    // the pictures (the swapchain's size)
    Image hudless;
    Image frame;
    Image ui;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pass = VK_NULL_HANDLE;
    bool failed = false;
    // the frame being made: its number (counted at the presents), the inputs and the HUD-less colour seen in it
    uint64_t serial = 1;
    uint64_t inputs_serial = 0;
    uint64_t hudless_serial = 0;
    bool menu = false;
    bool was_on = false;
    bool cut = false;
    uint64_t cuts_seen = 0;
    // the counts (frame_generation_counts; a provider's made / released added when it goes)
    FrameGenCounts counts;
    uint64_t made_before = 0;
    uint64_t released_before = 0;
    uint64_t logged_at = 0;
};

FrameGenState& framegen()
{
    static FrameGenState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// An image of the swapchain's size (made again when it changed), resting in VK_IMAGE_LAYOUT_GENERAL.
bool ensure_picture(Backend& state, Image* image, Extent extent)
{
    if (image->image != VK_NULL_HANDLE && image->extent.width == extent.width && image->extent.height == extent.height) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = kPictureFormat,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    image->resting = VK_IMAGE_LAYOUT_GENERAL;
    prepare_image(state, image);
    return true;
}

// The back buffer copied into `to` (the swapchain's size: scaled as the present's copy is).
void copy_back_buffer(VkCommandBuffer commands, Image* from, Image* to)
{
    Memory::transition(commands, from, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, to, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    VkImageBlit region{};
    region.srcSubresource = layers;
    region.srcOffsets[1] = {.x = static_cast<int32_t>(from->extent.width), .y = static_cast<int32_t>(from->extent.height), .z = 1};
    region.dstSubresource = layers;
    region.dstOffsets[1] = {.x = static_cast<int32_t>(to->extent.width), .y = static_cast<int32_t>(to->extent.height), .z = 1};
    vkCmdBlitImage(commands, from->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, to->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region, VK_FILTER_LINEAR);
    Memory::transition(commands, from, from->resting);
    Memory::transition(commands, to, VK_IMAGE_LAYOUT_GENERAL);
}

// Everything written before visible to everything after (the copies, the UI pass, the provider).
void full_barrier(VkCommandBuffer commands)
{
    const VkMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);
}

bool ensure_ui_pass(Backend& state, std::string* error)
{
    FrameGenState& own = framegen();
    if (own.pass != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    constexpr uint32_t kBindings = 3;
    std::array<VkDescriptorSetLayoutBinding, kBindings> bindings{};
    for (uint32_t binding = 0; binding < kBindings; ++binding) {
        core::at(bindings, binding) = {.binding = binding,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                       .descriptorCount = 1,
                                       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                       .pImmutableSamplers = nullptr};
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = kBindings,
                                                   .pBindings = bindings.data()};
    const VkPushConstantRange constants{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(UiConstants)};
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS) {
        own.set_layout = VK_NULL_HANDLE;
        *error = "the descriptor layout";
        return false;
    }
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = 1,
                                                 .pSetLayouts = &own.set_layout,
                                                 .pushConstantRangeCount = 1,
                                                 .pPushConstantRanges = &constants};
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
        own.layout = VK_NULL_HANDLE;
        *error = "the pipeline layout";
        return false;
    }
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, kUiSource);
    if (!compiled.ok) {
        *error = "the UI shader doesn't compile: " + compiled.log;
        return false;
    }
    const VkShaderModuleCreateInfo module_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                               .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &module_info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
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
                                           .layout = own.layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &own.pass);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        own.pass = VK_NULL_HANDLE;
        *error = "vkCreateComputePipelines failed";
        return false;
    }
    return true;
}

// The UI image from the frame's back buffer and its HUD-less colour.
bool record_ui(Backend& state, VkCommandBuffer commands, Extent shown, std::string* error)
{
    FrameGenState& own = framegen();
    if (!ensure_ui_pass(state, error) || !ensure_picture(state, &own.frame, shown) || !ensure_picture(state, &own.ui, shown)) {
        return false;
    }
    copy_back_buffer(commands, &state.back_buffer->image, &own.frame);
    Memory::transition(commands, &own.ui, VK_IMAGE_LAYOUT_GENERAL);
    full_barrier(commands);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.pass);
    const std::array<VkDescriptorImageInfo, 3> images{{
        {.sampler = VK_NULL_HANDLE, .imageView = own.frame.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.hudless.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.ui.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
    }};
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (uint32_t binding = 0; binding < writes.size(); ++binding) {
        VkWriteDescriptorSet& write = core::at(writes, binding);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &core::at(images, binding);
    }
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, static_cast<uint32_t>(writes.size()),
                              writes.data());
    const UiConstants constants{.size = {static_cast<int32_t>(shown.width), static_cast<int32_t>(shown.height)}};
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commands, (shown.width + kGroupSide - 1) / kGroupSide, (shown.height + kGroupSide - 1) / kGroupSide, 1);
    full_barrier(commands);
    state.bound = BoundState{}; // (a compute pipeline and push descriptors of its own)
    return true;
}

// The HUD-less colour: the world finished, before the 2D (last of the listeners at world_drawn).
class HudlessListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kFrameGeneration; }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override {}
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        FrameGenState& own = framegen();
        if (!state.device_made || !state.draws_made || own.provider == nullptr || own.inputs_serial != own.serial ||
            state.back_buffer == nullptr || core::at(state.targets, 0) != state.back_buffer) {
            return;
        }
        Image& back = state.back_buffer->image;
        const VkExtent2D size = state.context.extent();
        if (back.samples != VK_SAMPLE_COUNT_1_BIT || size.width == 0 || size.height == 0) {
            if (first_report(state, kReportTarget)) {
                get_log("vulkan: frame generation: the back buffer is multisampled or there's no swapchain - no frames made");
            }
            return;
        }
        const Extent shown{.width = size.width, .height = size.height};
        if (!ensure_picture(state, &own.hudless, shown)) {
            return;
        }
        VkCommandBuffer commands = outside_pass(state);
        copy_back_buffer(commands, &back, &own.hudless);
        full_barrier(commands);
        own.hudless.used = state.frames.serial();
        own.hudless_serial = own.serial;
        own.menu = view.menu;
    }
};

HudlessListener& hudless_listener()
{
    static HudlessListener instance;
    return instance;
}

// The provider for the settings (DLSS-G first where it may run, else FSR), made before the first swapchain.
void choose_provider(Backend& state)
{
    FrameGenState& own = framegen();
    own.tried = true;
    const FrameGenSettings& settings = own.settings;
    std::string why;
    if (settings.source != FrameGenSource::kFsr) {
        own.provider = make_dlssg_provider(state, &why);
        if (!own.provider) {
            get_log("vulkan: frame generation: DLSS Frame Generation can't run here (%s)%s", why.c_str(),
                    settings.source == FrameGenSource::kDlss ? " - no frame generation" : " - FSR instead");
        }
    }
    if (!own.provider && settings.source != FrameGenSource::kDlss) {
        why.clear();
        own.provider = make_fsr_framegen_provider(state, &why);
        if (!own.provider) {
            get_log("vulkan: frame generation: FSR frame generation can't run here (%s) - no frame generation", why.c_str());
        }
    }
    if (!own.provider) {
        return;
    }
    const uint32_t wanted = frame_generation_multiplier(settings.mode);
    own.multiplier = std::clamp(wanted, 2U, own.provider->most_multiplier());
    get_log("vulkan: frame generation: %s - %s, %ux (%ux asked)", own.provider->name(), own.provider->describe().c_str(),
            own.multiplier, wanted);
    if (state.settings.present_mode == 0) {
        get_log("vulkan: frame generation: V-sync (present_mode=vsync) holds the frames shown to the display's rate - "
                "mailbox or immediate (a variable-refresh display) let the made frames through");
    }
    rt::add_view_listener(&hudless_listener());
}

} // namespace

// --- settings ------------------------------------------------------------------------------------------------------

const FrameGenSettings& frame_generation_settings_locked() { return framegen().settings; }

void set_frame_generation_settings(const FrameGenSettings& settings)
{
    const std::scoped_lock lock(backend().mutex);
    framegen().settings = settings;
}

FrameGenSettings frame_generation_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return framegen().settings;
}

uint32_t frame_generation_multiplier(FrameGeneration mode)
{
    switch (mode) {
    case FrameGeneration::kOff:
        return 1;
    case FrameGeneration::k2x:
        return 2;
    case FrameGeneration::k3x:
        return 3;
    case FrameGeneration::k4x:
        return 4;
    }
    return 1;
}

bool parse_frame_generation(const std::string& text, FrameGeneration* mode)
{
    if (text.empty() || text == "off" || text == "0" || text == "1") {
        *mode = FrameGeneration::kOff;
    } else if (text == "2x" || text == "2" || text == "on") {
        *mode = FrameGeneration::k2x;
    } else if (text == "3x" || text == "3") {
        *mode = FrameGeneration::k3x;
    } else if (text == "4x" || text == "4") {
        *mode = FrameGeneration::k4x;
    } else {
        return false;
    }
    return true;
}

bool parse_frame_generation_source(const std::string& text, FrameGenSource* source)
{
    if (text.empty() || text == "auto") {
        *source = FrameGenSource::kAuto;
    } else if (text == "dlss" || text == "dlssg" || text == "dlss-g") {
        *source = FrameGenSource::kDlss;
    } else if (text == "fsr" || text == "fsr3") {
        *source = FrameGenSource::kFsr;
    } else {
        return false;
    }
    return true;
}

bool parse_reflex(const std::string& text, Reflex* reflex)
{
    if (text.empty() || text == "off" || text == "0") {
        *reflex = Reflex::kOff;
    } else if (text == "on" || text == "1") {
        *reflex = Reflex::kOn;
    } else if (text == "boost" || text == "2") {
        *reflex = Reflex::kBoost;
    } else {
        return false;
    }
    return true;
}

FrameGenCounts frame_generation_counts()
{
    const std::scoped_lock lock(backend().mutex);
    const FrameGenState& own = framegen();
    FrameGenCounts counts = own.counts;
    counts.provider = own.provider ? own.provider->name() : nullptr;
    counts.multiplier = own.provider ? own.multiplier : 1;
    counts.features_made = own.made_before + (own.provider ? own.provider->made() : 0);
    counts.features_released = own.released_before + (own.provider ? own.provider->released() : 0);
    counts.reflex = reflex_running();
    return counts;
}

// --- the device ----------------------------------------------------------------------------------------------------

bool frame_generation_wants_dlss()
{
    const FrameGenSettings& settings = framegen().settings;
    return settings.mode != FrameGeneration::kOff && settings.source != FrameGenSource::kFsr;
}

bool frame_generation_wants_context()
{
    const FrameGenSettings& settings = framegen().settings;
    return settings.mode != FrameGeneration::kOff || settings.reflex != Reflex::kOff;
}

void frame_generation_device_wants(VkPhysicalDevice /*physical*/, ContextAdditions::DeviceWants* wants)
{
    const FrameGenSettings& settings = framegen().settings;
    if (settings.mode != FrameGeneration::kOff && settings.source != FrameGenSource::kDlss && fsr_swapchain_owner() != nullptr) {
        wants->present_queues += kPresentQueues;
    }
}

void frame_generation_device_made(const ContextAdditions::DeviceMade& made)
{
    if (framegen().settings.mode != FrameGeneration::kOff) {
        fsr_framegen_queues(made);
    }
}

void prepare_frame_generation(Backend& state)
{
    FrameGenState& own = framegen();
    if (own.settings.mode == FrameGeneration::kOff || own.tried) {
        return;
    }
    choose_provider(state);
}

void frame_generation_marker(Backend& /*state*/, GpuContext::LatencyMarker marker)
{
    if (reflex_running()) {
        reflex_marker(marker);
    }
}

bool frame_generation_cut(Backend& /*state*/)
{
    FrameGenState& own = framegen();
    if (own.provider == nullptr) {
        return false;
    }
    const uint64_t cuts = frame_blend::cuts();
    if (cuts != own.cuts_seen) {
        own.cuts_seen = cuts;
        own.cut = true;
    }
    return own.cut;
}

void framegen_inputs(Backend& state, const UpscaleFrame& frame, const rt::ViewDrawn& view)
{
    FrameGenState& own = framegen();
    if (own.provider == nullptr || own.failed) {
        return;
    }
    FrameGenFrame inputs;
    inputs.on = !view.menu;
    inputs.reset = own.cut || !own.was_on;
    inputs.commands = frame.commands;
    inputs.inputs = &frame;
    inputs.multiplier = own.multiplier;
    std::string error;
    if (!own.provider->inputs(state, inputs, &error)) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: frame generation: %s failed (%s) - no frames made", own.provider->name(), error.c_str());
        }
        return;
    }
    own.inputs_serial = own.serial;
}

void frame_generation_before_present(Backend& state)
{
    FrameGenState& own = framegen();
    if (own.provider == nullptr || state.back_buffer == nullptr) {
        return;
    }
    const VkExtent2D size = state.context.extent();
    const Extent shown{.width = size.width, .height = size.height};
    bool on = !own.failed && own.inputs_serial == own.serial && own.hudless_serial == own.serial && !own.menu &&
              !state.context.swapchain_stale() && shown.width != 0 && own.hudless.extent.width == shown.width &&
              own.hudless.extent.height == shown.height;
    if (on && gamma_ramp_applies(state)) {
        on = false; // (the frame shown goes through the full-screen gamma ramp; the HUD-less colour doesn't)
        if (first_report(state, kReportGamma)) {
            get_log("vulkan: frame generation: the full-screen gamma ramp is set - no frames made while it is");
        }
    }
    FrameGenFrame frame;
    frame.on = on;
    frame.reset = on && (own.cut || !own.was_on);
    frame.commands = outside_pass(state);
    frame.shown = shown;
    frame.multiplier = own.multiplier;
    std::string error;
    if (on) {
        frame.hudless = &own.hudless;
        if (own.provider->wants_ui()) {
            if (!record_ui(state, frame.commands, shown, &error)) {
                own.failed = true;
                frame.on = false;
                if (first_report(state, kReportFailed)) {
                    get_log("vulkan: frame generation: the UI image couldn't be made (%s) - no frames made", error.c_str());
                }
            } else {
                frame.ui = &own.ui;
                const uint64_t serial = state.frames.serial();
                own.frame.used = serial;
                own.ui.used = serial;
                own.hudless.used = serial;
            }
        }
    }
    if (!own.provider->before_present(state, frame, &error)) {
        own.failed = true;
        frame.on = false;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: frame generation: %s failed (%s) - no frames made", own.provider->name(), error.c_str());
        }
    }
    if (frame.on) {
        ++own.counts.frames_on;
        own.counts.resets += frame.reset ? 1 : 0;
        own.cut = false;
        if (first_report(state, kReportFirst)) {
            get_log("vulkan: frame generation: the first frame made by %s (%ux, %ux%u)", own.provider->name(), own.multiplier,
                    shown.width, shown.height);
        }
    } else {
        ++own.counts.frames_off;
    }
    own.was_on = frame.on;
}

void frame_generation_present_marker(Backend& /*state*/, bool start)
{
    if (reflex_running()) {
        reflex_present_marker(start);
    }
}

void frame_generation_after_present(Backend& state)
{
    FrameGenState& own = framegen();
    ++own.serial;
    if (own.provider == nullptr && !reflex_running()) {
        return;
    }
    streamline_frame_done();
    if (own.provider == nullptr) {
        return;
    }
    own.counts.frames_shown += own.provider->after_present(state);
    if (own.counts.frames_on > 0 && own.counts.frames_on - own.logged_at >= kLogEvery) {
        own.logged_at = own.counts.frames_on;
        get_log("vulkan: frame generation: %llu frames shown by %s for %llu drawn with it on (%llu with it off)",
                static_cast<unsigned long long>(own.counts.frames_shown), own.provider->name(),
                static_cast<unsigned long long>(own.counts.frames_on), static_cast<unsigned long long>(own.counts.frames_off));
    }
}

void frame_generation_other_swapchain(bool making)
{
    if (framegen().provider != nullptr) {
        dlssg_other_swapchain(making);
    }
}

uint64_t frame_generation_frames_shown()
{
    const FrameGenState& own = framegen();
    return own.provider != nullptr ? own.counts.frames_shown : 0;
}

void release_frame_generation(Backend& state)
{
    FrameGenState& own = framegen();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        if (own.pass != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, own.pass, nullptr);
        }
        if (own.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
    }
    own.pass = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    if (state.memory.ready()) {
        for (Image* image : {&own.hudless, &own.frame, &own.ui}) {
            state.memory.destroy_image(image);
        }
    }
    own.failed = false;
    own.was_on = false;
}

void shutdown_frame_generation(Backend& state)
{
    FrameGenState& own = framegen();
    rt::remove_view_listener(&hudless_listener());
    if (own.provider) {
        own.provider->release(state);
        own.made_before += own.provider->made();
        own.released_before += own.provider->released();
        own.provider.reset();
    }
    own.tried = false;
    own.multiplier = 1;
}

} // namespace render::vulkan

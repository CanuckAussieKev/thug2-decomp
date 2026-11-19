// NOT ORIGINAL - the device's pipelines counted (vulkan_pipeline_count.h).
#include "gpu_vulkan/vulkan_pipeline_count.h"

#include <atomic>
#include <cstdint>
#include <volk.h>

namespace render::vulkan {

namespace {

// NOT ORIGINAL grouping - the count and the device's own creation calls (volk's, as volkLoadDevice set them).
struct PipelineCount {
    std::atomic<uint64_t> made{0};
    PFN_vkCreateGraphicsPipelines graphics = nullptr;
    PFN_vkCreateComputePipelines compute = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR ray_tracing = nullptr;
};

PipelineCount& pipeline_count()
{
    static PipelineCount count;
    return count;
}

VKAPI_ATTR VkResult VKAPI_CALL counted_graphics(VkDevice device, VkPipelineCache cache, uint32_t count,
                                                const VkGraphicsPipelineCreateInfo* infos,
                                                const VkAllocationCallbacks* allocator, VkPipeline* pipelines)
{
    pipeline_count().made.fetch_add(count, std::memory_order_relaxed);
    return pipeline_count().graphics(device, cache, count, infos, allocator, pipelines);
}

VKAPI_ATTR VkResult VKAPI_CALL counted_compute(VkDevice device, VkPipelineCache cache, uint32_t count,
                                               const VkComputePipelineCreateInfo* infos,
                                               const VkAllocationCallbacks* allocator, VkPipeline* pipelines)
{
    pipeline_count().made.fetch_add(count, std::memory_order_relaxed);
    return pipeline_count().compute(device, cache, count, infos, allocator, pipelines);
}

VKAPI_ATTR VkResult VKAPI_CALL counted_ray_tracing(VkDevice device, VkDeferredOperationKHR deferred,
                                                   VkPipelineCache cache, uint32_t count,
                                                   const VkRayTracingPipelineCreateInfoKHR* infos,
                                                   const VkAllocationCallbacks* allocator, VkPipeline* pipelines)
{
    pipeline_count().made.fetch_add(count, std::memory_order_relaxed);
    return pipeline_count().ray_tracing(device, deferred, cache, count, infos, allocator, pipelines);
}

} // namespace

void count_pipeline_creation()
{
    PipelineCount& count = pipeline_count();
    // (volkLoadDevice has just set them: the device's own - never one of ours, which it would loop through)
    if (vkCreateGraphicsPipelines != nullptr && vkCreateGraphicsPipelines != &counted_graphics) {
        count.graphics = vkCreateGraphicsPipelines;
        vkCreateGraphicsPipelines = &counted_graphics;
    }
    if (vkCreateComputePipelines != nullptr && vkCreateComputePipelines != &counted_compute) {
        count.compute = vkCreateComputePipelines;
        vkCreateComputePipelines = &counted_compute;
    }
    if (vkCreateRayTracingPipelinesKHR != nullptr && vkCreateRayTracingPipelinesKHR != &counted_ray_tracing) {
        count.ray_tracing = vkCreateRayTracingPipelinesKHR;
        vkCreateRayTracingPipelinesKHR = &counted_ray_tracing;
    }
}

uint64_t pipelines_made() { return pipeline_count().made.load(std::memory_order_relaxed); }

} // namespace render::vulkan

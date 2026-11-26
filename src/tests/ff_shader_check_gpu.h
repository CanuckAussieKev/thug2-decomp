#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gpu_vulkan/ff_key.h"

// NOT ORIGINAL - the fixed-function shader check's GPU part (ff_shader_check_gpu.cpp): every compiled module handed to
// a Vulkan driver (vkCreateShaderModule), and a graphics pipeline made for each sampled pair of shaders - the vertex
// format's attributes, the uniform blocks' and textures' descriptor layout of gpu_vulkan/ff_uniforms.h, a colour and
// depth render pass - so the driver compiles each pair and checks that the two halves' interfaces agree.
namespace ff_check {

// One pair of shaders to make a pipeline of: its key, and the fragment module's place in the batch given to run_batch.
struct BatchPair {
    const render::vulkan::ShaderKey* key = nullptr;
    std::size_t fragment = 0;
};

// The GPU part in batches, so what it holds stays bounded however many keys there are: open it, hand it the vertex
// modules (they stay - a pair of the sweep needs one of ~8k), then each batch of fragment shaders with the pairs that
// use them - the batch's modules and pipelines are made, checked and destroyed before the next batch. finish() prints
// the totals and says whether every module and pipeline was accepted.
class GpuSession {
public:
    ~GpuSession();
    GpuSession(const GpuSession&) = delete;
    GpuSession& operator=(const GpuSession&) = delete;

    // Null (with a note, ok true) when there is no Vulkan; null with ok false when the context's objects failed.
    // pipeline_limit: at most this many pipelines over all batches (-1: every pair).
    static std::unique_ptr<GpuSession> open(int device_index, int pipeline_limit, bool& ok);

    // The vertex shaders' modules, in the order of `keys` (a null SPIR-V: no module - a failure counted elsewhere).
    void add_vertex_modules(const std::vector<render::vulkan::VertexKey>& keys,
                            const std::vector<const std::vector<uint32_t>*>& spirv);
    // A batch of fragment shaders (a null SPIR-V: skipped) and the pairs that use them.
    void run_batch(const std::vector<const std::vector<uint32_t>*>& fragment_spirv, const std::vector<BatchPair>& pairs);
    // Prints the totals; false when a module or a pipeline was refused or the validation layer reported an error.
    bool finish();

private:
    GpuSession();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ff_check

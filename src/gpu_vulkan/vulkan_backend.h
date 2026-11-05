#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_samplers.h"

struct SDL_Window;

// NOT ORIGINAL - what the Vulkan Gpu and GpuContext share (vulkan_gpu.cpp, vulkan_gpu_context.cpp): the context, the
// memory, the objects made, the device's own surfaces and bindings, and the lock. Seen by the Vulkan Gpu's files and its
// check (src/tests/vulkan_gpu_check.cpp) only.
//
// Threads: the loading screen's timer thread and the loading console's draw through the Gpu while the main thread
// loads (Direct3D 9's device is made multithreaded for them). Every Gpu and GpuContext operation here takes `mutex`
// for its whole length, so their calls run one at a time. The state block (engine/gfx/gpu.h) is recorded by Gpu's
// setters before the operation takes the lock, as with Direct3D 9: it is a record, read only by the Gpu itself at draw
// time (V7), when the lock is held.
namespace render::vulkan {

// The objects of one kind that the Gpu made, owned until released.
template <typename T>
class Registry {
public:
    T* add(std::unique_ptr<T> object)
    {
        T* const made = object.get();
        objects_.emplace(made, std::move(object));
        return made;
    }
    void erase(const T* object) { objects_.erase(object); }
    [[nodiscard]] bool contains(const T* object) const { return objects_.contains(object); }
    [[nodiscard]] std::size_t size() const { return objects_.size(); }
    // Each object left (shutdown).
    template <typename Visit>
    void for_each(Visit visit)
    {
        for (auto& [address, object] : objects_) {
            visit(object.get());
        }
    }
    void clear() { objects_.clear(); }

private:
    std::unordered_map<const T*, std::unique_ptr<T>> objects_;
};

// The vertex streams and render targets the device has (Direct3D 9's limits the game stays within).
inline constexpr std::size_t kStreamCount = 16;
inline constexpr std::size_t kRenderTargetCount = 4;
// Occlusion query slots per pool.
inline constexpr uint32_t kQueriesPerPool = 64;

// A vertex stream's source (set_stream_source).
struct StreamSource {
    Buffer* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t stride = 0;
};

struct Backend {
    std::mutex mutex;

    // Before the context opens: the options and the window (the game's SDL window when none is given).
    ContextOptions options;
    SDL_Window* window = nullptr;

    Context context;
    bool opened = false;      // instance, surface and device made (GpuContext::create_instance)
    bool device_made = false; // the swapchain, the memory and the device's surfaces made (create_device)
    bool ready = false;       // GpuContext::mark_ready
    GpuSettings settings;
    GpuAdapterInfo adapter;
    FormatSupport formats;
    SamplerLimits sampler_limits;

    Memory memory;
    SamplerCache samplers;
    UploadRing ring;

    Registry<Texture> textures;
    Registry<Surface> surfaces;
    Registry<Buffer> buffers;
    Registry<PixelShader> shaders;
    Registry<Query> queries;
    std::vector<VkQueryPool> query_pools;
    std::vector<Query*> query_slots; // per slot (pool * kQueriesPerPool + slot): its query, or null when free

    // The device's own surfaces and what is bound: the targets drawn into, the vertex streams, the index buffer. The
    // device holds a reference to each, as Direct3D 9's does.
    Surface* back_buffer = nullptr;
    Surface* auto_depth = nullptr;
    std::array<Surface*, kRenderTargetCount> targets{};
    Surface* depth = nullptr;
    std::array<StreamSource, kStreamCount> streams{};
    Buffer* indices = nullptr;
    bool in_scene = false;
    uint64_t frame = 0;           // frames presented
    uint64_t completed_frame = 0; // the last frame the device has finished (V7's fences; all of them until then)
    GpuGammaRamp gamma{};
};

// The one backend (made on first use, never destroyed).
Backend& backend();

// The context's options and the window, before GpuContext::create_instance (the checks pick a device this way; the game's
// window and the default options otherwise).
void set_context_options(const ContextOptions& options, SDL_Window* window);

// The device's own objects made at create_device (the memory, the ring, the samplers, the back buffer and the depth
// surface the settings name) and everything released at shutdown (vulkan_gpu.cpp). The caller holds the lock.
long create_device_objects(Backend& state);
void release_device_objects(Backend& state);

// The checks' views (src/tests/vulkan_gpu_check.cpp): an object's image, a level read back in the image's format, a
// buffer's device memory read back.
const Texture* texture_of(GpuTexture* texture);
bool read_texture_level(GpuTexture* texture, uint32_t level, std::vector<uint8_t>* texels);
bool read_surface(GpuSurface* surface, std::vector<uint8_t>* texels);
bool read_vertex_buffer(GpuVertexBuffer* buffer, std::vector<uint8_t>* bytes);
bool read_index_buffer(GpuIndexBuffer* buffer, std::vector<uint8_t>* bytes);
// The sampler a stage's states make now (from the Gpu's state block).
VkSampler stage_sampler(uint32_t stage);

} // namespace render::vulkan

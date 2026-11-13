#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_frames.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_pipelines.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_materials.h"
#include "gpu_vulkan/vulkan_samplers.h"

struct SDL_Window;

// NOT ORIGINAL - what the Vulkan Gpu and GpuContext share (vulkan_gpu.cpp, vulkan_draws.cpp, vulkan_gpu_context.cpp):
// the context, the memory, the frames and pipelines, the objects made, the device's own surfaces and bindings, the
// open pass, and the lock. Seen by the Vulkan Gpu's files and its check (src/tests/vulkan_gpu_check.cpp) only.
//
// Threads: the loading screen's timer thread and the loading console's draw through the Gpu while the main thread
// loads (Direct3D 9's device is made multithreaded for them). Every Gpu and GpuContext operation here takes `mutex`
// for its whole length, so their calls run one at a time. The state block (engine/gfx/gpu.h) is recorded by Gpu's
// setters before the operation takes the lock, as with Direct3D 9: it is a record, read only by the Gpu itself at draw
// time, when the lock is held.
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
// The largest minUniformBufferOffsetAlignment Vulkan allows (the device's own is read at creation).
inline constexpr VkDeviceSize kUniformAlignmentAtMost = 256;
// Occlusion query slots per pool.
inline constexpr uint32_t kQueriesPerPool = 64;

// A vertex stream's source (set_stream_source).
struct StreamSource {
    Buffer* buffer = nullptr;
    uint32_t offset = 0;
    uint32_t stride = 0;
};

// The rendering pass open in the stream (docs/design/vulkan.md, V5d's inference): opened at the first draw or clear
// into the device's targets, ended by a draw or clear into others, a copy, a transfer that must follow what was drawn,
// a read-back or the present.
struct OpenPass {
    bool open = false;
    Surface* colour = nullptr;
    Surface* depth = nullptr;
    Image* colour_image = nullptr;
    Image* depth_image = nullptr; // (null: drawn without depth)
    bool motion = false;          // (the upscaler's) the motion attachment is the second colour attachment
    VkExtent2D area{};
    std::vector<Query*> queries; // begun inside it, not yet ended
};

// What the stream command buffer has bound and set, so a draw sets only what changed (forgotten when a new one begins).
struct BoundState {
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool dynamic_set = false; // the states below hold what the stream has
    VkViewport viewport{};
    VkRect2D scissor{};
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
    bool depth_test = false;
    bool depth_write = false;
    VkCompareOp depth_compare = VK_COMPARE_OP_MAX_ENUM;
    std::array<float, 2> depth_bias{};
    Float4 blend_constants{};
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceSize vertex_offset = 0;
    VkDeviceSize vertex_stride = 0;
    VkBuffer previous_buffer = VK_NULL_HANDLE; // (the upscaler's motion: binding 1)
    VkDeviceSize previous_offset = 0;
    VkDeviceSize previous_stride = 0;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceSize index_offset = 0;
    VkIndexType index_type = VK_INDEX_TYPE_MAX_ENUM;
    // the uniform blocks last copied into the ring, and where
    bool uniforms_kept = false;
    VertexUniforms vertex_uniforms{};
    FragmentUniforms fragment_uniforms{};
    VkDescriptorBufferInfo vertex_block{};
    VkDescriptorBufferInfo fragment_block{};
    // the descriptors last pushed
    bool descriptors_set = false;
    std::array<VkDescriptorImageInfo, kStageCount> images{};
    VkDescriptorBufferInfo pushed_vertex_block{};
    VkDescriptorBufferInfo pushed_fragment_block{};
    // (NOT ORIGINAL - rt_pbr, vulkan_surface.h) the per-pixel lighting's block last copied, and what was last pushed
    SurfaceUniforms surface_uniforms{};
    VkDescriptorBufferInfo surface_block{};
    VkDescriptorBufferInfo pushed_surface_block{};
    VkDescriptorImageInfo pushed_surface_map{};
    bool surface_kept = false;
    bool surface_pushed = false;
};

// Counts and times for the log (vulkan_draws.cpp writes a line every so many frames).
struct DrawStats {
    uint64_t draws = 0;
    uint64_t passes = 0;
    uint64_t uploads = 0;
    uint64_t upload_bytes = 0;
    double upload_seconds = 0.0;
    uint64_t finishes = 0; // the frame's work waited for before its end (read-backs, a full staging ring)
    double frame_seconds = 0.0;
    double wait_seconds = 0.0; // of it in the present: the frame in flight's fence, the swapchain's image
    uint64_t frames = 0;
    double last_present = 0.0; // (seconds, a steady clock)
    double last_log = 0.0;
};

// A window of the UI's own (Dear ImGui's platform windows: a panel of the level editor dragged out of the game's window
// - vulkan_ui_windows.cpp): its surface and swapchain, drawn into by the stream (record_ui_window_draws) and presented
// after the frame's submission (present_ui_windows), its acquire waited for and its present's semaphore signalled by
// whichever submission comes next (Frames::add_wait / add_signal).
struct UiWindow {
    SDL_Window* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> presented;                   // per swapchain image
    std::array<VkSemaphore, Frames::kSlots> acquired{};   // per frame slot
    bool stale = true;        // made again before it is next drawn (resized, out of date)
    bool pending = false;     // drawn this frame, its image presented after the submission
    uint32_t image = 0;       // (while pending)
    bool failed_logged = false;
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
    UploadRing ring;    // the draws' vertices, indices and uniform blocks
    UploadRing staging; // the uploads' bytes
    Frames frames;
    Pipelines pipelines;
    bool draws_made = false;
    std::string cache_folder; // where the shader and pipeline caches are kept (empty: not kept)
    VkDeviceSize uniform_alignment = kUniformAlignmentAtMost;
    Image blank;          // a 1x1 opaque white texture for the stages with none
    Image present_source; // the multisampled back buffer resolved, for the present's copy
    Image gamma_source;   // the frame copied for the gamma pass (sampled), and the frame through the ramp
    Image gamma_target;
    OpenPass pass;
    BoundState bound;
    DrawStats stats;
    std::unordered_set<uint64_t> reported; // what was logged once (keys refused, shaders not recognised)

    Registry<Texture> textures;
    Registry<Surface> surfaces;
    Registry<Buffer> buffers;
    Registry<PixelShader> shaders;
    Registry<Query> queries;
    std::vector<VkQueryPool> query_pools;
    std::vector<Query*> query_slots;       // per slot (pool * kQueriesPerPool + slot): its query, or null when free
    std::vector<uint64_t> query_slot_used; // per slot: the submission that last used it (a reset is recorded before)

    // The device's own surfaces and what is bound: the targets drawn into, the vertex streams, the index buffer. The
    // device holds a reference to each, as Direct3D 9's does.
    Surface* back_buffer = nullptr;
    Surface* auto_depth = nullptr;
    std::array<Surface*, kRenderTargetCount> targets{};
    Surface* depth = nullptr;
    std::array<StreamSource, kStreamCount> streams{};
    Buffer* indices = nullptr;
    bool in_scene = false;
    uint64_t frame = 0; // frames presented
    VkExtent2D shown_extent{}; // the swapchain's size when last made again (logged when it changes)
    GpuGammaRamp gamma{};
    std::vector<std::unique_ptr<UiWindow>> ui_windows; // the UI's own windows (vulkan_ui_windows.cpp)
    MaterialTable materials; // NOT ORIGINAL - ray tracing's material table (vulkan_rt_materials.h), when it is enabled
    MotionVectors motion;    // NOT ORIGINAL - the upscaler's motion vectors (vulkan_motion.h)
};

// The one backend (made on first use, never destroyed).
Backend& backend();

// The context's options and the window, before GpuContext::create_instance (the checks pick a device this way; the game's
// window and the default options otherwise).
void set_context_options(const ContextOptions& options, SDL_Window* window);

// The device's own objects made at create_device (the memory, the rings, the frames, the pipelines, the samplers, the back
// buffer and the depth surface the settings name) and everything released at shutdown (vulkan_gpu.cpp). The caller holds
// the lock.
long create_device_objects(Backend& state);
void release_device_objects(Backend& state);
// What Direct3D 9's Reset does to the device's bindings (the PC layer resets the state block after it): the back buffer
// and its depth surface made again when the settings' size or samples changed, render target 0 the back buffer and the
// others none, the depth surface the device's own, no vertex streams or indices. The caller holds the lock.
bool reset_device_bindings(Backend& state);

// --- the draws, the frame, the transfers (vulkan_draws.cpp); the caller holds the lock ------------------------------

// The frames, the staging ring, the pipelines and the blank texture (after the memory); and all of it released.
bool create_draw_objects(Backend& state);
void release_draw_objects(Backend& state);
// The command buffer a transfer goes into: the uploads (before the stream) when nothing in this submission's stream
// used what it writes (`touched`: the last submission that did), else the stream outside a pass, the buffers' reads
// before it waited for.
struct TransferTarget {
    VkCommandBuffer commands = nullptr;
    bool in_stream = false;
};
TransferTarget transfer_commands(Backend& state, uint64_t touched);
// After a transfer of `staged` bytes: the stream's reads of buffers wait for it (when it went into the stream); the
// frame's work is finished when the staging ring has grown past its limit (loading).
void transfer_done(Backend& state, const TransferTarget& target, VkDeviceSize staged);
// Before a copy into what an earlier copy of the same command buffer wrote: that copy's writes waited for (two copies
// into one buffer are otherwise unordered - a static buffer unlocked twice before its uploads are submitted).
void copy_after_copy(VkCommandBuffer commands);
// An image or buffer released: destroyed once the submissions that may use it have finished.
void retire_image(Backend& state, Image* image);
void retire_buffer(Backend& state, DeviceBuffer* buffer);
// Everything recorded submitted and waited for (before a read-back or a reset).
bool finish_work(Backend& state);
// The pass ended, the stream's command buffer returned (for a copy or a blit).
VkCommandBuffer outside_pass(Backend& state);
// An image moved to its first layout before anything uses it (a new image).
void prepare_image(Backend& state, Image* image);

// A draw: from the bound streams or from the ring, with or without indices.
struct DrawCall {
    GpuPrimitive primitive = GpuPrimitive::kTriangleList;
    uint32_t vertices = 0; // what vkCmdDraw / vkCmdDrawIndexed is given
    bool indexed = false;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceSize vertex_offset = 0;
    uint32_t stride = 0;
    uint32_t first_vertex = 0;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceSize index_offset = 0;
    VkIndexType index_type = VK_INDEX_TYPE_UINT16;
    uint32_t first_index = 0;
    int32_t base_vertex = 0;
};
long record_draw(Backend& state, const GpuStateBlock& block, const DrawCall& call);
// A clear's flags (D3DCLEAR_*), colour (A8R8G8B8) and depth.
struct ClearRequest {
    uint32_t flags = 0;
    uint32_t colour = 0;
    float z = 1.0F;
};
long record_clear(Backend& state, const GpuStateBlock& block, std::span<const GpuRect> rects, const ClearRequest& request);
long record_present(Backend& state);
// The gamma ramp (Backend::gamma) as Direct3D 9 applies it: full screen only, and only when it changes something.
bool gamma_ramp_applies(const Backend& state);
// The frame (single-sampled) through the ramp into gamma_target, recorded in the stream outside a pass; null when the
// ramp changes nothing or the pass can't be made.
Image* record_gamma_pass(Backend& state, Image* frame);
// Occlusion queries: begun in the pass the draws go into (opened for it), ended there (or when the pass ends first).
void record_begin_query(Backend& state, const GpuStateBlock& block, Query* query);
void record_end_query(Backend& state, Query* query);

// The UI's draws (vulkan_ui_backend.cpp: Dear ImGui's draw data). One draw: `indices` indices from `first_index` (into
// the batch's), their vertices counted from `base_vertex`, the texture (null, or released: the blank one), the clip
// rectangle in the target's pixels.
struct UiDraw {
    Texture* texture = nullptr;
    GpuRect clip{};
    uint32_t first_index = 0;
    uint32_t indices = 0;
    int32_t base_vertex = 0;
};
// A frame's UI: its vertices (Pipelines::kUiVertexBytes each: ImGui's), 16-bit indices, the rectangle it covers (left,
// top, width, height in the target's pixels: ImGui's display) and the draws.
struct UiBatch {
    std::span<const uint8_t> vertices;
    std::span<const uint16_t> indices;
    std::array<float, 4> display{};
    std::span<const UiDraw> draws;
};
// The batch drawn into the device's targets (render target 0) over what is there, in the stream: in the pass on them
// (opened, after the pass on others ended, as a draw does), its textures made ready to be sampled first; the stream's
// bound state forgotten after (Pipelines::ui_pass's is its own).
long record_ui_draws(Backend& state, const UiBatch& batch);

// --- the UI's own windows (vulkan_ui_windows.cpp; the caller holds the lock) -------------------------------------

// A window for `window` (made with SDL_WINDOW_VULKAN): its surface now, its swapchain when first drawn; null (logged)
// when the surface can't be made or the queue can't present to it.
UiWindow* create_ui_window(Backend& state, SDL_Window* window);
// The window's swapchain and surface released (the device idle first); `window` forgotten.
void destroy_ui_window(Backend& state, UiWindow* window);
void destroy_ui_windows(Backend& state);
// The next image of the window's swapchain acquired (the swapchain made again first when it is stale): false when the
// window can't be drawn this frame (minimised, out of date, a failure).
bool acquire_ui_window(Backend& state, UiWindow* window);
// The batch drawn into the window's acquired image (cleared first), in the stream, and the image moved to be presented;
// the submission that takes it waits for the acquire and signals the present (vulkan_draws.cpp).
long record_ui_window_draws(Backend& state, UiWindow* window, const UiBatch& batch);
// After the frame's submission: every window drawn this frame presented.
void present_ui_windows(Backend& state);

// The checks' views (src/tests/vulkan_gpu_check.cpp): an object's image, a level read back in the image's format, a
// buffer's device memory read back.
const Texture* texture_of(GpuTexture* texture);
bool read_texture_level(GpuTexture* texture, uint32_t level, std::vector<uint8_t>* texels);
bool read_surface(GpuSurface* surface, std::vector<uint8_t>* texels);
// A single-sampled render target put through the gamma pass now (whether or not the window is full screen) and the
// pass's result read back (the pass's images released after); false when the ramp changes nothing.
bool read_gamma_pass(GpuSurface* surface, std::vector<uint8_t>* texels);
bool read_vertex_buffer(GpuVertexBuffer* buffer, std::vector<uint8_t>* bytes);
bool read_index_buffer(GpuIndexBuffer* buffer, std::vector<uint8_t>* bytes);
// The sampler a stage's states make now (from the Gpu's state block).
VkSampler stage_sampler(uint32_t stage);

} // namespace render::vulkan

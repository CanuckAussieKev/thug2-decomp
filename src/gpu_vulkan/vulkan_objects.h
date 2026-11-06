#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_pixel_program.h"

// NOT ORIGINAL - the Vulkan Gpu's objects behind the engine's handles (engine/gfx/gpu_handles.h): each handle is the
// address of one of these, as a Direct3D 9 handle is its COM object's. They count references the way Direct3D 9's
// objects do (made with one; the Get* calls that hand one out add one; the release that takes the last destroys it), so
// the PC layer's calls keep their meaning. Included only by the Vulkan Gpu's own files.
namespace render::vulkan {

// A level locked (Gpu::lock_texture / lock_surface): which, and whether the caller only reads it.
struct LockState {
    bool locked = false;
    bool read_only = false;
};

struct Surface;

// A texture (GpuTexture): its image, and per level the bytes in Direct3D 9's layout - the managed pool's system-memory
// copy, which a lock hands out and an unlock uploads (converted where the image's format differs).
struct Texture {
    uint32_t refs = 1;
    GpuTextureDesc desc;
    FormatPlan plan;
    Image image;
    std::vector<std::vector<uint8_t>> levels; // (empty until the level is first locked)
    std::vector<LockState> locks;
    std::vector<std::unique_ptr<Surface>> surfaces; // texture_surface's, made on first ask
};

// What a surface is.
enum class SurfaceKind : uint8_t {
    kTextureLevel, // a texture's level (texture_surface): its references are the texture's
    kRenderTarget, // an image of its own drawn into (create_render_target; the back buffer)
    kDepth,        // a depth / stencil image of its own (create_depth_surface; the device's own depth)
    kSystemMemory, // bytes only (create_offscreen_surface: the system-memory and scratch pools)
};

// A surface (GpuSurface).
struct Surface {
    uint32_t refs = 1;
    SurfaceKind kind = SurfaceKind::kSystemMemory;
    Texture* texture = nullptr; // kTextureLevel
    uint32_t level = 0;
    Image image; // kRenderTarget, kDepth
    FormatPlan plan;
    uint32_t d3d_format = 0;
    Extent extent;
    uint32_t samples = 0; // D3DMULTISAMPLE_TYPE
    uint32_t quality = 0;
    bool lockable = false;      // a render target that can be locked (its lock reads the image back)
    std::vector<uint8_t> bytes; // kSystemMemory's pixels, a locked render target's copy (Direct3D 9's layout)
    LockState lock;
};

// A version of a dynamic buffer's memory: a discard moves to a free one when the device may still read this one.
struct BufferVersion {
    DeviceBuffer memory;
    uint64_t last_use = 0; // the last submission that drew from it or copied into it (vulkan_frames.h)
    uint64_t copied = 0;   // the last submission whose uploads or stream copied into it (copied_in_stream: which)
    bool copied_in_stream = false;
};

// A vertex or index buffer (GpuVertexBuffer, GpuIndexBuffer). A static one is device memory with a system-memory copy
// that a lock hands out and an unlock uploads (the part written); a dynamic one is memory the processor writes, locked
// in place (no-overwrite) or in a fresh version (discard).
struct Buffer {
    uint32_t refs = 1;
    bool index = false;
    uint32_t bytes = 0;
    uint32_t usage = 0;
    uint32_t format = 0; // a vertex buffer's FVF, an index buffer's index format
    uint32_t pool = 0;
    bool dynamic = false;
    std::vector<BufferVersion> versions; // a static buffer: one
    std::size_t current = 0;
    std::vector<uint8_t> shadow; // a static buffer's copy
    LockState lock;
    uint32_t lock_offset = 0;
    uint32_t lock_bytes = 0;
};

// A pixel shader (GpuPixelShader): its ps.1.1 text, and the program it stands for (vulkan_pixel_program.h).
struct PixelShader {
    uint32_t refs = 1;
    std::string source;
    ShaderProgram program;
};

// An occlusion query (GpuQuery): a slot of a query pool.
struct Query {
    uint32_t refs = 1;
    VkQueryPool pool = VK_NULL_HANDLE;
    uint32_t slot = 0;
    uint32_t index = 0;  // in the backend's slots (pool x kQueriesPerPool + slot)
    bool fresh = false;  // reset, not begun since
    bool begun = false;
    bool ended = false;
    bool recorded = false;   // begun in a pass (else it ended without drawing: 0 pixels)
    uint64_t submission = 0; // the submission it ended in (its result is there once that one has run)
};

namespace detail {
// The same address as a pointer to To (a handle and its object: never dereferenced as the other's type).
template <typename To, typename From>
To* same_address(From* pointer) noexcept
{
    void* const address = static_cast<void*>(pointer);
    return static_cast<To*>(address);
}
} // namespace detail

// The object behind a handle, and the handle of an object (the same address).
inline Texture* object(GpuTexture* handle) noexcept { return detail::same_address<Texture>(handle); }
inline Surface* object(GpuSurface* handle) noexcept { return detail::same_address<Surface>(handle); }
inline Buffer* object(GpuVertexBuffer* handle) noexcept { return detail::same_address<Buffer>(handle); }
inline Buffer* object(GpuIndexBuffer* handle) noexcept { return detail::same_address<Buffer>(handle); }
inline PixelShader* object(GpuPixelShader* handle) noexcept { return detail::same_address<PixelShader>(handle); }
inline Query* object(GpuQuery* handle) noexcept { return detail::same_address<Query>(handle); }
inline GpuTexture* texture_handle(Texture* object) noexcept { return detail::same_address<GpuTexture>(object); }
inline GpuSurface* surface_handle(Surface* object) noexcept { return detail::same_address<GpuSurface>(object); }
inline GpuVertexBuffer* vertex_handle(Buffer* object) noexcept { return detail::same_address<GpuVertexBuffer>(object); }
inline GpuIndexBuffer* index_handle(Buffer* object) noexcept { return detail::same_address<GpuIndexBuffer>(object); }
inline GpuPixelShader* shader_handle(PixelShader* object) noexcept
{
    return detail::same_address<GpuPixelShader>(object);
}
inline GpuQuery* query_handle(Query* object) noexcept { return detail::same_address<GpuQuery>(object); }

} // namespace render::vulkan

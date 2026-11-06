// NOT ORIGINAL - the UI backend on Vulkan (gpu_vulkan.h; the interface: engine/gfx/ui_backend.h; docs/design/vulkan.md,
// V9): Dear ImGui's SDL3 platform backend, and ImGui's draw lists drawn by the Vulkan Gpu itself rather than by ImGui's
// own Vulkan renderer backend - so the UI is drawn in the frame the Gpu records: in its stream command buffer, in the
// pass it infers on the device's target (the back buffer, before the present copies it into the swapchain), with its
// ring for the vertices (two frames in flight), its push descriptors and its pipeline cache, and the images ImGui shows
// are the Gpu's own textures (released through the Gpu, so destroyed only once no frame in flight uses them).
//
// - Images (create_texture): Gpu textures in the managed pool (A8R8G8B8 or DXT1, one level); an id is the texture's
//   handle as 64 bits. The font atlas is one, made by new_frame when the context has none (invalidate_device_objects
//   drops it: a reset, or the fonts built again at another scale).
// - The preview and the thumbnails' atlas: Gpu textures drawn into (X8R8G8B8: the game's blending leaves nothing in
//   alpha that shows through) with depth surfaces of their own in the game depth's format, switched to and from as
//   Direct3D 9's backend does on its device - through the Gpu's own calls, so the state block stays true.
// - A device reset (GpuContext::reset) keeps every image: the Vulkan Gpu loses nothing; the editor frees the targets
//   and the font atlas around it anyway, as it does on Direct3D 9, and they are made again on next use.
#include "gpu_vulkan/gpu_vulkan.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "backends/imgui_impl_sdl3.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/core/packed_colour.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/ui_backend.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_pipelines.h"
#include "imgui.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr long kOk = gpu_value::kResultOk;
constexpr int kBgraBytes = 4;
// A new atlas's colour before any cell is drawn (Direct3D 9's backend fills it so)
constexpr uint32_t kAtlasEmpty = 0xFF2A2C30U;

static_assert(sizeof(ImDrawVert) == Pipelines::kUiVertexBytes && offsetof(ImDrawVert, uv) == Pipelines::kUiUvOffset &&
                  offsetof(ImDrawVert, col) == Pipelines::kUiColourOffset,
              "ImGui's vertex is what the UI's pipeline reads");
static_assert(sizeof(ImDrawIdx) == sizeof(uint16_t), "ImGui's indices are 16-bit");

UiTextureId id_of(GpuTexture* texture) { return static_cast<UiTextureId>(std::bit_cast<uintptr_t>(texture)); }
GpuTexture* texture_of(UiTextureId id) { return std::bit_cast<GpuTexture*>(static_cast<uintptr_t>(id)); }

// What a context's renderer side keeps (ImGui's IO points at it: BackendRendererUserData).
struct ContextData {
    UiTextureId font = 0;
};

// A target the renderer draws into for the UI: its texture, its depth surface, and what begin switched from.
constexpr std::array<GpuTransform, 3> kSavedTransforms{GpuTransform::kWorld, GpuTransform::kView,
                                                       GpuTransform::kProjection};
struct Target {
    GpuTexture* texture = nullptr;
    GpuSurface* depth = nullptr;
    int size = 0;
    bool stencil = false; // (the depth format has one: cleared with it)
    bool failed_logged = false;
    bool active = false; // (between begin and end)
    GpuSurface* saved_target = nullptr;
    GpuSurface* saved_depth = nullptr;
    GpuViewport saved_viewport{};
    std::array<GpuMatrix, 3> saved_transforms{};
};

void release_target(Target* target)
{
    if (target->texture != nullptr) {
        gpu().release_texture(target->texture);
    }
    if (target->depth != nullptr) {
        gpu().release_surface(target->depth);
    }
    target->texture = nullptr;
    target->depth = nullptr;
    target->size = 0;
}

// The target made at `size` (kept when it already is); false (logged once, as `what`) when it can't be.
bool make_target(Target* target, int size, const char* what)
{
    if (target->texture != nullptr && target->size == size) {
        return true;
    }
    release_target(target);
    Gpu& device = gpu();
    uint32_t depth_format = gpu_value::kFormatD24S8;
    GpuSurface* game_depth = nullptr;
    if (device.depth_surface(&game_depth) == kOk && game_depth != nullptr) {
        GpuSurfaceDesc desc;
        if (device.surface_desc(game_depth, &desc) == kOk) {
            depth_format = desc.format;
        }
        device.release_surface(game_depth);
    }
    const auto side = static_cast<uint32_t>(size);
    long made = device.create_texture({.width = side,
                                       .height = side,
                                       .levels = 1,
                                       .usage = gpu_value::kUsageRenderTarget,
                                       .format = gpu_value::kFormatX8R8G8B8,
                                       .pool = gpu_value::kPoolDefault},
                                      &target->texture);
    if (made == kOk) {
        made = device.create_depth_surface({.width = side,
                                            .height = side,
                                            .format = depth_format,
                                            .samples = 0,
                                            .quality = 0,
                                            .lockable_or_discard = true},
                                           &target->depth);
    }
    if (made != kOk) {
        if (!target->failed_logged) {
            get_log("level editor UI: no %s of %d pixels (%08X)", what, size, static_cast<unsigned>(made));
        }
        target->failed_logged = true;
        release_target(target);
        return false;
    }
    target->size = size;
    target->stencil = depth_format == gpu_value::kFormatD24S8;
    return true;
}

// The target made the device's (what was there saved first), the viewport set; false when its level can't be had.
bool switch_to(Target* target, const GpuViewport& viewport)
{
    Gpu& device = gpu();
    GpuSurface* surface = nullptr;
    if (device.texture_surface(target->texture, 0, &surface) != kOk || surface == nullptr) {
        return false;
    }
    device.render_target(0, &target->saved_target);
    device.depth_surface(&target->saved_depth);
    device.get_viewport(&target->saved_viewport);
    for (std::size_t i = 0; i < kSavedTransforms.size(); ++i) {
        device.get_transform(core::at(kSavedTransforms, i), core::at(target->saved_transforms, i).data());
    }
    device.set_render_target(0, surface);
    device.release_surface(surface);
    device.set_depth_surface(target->depth);
    device.set_viewport(viewport);
    target->active = true;
    return true;
}

// What switch_to saved put back.
void switch_back(Target* target)
{
    if (!target->active) {
        return;
    }
    Gpu& device = gpu();
    if (target->saved_target != nullptr) {
        device.set_render_target(0, target->saved_target);
    }
    device.set_depth_surface(target->saved_depth);
    device.set_viewport(target->saved_viewport); // (after the target: setting one resets the viewport)
    for (std::size_t i = 0; i < kSavedTransforms.size(); ++i) {
        device.set_transform(core::at(kSavedTransforms, i), core::at(target->saved_transforms, i).data());
    }
    if (target->saved_target != nullptr) {
        device.release_surface(target->saved_target);
    }
    if (target->saved_depth != nullptr) {
        device.release_surface(target->saved_depth);
    }
    target->saved_target = target->saved_depth = nullptr;
    target->active = false;
}

void clear_target(const Target& target, uint32_t argb)
{
    gpu().clear(0, nullptr,
                gpu_value::kClearTarget | gpu_value::kClearDepth | (target.stencil ? gpu_value::kClearStencil : 0U),
                argb, 1.0F, 0);
}

GpuViewport square(int x, int y, int size)
{
    return {.x = static_cast<uint32_t>(x),
            .y = static_cast<uint32_t>(y),
            .width = static_cast<uint32_t>(size),
            .height = static_cast<uint32_t>(size),
            .min_z = 0.0F,
            .max_z = 1.0F};
}

// Whether the Gpu's device is made (its draws ready).
bool device_ready()
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    return state.draws_made && state.back_buffer != nullptr;
}

class VulkanUiBackend final : public UiBackend {
public:
    VulkanUiBackend() = default;
    VulkanUiBackend(const VulkanUiBackend&) = delete;
    VulkanUiBackend& operator=(const VulkanUiBackend&) = delete;
    VulkanUiBackend(VulkanUiBackend&&) = delete;
    VulkanUiBackend& operator=(VulkanUiBackend&&) = delete;
    ~VulkanUiBackend() = default;

    UiStart start(SDL_Window* window) override
    {
        if (!device_ready()) {
            return UiStart::kNoDevice;
        }
        if (!ImGui_ImplSDL3_InitForVulkan(window)) {
            return UiStart::kWindowRefused;
        }
        // (no controllers: the game has them; the UI is mouse and keyboard)
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
        if (!pipeline_made()) {
            ImGui_ImplSDL3_Shutdown();
            return UiStart::kDeviceRefused;
        }
        ContextData& data = contexts_[ImGui::GetCurrentContext()];
        data = ContextData{};
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererUserData = &data;
        io.BackendRendererName = "thug2_vulkan_ui";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset; // (ImDrawCmd::VtxOffset: each draw's base vertex)
        return UiStart::kStarted;
    }

    [[nodiscard]] const char* describe() const override { return "SDL3 and Vulkan"; }

    bool process_event(const SDL_Event* event) override { return ImGui_ImplSDL3_ProcessEvent(event); }

    void new_frame() override
    {
        ContextData* data = context_data();
        if (data != nullptr && data->font == 0) {
            make_font(data);
        }
        ImGui_ImplSDL3_NewFrame();
    }

    void render_draw_data(ImDrawData* data) override
    {
        if (data == nullptr || data->TotalVtxCount <= 0 || data->DisplaySize.x <= 0.0F || data->DisplaySize.y <= 0.0F) {
            return;
        }
        vertices_.clear();
        indices_.clear();
        draws_.clear();
        const ImVec2 origin = data->DisplayPos;
        uint32_t index_base = 0;
        int32_t vertex_base = 0;
        for (const ImDrawList* list : data->CmdLists) {
            const void* const vertex_bytes = list->VtxBuffer.Data;
            const std::span<const uint8_t> vertices(static_cast<const uint8_t*>(vertex_bytes),
                                                    static_cast<std::size_t>(list->VtxBuffer.Size) *
                                                        sizeof(ImDrawVert));
            vertices_.insert(vertices_.end(), vertices.begin(), vertices.end());
            const std::span<const ImDrawIdx> indices(list->IdxBuffer.Data,
                                                     static_cast<std::size_t>(list->IdxBuffer.Size));
            indices_.insert(indices_.end(), indices.begin(), indices.end());
            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.UserCallback != nullptr) {
                    // (a callback: the editor and the front end add none; ImGui's own one, a reset of the render state,
                    // needs nothing here - the draws set all of theirs)
                    continue;
                }
                const UiTextureId id = command.GetTexID();
                draws_.push_back({.texture = id != 0 ? object(texture_of(id)) : nullptr,
                                  .clip = {.left = static_cast<int32_t>(command.ClipRect.x - origin.x),
                                           .top = static_cast<int32_t>(command.ClipRect.y - origin.y),
                                           .right = static_cast<int32_t>(command.ClipRect.z - origin.x),
                                           .bottom = static_cast<int32_t>(command.ClipRect.w - origin.y)},
                                  .first_index = command.IdxOffset + index_base,
                                  .indices = command.ElemCount,
                                  .base_vertex = static_cast<int32_t>(command.VtxOffset) + vertex_base});
            }
            index_base += static_cast<uint32_t>(list->IdxBuffer.Size);
            vertex_base += list->VtxBuffer.Size;
        }
        const UiBatch batch{.vertices = vertices_,
                            .indices = indices_,
                            .display = {origin.x, origin.y, data->DisplaySize.x, data->DisplaySize.y},
                            .draws = draws_};
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        record_ui_draws(state, batch);
    }

    void invalidate_device_objects() override
    {
        ContextData* data = context_data();
        if (data == nullptr) {
            return;
        }
        if (data->font != 0) {
            release_texture(data->font);
            data->font = 0;
        }
        ImGui::GetIO().Fonts->SetTexID(0);
    }

    UiTextureId create_texture(int width, int height, UiPixelLayout format, const void* data,
                               UiRowLayout row_layout) override
    {
        if (width <= 0 || height <= 0 || data == nullptr || row_layout.row_bytes_ <= 0 || row_layout.rows <= 0 ||
            !device_ready()) {
            return 0;
        }
        Gpu& device = gpu();
        GpuTexture* texture = nullptr;
        // (managed: the Gpu keeps a copy in memory)
        if (device.create_texture(
                {.width = static_cast<uint32_t>(width),
                 .height = static_cast<uint32_t>(height),
                 .levels = 1,
                 .usage = 0,
                 .format = format == UiPixelLayout::kDxt1 ? gpu_value::kFormatDxt1 : gpu_value::kFormatA8R8G8B8,
                 .pool = gpu_value::kPoolManaged},
                &texture) != kOk ||
            texture == nullptr) {
            return 0;
        }
        GpuLockedRect locked;
        if (device.lock_texture(texture, 0, &locked, nullptr, 0) != kOk || locked.bits == nullptr) {
            device.release_texture(texture);
            return 0;
        }
        const auto rows = static_cast<std::size_t>(row_layout.rows);
        const auto source_pitch = static_cast<std::size_t>(row_layout.row_bytes_);
        const auto target_pitch = static_cast<std::size_t>(locked.pitch);
        const std::size_t copied = std::min(source_pitch, target_pitch);
        const std::span<const uint8_t> from(static_cast<const uint8_t*>(data), rows * source_pitch);
        const std::span<uint8_t> to(static_cast<uint8_t*>(locked.bits), rows * target_pitch);
        for (std::size_t row = 0; row < rows; ++row) {
            std::memcpy(to.subspan(row * target_pitch).data(), from.subspan(row * source_pitch).data(), copied);
        }
        device.unlock_texture(texture, 0);
        return id_of(texture); // (released by release_texture)
    }

    void release_texture(UiTextureId id) override
    {
        if (id != 0U) {
            gpu().release_texture(texture_of(id));
        }
    }

    bool begin_offscreen(int size, colour::Packed argb_colour) override
    {
        if (offscreen_.active || size <= 0 || !device_ready() || !make_target(&offscreen_, size, "off-screen target") ||
            !switch_to(&offscreen_, square(0, 0, size))) {
            return false;
        }
        clear_target(offscreen_, argb_colour.value());
        return true;
    }

    void end_offscreen() override { switch_back(&offscreen_); }

    [[nodiscard]] UiTextureId offscreen_texture() override
    {
        return offscreen_.texture != nullptr ? id_of(offscreen_.texture) : 0;
    }

    void free_offscreen() override
    {
        end_offscreen();
        release_target(&offscreen_);
    }

    bool begin_atlas_cell(int atlas_size, int x, int y, int size, colour::Packed argb_colour) override
    {
        if (atlas_.active || offscreen_.active || atlas_size <= 0 || size <= 0 || x < 0 || y < 0 ||
            x + size > atlas_size || y + size > atlas_size || !device_ready()) {
            return false;
        }
        const bool fresh = atlas_.texture == nullptr || atlas_.size != atlas_size;
        if (fresh && atlas_.texture != nullptr) {
            ++atlas_generation_;
        }
        if (!make_target(&atlas_, atlas_size, "thumbnail atlas") || !switch_to(&atlas_, square(0, 0, atlas_size))) {
            return false;
        }
        if (fresh) {
            // (a new atlas: every cell cleared once, so an unfilled one isn't garbage)
            gpu().clear(0, nullptr, gpu_value::kClearTarget, kAtlasEmpty, 1.0F, 0);
        }
        gpu().set_viewport(square(x, y, size));
        // (no rectangles: the viewport - the cell - is cleared)
        clear_target(atlas_, argb_colour.value());
        return true;
    }

    void end_atlas_cell() override { switch_back(&atlas_); }

    [[nodiscard]] UiTextureId atlas_texture() override { return atlas_.texture != nullptr ? id_of(atlas_.texture) : 0; }

    [[nodiscard]] uint32_t atlas_generation() override { return atlas_generation_; }

    void free_atlas() override
    {
        end_atlas_cell();
        if (atlas_.texture != nullptr || atlas_.depth != nullptr) {
            ++atlas_generation_;
        }
        release_target(&atlas_);
    }

private:
    static ContextData* context_data()
    {
        if (ImGui::GetCurrentContext() == nullptr) {
            return nullptr;
        }
        return static_cast<ContextData*>(ImGui::GetIO().BackendRendererUserData);
    }

    // The UI's pipeline for the back buffer and the device's depth surface (what it draws into), made now: false when
    // the device refuses it (logged).
    static bool pipeline_made()
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        const Image& colour = state.back_buffer->image;
        const VkFormat depth = state.auto_depth != nullptr ? state.auto_depth->image.format : VK_FORMAT_UNDEFINED;
        std::string error;
        if (state.pipelines.ui_pass(colour.format, depth, static_cast<uint32_t>(colour.samples), &error) ==
            VK_NULL_HANDLE) {
            get_log("vulkan: no pipeline for the UI (%s)", error.c_str());
            return false;
        }
        return true;
    }

    // The context's font atlas as a texture (ImGui's RGBA texels as BGRA), its id given to ImGui.
    void make_font(ContextData* data)
    {
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        if (pixels == nullptr || width <= 0 || height <= 0) {
            return;
        }
        const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * kBgraBytes;
        const std::span<const uint8_t> rgba(pixels, bytes);
        std::vector<uint8_t> bgra(rgba.begin(), rgba.end());
        for (std::size_t at = 0; at + 2 < bytes; at += kBgraBytes) {
            std::swap(core::at(bgra, at), core::at(bgra, at + 2));
        }
        data->font = create_texture(width, height, UiPixelLayout::kBgra32, bgra.data(),
                                    {.row_bytes_ = width * kBgraBytes, .rows = height});
        io.Fonts->SetTexID(static_cast<ImTextureID>(data->font));
    }

    std::unordered_map<const ImGuiContext*, ContextData> contexts_; // (each context's, kept: ImGui's IO points in)
    Target offscreen_;
    Target atlas_;
    uint32_t atlas_generation_ = 1;
    // the draw data gathered (kept between frames: their room is reused)
    std::vector<uint8_t> vertices_;
    std::vector<uint16_t> indices_;
    std::vector<UiDraw> draws_;
};

} // namespace

UiBackend& ui_backend()
{
    static core::NoDestroy<VulkanUiBackend> backend;
    return backend.get();
}

} // namespace render::vulkan

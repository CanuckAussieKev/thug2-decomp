// NOT ORIGINAL - the UI backend on Vulkan (gpu_vulkan.h): none yet. Dear ImGui's Vulkan renderer backend is V9's
// (docs/design/vulkan.md); until then the backend refuses to start, so the level editor falls back to its old HUD and
// the new front end isn't drawn (the original menus show), and every picture it is asked for is none.
#include "gpu_vulkan/gpu_vulkan.h"

#include <cstdint>

#include "base/core/no_destroy.h"
#include "base/core/packed_colour.h"
#include "engine/gfx/ui_backend.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

class VulkanUiBackend final : public UiBackend {
public:
    VulkanUiBackend() = default;
    VulkanUiBackend(const VulkanUiBackend&) = delete;
    VulkanUiBackend& operator=(const VulkanUiBackend&) = delete;
    VulkanUiBackend(VulkanUiBackend&&) = delete;
    VulkanUiBackend& operator=(VulkanUiBackend&&) = delete;
    ~VulkanUiBackend() = default;

    UiStart start(SDL_Window* /*window*/) override
    {
        if (!logged_) {
            get_log("vulkan: no Dear ImGui backend on Vulkan yet - the editor's and the new front end's ImGui parts "
                    "aren't drawn");
            logged_ = true;
        }
        return UiStart::kDeviceRefused;
    }
    [[nodiscard]] const char* describe() const override { return "none on Vulkan yet"; }
    bool process_event(const SDL_Event* /*event*/) override { return false; }
    void new_frame() override {}
    void render_draw_data(ImDrawData* /*data*/) override {}
    void invalidate_device_objects() override {}
    UiTextureId create_texture(int /*width*/, int /*height*/, UiPixelLayout /*format*/, const void* /*data*/,
                               UiRowLayout /*row_layout*/) override
    {
        return 0;
    }
    void release_texture(UiTextureId /*id*/) override {}
    bool begin_offscreen(int /*size*/, colour::Packed /*argb_colour*/) override { return false; }
    void end_offscreen() override {}
    [[nodiscard]] UiTextureId offscreen_texture() override { return 0; }
    void free_offscreen() override {}
    bool begin_atlas_cell(int /*atlas_size*/, int /*x*/, int /*y*/, int /*size*/, colour::Packed /*argb_colour*/) override
    {
        return false;
    }
    void end_atlas_cell() override {}
    [[nodiscard]] UiTextureId atlas_texture() override { return 0; }
    [[nodiscard]] uint32_t atlas_generation() override { return 0; }
    void free_atlas() override {}

private:
    bool logged_ = false;
};

} // namespace

UiBackend& ui_backend()
{
    static core::NoDestroy<VulkanUiBackend> backend;
    return backend.get();
}

} // namespace render::vulkan

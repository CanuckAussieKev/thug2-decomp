// The engine's platform calls that draw or make device objects: see pc_engine.h.

#include "engine/gfx/engine.h"
#include "render_d3d9/pc_gamma.h"
#include "render_d3d9/pc_engine.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>

#include "engine/memory/mem.h"
#include "engine/gfx/view_cam.h"
#include "render_d3d9/device.h"
#include "engine/gfx/engine_sprite.h"
#include "render_d3d9/geom.h"
#include "render_d3d9/pc_model.h"
#include "render_d3d9/pc_scene.h"
#include "render_d3d9/pc_sprite.h"
#include "render_d3d9/pc_texture.h"
#include "render_d3d9/pc_weather.h"
#include "engine/gfx/render_camera.h"
#include "render_d3d9/render_state.h"
#include "render_d3d9/mesh_list.h"
#include "render_d3d9/shadow_base.h"
#include "render_d3d9/bitmap_texture.h"
#include "base/maths/vec4.h"
#include "render_d3d9/pc_particles.h"
#include "engine/objects/particle_component.h"
#include <gsl/pointers>
#include <array>
#include <gsl/assert>
#include "base/core/checked_index.h"
#include "base/core/down_cast.h"
#include "engine/file/disk.h"
#include "base/core/format.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/effects_state.h"
#include "engine/gfx/view_setup.h"
#include "engine/gfx/engine_parts.h"
#include "base/maths/rect.h"
#include "render_d3d9/d3dx_jpg.h"

namespace render {

constexpr std::size_t kSceneFileNameSize = 32;
constexpr int kEmptySceneSectors = 10; // an empty scene is made with room for this many sectors

namespace {

// The loading screen is up (0x00786D04, show_loading_screen): it draws and presents on its own.

// Rendering skipped (frames not drawn or shown).

// The screenshot asked for by name, taken once the frame is shown (0x00786D94, 0x80 characters), and whether that
// name is used as it is (0x00786E14) rather than numbered.

// The clear colour: the fog's with fog and distance clipping on, else the background's.
uint32_t back_buffer_fill()
{
    if (view_setup().fog_setting() && view_setup().distance_clipping()) {
        return engine_parts().fog_colour();
    }
    return renderer().render_camera().background_colour_;
}

// D3DX (linked library code): a surface written to an image file (format 1: JPEG).
long save_surface_to_file(const char* file, GpuSurface* surface)
{
    return d3dx::save_surface_to_jpg(file, surface);
}

// A shadow caster's key: the texture's image (its own, or its edit's: image_of).
TextureRef* caster_key(const BitmapTexture* texture) { return image_of(texture); }

} // namespace

// 0x004B2280 (the device 0x007D69E0's BeginScene, vtable slot 41; not while the loading screen is up: 0x00786D04)
void begin_frame_draw()
{
    if (engine_parts().loading_screen_up()) {
        return;
    }
    gpu().begin_scene();
}

// 0x004B22B0
void set_screen_clip(const SkinScaling* settings) { set_skin_scaling(settings); }

// 0x004B24C0
void set_gamma(float red, float green, float blue) { set_device_gamma(red, green, blue); }

// 0x004B24D0
std::array<float, 3> get_gamma() { return get_device_gamma(); }

// 0x004B24E0
void screen_shot(const char* name, int /*unused*/)
{
    // (the original sprintfs into these 32 bytes; ours truncates a longer name)
    std::array<char, kSceneFileNameSize> file{};
    if (effects_state().screen_shot_name_exact()) {
        text::format_to(file, "%s.jpg", name);
    } else {
        // the first numbered name with no file yet
        int number = 0;
        text::format_to(file, "%s%03d.jpg", name, number);
        while (fs::DiskStream* existing = fs::disk().open_stream(file.data(), "rb", fs::StreamSharing::kAll)) { // (fopen)
            fs::disk().stream_close(existing);
            ++number;
            text::format_to(file, "%s%03d.jpg", name, number);
        }
    }
    GpuSize mode;
    gpu().display_size(&mode);
    GpuSurface* surface = nullptr;
    gpu().create_offscreen_surface(mode.width, mode.height, gpu_value::kFormatA8R8G8B8, gpu_value::kPoolScratch, &surface);
    gpu().read_front_buffer(surface);
    save_surface_to_file(file.data(), surface);
    gpu().release_surface(surface);
}

// 0x004B25F0
void start_engine()
{
    init_direct_3d();
    objects::set_particle_hub(std::make_unique<PcParticleManager>());
    engine_parts().weather() = std::make_unique<PcWeather>();
    engine_parts().screen_effects() = std::make_unique<PcScreenEffects>();
}

// 0x004B2750
void finish_rendering()
{
    if (engine_parts().loading_screen_up()) {
        return;
    }
    gpu().end_scene();
    if (engine_parts().suppress_draw()) {
        return;
    }
    gpu().present();
    renderer().reset_draw_counts(); // the frame's draw counts
    if (core::at(effects_state().pending_screen_shot(), 0) != '\0') {
        screen_shot(effects_state().pending_screen_shot().data(), 0);
        core::at(effects_state().pending_screen_shot(), 0) = 0;
        effects_state().set_screen_shot_name_exact(false);
    }
    // The next frame's clear: with the screen blur on, colour and depth (and the blur's frames counted, from 0
    // again once it's off); letterboxed, to black; with 0x00786CBD set, colour and depth; else the depth alone.
    if (effects_state().screen_blur() != 0) {
        if (!engine_parts().suppress_draw()) {
            gpu().clear(0, nullptr, gpu_value::kClearTarget | gpu_value::kClearDepth, back_buffer_fill(), 1.0F, 0);
        }
        ++effects_state().screen_blur_frames();
        return;
    }
    if (renderer().render_camera().letterbox_active_ != 0U) {
        if (!engine_parts().suppress_draw()) {
            clear_device(0, nullptr, gpu_value::kClearTarget | gpu_value::kClearDepth, 0, 1.0F, 0);
        }
    } else if (renderer().render_camera().clear_colour_and_depth_) {
        if (!engine_parts().suppress_draw()) {
            clear_device(0, nullptr, gpu_value::kClearTarget | gpu_value::kClearDepth, back_buffer_fill(), 1.0F, 0);
        }
    } else if (!engine_parts().suppress_draw()) {
        clear_device(0, nullptr, gpu_value::kClearDepth, renderer().render_camera().background_colour_, 1.0F, 0);
    }
    effects_state().screen_blur_frames() = 0;
}

// 0x004B28C0
gsl::owner<PcScene*> make_empty_scene(const char* /*name*/, TextureSet* /*textures*/, bool with_super_sectors)
{
    gsl::owner<render::PcScene*> scene{memory::make_owned<PcScene>(kEmptySceneSectors)};
    // (no check: a failed allocation writes through null, as the original)
    scene->set_builds_collision_grid(with_super_sectors);
    scene->set_is_sky(false);
    void* memory = memory::alloc(sizeof(SceneRef));
    scene->set_meshes(memory ? memory::construct_in<SceneRef>(memory) : nullptr);
    return scene;
}

// 0x004B2990
bool unload_scene(WorldScene* world_scene)
{
    auto* scene = core::down_cast<PcScene>(world_scene);
    scene->unload_sectors();
    discard_scene(scene->meshes());
    scene->set_meshes(nullptr);
    return true;
}

// 0x004B29C0
gsl::owner<RenderModel*> spawn_model() { return new PcModel; }

// 0x004B2A20
gsl::owner<Geom*> create_geom() { return new PcGeom; }

// 0x004B2A80
gsl::owner<BillboardQuad*> build_sprite_quad() { return new PcSprite; }

// 0x004B2AF0
gsl::owner<TexturedQuad*> create_textured_quad() { return new TexturedQuad(nullptr); }

// 0x004B2B60
gsl::owner<void*> create_render_target(maths::PixelSize size, uint8_t depth, int unused)
{
    const auto width = size.width;
    const auto height = size.height;
    gsl::owner<render::PcTexture*> target{memory::make_owned<PcTexture>()};
    void* memory = memory::alloc(sizeof(TextureRef));
    TextureRef* texture = memory ? memory::construct_in<TextureRef>(memory) : nullptr;
    target->set_texture(texture);
    // (no check: a failed allocation calls through null, as the original)
    Expects(texture != nullptr);
    texture->create_render_target({.width = width, .height = height}, depth, unused);
    return target;
}

// 0x004B2C30
void add_texture_shadow_caster(const BitmapTexture* texture, void* model, WorldScene* /*unused*/)
{
    add_shadow_caster(caster_key(texture), model, 0);
}

// 0x004B2C50
void aim_texture_shadow_caster(const BitmapTexture* texture, gfx::ViewCam* camera)
{
    const maths::Vec4& pos = *camera->fetch_location();
    std::array<float, 3> position = {{pos.x, pos.y, pos.z}};
    const maths::Mat4& matrix = *camera->fetch_mat();
    // the point it looks at: the position plus the matrix's third row
    std::array<float, 3> target = {{core::at(matrix.row, 2).x + core::at(position, 0),
                                    core::at(matrix.row, 2).y + core::at(position, 1),
                                    core::at(matrix.row, 2).z + core::at(position, 2)}};
    aim_shadow_caster(caster_key(texture), position, target);
}

// 0x004B2D10
void remove_texture_shadow_caster(const BitmapTexture* texture)
{
    remove_shadow_caster(caster_key(texture));
}

} // namespace render

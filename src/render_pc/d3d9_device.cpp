// NOT ORIGINAL - the Direct3D 9 backend's render::Device (d3d9_device.h). Each method calls the platform layer's own
// function, or does what the engine side did with Direct3D at that place, unchanged: the same calls in the same order.

#include "render_pc/d3d9_device.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/render_camera.h"
#include "base/core/checksums.h"
#include "base/core/down_cast.h"
#include "base/core/no_destroy.h"
#include "base/maths/rect.h"
#include "render_pc/bitmap_texture.h"
#include "engine/memory/mem.h"
#include "render_pc/device.h"
#include "render_pc/draw_frame.h"
#include "engine/gfx/engine_glue.h"
#include "render_pc/pc_engine.h"
#include "render_pc/flash.h"
#include "render_pc/geom.h"
#include "render_pc/loading_bar.h"
#include "render_pc/loading_screen.h"
#include "render_pc/pc_billboards.h"
#include "render_pc/pc_fog.h"
#include "render_pc/pc_flexible_particles.h"
#include "render_pc/pc_font.h"
#include "render_pc/pc_lights.h"
#include "render_pc/pc_mesh.h"  // IWYU pragma: keep - PcMesh to SubMesh
#include "render_pc/pc_scene.h" // IWYU pragma: keep - PcScene to WorldScene
#include "render_pc/pc_primitive.h"
#include "render_pc/pc_texture.h"
#include "render_pc/pc_texture_projection.h"
#include "render_pc/pc_viewport.h"
#include "render_pc/primitive_states.h"
#include "engine/gfx/render_device.h"
#include "render_pc/scene_load.h"
#include "render_pc/screenshot_capture.h"
#include "render_pc/shadow_base.h"
#include "render_pc/shatter.h"
#include "render_pc/sub_mesh.h"
#include "render_pc/texture_splats.h"
#include "render_pc/view_mode.h"
#include "render_pc/editor_support.h"
#include "engine/gfx/view_modes.h"

namespace render::d3d9 {

namespace {

class D3d9Device final : public Device {
public:
    // --- the frame ---
    void start_engine() override { render::start_engine(); }
    void begin_frame_draw() override { render::begin_frame_draw(); }
    void draw_world() override { render::draw_world(); }
    void finish_rendering() override { render::finish_rendering(); }

    // --- a lost device (the main loop's 0x0044EE90) ---
    bool can_reset() override { return render::gpu().test_cooperative_level() != gpu_value::kResultDeviceLost; }
    void release_device_objects() override
    {
        release_render_target_textures();
        release_buffers();
        release_render_targets();
    }
    void restore_device_objects() override
    {
        restore_render_target_textures();
        recreate_buffers();
        create_render_targets();
    }
    bool reset() override
    {
        const bool reset = render::gpu_context().reset();
        // NOT ORIGINAL - the Gpu's state block (engine/gfx/gpu.h) to the defaults the reset device has.
        render::gpu().reset_state_block();
        return reset;
    }

    // --- resources ---
    gsl::owner<WorldScene*> make_empty_scene(const char* name, TextureSet* textures, bool with_super_sectors) override
    {
        return render::make_empty_scene(name, textures, with_super_sectors);
    }
    gsl::owner<WorldScene*> load_scene_from_memory(const uint8_t* data, size_t size, const TextureSet* textures,
                                                   bool with_super_sectors, bool flag_8d, bool mesh_flag) override
    {
        return render::load_scene_from_memory(data, size, textures, with_super_sectors, flag_8d, mesh_flag);
    }
    gsl::owner<WorldScene*> load_scene_from_file(const char* name, const TextureSet* textures, bool with_super_sectors,
                                                 bool flag_8d, bool mesh_flag, VertexArena* arena) override
    {
        return render::load_scene_from_file(name, textures, with_super_sectors, flag_8d, mesh_flag, arena);
    }
    bool unload_scene(WorldScene* scene) override { return render::unload_scene(scene); }
    gsl::owner<SubMesh*> load_mesh_file(const char* name, TextureSet* textures) override
    {
        return render::load_mesh_file(name, textures);
    }
    gsl::owner<SubMesh*> load_mesh_from_memory(uint32_t checksum, const uint8_t* data, size_t size, const void* skin,
                                              size_t skin_size, TextureSet* textures) override
    {
        return render::load_mesh_from_memory(checksum, data, size, skin, skin_size, textures);
    }
    gsl::owner<RenderModel*> spawn_model() override { return render::spawn_model(); }
    gsl::owner<Geom*> create_geom() override { return render::create_geom(); }
    gsl::owner<VertexRun*> create_primitive(int own) override { return memory::make_owned<PcPrimitive>(own); }
    gsl::owner<VertexRun*> make_primitive_at(std::span<std::byte> storage, int own) override
    {
        static_assert(sizeof(PcPrimitive) <= kStackPrimitiveBytes && alignof(PcPrimitive) <= alignof(std::max_align_t),
                      "the drawer fits the stack bytes kept for it (0x158 bytes; 0x1A0 on x64)");
        Expects(storage.size() >= sizeof(PcPrimitive));
        return memory::construct_in<PcPrimitive>(storage.data(), own);
    }
    gsl::owner<BillboardQuad*> build_sprite_quad() override { return render::build_sprite_quad(); }
    gsl::owner<TexturedQuad*> create_textured_quad() override { return render::create_textured_quad(); }
    gsl::owner<void*> create_render_target(maths::PixelSize size, uint8_t depth, int unused) override
    {
        return render::create_render_target(size, depth, unused);
    }
    gsl::owner<ModelLights*> spawn_model_lights() override { return render::spawn_model_lights(); }
    gsl::owner<TextureSet*> load_tex_dict_file(const char* name) override { return render::load_tex_dict_file(name); }
    gsl::owner<TextureSet*> load_tex_dict_memory(uint32_t checksum, const uint8_t* data, size_t size) override
    {
        return render::load_tex_dict_memory(checksum, data, size);
    }
    gsl::owner<TextureSet*> create_tex_dict(uint32_t checksum) override { return render::create_tex_dict(checksum); }
    uint32_t tex_dict_memory(const TextureSet* textures) override
    {
        return core::down_cast<const PcTexDict>(textures)->unknown10();
    }
    gsl::owner<ViewRegion*> create_viewport(const std::array<float, 4>* rect, gfx::ViewCam* camera) override
    {
        return render::create_viewport(rect, camera);
    }
    gsl::owner<TextureProjection*> create_texture_projection() override { return new PcTextureProjection; }
    gsl::owner<EmitterInstance*> create_flexible_particles(checksums::Name system_name, checksums::Name kind,
                                                           int particle_cap, int max_streams, uint32_t texture,
                                                           uint32_t blend_mode, int fixed_alpha, int segments,
                                                           float unknown, int history, const char* model) override
    {
        return render::create_flexible_particles(system_name, kind, particle_cap, max_streams, texture, blend_mode,
                                                 fixed_alpha, segments, unknown, history, model);
    }
    gsl::owner<Typeface*> create_pc_font(const char* name) override { return render::create_pc_font(name); }
    void unload_pc_font(Typeface* font) override { render::unload_pc_font(core::down_cast<PcFont>(font)); }
    void allocate_text_pool(uint32_t count) override { render::allocate_text_pool(count); }
    void create_window_pool() override { render::create_window_pool(); }

    // --- state blocks ---
    void toggle_fog(bool on) override { render::toggle_fog(on); }
    void set_fog_distance(float start) override { render::set_fog_distance(start); }
    void set_fog_color(uint32_t rgba) override { render::set_fog_color(rgba); }
    void set_gamma(float red, float green, float blue) override { render::set_gamma(red, green, blue); }
    std::array<float, 3> get_gamma() override { return render::get_gamma(); }
    void set_screen_clip(const SkinScaling* settings) override { render::set_screen_clip(settings); }
    void set_primitive_states() override { render::set_primitive_states(); }
    void reset_primitive_states() override { render::reset_primitive_states(); }
    void set_primitive_depth_test(DepthTest depth_test) override { render::set_primitive_depth_test(depth_test); }
    bool update_ambient_light() override { return render::update_ambient_light(); }
    uint32_t* fetch_ambient_hue(uint32_t* out) override { return render::fetch_ambient_hue(out); }
    bool update_light_color(int light) override { return render::update_light_color(light); }
    uint32_t* fetch_light_tint(uint32_t* out, int light) override { return render::fetch_light_tint(out, light); }
    bool update_light_direction(int light) override { return render::update_light_direction(light); }
    maths::Vec4* get_light_direction(int light) override { return render::get_light_direction(light); }
    void update_all_lights() override { render::update_all_lights(); }

    // --- effects ---
    void draw_flash(const Flash* flash) override { render::draw_flash(flash); }
    void shatter_geom(Geom* geom) override { render::shatter_geom(core::down_cast<PcGeom>(geom)); }
    void update_shatter(Shatter* shatter, float step) override
    {
        render::update_shatter(core::down_cast<PcShatter>(shatter), step);
    }
    void draw_shatter(Shatter* shatter) override { render::draw_shatter(core::down_cast<PcShatter>(shatter)); }
    void kill_all_shatters() override { render::kill_all_shatters(); }
    TextureSplats* create_texture_splat(void** sectors, HitMesh* const* objects, SplatParams* params) override
    {
        return render::create_texture_splat(sectors, objects, params);
    }
    void light_texture_splats(TextureSplats* splats) override { render::light_texture_splats(splats); }
    void kill_all_texture_splats() override { render::kill_all_texture_splats(); }
    void kill_texture_splats_in_box(const maths::BBox* box) override { render::kill_texture_splats_in_box(box); }
    void add_texture_shadow_caster(const BitmapTexture* texture, void* model, WorldScene* unused) override
    {
        render::add_texture_shadow_caster(texture, model, unused);
    }
    void aim_texture_shadow_caster(const BitmapTexture* texture, gfx::ViewCam* camera) override
    {
        render::aim_texture_shadow_caster(texture, camera);
    }
    void remove_texture_shadow_caster(const BitmapTexture* texture) override
    {
        render::remove_texture_shadow_caster(texture);
    }
    void set_shadow_companion(void* (*companion)(void* caster_model)) override
    {
        render::set_shadow_companion(companion);
    }
    uint32_t occlusion_pixels() override { return render::occlusion_pixels(); }

    // --- what is shown ---
    void screen_shot(const char* name, int unused) override { render::screen_shot(name, unused); }
    bool capture_back_buffer(sys::screenshot::TickCycle* out, std::string* error) override
    {
        return gfx::screenshot_capture::capture_back_buffer(out, error);
    }
    uint32_t presented_width() override { return render::presented_width(); }
    bool screen_copies_made() override { return render::screen_copies_made(); }
    [[noreturn]] void show_disc_error() override { render::show_disc_error(); }
    void show_loading_screen(const char* name, bool keep, bool black) override
    {
        render::show_loading_screen(name, keep, black);
    }
    void hide_loading_screen() override { render::hide_loading_screen(); }
    void free_loading_screen_sprite() override { render::free_loading_screen_sprite(); }
    void update_loading_bar_colors() override { render::update_loading_bar_colors(); }
    void show_progress_bar(float seconds) override { render::show_progress_bar(seconds); }

    // --- what our level editor asks ---
    [[nodiscard]] bool device_made() override { return render::device_made(); }
    void set_view_mode(view_mode::Mode mode) override { view_mode::assign_value(mode); }
    void draw_screen_lines(std::span<const ScreenLineVertex> vertices) override
    {
        editor_support::draw_screen_lines(vertices);
    }
    [[nodiscard]] uint32_t geom_mesh_count(const Geom* geom) override { return editor_support::geom_mesh_count(geom); }
    [[nodiscard]] MeshData* geom_mesh(const Geom* geom, uint32_t index) override
    {
        return editor_support::geom_mesh(geom, index);
    }
    [[nodiscard]] const void* geom_mesh_list(const Geom* geom) override { return editor_support::geom_mesh_list(geom); }
    [[nodiscard]] InstanceData* geom_instance(const Geom* geom) override { return editor_support::geom_instance(geom); }
    [[nodiscard]] bool geom_active(const Geom* geom) override { return editor_support::geom_active(geom); }
    gsl::owner<VertexBufferData*> create_vertex_buffer(uint32_t bytes) override
    {
        return editor_support::create_vertex_buffer(bytes);
    }
    gsl::owner<IndexBufferData*> create_index_buffer(uint32_t bytes) override
    {
        return editor_support::create_index_buffer(bytes);
    }
    void upload_vertex_buffer(VertexBufferData* buffer) override { editor_support::upload_vertex_buffer(buffer); }
    void upload_index_buffer(IndexBufferData* buffer) override { editor_support::upload_index_buffer(buffer); }
    void release_vertex_buffer(gsl::owner<VertexBufferData*> buffer) override
    {
        editor_support::release_vertex_buffer(buffer);
    }
    void release_index_buffer(gsl::owner<IndexBufferData*> buffer) override
    {
        editor_support::release_index_buffer(buffer);
    }
    void begin_preview_view(const maths::Mat4* orientation, const maths::Vec4* position, CameraLens lens) override
    {
        editor_support::begin_preview_view(orientation, position, lens);
    }
    void draw_preview_geom(Geom* geom, bool through_instance) override
    {
        editor_support::draw_preview_geom(geom, through_instance);
    }
    void end_preview_view() override { editor_support::end_preview_view(); }
};

} // namespace

Device& device()
{
    static core::NoDestroy<D3d9Device> made;
    return made.get();
}

} // namespace render::d3d9

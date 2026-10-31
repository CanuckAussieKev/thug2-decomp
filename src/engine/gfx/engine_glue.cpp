// The engine's platform calls: see engine_glue.h.

#include "engine/gfx/engine.h"
#include "engine/gfx/d3dx.h"
#include "engine/gfx/pc_gamma.h"
#include "engine/gfx/engine_glue.h"

#include <windows.h>

#include <d3d9.h>

#include <share.h>

#include <cstdint>
#include <cstdio>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/device.h"
#include "engine/gfx/engine_sprite.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_model.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/pc_sprite.h"
#include "engine/gfx/pc_texture.h"
#include "engine/gfx/pc_weather.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/settings.h"
#include "engine/gfx/shadow_base.h"
#include "engine/gfx/bitmap_texture.h"
#include "engine/maths/vec4.h"
#include "engine/core/code_address.h"
#include "engine/gfx/pc_particles.h"
#include "engine/objects/particle_component.h"

namespace render {

namespace {
extern void** const kCPcParticleManagerVtable;
} // namespace

bool g_loading_screen_up; // the loading screen is up: it draws and presents on its own
void* g_weather;

void* g_screen_effects;
uint32_t g_fog_colour;
uint32_t g_screen_blur_frames;
uint32_t g_screen_blur;
bool g_screen_shot_name_exact;
char g_pending_screen_shot[128]; // the screenshot asked for by name
namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
uint32_t g_mem_view_toggle;

// The loading screen is up (0x00786D04, show_loading_screen): it draws and presents on its own.
bool loading_screen_up() { return g_loading_screen_up; }
// Rendering skipped (frames not drawn or shown).
bool omit_from_draw() { return g_suppress_draw; }

// The screenshot asked for by name, taken once the frame is shown (0x00786D94, 0x80 characters), and whether that
// name is used as it is (0x00786E14) rather than numbered.
char* pending_screen_shot() { return g_pending_screen_shot; }
bool& screen_shot_name_exact() { return g_screen_shot_name_exact; }

// The clear colour: the fog's with fog and distance clipping on, else the background's.
uint32_t back_buffer_fill()
{
    if (g_fog_setting && g_distance_clipping)
        return g_fog_colour;
    return g_camera.background_colour_;
}

// D3DX (linked library code): a surface written to an image file (format 1: JPEG).
long save_surface_to_file(const char* file, IDirect3DSurface9* surface)
{
    return d3dx::save_surface_to_jpg(file, surface);
}

// A shadow caster's key: the texture's (+0x10, through one more pointer with flag 1 of +0x08).
uint8_t* caster_key(const uint8_t* texture)
{
    uint8_t* key = *reinterpret_cast<uint8_t* const*>(texture + 0x10);
    if (texture[8] & 1)
        key = *reinterpret_cast<uint8_t**>(key);
    return key;
}

} // namespace

// 0x004B2270
void __cdecl toggle_mem_view() { g_mem_view_toggle ^= 1; }

// 0x004B2280 (the device 0x007D69E0's BeginScene, vtable slot 41; not while the loading screen is up: 0x00786D04)
void __cdecl begin_frame_draw()
{
    if (g_loading_screen_up)
        return;
    void* device = g_d3d_device;
    reinterpret_cast<long(__stdcall*)(void*)>((*static_cast<void***>(device))[41])(device);
}

// 0x004B22A0
void __cdecl delete_object(void* object)
{
    if (object)
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(object))[0])(object, 1);
}

// 0x004B22B0
void __cdecl set_screen_clip(const void* rect) { set_skin_scaling(static_cast<const uint32_t*>(rect)); }

// 0x004B22C0
void __cdecl add_debug_shape(int type, const maths::Vec4* points, uint32_t colour)
{
    if (type == 4)
        insert_occluder_poly(points, points + 1, points + 2, points + 3, colour);
}

// 0x004B22F0
void __cdecl flag_debug_lines(uint32_t id, bool flag) { activate_occluder_poly(id, flag); }

// 0x004B2300
void __cdecl clear_debug_lines() { clear_occluder_polys(); }

// 0x004B2310
int __cdecl sphere_visible(const maths::Vec4* centre, float radius) { return sphere_outside_view(centre, radius); }

// 0x004B2320
const char* __cdecl platform_suffix() { return "Xbx"; }

// 0x004B24C0
void __cdecl set_gamma(float red, float green, float blue) { set_device_gamma(red, green, blue); }

// 0x004B24D0
void __cdecl get_gamma(float* red, float* green, float* blue) { get_device_gamma(red, green, blue); }

// 0x004B24E0
void __cdecl screen_shot(const char* name, int /*unused*/)
{
    // (the original sprintfs into these 32 bytes; ours truncates a longer name)
    char file[32];
    if (screen_shot_name_exact()) {
        std::snprintf(file, sizeof(file), "%s.jpg", name);
    } else {
        // the first numbered name with no file yet
        int number = 0;
        std::snprintf(file, sizeof(file), "%s%03d.jpg", name, number);
        while (FILE* existing = _fsopen(file, "rb", _SH_DENYNO)) { // (fopen)
            std::fclose(existing);
            ++number;
            std::snprintf(file, sizeof(file), "%s%03d.jpg", name, number);
        }
    }
    D3DDISPLAYMODE mode;
    input_port()->GetDisplayMode(0, &mode);
    IDirect3DSurface9* surface = nullptr;
    input_port()->CreateOffscreenPlainSurface(mode.Width, mode.Height, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &surface,
                                              nullptr);
    input_port()->GetFrontBufferData(0, surface);
    save_surface_to_file(file, surface);
    surface->Release();
}

// 0x004B25F0
void __cdecl start_engine()
{
    init_direct_3d();
    void* memory = memory::alloc(0x1C);
    void* particles = nullptr;
    if (memory) {
        particles = static_cast<EffectsDirector*>(memory)->make_new();
        *static_cast<void**>(particles) = reinterpret_cast<void*>(kCPcParticleManagerVtable);
    }
    reinterpret_cast<void*&>(objects::g_particle_hub) = particles;
    memory = memory::alloc(0x12AC);
    g_weather = memory ? static_cast<PcWeather*>(memory)->make_new() : nullptr;
    memory = memory::alloc(8);
    g_screen_effects = memory ? static_cast<PcScreenEffects*>(memory)->make_new() : nullptr;
}

// 0x004B2710
PcParticleManager* PcParticleManager::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004B2740
void PcParticleManager::destroy_body() { reinterpret_cast<EffectsDirector*>(this)->destroy_body(); }

// 0x004B2750
void __cdecl finish_rendering()
{
    if (loading_screen_up())
        return;
    input_port()->EndScene();
    if (omit_from_draw())
        return;
    input_port()->Present(nullptr, nullptr, nullptr, nullptr);
    g_draw_calls = 0; // the frame's draw counts
    g_drawn_triangles = 0;
    if (pending_screen_shot()[0]) {
        screen_shot(pending_screen_shot(), 0);
        pending_screen_shot()[0] = 0;
        screen_shot_name_exact() = false;
    }
    // The next frame's clear: with the screen blur on, colour and depth (and the blur's frames counted, from 0
    // again once it's off); letterboxed, to black; with 0x00786CBD set, colour and depth; else the depth alone.
    if (g_screen_blur != 0) {
        if (!omit_from_draw())
            input_port()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, back_buffer_fill(), 1.0f, 0);
        ++g_screen_blur_frames;
        return;
    }
    if (g_camera.letterbox_active_) {
        if (!omit_from_draw())
            clear_device(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    } else if (g_camera.clear_colour_and_depth_) {
        if (!omit_from_draw())
            clear_device(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, back_buffer_fill(), 1.0f, 0);
    } else if (!omit_from_draw()) {
        clear_device(0, nullptr, D3DCLEAR_ZBUFFER, g_camera.background_colour_, 1.0f, 0);
    }
    g_screen_blur_frames = 0;
}

// 0x004B28C0
void* __cdecl make_empty_scene(const char* /*name*/, void* /*textures*/, bool with_super_sectors)
{
    void* memory = memory::alloc(0x284);
    uint8_t* scene = memory ? reinterpret_cast<uint8_t*>(static_cast<PcScene*>(memory)->make_new(10)) : nullptr;
    // (no check: a failed allocation writes through null, as the original)
    scene[0x8C] = with_super_sectors;
    scene[0x8D] = 0;
    memory = memory::alloc(sizeof(SceneRef));
    *reinterpret_cast<SceneRef**>(scene + 0x280) = memory ? static_cast<SceneRef*>(memory)->make_new() : nullptr;
    return scene;
}

// 0x004B2990
bool __cdecl unload_scene(void* scene)
{
    auto* bytes = static_cast<uint8_t*>(scene);
    static_cast<PcScene*>(scene)->unload_sectors();
    discard_scene(*reinterpret_cast<SceneRef**>(bytes + 0x280));
    *reinterpret_cast<SceneRef**>(bytes + 0x280) = nullptr;
    return true;
}

// 0x004B29C0
void* __cdecl spawn_model()
{
    void* memory = memory::alloc_zeroed(0x50);
    return memory ? static_cast<PcModel*>(memory)->make_new() : nullptr;
}

// 0x004B2A20
void* __cdecl create_geom()
{
    void* memory = memory::alloc_zeroed(0x64);
    return memory ? static_cast<PcGeom*>(memory)->make_new() : nullptr;
}

// 0x004B2A80
void* __cdecl build_sprite_quad()
{
    void* memory = memory::alloc(0x48);
    return memory ? static_cast<PcSprite*>(memory)->make_new() : nullptr;
}

// 0x004B2AF0
void* __cdecl create_textured_quad()
{
    void* memory = memory::alloc(0x20);
    return memory ? static_cast<TexturedQuad*>(memory)->make_new(nullptr) : nullptr;
}

// 0x004B2B60
void* __cdecl create_render_target(int width, int height, uint8_t depth, int unused)
{
    void* memory = memory::alloc(0x14);
    PcTexture* target = memory ? static_cast<PcTexture*>(memory)->make_new() : nullptr;
    memory = memory::alloc(sizeof(TextureRef));
    TextureRef* texture = memory ? static_cast<TextureRef*>(memory)->make_new() : nullptr;
    target->set_texture(texture);
    // (no check: a failed allocation calls through null, as the original)
    texture->create_render_target(width, height, depth, unused);
    return target;
}

// 0x004B2C30
void __cdecl add_texture_shadow_caster(const uint8_t* texture, void* model, uint32_t /*unused*/)
{
    add_shadow_caster(caster_key(texture), model, 0);
}

// 0x004B2C50
void __cdecl aim_texture_shadow_caster(const uint8_t* texture, gfx::ViewCam* camera)
{
    const maths::Vec4& pos = *camera->fetch_location();
    float position[3] = {pos.x, pos.y, pos.z};
    const maths::Mat4& m = *camera->fetch_mat();
    // the point it looks at: the position plus the matrix's third row
    float target[3] = {m.row[2].x + position[0], m.row[2].y + position[1], m.row[2].z + position[2]};
    aim_shadow_caster(reinterpret_cast<uint32_t>(caster_key(texture)), position, target);
}

// 0x004B2D10
void __cdecl remove_texture_shadow_caster(const uint8_t* texture)
{
    remove_shadow_caster(reinterpret_cast<uint32_t>(caster_key(texture)));
}

namespace {
static VtableSlot g_c_pc_particle_manager_vtable_slots[] = {&render::PcParticleManager::teardown,
                                                            render::PcParticleSystem::fabricate};
void** const kCPcParticleManagerVtable = reinterpret_cast<void**>(g_c_pc_particle_manager_vtable_slots);
} // namespace

} // namespace render

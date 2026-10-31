// The engine's own entry points (render::Engine): see engine.h. From the disassembly of 0x0048BF20-0x0048C741.

#include "engine/gfx/engine.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_imposters.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/pc_font.h"
#include "engine/gfx/pc_lights.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/scene_lights.h"
#include "engine/gfx/scene_load.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/spawned_polys.h"
#include "engine/gfx/viewport.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/objects/particle_component.h"
#include "game/skate/skate_module.h"
#include "engine/sys/startup_init.h"
#include "engine/gfx/weather.h"

#pragma warning(disable : 4996) // (sprintf / strcpy: the original's file names, unbounded as it builds them)

namespace render {

bool g_suppress_draw; // rendering skipped (frames not drawn or shown)

void* g_flexible_particle_manager;
void* g_imposter_manager;
int g_render_mode;
// The scenes' list (its head: data -1 once made; its first node, the head's next, is at +0xC, the last at +0x10).
memory::ListNode g_scene_list;
namespace {

extern void** const kSceneNodeVtable;

constexpr uint32_t kCameraFov = 0x99529205;                     // camera_fov
constexpr float kAspectRatio = 1.33333337f;                     // 0x3FAAAAAB (4:3)
const char* const kSceneFileFormat = "levels\\%s\\%s%s.scn.%s"; // 0x00648A20 (the third: "", 0x006469B4)
const char* const kModelFileFormat = "%s.%s.%s";                // 0x006458F4

// The scenes' list (0x006FA7E0: its head, data -1; the first node at +0xC, the last at +0x10).
memory::ListNode& scene_list() { return g_scene_list; }
const auto kNoScene = reinterpret_cast<void*>(static_cast<uintptr_t>(0xFFFFFFFF));
// The node after `node`, null at the end (the head).
SceneNode* next_scene(memory::ListNode* node)
{
    return node->next->data == kNoScene ? nullptr : static_cast<SceneNode*>(node->next);
}
// The first node (null when there's none - the original reads the head's data before checking for no list).
SceneNode* first_scene()
{
    memory::ListNode* first = scene_list().next;
    if (first->data == kNoScene || !first)
        return nullptr;
    return static_cast<SceneNode*>(first);
}
WorldScene* scene_of(memory::ListNode* node) { return static_cast<WorldScene*>(node->data); }

void* g_scene_objects;
int g_render_ignore0;
int g_render_ignore1;
bool g_stats_overlay_on;
engine::IdleFunction g_idle_function;

// The engine's managers: the flexible particle manager (0x006FA7C0), the imposter manager (0x006FA7BC, 8 bytes:
// constructor 0x0048E3E0, its frame 0x0048E2D0 - not rewritten yet), the particle manager and the weather (made by
// start_engine).
void*& flexible_particle_manager() { return g_flexible_particle_manager; }
void*& imposter_manager() { return g_imposter_manager; }
EffectsDirector* particle_hub_handle() { return objects::g_particle_hub; }
WeatherSystem* get_weather() { return g_weather; }
bool omit_from_draw() { return g_suppress_draw; }
int viewport_count() { return modules::g_viewport_count; }

// (0x004904E0, not rewritten yet: the frame's ... after the imposters)
void update_after_imposters() { update_scene_light_queue(); }

// The compiled-out call (0x005480A0, no arguments) delete_geom_object and UnloadScene make first.
inline void debug_before_delete() {}

} // namespace

// 0x0048C4B0
void SceneNode::destroy_body() { unlink_as(kSceneNodeVtable); }

// 0x0048C4D0
SceneNode* SceneNode::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

namespace engine {

// 0x0048C1D0
void __cdecl prepare_all(uint32_t text_pool_size)
{
    memory::push_mem_profile("sAllocTextPool");
    allocate_text_pool(text_pool_size);
    memory::pop_mem_profile();
    memory::push_mem_profile("sAllocWindow2DPool");
    create_window_pool();
    memory::pop_mem_profile();
    set_screen_aspect_ratio(kAspectRatio);
    set_screen_angle(script::fetch_float(kCameraFov));
    set_screen_field_of_view(0.0f);
    memory::push_mem_profile("CViewportManager");
    create_viewports();
    set_screen_mode(0);
    memory::pop_mem_profile();
    memory::push_mem_profile("CFlexibleParticleManager");
    flexible_particle_manager() = new FlexibleParticleManager;
    memory::pop_mem_profile();
    memory::push_mem_profile("CImposterManager");
    void* memory = memory::alloc(8);
    imposter_manager() = memory ? static_cast<ImposterManager*>(memory)->make_new() : nullptr;
    memory::pop_mem_profile();
    memory::push_mem_profile("MiscFXInitialize");
    init_effects();
    memory::pop_mem_profile();
    start_engine();
}

// 0x0048BF20
void __cdecl end_render()
{
    finish_rendering();
    purge_doomed_cameras();
}

// 0x0048C330
void __cdecl render_frame()
{
    update_flashes();
    update_spawned_polys();
    age_all_texture_splats();
    update_shatters();
    particle_hub_handle()->tick_particles();
    // the weather's frame (through the folded jump 0x004E3F90), by the slow-motion frame time
    get_weather()->tick_frame(static_cast<float>(frame_clock::get_slomo_frame_seconds()));
    static_cast<ImposterManager*>(imposter_manager())->tick_frame();
    update_after_imposters();
    if (!omit_from_draw())
        draw_world();
}

// 0x0048C0A0
void __cdecl change_entity_hub(void* manager) { g_scene_objects = manager; }

// 0x0048C0B0
uint32_t* __cdecl get_collision_scenes() { return reinterpret_cast<uint32_t*>(g_collision_scenes); }

// 0x0048C0D0
void __cdecl toggle_render_mode()
{
    int& mode = g_render_mode;
    if (++mode == 4)
        mode = 0;
}

// 0x0048C0F0
void __cdecl toggle_metrics()
{
    bool& on = g_stats_overlay_on;
    on = !on;
}

// 0x0048C110
void __cdecl set_render_ignore(int ignore1, int ignore0)
{
    g_render_ignore0 = ignore0;
    g_render_ignore1 = ignore1;
}

// 0x0048C1C0
IdleFunction __cdecl get_idle_function() { return g_idle_function; }

// 0x0048BFE0
bool __cdecl split_skin_name(const char* name, char* base)
{
    std::strcpy(base, name);
    char* skin = std::strstr(base, ".skin");
    bool found = skin != nullptr;
    if (found)
        *skin = '\0';
    if (char* model = std::strstr(base, ".mdl"))
        *model = '\0';
    return found;
}

// 0x0048C030
void* __cdecl load_model_from_memory(uint32_t checksum, const uint8_t* data, int size, void* skin, void* textures, int,
                                     uint32_t, uint32_t)
{
    memory::push_mem_profile("model_data_buffer");
    void* model = load_mesh_from_memory(checksum, data, size, skin, textures);
    memory::pop_mem_profile();
    return model;
}

// 0x0048C390
void __cdecl build_scene_file_path(char* out, const char* name, uint32_t)
{
    std::sprintf(out, kSceneFileFormat, name, name, "", platform_suffix());
}

// 0x0048C3C0
bool __cdecl delete_geom_object(void* object)
{
    debug_before_delete();
    return delete_model_lights(static_cast<PcModelLights*>(object));
}

// 0x0048C3D0
void* __cdecl load_model_file(const char* file_name, void* textures, int, bool)
{
    char base[0x200];
    bool skinned = split_skin_name(file_name, base);
    text::to_lower_text(base);
    char path[0x200];
    std::sprintf(path, kModelFileFormat, base, skinned ? "skin" : "mdl", platform_suffix());
    memory::push_mem_profile(path);
    // (the original also passes the texture dictionary offset, whether it's skinned and the shadow volume flag: the
    // mesh load doesn't read them)
    void* mesh = load_mesh_file(path, textures);
    memory::pop_mem_profile();
    return mesh;
}

// 0x0048C490
int __cdecl sphere_visible_in_one_viewport(const maths::Vec4* centre, float radius)
{
    if (viewport_count() > 1)
        return 0;
    return sphere_visible(centre, radius);
}

// 0x0048C510
void* __cdecl look_up_sector(uint32_t checksum)
{
    for (SceneNode* node = first_scene(); node;) {
        SceneNode* next = next_scene(node);
        if (void* sector = scene_of(node)->look_up_sector(checksum))
            return sector;
        node = next;
    }
    return nullptr;
}

// 0x0048C560
void __cdecl add_scene(WorldScene* scene, const char* name, void* textures, bool loaded, void* arena)
{
    char file[0x80];
    std::sprintf(file, kSceneFileFormat, name, name, "", platform_suffix());
    scene->checksum_ = checksums::hash_from_text(name);
    scene->texture_set_ = static_cast<TextureSet*>(textures);
    if (!loaded)
        scene->finish_load(file, arena);
    memory::push_mem_profile("Asset Overhead");
    auto* node = static_cast<SceneNode*>(memory::alloc_zeroed(sizeof(SceneNode)));
    if (node) {
        node->vtable = kSceneNodeVtable;
        node->data = scene;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    // at the end of the list (no memory: a crash, as the original)
    memory::ListNode& head = scene_list();
    node->prev = head.prev;
    node->next = &head;
    head.prev->next = node;
    head.prev = node;
    memory::pop_mem_profile();
}

// 0x0048C650
void __cdecl toggle_add_scenes()
{
    for (SceneNode* node = first_scene(); node;) {
        SceneNode* next = next_scene(node);
        scene_of(node)->flip_extra_scene();
        node = next;
    }
}

// 0x0048C690
bool __cdecl unload_scene(WorldScene* scene, int /*flags*/)
{
    debug_before_delete();
    scene->unload_added_scene();
    if (!render::unload_scene(scene)) // (the platform's: the original passes `flags` on too - unused)
        return false;
    if (scene)
        reinterpret_cast<void*(__thiscall*)(WorldScene*, unsigned int)>((*reinterpret_cast<void***>(scene))[0])(scene,
                                                                                                                1);
    for (SceneNode* node = first_scene(); node;) {
        SceneNode* next = next_scene(node);
        if (scene_of(node) == scene) {
            reinterpret_cast<void*(__thiscall*)(SceneNode*, unsigned int)>(node->vtable[0])(node, 1);
            return true;
        }
        node = next;
    }
    return false;
}

// 0x0048C700
WorldScene* __cdecl find_scene(uint32_t checksum)
{
    for (SceneNode* node = first_scene(); node;) {
        SceneNode* next = next_scene(node);
        if (scene_of(node)->checksum_ == checksum)
            return scene_of(node);
        node = next;
    }
    return nullptr;
}

} // namespace engine

namespace {
static VtableSlot g_scene_node_vtable_slots[] = {&render::SceneNode::teardown};
void** const kSceneNodeVtable = reinterpret_cast<void**>(g_scene_node_vtable_slots);
} // namespace

} // namespace render

namespace render {

// 0x00640B00 (a static initialiser): the scene list, an empty head.
void construct_scene_list() { reinterpret_cast<SceneListHead*>(&g_scene_list)->make_new(); }

} // namespace render

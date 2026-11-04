// The engine's own entry points (render::Engine): see engine.h. From the disassembly of 0x0048BF20-0x0048C741.

#include "engine/gfx/engine.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>

#include "base/core/checksums.h"
#include "engine/memory/mem.h"
#include "base/core/str.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_imposters.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/pc_font.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/scene_lights.h"
#include "engine/gfx/scene_load.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/spawned_polys.h"
#include "engine/gfx/viewport.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/objects/particle_component.h"
#include "game/skate/skate_module.h"
#include "engine/sys/startup_init.h"
#include "engine/gfx/weather.h"
#include <gsl/pointers>
#include <array>
#include "base/core/compiler.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/format.h"
#include "platform/crt.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's file names, unbounded as it builds them)

namespace render {

constexpr int kSmallBlockBytes = 8;
constexpr std::size_t kEnginePathSize = 0x200;
constexpr std::size_t kEngineFileNameSize = 0x80;

FlexibleParticleManager* flexible_particle_manager() { return engine_parts().flexible_particle_manager().get(); }
ImposterManager* imposter_manager() { return engine_parts().imposter_manager().get(); }

namespace {

constexpr uint32_t kCameraFov = 0x99529205;                     // camera_fov
constexpr float kAspectRatio = 1.33333337F;                     // 0x3FAAAAAB (4:3)
constexpr const char* kSceneFileFormat = "levels\\%s\\%s%s.scn.%s"; // 0x00648A20 (the third: "", 0x006469B4)
constexpr const char* kModelFileFormat = "%s.%s.%s";                // 0x006458F4

// The scenes' list (0x006FA7E0: its head, data -1; the first node at +0xC, the last at +0x10).

const auto kNoScene = memory::ListNode::head_marker();
// The node after `node`, null at the end (the head).
memory::ListNode* next_scene(memory::ListNode* node) { return node->next()->data() == kNoScene ? nullptr : node->next(); }
// The first node (null when there's none - the original reads the head's data before checking for no list).
memory::ListNode* first_scene()
{
    memory::ListNode* first = engine_parts().scene_list().next();
    if (first->data() == kNoScene || !first) {
        return nullptr;
    }
    return first;
}
WorldScene* scene_of(memory::ListNode* node) { return static_cast<WorldScene*>(node->data()); }

// NOT ORIGINAL grouping - engine.cpp's state (the original's globals). Made on first use, never destroyed
// (engine_state()).
struct EngineState {
    void* scene_objects_{};
    int render_ignore0_{};
    int render_ignore1_{};
    bool stats_overlay_on_{};
    engine::IdleFunction idle_function_{};
};

EngineState& engine_state()
{
    static core::NoDestroy<EngineState> state;
    return state.get();
}

// The engine's managers: the flexible particle manager (0x006FA7C0), the imposter manager (0x006FA7BC, 8 bytes:
// constructor 0x0048E3E0, its frame 0x0048E2D0 - not rewritten yet), the particle manager and the weather (made by
// start_engine).

// (0x004904E0, not rewritten yet: the frame's ... after the imposters)
void update_after_imposters() { update_scene_light_queue(); }

// The compiled-out call (0x005480A0, no arguments) delete_geom_object and UnloadScene make first.
inline void debug_before_delete() {}

} // namespace

namespace engine {

// 0x0048C1D0
void prepare_all(uint32_t text_pool_size)
{
    memory::push_mem_profile("sAllocTextPool");
    allocate_text_pool(text_pool_size);
    memory::pop_mem_profile();
    memory::push_mem_profile("sAllocWindow2DPool");
    create_window_pool();
    memory::pop_mem_profile();
    set_screen_aspect_ratio(kAspectRatio);
    set_screen_angle(script::fetch_float(kCameraFov));
    set_screen_field_of_view(0.0F);
    memory::push_mem_profile("CViewportManager");
    create_viewports();
    set_screen_mode(0);
    memory::pop_mem_profile();
    memory::push_mem_profile("CFlexibleParticleManager");
    engine_parts().flexible_particle_manager() = std::make_unique<FlexibleParticleManager>();
    memory::pop_mem_profile();
    memory::push_mem_profile("CImposterManager");
    void* memory = memory::alloc(kSmallBlockBytes);
    engine_parts().imposter_manager() = memory::HeapUnique<ImposterManager>(memory ? memory::construct_in<ImposterManager>(memory) : nullptr);
    memory::pop_mem_profile();
    memory::push_mem_profile("MiscFXInitialize");
    init_effects();
    memory::pop_mem_profile();
    start_engine();
}

// 0x0048BF20
void end_render()
{
    finish_rendering();
    purge_doomed_cameras();
}

// 0x0048C330
void render_frame()
{
    update_flashes();
    update_spawned_polys();
    age_all_texture_splats();
    update_shatters();
    objects::particle_hub()->tick_particles();
    // the weather's frame (through the folded jump 0x004E3F90), by the slow-motion frame time
    weather()->tick_frame(static_cast<float>(frame_clock::get_slomo_frame_seconds()));
    imposter_manager()->tick_frame();
    update_after_imposters();
    if (!engine_parts().suppress_draw()) {
        draw_world();
    }
}

// 0x0048C0A0
void change_entity_hub(void* manager) { engine_state().scene_objects_ = manager; }

// 0x0048C0B0

// 0x0048C0D0
void toggle_render_mode()
{
    auto& parts = engine_parts();
    parts.set_render_mode(parts.render_mode() + 1);
    if (parts.render_mode() == 4) {
        parts.set_render_mode(0);
    }
}

// 0x0048C0F0
void toggle_metrics()
{
    bool& on = engine_state().stats_overlay_on_;
    on = !on;
}

// 0x0048C110
void set_render_ignore(int ignore1, int ignore0)
{
    engine_state().render_ignore0_ = ignore0;
    engine_state().render_ignore1_ = ignore1;
}

// 0x0048C1C0
IdleFunction get_idle_function() { return engine_state().idle_function_; }

// 0x0048BFE0
bool split_skin_name(const char* name, std::span<char> base)
{
    text::copy_into(base, name);
    char* skin = std::strstr(base.data(), ".skin");
    const bool found = skin != nullptr;
    if (found) {
        *skin = '\0';
    }
    if (char* model = std::strstr(base.data(), ".mdl")) {
        *model = '\0';
    }
    return found;
}

// 0x0048C030
gsl::owner<PcMesh*> load_model_from_memory(uint32_t checksum, const uint8_t* data, int size, const void* skin,
                                           size_t skin_size, TextureSet* textures, int /*unused*/, uint32_t /*unused*/,
                                           uint32_t /*unused*/)
{
    memory::push_mem_profile("model_data_buffer");
    gsl::owner<PcMesh*> model = load_mesh_from_memory(checksum, data, static_cast<size_t>(size), skin, skin_size, textures);
    memory::pop_mem_profile();
    return model;
}

// 0x0048C390
void build_scene_file_path(char* out, const char* name, uint32_t /*unused*/)
{
    crt::get_sprintf(out, kSceneFileFormat, name, name, "", platform_suffix());
}

// 0x0048C3D0
gsl::owner<PcMesh*> load_model_file(const char* file_name, TextureSet* textures, int /*unused*/, bool /*unused*/)
{
    std::array<char, kEnginePathSize> base{};
    const bool skinned = split_skin_name(file_name, base);
    text::to_lower_text(base.data());
    std::array<char, kEnginePathSize> path{};
    text::format_to(path, kModelFileFormat, base.data(), skinned ? "skin" : "mdl", platform_suffix());
    memory::push_mem_profile(path.data());
    // (the original also passes the texture dictionary offset, whether it's skinned and the shadow volume flag: the
    // mesh load doesn't read them)
    gsl::owner<PcMesh*> mesh = load_mesh_file(path.data(), textures);
    memory::pop_mem_profile();
    return mesh;
}

// 0x0048C490
int sphere_visible_in_one_viewport(const maths::Vec4* centre, float radius)
{
    if (modules::viewport_count() > 1) {
        return 0;
    }
    return sphere_visible(centre, radius);
}

// 0x0048C510
WorldCell* look_up_sector(uint32_t checksum)
{
    for (memory::ListNode* node = first_scene(); node;) {
        memory::ListNode* next = next_scene(node);
        if (WorldCell* sector = scene_of(node)->look_up_sector(checksum)) {
            return sector;
        }
        node = next;
    }
    return nullptr;
}

// 0x0048C560
void add_scene(WorldScene* scene, const char* name, void* textures, bool loaded, void* arena)
{
    std::array<char, kEngineFileNameSize> file{};
    text::format_to(file, kSceneFileFormat, name, name, "", platform_suffix());
    scene->set_checksum(checksums::hash_from_text(name));
    scene->set_texture_set(static_cast<TextureSet*>(textures));
    if (!loaded) {
        scene->finish_load(file.data(), arena);
    }
    memory::push_mem_profile("Asset Overhead");
    memory::ListNode* node = memory::ListNode::create(scene);
    // at the end of the list (no memory: a crash, as the original)
    memory::ListNode& head = engine_parts().scene_list();
    node->set_prev(head.prev());
    node->set_next(&head);
    head.prev()->set_next(node);
    head.set_prev(node);
    memory::pop_mem_profile();
}

// 0x0048C650
void toggle_add_scenes()
{
    for (memory::ListNode* node = first_scene(); node;) {
        memory::ListNode* next = next_scene(node);
        scene_of(node)->flip_extra_scene();
        node = next;
    }
}

// 0x0048C690
bool unload_scene(WorldScene* scene, int /*flags*/)
{
    debug_before_delete();
    scene->unload_added_scene();
    if (!render::unload_scene(scene)) { // (the platform's: the original passes `flags` on too - unused)
        return false;
    }
    gsl::owner<WorldScene*> owned = memory::owned_element(scene); // (the engine's scene list's)
    delete owned;
    for (memory::ListNode* node = first_scene(); node;) {
        memory::ListNode* next = next_scene(node);
        if (scene_of(node) == scene) {
            memory::ListNode::destroy(node); // (0x0048C4D0)
            return true;
        }
        node = next;
    }
    return false;
}

// 0x0048C700
WorldScene* find_scene(uint32_t checksum)
{
    for (memory::ListNode* node = first_scene(); node;) {
        memory::ListNode* next = next_scene(node);
        if (scene_of(node)->checksum() == checksum) {
            return scene_of(node);
        }
        node = next;
    }
    return nullptr;
}

} // namespace engine

} // namespace render

namespace render {

// 0x00640B00 (a static initialiser): the scene list, an empty head.
void construct_scene_list() { new (&engine_parts().scene_list()) SceneListHead; }

} // namespace render

// The engine's loaded scenes: see engine_scene_list.h.

#include "engine/gfx/engine_scene_list.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <span>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "engine/memory/mem.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/scene_load.h"
#include "engine/gfx/texture_dict.h"
#include "base/maths/shape_tests.h"
#include "base/maths/vec4.h"
#include "engine/render/coll_grid.h"
#include "engine/script/param_struct.h"
#include "engine/sys/stopwatch.h"
#include "engine/memory/arena_alloc.h"
#include "engine/gfx/pc_scene.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/render_device.h"

namespace render {

constexpr std::size_t kCollisionGridCount = 16;
constexpr int kMaxGridsAlongLine = 15; // the lists are null-terminated in 16 entries
constexpr uint32_t kMainScene = 0x9B10525CU; // mainscene
constexpr uint32_t kSkyScene = 0x02C5EA34U;  // skyscene
constexpr std::size_t kSceneFileNameSize = 0x80;

namespace {

// The list's first node (the head's next; the head itself when the list is empty).
memory::ListNode* head_of_scene_list() { return engine_parts().scene_list().next(); }
bool is_head(const memory::ListNode* node) { return node->data() == memory::ListNode::head_marker(); }
// The node after `node` (null at the end).
memory::ListNode* get_next_node(const memory::ListNode* node)
{
    memory::ListNode* next = node->next();
    return is_head(next) ? nullptr : next;
}
WorldScene* scene_of(const memory::ListNode* node) { return static_cast<WorldScene*>(node->data()); }

// The first scene whose is_sky_ is `sky` (the original's walk: the first node's data checked before the node).
WorldScene* first_scene(bool sky)
{
    const memory::ListNode* node = head_of_scene_list();
    if (is_head(node) || !node) {
        return nullptr;
    }
    for (;;) {
        const memory::ListNode* next = get_next_node(node);
        if (scene_of(node)->is_sky() == sky) {
            return scene_of(node);
        }
        if (!next) {
            return nullptr;
        }
        node = next;
    }
}

// The rest of the engine's scene calls (0x0048C560-0x0048C74F, not rewritten yet; cdecl): a scene registered (its
// name's checksum, its textures, PostLoad unless `skip_post_load`, a node at the end of the list; the sixth argument
// isn't read), a scene unloaded (with `flag` its collision too), the scene of a checksum, every scene's added scene
// shown or hidden.
void register_scene(WorldScene* scene, const char* name, TextureSet* textures, int skip_post_load, void* arena,
                    int unused)
{
    (void)unused;
    engine::add_scene(scene, name, textures, skip_post_load != 0, arena);
}
void unload_engine_scene(WorldScene* scene, int flag) { engine::unload_scene(scene, flag); }
WorldScene* look_up_scene(uint32_t checksum) { return engine::find_scene(checksum); }
void toggle_add_scenes() { engine::toggle_add_scenes(); }

// Compiled out (0x005B9E50, false): the platform's own load of an added scene's geometry.
bool plat_load_add_scene(WorldScene* /*scene*/, const char* /*fileName*/) { return false; }
// "levels\<name>\<name>.scn.<the platform's extension>" (the original's sprintf; no length limit there).
void build_scene_file_path(char* out, size_t size, const char* name)
{
    text::format_to(std::span(out, size), "levels\\%s\\%s%s.scn.%s", name, name, "", platform_suffix());
}

} // namespace

// 0x0048CC30
SceneListHead::SceneListHead() : ListNode() { make_head(); }

// 0x0048C7F0
memory::ListNode* first_scene_node()
{
    memory::ListNode* node = head_of_scene_list();
    return is_head(node) ? nullptr : node;
}

memory::ListNode* next_scene_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next();
    return next->is_head() ? nullptr : next;
}

WorldScene* scene_of_node(const memory::ListNode* node) { return static_cast<WorldScene*>(node->data()); }

// 0x0048C750
WorldScene* get_scene_by_name(const char* name) { return look_up_scene(checksums::hash_from_text(name)); }

// 0x0048C770
WorldScene* get_main_scene() { return first_scene(false); }

// 0x0048C7B0
WorldScene* get_sky_scene() { return first_scene(true); }

// 0x0048C810
bool unload_all_scenes()
{
    const memory::ListNode* node = head_of_scene_list();
    if (!is_head(node) && node) {
        for (bool again = true; again; again = (node != nullptr)) {
            const memory::ListNode* next = get_next_node(node);
            WorldScene* scene = scene_of(node);
            TextureSet* textures = scene->texture_set();
            unload_engine_scene(scene, 1);
            if (textures) {
                unload_tex_dict(textures);
            }
            node = next;
        }
    }
    clear_debug_lines(); // (the original pushes a 0 it doesn't read)
    return true;
}

// 0x0048C870
void add_shadow_caster(const BitmapTexture* texture, void* model, WorldScene* scene)
{
    if (!scene) {
        scene = get_main_scene();
    }
    device().add_texture_shadow_caster(texture, model, scene);
}

// 0x0048C8F0
void print_coll_statics()
{
    frame_clock::get_time();
    const memory::ListNode* node = head_of_scene_list();
    if (!is_head(node) && node) {
        for (bool again = true; again; again = (node != nullptr)) {
            const memory::ListNode* next = get_next_node(node);
            WorldScene* scene = scene_of(node);
            if (!scene->is_sky()) {
                scene->print_coll_statics();
            }
            node = next;
        }
    }
    frame_clock::get_time();
}

// 0x0048C940
CollGrid* get_first_coll_grid()
{
    const memory::ListNode* node = head_of_scene_list();
    if (is_head(node) || !node) {
        return nullptr;
    }
    for (bool again = true; again; again = (node != nullptr)) {
        const memory::ListNode* next = get_next_node(node);
        if (CollGrid* grid = scene_of(node)->coll_grid()) {
            return grid;
        }
        node = next;
    }
    return nullptr;
}

namespace {
// NOT ORIGINAL grouping - engine_scene_list.cpp's state (the original's globals). Made on first use, never destroyed
// (engine_scene_list_state()).
struct EngineSceneListState {
    uint32_t next_sector_name_{1};
    std::array<CollGrid*, kCollisionGridCount> collision_grids_{};
};

EngineSceneListState& engine_scene_list_state()
{
    static core::NoDestroy<EngineSceneListState> state;
    return state.get();
}

} // namespace

// 0x0048C980
std::span<CollGrid* const> get_coll_grids_on_line(const maths::Seg* line)
{
    auto& scenes = engine_parts().collision_scenes();
    auto& grids = engine_scene_list_state().collision_grids_;
    maths::BBox box(line->start);
    box.append_spot(line->end);
    uint32_t count = 0;
    const memory::ListNode* node = head_of_scene_list();
    if (is_head(node) || !node) {
        return {};
    }
    for (bool again = true; again; again = (node != nullptr)) {
        const memory::ListNode* next = get_next_node(node);
        WorldScene* scene = scene_of(node);
        CollGrid* grid = scene->coll_grid();
        // (each "past" test as the original: NaN doesn't rule a grid out)
        if (grid && !(grid->box().min.x > box.max.x) && !(box.min.x > grid->box().max.x) &&
            !(grid->box().min.z > box.max.z) && !(box.min.z > grid->box().max.z) && !(grid->box().min.y > box.max.y) &&
            !(box.min.y > grid->box().max.y)) {
            core::at(scenes, count) = scene;
            core::at(grids, count) = grid;
            if (++count >= kMaxGridsAlongLine) {
                break;
            }
        }
        node = next;
    }
    if (count == 0) {
        return {};
    }
    core::at(grids, count) = nullptr;
    core::at(scenes, count) = nullptr;
    return std::span<CollGrid* const>(grids).first(count);
}

// 0x0048CA80
uint32_t new_sector_name()
{
    uint32_t& counter = engine_scene_list_state().next_sector_name_;
    for (;;) {
        const uint32_t name = counter;
        bool used = false;
        const memory::ListNode* node = head_of_scene_list();
        if (!is_head(node) && node) {
            for (bool again = true; again; again = (node != nullptr)) {
                const memory::ListNode* next = get_next_node(node);
                if (scene_of(node)->look_up_sector(name)) {
                    used = true;
                    break;
                }
                node = next;
            }
        }
        if (!used) {
            return counter++;
        }
        ++counter;
    }
}

// 0x0048CAF0
bool cfunc_toggle_add_scenes(script::Struct* /*params*/, script::Script* /*script*/)
{
    toggle_add_scenes();
    return true;
}

// 0x0048CB00
void get_scene_debug_info(script::Struct* info)
{
    if (get_main_scene()) {
        gsl::owner<script::Struct*> main = script::spawn_param_bag();
        if (main) {
            new (main) script::Struct;
        }
        get_main_scene()->fetch_diagnostics(main);
        info->append_struct_ref(kMainScene, main);
    }
    if (get_sky_scene()) {
        gsl::owner<script::Struct*> sky = script::spawn_param_bag();
        if (sky) {
            new (sky) script::Struct;
        }
        get_sky_scene()->fetch_diagnostics(sky);
        info->append_struct_ref(kSkyScene, sky);
    }
}

// 0x0048CC80
WorldScene* build_world_scene(const char* name, TextureSet* textures, bool with_super_sectors)
{
    gsl::owner<WorldScene*> made = render::device().make_empty_scene(name, textures, with_super_sectors);
    WorldScene* const scene = made; // (the scene list's)
    register_scene(scene, name, textures, 1, nullptr, 0);
    return scene;
}

// 0x0048CCB0
WorldScene* load_scene(const char* name, TextureSet* textures, bool with_super_sectors, bool is_sky, bool mesh_flag,
                       int unused)
{
    std::array<char, kSceneFileNameSize> file{};
    build_scene_file_path(file.data(), sizeof(file), name);
    memory::push_mem_profile(file.data());
    VertexArena vertex_arena{};
    VertexArena* arena = renderer().stream_offset_supported() ? &vertex_arena : nullptr;
    gsl::owner<WorldScene*> made = device().load_scene_from_file(file.data(), textures, with_super_sectors, is_sky, mesh_flag, arena);
    WorldScene* const scene = made; // (the scene list's)
    memory::pop_mem_profile();
    register_scene(scene, name, textures, 0, arena, unused);
    return scene;
}

// 0x0048CD80
WorldScene* load_named_scene_from_memory(const char* name, const uint8_t* data, size_t size, TextureSet* textures,
                                         bool with_super_sectors, bool is_sky, bool mesh_flag, int unused)
{
    memory::push_mem_profile("Scene Processing");
    gsl::owner<WorldScene*> made = render::device().load_scene_from_memory(data, size, textures, with_super_sectors, is_sky, mesh_flag);
    WorldScene* const scene = made; // (the scene list's)
    memory::pop_mem_profile();
    register_scene(scene, name, textures, 0, nullptr, unused);
    return scene;
}

// 0x0048CDD0
WorldScene* add_scene(SceneAddition scene_addition)
{
    const auto* const scene_name = scene_addition.scene;
    const auto* const add_name = scene_addition.addition;
    std::array<char, kSceneFileNameSize> file{};
    std::array<char, kSceneFileNameSize> tex_file{};
    build_scene_file_path(file.data(), sizeof(file), add_name);
    text::format_to(tex_file, "levels\\%s\\%s.tex", add_name, add_name);
    WorldScene* scene = look_up_scene(checksums::hash_from_text(scene_name));
    if (std::strcmp(scene->add_name().data(), file.data()) == 0) {
        scene->unload_added_scene();
        if (TextureSet* old = find_tex_dict(checksums::hash_from_text(tex_file.data()))) {
            unload_tex_dict(old);
        }
    }
    TextureSet* textures = load_tex_dict(tex_file.data(), 1, 0, 0, 0);
    plat_load_add_scene(scene, file.data());
    scene->finish_add(file.data(), textures);
    return scene;
}

// 0x0048CEF0
bool reload_scene(const char* name)
{
    WorldScene* scene = look_up_scene(checksums::hash_from_text(name));
    if (!scene) {
        return false;
    }
    const bool is_sky = scene->is_sky();
    TextureSet* textures = scene->texture_set();
    unload_engine_scene(scene, 1);
    load_scene(name, textures, !is_sky, is_sky, false, 0);
    return true;
}

} // namespace render

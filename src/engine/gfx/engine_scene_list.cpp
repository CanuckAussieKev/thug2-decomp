// The engine's loaded scenes: see engine_scene_list.h.

#include "engine/gfx/device.h"
#include "engine/gfx/engine_scene_list.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/scene_load.h"
#include "engine/gfx/texture_dict.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_grid.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"

namespace render {

namespace {

// The list's first node (the head's next; the head itself when the list is empty).
memory::ListNode* head_of_scene_list() { return g_scene_list.next; }
bool is_head(const memory::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
// The node after `node` (null at the end).
memory::ListNode* get_next_node(const memory::ListNode* node)
{
    memory::ListNode* next = node->next;
    return is_head(next) ? nullptr : next;
}
WorldScene* scene_of(const memory::ListNode* node) { return static_cast<WorldScene*>(node->data); }

// The first scene whose is_sky_ is `sky` (the original's walk: the first node's data checked before the node).
WorldScene* first_scene(bool sky)
{
    memory::ListNode* node = head_of_scene_list();
    if (is_head(node) || !node)
        return nullptr;
    for (;;) {
        memory::ListNode* next = get_next_node(node);
        if (scene_of(node)->is_sky_ == sky)
            return scene_of(node);
        if (!next)
            return nullptr;
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
    std::snprintf(out, size, "levels\\%s\\%s%s.scn.%s", name, name, "", platform_suffix());
}

} // namespace

// 0x0048CC30
SceneListHead* SceneListHead::make_new()
{
    make_head();
    return this;
}

// 0x0048C7F0
memory::ListNode* __cdecl first_scene_node()
{
    memory::ListNode* node = head_of_scene_list();
    return is_head(node) ? nullptr : node;
}

// 0x0048C750
WorldScene* __cdecl get_scene_by_name(const char* name) { return look_up_scene(checksums::hash_from_text(name)); }

// 0x0048C770
WorldScene* __cdecl get_main_scene() { return first_scene(false); }

// 0x0048C7B0
WorldScene* __cdecl get_sky_scene() { return first_scene(true); }

// 0x0048C810
bool __cdecl unload_all_scenes()
{
    memory::ListNode* node = head_of_scene_list();
    if (!is_head(node) && node) {
        do {
            memory::ListNode* next = get_next_node(node);
            WorldScene* scene = scene_of(node);
            TextureSet* textures = scene->texture_set_;
            unload_engine_scene(scene, 1);
            if (textures)
                unload_tex_dict(textures);
            node = next;
        } while (node);
    }
    clear_debug_lines(); // (the original pushes a 0 it doesn't read)
    return true;
}

// 0x0048C870
void __cdecl add_shadow_caster(const uint8_t* texture, void* model, WorldScene* scene)
{
    if (!scene)
        scene = get_main_scene();
    add_texture_shadow_caster(texture, model, reinterpret_cast<uint32_t>(scene));
}

// 0x0048C8F0
void __cdecl print_coll_statics()
{
    frame_clock::get_time();
    memory::ListNode* node = head_of_scene_list();
    if (!is_head(node) && node) {
        do {
            memory::ListNode* next = get_next_node(node);
            WorldScene* scene = scene_of(node);
            if (!scene->is_sky_)
                scene->print_coll_statics();
            node = next;
        } while (node);
    }
    frame_clock::get_time();
}

// 0x0048C940
CollGrid* __cdecl get_first_coll_grid()
{
    memory::ListNode* node = head_of_scene_list();
    if (is_head(node) || !node)
        return nullptr;
    do {
        memory::ListNode* next = get_next_node(node);
        if (CollGrid* grid = scene_of(node)->coll_grid_)
            return grid;
        node = next;
    } while (node);
    return nullptr;
}

uint32_t g_next_sector_name{1};
CollGrid* g_collision_grids[16];
WorldScene* g_collision_scenes[16];

// 0x0048C980
CollGrid** __cdecl get_coll_grids_on_line(const maths::Seg* line)
{
    auto* scenes = g_collision_scenes;
    auto* grids = g_collision_grids;
    maths::BBox box(line->start);
    box.append_spot(line->end);
    uint32_t count = 0;
    memory::ListNode* node = head_of_scene_list();
    if (is_head(node) || !node)
        return nullptr;
    do {
        memory::ListNode* next = get_next_node(node);
        WorldScene* scene = scene_of(node);
        CollGrid* grid = scene->coll_grid_;
        // (each "past" test as the original: NaN doesn't rule a grid out)
        if (grid && !(grid->box_.min.x > box.max.x) && !(box.min.x > grid->box_.max.x) &&
            !(grid->box_.min.z > box.max.z) && !(box.min.z > grid->box_.max.z) && !(grid->box_.min.y > box.max.y) &&
            !(box.min.y > grid->box_.max.y)) {
            scenes[count] = scene;
            grids[count] = grid;
            if (++count >= 15)
                break;
        }
        node = next;
    } while (node);
    if (count == 0)
        return nullptr;
    grids[count] = nullptr;
    scenes[count] = nullptr;
    return grids;
}

// 0x0048CA80
uint32_t __cdecl new_sector_name()
{
    uint32_t& counter = g_next_sector_name;
    for (;;) {
        uint32_t name = counter;
        bool used = false;
        memory::ListNode* node = head_of_scene_list();
        if (!is_head(node) && node) {
            do {
                memory::ListNode* next = get_next_node(node);
                if (scene_of(node)->look_up_sector(name)) {
                    used = true;
                    break;
                }
                node = next;
            } while (node);
        }
        if (!used)
            return counter++;
        ++counter;
    }
}

// 0x0048CAF0
bool __cdecl cfunc_toggle_add_scenes(script::Struct* /*params*/, script::Script* /*script*/)
{
    toggle_add_scenes();
    return true;
}

// 0x0048CB00
void __cdecl get_scene_debug_info(script::Struct* info)
{
    if (get_main_scene()) {
        script::Struct* main = script::spawn_param_bag();
        if (main)
            main->prepare_all();
        get_main_scene()->fetch_diagnostics(main);
        info->append_struct_ref(0x9B10525Cu, main);
    }
    if (get_sky_scene()) {
        script::Struct* sky = script::spawn_param_bag();
        if (sky)
            sky->prepare_all();
        get_sky_scene()->fetch_diagnostics(sky);
        info->append_struct_ref(0x02C5EA34u, sky);
    }
}

// 0x0048CC80
WorldScene* __cdecl build_world_scene(const char* name, TextureSet* textures, bool with_super_sectors)
{
    auto* scene = static_cast<WorldScene*>(render::make_empty_scene(name, textures, with_super_sectors));
    register_scene(scene, name, textures, 1, nullptr, 0);
    return scene;
}

// 0x0048CCB0
WorldScene* __cdecl load_scene(const char* name, TextureSet* textures, bool with_super_sectors, bool is_sky,
                               bool mesh_flag, int unused)
{
    char file[0x80];
    build_scene_file_path(file, sizeof(file), name);
    memory::push_mem_profile(file);
    uint32_t vertex_arena[4] = {0, 0, 0, 0};
    void* arena = g_stream_offset_supported ? vertex_arena : nullptr;
    auto* scene = static_cast<WorldScene*>(
        load_scene_from_file(file, textures, with_super_sectors, is_sky, mesh_flag, reinterpret_cast<int>(arena)));
    memory::pop_mem_profile();
    register_scene(scene, name, textures, 0, arena, unused);
    return scene;
}

// 0x0048CD80
WorldScene* __cdecl load_named_scene_from_memory(const char* name, const uint8_t* data, size_t size,
                                                 TextureSet* textures, bool with_super_sectors, bool is_sky,
                                                 bool mesh_flag, int unused)
{
    memory::push_mem_profile("Scene Processing");
    auto* scene = static_cast<WorldScene*>(
        render::load_scene_from_memory(data, size, textures, with_super_sectors, is_sky, mesh_flag));
    memory::pop_mem_profile();
    register_scene(scene, name, textures, 0, nullptr, unused);
    return scene;
}

// 0x0048CDD0
WorldScene* __cdecl add_scene(const char* scene_name, const char* add_name)
{
    char file[0x80];
    char tex_file[0x80];
    build_scene_file_path(file, sizeof(file), add_name);
    std::snprintf(tex_file, sizeof(tex_file), "levels\\%s\\%s.tex", add_name, add_name);
    WorldScene* scene = look_up_scene(checksums::hash_from_text(scene_name));
    if (std::strcmp(scene->add_name_, file) == 0) {
        scene->unload_added_scene();
        if (TextureSet* old = find_tex_dict(checksums::hash_from_text(tex_file)))
            unload_tex_dict(old);
    }
    TextureSet* textures = load_tex_dict(tex_file, 1, 0, 0, 0);
    plat_load_add_scene(scene, file);
    scene->finish_add(file, reinterpret_cast<uint32_t>(textures));
    return scene;
}

// 0x0048CEF0
bool __cdecl reload_scene(const char* name)
{
    WorldScene* scene = look_up_scene(checksums::hash_from_text(name));
    if (!scene)
        return false;
    bool is_sky = scene->is_sky_;
    TextureSet* textures = scene->texture_set_;
    unload_engine_scene(scene, 1);
    load_scene(name, textures, !is_sky, is_sky, false, 0);
    return true;
}

} // namespace render

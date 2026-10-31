// The PC engine's scene and mesh loaders: see scene_load.h.

#include "engine/gfx/scene_load.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/file/file_load.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/frame_source.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/pc_mesh.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/sector.h"

namespace render {

memory::PoolPair g_frame_source_pools;

namespace {

// The engine's scenes and sectors (not rewritten yet; thiscall).
uint8_t* new_pc_scene(int hash_bits) { return reinterpret_cast<uint8_t*>(new PcScene(hash_bits)); }
void* spawn_sector(uint8_t* scene) { return reinterpret_cast<WorldScene*>(scene)->spawn_sector(); }
void choose_geom(void* sector, void* geom) { static_cast<WorldCell*>(sector)->choose_geom(geom); }
void insert_sector(uint8_t* scene, void* sector)
{
    reinterpret_cast<WorldScene*>(scene)->insert_sector(static_cast<WorldCell*>(sector));
}
void set_up_sectors(uint8_t* scene, const char* name)
{
    reinterpret_cast<WorldScene*>(scene)->finish_load(name, nullptr);
}
// A PC geom for a scene's sector (0x64 bytes, zeroed; the scene at +0x34).
uint8_t* new_sector_geom(uint8_t* scene)
{
    auto* geom = new PcGeom;
    // (no check: a failed allocation writes through null, as the original)
    geom->scene = scene;
    return reinterpret_cast<uint8_t*>(static_cast<Geom*>(geom));
}
void prepare_geometry(uint8_t* geom) { static_cast<PcGeom*>(reinterpret_cast<Geom*>(geom))->start_mesh_list(); }
// The 2D clip rectangle off (0x004D3CE0).
void clear_screen_clip() { clear_skin_scaling(); }

// The scene's hash table size: 2^n entries (n 1-12), the first at least twice the sectors.
int table_bit_width(int sectors)
{
    unsigned int wanted = 2 * static_cast<unsigned int>(sectors);
    unsigned int size = 2;
    int bits = 1;
    while (wanted > size && bits < 12) {
        size <<= 1;
        ++bits;
    }
    return bits;
}

// The hierarchy's array (new[]: its count in the word before it), constructed.
HierarchyNode* new_hierarchy(int count)
{
    void* memory = memory::alloc(count * sizeof(HierarchyNode) + 4);
    if (!memory)
        return nullptr;
    *static_cast<int*>(memory) = count;
    auto* nodes = reinterpret_cast<HierarchyNode*>(static_cast<uint8_t*>(memory) + 4);
    for (int i = 0; i < count; ++i)
        nodes[i].make_new();
    return nodes;
}

// A new mesh list with the flag at +0x40.
SceneRef* new_mesh_list(bool mesh_flag)
{
    void* memory = memory::alloc(sizeof(SceneRef));
    SceneRef* list = memory ? static_cast<SceneRef*>(memory)->make_new() : nullptr;
    list->unknown40 = mesh_flag;
    return list;
}

uint8_t* build_world_scene(int sectors, bool with_super_sectors, bool flag_8d, SceneRef* list)
{
    uint8_t* scene = new_pc_scene(table_bit_width(sectors));
    scene[0x8C] = with_super_sectors;
    scene[0x8D] = flag_8d;
    *reinterpret_cast<SceneRef**>(scene + 0x280) = list;
    return scene;
}

// The mesh made around a loaded scene: the scene (+0xC4) and its hierarchy (+0xB4, +0xB8); the textures at +0x04.
void set_mesh_scene(PcMesh* mesh, uint8_t* scene) { mesh->choose_scene(scene); }

} // namespace

// 0x004B2D30
HierarchyNode* HierarchyNode::make_new()
{
    checksum = 0;
    parent_checksum_ = 0;
    parent_index_ = 0xFFFF;
    bone_slot_ = 0;
    for (int i = 0; i < 16; ++i)
        matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    return this;
}

// 0x004B3490
void* __cdecl load_scene_from_memory(const uint8_t* data, void* textures, bool with_super_sectors, bool flag_8d,
                                     bool mesh_flag)
{
    SceneRef* list = new_mesh_list(mesh_flag);
    const uint8_t* p = data + 0xC; // (past the header)
    list->materials = read_materials_from_buffer(
        const_cast<uint8_t**>(&p),
        static_cast<const lists::KeyMap*>(*reinterpret_cast<void**>(static_cast<uint8_t*>(textures) + 0xC)));
    int sectors = *reinterpret_cast<const int*>(p);
    p += 4;
    uint8_t* scene = build_world_scene(sectors, with_super_sectors, flag_8d, list);
    for (int i = sectors; i > 0; --i) {
        void* sector = spawn_sector(scene);
        uint8_t* geom = new_sector_geom(scene);
        choose_geom(sector, geom);
        prepare_geometry(geom);
        if (static_cast<PcSector*>(sector)->read_from_ram_block(const_cast<uint8_t**>(&p)))
            insert_sector(scene, sector);
    }
    list->update_sphere();
    int nodes = *reinterpret_cast<const int*>(p);
    p += 4;
    if (nodes > 0) {
        HierarchyNode* hierarchy = new_hierarchy(nodes);
        list->extra = reinterpret_cast<uint8_t*>(hierarchy);
        // (no check: a failed allocation copies to null, as the original)
        std::memcpy(hierarchy, p, nodes * sizeof(HierarchyNode));
        list->unknown78 = static_cast<uint32_t>(nodes);
    }
    return scene;
}

// 0x004B36D0
void* __cdecl load_scene_from_file(const char* name, void* textures, bool with_super_sectors, bool flag_8d,
                                   bool mesh_flag, int sector_flags)
{
    SceneRef* list = new_mesh_list(mesh_flag);
    file::Hdl file = file::unseal(name, "rb");
    if (!file)
        return nullptr; // (the mesh list isn't freed, as the original)
    uint32_t header[3];
    file::get_take_in(&header[0], 4, 1, file);
    file::get_take_in(&header[1], 4, 1, file);
    file::get_take_in(&header[2], 4, 1, file);
    list->materials = load_materials_from_file(
        file, static_cast<const lists::KeyMap*>(*reinterpret_cast<void**>(static_cast<uint8_t*>(textures) + 0xC)));
    int sectors;
    file::get_take_in(&sectors, 4, 1, file);
    uint8_t* scene = build_world_scene(sectors, with_super_sectors, flag_8d, list);
    for (int i = 0; i < sectors; ++i) {
        void* sector = spawn_sector(scene);
        uint8_t* geom = new_sector_geom(scene);
        choose_geom(sector, geom);
        prepare_geometry(geom);
        if (static_cast<PcSector*>(sector)->ingest_from_file(file, static_cast<uint32_t>(sector_flags)))
            insert_sector(scene, sector);
    }
    list->update_sphere();
    int nodes;
    file::get_take_in(&nodes, 4, 1, file);
    if (nodes > 0) {
        HierarchyNode* hierarchy = new_hierarchy(nodes);
        list->extra = reinterpret_cast<uint8_t*>(hierarchy);
        file::get_take_in(hierarchy, sizeof(HierarchyNode), nodes, file); // (null when out of memory, as the original)
        list->unknown78 = static_cast<uint32_t>(nodes);
    }
    file::shut(file);
    return scene;
}

// 0x004B3990
void* __cdecl create_frame_source() { return new FrameSource; }

// 0x004B3A20
void* __cdecl load_mesh_file(const char* name, void* textures)
{
    auto* scene = static_cast<uint8_t*>(load_scene_from_file(name, textures, false, false, false, 0));
    *reinterpret_cast<uint32_t*>(scene + 0x84) = checksums::hash_from_text(name);
    *reinterpret_cast<void**>(scene + 0x88) = textures;
    set_up_sectors(scene, name);
    clear_screen_clip();
    auto* mesh = new PcMesh(name);
    set_mesh_scene(mesh, scene);
    mesh->textures_ = textures;
    return mesh;
}

// 0x004B3AD0
void* __cdecl load_mesh_from_memory(uint32_t checksum, const uint8_t* data, int /*unused*/, void* skin, void* textures)
{
    char name[16];
    std::snprintf(name, sizeof(name), "%d\n", static_cast<int>(checksum)); // (with a line feed, as the original)
    auto* scene = static_cast<uint8_t*>(load_scene_from_memory(data, textures, false, false, false));
    *reinterpret_cast<uint32_t*>(scene + 0x84) = checksums::hash_from_text(name);
    *reinterpret_cast<void**>(scene + 0x88) = textures;
    set_up_sectors(scene, name);
    auto* mesh = new PcMesh;
    mesh->load_skin(static_cast<const uint8_t*>(skin));
    clear_screen_clip();
    set_mesh_scene(mesh, scene);
    mesh->textures_ = textures;
    return mesh;
}

} // namespace render

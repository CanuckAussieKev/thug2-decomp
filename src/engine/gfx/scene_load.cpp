// The PC engine's scene and mesh loaders: see scene_load.h.

#include "engine/gfx/scene_load.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
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
#include "engine/gfx/scene_file_stream.h"
#include "engine/gfx/scene_format.h"
#include "engine/gfx/sector.h"
#include "engine/core/owner.h"

namespace render {

memory::PoolPair g_frame_source_pools;

namespace {

// The engine's scenes and sectors (not rewritten yet; thiscall).
uint8_t* new_pc_scene(int hash_bits) { return reinterpret_cast<uint8_t*>(new PcScene(hash_bits)); }
WorldCell* spawn_sector(uint8_t* scene) { return reinterpret_cast<WorldScene*>(scene)->spawn_sector(); }
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
        new (&nodes[i]) HierarchyNode;
    return nodes;
}

// A new mesh list with the flag at +0x40.
SceneRef* new_mesh_list(bool mesh_flag)
{
    void* memory = memory::alloc(sizeof(SceneRef));
    SceneRef* list = memory ? new (memory) SceneRef : nullptr;
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

// The texture dictionary's table of textures by checksum (+0x0C).
const lists::KeyMap* texture_table(void* textures)
{
    return static_cast<const lists::KeyMap*>(*reinterpret_cast<void**>(static_cast<uint8_t*>(textures) + 0xC));
}

// What both of the original's scene loaders do after the materials (0x004B3490 from memory, 0x004B36D0 from a file -
// the same records, scene_format.h): the sectors (each loaded by `load_sector`), the hierarchy.
// Not original - a damaged record ends the scene there (the sectors loaded so far kept, no hierarchy read); a sector
// count past what is there reads none, a hierarchy past the end isn't read (the original read on past the scene's
// end).
template <typename Stream, typename LoadSector>
uint8_t* read_sectors(Stream& in, SceneRef* list, bool with_super_sectors, bool flag_8d, LoadSector load_sector)
{
    int sectors = scene_format::take<int>(in);
    if (sectors < 0 || !in.fits(static_cast<uint64_t>(sectors) * 0x38)) // (each sector at least its fields)
        sectors = 0;
    uint8_t* scene = build_world_scene(sectors, with_super_sectors, flag_8d, list);
    for (int i = sectors; i > 0 && in.ok(); --i) {
        WorldCell* sector = spawn_sector(scene);
        uint8_t* geom = new_sector_geom(scene);
        choose_geom(sector, geom);
        prepare_geometry(geom);
        load_sector(static_cast<PcSector*>(sector)); // (a damaged one keeps the meshes it made)
        insert_sector(scene, sector);
    }
    list->update_sphere();
    int nodes = scene_format::take<int>(in);
    if (nodes > 0 && in.fits(static_cast<uint64_t>(nodes) * sizeof(HierarchyNode))) {
        HierarchyNode* hierarchy = new_hierarchy(nodes);
        list->extra = reinterpret_cast<uint8_t*>(hierarchy);
        // (no check: a failed allocation reads into null, as the original)
        in.take(hierarchy, nodes * sizeof(HierarchyNode));
        list->unknown78 = static_cast<uint32_t>(nodes);
    }
    if (!in.ok())
        debug::printf("scene damaged: cut short or doesn't fit (%d sectors)\n", sectors);
    return scene;
}

} // namespace

// 0x004B2D30
HierarchyNode::HierarchyNode()
{
    checksum = 0;
    parent_checksum_ = 0;
    parent_index_ = 0xFFFF;
    bone_slot_ = 0;
    for (int i = 0; i < 16; ++i)
        matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

// 0x004B3490
void* load_scene_from_memory(const uint8_t* data, size_t size, void* textures, bool with_super_sectors, bool flag_8d,
                             bool mesh_flag)
{
    SceneRef* list = new_mesh_list(mesh_flag);
    scene_format::MemoryStream in(data, size);
    in.skip(scene_format::kHeaderSize);
    list->materials = read_materials_from_buffer(in, texture_table(textures));
    return read_sectors(in, list, with_super_sectors, flag_8d,
                        [&in](PcSector* sector) { return sector->read_from_ram_block(in); });
}

// 0x004B36D0
void* load_scene_from_file(const char* name, void* textures, bool with_super_sectors, bool flag_8d, bool mesh_flag,
                           int sector_flags)
{
    SceneRef* list = new_mesh_list(mesh_flag);
    file::Hdl file = file::unseal(name, "rb");
    if (!file)
        return nullptr; // (the mesh list isn't freed, as the original)
    scene_format::FileStream in(file);
    in.skip(scene_format::kHeaderSize); // (the header's three words: not used)
    list->materials = load_materials_from_file(in, texture_table(textures));
    uint8_t* scene = read_sectors(in, list, with_super_sectors, flag_8d, [&in, sector_flags](PcSector* sector) {
        return sector->ingest_from_file(in, static_cast<uint32_t>(sector_flags));
    });
    file::shut(file);
    return scene;
}

// 0x004B3990
memory::owner<void*> create_frame_source() { return new FrameSource; }

// 0x004B3A20
void* load_mesh_file(const char* name, void* textures)
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
void* load_mesh_from_memory(uint32_t checksum, const uint8_t* data, size_t size, const void* skin, size_t skin_size,
                            void* textures)
{
    char name[16];
    std::snprintf(name, sizeof(name), "%d\n", static_cast<int>(checksum)); // (with a line feed, as the original)
    auto* scene = static_cast<uint8_t*>(load_scene_from_memory(data, size, textures, false, false, false));
    *reinterpret_cast<uint32_t*>(scene + 0x84) = checksums::hash_from_text(name);
    *reinterpret_cast<void**>(scene + 0x88) = textures;
    set_up_sectors(scene, name);
    auto* mesh = new PcMesh;
    mesh->load_skin(static_cast<const uint8_t*>(skin), skin_size);
    clear_screen_clip();
    set_mesh_scene(mesh, scene);
    mesh->textures_ = textures;
    return mesh;
}

} // namespace render

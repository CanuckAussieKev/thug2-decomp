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
#include "formats/scene_format.h"
#include "engine/gfx/sector.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/texture_dict.h"

namespace render {

memory::PoolPair g_frame_source_pools;

namespace {

// A PC geom for a scene's sector (zeroed; its scene set).
gsl::owner<PcGeom*> new_sector_geom(PcScene* scene)
{
    auto geom = memory::make_owned<PcGeom>();
    // (no check: a failed allocation writes through null, as the original)
    geom->scene = scene;
    return geom;
}
// The 2D clip rectangle off (0x004D3CE0).
void clear_screen_clip() { clear_skin_scaling(); }

// The scene's hash table size: 2^n entries (n 1-12), the first at least twice the sectors.
int table_bit_width(int sectors)
{
    const unsigned int wanted = 2 * static_cast<unsigned int>(sectors);
    unsigned int size = 2;
    int bits = 1;
    while (wanted > size && bits < 12) {
        size <<= 1;
        ++bits;
    }
    return bits;
}


// A new mesh list with the flag at +0x40.
SceneRef* new_mesh_list(bool mesh_flag)
{
    void* memory = memory::alloc(sizeof(SceneRef));
    SceneRef* list = memory ? memory::construct_in<SceneRef>(memory) : nullptr;
    list->unknown40 = mesh_flag;
    return list;
}

gsl::owner<PcScene*> build_world_scene(int sectors, bool with_super_sectors, bool flag_8d, SceneRef* list)
{
    auto scene = memory::make_owned<PcScene>(table_bit_width(sectors));
    scene->builds_collision_grid_ = with_super_sectors;
    scene->is_sky_ = flag_8d;
    scene->meshes = list;
    return scene;
}

// The texture dictionary's table of textures by checksum.
const lists::KeyMap* texture_table(const TextureSet* textures) { return textures->textures_; }

// What both of the original's scene loaders do after the materials (0x004B3490 from memory, 0x004B36D0 from a file -
// the same records, scene_format.h): the sectors (each loaded by `load_sector`), the hierarchy.
// Not original - a damaged record ends the scene there (the sectors loaded so far kept, no hierarchy read); a sector
// count past what is there reads none, a hierarchy past the end isn't read (the original read on past the scene's
// end).
template <typename Stream, typename LoadSector>
PcScene* read_sectors(Stream& in, SceneRef* list, bool with_super_sectors, bool flag_8d, LoadSector load_sector)
{
    int sectors = scene_format::take<int>(in);
    if (sectors < 0 || !in.fits(static_cast<uint64_t>(sectors) * 0x38)) // (each sector at least its fields)
        sectors = 0;
    gsl::owner<PcScene*> scene = build_world_scene(sectors, with_super_sectors, flag_8d, list);
    for (int i = sectors; i > 0 && in.ok(); --i) {
        gsl::owner<WorldCell*> sector = scene->spawn_sector();
        gsl::owner<PcGeom*> geom = new_sector_geom(scene);
        sector->choose_geom(geom);
        geom->start_mesh_list();
        load_sector(core::down_cast<PcSector>(sector)); // (a damaged one keeps the meshes it made)
        scene->insert_sector(sector);
    }
    list->update_sphere();
    const int nodes = scene_format::take<int>(in);
    if (nodes > 0 && in.fits(static_cast<uint64_t>(nodes) * sizeof(HierarchyNode))) {
        HierarchyNode* hierarchy = new_hierarchy(nodes);
        list->hierarchy = hierarchy;
        // (no check: a failed allocation reads into null, as the original)
        in.take(hierarchy, nodes * sizeof(HierarchyNode));
        list->unknown78 = static_cast<uint32_t>(nodes);
    }
    if (!in.ok())
        debug::printf("scene damaged: cut short or doesn't fit (%d sectors)\n", sectors);
    return scene;
}

} // namespace

HierarchyNode* new_hierarchy(int count)
{
    void* memory = memory::alloc(count * sizeof(HierarchyNode));
    if (!memory)
        return nullptr;
    auto* nodes = static_cast<HierarchyNode*>(memory);
    for (int i = 0; i < count; ++i)
        new (&nodes[i]) HierarchyNode;
    return nodes;
}

// (the original's delete[]: the nodes' destructor is empty)
void free_hierarchy(HierarchyNode* nodes)
{
    if (nodes)
        memory::Hub::sole()->dealloc(nodes);
}

// 0x004B2D30
HierarchyNode::HierarchyNode()
{
    matrix.row = {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}};
}

// 0x004B3490
PcScene* load_scene_from_memory(const uint8_t* data, size_t size, const TextureSet* textures, bool with_super_sectors,
                                bool flag_8d, bool mesh_flag)
{
    SceneRef* list = new_mesh_list(mesh_flag);
    scene_format::MemoryStream in(data, size);
    in.skip(scene_format::kHeaderSize);
    list->materials = read_materials_from_buffer(in, texture_table(textures));
    return read_sectors(in, list, with_super_sectors, flag_8d,
                        [&in](PcSector* sector) { return sector->read_from_ram_block(in); });
}

// 0x004B36D0
PcScene* load_scene_from_file(const char* name, const TextureSet* textures, bool with_super_sectors, bool flag_8d,
                              bool mesh_flag, VertexArena* arena)
{
    SceneRef* list = new_mesh_list(mesh_flag);
    file::Hdl file = file::unseal(name, "rb");
    if (!file)
        return nullptr; // (the mesh list isn't freed, as the original)
    scene_format::FileStream in(file);
    in.skip(scene_format::kHeaderSize); // (the header's three words: not used)
    list->materials = load_materials_from_file(in, texture_table(textures));
    PcScene* scene = read_sectors(in, list, with_super_sectors, flag_8d, [&in, arena](PcSector* sector) {
        return sector->ingest_from_file(in, arena);
    });
    file::shut(file);
    return scene;
}

// 0x004B3990
gsl::owner<void*> create_frame_source() { return new FrameSource; }

// 0x004B3A20
gsl::owner<PcMesh*> load_mesh_file(const char* name, TextureSet* textures)
{
    PcScene* scene = load_scene_from_file(name, textures, false, false, false, nullptr);
    scene->checksum_ = checksums::hash_from_text(name);
    scene->texture_set_ = textures;
    scene->finish_load(name, nullptr);
    clear_screen_clip();
    auto mesh = memory::make_owned<PcMesh>(name);
    mesh->choose_scene(scene);
    mesh->textures_ = textures;
    return mesh;
}

// 0x004B3AD0
gsl::owner<PcMesh*> load_mesh_from_memory(uint32_t checksum, const uint8_t* data, size_t size, const void* skin, size_t skin_size,
                              TextureSet* textures)
{
    std::array<char, 16> name{};
    std::snprintf(name.data(), sizeof(name), "%d\n", static_cast<int>(checksum)); // (with a line feed, as the original)
    PcScene* scene = load_scene_from_memory(data, size, textures, false, false, false);
    scene->checksum_ = checksums::hash_from_text(name.data());
    scene->texture_set_ = textures;
    scene->finish_load(name.data(), nullptr);
    auto mesh = memory::make_owned<PcMesh>();
    mesh->load_skin(static_cast<const uint8_t*>(skin), skin_size);
    clear_screen_clip();
    mesh->choose_scene(scene);
    mesh->textures_ = textures;
    return mesh;
}

} // namespace render

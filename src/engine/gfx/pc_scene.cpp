// The PC scene and sector, the PC screen effects: see pc_scene.h.

#include "engine/gfx/pc_scene.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/assets/resource_entry.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/pc_grass.h"
#include "engine/gfx/scene_file_stream.h"
#include "engine/gfx/scene_format.h"
#include "engine/gfx/screen_effects.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/mesh_list.h"
#include "engine/lists/key_map.h"
#include "engine/render/coll_movable.h"
#include "engine/gfx/device.h"
#include "engine/core/owner.h"

namespace render {

namespace {} // namespace

uint32_t g_bloom_on;
uint32_t g_screen_effect_unknown5724;
uint32_t g_screen_effect_unknown5728;
// The empty bounding box (all zero at start, as the original's) and its guard.
maths::BBox g_empty_bounding_box{maths::BBox(maths::Vec4{0.0f, 0.0f, 0.0f, 0.0f})};
uint32_t g_empty_bounding_box_guard;

uint32_t g_bloom_alpha_first{0x10};
uint32_t g_bloom_alpha_second{0x10};
uint32_t g_screen_effect_unknown984{0x42A00000};
uint32_t g_screen_effect_unknown988{0x43000000};
uint32_t g_bloom_cut_off{0x80808080};
uint32_t g_screen_effect_unknown990{0x80808080};
uint32_t g_screen_effect_unknown994{0x80808080};

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
PcGeom* geometry_of(void* sector)
{
    return static_cast<PcGeom*>(static_cast<PcSector*>(sector)->geom_handle_);
} // (+0x14: the getter 0x004A2BA0, folded with others)

// The first sector of a fresh iteration of the table (null if none).
void* first_sector(lists::KeyMap* table)
{
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    if (!table->step_iterator() || !table->iter_item_)
        return nullptr;
    return table->iter_item_->value;
}
void* next_sector(lists::KeyMap* table)
{
    if (!table->step_iterator() || !table->iter_item_)
        return nullptr;
    return table->iter_item_->value;
}

} // namespace

// --- the sector ---

// 0x004C6530
void PcSector::plat_set_shatter(bool on)
{
    if (on && geom_handle_)
        engine::shatter_geom(geom_handle_);
}

// 0x004C65C0
memory::owner<PcSector*> PcSector::plat_clone(int, void*) { return new PcSector(); }

// 0x004C64E0
const maths::BBox* PcSector::fetch_empty_bounds()
{
    auto* empty = &g_empty_bounding_box; // (the game's, made the first time: its guard's bit 0)
    uint32_t& made = g_empty_bounding_box_guard;
    if (!(made & 1)) {
        made |= 1;
        new (empty) maths::BBox();
    }
    return empty;
}

// --- the scene ---

// 0x004C6190
memory::owner<WorldCell*> PcScene::plat_create_sector() { return new PcSector(); }

// 0x004C6200
void PcScene::unload_sectors()
{
    lists::KeyMap* table = sectors_;
    for (void* sector = first_sector(table); sector; sector = next_sector(table)) {
    }
}

// 0x004C6270
void PcScene::post_load(void* arena_word)
{
    auto* arena = static_cast<VertexArena*>(arena_word);
    lists::KeyMap* table = sectors_;
    void* sector = first_sector(table);
    if (arena) {
        input_port()->CreateVertexBuffer(arena->size, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                         reinterpret_cast<IDirect3DVertexBuffer9**>(&arena->buffer), nullptr);
        static_cast<IDirect3DVertexBuffer9*>(arena->buffer)->Lock(0, 0, &arena->data, 0);
        arena->used = 0;
        meshes->com_object_ = arena->buffer;
        meshes->unknown80 = arena->size;
    }
    for (; sector; sector = next_sector(table)) {
        PcGeom* geom = geometry_of(sector);
        geom->make_mesh_table(arena);
        geom->add_to_mesh_list(true);
    }
    if (arena) {
        static_cast<IDirect3DVertexBuffer9*>(arena->buffer)->Unlock();
        arena->data = nullptr;
    }
    meshes->allocate_mesh_list();
    for (sector = first_sector(table); sector; sector = next_sector(table))
        geometry_of(sector)->add_to_mesh_list(false);
    meshes->order_meshes();
}

// --- the screen effects ---

// 0x004C63C0
void PcScreenEffects::set_bloom(int on) { g_bloom_on = on != 0; }
// 0x004C63E0
void PcScreenEffects::set_bloom_cut_off(uint32_t colour) { g_bloom_cut_off = colour; }
// 0x004C63F0
void PcScreenEffects::set_bloom_alphas(int first, int second)
{
    g_bloom_alpha_first = static_cast<uint32_t>(first);
    g_bloom_alpha_second = static_cast<uint32_t>(second);
}
// 0x004C6410
void PcScreenEffects::set_bloom_colour(uint32_t colour) { g_screen_effect_unknown990 = colour; }
// 0x004C6420
void PcScreenEffects::set_dof_on(int on) { g_screen_effect_unknown5724 = on != 0; }
// 0x004C6460
void PcScreenEffects::set_dof_colour_a(uint32_t colour) { g_screen_effect_unknown994 = colour; }
// 0x004C6470
void PcScreenEffects::set_dof_value_a(float value) { g_screen_effect_unknown984 = std::bit_cast<uint32_t>(value); }
// 0x004C6440
void PcScreenEffects::set_dof_value_c(float value) { g_screen_effect_unknown988 = std::bit_cast<uint32_t>(value); }
// 0x004C6450
void PcScreenEffects::set_dof_value_d(float value) { g_screen_effect_unknown5728 = std::bit_cast<uint32_t>(value); }

namespace {

// The engine and mesh code the loaders go through.
void change_sector_key(PcSector* sector, uint32_t checksum)
{
    sector->checksum_ = checksum; // (the original calls a setter folded with Resource::set_checksum)
}
MeshRef* new_mesh()
{
    void* memory = memory::alloc(sizeof(MeshRef));
    return memory ? static_cast<MeshRef*>(memory)->make_new() : nullptr; // (its constructor)
}
void set_mesh_sphere_and_box(MeshRef* mesh, const float* centre, float radius, const maths::Vec4* min,
                             const maths::Vec4* max)
{
    mesh->change_bound_sphere(centre, radius, static_cast<int>(reinterpret_cast<intptr_t>(min)),
                              static_cast<int>(reinterpret_cast<intptr_t>(max)));
}
// The rest of a mesh's set-up, the same for both loaders: the scene's box grown, a billboard made, the mesh added.
void finish_mesh(PcSector* sector, PcGeom* geom, MeshRef* mesh, uint32_t billboard_type, const maths::Vec4* pivot,
                 const maths::Vec4* axis, const maths::Vec4* min, const maths::Vec4* max, uint8_t light_group)
{
    auto* bounds = reinterpret_cast<maths::BBox*>(*reinterpret_cast<uint8_t**>(geom->scene + 0x280) + 0x44);
    bounds->append_spot(*min);
    bounds->append_spot(*max);
    if (sector->flags & 0x800000) {
        mesh->make_billboard(static_cast<int>(billboard_type), pivot, axis);
        g_billboard_pool.append_mesh(mesh);
    }
    if (sector->flags & 0x200000)
        mesh->flags |= 0x200;
    geom->append_mesh(mesh);
    reinterpret_cast<uint8_t*>(mesh)[0x1F] = light_group;
    make_grass(geom, mesh);
}
// The meshes the load put on the list, counted (to a node that is the head, data -1, or null).
uint32_t count_loaded_meshes(const PcGeom* geom)
{
    uint32_t count = 0;
    for (memory::ListNode* node = geom->load_list_->next; node->data != reinterpret_cast<void*>(-1) && node;
         node = node->next)
        ++count;
    return count;
}

// What both of the original's sector loaders do (0x004C6650 from memory, 0x004C6B10 from a file - the same
// records, scene_format.h): its checksum, flags, box and billboard, then each mesh made, its vertices loaded by
// `load_body`. False if a record is damaged (the stream failed; the meshes made so far kept).
template <typename Stream, typename LoadBody>
bool read_sector(PcSector* sector, Stream& in, LoadBody load_body)
{
    auto* geometry = static_cast<PcGeom*>(sector->geom_handle_);
    scene_format::Sector record;
    // Not original - each record read and checked first: a sector, mesh or index list cut short (or a mesh count
    // past what is there) ends the scene (the original read on past its end).
    bool read = scene_format::read_sector(in, &record);
    change_sector_key(sector, record.checksum);
    sector->flags = record.flags;
    geometry->mesh_count_ = read ? record.mesh_count : 0;
    // (the box's corners kept: the meshes' are read over them, their w still 1)
    maths::Vec4 min = {record.box[0], record.box[1], record.box[2], 1.0f};
    maths::Vec4 max = {record.box[3], record.box[4], record.box[5], 1.0f};
    geometry->bounds.min = min;
    geometry->bounds.max = max;
    uint32_t billboard_type = record.billboard_type;
    maths::Vec4 pivot = {record.pivot[0], record.pivot[1], -record.pivot[2], 0.0f};
    maths::Vec4 axis = {record.axis[0], record.axis[1], record.axis[2], 0.0f};
    if (!read) {
        debug::printf("scene sector %08x damaged: cut short\n", record.checksum);
        in.fail();
        geometry->mesh_count_ = count_loaded_meshes(geometry);
        return false;
    }
    float centre[4] = {}; // (its w not read)
    scene_format::SectorMesh fields;
    for (uint32_t i = 0; i < geometry->mesh_count_; ++i) {
        if (!scene_format::read_sector_mesh(in, &fields)) {
            debug::printf("scene sector %08x damaged: mesh %u cut short\n", record.checksum, i);
            in.fail();
            break;
        }
        MeshRef* mesh = new_mesh();
        std::memcpy(centre, fields.centre, sizeof(fields.centre));
        std::memcpy(&min, fields.min, sizeof(fields.min));
        std::memcpy(&max, fields.max, sizeof(fields.max));
        if (fields.flags & 0x400)
            mesh->flags |= 2;
        if (fields.flags & 0x20000)
            mesh->flags |= 0x20000;
        // (the index lists: read and dropped - the PC's are built from the vertices' own)
        mesh->load_index_ = static_cast<uint16_t>(i);
        if (!load_body(mesh, fields.material, *reinterpret_cast<SceneRef**>(geometry->scene + 0x280))) {
            memory::Hub::get_pull()->dealloc(mesh); // (nothing of it made yet)
            break;
        }
        set_mesh_sphere_and_box(mesh, centre, fields.radius, &min, &max);
        finish_mesh(sector, geometry, mesh, billboard_type, &pivot, &axis, &min, &max,
                    static_cast<uint8_t>(record.light_group));
    }
    geometry->mesh_count_ = count_loaded_meshes(geometry);
    return in.ok();
}

} // namespace

// 0x004C6650
bool PcSector::read_from_ram_block(scene_format::MemoryStream& in)
{
    return read_sector(this, in, [&in](MeshRef* mesh, uint32_t material, SceneRef* list) {
        return mesh->read_from_ram_block(in, material, list, nullptr);
    });
}

// 0x004C6B10
bool PcSector::ingest_from_file(scene_format::FileStream& in, uint32_t unknown)
{
    return read_sector(this, in, [this, &in, unknown](MeshRef* mesh, uint32_t material, SceneRef* list) {
        // (the vertices given `unknown` unless a billboard; dynamic with the billboard bit, read as a byte: bit 23)
        auto* arena = reinterpret_cast<VertexArena*>(static_cast<uintptr_t>(flags & 0x800000 ? 0 : unknown));
        return mesh->ingest_from_file(in, material, list, arena, ((flags >> 23) & 1) != 0);
    });
}

} // namespace render

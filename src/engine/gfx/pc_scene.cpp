// The PC scene and sector, the PC screen effects: see pc_scene.h.

#include "engine/gfx/pc_scene.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/assets/resource_entry.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/pc_grass.h"
#include "engine/gfx/screen_effects.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/mesh_list.h"
#include "engine/lists/key_map.h"
#include "engine/core/code_address.h"
#include "engine/render/coll_movable.h"
#include "engine/core/stubs.h"
#include "game/save_cards/memcard.h"
#include "engine/gfx/device.h"

namespace render {

namespace {
extern void** const kCPcSceneVtable;
extern void** const kCPcScreenEffectsVtable;
extern void** const kCPcSectorVtable;
} // namespace

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
    return static_cast<PcGeom*>(static_cast<PcSector*>(sector)->geom);
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

// 0x004C6550
PcSector* PcSector::make_new()
{
    reinterpret_cast<WorldCell*>(this)->WorldCell::make_new();
    vtable = kCPcSectorVtable;
    unknown20[0] = unknown20[1] = unknown20[2] = 0.0f;
    unknown2c_ = 1.0f;
    return this;
}

// 0x004C65B0
void PcSector::destroy_body() { reinterpret_cast<WorldCell*>(this)->WorldCell::destroy_body(); }

// 0x004C6580
PcSector* PcSector::teardown(unsigned int free_flags)
{
    destroy_body();
    if (free_flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004C6530
void PcSector::change_live(bool on)
{
    if (on && geom)
        engine::shatter_geom(geom);
}

// 0x004C65C0
PcSector* PcSector::replicate(int, int)
{
    void* memory = memory::alloc(sizeof(PcSector));
    return memory ? static_cast<PcSector*>(memory)->make_new() : nullptr;
}

// 0x004C64E0
const maths::BBox* PcSector::fetch_bounds()
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

// 0x004C6130
PcScene* PcScene::make_new(int arg)
{
    reinterpret_cast<WorldScene*>(this)->WorldScene::make_new(arg);
    vtable = kCPcSceneVtable;
    return this;
}

// 0x004C6180
void PcScene::destroy_body() { reinterpret_cast<WorldScene*>(this)->WorldScene::destroy_body(); }

// 0x004C6150
PcScene* PcScene::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004C6190
PcSector* PcScene::spawn_sector()
{
    void* memory = memory::alloc(sizeof(PcSector));
    return memory ? static_cast<PcSector*>(memory)->make_new() : nullptr;
}

// 0x004C6200
void PcScene::unload_sectors()
{
    auto* table = static_cast<lists::KeyMap*>(sectors);
    for (void* sector = first_sector(table); sector; sector = next_sector(table)) {
    }
}

// 0x004C6270
void PcScene::post_load(VertexArena* arena)
{
    auto* table = static_cast<lists::KeyMap*>(sectors);
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

// 0x004C6480
PcScreenEffects* PcScreenEffects::make_new()
{
    reinterpret_cast<ScreenEffects*>(this)->ScreenEffects::make_new();
    vtable = kCPcScreenEffectsVtable;
    return this;
}

// 0x004C64A0
void PcScreenEffects::destroy_body()
{
    vtable = kCPcScreenEffectsVtable;
    reinterpret_cast<ScreenEffects*>(this)->ScreenEffects::destroy_body();
}

// 0x004C64B0
PcScreenEffects* PcScreenEffects::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004C63C0
void PcScreenEffects::set_bloom(int on) { g_bloom_on = on != 0; }
// 0x004C63E0
void PcScreenEffects::set_bloom_cut_off(uint32_t colour) { g_bloom_cut_off = colour; }
// 0x004C63F0
void PcScreenEffects::set_bloom_alphas(uint32_t first, uint32_t second)
{
    g_bloom_alpha_first = first;
    g_bloom_alpha_second = second;
}
// 0x004C6410
void PcScreenEffects::set_unknown990(uint32_t value) { g_screen_effect_unknown990 = value; }
// 0x004C6420
void PcScreenEffects::set_unknown5724(int on) { g_screen_effect_unknown5724 = on != 0; }
// 0x004C6460
void PcScreenEffects::set_unknown994(uint32_t value) { g_screen_effect_unknown994 = value; }
// 0x004C6470
void PcScreenEffects::set_unknown984(uint32_t value) { g_screen_effect_unknown984 = value; }
// 0x004C6440
void PcScreenEffects::set_unknown988(uint32_t value) { g_screen_effect_unknown988 = value; }
// 0x004C6450
void PcScreenEffects::set_unknown5728(uint32_t value) { g_screen_effect_unknown5728 = value; }

namespace {

// The engine and mesh code the loaders go through.
void change_sector_key(PcSector* sector, uint32_t checksum)
{
    reinterpret_cast<assets::Resource*>(sector)->set_checksum(checksum); // (folded with Resource's)
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

} // namespace

// 0x004C6650
bool PcSector::read_from_ram_block(uint8_t** cursor)
{
    auto* geometry = static_cast<PcGeom*>(geom);
    auto* p = reinterpret_cast<const uint32_t*>(*cursor);
    change_sector_key(this, *p++);
    uint32_t light_group = *p++;
    flags = *p++;
    geometry->mesh_count_ = *p++;
    float box[6];
    std::memcpy(box, p, sizeof(box));
    p += 6;
    geometry->bounds.min = {box[0], box[1], box[2], 1.0f};
    geometry->bounds.max = {box[3], box[4], box[5], 1.0f};
    p += 4; // (a bounding sphere, not kept)
    uint32_t billboard_type = 0;
    maths::Vec4 pivot, axis; // (their w not set, as the original)
    if (flags & 0x800000) {
        billboard_type = p[0];
        p += 4; // (the type and a vector not kept)
        std::memcpy(&pivot, p, 12);
        p += 3;
        pivot.z = -pivot.z;
        std::memcpy(&axis, p, 12);
        p += 3;
    }
    float centre[4] = {0, 0, 0, box[3]}; // (its w the box's last value, as the original's stack has it)
    for (uint32_t i = 0; i < geometry->mesh_count_; ++i) {
        MeshRef* mesh = new_mesh();
        std::memcpy(centre, p, 12);
        p += 3;
        float radius;
        std::memcpy(&radius, p++, 4);
        maths::Vec4 min, max;
        std::memcpy(&min, p, 12);
        p += 3;
        std::memcpy(&max, p, 12);
        p += 3;
        uint32_t mesh_flags = *p++;
        if (mesh_flags & 0x400)
            mesh->flags |= 2;
        if (mesh_flags & 0x20000)
            mesh->flags |= 0x20000;
        uint32_t material = *p++;
        uint32_t lists = *p++;
        // The index lists: read and dropped (the PC's are built from the vertices' own).
        uint16_t* indices[8] = {};
        uint32_t counts[8] = {};
        for (uint32_t j = 0; j < lists; ++j) {
            counts[j] = *p++;
            memory::Hub* manager = memory::Hub::get_pull();
            manager->enter_heap_scope(manager->top_down_heap_);
            uint32_t bytes = counts[j] * 2;
            indices[j] = static_cast<uint16_t*>(memory::alloc(bytes));
            memory::Hub::get_pull()->leave_heap_scope();
            std::memcpy(indices[j], p, bytes);
            p = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(p) + bytes);
        }
        mesh->load_index_ = static_cast<uint16_t>(i);
        auto* at = reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(p));
        mesh->read_from_ram_block(&at, material, *reinterpret_cast<SceneRef**>(geometry->scene + 0x280),
                                  nullptr); // (the vertices)
        p = reinterpret_cast<const uint32_t*>(at);
        set_mesh_sphere_and_box(mesh, centre, radius, &min, &max);
        finish_mesh(this, geometry, mesh, billboard_type, &pivot, &axis, &min, &max, static_cast<uint8_t>(light_group));
        for (uint16_t* list : indices)
            memory::Hub::get_pull()->dealloc(list);
    }
    geometry->mesh_count_ = count_loaded_meshes(geometry);
    *cursor = reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(p));
    return true;
}

// 0x004C6B10
bool PcSector::ingest_from_file(file::Hdl file, uint32_t unknown)
{
    auto* geometry = static_cast<PcGeom*>(geom);
    auto read = [file](void* into, int size, int count) { file::get_take_in(into, size, count, file); };
    uint32_t checksum;
    read(&checksum, 4, 1);
    change_sector_key(this, checksum);
    uint32_t light_group;
    read(&light_group, 4, 1);
    read(&flags, 4, 1);
    read(&geometry->mesh_count_, 4, 1);
    float box[6];
    read(box, 4, 6);
    // (the box's corners kept: the meshes' are read over them, their w still 1)
    maths::Vec4 min = {box[0], box[1], box[2], 1.0f}, max = {box[3], box[4], box[5], 1.0f};
    geometry->bounds.min = min;
    geometry->bounds.max = max;
    uint32_t sphere[4];
    read(sphere, 4, 4); // (not kept)
    uint32_t billboard_type = 0;
    maths::Vec4 unused, pivot, axis;
    if (flags & 0x800000) {
        read(&billboard_type, 4, 1);
        read(&unused, 0xC, 1);
        read(&pivot, 0xC, 1);
        read(&axis, 0xC, 1);
        unused.z = -unused.z;
        unused.w = 0.0f;
        pivot.w = 0.0f;
        axis.w = 0.0f;
        pivot.z = -pivot.z;
    }
    float centre[4] = {}; // (its w never read in: zero here)
    for (uint32_t i = 0; i < geometry->mesh_count_; ++i) {
        MeshRef* mesh = new_mesh();
        read(centre, 4, 3);
        float radius;
        read(&radius, 4, 1);
        read(&min, 4, 3);
        read(&max, 4, 3);
        uint32_t mesh_flags;
        read(&mesh_flags, 4, 1);
        if (mesh_flags & 0x400)
            mesh->flags |= 2;
        if (mesh_flags & 0x20000)
            mesh->flags |= 0x20000;
        uint32_t material, lists;
        read(&material, 4, 1);
        read(&lists, 4, 1);
        uint16_t* indices[8] = {};
        uint32_t counts[8] = {};
        for (uint32_t j = 0; j < lists; ++j) { // (read and dropped)
            read(&counts[j], 4, 1);
            memory::Hub* manager = memory::Hub::get_pull();
            manager->enter_heap_scope(manager->top_down_heap_);
            indices[j] = static_cast<uint16_t*>(memory::alloc(counts[j] * 2));
            memory::Hub::get_pull()->leave_heap_scope();
            read(indices[j], 2, static_cast<int>(counts[j]));
        }
        mesh->load_index_ = static_cast<uint16_t>(i);
        mesh->ingest_from_file(file, material, *reinterpret_cast<SceneRef**>(geometry->scene + 0x280),
                               reinterpret_cast<VertexArena*>(static_cast<uintptr_t>(flags & 0x800000 ? 0 : unknown)),
                               ((flags >> 23) & 1) != 0); // (the vertices; read as a byte: bit 23)
        set_mesh_sphere_and_box(mesh, centre, radius, &min, &max);
        finish_mesh(this, geometry, mesh, billboard_type, &pivot, &axis, &min, &max, static_cast<uint8_t>(light_group));
        for (uint16_t* list : indices)
            memory::Hub::get_pull()->dealloc(list);
    }
    geometry->mesh_count_ = count_loaded_meshes(geometry);
    return true;
}

namespace {
static VtableSlot g_c_pc_scene_vtable_slots[] = {&render::PcScene::teardown,
                                                 &render::PcScene::post_load,
                                                 &render::WorldScene::post_add,
                                                 &save_cards::SaveCard::always_true,
                                                 &save_cards::SaveCard::always_true,
                                                 &render::WorldScene::add_collision,
                                                 stubs::return_false,
                                                 &render::PcScene::spawn_sector,
                                                 stubs::ignore_argument,
                                                 &render::WorldScene::get_default_colour,
                                                 &render::WorldScene::set_rendered};
void** const kCPcSceneVtable = reinterpret_cast<void**>(g_c_pc_scene_vtable_slots);
static VtableSlot g_c_pc_screen_effects_vtable_slots[] = {
    &render::PcScreenEffects::teardown,         stubs::do_nothing,
    &render::PcScreenEffects::set_bloom,        &render::PcScreenEffects::set_bloom_cut_off,
    &render::PcScreenEffects::set_bloom_alphas, &render::PcScreenEffects::set_unknown990,
    &render::PcScreenEffects::set_unknown5724,  stubs::ignore_argument,
    &render::PcScreenEffects::set_unknown994,   stubs::ignore_argument,
    &render::PcScreenEffects::set_unknown984,   &render::PcScreenEffects::set_unknown988,
    &render::PcScreenEffects::set_unknown5728};
void** const kCPcScreenEffectsVtable = reinterpret_cast<void**>(g_c_pc_screen_effects_vtable_slots);
static VtableSlot g_c_pc_sector_vtable_slots[] = {&render::PcSector::teardown,
                                                  &render::PcSector::change_live,
                                                  &render::WorldCell::plat_get_shatter,
                                                  &render::PcSector::replicate,
                                                  stubs::ignore_argument,
                                                  stubs::do_nothing,
                                                  stubs::ignore_argument,
                                                  stubs::ignore_argument,
                                                  stubs::ignore_argument,
                                                  &render::PcSector::fetch_bounds,
                                                  &render::HitMovingMesh::fetch_facing};
void** const kCPcSectorVtable = reinterpret_cast<void**>(g_c_pc_sector_vtable_slots);
} // namespace

} // namespace render

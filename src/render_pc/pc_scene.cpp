// The PC scene and sector, the PC screen effects: see pc_scene.h.

#include "render_pc/pc_scene.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <new>

#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_scene.h"
#include "render_pc/geom.h"
#include "render_pc/pc_billboards.h"
#include "render_pc/pc_grass.h"
#include "render_pc/scene_file_stream.h"
#include "base/maths/shape_tests.h"
#include "base/maths/vec4.h"
#include "formats/scene_format.h"
#include "engine/gfx/sector.h"
#include "render_pc/sub_mesh.h"
#include "render_pc/mesh_list.h"
#include "engine/lists/key_map.h"
#include <gsl/pointers>
#include <array>
#include <span>
#include "engine/memory/arena_alloc.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/effects_state.h"

namespace render {

constexpr uint32_t kEightyBits = 0x42A00000;        // 80.0f
constexpr uint32_t kOneTwentyEightBits = 0x43000000; // 128.0f
constexpr uint32_t kNeutralColour = 0x80808080U;
constexpr int kSectorFlagBillboard = 0x800000;
constexpr int kSectorFlagSetsMeshFlag = 0x200000;
constexpr int kMeshFlagFromSector = 0x200;
constexpr int kBoxMaxZ = 5;                  // a sector's box record: min x y z, then max x y z (the others are 3 and 4)
constexpr int kFieldFlagMeshFlag2 = 0x400; // sets the mesh's flag 2
constexpr int kFieldFlagCarried = 0x20000;
constexpr int kMeshFlagCarried = 0x20000;
constexpr int kSceneFlagNoArena = 0x800000;  // the scene's meshes are not carved from the arena
constexpr int kSceneFlagNoArenaBit = 23;

namespace {
// NOT ORIGINAL grouping - pc_scene.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_scene_state()).
struct PcSceneState {
    uint32_t screen_effect_unknown5724_{};
    uint32_t screen_effect_unknown5728_{};
    // The empty bounding box (all zero at start, as the original's) and its guard.
    maths::BBox empty_bounding_box_{maths::BBox(maths::Vec4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F})};
    uint32_t empty_bounding_box_guard_{};
    uint32_t screen_effect_unknown984_{kEightyBits};
    uint32_t screen_effect_unknown988_{kOneTwentyEightBits};
    uint32_t screen_effect_unknown990_{kNeutralColour};
    uint32_t screen_effect_unknown994_{kNeutralColour};
};

PcSceneState& pc_scene_state()
{
    static core::NoDestroy<PcSceneState> state;
    return state.get();
}

} // namespace

namespace {

PcGeom* geometry_of(void* sector)
{
    return static_cast<PcGeom*>(static_cast<PcSector*>(sector)->geom_handle());
} // (+0x14: the getter 0x004A2BA0, folded with others)

// The first sector of a fresh iteration of the table (null if none).
void* first_sector(lists::KeyMap* table)
{
    table->set_iter_bucket(-1);
    table->set_iter_item(nullptr);
    if (!table->step_iterator() || !table->iter_item()) {
        return nullptr;
    }
    return table->iter_item()->value;
}
void* next_sector(lists::KeyMap* table)
{
    if (!table->step_iterator() || !table->iter_item()) {
        return nullptr;
    }
    return table->iter_item()->value;
}

} // namespace

// --- the sector ---

// 0x004C6530
void PcSector::plat_set_shatter(bool on)
{
    if (on && geom_handle()) {
        engine::shatter_geom(geom_handle());
    }
}

// 0x004C65C0
gsl::owner<PcSector*> PcSector::plat_clone(int /*instance*/, void* /*scene*/) { return new PcSector(); }

// 0x004C64E0
const maths::BBox* PcSector::fetch_empty_bounds()
{
    auto* empty = &pc_scene_state().empty_bounding_box_; // (the game's, made the first time: its guard's bit 0)
    uint32_t& made = pc_scene_state().empty_bounding_box_guard_;
    if ((made & 1) == 0U) {
        made |= 1;
        new (empty) maths::BBox();
    }
    return empty;
}

// --- the scene ---

// 0x004C6190
gsl::owner<WorldCell*> PcScene::plat_create_sector() { return new PcSector(); }

// 0x004C6200
void PcScene::unload_sectors()
{
    lists::KeyMap* table = sectors();
    for (const void* sector = first_sector(table); sector; sector = next_sector(table)) {
    }
}

// 0x004C6270
void PcScene::post_load(void* arena_word)
{
    auto* arena = static_cast<VertexArena*>(arena_word);
    lists::KeyMap* table = sectors();
    void* sector = first_sector(table);
    if (arena) {
        gpu().create_vertex_buffer(arena->size, gpu_value::kUsageWriteOnly, 0, gpu_value::kPoolDefault, &arena->buffer);
        gpu().lock_vertex_buffer(arena->buffer, 0, 0, &arena->data, 0);
        arena->used = 0;
        meshes_->set_arena_buffer(arena->buffer);
        meshes_->set_unknown80(arena->size);
    }
    for (; sector; sector = next_sector(table)) {
        PcGeom* geom = geometry_of(sector);
        geom->make_mesh_table(arena);
        geom->add_to_mesh_list(true);
    }
    if (arena) {
        gpu().unlock_vertex_buffer(arena->buffer);
        arena->data = nullptr;
    }
    meshes_->allocate_mesh_list();
    for (sector = first_sector(table); sector; sector = next_sector(table)) {
        geometry_of(sector)->add_to_mesh_list(false);
    }
    meshes_->order_meshes();
}

// --- the screen effects ---

// 0x004C63C0
void PcScreenEffects::set_bloom(int on) { effects_state().set_bloom_on(static_cast<uint32_t>(on != 0)); }
// 0x004C63E0
void PcScreenEffects::set_bloom_cut_off(uint32_t colour) { effects_state().set_bloom_cut_off(colour); }
// 0x004C63F0
void PcScreenEffects::set_bloom_alphas(int first, int second)
{
    effects_state().set_bloom_alpha_first(static_cast<uint32_t>(first));
    effects_state().set_bloom_alpha_second(static_cast<uint32_t>(second));
}
// 0x004C6410
void PcScreenEffects::set_bloom_colour(uint32_t colour) { pc_scene_state().screen_effect_unknown990_ = colour; }
// 0x004C6420
void PcScreenEffects::set_dof_on(int on)
{
    pc_scene_state().screen_effect_unknown5724_ = static_cast<uint32_t>(on != 0);
}
// 0x004C6460
void PcScreenEffects::set_dof_colour_a(uint32_t colour) { pc_scene_state().screen_effect_unknown994_ = colour; }
// 0x004C6470
void PcScreenEffects::set_dof_value_a(float value) { pc_scene_state().screen_effect_unknown984_ = std::bit_cast<uint32_t>(value); }
// 0x004C6440
void PcScreenEffects::set_dof_value_c(float value) { pc_scene_state().screen_effect_unknown988_ = std::bit_cast<uint32_t>(value); }
// 0x004C6450
void PcScreenEffects::set_dof_value_d(float value) { pc_scene_state().screen_effect_unknown5728_ = std::bit_cast<uint32_t>(value); }

namespace {

// The engine and mesh code the loaders go through.
void change_sector_key(PcSector* sector, uint32_t checksum)
{
    sector->set_checksum(checksum); // (the original calls a setter folded with Resource::set_checksum)
}
gsl::owner<MeshRef*> new_mesh()
{
    void* memory = memory::alloc(sizeof(MeshRef));
    return memory ? memory::construct_in<MeshRef>(memory) : nullptr; // (its constructor)
}
// (the original passed the box too: unused)
void set_mesh_sphere_and_box(MeshRef* mesh, std::span<const float, 3> centre, float radius)
{
    mesh->change_bound_sphere(centre, radius);
}
// The rest of a mesh's set-up, the same for both loaders: the scene's box grown, a billboard made, the mesh added.
void finish_mesh(PcSector* sector, PcGeom* geom, MeshRef* mesh, uint32_t billboard_type, const maths::Vec4* pivot,
                 const maths::Vec4* axis, const maths::Vec4* min, const maths::Vec4* max, uint8_t light_group)
{
    maths::BBox* bounds = &geom->scene()->meshes()->bounds();
    bounds->append_spot(*min);
    bounds->append_spot(*max);
    if ((sector->flags() & kSectorFlagBillboard) != 0U) {
        mesh->make_billboard(static_cast<int>(billboard_type), pivot, axis);
        billboard_pool().append_mesh(mesh);
    }
    if ((sector->flags() & kSectorFlagSetsMeshFlag) != 0U) {
        mesh->flags() |= kMeshFlagFromSector;
    }
    geom->append_mesh(mesh);
    mesh->set_bone(light_group); // (the bone slot)
    make_grass(geom, mesh);
}
// The meshes the load put on the list, counted (to a node that is the head, data -1, or null).
uint32_t count_loaded_meshes(const PcGeom* geom)
{
    uint32_t count = 0;
    for (const memory::ListNode* node = geom->load_list()->next();
         node->data() != memory::ListNode::head_marker() && node; node = node->next()) {
        ++count;
    }
    return count;
}

// What both of the original's sector loaders do (0x004C6650 from memory, 0x004C6B10 from a file - the same
// records, scene_format.h): its checksum, flags, box and billboard, then each mesh made, its vertices loaded by
// `load_body`. False if a record is damaged (the stream failed; the meshes made so far kept).
template <typename Stream, typename LoadBody>
bool read_sector(PcSector* sector, Stream& in, LoadBody load_body)
{
    auto* geometry = static_cast<PcGeom*>(sector->geom_handle());
    scene_format::Sector record;
    // Not original - each record read and checked first: a sector, mesh or index list cut short (or a mesh count
    // past what is there) ends the scene (the original read on past its end).
    const bool read = scene_format::read_sector(in, &record);
    change_sector_key(sector, record.checksum);
    sector->set_flags(record.flags);
    geometry->set_mesh_count(read ? record.mesh_count : 0);
    // (the box's corners kept: the meshes' are read over them, their w still 1)
    maths::Vec4 min = {
        .x = core::at(record.box, 0), .y = core::at(record.box, 1), .z = core::at(record.box, 2), .w = 1.0F};
    maths::Vec4 max = {
        .x = core::at(record.box, 3), .y = core::at(record.box, 4), .z = core::at(record.box, kBoxMaxZ), .w = 1.0F};
    geometry->bounds().min = min;
    geometry->bounds().max = max;
    const uint32_t billboard_type = record.billboard_type;
    const maths::Vec4 pivot = {
        .x = core::at(record.pivot, 0), .y = core::at(record.pivot, 1), .z = -core::at(record.pivot, 2), .w = 0.0F};
    const maths::Vec4 axis = {
        .x = core::at(record.axis, 0), .y = core::at(record.axis, 1), .z = core::at(record.axis, 2), .w = 0.0F};
    if (!read) {
        debug::printf("scene sector %08x damaged: cut short\n", record.checksum);
        in.fail();
        geometry->set_mesh_count(count_loaded_meshes(geometry));
        return false;
    }
    std::array<float, 4> centre = {}; // (its w not read)
    scene_format::SectorMesh fields;
    for (uint32_t i = 0; i < geometry->mesh_count(); ++i) {
        if (!scene_format::read_sector_mesh(in, &fields)) {
            debug::printf("scene sector %08x damaged: mesh %u cut short\n", record.checksum, i);
            in.fail();
            break;
        }
        gsl::owner<MeshRef*> mesh = new_mesh();
        std::memcpy(centre.data(), fields.centre.data(), sizeof(fields.centre));
        std::memcpy(&min, fields.min.data(), sizeof(fields.min));
        std::memcpy(&max, fields.max.data(), sizeof(fields.max));
        if (fields.flags & kFieldFlagMeshFlag2) {
            mesh->flags() |= 2;
        }
        if (fields.flags & kFieldFlagCarried) {
            mesh->flags() |= kMeshFlagCarried;
        }
        // (the index lists: read and dropped - the PC's are built from the vertices' own)
        mesh->set_load_index(static_cast<uint16_t>(i));
        if (!load_body(mesh, fields.material, geometry->scene()->meshes())) {
            memory::Hub::dealloc(mesh); // (nothing of it made yet)
            break;
        }
        set_mesh_sphere_and_box(mesh, std::span<const float>(centre).first<3>(), fields.radius);
        finish_mesh(sector, geometry, mesh, billboard_type, &pivot, &axis, &min, &max,
                    static_cast<uint8_t>(record.light_group));
    }
    geometry->set_mesh_count(count_loaded_meshes(geometry));
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
bool PcSector::ingest_from_file(scene_format::FileStream& in, VertexArena* arena)
{
    return read_sector(this, in, [this, &in, arena](MeshRef* mesh, uint32_t material, SceneRef* list) {
        // (the vertices into the arena unless a billboard; dynamic with the billboard bit, read as a byte: bit 23)
        return mesh->ingest_from_file(in, material, list, (flags_ & kSceneFlagNoArena) ? nullptr : arena, ((flags_ >> kSceneFlagNoArenaBit) & 1) != 0);
    });
}

} // namespace render

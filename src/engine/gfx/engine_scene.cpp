// The engine's scene (render::WorldScene): its members of 0x0049C910-0x0049D18E (see engine_scene.h), and two destructors of
// the collision it keeps (the grid's body, the static collision objects' deleting destructor). 0x0049CAF0 isn't a
// Ghidra function.

#include "engine/gfx/engine_scene.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/file/packs.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/sector.h"
#include "engine/net/buddy_status.h"
#include "engine/lists/key_map.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "formats/collision_format.h"
#include "engine/render/coll_capsule.h"
#include "engine/render/coll_sphere.h"
#include "engine/render/coll_rect.h"
#include "engine/render/coll_line.h"
#include "engine/render/coll_obj.h"
#include <gsl/pointers>
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace render {

namespace {

constexpr int kTriDataSize = 0x40;

// The engine's sector (not rewritten here; thiscall): its flags (+8) read / set / cleared, its name (+4 - the folded
// getter 0x005480C0), its box, drawn and collided with or not.
uint32_t sector_attribute_bits(void* sector) { return static_cast<WorldCell*>(sector)->fetch_bits(); }
void apply_sector_bits(void* sector, uint32_t flags) { static_cast<WorldCell*>(sector)->change_bits(flags); }
void reset_sector_bits(void* sector, uint32_t flags) { static_cast<WorldCell*>(sector)->clear_flags(flags); }
uint32_t sector_key_from_spec(void* sector) { return static_cast<WorldCell*>(sector)->checksum_; }
const maths::BBox* sector_box(void* sector) { return static_cast<WorldCell*>(sector)->fetch_bounds(); }
// (0x0049E9B0 tests the argument's low byte only)
void enable_sector(void* sector, uint32_t on)
{
    static_cast<WorldCell*>(sector)->change_live(static_cast<uint8_t>(on) != 0);
}

// (a static collision object's own print, compiled out: the empty 0x005480A0 with it in ECX)
inline void print_coll_static(const StaticCollider*) {}

// A C string copied (unbounded, as the original's inlined strcpy).
void copy_string(char* to, const char* from)
{
    while ((*to++ = *from++) != 0) {
    }
}

// The table's items in turn, as far as the first null item or value.
template <typename Body>
void for_each_sector(lists::KeyMap* table, Body body)
{
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    while (table->step_iterator()) {
        const lists::KeyMap::Item* item = table->iter_item_;
        if (!item || !item->value)
            break;
        body(item->value);
    }
}

} // namespace

// 0x0049C910
void WorldScene::finish_load(const char* name, void* arena)
{
    copy_string(label_.data(), name);
    post_load(arena);
}

// 0x0049C940
void WorldScene::finish_add(const char* name, uint32_t data)
{
    copy_string(add_name_.data(), name);
    add_data_ = data;
    added_ = true;
    post_add();
}

// 0x0049C9A0
void WorldScene::print_coll_statics()
{
    for (int i = 0; i < coll_static_count_; ++i) {
        debug::printf("%4d/%d ", i, coll_static_count_);
        print_coll_static(fetch_static_collision(i));
    }
}

// 0x0049C9F0
// NOLINTNEXTLINE(bugprone-pointer-arithmetic-on-polymorphic-object): an array of exactly this type
StaticCollider* WorldScene::fetch_static_collision(int index) { return &coll_statics_[index]; }

// 0x0049CA10
WorldCell* WorldScene::spawn_sector()
{
    WorldCell* sector = plat_create_sector();
    sector->geom_handle_ = nullptr;
    sector->hit_body_ = nullptr;
    return sector;
}

// 0x0049CA20
int make_collision_file_name(char* out, const char* level)
{
    const char* extension = platform_suffix();
    return std::snprintf(out, 0x7FFFFFFF, "levels\\%s\\%s%s.col.%s", level, level, "", extension);
}

// 0x0049CB00
void* WorldScene::look_up_sector(uint32_t name)
{
    const lists::KeyMap* table = sectors_;
    const lists::KeyMap::Item* item = &table->buckets_[name & ((1u << table->bits_) - 1)];
    for (; item; item = item->next) {
        if (item->key == name && item->value)
            return item->value;
    }
    return nullptr;
}

// 0x0049CB40
bool WorldScene::remove_scene_sector(void* sector)
{
    if (sector_attribute_bits(sector) & 0x40) {
        apply_sector_bits(sector, 8);
        sector_kept_ = true;
        return false;
    }
    if (look_up_sector(sector_key_from_spec(sector)) == sector) {
        const uint32_t name = sector_key_from_spec(sector);
        sectors_->erase_entry(name);
    }
    delete static_cast<WorldCell*>(sector);
    return true;
}

// 0x0049CF80
bool WorldScene::delete_sector_named(uint32_t name)
{
    void* sector = look_up_sector(name);
    return sector ? remove_scene_sector(sector) : false;
}

// 0x0049CBB0
bool WorldScene::load_collision_data(uint8_t* data, size_t size, int* count, StaticCollider** statics,
                                     uint8_t** objects, maths::BBox* box, int)
{
    // The header read and the sections laid out through collision_format.h (+4 objects, +8 vertex bytes, +0xC / +0x10
    // faces, +0x14 / +0x18 vertices; the objects 0x40 bytes each, then the vertices, faces, nodes and face lists).
    // Not original - a file whose sections, objects or trees aren't within it is refused (no collision; the original
    // fixed up and read whatever the offsets said). Valid files are laid out as before.
    collision_format::CollisionHeader header;
    collision_format::CollisionLayout layout;
    const bool valid = collision_format::lay_out_collision(data, size, &header, &layout) &&
                       collision_format::check_collision_objects(data, header, layout);
    if (!valid) {
        debug::printf("a damaged collision file: not loaded\n");
        header.object_count = 0;
    }
    *count = header.object_count;
    *objects = data + collision_format::kHeaderSize;
    const uintptr_t vertices = layout.vertices;
    const uintptr_t vertex_bytes = layout.vertex_bytes;
    const uintptr_t faces = layout.faces;
    uintptr_t buffer = layout.face_buffers;
    const uintptr_t nodes = layout.nodes;
    const uintptr_t face_lists = layout.face_lists;
    bool any_vertices = false;
    // the static objects: an array new (the count before them)
    *statics = new StaticCollider[static_cast<unsigned int>(header.object_count)];
    for (int i = 0; i < header.object_count; ++i) {
        auto* geometry = raw::field_at<TriMeshSource>(*objects, i * kTriDataSize);
        geometry->relocate(cell::of(this), static_cast<uint32_t>(vertices),
                           static_cast<uint32_t>(vertex_bytes), static_cast<uint32_t>(faces),
                           static_cast<uint32_t>(nodes), static_cast<uint32_t>(face_lists));
        geometry->set_up_all_faces();
        // NOLINTNEXTLINE(bugprone-pointer-arithmetic-on-polymorphic-object): an array of exactly this type
        StaticCollider* object = &(*statics)[i];
        object->assign_shape_data(geometry);
        buffer = cell::address(object->take_buffer(cell::pointer<uint8_t>(buffer), geometry));
        if (geometry->tri_count_ > 0) {
            box->append_spot(geometry->lower_limit_);
            box->append_spot(geometry->upper_limit_);
            any_vertices = true;
        }
    }
    if (!any_vertices) {
        const maths::Vec4 low = {-100.0f, -100.0f, -100.0f, 1.0f};
        box->append_spot(low);
        const maths::Vec4 high = {100.0f, 100.0f, 100.0f, 1.0f};
        box->append_spot(high);
    }
    return true;
}

// 0x0049CE60
bool WorldScene::init_collision_grid(const maths::BBox* box)
{
    if (!builds_collision_grid_)
        return false;
    collision_box_ = *box;
    void* block = memory::alloc(sizeof(CollGrid));
    CollGrid* grid = block ? memory::construct_in<CollGrid>(block) : nullptr; // (its box and cells constructed)
    coll_grid_ = grid;
    grid->prepare_all(&collision_box_); // (no check, as the original)
    return true;
}

// 0x0049CFB0
bool WorldScene::unload_collision()
{
    if (CollGrid* grid = coll_grid_) {
        grid->~CollGrid(); // (0x0049C980: the cells destroyed, last first)
        memory::dealloc(grid);
        coll_grid_ = nullptr;
    }
    if (coll_statics_) {
        delete[] coll_statics_; // (0x0049CE00, the array's deleting destructor)
        coll_statics_ = nullptr;
    }
    if (core::at(pip_name_, 0)) {
        packs::offload(pip_name_.data());
        core::at(pip_name_, 0) = 0;
    }
    return true;
}

// 0x0049D030
void WorldScene::delete_marked_sectors()
{
    if (!coll_grid_)
        return;
    for_each_sector(sectors_, [](void* sector) {
        if (sector_attribute_bits(sector) & 8) {
            reset_sector_bits(sector, 0x40);
            delete static_cast<WorldCell*>(sector);
        }
    });
    coll_grid_->get_clear();
    sectors_->empty_table();
}

// 0x0049D0B0
void WorldScene::set_sectors_in_box(const maths::Vec4* box, uint32_t on, int)
{
    float min_x = box[0].x, min_y = box[0].y, min_z = box[0].z;
    float max_x = box[1].x, max_y = box[1].y, max_z = box[1].z;
    for_each_sector(sectors_, [&](void* sector) {
        const maths::BBox* bounds = sector_box(sector);
        const auto* f = raw::as<const float>(bounds);
        // (each "not past it": NaN counts as touching)
        if (!(min_x > f[4]) && !(f[0] > max_x) && !(min_z > f[6]) && !(f[2] > max_z) && !(min_y > f[5]) &&
            !(f[1] > max_y))
            enable_sector(sector, on);
    });
}

// --- the virtuals' base bodies ---

// 0x0049CA50 / 0x0049CA60 / 0x0049CA70
void WorldScene::post_load(void*) { debug::printf("STUB: PostLoad\n"); }
void WorldScene::set_rendered(int) { debug::printf("STUB: plat_set_rendered\n"); }
void WorldScene::post_add() { debug::printf("STUB: PostAdd\n"); }
// 0x0049CA80 / 0x0049CAA0 / 0x0049CAC0 / 0x0049CAE0
bool WorldScene::load_textures(const char*)
{
    debug::printf("STUB: LoadTextures\n");
    return false;
}
bool WorldScene::load_collision(const char*)
{
    debug::printf("STUB: LoadCollision\n");
    return false;
}
bool WorldScene::add_collision(const char*)
{
    debug::printf("STUB: AddCollision\n");
    return false;
}
bool WorldScene::unload_add_scene()
{
    debug::printf("STUB: UnloadAddScene\n");
    return false;
}
// 0x0049CAF0
gsl::owner<WorldCell*> WorldScene::plat_create_sector()
{
    debug::printf("STUB: plat_create_sector\n");
    return nullptr;
}
// 0x0049CF60
uint8_t* WorldScene::get_default_colour(uint8_t* out)
{
    out[0] = out[1] = out[2] = out[3] = 0x80;
    return out;
}

} // namespace render

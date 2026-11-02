// The engine's scene (render::WorldScene): its members of 0x0049C910-0x0049D18E (see engine_scene.h), and two destructors of
// the collision it keeps (the grid's body, the static collision objects' deleting destructor). 0x0049CAF0 isn't a
// Ghidra function.

#include "engine/gfx/engine_scene.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
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
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"

namespace render {

namespace {


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
void copy_string(std::span<char> to, const char* from)
{
    std::size_t i = 0;
    for (core::CStringCursor at(from);; ++at, ++i) {
        core::at(to, i) = *at;
        if (!*at)
            break;
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
    copy_string(label_, name);
    post_load(arena);
}

// 0x0049C940
void WorldScene::finish_add(const char* name, TextureSet* textures)
{
    copy_string(add_name_, name);
    add_data_ = textures;
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
StaticCollider* WorldScene::fetch_static_collision(int index)
{
    const std::span<StaticCollider> run(coll_statics_, static_cast<std::size_t>(coll_static_count_));
    return run.empty() ? nullptr : &core::at(run, index); // (no collision loaded: none, as the original's null array)
}

// 0x0049CA10
gsl::owner<WorldCell*> WorldScene::spawn_sector()
{
    gsl::owner<WorldCell*> sector = plat_create_sector();
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
WorldCell* WorldScene::look_up_sector(uint32_t name)
{
    const lists::KeyMap* table = sectors_;
    const lists::KeyMap::Item* item = &core::at(table->bucket_run(), name & ((1u << table->bits_) - 1));
    for (; item; item = item->next) {
        if (item->key == name && item->value)
            return static_cast<WorldCell*>(item->value);
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
    gsl::owner<WorldCell*> cell = memory::adopt<WorldCell*>(sector); // (the table's)
    delete cell;
    return true;
}

// 0x0049CF80
bool WorldScene::delete_sector_named(uint32_t name)
{
    void* sector = look_up_sector(name);
    return sector ? remove_scene_sector(sector) : false;
}

// 0x0049CBB0
bool WorldScene::load_collision_data(uint8_t* data, size_t size, int* count, gsl::owner<StaticCollider*>* statics,
                                     uint8_t** objects, maths::BBox* box, int)
{
    // The header read and the sections laid out through collision_format.h (+4 objects, +8 vertex bytes, +0xC / +0x10
    // faces, +0x14 / +0x18 vertices; the objects 0x40 bytes each, then the vertices, faces, nodes and face lists).
    // Not original - a file whose sections, objects or trees aren't within it is refused (no collision; the original
    // fixed up and read whatever the offsets said). Valid files are laid out as before.
    collision_format::CollisionHeader header;
    collision_format::CollisionLayout layout;
    const std::span<uint8_t> file(data, size);
    const bool valid = collision_format::lay_out_collision(file, &header, &layout) &&
                       collision_format::check_collision_objects(file, header, layout);
    if (!valid) {
        debug::printf("a damaged collision file: not loaded\n");
        header.object_count = 0;
    }
    *count = header.object_count;
    *objects = file.subspan(std::min(collision_format::kHeaderSize, size)).data();
    uint8_t* buffer = collision_format::face_buffers(layout);
    bool any_vertices = false;
    // the static objects: an array new (the count before them)
    *statics = new StaticCollider[static_cast<unsigned int>(header.object_count)];
    for (int i = 0; i < header.object_count; ++i) {
        collision_format::relocate_object(file, i, layout);
        auto* geometry = collision_format::object_record<TriMeshSource>(file, i);
        geometry->set_up_all_faces();
        StaticCollider* object = &core::at(std::span<StaticCollider>(*statics, static_cast<std::size_t>(header.object_count)), i);
        object->assign_shape_data(geometry);
        buffer = object->take_buffer(buffer, geometry);
        if (geometry->tri_count_ > 0) {
            box->append_spot(geometry->bounds_.min);
            box->append_spot(geometry->bounds_.max);
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
            gsl::owner<WorldCell*> cell = memory::adopt<WorldCell*>(sector); // (the table's)
            delete cell;
        }
    });
    coll_grid_->get_clear();
    sectors_->empty_table();
}

// 0x0049D0B0
void WorldScene::set_sectors_in_box(const maths::Vec4* box, uint32_t on, int)
{
    const std::span<const maths::Vec4, 2> corners(box, 2);
    float min_x = corners.front().x, min_y = corners.front().y, min_z = corners.front().z;
    float max_x = corners.back().x, max_y = corners.back().y, max_z = corners.back().z;
    for_each_sector(sectors_, [&](void* sector) {
        const maths::BBox* bounds = sector_box(sector);
        const maths::Vec4& low = bounds->min;
        const maths::Vec4& high = bounds->max;
        // (each "not past it": NaN counts as touching)
        if (!(min_x > high.x) && !(low.x > max_x) && !(min_z > high.z) && !(low.z > max_z) && !(min_y > high.y) &&
            !(low.y > max_y))
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
    const std::span<uint8_t, 4> colour(out, 4);
    std::ranges::fill(colour, uint8_t{0x80});
    return out;
}

} // namespace render

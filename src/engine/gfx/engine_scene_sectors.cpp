// The engine's scene (see engine_scene.h): its members of 0x0049D1A0-0x0049E2EF - the constructor and destructor,
// the statistics, sectors added and cloned, the collision added and the super sectors (the collision grid) brought up
// to date, a scene added over it - and the nodes of its sector lists.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "engine/gfx/engine_geom.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/packs.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/texture_dict.h"
#include "engine/lists/key_map.h"
#include "engine/render/coll_grid.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/script/param_struct.h"
#include "platform/code_address.h"
#include "engine/gfx/weather.h"
#include "engine/gfx/pc_texture.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace render {

namespace {

// The rest of the scene (0x0049C910-0x0049D18E, engine_scene.cpp): the sector of a name, a sector deleted, a scene
// file's collision data read, the collision let go of.
WorldCell* scene_sector(WorldScene* scene, uint32_t name)
{
    return static_cast<WorldCell*>(scene->look_up_sector(name));
}
bool delete_scene_sector(WorldScene* scene, WorldCell* sector) { return scene->remove_scene_sector(sector); }
bool scene_load_collision_data(WorldScene* scene, uint8_t* data, size_t size, int* count,
                               gsl::owner<StaticCollider*>* statics, uint8_t** objects, maths::BBox* box, int flag)
{
    return scene->load_collision_data(data, size, count, statics, objects, box, flag);
}
bool scene_unload_collision(WorldScene* scene) { return scene->unload_collision(); }

// The platform's new sector (slot 7), its geometry and collision cleared.
gsl::owner<WorldCell*> alloc_sector(WorldScene* scene)
{
    gsl::owner<WorldCell*> sector = scene->plat_create_sector();
    sector->geom_handle_ = nullptr;
    sector->hit_body_ = nullptr;
    return sector;
}

// NOLINTNEXTLINE(bugprone-pointer-arithmetic-on-polymorphic-object): an array of exactly this type
StaticCollider* coll_static_at(StaticCollider* statics, int index) { return &statics[index]; }

// The lists (lists::Foremost / Node): a head ends its list (its data is -1).
bool is_head(const memory::ListNode* node) { return node->data == memory::ListNode::head_marker(); }
// The node after `node`, or null at the head.
memory::ListNode* get_next_node(const memory::ListNode* node) { return is_head(node->next) ? nullptr : node->next; }
// The first node, or null when the list's empty (the original checks the head's next is no head and not null).
memory::ListNode* head_of_scene_list(const memory::ListNode* head)
{
    return is_head(head->next) ? nullptr : head->next;
}
// A head (on the stack or new).
void setup_list_sentinel(memory::ListNode* head) { head->make_head(); }
// A new head (null without memory).
memory::ListNode* new_head() { return memory::ListNode::create_head(); }
// A new node (null without memory) for `data`.
memory::ListNode* new_node(void* data) { return memory::ListNode::create(data); }
// `node` put at the end of the list (no memory: written through null, as the original).
void append_last(memory::ListNode* head, memory::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
// Every node of a list deleted (out of the list, freed).
void delete_nodes(memory::ListNode* head)
{
    for (memory::ListNode* node = head_of_scene_list(head); node;) {
        memory::ListNode* next = get_next_node(node);
        memory::ListNode::destroy(node);
        node = next;
    }
}

// The sectors in the table in turn, as far as the first null item or value.
template <typename Body>
void for_each_sector(WorldScene* scene, Body body)
{
    scene->sectors_->iter_bucket_ = -1;
    scene->sectors_->iter_item_ = nullptr;
    for (;;) {
        lists::KeyMap* table = scene->sectors_;
        if (!table->step_iterator() || !table->iter_item_ || !table->iter_item_->value)
            break;
        body(static_cast<WorldCell*>(table->iter_item_->value));
    }
}

void put_sector(WorldScene* scene, WorldCell* sector)
{
    const uint32_t key = sector->checksum_;
    scene->sectors_->store_entry(key, sector);
}

// A geom's counts (the engine's CGeom calls: 0x0048DF70, the slot-33 dispatcher, 0x0049C6F0).
int geom_count70(void* geom) // (through the jump 0x0048DF70)
{
    return static_cast<int>(static_cast<Geom*>(geom)->fetch_vertex_tally());
}
int geom_slot33(void* geom) { return static_cast<int>(static_cast<Geom*>(geom)->plat_get_num_render_polys()); }
int geom_count_c_6f_0(void* geom) // (through the jump 0x0049C6F0)
{
    return static_cast<int>(static_cast<Geom*>(geom)->get_diagnostic_count());
}

} // namespace

// 0x0049D390 (the original's SEH frame around the table's `new` left out)
WorldScene::WorldScene(int bits)
{
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set twice, as the original does
    coll_grid_ = nullptr;
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    sectors_ = memory ? memory::construct_in<lists::KeyMap>(memory, bits) : nullptr;
    core::at(label_, 0) = '\0';
    core::at(pip_name_, 0) = '\0';
    coll_grid_ = nullptr;
    core::at(add_name_, 0) = '\0';
    hidden_sectors_ = new_head();
    add_scene_sectors_ = new_head();
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x0049DD10
WorldScene::~WorldScene()
{
    if (sectors_) {
        for_each_sector(this, [](WorldCell* sector) {
            sector->clear_flags(kSectorInGrid);
            gsl::owner<WorldCell*> owned = memory::owned_element(sector); // (the table's)
            delete owned;
        });
        if (lists::KeyMap* table = sectors_) {
            if (table->buckets_) {
                table->empty_table();
                memory::dealloc(table->buckets_);
                table->buckets_ = nullptr;
            }
            memory::dealloc(table);
        }
    }
    scene_unload_collision(this);
    memory::ListNode::destroy(hidden_sectors_);
    memory::ListNode::destroy(add_scene_sectors_);
    if (add_data_)
        unload_tex_dict(add_data_);
}

// 0x0049D1A0 (the third count is added twice under the same name, as the original)
void WorldScene::fetch_diagnostics(script::Struct* info)
{
    info->append_word(0xC3F4169A, label_.data());
    int sectors = 0, count70 = 0, slot33 = 0, slot33_again = 0, count_c_6f_0 = 0;
    if (sectors_) {
        for_each_sector(this, [&](WorldCell* sector) {
            ++sectors;
            if (void* geom = sector->geom_handle_) {
                count70 += geom_count70(geom);
                slot33 += geom_slot33(geom);
                slot33_again += geom_slot33(geom);
                count_c_6f_0 += geom_count_c_6f_0(geom);
            }
        });
    }
    info->put_int(0x4A6BF967, sectors);
    info->put_int(0x0AC30A9D, coll_static_count_);
    info->put_int(0x969D3AF6, count70);
    info->put_int(0xD576DF05, slot33);
    info->put_int(0x1E3061EE, slot33_again);
    info->put_int(0x1E3061EE, slot33_again);
    info->put_int(0x78CAD2E8, count_c_6f_0);
    info->put_int(0x5714C480, static_cast<int>(core::down_cast<PcTexDict>(texture_set_)->unknown10_));
}

// 0x0049D4E0
void WorldScene::insert_sector(WorldCell* sector) { put_sector(this, sector); }

// 0x0049D510
uint32_t WorldScene::duplicate_scene_sector(WorldCell* sector, WorldScene* scene, int instance, bool flag,
                                            uint32_t name)
{
    if (!scene)
        scene = this;
    gsl::owner<WorldCell*> clone = sector->replicate(instance, flag, scene);
    if (!clone)
        return 0;
    if (name)
        clone->checksum_ = name;
    put_sector(scene, clone);
    if (flag)
        scene->sector_kept_ = true;
    return clone->checksum_;
}

// 0x0049DF30
uint32_t WorldScene::clone_sector_named(uint32_t section_id, WorldScene* scene, int instance, bool flag, uint32_t name)
{
    WorldCell* sector = scene_sector(this, section_id);
    if (!sector)
        return 0;
    return duplicate_scene_sector(sector, scene, instance, flag, name);
}

// 0x0049D590 (the original's SEH frame around the grid's `new` left out)
bool WorldScene::add_collision_data(uint8_t* data, size_t size, int flag)
{
    scene_load_collision_data(this, data, size, &coll_static_count_, &coll_statics_, &collision_blob_, &collision_box_,
                              flag);
    if (coll_static_count_ > 0) {
        if (builds_collision_grid_) {
            void* memory = memory::alloc(sizeof(CollGrid));
            CollGrid* grid =
                memory ? memory::construct_in<CollGrid>(memory) : nullptr; // (its box and cells constructed)
            coll_grid_ = grid;
            grid->prepare_all(&collision_box_);
            coll_grid_->get_fill(std::span<StaticCollider>(coll_statics_, static_cast<size_t>(coll_static_count_)));
        }
        for (int i = 0; i < coll_static_count_; ++i) {
            const StaticCollider* coll = coll_static_at(coll_statics_, i);
            const uint32_t name = coll->checksum_;
            gsl::owner<WorldCell*> sector = memory::adopt<WorldCell*>(sectors_->fetch_entry(name));
            if (!sector) {
                sector = alloc_sector(this);
                sector->checksum_ = name;
                sector->change_live(true);
                put_sector(this, sector);
            }
            sector->set_coll_obj(coll_static_at(coll_statics_, i));
            sector->change_bits(kSectorInGrid);
        }
    }
    if (script::fetch_int_value(0xBA1E4254)) {
        g_weather->tick_grid(); // (through the jump 0x004A1090)
    }
    return true;
}

// 0x0049DF70 (the original sprintfs into its 128 bytes unbounded)
bool WorldScene::load_collision_file(const char* name, int flag)
{
    std::snprintf(pip_name_.data(), sizeof(pip_name_), "levels\\%s\\%s%s.col.%s", name, name, "", platform_suffix());
    auto* data = static_cast<uint8_t*>(packs::read_in(pip_name_.data(), false, nullptr, nullptr, nullptr));
    if (!data)
        return false;
    add_collision_data(data, static_cast<size_t>(packs::get_file_size(pip_name_.data())), flag); // (not original: sized)
    return true;
}

// 0x0049D780 (the original's four list heads on its stack, and its SEH frame)
void WorldScene::get_update_super_sectors(memory::ListNode* added)
{
    if (!coll_grid_)
        return;
    memory::ListNode to_add{}, to_remove{}, moved{}, to_delete{}; // (collision objects; sectors)
    setup_list_sentinel(&to_add);
    setup_list_sentinel(&to_remove);
    setup_list_sentinel(&moved);
    setup_list_sentinel(&to_delete);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    for_each_sector(this, [&](WorldCell* sector) {
        if (sector->fetch_bits() & kSectorAddToGrid) {
            append_last(&to_add, new_node(sector->hit_body_));
            sector->clear_flags(kSectorAddToGrid);
            sector->change_bits(kSectorInGrid);
        }
        if (sector->fetch_bits() & (kSectorDelete | kSectorRemoveFromGrid)) {
            append_last(&to_remove, new_node(sector->hit_body_));
            if (sector->fetch_bits() & kSectorDelete)
                append_last(&to_delete, new_node(sector));
            else
                sector->clear_flags(kSectorRemoveFromGrid);
        }
        if (sector->fetch_bits() & kSectorMoved) {
            append_last(&moved, new_node(sector->hit_body_));
            sector->clear_flags(kSectorMoved);
        }
    });
    if (added && !is_head(added->next) && added->next) {
        for (const memory::ListNode* node = added->next; node;) {
            const memory::ListNode* next = get_next_node(node);
            auto* sector = static_cast<WorldCell*>(node->data);
            bool listed = false;
            for (const memory::ListNode* removing = head_of_scene_list(&to_remove); removing && !listed;
                 removing = get_next_node(removing))
                listed = removing->data == sector->hit_body_;
            if (!listed) {
                append_last(&to_remove, new_node(sector->hit_body_));
                sector->clear_flags(kSectorRemoveFromGrid);
            }
            node = next;
        }
    }
    memory::Hub::get_pull()->leave_heap_scope();
    coll_grid_->tick_frame(&to_add, &to_remove, &moved);
    delete_nodes(&to_add);
    delete_nodes(&to_remove);
    delete_nodes(&moved);
    for (memory::ListNode* node = head_of_scene_list(&to_delete); node;) {
        memory::ListNode* next = get_next_node(node);
        gsl::owner<WorldCell*> sector = memory::adopt<WorldCell*>(node->data);
        const uint32_t key = sector->checksum_;
        sectors_->erase_entry(key);
        sector->clear_flags(kSectorInGrid);
        delete sector;
        memory::ListNode::destroy(node);
        node = next;
    }
    sector_kept_ = false;
}

// 0x0049E1B0
bool WorldScene::update_super_sectors_if_marked(memory::ListNode* added)
{
    if (!sector_kept_)
        return false;
    get_update_super_sectors(added);
    return true;
}

// 0x0049DFD0
bool WorldScene::load_add_scene(const char* name)
{
    maths::BBox box;
    if (!coll_statics_)
        return false;
    std::snprintf(add_pip_name_.data(), sizeof(add_pip_name_), "levels\\%s\\%s%s.col.%s", name, name, "", platform_suffix());
    // (the base's collision file, not the one just named - as the original)
    if (auto* data = static_cast<uint8_t*>(packs::read_in(pip_name_.data(), false, nullptr, nullptr, nullptr)))
        scene_load_collision_data(this, data, static_cast<size_t>(packs::get_file_size(pip_name_.data())),
                                  &add_coll_static_count_, &add_coll_statics_, &add_coll_objects_, &box, 0);
    if (add_coll_static_count_ > 0) {
        for (int i = 0; i < add_coll_static_count_; ++i) {
            const uint32_t section_id = coll_static_at(add_coll_statics_, i)->checksum_;
            gsl::owner<WorldCell*> sector = memory::adopt<WorldCell*>(sectors_->fetch_entry(section_id));
            if (!sector) {
                sector = alloc_sector(this);
                sector->checksum_ = section_id;
                sector->change_live(true);
                sector->change_bits(kSectorAddToGrid);
                sector_kept_ = true;
                put_sector(this, sector);
                append_last(add_scene_sectors_, new_node(sector));
            }
            sector->set_coll_obj(coll_static_at(add_coll_statics_, i));
        }
        get_update_super_sectors(hidden_sectors_);
    }
    return add_collision(name);
}

// 0x0049DDE0
bool WorldScene::flip_extra_scene()
{
    if (!head_of_scene_list(add_scene_sectors_))
        return false;
    added_ = !added_;
    memory::ListNode* hide = added_ ? hidden_sectors_ : add_scene_sectors_;
    const memory::ListNode* show = added_ ? add_scene_sectors_ : hidden_sectors_;
    for (const memory::ListNode* node = head_of_scene_list(hide); node;) {
        const memory::ListNode* next = get_next_node(node);
        auto* sector = static_cast<WorldCell*>(node->data);
        sector->change_bits(kSectorRemoveFromGrid);
        sector_kept_ = true;
        const uint32_t key = sector->checksum_;
        sectors_->erase_entry(key);
        sector->change_live(false);
        node = next;
    }
    for (const memory::ListNode* node = head_of_scene_list(show); node;) {
        const memory::ListNode* next = get_next_node(node);
        auto* sector = static_cast<WorldCell*>(node->data);
        sector->change_bits(kSectorAddToGrid);
        sector_kept_ = true;
        put_sector(this, sector);
        sector->change_live(true);
        node = next;
    }
    get_update_super_sectors(hide);
    return added_;
}

// 0x0049E200 (the added collision's array and file are let go of but not cleared, as the original)
bool WorldScene::unload_added_scene()
{
    if (added_)
        flip_extra_scene();
    if (!unload_add_scene())
        return false;
    delete[] add_coll_statics_; // (the vector deleting destructor; the pointer kept, as the original)
    if (add_coll_objects_)
        packs::offload(add_pip_name_.data());
    if (hidden_sectors_)
        delete_nodes(hidden_sectors_);
    if (add_scene_sectors_) {
        for (memory::ListNode* node = head_of_scene_list(add_scene_sectors_); node;) {
            memory::ListNode* next = get_next_node(node);
            delete_scene_sector(this, static_cast<WorldCell*>(node->data));
            memory::ListNode::destroy(node);
            node = next;
        }
    }
    added_ = false;
    return true;
}

} // namespace render

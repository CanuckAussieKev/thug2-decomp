// The engine's scene (see engine_scene.h): its members of 0x0049D1A0-0x0049E2EF - the constructor and destructor,
// the statistics, sectors added and cloned, the collision added and the super sectors (the collision grid) brought up
// to date, a scene added over it - and the nodes of its sector lists.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

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
#include "engine/core/code_address.h"
#include "engine/core/game_vtables.h"
#include "engine/core/stubs.h"
#include "engine/gfx/weather.h"

namespace render {

namespace {

extern void** const kSceneVtable;
constexpr uint32_t kCollStaticSize = 0x18;
extern void** const kSectorNodeVtable;
extern void** const kSectorHeadVtable;
void** const& g_coll_node_vtable = vt::kCollGridNode; // (the collision grid's lists')
void** const& g_coll_head_vtable = vt::kCollGridHead;

template <typename R = void, typename... A>
R Virtual(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

// The rest of the scene (0x0049C910-0x0049D18E, engine_scene.cpp): the sector of a name, a sector deleted, a scene
// file's collision data read, the collision let go of.
WorldCell* scene_sector(WorldScene* scene, uint32_t name)
{
    return static_cast<WorldCell*>(scene->look_up_sector(name));
}
bool delete_scene_sector(WorldScene* scene, WorldCell* sector) { return scene->remove_scene_sector(sector); }
bool scene_load_collision_data(WorldScene* scene, const uint8_t* data, int* count, StaticCollider** statics,
                               uint8_t** objects, maths::BBox* box, int flag)
{
    return scene->load_collision_data(data, count, statics, objects, box, flag);
}
bool scene_unload_collision(WorldScene* scene) { return scene->unload_collision(); }

// The platform's new sector (slot 7), its geometry and collision cleared.
WorldCell* alloc_sector(WorldScene* scene)
{
    auto* sector = Virtual<WorldCell*>(scene, 7);
    sector->geom_handle_ = nullptr;
    sector->hit_body_ = nullptr;
    return sector;
}

StaticCollider* coll_static_at(StaticCollider* statics, int index)
{
    return reinterpret_cast<StaticCollider*>(reinterpret_cast<uint8_t*>(statics) + index * kCollStaticSize);
}

// The lists (lists::Foremost / Node): a head ends its list (its data is -1).
bool is_head(const memory::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
// The node after `node`, or null at the head.
memory::ListNode* get_next_node(const memory::ListNode* node) { return is_head(node->next) ? nullptr : node->next; }
// The first node, or null when the list's empty (the original checks the head's next is no head and not null).
memory::ListNode* head_of_scene_list(const memory::ListNode* head)
{
    return is_head(head->next) ? nullptr : head->next;
}
// A head (on the stack or new).
void setup_list_sentinel(memory::ListNode* head, void** vtable)
{
    head->vtable = vtable;
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
}
// A new head (null without memory).
memory::ListNode* new_head()
{
    auto* head = static_cast<memory::ListNode*>(memory::alloc_zeroed(sizeof(memory::ListNode)));
    if (head)
        setup_list_sentinel(head, kSectorHeadVtable);
    return head;
}
// A new node (null without memory) for `data`.
memory::ListNode* new_node(void** vtable, void* data)
{
    auto* node = static_cast<memory::ListNode*>(memory::alloc_zeroed(sizeof(memory::ListNode)));
    if (node) {
        node->vtable = vtable;
        node->data = data;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    return node;
}
// `node` put at the end of the list (no memory: written through null, as the original).
void append_last(memory::ListNode* head, memory::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
// Every node of a list deleted (its virtual [0]).
void delete_nodes(memory::ListNode* head)
{
    for (memory::ListNode* node = head_of_scene_list(head); node;) {
        memory::ListNode* next = get_next_node(node);
        Virtual<void*>(node, 0, 1u);
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
    uint32_t key = sector->checksum_;
    scene->sectors_->store_entry(key, sector);
}

// A geom's counts (the engine's CGeom calls: 0x0048DF70, the slot-33 dispatcher, 0x0049C6F0).
int geom_count70(void* geom) // (slot 36, through the jump 0x0048DF70)
{
    return reinterpret_cast<int(__thiscall*)(void*)>((*static_cast<void***>(geom))[36])(geom);
}
int geom_slot33(void* geom) { return reinterpret_cast<int(__thiscall*)(void*)>(&virtual_slot33_jump)(geom); }
int geom_count_c_6f_0(void* geom) // (slot 35, through the jump 0x0049C6F0)
{
    return reinterpret_cast<int(__thiscall*)(void*)>((*static_cast<void***>(geom))[35])(geom);
}

} // namespace

// 0x0049D390 (the original's SEH frame around the table's `new` left out)
WorldScene* WorldScene::make_new(int bits)
{
    vtable_ = reinterpret_cast<void**>(kSceneVtable);
    new (&unknown_box_) maths::BBox();
    new (&collision_box_) maths::BBox();
    coll_grid_ = nullptr;
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    sectors_ = memory ? new (memory) lists::KeyMap(bits) : nullptr;
    label_[0] = '\0';
    pip_name_[0] = '\0';
    coll_grid_ = nullptr;
    coll_statics_ = nullptr;
    collision_blob_ = nullptr;
    coll_static_count_ = 0;
    add_name_[0] = '\0';
    add_data_ = 0;
    add_coll_statics_ = nullptr;
    add_coll_objects_ = nullptr;
    add_coll_static_count_ = 0;
    sector_kept_ = false;
    added_ = false;
    hidden_sectors_ = new_head();
    add_scene_sectors_ = new_head();
    builds_collision_grid_ = false;
    is_sky_ = false;
    return this;
}

// 0x0049DD10
void WorldScene::destroy_body()
{
    vtable_ = reinterpret_cast<void**>(kSceneVtable);
    if (sectors_) {
        for_each_sector(this, [](WorldCell* sector) {
            sector->clear_flags(kSectorInGrid);
            Virtual<void*>(sector, 0, 1u);
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
    if (hidden_sectors_)
        Virtual<void*>(hidden_sectors_, 0, 1u);
    if (add_scene_sectors_)
        Virtual<void*>(add_scene_sectors_, 0, 1u);
    if (add_data_)
        unload_tex_dict(reinterpret_cast<TextureSet*>(add_data_));
}

// 0x0049E1D0
WorldScene* WorldScene::teardown(unsigned int free_flags)
{
    destroy_body();
    if (free_flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x0049D1A0 (the third count is added twice under the same name, as the original)
void WorldScene::fetch_diagnostics(script::Struct* info)
{
    info->append_word(0xC3F4169A, label_);
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
    info->put_int(0x5714C480, *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(texture_set_) + 0x10));
}

// 0x0049D4E0
void WorldScene::insert_sector(WorldCell* sector) { put_sector(this, sector); }

// 0x0049D510
uint32_t WorldScene::duplicate_scene_sector(WorldCell* sector, WorldScene* scene, int instance, bool flag,
                                            uint32_t name)
{
    if (!scene)
        scene = this;
    WorldCell* clone = sector->replicate(instance, flag, scene);
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
bool WorldScene::add_collision_data(const uint8_t* data, int flag)
{
    scene_load_collision_data(this, data, &coll_static_count_, &coll_statics_, &collision_blob_, &collision_box_, flag);
    if (coll_static_count_ > 0) {
        if (builds_collision_grid_) {
            void* memory = memory::alloc(sizeof(CollGrid));
            CollGrid* grid = nullptr;
            if (memory) {
                grid = static_cast<CollGrid*>(memory);
                new (memory) maths::BBox();
                for (int x = 0; x < CollGrid::kSize; ++x) {
                    for (int z = 0; z < CollGrid::kSize; ++z)
                        grid->get_cell(x, z)->make_new();
                }
            }
            coll_grid_ = grid;
            grid->prepare_all(&collision_box_);
            coll_grid_->get_fill(coll_statics_, coll_static_count_);
        }
        for (int i = 0; i < coll_static_count_; ++i) {
            StaticCollider* coll = coll_static_at(coll_statics_, i);
            uint32_t name = reinterpret_cast<HitMesh*>(coll)->checksum_;
            auto* sector = static_cast<WorldCell*>(sectors_->fetch_entry(name));
            if (!sector) {
                sector = alloc_sector(this);
                sector->checksum_ = name;
                sector->change_live(true);
                put_sector(this, sector);
            }
            sector->set_coll_obj(reinterpret_cast<HitMesh*>(coll_static_at(coll_statics_, i)));
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
    std::snprintf(pip_name_, sizeof(pip_name_), "levels\\%s\\%s%s.col.%s", name, name, "", platform_suffix());
    auto* data = static_cast<const uint8_t*>(packs::read_in(pip_name_, false, nullptr, nullptr, nullptr));
    if (!data)
        return false;
    add_collision_data(data, flag);
    return true;
}

// 0x0049D780 (the original's four list heads on its stack, and its SEH frame)
void WorldScene::get_update_super_sectors(memory::ListNode* added)
{
    if (!coll_grid_)
        return;
    memory::ListNode to_add, to_remove, moved, to_delete; // (collision objects; sectors)
    setup_list_sentinel(&to_add, g_coll_head_vtable);
    setup_list_sentinel(&to_remove, g_coll_head_vtable);
    setup_list_sentinel(&moved, g_coll_head_vtable);
    setup_list_sentinel(&to_delete, kSectorHeadVtable);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    for_each_sector(this, [&](WorldCell* sector) {
        if (sector->fetch_bits() & kSectorAddToGrid) {
            append_last(&to_add, new_node(g_coll_node_vtable, sector->hit_body_));
            sector->clear_flags(kSectorAddToGrid);
            sector->change_bits(kSectorInGrid);
        }
        if (sector->fetch_bits() & (kSectorDelete | kSectorRemoveFromGrid)) {
            append_last(&to_remove, new_node(g_coll_node_vtable, sector->hit_body_));
            if (sector->fetch_bits() & kSectorDelete)
                append_last(&to_delete, new_node(kSectorNodeVtable, sector));
            else
                sector->clear_flags(kSectorRemoveFromGrid);
        }
        if (sector->fetch_bits() & kSectorMoved) {
            append_last(&moved, new_node(g_coll_node_vtable, sector->hit_body_));
            sector->clear_flags(kSectorMoved);
        }
    });
    if (added && !is_head(added->next) && added->next) {
        for (memory::ListNode* node = added->next; node;) {
            memory::ListNode* next = get_next_node(node);
            auto* sector = static_cast<WorldCell*>(node->data);
            bool listed = false;
            for (memory::ListNode* removing = head_of_scene_list(&to_remove); removing && !listed;
                 removing = get_next_node(removing))
                listed = removing->data == sector->hit_body_;
            if (!listed) {
                append_last(&to_remove, new_node(g_coll_node_vtable, sector->hit_body_));
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
        auto* sector = static_cast<WorldCell*>(node->data);
        uint32_t key = sector->checksum_;
        sectors_->erase_entry(key);
        sector->clear_flags(kSectorInGrid);
        if (sector)
            Virtual<void*>(sector, 0, 1u);
        Virtual<void*>(node, 0, 1u);
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
    std::snprintf(add_pip_name_, sizeof(add_pip_name_), "levels\\%s\\%s%s.col.%s", name, name, "", platform_suffix());
    // (the base's collision file, not the one just named - as the original)
    if (auto* data = static_cast<const uint8_t*>(packs::read_in(pip_name_, false, nullptr, nullptr, nullptr)))
        scene_load_collision_data(this, data, &add_coll_static_count_, &add_coll_statics_, &add_coll_objects_, &box, 0);
    if (add_coll_static_count_ > 0) {
        for (int i = 0; i < add_coll_static_count_; ++i) {
            uint32_t section_id = reinterpret_cast<HitMesh*>(coll_static_at(add_coll_statics_, i))->checksum_;
            auto* sector = static_cast<WorldCell*>(sectors_->fetch_entry(section_id));
            if (!sector) {
                sector = alloc_sector(this);
                sector->checksum_ = section_id;
                sector->change_live(true);
                sector->change_bits(kSectorAddToGrid);
                sector_kept_ = true;
                put_sector(this, sector);
                append_last(add_scene_sectors_, new_node(kSectorNodeVtable, sector));
            }
            sector->set_coll_obj(reinterpret_cast<HitMesh*>(coll_static_at(add_coll_statics_, i)));
        }
        get_update_super_sectors(hidden_sectors_);
    }
    return Virtual<bool>(this, 5, name);
}

// 0x0049DDE0
bool WorldScene::flip_extra_scene()
{
    if (!head_of_scene_list(add_scene_sectors_))
        return false;
    added_ = !added_;
    memory::ListNode* hide = added_ ? hidden_sectors_ : add_scene_sectors_;
    memory::ListNode* show = added_ ? add_scene_sectors_ : hidden_sectors_;
    for (memory::ListNode* node = head_of_scene_list(hide); node;) {
        memory::ListNode* next = get_next_node(node);
        auto* sector = static_cast<WorldCell*>(node->data);
        sector->change_bits(kSectorRemoveFromGrid);
        sector_kept_ = true;
        uint32_t key = sector->checksum_;
        sectors_->erase_entry(key);
        sector->change_live(false);
        node = next;
    }
    for (memory::ListNode* node = head_of_scene_list(show); node;) {
        memory::ListNode* next = get_next_node(node);
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
    if (!Virtual<bool>(this, 6))
        return false;
    if (StaticCollider* statics = add_coll_statics_) {
        auto* array = reinterpret_cast<uint8_t*>(statics) - 4;
        if (*reinterpret_cast<int*>(array))
            Virtual<void*>(statics, 0, 3u); // (the vector deleting destructor)
        else
            memory::dealloc(array);
    }
    if (add_coll_objects_)
        packs::offload(add_pip_name_);
    if (hidden_sectors_)
        delete_nodes(hidden_sectors_);
    if (add_scene_sectors_) {
        for (memory::ListNode* node = head_of_scene_list(add_scene_sectors_); node;) {
            memory::ListNode* next = get_next_node(node);
            delete_scene_sector(this, static_cast<WorldCell*>(node->data));
            Virtual<void*>(node, 0, 1u);
            node = next;
        }
    }
    added_ = false;
    return true;
}

// --- the sector lists' nodes ------------------------------------------------------------------------------------

// 0x0049D300
void SectorListNodes::destroy_body() { reinterpret_cast<memory::ListNode*>(this)->unlink_as(kSectorNodeVtable); }

// 0x0049D320
memory::ListNode* SectorListNodes::destroy_node(unsigned int flags)
{
    auto* node = reinterpret_cast<memory::ListNode*>(this);
    node->unlink_as(kSectorNodeVtable);
    if (flags & 1)
        memory::dealloc(node);
    return node;
}

// 0x0049D360
memory::ListNode* SectorListNodes::destroy_head(unsigned int flags)
{
    auto* node = reinterpret_cast<memory::ListNode*>(this);
    node->unlink_as(kSectorNodeVtable);
    if (flags & 1)
        memory::dealloc(node);
    return node;
}

namespace {
static VtableSlot g_scene_vtable_slots[] = {&render::WorldScene::teardown,
                                            &render::WorldScene::post_load,
                                            &render::WorldScene::post_add,
                                            &render::WorldScene::load_textures,
                                            &render::WorldScene::load_collision,
                                            &render::WorldScene::add_collision,
                                            &render::WorldScene::unload_add_scene,
                                            &render::WorldScene::plat_create_sector,
                                            stubs::ignore_argument,
                                            &render::WorldScene::get_default_colour,
                                            &render::WorldScene::set_rendered};
void** const kSceneVtable = reinterpret_cast<void**>(g_scene_vtable_slots);
static VtableSlot g_sector_node_vtable_slots[] = {&render::SectorListNodes::destroy_node};
void** const kSectorNodeVtable = reinterpret_cast<void**>(g_sector_node_vtable_slots);
static VtableSlot g_sector_head_vtable_slots[] = {&render::SectorListNodes::destroy_head};
void** const kSectorHeadVtable = reinterpret_cast<void**>(g_sector_head_vtable_slots);
} // namespace

} // namespace render

namespace vt {
static VtableSlot g_coll_grid_node_slots[] = {&render::CollGridListNodes::destroy_node};
void** const kCollGridNode = reinterpret_cast<void**>(g_coll_grid_node_slots);
static VtableSlot g_coll_grid_head_slots[] = {&render::CollGridListNodes::destroy_head};
void** const kCollGridHead = reinterpret_cast<void**>(g_coll_grid_head_slots);
} // namespace vt

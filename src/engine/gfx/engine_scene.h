#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/maths/shape_tests.h"

namespace lists {
class KeyMap;
}
namespace script {
class Struct;
}

// The engine's scene (Nx; the platform-independent base of the PC scene in pc_scene.h, its first 0x280 bytes): its
// sectors in a hash table, their collision (the static collision objects and a grid over them), its names, and a scene
// added over it (its own collision file: new sectors, and the base's it hides, swapped in and out). The members of
// 0x0049C910-0x0049D18E are in engine_scene.cpp, those of 0x0049D1A0-0x0049E2EF (the constructor and destructor,
// the super sectors' update, clones, the added scene) in engine_scene_sectors.cpp.
namespace render {

class CollGrid;
class StaticCollider;
class WorldCell;
class TextureSet;

// (vtable 0x006499E0: 0 the destructor, 1 PostLoad, 2 PostAdd, 3 LoadTextures, 4 LoadCollision, 5 AddCollision,
// 6 UnloadAddScene, 7 spawn_sector (the platform's), 8 -, 9 get_default_colour, 10 set_rendered.)
struct WorldScene {
    void** vtable_;
    char label_[0x80];            // +0x04: the file it was loaded from
    uint32_t checksum_;          // +0x84: its name's
    TextureSet* texture_set_;         // +0x88
    bool builds_collision_grid_;          // +0x8C: its collision gets a grid (super sectors)
    bool is_sky_;                 // +0x8D: a sky (the scene list tells the sky from the main scene by it)
    uint8_t pad8e_[2];
    maths::BBox unknown_box_;       // +0x90 (made empty)
    maths::BBox collision_box_;     // +0xB0
    lists::KeyMap* sectors_;   // +0xD0
    CollGrid* coll_grid_;       // +0xD4
    char pip_name_[0x80];         // +0xD8: the collision file's (unloaded with the collision)
    int coll_static_count_;        // +0x158
    StaticCollider* coll_statics_;  // +0x15C (an array new)
    uint8_t* collision_blob_;      // +0x160: the collision geometry (load_collision_data's)
    char add_name_[0x80];         // +0x164: the scene added to it
    uint32_t add_data_;           // +0x1E4: the added scene's texture dictionary (unloaded with the scene)
    char add_pip_name_[0x80];      // +0x1E8: the added scene's collision file
    int add_coll_static_count_;     // +0x268: the added scene's collision, as the base's
    StaticCollider* add_coll_statics_; // +0x26C
    uint8_t* add_coll_objects_;   // +0x270
    bool sector_kept_;            // +0x274: a sector couldn't be deleted (flag 0x40): marked instead
    bool added_;                 // +0x275: the added scene shown
    uint8_t pad276[2];
    memory::ListNode* hidden_sectors_;   // +0x278: lists::Foremost<WorldCell> - the base's sectors the added scene hides
    memory::ListNode* add_scene_sectors_; // +0x27C: lists::Foremost<WorldCell> - the sectors the added scene made

    // 0x0049C910: the name kept, then PostLoad (vtable slot 1) with `arena`.
    void finish_load(const char* name, void* arena);
    // 0x0049C940: an added scene's name and data kept, marked added, then PostAdd (vtable slot 2).
    void finish_add(const char* name, uint32_t data);
    // 0x0049C9A0: the static collision objects listed (each "%4d/%d " - the object's own print compiled out).
    void print_coll_statics();
    // 0x0049C9F0: static collision object `index`.
    StaticCollider* fetch_static_collision(int index);
    // 0x0049CA10: a new sector (vtable slot 7) with +0x14 / +0x18 cleared (no null check, as the original).
    void* spawn_sector();
    // 0x0049CB00: the sector named `name` (the hash table's lookup, inlined), or null.
    void* look_up_sector(uint32_t name);
    // 0x0049CB40: `sector` deleted (out of the table if it's the one there under its name) - true; a sector with
    // flag 0x40 is only given flag 8 and the scene marked (+0x274) - false.
    bool remove_scene_sector(void* sector);
    // 0x0049CF80: the sector named `name` deleted (false if there's none).
    bool delete_sector_named(uint32_t name);
    // 0x0049CBB0: the collision from a scene file's collision data in memory: `*count` objects from the header,
    // their geometry (`*objects`, 0x40 bytes each) relocated to the data after it, a static collision object each
    // (`*statics`, an array new) given its geometry and face-rejection buffer; `box` grown by every geometry with
    // vertices - (-100, -100, -100) - (100, 100, 100) when none has. True.
    bool load_collision_data(const uint8_t* data, int* count, StaticCollider** statics, uint8_t** objects, maths::BBox* box,
                           int unused);
    // 0x0049CE60: with collision, `box` kept and the collision grid made over it. Whether there's collision.
    bool init_collision_grid(const maths::BBox* box);
    // 0x0049CFB0: the grid, the static collision objects and the collision file let go of. True.
    bool unload_collision();
    // 0x0049D030: with a grid: every sector with flag 8 deleted (flag 0x40 cleared first) as far as the first gap in
    // the table, the grid cleared, the table emptied.
    void delete_marked_sectors();
    // 0x0049D0B0: every sector whose box touches `box` (two points) given 0x0049E9B0(`on`) - stopping at the first
    // gap in the table.
    void set_sectors_in_box(const maths::Vec4* box, uint32_t on, int unused);

    // The virtuals' base bodies (STUB messages; false / null).
    void post_load(void* arena);          // 0x0049CA50
    void set_rendered(int rendered);      // 0x0049CA60
    void post_add();                      // 0x0049CA70
    bool load_textures(const char* name); // 0x0049CA80
    bool load_collision(const char* name);  // 0x0049CAA0
    bool add_collision(const char* name); // 0x0049CAC0
    bool unload_add_scene();               // 0x0049CAE0
    void* plat_create_sector();            // 0x0049CAF0 (not a Ghidra function)
    // 0x0049CF60 (slot 9): grey (0x80 each of RGBA) into `out`. Returns out.
    uint8_t* get_default_colour(uint8_t* out);

    // --- engine_scene_sectors.cpp ---
    // 0x0049D390: construct - empty boxes, no grid, a sector table of 2^`bits` buckets (none without memory), no
    // names, no collision, empty lists of hidden and added sectors (none without memory). Returns this.
    WorldScene* make_new(int bits);
    // 0x0049DD10: the destructor's body - every sector deleted (out of the grid first), the table emptied and freed,
    // the collision let go of (0x0049CFB0), the two lists' heads deleted (their nodes left), the added scene's
    // texture dictionary unloaded.
    void destroy_body();
    // 0x0049E1D0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    WorldScene* teardown(unsigned int free_flags);
    // 0x0049D1A0: its statistics into `info` - name, sectors, collision objects, and its geometry's counts.
    void fetch_diagnostics(script::Struct* info);
    // 0x0049D4E0: `sector` put in the table under its name.
    void insert_sector(WorldCell* sector);
    // 0x0049D510: a clone of `sector` (Clone's `instance`, `flag`, the scene) named `name` (when not 0) in `scene`
    // (this one if null) - kept there; with `flag`, that scene's super sectors marked to be brought up to date. The
    // clone's name (0 if none was made).
    uint32_t duplicate_scene_sector(WorldCell* sector, WorldScene* scene, int instance, bool flag, uint32_t name);
    // 0x0049DF30: the same for the sector `section_id` of this scene (0 if there's none).
    uint32_t clone_sector_named(uint32_t section_id, WorldScene* scene, int instance, bool flag, uint32_t name);
    // 0x0049D590: the collision from a scene file's collision data - the static objects (and with builds_collision_grid_
    // their grid), each given to its sector (a new one, shown, if there's none of its name) and the sector put in the
    // grid (0x40); the weather told (0x004A1090) when the script integer 0xBA1E4254 is set. True.
    bool add_collision_data(const uint8_t* data, int flag);
    // 0x0049DF70: the collision file "levels\<name>\<name>.col.<ext>" (its name kept) loaded and added. Whether it
    // was there.
    bool load_collision_file(const char* name, int flag);
    // 0x0049D780: the super sectors (the collision grid) brought up to date: the sectors flagged 4 put in (0x40),
    // those flagged 8 or 0x10 taken out - 8: deleted, out of the table, after -, those flagged 0x20 moved; `added`
    // (a list of sectors) taken out too unless they already are. Nothing without a grid.
    void get_update_super_sectors(memory::ListNode* added);
    // 0x0049E1B0: UpdateSuperSectors(`added`) if a sector was marked (sector_kept_). Whether it was.
    bool update_super_sectors_if_marked(memory::ListNode* added);
    // 0x0049DFD0: a scene added over this one: (with collision) the base's collision file loaded again (as the
    // original - the added scene's name is only kept), its objects given to their sectors - new ones, shown, flagged
    // 4 and listed as the added scene's -, the super sectors brought up to date; then the platform's slot 5 with the
    // name. Its result (false without collision).
    bool load_add_scene(const char* name);
    // 0x0049DDE0: the added scene shown or hidden in turn: its sectors put in the table (4) and shown, the base's it
    // hides taken out (0x10) and hidden - or the other way round; the super sectors brought up to date. Whether it's
    // shown now (false, and nothing done, while the added scene has no sectors).
    bool flip_extra_scene();
    // 0x0049E200: the added scene let go of - hidden first, the platform's slot 6 (false: nothing more), its static
    // collision objects deleted (the array), its collision file unloaded, the hidden list emptied, its own sectors
    // deleted (0x0049CB40). True.
    bool unload_added_scene();
};
static_assert(offsetof(WorldScene, builds_collision_grid_) == 0x8C && offsetof(WorldScene, collision_box_) == 0xB0 &&
                  offsetof(WorldScene, sectors_) == 0xD0 && offsetof(WorldScene, coll_grid_) == 0xD4 &&
                  offsetof(WorldScene, pip_name_) == 0xD8 && offsetof(WorldScene, coll_static_count_) == 0x158 &&
                  offsetof(WorldScene, coll_statics_) == 0x15C && offsetof(WorldScene, add_name_) == 0x164 &&
                  offsetof(WorldScene, add_data_) == 0x1E4 && offsetof(WorldScene, sector_kept_) == 0x274 &&
                  offsetof(WorldScene, added_) == 0x275 && offsetof(WorldScene, unknown_box_) == 0x90 &&
                  offsetof(WorldScene, add_coll_statics_) == 0x26C && offsetof(WorldScene, add_scene_sectors_) == 0x27C &&
                  sizeof(WorldScene) == 0x280,
              "layout must match the original");

// 0x0049CA20 (cdecl): a level's collision file name - "levels\<level>\<level>.col.<the platform's extension>" -
// into `out`. The length.
int __cdecl make_collision_file_name(char* out, const char* level);

// The nodes of the scene's lists of sectors (lists::Elem<WorldCell>, vtable 0x006499D8; the heads' 0x006499DC).
struct SectorListNodes {
    // 0x0049D300: the destructor's body (unlinked).
    void destroy_body();
    // 0x0049D320 / 0x0049D360 (virtual [0] of the node / the head): destroyed, and freed with flags & 1. Return this.
    memory::ListNode* destroy_node(unsigned int flags);
    memory::ListNode* destroy_head(unsigned int flags);
};

} // namespace Nx

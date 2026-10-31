#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

// The engine's sector (render::WorldCell; the PC sector PcSector - pc_scene.h - extends it): a piece of a scene with a
// name, its geometry (a CGeom - drawn) and its collision (a HitMesh - collided with), moved, turned, scaled, shown,
// hidden and coloured through both.
namespace render {

class HitMesh;

// WorldCell::flag_bits_ (4, 8, 0x10 and 0x20 are acted on by the scene's next UpdateSuperSectors)
enum : uint32_t {
    kSectorCollidable = 0x01,     // (set when made)
    kSectorClone = 0x02,          // a clone: its collision object is its own (deleted with it)
    kSectorAddToGrid = 0x04,      // its collision to be put in the super sectors (a clone made with Clone's flag)
    kSectorDelete = 0x08,         // to be taken out of them and deleted
    kSectorRemoveFromGrid = 0x10, // to be taken out of them
    kSectorMoved = 0x20,          // moved, turned or scaled while kSectorInGrid: to be moved in them
    kSectorInGrid = 0x40,         // its collision is in the scene's super sectors
};

// The sector (0x1C bytes of the engine's; the original's vtable 0x00649A40 - PC 0x0064A718). Its virtual functions:
// the destructor, plat_set_shatter, plat_clone (the platform's new sector); slot 2, PlatGetShatter, the same in both
// tables and never called, is a plain method.
class WorldCell : public memory::HeapObject {
public:
    // 0x0049E900: collidable, no geometry or collision, unturned, the default terrain 0xE3714DC1.
    WorldCell();
    WorldCell(const WorldCell&) = delete;
    WorldCell& operator=(const WorldCell&) = delete;
    // 0x0049E630 (0x0049E930 the deleting destructor): its collision object deleted if it's a clone's, its geometry
    // deleted.
    virtual ~WorldCell();
    // 0x0049E8C0 / 0x0049E8E0 (the engine's stubs: "STUB: Plat...").
    virtual void plat_set_shatter(bool on);
    virtual WorldCell* plat_clone(int instance, void* scene);
    // 0x0049E8D0 ("STUB: PlatGetShatter").
    bool plat_get_shatter();

    // 0x0049EAD0: a clone - the platform's new sector with a new name (0x0048CA80), these flags and kSectorClone,
    // the turn, a clone of the geometry (for `scene`) and of the collision (named as the clone, out of the grid,
    // kSectorAddToGrid with `flag`; the geometry given its faces). Null if the platform made none.
    WorldCell* replicate(int instance, bool flag, void* scene);

    // 0x0049E660: the geometry.
    void choose_geom(void* geom);
    // 0x0049EAA0: the collision object, if it has none yet (the geometry given its faces). Whether it was set.
    bool set_coll_obj(HitMesh* coll);
    // 0x0049E810 (4 bytes; folded with other classes' getters of the word at +8): the flags.
    uint32_t fetch_bits();
    // 0x0049E890 / 0x0049E8A0: flags set / cleared.
    void change_bits(uint32_t flags);
    void clear_flags(uint32_t flags);

    // 0x0049E670: the geometry's colour taken off.
    void wipe_tint();
    // 0x0049E980: the geometry's colour (RGBA) into `out` - 0 without geometry. Returns out.
    uint8_t* fetch_tint(uint8_t* out);
    // 0x0049E680 / 0x0049E690: the viewports it shows in (a mask; 0 without geometry).
    void toggle_shown(uint32_t mask);
    uint32_t fetch_shown();
    // 0x0049E9B0 / 0x0049E9E0: drawn and collided with (the collision's flag 4 cleared), or neither; whether it is
    // (the geometry's, else the collision's flag).
    void change_live(bool active);
    bool is_operative();
    // 0x0049EA30: collided with or not (kSectorCollidable; the collision's flag 2 cleared / set).
    void enable_collision(bool collidable);
    // 0x0049EA60: an occluder - the collision's flag 0x80 set (and its occlusion polygons added, 0x0040AEB0) the
    // first time, cleared with false. Nothing without collision.
    void mark_as_occluder(bool occluder);

    // 0x0049E6A0: its box (the geometry's, else the collision's - slot 21).
    const maths::BBox* fetch_bounds();
    // 0x0049E740: its orientation (the collision's - slot 8 - either way).
    const maths::Mat4* fetch_facing();
    // 0x0049E6C0 / 0x0049E700: where it is / its orientation (collision and geometry; kSectorMoved when in the grid).
    void place_at_absolute_point(const maths::Vec4* position);
    void change_facing(const maths::Mat4* orientation);
    // 0x0049E760: turned to `turns` quarter turns about y (the collision about the geometry's position, else its
    // own) - by the difference from the last.
    void spin_around_up(int turns);
    // 0x0049E7C0: its scale (the collision about the geometry's position, else its own; kSectorMoved when in the
    // grid).
    void change_scale(const maths::Vec4* scale);
    // 0x0049EA00: the collision geometry's vertices set (kSectorMoved when in the grid). Nothing without collision
    // or its geometry.
    void choose_raw_verts(const maths::Vec4* vertices);

    // The geometry's vertex colour and UV wibbles and texture animation (nothing without geometry): 0x0049E820 the
    // vertex colour wibble played from a keyframe (time in ms), 0x0049E830 the UV wibble's parameters, 0x0049E840
    // the explicit UV wibble on / off, 0x0049E850 its offsets, 0x0049E860 / 0x0049E870 / 0x0049E880 the texture
    // animation reset, started, stopped.
    void play_vc_wibble_from_keyframe(int sequence, int keyframe, int time_ms);
    void set_uv_wibble_params(float u_vel, float u_amp, float u_freq, float u_phase, float v_vel, float v_amp,
                              float v_freq, float v_phase);
    void force_uv_wobble(bool on);
    void set_uv_wibble_offsets(float u, float v);
    void reset_texture_anim();
    void start_texture_anim(bool reset);
    void stop_texture_anim();

    uint32_t checksum_;    // +0x04: its name
    uint32_t flag_bits_;   // +0x08
    uint32_t light_group_; // +0x0C: its light group (the time of day's; outdoor by default)
    int rot_y_;            // +0x10: quarter turns about y
    void* geom_handle_;    // +0x14: CGeom (on PC a PcGeom), or null
    HitMesh* hit_body_;    // +0x18: or null
};
static_assert(offsetof(WorldCell, rot_y_) == 0x10 && offsetof(WorldCell, hit_body_) == 0x18 &&
                  sizeof(WorldCell) == 0x1C,
              "layout must match the original");

} // namespace render

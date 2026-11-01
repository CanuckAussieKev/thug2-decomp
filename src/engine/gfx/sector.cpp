// The engine's sector (see sector.h): what it does through its geometry and its collision object.

#include <cstdint>

#include "engine/gfx/sector.h"

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/render/coll_obj.h"
#include "engine/core/owner.h"

namespace render {

namespace {

// The geometry (the original's calls go through jumps to its virtual slots).
Geom* geom_of(void* geom) { return static_cast<Geom*>(geom); }
const maths::Vec4* geom_world_position(void* geom) { return geom_of(geom)->get_absolute_point(); }
void set_geom_coll_tri_data(void* geom, TriMeshSource* faces) { static_cast<Geom*>(geom)->choose_tri_source(faces); }

// The collision object's flag byte (+8): 2 not collided with, 4 inactive, 0x80 an occluder.
uint8_t& coll_flags(HitMesh* coll) { return *reinterpret_cast<uint8_t*>(&coll->flag_bits_); }


} // namespace

// 0x0049E900
WorldCell::WorldCell() = default;

// 0x0049E630
WorldCell::~WorldCell()
{
    if (hit_body_ && (flag_bits_ & kSectorClone))
        delete hit_body_;
    delete geom_of(geom_handle_);
}

// 0x0049E8C0
void WorldCell::plat_set_shatter(bool) { debug::printf("STUB: PlatSetShatter\n"); } // "STUB: PlatSetShatter\n"

// 0x0049E8D0
bool WorldCell::plat_get_shatter()
{
    debug::printf("STUB: PlatGetShatter\n"); // "STUB: PlatGetShatter\n"
    return false;
}

// 0x0049E8E0 (not a function in Ghidra: only the vtable reaches it)
memory::owner<WorldCell*> WorldCell::plat_clone(int, void*)
{
    debug::printf("STUB: PlatClone\n"); // "STUB: PlatClone\n"
    return nullptr;
}

// 0x0049EAD0
WorldCell* WorldCell::replicate(int instance, bool flag, void* scene)
{
    WorldCell* clone = plat_clone(instance, scene);
    if (!clone)
        return nullptr;
    clone->checksum_ = new_sector_name();
    clone->flag_bits_ = flag_bits_ | kSectorClone;
    clone->rot_y_ = rot_y_;
    if (geom_handle_)
        clone->geom_handle_ = static_cast<Geom*>(geom_handle_)->replicate(instance, scene);
    if (!hit_body_)
        return clone;
    HitMesh* coll = hit_body_->replicate(instance);
    if (coll) {
        clone->hit_body_ = coll;
        coll->set_checksum(clone->checksum_);
        clone->flag_bits_ &= ~kSectorInGrid;
        if (flag)
            clone->flag_bits_ |= kSectorAddToGrid;
        if (clone->geom_handle_)
            set_geom_coll_tri_data(clone->geom_handle_, coll->geometry_);
    } else {
        clone->flag_bits_ &= ~kSectorInGrid;
        clone->hit_body_ = nullptr;
        if (clone->geom_handle_)
            set_geom_coll_tri_data(clone->geom_handle_, nullptr);
    }
    return clone;
}

// 0x0049E660
void WorldCell::choose_geom(void* geom) { geom_handle_ = geom; }

// 0x0049EAA0
bool WorldCell::set_coll_obj(HitMesh* coll)
{
    if (hit_body_)
        return false;
    hit_body_ = coll;
    if (geom_handle_)
        set_geom_coll_tri_data(geom_handle_, coll->geometry_);
    return true;
}

// 0x0049E810
uint32_t WorldCell::fetch_bits() { return flag_bits_; }

// 0x0049E890
void WorldCell::change_bits(uint32_t flags) { flag_bits_ |= flags; }

// 0x0049E8A0
void WorldCell::clear_flags(uint32_t flags) { flag_bits_ &= ~flags; }

// 0x0049E670
void WorldCell::wipe_tint()
{
    if (geom_handle_)
        geom_of(geom_handle_)->wipe_tint();
}

// 0x0049E980
uint8_t* WorldCell::fetch_tint(uint8_t* out)
{
    if (geom_handle_) {
        static_cast<Geom*>(geom_handle_)->fetch_tint(reinterpret_cast<uint32_t*>(out));
        return out;
    }
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    return out;
}

// 0x0049E680
void WorldCell::toggle_shown(uint32_t mask)
{
    if (geom_handle_)
        geom_of(geom_handle_)->toggle_shown(mask);
}

// 0x0049E690
uint32_t WorldCell::fetch_shown() { return geom_handle_ ? geom_of(geom_handle_)->fetch_shown() : 0; }

// 0x0049E9B0
void WorldCell::change_live(bool active)
{
    if (hit_body_) {
        if (active)
            coll_flags(hit_body_) &= ~4;
        else
            coll_flags(hit_body_) |= 4;
    }
    if (geom_handle_)
        geom_of(geom_handle_)->change_live(active);
}

// 0x0049E9E0 (no geometry and no collision: the original reads through null)
bool WorldCell::is_operative()
{
    if (geom_handle_)
        return geom_of(geom_handle_)->is_operative();
    return !(coll_flags(hit_body_) & 4);
}

// 0x0049EA30
void WorldCell::enable_collision(bool collidable)
{
    if (collidable)
        flag_bits_ |= kSectorCollidable;
    else
        flag_bits_ &= ~kSectorCollidable;
    if (hit_body_) {
        if (collidable)
            coll_flags(hit_body_) &= ~2;
        else
            coll_flags(hit_body_) |= 2;
    }
}

// 0x0049EA60
void WorldCell::mark_as_occluder(bool occluder)
{
    if (!hit_body_)
        return;
    if (!occluder) {
        coll_flags(hit_body_) &= 0x7F;
        return;
    }
    if (!(coll_flags(hit_body_) & 0x80)) {
        hit_body_->flag_bits_ |= 0x80;
        hit_body_->add_occlusion_polys(); // its occlusion polygons added
    }
}

// 0x0049E6A0
const maths::BBox* WorldCell::fetch_bounds()
{
    if (geom_handle_)
        return reinterpret_cast<const maths::BBox*>(geom_of(geom_handle_)->fetch_bounds());
    return hit_body_->get_world_bounds();
}

// 0x0049E740 (both of the original's branches call the collision's: there must be one)
const maths::Mat4* WorldCell::fetch_facing() { return hit_body_->fetch_facing(); }

// 0x0049E6C0
void WorldCell::place_at_absolute_point(const maths::Vec4* position)
{
    if (hit_body_) {
        hit_body_->place_at_absolute_point(position);
        if (flag_bits_ & kSectorInGrid)
            flag_bits_ |= kSectorMoved;
    }
    if (geom_handle_)
        geom_of(geom_handle_)->place_at_absolute_point(position);
}

// 0x0049E700
void WorldCell::change_facing(const maths::Mat4* orientation)
{
    if (hit_body_) {
        hit_body_->change_facing(orientation);
        if (flag_bits_ & kSectorInGrid)
            flag_bits_ |= kSectorMoved;
    }
    if (geom_handle_)
        geom_of(geom_handle_)->change_facing(orientation);
}

// 0x0049E760 (the difference's remainder as C's: (turns - rot_y_ + 4) % 4 may be negative)
void WorldCell::spin_around_up(int turns)
{
    int delta = (turns - rot_y_ + 4) % 4;
    if (hit_body_) {
        const maths::Vec4* about = geom_handle_ ? geom_world_position(geom_handle_) : hit_body_->get_absolute_point();
        hit_body_->rotate_y(about, delta);
    }
    if (geom_handle_)
        geom_of(geom_handle_)->rotate_y(delta);
    rot_y_ = turns;
}

// 0x0049E7C0
void WorldCell::change_scale(const maths::Vec4* scale)
{
    if (hit_body_) {
        const maths::Vec4* about = geom_handle_ ? geom_world_position(geom_handle_) : hit_body_->get_absolute_point();
        hit_body_->scale_about(about, scale);
        if (flag_bits_ & kSectorInGrid)
            flag_bits_ |= kSectorMoved;
    }
    if (geom_handle_)
        geom_of(geom_handle_)->change_scale(scale);
}

// 0x0049EA00
void WorldCell::choose_raw_verts(const maths::Vec4* vertices)
{
    if (!hit_body_ || !hit_body_->geometry_)
        return;
    // TriMeshSource: every vertex set (0x00415C10)
    hit_body_->geometry_->choose_raw_verts(vertices);
    if (flag_bits_ & kSectorInGrid)
        flag_bits_ |= kSectorMoved;
}

// 0x0049E820 (the geom's call through the dispatcher 0x0049F420)
void WorldCell::play_vc_wibble_from_keyframe(int sequence, int keyframe, int time_ms)
{
    if (geom_handle_)
        geom_of(geom_handle_)->play_vc_wibble_from_keyframe(sequence, keyframe, time_ms);
}

// 0x0049E830
void WorldCell::set_uv_wibble_params(float u_vel, float u_amp, float u_freq, float u_phase, float v_vel, float v_amp,
                                     float v_freq, float v_phase)
{
    if (geom_handle_)
        geom_of(geom_handle_)->set_uv_wibble_params(u_vel, u_amp, u_freq, u_phase, v_vel, v_amp, v_freq, v_phase);
}

// 0x0049E840
void WorldCell::force_uv_wobble(bool on)
{
    if (geom_handle_)
        geom_of(geom_handle_)->enable_explicit_uv_wibble(on);
}

// 0x0049E850
void WorldCell::set_uv_wibble_offsets(float u, float v)
{
    if (geom_handle_)
        geom_of(geom_handle_)->set_uv_wibble_offsets(u, v);
}

// 0x0049E860
void WorldCell::reset_texture_anim()
{
    if (geom_handle_)
        geom_of(geom_handle_)->reset_texture_anims();
}

// 0x0049E870
void WorldCell::start_texture_anim(bool reset)
{
    if (geom_handle_)
        geom_of(geom_handle_)->start_texture_anims(reset);
}

// 0x0049E880
void WorldCell::stop_texture_anim()
{
    if (geom_handle_)
        geom_of(geom_handle_)->stop_texture_anims();
}

} // namespace render

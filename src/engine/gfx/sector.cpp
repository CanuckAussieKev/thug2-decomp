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
#include "engine/core/code_address.h"

namespace render {

namespace {

// The engine's CGeom calls (each a jump through the geom's vtable; not rewritten yet).
template <typename R, typename... A>
R geom_call(uint32_t slot, void* geom, A... args)
{
    // (the geom's virtual slot `slot`: the original goes through a jump stub for each)
    return reinterpret_cast<R(__thiscall*)(void*, A...)>((*static_cast<void***>(geom))[slot])(geom, args...);
}
const maths::Vec4* geom_world_position(void* geom) { return geom_call<const maths::Vec4*>(19, geom); }
void set_geom_coll_tri_data(void* geom, TriMeshSource* faces) { static_cast<Geom*>(geom)->choose_tri_source(faces); }

// The collision object's flag byte (+8): 2 not collided with, 4 inactive, 0x80 an occluder.
uint8_t& coll_flags(HitMesh* coll) { return *reinterpret_cast<uint8_t*>(&coll->flag_bits_); }

constexpr uint32_t kDefaultTerrain = 0xE3714DC1;
extern void** const kSectorVtable;

} // namespace

// 0x0049E900
WorldCell* WorldCell::make_new()
{
    vtable = reinterpret_cast<void**>(kSectorVtable);
    flag_bits_ = kSectorCollidable;
    geom_handle_ = nullptr;
    hit_body_ = nullptr;
    rot_y_ = 0;
    surface_type_ = kDefaultTerrain;
    return this;
}

// 0x0049E630
void WorldCell::destroy_body()
{
    vtable = reinterpret_cast<void**>(kSectorVtable);
    if (hit_body_ && (flag_bits_ & kSectorClone))
        hit_body_->get_call<void*>(0, 1u);
    if (geom_handle_)
        (*reinterpret_cast<void* (__thiscall***)(void*, unsigned int)>(geom_handle_))[0](geom_handle_, 1);
}

// 0x0049E930
WorldCell* WorldCell::teardown(unsigned int free_flags)
{
    destroy_body();
    if (free_flags & 1)
        memory::dealloc(this);
    return this;
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
WorldCell* WorldCell::plat_clone(int, void*)
{
    debug::printf("STUB: PlatClone\n"); // "STUB: PlatClone\n"
    return nullptr;
}

// 0x0049EAD0
WorldCell* WorldCell::replicate(int instance, bool flag, void* scene)
{
    auto* clone = reinterpret_cast<WorldCell* (__thiscall*)(WorldCell*, int, void*)>(vtable[3])(this, instance, scene);
    if (!clone)
        return nullptr;
    clone->checksum_ = new_sector_name();
    clone->flag_bits_ = flag_bits_ | kSectorClone;
    clone->rot_y_ = rot_y_;
    if (geom_handle_)
        clone->geom_handle_ = static_cast<Geom*>(geom_handle_)->replicate(instance, scene);
    if (!hit_body_)
        return clone;
    auto* coll = hit_body_->get_call<HitMesh*>(12, instance);
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
        geom_call<void>(8, geom_handle_);
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
        geom_call<void>(11, geom_handle_, mask);
}

// 0x0049E690
uint32_t WorldCell::fetch_shown() { return geom_handle_ ? geom_call<uint32_t>(12, geom_handle_) : 0; }

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
        geom_call<void>(13, geom_handle_, active);
}

// 0x0049E9E0 (no geometry and no collision: the original reads through null)
bool WorldCell::is_operative()
{
    if (geom_handle_)
        return geom_call<bool>(14, geom_handle_);
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
        static_cast<CollGeometryForwarders*>(hit_body_)->add_occlusion_polys(); // its occlusion polygons added
    }
}

// 0x0049E6A0
const maths::BBox* WorldCell::fetch_bounds()
{
    if (geom_handle_)
        return geom_call<const maths::BBox*>(15, geom_handle_);
    return hit_body_->get_call<const maths::BBox*>(21);
}

// 0x0049E740 (both of the original's branches call the collision's: there must be one)
const maths::Mat4* WorldCell::fetch_facing() { return hit_body_->get_call<const maths::Mat4*>(8); }

// 0x0049E6C0
void WorldCell::place_at_absolute_point(const maths::Vec4* position)
{
    if (hit_body_) {
        hit_body_->get_call<void>(5, position);
        if (flag_bits_ & kSectorInGrid)
            flag_bits_ |= kSectorMoved;
    }
    if (geom_handle_)
        geom_call<void>(18, geom_handle_, position);
}

// 0x0049E700
void WorldCell::change_facing(const maths::Mat4* orientation)
{
    if (hit_body_) {
        hit_body_->get_call<void>(6, orientation);
        if (flag_bits_ & kSectorInGrid)
            flag_bits_ |= kSectorMoved;
    }
    if (geom_handle_)
        geom_call<void>(20, geom_handle_, orientation);
}

// 0x0049E760 (the difference's remainder as C's: (turns - rot_y_ + 4) % 4 may be negative)
void WorldCell::spin_around_up(int turns)
{
    int delta = (turns - rot_y_ + 4) % 4;
    if (hit_body_) {
        const maths::Vec4* about = geom_handle_ ? geom_world_position(geom_handle_) : hit_body_->get_call<const maths::Vec4*>(7);
        hit_body_->get_call<void>(22, about, delta);
    }
    if (geom_handle_)
        geom_call<void>(22, geom_handle_, delta);
    rot_y_ = turns;
}

// 0x0049E7C0
void WorldCell::change_scale(const maths::Vec4* scale)
{
    if (hit_body_) {
        const maths::Vec4* about = geom_handle_ ? geom_world_position(geom_handle_) : hit_body_->get_call<const maths::Vec4*>(7);
        hit_body_->get_call<void>(23, about, scale);
        if (flag_bits_ & kSectorInGrid)
            flag_bits_ |= kSectorMoved;
    }
    if (geom_handle_)
        geom_call<void>(25, geom_handle_, scale);
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

// 0x0049E820 (through the geom's slot 41, the dispatcher 0x0049F420)
void WorldCell::play_vc_wibble_from_keyframe(int sequence, int keyframe, int time_ms)
{
    if (geom_handle_)
        reinterpret_cast<void(__thiscall*)(void*, int, int, int)>(&virtual_slot41_jump)(geom_handle_, sequence, keyframe,
                                                                                    time_ms);
}

// 0x0049E830
void WorldCell::set_uv_wibble_params(float u_vel, float u_amp, float u_freq, float u_phase, float v_vel, float v_amp, float v_freq,
                                float v_phase)
{
    if (geom_handle_)
        geom_call<void>(42, geom_handle_, u_vel, u_amp, u_freq, u_phase, v_vel, v_amp, v_freq, v_phase);
}

// 0x0049E840
void WorldCell::force_uv_wobble(bool on)
{
    if (geom_handle_)
        geom_call<void>(43, geom_handle_, on);
}

// 0x0049E850
void WorldCell::set_uv_wibble_offsets(float u, float v)
{
    if (geom_handle_)
        geom_call<void>(45, geom_handle_, u, v);
}

// 0x0049E860
void WorldCell::reset_texture_anim()
{
    if (geom_handle_)
        geom_call<void>(48, geom_handle_);
}

// 0x0049E870
void WorldCell::start_texture_anim(bool reset)
{
    if (geom_handle_)
        geom_call<void>(49, geom_handle_, reset);
}

// 0x0049E880
void WorldCell::stop_texture_anim()
{
    if (geom_handle_)
        geom_call<void>(50, geom_handle_);
}

namespace {
static VtableSlot g_sector_vtable_slots[] = {&render::WorldCell::teardown, &render::WorldCell::plat_set_shatter,
                &render::WorldCell::plat_get_shatter, &render::WorldCell::plat_clone};
void** const kSectorVtable = reinterpret_cast<void**>(g_sector_vtable_slots);
} // namespace

} // namespace Nx

#pragma once

// What the model's source (model.cpp, model_geoms.cpp) reaches of the engine not rewritten yet - the engine's CGeom
// calls (each a jump through the geom's vtable: the slots are PcGeom's, geom.h) and the scene / object calls - and
// the list walk. Internal to those files.

#include <cstdint>

#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/model.h"
#include "engine/gfx/pc_lights.h"
#include "engine/gfx/rig.h"
#include "engine/gfx/texture_dict.h"
#include "engine/net/buddy_status.h"

namespace render::model_geoms {

template <typename R, typename... A>
R geom_call(uint32_t slot, void* geom, A... args)
{
    // (the geom's virtual slot `slot`: the original goes through a jump stub for each)
    return reinterpret_cast<R(__thiscall*)(void*, A...)>((*static_cast<void***>(geom))[slot])(geom, args...);
}

// The geom's slots: [3] made an instance of a scene for a model (0x0048DDF0 first sets its +4 to 1 and its +0x10 -
// multicolour - to the flag), [8] colour cleared, [9] a material pass coloured, [11] / [12] the viewport mask, [13]
// active, [27] the lights, [28] the bone matrices, [29] bones copied, [30] the bones pointed at, [31] strips broken,
// [32] the shadow, [44] - [47] UV offsets / matrix / matrix parameters.
inline void load_geom(void* geom, uint8_t* scene, RenderModel* model, bool multicolor)
{
    static_cast<Geom*>(geom)->init_instance(scene, model, multicolor);
}
inline void clear_geom_color(void* geom) { geom_call<void>(8, geom); }
inline bool set_geom_material_color(void* geom, uint32_t material, int pass, uint32_t rgba)
{
    return geom_call<bool>(9, geom, material, pass, rgba);
}
inline void set_geom_visibility(void* geom, uint32_t mask) { geom_call<void>(11, geom, mask); }
inline uint32_t get_geom_visibility(void* geom) { return geom_call<uint32_t>(12, geom); }
inline void set_geom_active(void* geom, uint8_t active) { geom_call<void>(13, geom, active); }
inline void set_geom_lights(void* geom, uint8_t* lights) { geom_call<void>(27, geom, lights); }
inline void set_geom_bone_matrices(void* geom, const maths::Mat4* transform, uint8_t* bones, int count, int flag,
                                int index)
{
    geom_call<void>(28, geom, transform, bones, count, flag, index);
}
inline void copy_geom_bones(void* geom, uint8_t* bones, int count) { geom_call<void>(29, geom, bones, count); }
inline void set_geom_bones(void* geom, uint8_t* bones, int count) { geom_call<void>(30, geom, bones, count); }
inline void break_geom_strips(void* geom, uint32_t mask) { geom_call<void>(31, geom, mask); }
inline void clean_geom_instance_strips(void* geom) { geom_call<void>(4, geom); }
inline void set_geom_shadow(void* geom, uint8_t on) { geom_call<void>(32, geom, on); }
// Slot [6], called straight through the vtable: the geom's colour (RGBA).
inline void set_geom_color(void* geom, uint32_t rgba)
{
    auto** vtable = *static_cast<void***>(geom);
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(vtable[6])(geom, rgba);
}
// The geom's multicolour flag (+0x10, the folded getter 0x005480B0).
inline bool is_geom_multicolor(void* geom) { return reinterpret_cast<online::FoldedGetters*>(geom)->byte10(); }

// An engine object deleted (0x0048C3C0) / a scene or lights deleted (0x0048C080, cdecl: the second argument only
// some callers pass); a texture dictionary unloaded (0x0049EFD0).
inline void delete_geom_object(void* object) { engine::delete_geom_object(object); }
inline void discard_scene(void* scene) { delete_model_lights(static_cast<PcModelLights*>(scene)); }
inline void discard_scene(void* scene, int) { delete_model_lights(static_cast<PcModelLights*>(scene)); }
inline void unload_textures(void* textures) { unload_tex_dict(static_cast<TextureSet*>(textures)); }

// The model's own virtual slots.
template <typename R, typename... A>
R model_virtual(RenderModel* model, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(RenderModel*, A...)>(model->vtable[slot])(model, args...);
}

// A skeleton's bone matrices (+0x04, the folded getter 0x005480C0) and bone count (+0x14, 0x004A2BA0).
inline uint8_t* skeleton_bones(uint8_t* skeleton)
{
    return reinterpret_cast<uint8_t*>(reinterpret_cast<online::FoldedGetters*>(skeleton)->word04());
}
inline int skeleton_bone_count(uint8_t* skeleton)
{
    return reinterpret_cast<gfx::Rig*>(skeleton)->get_num_bones();
}

// The list's first node (none without a head - which the original then reads through anyway).
inline ModelGeomNode* get_first(const RenderModel* model) { return model->geoms_ ? model->geoms_->next : nullptr; }
inline ModelGeom* table_row(ModelGeomNode* node) { return reinterpret_cast<ModelGeom*>(node->entry); }
inline void* get_geom(ModelGeomNode* node) { return table_row(node)->geom; }

// A bit of the visible-geoms mask (the original shifts by the index's low five bits).
inline uint32_t geom_bit(int index) { return 1u << (index & 31); }

} // namespace render::model_geoms

#pragma once

// What the model's source (model.cpp, model_geoms.cpp) reaches of the engine not rewritten yet - the engine's CGeom
// calls (the geom's virtual functions, geom.h) and the scene / object calls - and
// the list walk. Internal to those files.

#include <cstdint>

#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/model.h"
#include "engine/gfx/pc_lights.h"
#include "engine/gfx/rig.h"
#include "engine/gfx/texture_dict.h"
#include "engine/net/buddy_status.h"
#include "engine/core/raw_view.h"
#include "engine/gfx/pc_mesh.h"

namespace render::model_geoms {

// The geom's calls (the original's through jumps to its virtual slots): [3] made an instance of a scene for a model
// (0x0048DDF0 first sets its +4 to 1 and its +0x10 - multicolour - to the flag), [8] colour cleared, [9] a material
// pass coloured, [11] / [12] the viewport mask, [13] active, [27] the lights, [28] the bone matrices, [29] bones
// copied, [30] the bones pointed at, [31] strips broken, [32] the shadow, [4] instance strips cleaned, [6] the colour.
inline Geom* as_geom(void* geom) { return static_cast<Geom*>(geom); }
inline void load_geom(void* geom, PcMesh* scene, RenderModel* model, bool multicolor)
{
    as_geom(geom)->init_instance(scene, model, multicolor);
}
inline void clear_geom_color(void* geom) { as_geom(geom)->wipe_tint(); }
inline bool set_geom_material_color(void* geom, uint32_t material, int pass, uint32_t rgba)
{
    return as_geom(geom)->change_material_tint(material, pass, rgba);
}
inline void set_geom_visibility(void* geom, uint32_t mask) { as_geom(geom)->toggle_shown(mask); }
inline uint32_t get_geom_visibility(void* geom) { return as_geom(geom)->fetch_shown(); }
inline void set_geom_active(void* geom, uint8_t active) { as_geom(geom)->change_live(active != 0); }
inline void set_geom_lights(void* geom, ModelLights* lights) { as_geom(geom)->set_model_lights(lights); }
inline void set_geom_bone_matrices(void* geom, const maths::Mat4* transform, const maths::Mat4* bones, int count,
                                   int flag, int index)
{
    as_geom(geom)->upload_skinning_palette(transform, bones, count, flag, index);
}
inline void copy_geom_bones(void* geom, const maths::Mat4* bones, int count) { as_geom(geom)->copy_bones(bones, count); }
inline void set_geom_bones(void* geom, maths::Mat4* bones, int count) { as_geom(geom)->set_bones(bones, count); }
inline void break_geom_strips(void* geom, uint32_t mask) { as_geom(geom)->break_strips(mask); }
inline void clean_geom_instance_strips(void* geom) { as_geom(geom)->clean_instance_strips(); }
inline void set_geom_shadow(void* geom, uint8_t on) { as_geom(geom)->set_shadow(on != 0); }
inline void set_geom_color(void* geom, uint32_t rgba) { as_geom(geom)->change_tint(rgba); }
// The geom's multicolour flag (+0x10, the folded getter 0x005480B0).
inline bool is_geom_multicolor(void* geom) { return as_geom(geom)->multicolour_ != 0; }

// An engine object deleted (0x0048C3C0) / a scene or the model's lights deleted (0x0048C080, cdecl, the generic
// delete through slot 0: typed here); a texture dictionary unloaded (0x0049EFD0).
inline void delete_geom_object(void* object) { delete static_cast<Geom*>(object); }
inline void discard_lights(ModelLights* lights) { delete lights; }
inline void discard_mesh(PcMesh* mesh) { delete mesh; }
inline void unload_textures(TextureSet* textures) { unload_tex_dict(textures); }

// A skeleton's bone matrices (+0x04, the folded getter 0x005480C0) and bone count (+0x14, 0x004A2BA0).
inline maths::Mat4* skeleton_bones(gfx::Rig* skeleton) { return skeleton->matrix_array_; }
inline int skeleton_bone_count(gfx::Rig* skeleton) { return skeleton->get_num_bones(); }

// The list's first node (none without a head - which the original then reads through anyway).
inline ModelGeomNode* get_first(const RenderModel* model) { return model->geoms_ ? model->geoms_->next : nullptr; }
inline ModelGeom* table_row(ModelGeomNode* node) { return raw::as<ModelGeom>(node->entry); }
inline void* get_geom(ModelGeomNode* node) { return table_row(node)->geom; }

// A bit of the visible-geoms mask (the original shifts by the index's low five bits).
inline uint32_t geom_bit(int index) { return 1u << (index & 31); }

} // namespace render::model_geoms

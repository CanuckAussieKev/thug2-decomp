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
#include "engine/gfx/pc_mesh.h"
#include "engine/core/packed_colour.h"
#include <gsl/pointers>

namespace render::model_geoms {

constexpr int kShiftCountMask = 31; // x86 shifts a 32-bit value by the low five bits of the count

// The geom's calls (the original's through jumps to its virtual slots): [3] made an instance of a scene for a model
// (0x0048DDF0 first sets its +4 to 1 and its +0x10 - multicolour - to the flag), [8] colour cleared, [9] a material
// pass coloured, [11] / [12] the viewport mask, [13] active, [27] the lights, [28] the bone matrices, [29] bones
// copied, [30] the bones pointed at, [31] strips broken, [32] the shadow, [4] instance strips cleaned, [6] the colour.
inline void load_geom(Geom* geom, PcMesh* scene, RenderModel* model, bool multicolor)
{
    geom->init_instance(scene, model, static_cast<int>(multicolor));
}
inline void clear_geom_color(Geom* geom) { geom->wipe_tint(); }
inline bool set_geom_material_color(Geom* geom, uint32_t material, int pass, uint32_t rgba)
{
    return geom->change_material_tint(material, pass, colour::Packed{rgba});
}
inline void set_geom_visibility(Geom* geom, uint32_t mask) { geom->toggle_shown(mask); }
inline uint32_t get_geom_visibility(Geom* geom) { return geom->fetch_shown(); }
inline void set_geom_active(Geom* geom, uint8_t active) { geom->change_live(active != 0); }
inline void set_geom_lights(Geom* geom, ModelLights* lights) { geom->set_model_lights(lights); }
inline void set_geom_bone_matrices(Geom* geom, const maths::Mat4* transform, const maths::Mat4* bones, int count,
                                   int flag, int index)
{
    geom->upload_skinning_palette(transform, std::span<const maths::Mat4>(bones, static_cast<size_t>(count)), flag, index);
}
inline void copy_geom_bones(Geom* geom, const maths::Mat4* bones, int count) { geom->copy_bones(bones, count); }
inline void set_geom_bones(Geom* geom, maths::Mat4* bones, int count) { geom->set_bones(bones, count); }
inline void break_geom_strips(Geom* geom, uint32_t mask) { geom->break_strips(mask); }
inline void clean_geom_instance_strips(Geom* geom) { geom->clean_instance_strips(); }
inline void set_geom_shadow(Geom* /*geom*/, uint8_t on) { render::Geom::set_shadow(on != 0); }
inline void set_geom_color(Geom* geom, uint32_t rgba) { geom->change_tint(rgba); }
// The geom's multicolour flag (+0x10, the folded getter 0x005480B0).
inline bool is_geom_multicolor(Geom* geom) { return geom->multicolour() != 0; }

// An engine object deleted (0x0048C3C0) / a scene or the model's lights deleted (0x0048C080, cdecl, the generic
// delete through slot 0: typed here); a texture dictionary unloaded (0x0049EFD0).
inline void delete_geom_object(gsl::owner<Geom*> object) { delete object; }
inline void discard_lights(gsl::owner<ModelLights*> lights) { delete lights; }
inline void discard_mesh(gsl::owner<PcMesh*> mesh) { delete mesh; }
inline void unload_textures(TextureSet* textures) { unload_tex_dict(textures); }

// A skeleton's bone matrices (+0x04, the folded getter 0x005480C0) and bone count (+0x14, 0x004A2BA0).
inline maths::Mat4* skeleton_bones(gfx::Rig* skeleton) { return skeleton->matrix_array(); }
inline int skeleton_bone_count(gfx::Rig* skeleton) { return skeleton->get_num_bones(); }

// The list's first node (none without a head - which the original then reads through anyway).
inline ModelGeomNode* get_first(const RenderModel* model) { return model->geom_ring().head() ? model->geom_ring().head()->next : nullptr; }
inline ModelGeom* table_row(const ModelGeomNode* node) { return node->value; }
inline Geom* get_geom(ModelGeomNode* node) { return table_row(node)->geom; }

// A bit of the visible-geoms mask (the original shifts by the index's low five bits).
inline uint32_t geom_bit(int index) { return 1U << (index & kShiftCountMask); }

} // namespace render::model_geoms

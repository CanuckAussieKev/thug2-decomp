#pragma once

// What the model component's source (model_component*.cpp) reaches of the code not rewritten yet: render::RenderModel and its
// geoms and lights (called at their addresses), the network's players. Internal to those files.

#include <cstdint>

#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_utils.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/player_info.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/skeleton_component.h"
#include "engine/core/down_cast.h"

namespace render {
class RenderModel;
} // namespace render

namespace objects::model_nx {

// A skeleton component's skeleton (0x005480F0: its +0x18).
inline gfx::Rig* skeleton_of(ObjectModule* component) { return core::down_cast<objects::BoneRigModule>(component)->bone_rig_; }

// The model's lights (+0x2C, the folded getter 0x00532EB0), its file checksum (+0x30) and flags (+0x44: 2 active, 4
// hidden).
inline void* model_lights(render::RenderModel* model) { return model->lights_ptr_.get(); }
inline uint32_t model_file_checksum(render::RenderModel* model) { return model->source_path_; }
inline uint8_t get_model_flags(render::RenderModel* model) { return model->flag_bits_; }
// The model loaded from an asset (0x004934A0: name, geom name, multicolour, 1).
inline bool load_asset(render::RenderModel* model, uint32_t name, uint32_t mesh_label, bool multicolor)
{
    return model->add_asset_geom(name, mesh_label, multicolor, true);
}
// The scale from a structure's scale parameters (0x00487B00, cdecl: false without any), set on a model
// (0x00492EE0) and switched on or off (0x00492E60).
inline bool fetch_magnification(maths::Vec4* scale, void* params)
{
    return gfx::scale_from_args(scale, static_cast<script::Struct*>(params));
}
inline void change_scale(render::RenderModel* model, maths::Vec4* scale) { model->change_scale(scale); }
inline void allow_resizing(render::RenderModel* model, bool on) { model->allow_resizing(on); }
// The model's lights' brightness from the level's (0x0048E660).
inline void update_lights_brightness(void* lights)
{
    static_cast<render::ModelLights*>(lights)->get_update_brightness();
}

// The network manager (0x007CCE14) and its players: the player with an object id (0x0053AEA0), whether it's this
// machine's (0x00543300), its skater's index (0x005434B0) and that index's client app (0x00532EA0).
inline online::Hub* net_session_slot() { return online::online_hub(); }
inline void* find_player_slot_by_obj(void* manager, uint32_t id)
{
    return static_cast<online::Hub*>(manager)->find_player_slot_by_obj(static_cast<uint16_t>(id));
}
inline bool is_own_player(void* player) { return static_cast<online::PlayerRecord*>(player)->is_own_player() != 0; }
inline int player_skater_index(void* player)
{
    return static_cast<int>(static_cast<online::PlayerRecord*>(player)->get_skater_view_slot());
}
inline net::Guest* client_app(void* manager, int index)
{
    return core::down_cast<net::Guest>(static_cast<online::Hub*>(manager)->fetch_patron(index));
}

// The model's lights' direction setter: their vtable slot 2 with (index, direction). No address of its own: the
// original calls the tail jump to slot 2 folded at 0x004E3F90 (GameObjects::reapply_from_data).
void set_lights_direction(void* lights, int index, const maths::Vec4* direction);

} // namespace objects::model_nx

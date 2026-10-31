#pragma once

// What the model component's source (model_component*.cpp) reaches of the code not rewritten yet: Engine3D::CRenderModel and its
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

namespace Engine3D {
class CRenderModel;
} // namespace Nx

namespace GameObjects::ModelNx {

// A skeleton component's skeleton (0x005480F0: its +0x18), a skeleton's name (0x004A2BA0: its +0x14) and data (+0x08).
inline void* SkeletonOf(CObjectModule* component)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x18);
}
inline uint32_t SkeletonName(void* skeleton)
{
    return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(skeleton) + 0x14);
}
inline void* RigDefinition(void* skeleton) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(skeleton) + 0x08); }

// The model's lights (+0x2C, the folded getter 0x00532EB0), its file checksum (+0x30) and flags (+0x44: 2 active, 4
// hidden).
inline void* ModelLights(Engine3D::CRenderModel* model)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(model) + 0x2C);
}
inline uint32_t ModelFileChecksum(Engine3D::CRenderModel* model)
{
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(model) + 0x30);
}
inline uint8_t ModelFlags(Engine3D::CRenderModel* model) { return reinterpret_cast<uint8_t*>(model)[0x44]; }

// A level sector by name in any scene (0x0048C510) and its geometry (0x004A2BA0: its +0x14).
inline void* LookUpSector(uint32_t name) { return Engine3D::Engine::LookUpSector(name); }
inline void* SectorGeom(void* sector) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(sector) + 0x14); }
// A geom cloned for a model (0x0048DDC0) or from a sector (0x0048DD80), and activated (0x0048DE60); one added to a
// model (0x004938E0).
inline void* CloneModelGeom(void* geom, Engine3D::CRenderModel* to) { return static_cast<Engine3D::CGeom*>(geom)->CloneForModel(1, to); }
inline void* CloneSectorGeom(void* geom) { return static_cast<Engine3D::CGeom*>(geom)->Replicate(1, nullptr); }
inline void ActivateGeom(void* geom) { reinterpret_cast<void(__thiscall*)(void*, int)>(&Engine3D::VirtualSlot13Jump)(geom, 1); }
inline void AddGeom(Engine3D::CRenderModel* model, void* geom, uint32_t name) { model->AddGeom(geom, name, true); }
// The model loaded from an asset (0x004934A0: name, geom name, multicolour, 1).
inline bool LoadAsset(Engine3D::CRenderModel* model, uint32_t name, uint32_t meshLabel, bool multicolor)
{
    return model->AddAssetGeom(name, meshLabel, multicolor, true);
}
// The scale from a structure's scale parameters (0x00487B00, cdecl: false without any), set on a model
// (0x00492EE0) and switched on or off (0x00492E60).
inline bool FetchMagnification(Maths::Vec4* scale, void* params)
{
    return Gfx2D::ScaleFromArgs(scale, static_cast<Scripts::CStruct*>(params));
}
inline void ChangeScale(Engine3D::CRenderModel* model, Maths::Vec4* scale) { model->ChangeScale(scale); }
inline void AllowResizing(Engine3D::CRenderModel* model, bool on) { model->AllowResizing(on); }
// The model's lights' brightness from the level's (0x0048E660).
inline void UpdateLightsBrightness(void* lights) { static_cast<Engine3D::CModelLights*>(lights)->UpdateBrightness(); }

// The network manager (0x007CCE14) and its players: the player with an object id (0x0053AEA0), whether it's this
// machine's (0x00543300), its skater's index (0x005434B0) and that index's client app (0x00532EA0).
inline void* NetSessionSlot() { return reinterpret_cast<void*>(OnlinePlay::g_onlineHub); }
inline void* FindPlayerSlotByObj(void* manager, uint32_t id)
{
    return static_cast<OnlinePlay::Hub*>(manager)->FindPlayerSlotByObj(static_cast<uint16_t>(id));
}
inline bool IsOwnPlayer(void* player) { return static_cast<OnlinePlay::PlayerRecord*>(player)->IsOwnPlayer() != 0; }
inline int PlayerSkaterIndex(void* player)
{
    return static_cast<int>(static_cast<OnlinePlay::PlayerRecord*>(player)->GetSkaterValue2BC());
}
inline NetLink::Guest* ClientApp(void* manager, int index)
{
    return static_cast<NetLink::Guest*>(static_cast<OnlinePlay::Hub*>(manager)->FetchPatron(index));
}

// The model's lights' direction setter: their vtable slot 2 with (index, direction). No address of its own: the
// original calls the tail jump to slot 2 folded at 0x004E3F90 (GameObjects::ReapplyFromData). (__fastcall with an unused
// second argument: the lights in ECX and the two on the stack, popped - the original call's layout.)
void __fastcall SetLightsDirection(void* lights, void* unused, int index, const Maths::Vec4* direction);

} // namespace GameObjects::ModelNx

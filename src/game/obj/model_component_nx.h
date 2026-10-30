#pragma once

// What the model component's source (model_component*.cpp) reaches of the code not rewritten yet: Nx::CModel and its
// geoms and lights (called at their addresses), the network's players. Internal to those files.

#include <cstdint>

#include "game/gfx/engine.h"
#include "game/gfx/engine_geom.h"
#include "game/gfx/engine_model_lights.h"
#include "game/gfx/model.h"
#include "game/gfx/model_utils.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "hook/game.h"

namespace Nx {
class CModel;
} // namespace Nx

namespace Obj::ModelNx {

// A skeleton component's skeleton (0x005480F0: its +0x18), a skeleton's name (0x004A2BA0: its +0x14) and data (+0x08).
inline void* SkeletonOf(CBaseComponent* component)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x18);
}
inline uint32_t SkeletonName(void* skeleton)
{
    return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(skeleton) + 0x14);
}
inline void* SkeletonData(void* skeleton) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(skeleton) + 0x08); }

// The model's lights (+0x2C, the folded getter 0x00532EB0), its file checksum (+0x30) and flags (+0x44: 2 active, 4
// hidden).
inline void* ModelLights(Nx::CModel* model)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(model) + 0x2C);
}
inline uint32_t ModelFileChecksum(Nx::CModel* model)
{
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(model) + 0x30);
}
inline uint8_t ModelFlags(Nx::CModel* model) { return reinterpret_cast<uint8_t*>(model)[0x44]; }

// A level sector by name in any scene (0x0048C510) and its geometry (0x004A2BA0: its +0x14).
inline void* GetSector(uint32_t name) { return Nx::Engine::GetSector(name); }
inline void* SectorGeom(void* sector) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(sector) + 0x14); }
// A geom cloned for a model (0x0048DDC0) or from a sector (0x0048DD80), and activated (0x0048DE60); one added to a
// model (0x004938E0).
inline void* CloneModelGeom(void* geom, Nx::CModel* to) { return static_cast<Nx::CGeom*>(geom)->CloneForModel(1, to); }
inline void* CloneSectorGeom(void* geom) { return static_cast<Nx::CGeom*>(geom)->Clone(1, nullptr); }
inline void ActivateGeom(void* geom) { GameFunction<void(__thiscall*)(void*, int)>(0x0048DE60)(geom, 1); }
inline void AddGeom(Nx::CModel* model, void* geom, uint32_t name) { model->AddGeom(geom, name, true); }
// The model loaded from an asset (0x004934A0: name, geom name, multicolour, 1).
inline bool LoadAsset(Nx::CModel* model, uint32_t name, uint32_t geomName, bool multicolor)
{
    return model->AddAssetGeom(name, geomName, multicolor, true);
}
// The scale from a structure's scale parameters (0x00487B00, cdecl: false without any), set on a model
// (0x00492EE0) and switched on or off (0x00492E60).
inline bool GetScale(Mth::Vector* scale, void* params)
{
    return Gfx::GetScaleFromParams(scale, static_cast<Script::CStruct*>(params));
}
inline void SetScale(Nx::CModel* model, Mth::Vector* scale) { model->SetScale(scale); }
inline void EnableScaling(Nx::CModel* model, bool on) { model->EnableScaling(on); }
// The model's lights' brightness from the level's (0x0048E660).
inline void UpdateLightsBrightness(void* lights) { static_cast<Nx::CModelLights*>(lights)->UpdateBrightness(); }

// The network manager (0x007CCE14) and its players: the player with an object id (0x0053AEA0), whether it's this
// machine's (0x00543300), its skater's index (0x005434B0) and that index's client app (0x00532EA0).
inline void* NetManager() { return reinterpret_cast<void*>(GameNet::g_gameNetManager); }
inline void* PlayerByObjectId(void* manager, uint32_t id)
{
    return static_cast<GameNet::Manager*>(manager)->PlayerByObjectId(static_cast<uint16_t>(id));
}
inline bool IsLocalPlayer(void* player) { return static_cast<GameNet::PlayerInfo*>(player)->IsLocalPlayer() != 0; }
inline int PlayerSkaterIndex(void* player)
{
    return static_cast<int>(static_cast<GameNet::PlayerInfo*>(player)->GetSkaterValue2BC());
}
inline Net::Client* ClientApp(void* manager, int index)
{
    return static_cast<Net::Client*>(static_cast<GameNet::Manager*>(manager)->GetClient(index));
}

// The model's lights' direction setter: their vtable slot 2 with (index, direction). No address of its own: the
// original calls the tail jump to slot 2 folded at 0x004E3F90 (Obj::RefreshFromStructure). (__fastcall with an unused
// second argument: the lights in ECX and the two on the stack, popped - the original call's layout.)
void __fastcall SetLightsDirection(void* lights, void* unused, int index, const Mth::Vector* direction);

} // namespace Obj::ModelNx

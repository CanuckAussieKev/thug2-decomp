// The stream component (game/obj/stream_component.h).

#include "game/obj/stream_component.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/obj/emitter.h"
#include "game/obj/proxim.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream_commands.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00646594);
constexpr float kInchesPerFoot = 12.0f; // 0x0064546C

const Mth::Vector* CameraPosition(void* camera)
{
    return static_cast<Gfx::Camera*>(camera)->GetPos();
}

} // namespace

// 0x00433D90
CBaseComponent* __cdecl CStreamComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CStreamComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CStreamComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    component->mp_emitter = nullptr;
    component->mp_proxim = nullptr;
    return component;
}

// 0x00433870
void __cdecl CStreamComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), &GameVariable<uint32_t>(0x00684E70), 0x18);
}

// 0x00433C10
CStreamComponent* CStreamComponent::Construct()
{
    CBaseComponent::Construct();
    mp_emitter = nullptr;
    mp_proxim = nullptr;
    m_vtable = kVtable;
    m_type = kType;
    return this;
}

// 0x00433890
void CStreamComponent::DestroyBody()
{
    m_vtable = kVtable;
    for (uint32_t stream : m_streams) {
        if (stream)
            Pcm::StopStream(stream);
    }
    CBaseComponent::DestroyBody();
}

// 0x00433C40
CStreamComponent* CStreamComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00433900
void CStreamComponent::InitFromStructure(Script::CStruct* params)
{
    uint32_t node = reinterpret_cast<uint32_t>(params); // (read into the parameters' own argument slot)
    if (params->GetChecksum(0x08470F2E, &node, 0)) // ProximNode
        mp_proxim = Proxim_GetNode(node);
}

// 0x00433CD0
Mth::Vector* CStreamComponent::GetStreamPosition(Mth::Vector* out, void* camera)
{
    if (mp_emitter && camera) {
        mp_emitter->GetClosestPoint(out, CameraPosition(camera));
        return out;
    }
    *out = m_object->m_pos;
    return out;
}

// 0x00433D30
bool CStreamComponent::GetStreamExtent(void* camera, Mth::Vector* out)
{
    if (!camera || !mp_proxim)
        return false;
    Mth::Vector nearest;
    *out = *mp_proxim->GetClosestPoint(&nearest, CameraPosition(camera));
    return true;
}

// 0x00433970
bool CStreamComponent::PlayStream(Script::CStruct* params, Script::CScript* /*script*/)
{
    bool room = false;
    for (uint32_t stream : m_streams)
        room = room || !stream;
    if (!room)
        return false;
    float volume = 100.0f, pitch = 100.0f, dropoff = 0.0f;
    int priority = 50, loops = 0, falloff = 0;
    uint32_t id = 0;
    params->GetFloat(0xF6A36814, &volume, 0);    // vol
    params->GetFloat(0xD8604126, &pitch, 0);     // pitch
    params->GetInteger(0x9D5923D8, &priority, 0); // priority
    params->GetInteger(0xE46111E0, &loops, 0);    // num_loops
    params->GetChecksum(0x40C698AF, &id, 0);      // id
    if (params->GetFloat(0xFF2020EC, &dropoff, 0)) // dropoff (feet)
        dropoff = dropoff * kInchesPerFoot;
    uint32_t function = 0; // (uninitialised in the original: only read when found)
    if (params->GetChecksum(0x0C6AC50A, &function, 0)) // dropoff_function
        falloff = Sfx::FalloffFromChecksum(function);
    uint32_t emitter = 0;
    if (params->GetChecksum(0x8A7132CE, &emitter, 0)) // emitter
        mp_emitter = Emitter_GetNode(emitter);
    else
        mp_emitter = nullptr;
    uint32_t stream = 0;
    if (!params->GetChecksum(0u, &stream, 0))
        return false;
    int usePosInfo = 1; // (read into the parameters' own argument slot)
    params->GetInteger(0xC9B0DEB7, &usePosInfo, 0); // use_pos_info
    return Pcm::PlayStreamFromObject(this, stream, dropoff, volume, pitch, loops, priority, usePosInfo, falloff, id) != 0;
}

// 0x00433B30
bool CStreamComponent::AddStream(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t stream = 0;
    if (!params->GetChecksum(0u, &stream, 0))
        return false;
    uint32_t name = 0; // (read into the parameters' own argument slot, cleared first)
    if (!params->GetChecksum(0x02E58C18, &name, 0)) // Add
        return false;
    return Pcm::AddStream(stream, name, this);
}

// 0x00433BA0
bool CStreamComponent::RemoveStream(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t stream = 0;
    if (!params->GetChecksum(0u, &stream, 0))
        return false;
    uint32_t name = 0; // (read into the parameters' own argument slot, cleared first)
    if (!params->GetChecksum(0x977FE2CF, &name, 0)) // remove
        return false;
    return Pcm::RemoveStream(stream, name, this);
}

// 0x00433C70
bool CStreamComponent::Obj_PlayStream(Script::CStruct* params, Script::CScript* script)
{
    PlayStream(params, script);
    return true;
}

// 0x00433C90
bool CStreamComponent::Obj_AddStream(Script::CStruct* params, Script::CScript* script)
{
    AddStream(params, script);
    return true;
}

// 0x00433CB0
bool CStreamComponent::Obj_RemoveStream(Script::CStruct* params, Script::CScript* script)
{
    RemoveStream(params, script);
    return true;
}

// 0x00433930
bool CStreamComponent::Obj_StopStream(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    for (uint32_t& stream : m_streams) {
        if (stream) {
            Pcm::StopStream(stream);
            stream = 0;
        }
    }
    return true;
}

} // namespace Obj

REPLACE(0x00433D90, Obj::CStreamComponent::Create);
REPLACE(0x00433870, Obj::CStreamComponent::RegisterMemberFunctions);
REPLACE(0x00433C10, &Obj::CStreamComponent::Construct);
REPLACE(0x00433890, &Obj::CStreamComponent::DestroyBody);
REPLACE(0x00433C40, &Obj::CStreamComponent::Destroy);
REPLACE(0x00433900, &Obj::CStreamComponent::InitFromStructure);
REPLACE(0x00433CD0, &Obj::CStreamComponent::GetStreamPosition);
REPLACE(0x00433D30, &Obj::CStreamComponent::GetStreamExtent);
REPLACE(0x00433970, &Obj::CStreamComponent::PlayStream);
REPLACE(0x00433B30, &Obj::CStreamComponent::AddStream);
REPLACE(0x00433BA0, &Obj::CStreamComponent::RemoveStream);
REPLACE(0x00433C70, &Obj::CStreamComponent::Obj_PlayStream);
REPLACE(0x00433C90, &Obj::CStreamComponent::Obj_AddStream);
REPLACE(0x00433CB0, &Obj::CStreamComponent::Obj_RemoveStream);
REPLACE(0x00433930, &Obj::CStreamComponent::Obj_StopStream);

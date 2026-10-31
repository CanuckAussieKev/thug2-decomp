// The stream component (game/obj/stream_component.h).

#include "game/obj/stream_component.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/view_cam.h"
#include "game/obj/emitter_object.h"
#include "game/obj/proximity.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream_commands.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/sfx/sfx.h"
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

uint32_t g_streamComponentMemberFunctions[30]{
            0xDB05A233, 0x0F1641E3, 0, 0, 0, 0,
            0xC66E1B17, 0x0F1641E3, 0, 0, 0, 0,
            0x82EC1DDD, 0x0F1641E3, 0, 0, 0, 0,
            0xA738AEB8, 0x0F1641E3, 0, 0, 0, 0,
            0, 0x0F1641E3, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
constexpr float kInchesPerFoot = 12.0f; // 0x0064546C

const Maths::Vec4* ViewerLocation(void* camera)
{
    return static_cast<Gfx2D::ViewCam*>(camera)->FetchLocation();
}

} // namespace

// 0x00433D90
CObjectModule* __cdecl CAudioStreamModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CAudioStreamModule));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CAudioStreamModule*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    component->m_spewer = nullptr;
    component->mp_proxim = nullptr;
    return component;
}

// 0x00433870
void __cdecl CAudioStreamModule::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_streamComponentMemberFunctions, 0x18);
}

// 0x00433C10
CAudioStreamModule* CAudioStreamModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_spewer = nullptr;
    mp_proxim = nullptr;
    m_vtable = kVtable;
    m_kindTag = kType;
    return this;
}

// 0x00433890
void CAudioStreamModule::DestroyBody()
{
    m_vtable = kVtable;
    for (uint32_t stream : m_streams) {
        if (stream)
            AudioStream::HaltStream(stream);
    }
    CObjectModule::DestroyBody();
}

// 0x00433C40
CAudioStreamModule* CAudioStreamModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00433900
void CAudioStreamModule::ConfigureFromData(Scripts::CStruct* params)
{
    uint32_t node = reinterpret_cast<uint32_t>(params); // (read into the parameters' own argument slot)
    if (params->ReadHashValue(0x08470F2E, &node, 0)) // ProximNode
        mp_proxim = Proxim_GetNode(node);
}

// 0x00433CD0
Maths::Vec4* CAudioStreamModule::GetStreamPosition(Maths::Vec4* out, void* camera)
{
    if (m_spewer && camera) {
        m_spewer->NearestPointOn(out, ViewerLocation(camera));
        return out;
    }
    *out = m_entity->m_atPoint;
    return out;
}

// 0x00433D30
bool CAudioStreamModule::GetStreamExtent(void* camera, Maths::Vec4* out)
{
    if (!camera || !mp_proxim)
        return false;
    Maths::Vec4 nearest;
    *out = *mp_proxim->NearestPointOn(&nearest, ViewerLocation(camera));
    return true;
}

// 0x00433970
bool CAudioStreamModule::PlayStream(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    bool room = false;
    for (uint32_t stream : m_streams)
        room = room || !stream;
    if (!room)
        return false;
    float volume = 100.0f, pitch = 100.0f, dropoff = 0.0f;
    int priority = 50, loops = 0, falloff = 0;
    uint32_t id = 0;
    params->FetchFloat(0xF6A36814, &volume, 0);    // vol
    params->FetchFloat(0xD8604126, &pitch, 0);     // pitch
    params->FetchIntValue(0x9D5923D8, &priority, 0); // priority
    params->FetchIntValue(0xE46111E0, &loops, 0);    // num_loops
    params->ReadHashValue(0x40C698AF, &id, 0);      // id
    if (params->FetchFloat(0xFF2020EC, &dropoff, 0)) // dropoff (feet)
        dropoff = dropoff * kInchesPerFoot;
    uint32_t function = 0; // (uninitialised in the original: only read when found)
    if (params->ReadHashValue(0x0C6AC50A, &function, 0)) // dropoff_function
        falloff = SoundFx::FalloffFromChecksum(function);
    uint32_t emitter = 0;
    if (params->ReadHashValue(0x8A7132CE, &emitter, 0)) // emitter
        m_spewer = Emitter_GetNode(emitter);
    else
        m_spewer = nullptr;
    uint32_t stream = 0;
    if (!params->ReadHashValue(0u, &stream, 0))
        return false;
    int usePosInfo = 1; // (read into the parameters' own argument slot)
    params->FetchIntValue(0xC9B0DEB7, &usePosInfo, 0); // use_pos_info
    return AudioStream::PlayStreamAtObject(this, stream, dropoff, volume, pitch, loops, priority, usePosInfo, falloff, id) != 0;
}

// 0x00433B30
bool CAudioStreamModule::AddStream(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t stream = 0;
    if (!params->ReadHashValue(0u, &stream, 0))
        return false;
    uint32_t name = 0; // (read into the parameters' own argument slot, cleared first)
    if (!params->ReadHashValue(0x02E58C18, &name, 0)) // Add
        return false;
    return AudioStream::AddStream(stream, name, this);
}

// 0x00433BA0
bool CAudioStreamModule::RemoveStream(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t stream = 0;
    if (!params->ReadHashValue(0u, &stream, 0))
        return false;
    uint32_t name = 0; // (read into the parameters' own argument slot, cleared first)
    if (!params->ReadHashValue(0x977FE2CF, &name, 0)) // remove
        return false;
    return AudioStream::RemoveStream(stream, name, this);
}

// 0x00433C70
bool CAudioStreamModule::Obj_PlayStream(Scripts::CStruct* params, Scripts::CScript* script)
{
    PlayStream(params, script);
    return true;
}

// 0x00433C90
bool CAudioStreamModule::Obj_AddStream(Scripts::CStruct* params, Scripts::CScript* script)
{
    AddStream(params, script);
    return true;
}

// 0x00433CB0
bool CAudioStreamModule::Obj_RemoveStream(Scripts::CStruct* params, Scripts::CScript* script)
{
    RemoveStream(params, script);
    return true;
}

// 0x00433930
bool CAudioStreamModule::Obj_StopStream(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    for (uint32_t& stream : m_streams) {
        if (stream) {
            AudioStream::HaltStream(stream);
            stream = 0;
        }
    }
    return true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CAudioStreamModule::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         &GameObjects::CAudioStreamModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj

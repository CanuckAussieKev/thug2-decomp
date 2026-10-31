// The skater's finalize-physics component (GameObjects::CRiderPhysicsCommit): see finalize_physics.h.

#include "game/skater/finalize_physics.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/mat4.h"
#include "game/mth/vec4.h"
#include "game/skater/looping_sound.h"
#include "game/skater/rider.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterFinalizePhysics = 0x9B373E58;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kSkaterState = 0x43686585, kSkaterSound = 0x187574FA,
                   kSkaterLoopingSound = 0xB5F1E9D2;
constexpr uint32_t kSkaterMaxMaxSpeedStat = 0xCC5F87AA; // Skater_Max_Max_Speed_Stat

constexpr float kStillSpeed = 1.0f; // 0x006453A8: x and z speeds below this count as straight up

template <typename T>
T& Prop(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The skater state component's state (+0x18), as the core physics' update dispatches on it (0x00510830): 0 ground,
// 1 air (DoAirPhysics), 2-5 its other physics (3: a lip trick, which sets the core physics' +0x256 for the frame).
constexpr int kStateAir = 1, kStateLip = 3;

// The skater's physics value `name` (as GetScaledValue).
double ScaledPhysicsValue(CGameEntity* skater, uint32_t name)
{
    return static_cast<CRider*>(skater)->GetScaledPhysics(name);
}
// The looping sound component: the speed as a fraction of the top speed.
void SetLoopingSoundSpeed(CObjectModule* sound, float fraction)
{
    static_cast<CRiderRollingAudio*>(sound)->SetPaceRatio(fraction);
}

// `value` into `stored`, `changed` set if it differs.
template <typename T>
void Note(T& stored, T value, uint8_t& changed)
{
    changed |= stored != value ? 1 : 0;
    stored = value;
}

} // namespace

// 0x00511280
CObjectModule* __cdecl CRiderPhysicsCommit::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderPhysicsCommit));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderPhysicsCommit*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kSkaterFinalizePhysics;
    component->m_corePhysics = nullptr;
    component->m_curPhase = nullptr;
    return component;
}

// 0x00511010
void CRiderPhysicsCommit::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00511020
CRiderPhysicsCommit* CRiderPhysicsCommit::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00511050
void CRiderPhysicsCommit::Seal()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterCorePhysics));
    m_curPhase = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterState));
}

// 0x00511080
void CRiderPhysicsCommit::TickFrame()
{
    uint8_t* core = m_corePhysics;
    uint8_t* state = m_curPhase;
    // Out of a lip trick (the physics' +0x256: the frame began in one) straight up: into the air with hardly any speed
    // across the ground. Kept while in the air.
    if (Prop<uint8_t>(core, 0x256) && Prop<int>(state, 0x18) != kStateLip) {
        Prop<uint8_t>(state, 0xF4) = 0;
        if (Prop<int>(state, 0x18) == kStateAir && std::fabs(m_entity->m_vel.x) < kStillSpeed &&
            std::fabs(m_entity->m_vel.z) < kStillSpeed)
            Prop<uint8_t>(state, 0xF4) = 1;
    }
    if (Prop<int>(state, 0x18) != kStateAir)
        Prop<uint8_t>(state, 0xF4) = 0;
    Prop<Maths::Vec4>(state, 0xF8) = Prop<Maths::Vec4>(core, 0x58);
    Prop<Maths::Vec4>(state, 0x108) = Prop<Maths::Vec4>(core, 0x68);

    // The matrices: orthonormal again about up; the physics' own replaced by the object's if that needed repair.
    m_entity->m_basisFrame.SquareUpAxes(1);
    Maths::Mat4& physicsMatrix = Prop<Maths::Mat4>(core, 0x18);
    physicsMatrix.SquareUpAxes(1);
    if (m_entity->m_basisFrame.RestoreRightAngles())
        physicsMatrix = reinterpret_cast<CObjectModule*>(core)->m_entity->m_basisFrame;
    m_entity->m_displayMatrix = physicsMatrix;

    // The sounds: terrain and state.
    uint8_t* sound = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterSound));
    Prop<uint8_t>(sound, 0x1E) = Prop<uint8_t>(m_curPhase, 0x58);
    Prop<uint8_t>(sound, 0x1C) = Prop<uint8_t>(m_curPhase, 0xE1);
    CObjectModule* looping = m_entity->FetchModule(kSkaterLoopingSound);
    double x = m_entity->m_vel.x, z = m_entity->m_vel.z;
    float speed = static_cast<float>(std::sqrt(x * x + z * z));
    float fraction = static_cast<float>(speed / ScaledPhysicsValue(m_entity, kSkaterMaxMaxSpeedStat));
    SetLoopingSoundSpeed(looping, fraction);
    uint8_t* loop = reinterpret_cast<uint8_t*>(looping);
    Prop<uint8_t>(loop, 0xFF) = Prop<uint8_t>(m_curPhase, 0x70);
    uint8_t& changed = Prop<uint8_t>(loop, 0xFD);
    Note(Prop<uint8_t>(loop, 0x100), Prop<uint8_t>(m_curPhase, 0x58), changed);
    Note(Prop<uint8_t>(loop, 0x60), Prop<uint8_t>(m_curPhase, 0xE1), changed);
    Note(Prop<int>(loop, 0x74), Prop<int>(m_curPhase, 0x18), changed);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderPhysicsCommit::Teardown,
         &GameObjects::CRiderPhysicsCommit::TickFrame,
         Stubs::IgnoreArgument,
         &GameObjects::CRiderPhysicsCommit::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj

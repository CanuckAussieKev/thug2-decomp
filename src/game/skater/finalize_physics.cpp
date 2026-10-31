// The skater's finalize-physics component (Obj::CSkaterFinalizePhysicsComponent): see finalize_physics.h.

#include "game/skater/finalize_physics.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/skater/looping_sound.h"
#include "game/skater/skater.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterFinalizePhysics = 0x9B373E58;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kSkaterState = 0x43686585, kSkaterSound = 0x187574FA,
                   kSkaterLoopingSound = 0xB5F1E9D2;
constexpr uint32_t kSkaterMaxMaxSpeedStat = 0xCC5F87AA; // Skater_Max_Max_Speed_Stat

constexpr float kStillSpeed = 1.0f; // 0x006453A8: x and z speeds below this count as straight up

template <typename T>
T& Field(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The skater state component's state (+0x18), as the core physics' update dispatches on it (0x00510830): 0 ground,
// 1 air (DoAirPhysics), 2-5 its other physics (3: a lip trick, which sets the core physics' +0x256 for the frame).
constexpr int kStateAir = 1, kStateLip = 3;

// The skater's physics value `name` (as GetScaledValue).
double ScaledPhysicsValue(CCompositeObject* skater, uint32_t name)
{
    return static_cast<CSkater*>(skater)->GetScaledPhysics(name);
}
// The looping sound component: the speed as a fraction of the top speed.
void SetLoopingSoundSpeed(CBaseComponent* sound, float fraction)
{
    static_cast<CSkaterLoopingSoundComponent*>(sound)->SetSpeedFraction(fraction);
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
CBaseComponent* __cdecl CSkaterFinalizePhysicsComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterFinalizePhysicsComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterFinalizePhysicsComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterFinalizePhysics;
    component->m_corePhysics = nullptr;
    component->m_state = nullptr;
    return component;
}

// 0x00511010
void CSkaterFinalizePhysicsComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00511020
CSkaterFinalizePhysicsComponent* CSkaterFinalizePhysicsComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00511050
void CSkaterFinalizePhysicsComponent::Finalize()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterCorePhysics));
    m_state = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterState));
}

// 0x00511080
void CSkaterFinalizePhysicsComponent::Update()
{
    uint8_t* core = m_corePhysics;
    uint8_t* state = m_state;
    // Out of a lip trick (the physics' +0x256: the frame began in one) straight up: into the air with hardly any speed
    // across the ground. Kept while in the air.
    if (Field<uint8_t>(core, 0x256) && Field<int>(state, 0x18) != kStateLip) {
        Field<uint8_t>(state, 0xF4) = 0;
        if (Field<int>(state, 0x18) == kStateAir && std::fabs(m_object->m_vel.x) < kStillSpeed &&
            std::fabs(m_object->m_vel.z) < kStillSpeed)
            Field<uint8_t>(state, 0xF4) = 1;
    }
    if (Field<int>(state, 0x18) != kStateAir)
        Field<uint8_t>(state, 0xF4) = 0;
    Field<Mth::Vector>(state, 0xF8) = Field<Mth::Vector>(core, 0x58);
    Field<Mth::Vector>(state, 0x108) = Field<Mth::Vector>(core, 0x68);

    // The matrices: orthonormal again about up; the physics' own replaced by the object's if that needed repair.
    m_object->m_matrix.OrthoNormalizeAbout(1);
    Mth::Matrix& physicsMatrix = Field<Mth::Matrix>(core, 0x18);
    physicsMatrix.OrthoNormalizeAbout(1);
    if (m_object->m_matrix.PatchOrthogonality())
        physicsMatrix = reinterpret_cast<CBaseComponent*>(core)->m_object->m_matrix;
    m_object->m_displayMatrix = physicsMatrix;

    // The sounds: terrain and state.
    uint8_t* sound = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterSound));
    Field<uint8_t>(sound, 0x1E) = Field<uint8_t>(m_state, 0x58);
    Field<uint8_t>(sound, 0x1C) = Field<uint8_t>(m_state, 0xE1);
    CBaseComponent* looping = m_object->GetComponent(kSkaterLoopingSound);
    double x = m_object->m_vel.x, z = m_object->m_vel.z;
    float speed = static_cast<float>(std::sqrt(x * x + z * z));
    float fraction = static_cast<float>(speed / ScaledPhysicsValue(m_object, kSkaterMaxMaxSpeedStat));
    SetLoopingSoundSpeed(looping, fraction);
    uint8_t* loop = reinterpret_cast<uint8_t*>(looping);
    Field<uint8_t>(loop, 0xFF) = Field<uint8_t>(m_state, 0x70);
    uint8_t& changed = Field<uint8_t>(loop, 0xFD);
    Note(Field<uint8_t>(loop, 0x100), Field<uint8_t>(m_state, 0x58), changed);
    Note(Field<uint8_t>(loop, 0x60), Field<uint8_t>(m_state, 0xE1), changed);
    Note(Field<int>(loop, 0x74), Field<int>(m_state, 0x18), changed);
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CSkaterFinalizePhysicsComponent::Destroy,
         &Obj::CSkaterFinalizePhysicsComponent::Update,
         Stubs::IgnoreArgument,
         &Obj::CSkaterFinalizePhysicsComponent::Finalize,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj

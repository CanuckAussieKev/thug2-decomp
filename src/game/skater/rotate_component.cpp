// The skater's rotate component (Obj::CSkaterRotateComponent): see rotate_component.h.

#include "game/skater/rotate_component.h"
#include "game/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterRotate = 0x52D61A0D, kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kRotate = 0xC876ADF3;
constexpr uint32_t kDuration = 0x79A07F3F, kSeconds = 0xD029F619, kSecond = 0x49E0EE96, kFrames = 0x019176C5,
                   kFrame = 0x4A07C332, kX = 0x7323E97C, kY = 0x0424D9EA, kZ = 0x9D2D8850, kNode = 0x7A8017BA;

constexpr float kDegreesToRadians = 0.01745329238474369f; // 0x00645464
constexpr float kRadiansToDegrees = 57.2957763671875f;    // 0x00645468
constexpr float kSecondsPerMs = 0.001f;                   // 0x006461D8
constexpr float kSecondsPerFrame = 0.0166666675f;         // 0x006463B8

Mth::Matrix& CoreMatrix(uint8_t* corePhysics) { return *reinterpret_cast<Mth::Matrix*>(corePhysics + 0x18); }
// The core physics' matrix set from its object's.
void CoreMatrixFromObject(uint8_t* corePhysics)
{
    CoreMatrix(corePhysics) = reinterpret_cast<CBaseComponent*>(corePhysics)->m_object->m_matrix;
}
// m = rotation * m (D3DXMatrixMultiply, linked library code).
void TurnBy(Mth::Matrix& m, const Mth::Matrix& rotation)
{
    D3dx::MatrixMultiply(&m, &rotation, &m);
}

// A turn set up from `degrees` over `duration` seconds.
void StartTurn(CSkaterRotateComponent::Turn& turn, double degrees, float duration)
{
    turn.duration = duration;
    turn.turned = 0.0f;
    turn.active = true;
    turn.rate = static_cast<float>(degrees / duration * kDegreesToRadians);
    turn.remaining = static_cast<float>(std::fabs(degrees * kDegreesToRadians));
}

} // namespace

// 0x00517D40
CBaseComponent* __cdecl CSkaterRotateComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterRotateComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterRotateComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterRotate;
    component->m_corePhysics = nullptr;
    return component;
}

// 0x00517CC0
void CSkaterRotateComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00517CF0
CSkaterRotateComponent* CSkaterRotateComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00517CD0
void CSkaterRotateComponent::InitFromStructure(Script::CStruct* /*params*/)
{
    for (int axis = 2; axis >= 0; --axis)
        m_turns[axis].active = false;
}

// 0x00517D20
void CSkaterRotateComponent::Finalize()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterCorePhysics));
}

// 0x00517DC0
void CSkaterRotateComponent::Update()
{
    for (int axis = 0; axis < 3; ++axis) {
        Turn& turn = m_turns[axis];
        if (!turn.active)
            continue;
        double exact = Tmr::GetSlomoFrameSeconds() * turn.rate;
        float step = static_cast<float>(exact);
        float turned = static_cast<float>(std::fabs(exact) + turn.turned);
        turn.turned = turned;
        if (!(turned < turn.remaining)) {
            // The last step: cut so the turn ends exactly.
            turn.active = false;
            double sign = step < 0.0f ? -1.0 : 1.0;
            step = static_cast<float>(step - (static_cast<double>(turned) - turn.remaining) * sign);
        }
        Mth::Matrix rotation;
        Mth::CreateRotateMatrix(rotation, axis, step);
        TurnBy(m_object->m_matrix, rotation);
        TurnBy(CoreMatrix(m_corePhysics), rotation);
    }
    if (!m_turns[0].active && !m_turns[1].active && !m_turns[2].active)
        VirtualSuspend(true);
}

// 0x00517EA0
int CSkaterRotateComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* /*script*/)
{
    if (name != kRotate)
        return 2;
    float duration;
    if (params->GetFloat(kDuration, &duration, 0)) {
        // Over time: the duration in seconds (seconds / second), frames (frames / frame) or ms.
        if (!params->ContainsFlag(kSeconds) && !params->ContainsFlag(kSecond)) {
            if (params->ContainsFlag(kFrames) || params->ContainsFlag(kFrame))
                duration = static_cast<float>(static_cast<double>(duration) * kSecondsPerFrame);
            else
                duration = static_cast<float>(static_cast<double>(duration) * kSecondsPerMs);
        }
        if (params->GetFloat(kX, &m_turns[0].remaining, 0))
            StartTurn(m_turns[0], m_turns[0].remaining, duration);
        if (params->GetFloat(kY, &m_turns[1].remaining, 0))
            StartTurn(m_turns[1], m_turns[1].remaining, duration);
        if (params->GetFloat(kZ, &m_turns[2].remaining, 0))
            StartTurn(m_turns[2], m_turns[2].remaining, duration);
        // To face a node: about y by the angle to it in the skater's plane.
        uint32_t node = 0;
        if (params->GetChecksum(kNode, &node, 0)) {
            Mth::Vector pos;
            SkateScript::GetPosition(SkateScript::GetNode(node, true), &pos);
            const Mth::Vector& at = m_object->m_pos;
            Mth::Vector toNode = {pos.x - at.x, pos.y - at.y, pos.z - at.z, pos.w - at.w};
            double degrees = static_cast<double>(Mth::GetAngleInPlane(m_object->m_matrix, toNode, 2, 1)) *
                             kRadiansToDegrees;
            StartTurn(m_turns[1], degrees, duration);
        }
        VirtualSuspend(false);
        return 1;
    }
    // At once.
    float degrees;
    if (params->GetFloat(kX, &degrees, 0)) {
        Mth::Matrix rotation;
        Mth::CreateRotateXMatrix(rotation, static_cast<float>(static_cast<double>(degrees) * kDegreesToRadians));
        TurnBy(m_object->m_matrix, rotation);
    } else if (params->GetFloat(kY, &degrees, 0)) {
        m_object->m_matrix.RotateYLocal(static_cast<float>(static_cast<double>(degrees) * kDegreesToRadians));
    } else if (params->GetFloat(kZ, &degrees, 0)) {
        m_object->m_matrix.RotateZLocal(static_cast<float>(static_cast<double>(degrees) * kDegreesToRadians));
    } else {
        // Turned round: the at and right rows reversed (their w kept), and the core physics' flag at +0x34C flipped.
        Mth::Vector& atRow = m_object->m_matrix.row[2];
        atRow.x = -atRow.x;
        atRow.y = -atRow.y;
        atRow.z = -atRow.z;
        Mth::Vector& right = m_object->m_matrix.row[0];
        right.x = -right.x;
        right.y = -right.y;
        right.z = -right.z;
        CoreMatrixFromObject(m_corePhysics);
        m_corePhysics[0x34C] = m_corePhysics[0x34C] ? 0 : 1;
        return 1;
    }
    CoreMatrixFromObject(m_corePhysics);
    return 1;
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CSkaterRotateComponent::Destroy,
         &Obj::CSkaterRotateComponent::Update,
         &Obj::CSkaterRotateComponent::InitFromStructure,
         &Obj::CSkaterRotateComponent::Finalize,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::CSkaterRotateComponent::CallMemberFunction};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj

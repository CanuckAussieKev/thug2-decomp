// The proximity component (Obj::CProximityComponent, proximity_component.h): a leftover only its member function
// table reaches.

#include "game/obj/proximity_component.h"

#include "game/mth/geometry.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

constexpr uint32_t kSkaterId = 0x0AE89B6C;  // (its name unknown): the skater's id, for the scripts
constexpr uint32_t kInactive = 0x742CF11F;  // INACTIVE
constexpr uint32_t kDelay = 0x9D02737D;     // (its name unknown): seconds

// The skater the component watches and the radius added to its outer one (globals no code here sets).
CCompositeObject* Skater() { return GameVariable<CCompositeObject*>(0x0069399C); }
float SkaterRadius() { return GameVariable<float>(0x006939A8); }

int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace

// 0x0042B620
void CProximityComponent::RunScript(uint32_t script, CObject* skater)
{
    Script::CStruct* params = Script::NewStruct();
    if (params)
        params = params->Init();
    params->AddChecksum(kSkaterId, skater->m_id);
    if (mp_params)
        params->AppendStructure(mp_params);
    Script::RunScript(script, params, m_object, false, nullptr);
    if (params) {
        params->Clear(); // (its destructor)
        Script::DeleteStruct(params);
    }
}

// 0x0042B700
void CProximityComponent::Update()
{
    CCompositeObject* skater = Skater();
    if (!skater)
        return;
    int inside = m_inside;
    bool within = false;
    if (m_active) {
        const Mth::Vector& at = m_object->m_pos;
        double dx = static_cast<double>(skater->m_pos.x) - at.x;
        double dy = static_cast<double>(skater->m_pos.y) - at.y;
        double dz = static_cast<double>(skater->m_pos.z) - at.z;
        double reach = static_cast<double>(SkaterRadius()) + m_outerRadius;
        if (!(reach * reach < (dz * dz + dy * dy) + dx * dx)) {
            Mth::Line line;
            const Mth::Vector& head = skater->m_pos;
            line.start = head;
            line.end = {head.x, static_cast<float>(static_cast<double>(head.y) + SkaterRadius()), head.z, head.w};
            Mth::Capsule capsule(line, 0.0f);
            if (!m_inside) {
                capsule.radius = m_innerRadius;
                if (capsule.ContainsPoint(m_object->m_pos, nullptr))
                    inside = 1;
                within = true;
            } else {
                capsule.radius = m_outerRadius;
                within = capsule.ContainsPoint(m_object->m_pos, nullptr);
            }
        }
    }
    if (!within)
        inside = 0;
    if (inside == m_inside)
        return;
    m_inside = inside;
    RunScript(inside ? m_enterScript : m_exitScript, Skater());
}

// 0x0042B850
bool CProximityComponent::Proximity_Init(Script::CStruct* params, Script::CScript*)
{
    m_active = false;
    Update();
    InitFromStructure(params);
    m_active = true;
    VirtualSuspend(false);
    return true;
}

// 0x0042B590
bool CProximityComponent::Proximity_SetActive(Script::CStruct* params, Script::CScript*)
{
    m_active = !params->ContainsFlag(kInactive);
    if (!m_active) {
        float delay;
        if (params->GetFloat(kDelay, &delay, 0)) {
            int now = Tmr::GameClockElapsed();
            m_reactivateTime =
                now + (GameVariable<int>(0x00696D60) - Truncate(static_cast<double>(delay) * -1000.0f));
        } else {
            m_reactivateTime = 0;
        }
    }
    VirtualSuspend(false);
    return true;
}

// 0x0042B570
bool CProximityComponent::Proximity_EnterProximityNotHandled(Script::CStruct*, Script::CScript*)
{
    m_inside = 0;
    return true;
}

} // namespace Obj

using Obj::CProximityComponent;

REPLACE(0x0042B620, &CProximityComponent::RunScript);
REPLACE(0x0042B700, &CProximityComponent::Update);
REPLACE(0x0042B850, &CProximityComponent::Proximity_Init);
REPLACE(0x0042B590, &CProximityComponent::Proximity_SetActive);
REPLACE(0x0042B570, &CProximityComponent::Proximity_EnterProximityNotHandled);
// (the smart pointer's default constructor sits between them: the linker kept one copy for every type)
REPLACE(0x0042B580, &Obj::CSmtPtr<Obj::CRefCounted>::Construct);

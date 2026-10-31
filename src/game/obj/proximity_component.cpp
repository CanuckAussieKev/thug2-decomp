// The proximity component (GameObjects::CProximityComponent, proximity_component.h): a leftover only its member function
// table reaches.

#include <cstdint>

#include "game/obj/proximity_component.h"

#include "game/mth/geometry.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/core/crc.h"
#include "game/script/names.h"
#include "game/sys/startup_init.h"

namespace GameObjects {

CGameEntity* g_proximitySkater;
float g_proximitySkaterRadius;

namespace {

constexpr uint32_t kSkaterId = 0x0AE89B6C;  // (its name unknown): the skater's id, for the scripts
constexpr uint32_t kInactive = 0x742CF11F;  // CLOCK_IDLE
constexpr uint32_t kDelay = 0x9D02737D;     // (its name unknown): seconds

// The skater the component watches and the radius added to its outer one (globals no code here sets).
CGameEntity* Skater() { return g_proximitySkater; }
float SkaterRadius() { return g_proximitySkaterRadius; }

int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace

// 0x0042B620
void CProximityComponent::LaunchScript(uint32_t script, CObject* skater)
{
    Scripts::CStruct* params = Scripts::NewStruct();
    if (params)
        params = params->Init();
    params->PutNameKey(kSkaterId, skater->m_id);
    if (mp_params)
        params->MergeStructure(mp_params);
    Scripts::LaunchScript(script, params, m_object, false, nullptr);
    if (params) {
        params->Clear(); // (its destructor)
        Scripts::DeleteStruct(params);
    }
}

// 0x0042B700
void CProximityComponent::Update()
{
    CGameEntity* skater = Skater();
    if (!skater)
        return;
    int inside = m_isWithin;
    bool within = false;
    if (m_active) {
        const Maths::Vector& at = m_object->m_pos;
        double dx = static_cast<double>(skater->m_pos.x) - at.x;
        double dy = static_cast<double>(skater->m_pos.y) - at.y;
        double dz = static_cast<double>(skater->m_pos.z) - at.z;
        double reach = static_cast<double>(SkaterRadius()) + m_outerRadius;
        if (!(reach * reach < (dz * dz + dy * dy) + dx * dx)) {
            Maths::Line line;
            const Maths::Vector& head = skater->m_pos;
            line.start = head;
            line.end = {head.x, static_cast<float>(static_cast<double>(head.y) + SkaterRadius()), head.z, head.w};
            Maths::Capsule capsule(line, 0.0f);
            if (!m_isWithin) {
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
    if (inside == m_isWithin)
        return;
    m_isWithin = inside;
    LaunchScript(inside ? m_enterScript : m_dismountScript, Skater());
}

// 0x0042B850
bool CProximityComponent::Proximity_Init(Scripts::CStruct* params, Scripts::CScript*)
{
    m_active = false;
    Update();
    ConfigureFromData(params);
    m_active = true;
    VirtualSuspend(false);
    return true;
}

// 0x0042B590
bool CProximityComponent::Proximity_SetActive(Scripts::CStruct* params, Scripts::CScript*)
{
    m_active = !params->HasBareFlag(kInactive);
    if (!m_active) {
        float delay;
        if (params->GetFloat(kDelay, &delay, 0)) {
            int now = FrameClock::GameClockElapsed();
            m_reactivateTime =
                now + (g_clockOffset - Truncate(static_cast<double>(delay) * -1000.0f));
        } else {
            m_reactivateTime = 0;
        }
    }
    VirtualSuspend(false);
    return true;
}

// 0x0042B570
bool CProximityComponent::Proximity_EnterProximityNotHandled(Scripts::CStruct*, Scripts::CScript*)
{
    m_isWithin = 0;
    return true;
}

} // namespace Obj

using GameObjects::CProximityComponent;

namespace GameObjects {

namespace {

// The proximity component's script member functions (three records and the terminator; no table registers them).
uint32_t g_proximityMemberFunctions[24];

} // namespace

// 0x00644910 (a static initialiser): every record's name (the CRC of its text, computed here), the component's type and
// function - all of it code, unlike the other tables, whose names and types are data.
void FillProximityMemberFunctions()
{
    constexpr uint32_t kProximityType = 0xE54DB2EE;
    uint32_t* records = g_proximityMemberFunctions;
    records[0] = Checksums::HashFromText("Proximity_Init");
    records[6] = Checksums::HashFromText("Proximity_SetActive");
    records[12] = Checksums::HashFromText("Proximity_EnterProximityNotHandled");
    records[18] = 0;
    for (int i = 0; i < 4; ++i)
        records[6 * i + 1] = kProximityType;
    void* const code[] = {
        CodeAddress(&GameObjects::CProximityComponent::Proximity_Init),
        CodeAddress(&GameObjects::CProximityComponent::Proximity_SetActive),
        CodeAddress(&GameObjects::CProximityComponent::Proximity_EnterProximityNotHandled),
    };
    Scripts::FillMemberFunctionCode(records, code, 3);
}

} // namespace Obj

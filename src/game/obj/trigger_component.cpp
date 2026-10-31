// The trigger component (game/obj/trigger_component.h).

#include "game/obj/trigger_component.h"

#include <cstdint>
#include <set>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "game/core/code_address.h"
#include "game/sys/log.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/trigger_events.h"
#include "game/obj/camera_lookaround.h"

// The event lists (trigger_events.h).
const int g_wheelLeavesEvents[3]{1, 4, 0};
const int g_wheelLandsEvents[3]{1, 3, 0};
const int g_crossingEvents[3]{9, 0, 0};
const int g_pullUpEvents[3]{1, 3, 0};
const int g_hangEvents[3]{10, 3, 0};
const int g_exitHangEvents[3]{10, 4, 0};
const int g_ladderOffBottomEvents[3]{1, 3, 0};
const int g_ladderOffTopEvents[3]{14, 3, 0};
const int g_ontoLadderEvents[3]{10, 3, 0};
const int g_exitLadderEvents[3]{10, 4, 0};
const int g_projectileTouchEvents[3]{14, 0, 0};
const int g_otherFeelerLeaveEvents[3]{1, 5, 0};
const int g_jumpTriggerEvents[3]{2, 4, 0};
const int g_skaterEvents0064BB98[3]{8, 3, 0};
const int g_kickEvents[3]{1, 4, 0};
const int g_skaterEvents0064BBB0[3]{1, 3, 0};
const int g_bonkEvents[3]{9, 0, 0};
const int g_lipEvents[3]{7, 3, 0};
const int g_offLipEvents[3]{7, 4, 0};
const int g_lipOffEvents[3]{7, 2, 0};
const int g_skaterEvents0064BBEC[3]{6, 3, 0};
const int g_wallplantEvents[3]{6, 4, 0};
const int g_spinEvents[3]{12, 3, 0};
const int g_jumpEvents[3]{12, 2, 0};
const int g_grindOffEvents[3]{13, 4, 0};
const int g_grindEvents[3]{13, 3, 0};
const int g_taggingEvents[3]{11, 3, 0};
const int g_walkEvents0064C630[3]{1, 5, 0};
const int g_walkEvents0064C63C[3]{2, 4, 0};
const int g_walkEvents0064C648[3]{8, 3, 0};
const int g_walkEvents0064C654[3]{14, 3, 0};
const int g_walkEvents0064C660[3]{14, 4, 0};

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr uint32_t kTriggerScript = 0x2CA8A299; // TriggerScript
constexpr uint32_t kNetEnabled = 0x20209C31;    // NetEnabled
constexpr uint32_t kPermanent = 0x23627FD7;     // Permanent
constexpr uint32_t kParams = 0x5180A4DB;        // (a parameters structure on the other object's node)
constexpr uint32_t kTriggerType = 0x3A717382;   // TriggerType
constexpr uint32_t kNodeArray = 0xC472ECC5;     // nodearray (the component holding an object's own nodes)

// Faces the update's feeler looks for (the trigger flag 0x40 and 0x10).
constexpr uint16_t kTriggerFaces = 0x50;

// No events (0x0064663C: three zeros).
const int kNoEvents[3] = {0, 0, 0};

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The node array of `object`'s nodearray component (its vtable slot 13), or null without one.
Scripts::CArray* NodeArrayOf(CGameEntity* object)
{
    CObjectModule* nodes = object->FetchModule(kNodeArray);
    if (!nodes)
        return nullptr;
    return reinterpret_cast<Scripts::CArray*(__thiscall*)(CObjectModule*)>(nodes->m_vtable[13])(nodes);
}

} // namespace

// 0x00434B90
CObjectModule* __cdecl CActivationZoneModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CActivationZoneModule));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CActivationZoneModule*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    return component;
}

// 0x00434900
void CActivationZoneModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00434910
CActivationZoneModule* CActivationZoneModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00434940
void CActivationZoneModule::ConfigureFromData(Scripts::CStruct* /*params*/) { m_lastPos = m_entity->m_atPoint; }

// 0x00434D90
void CActivationZoneModule::TickFrame()
{
    CGameEntity* object = m_entity;
    const Maths::Vec4& pos = object->m_atPoint;
    // (not a number counts as moved, as the original's FUCOMPP)
    if (m_lastPos.x == pos.x && m_lastPos.y == pos.y && m_lastPos.z == pos.z && m_lastPos.w == pos.w)
        return;
    if (object->m_compositeFlags & COMPOSITE_FLAG_TELEPORTED) {
        m_lastPos = pos;
        return;
    }
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    feeler.m_line.start = m_lastPos;
    feeler.m_line.end = m_entity->m_atPoint;
    feeler.SetExcluded(0, kTriggerFaces);
    feeler.m_callback = reinterpret_cast<Engine3D::FeelerCallback>(&FeelerHit);
    feeler.m_unknownA8 = reinterpret_cast<uint32_t>(this);
    feeler.GetHitShape(true, false);
    m_lastPos = m_entity->m_atPoint;
}

// 0x00434970
int CActivationZoneModule::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (name != kTriggerType)
        return 2;
    // (the array is read into the name's own slot, cleared first)
    Scripts::CArray* array = nullptr;
    params->FetchList(0u, &array, 0);
    // One integer array, or an array of them - any one of which may match.
    Scripts::CArray* arrays = nullptr;
    uint32_t count = 1;
    if (array->m_kindTag == Scripts::QBVAL_ARRAY) {
        count = array->m_extent;
        arrays = array;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (arrays)
            array = reinterpret_cast<Scripts::CArray*>(arrays->FetchPart(i));
        if (array->m_kindTag != Scripts::QBVAL_INTEGER)
            return 0;
        uint32_t wanted = 0;
        for (int k = 0; k < static_cast<int>(array->m_extent); ++k) {
            if (static_cast<int>(array->FetchPart(static_cast<uint32_t>(k))) >= 0)
                wanted += 1u << (array->FetchPart(static_cast<uint32_t>(k)) & 31);
        }
        if ((m_events | wanted) == m_events)
            return 1;
    }
    return 0;
}

// 0x00434A60
bool CActivationZoneModule::TriggerNode(const int* events, uint32_t name, Scripts::CArray* nodes, const uint8_t* other)
{
    Scripts::CStruct* node = nullptr;
    if (!nodes) {
        node = LevelNodes::FetchVertex(name, false);
    } else {
        // (from the last; none named so leaves the first one checked - element 0 -, as the original)
        for (uint32_t i = nodes->m_extent; i > 0;) {
            node = reinterpret_cast<Scripts::CStruct*>(nodes->FetchPart(--i));
            if (LevelNodes::GetNodeName(node) == name)
                break;
        }
    }
    Scripts::CStruct* params = nullptr;
    if (other) {
        if (Scripts::CStruct* otherNode = LevelNodes::FetchVertex(*reinterpret_cast<const uint32_t*>(other + 0x14), false))
            otherNode->ReadSubStruct(kParams, &params, 0);
    }
    // Not original - a trigger face whose object has no node (a level's collision with another level's node array:
    // the front end's SKATE NOW in THPS4's shop had its halfpipe's gap triggers - the user, 2026-10-19) is no
    // trigger, logged once per name.
    // Original behaviour: the node's TriggerScript read at once, through the null when there's no node (a crash
    // reading address 4):
    //     uint32_t script = 0;
    //     node->ReadHashValue(kTriggerScript, &script, 0);
    if (!node) {
        static std::set<uint32_t> s_logged;
        if (s_logged.insert(name).second)
            Log("trigger: no node named %08X for a trigger face (its collision object) - nothing run", name);
        return false;
    }
    uint32_t script = 0;
    node->ReadHashValue(kTriggerScript, &script, 0);
    if (!script)
        return false;
    if (GameModules::Skate::ShouldBeAbsent(node))
        return false;
    uint32_t& recorded = m_events;
    recorded = 0;
    if (events[0])
        recorded = 1u << (events[0] & 31);
    if (events[1])
        recorded += 1u << (events[1] & 31);
    if (events[2])
        recorded += 1u << (events[2] & 31);
    CEntity* object = At<CEntity*>(this, 0x0C);
    bool permanent = node->HasBareFlag(kPermanent);
    bool replicateFlag = node->HasBareFlag(kNetEnabled);
    object->StartScriptTask(script, static_cast<int>(name), replicateFlag, permanent, params);
    return true;
}

// 0x00434BF0
bool CActivationZoneModule::CheckFeeler(const int* events, Engine3D::CProbeRay* feeler)
{
    if (!feeler->m_hit.faceFlag40 || !feeler->m_hit.objectChecksum)
        return false;
    if (feeler->GetTestedOwner()) {
        // A movable object's face, while it's being tested: through its own nodes.
        auto* owner = reinterpret_cast<CGameEntity*>(feeler->GetTestedOwner());
        Scripts::CArray* nodes = NodeArrayOf(owner);
        uint32_t name = feeler->m_hit.objectChecksum;
        return TriggerNode(events, name, nodes, reinterpret_cast<const uint8_t*>(feeler->GetTestedOwner()));
    }
    if (!feeler->HitObjectAlive()) // (which clears the name when the object is gone)
        return TriggerNode(events, feeler->m_hit.objectChecksum, nullptr, nullptr);
    if (!feeler->GetHitObject())
        return false;
    Scripts::CArray* nodes = NodeArrayOf(feeler->GetHitObject());
    uint32_t name = feeler->m_hit.objectChecksum;
    return TriggerNode(events, name, nodes, reinterpret_cast<const uint8_t*>(feeler->GetHitObject()));
}

// 0x00434CE0
void CActivationZoneModule::TriggerHits(const int* events, const uint8_t* hits)
{
    const int count = *reinterpret_cast<const int*>(hits + 0xC30);
    auto entry = [hits](int i) { return hits + 0x88 + 0x60 * i; };
    for (int i = count - 1; i >= 0; --i) {
        if (!entry(i)[4])
            continue;
        uint32_t name = *reinterpret_cast<const uint32_t*>(entry(i));
        // (the count is read again: TriggerNode may run scripts)
        int total = *reinterpret_cast<const int*>(hits + 0xC30);
        int later = i + 1;
        while (later < total && name != *reinterpret_cast<const uint32_t*>(entry(later)))
            ++later;
        if (later == total)
            TriggerNode(events, name, nullptr, nullptr);
    }
}

// 0x00434D70
bool __cdecl CActivationZoneModule::FeelerHit(Engine3D::CProbeRay* feeler)
{
    auto* component = reinterpret_cast<CActivationZoneModule*>(feeler->m_unknownA8);
    return component->CheckFeeler(kNoEvents, feeler);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CActivationZoneModule::Teardown,
         &GameObjects::CActivationZoneModule::TickFrame,
         &GameObjects::CActivationZoneModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CActivationZoneModule::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj

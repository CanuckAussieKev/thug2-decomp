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
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

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
Script::CArray* NodeArrayOf(CCompositeObject* object)
{
    CBaseComponent* nodes = object->GetComponent(kNodeArray);
    if (!nodes)
        return nullptr;
    return reinterpret_cast<Script::CArray*(__thiscall*)(CBaseComponent*)>(nodes->m_vtable[13])(nodes);
}

} // namespace

// 0x00434B90
CBaseComponent* __cdecl CTriggerComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CTriggerComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CTriggerComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    return component;
}

// 0x00434900
void CTriggerComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00434910
CTriggerComponent* CTriggerComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00434940
void CTriggerComponent::InitFromStructure(Script::CStruct* /*params*/) { m_lastPos = m_object->m_pos; }

// 0x00434D90
void CTriggerComponent::Update()
{
    CCompositeObject* object = m_object;
    const Mth::Vector& pos = object->m_pos;
    // (not a number counts as moved, as the original's FUCOMPP)
    if (m_lastPos.x == pos.x && m_lastPos.y == pos.y && m_lastPos.z == pos.z && m_lastPos.w == pos.w)
        return;
    if (object->m_compositeFlags & COMPOSITE_FLAG_TELEPORTED) {
        m_lastPos = pos;
        return;
    }
    Nx::CFeeler feeler;
    feeler.Construct();
    feeler.m_line.start = m_lastPos;
    feeler.m_line.end = m_object->m_pos;
    feeler.SetIgnore(0, kTriggerFaces);
    feeler.m_callback = reinterpret_cast<Nx::FeelerCallback>(&FeelerHit);
    feeler.m_unknownA8 = reinterpret_cast<uint32_t>(this);
    feeler.GetCollision(true, false);
    m_lastPos = m_object->m_pos;
}

// 0x00434970
int CTriggerComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* /*script*/)
{
    if (name != kTriggerType)
        return 2;
    // (the array is read into the name's own slot, cleared first)
    Script::CArray* array = nullptr;
    params->GetArray(0u, &array, 0);
    // One integer array, or an array of them - any one of which may match.
    Script::CArray* arrays = nullptr;
    uint32_t count = 1;
    if (array->m_type == Script::ESYMBOLTYPE_ARRAY) {
        count = array->m_size;
        arrays = array;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (arrays)
            array = reinterpret_cast<Script::CArray*>(arrays->GetElement(i));
        if (array->m_type != Script::ESYMBOLTYPE_INTEGER)
            return 0;
        uint32_t wanted = 0;
        for (int k = 0; k < static_cast<int>(array->m_size); ++k) {
            if (static_cast<int>(array->GetElement(static_cast<uint32_t>(k))) >= 0)
                wanted += 1u << (array->GetElement(static_cast<uint32_t>(k)) & 31);
        }
        if ((m_events | wanted) == m_events)
            return 1;
    }
    return 0;
}

// 0x00434A60
bool CTriggerComponent::TriggerNode(const int* events, uint32_t name, Script::CArray* nodes, const uint8_t* other)
{
    Script::CStruct* node = nullptr;
    if (!nodes) {
        node = SkateScript::GetNode(name, false);
    } else {
        // (from the last; none named so leaves the first one checked - element 0 -, as the original)
        for (uint32_t i = nodes->m_size; i > 0;) {
            node = reinterpret_cast<Script::CStruct*>(nodes->GetElement(--i));
            if (SkateScript::GetNodeName(node) == name)
                break;
        }
    }
    Script::CStruct* params = nullptr;
    if (other) {
        if (Script::CStruct* otherNode = SkateScript::GetNode(*reinterpret_cast<const uint32_t*>(other + 0x14), false))
            otherNode->GetStructure(kParams, &params, 0);
    }
    // Not original - a trigger face whose object has no node (a level's collision with another level's node array:
    // the front end's SKATE NOW in THPS4's shop had its halfpipe's gap triggers - the user, 2026-10-19) is no
    // trigger, logged once per name.
    // Original behaviour: the node's TriggerScript read at once, through the null when there's no node (a crash
    // reading address 4):
    //     uint32_t script = 0;
    //     node->GetChecksum(kTriggerScript, &script, 0);
    if (!node) {
        static std::set<uint32_t> s_logged;
        if (s_logged.insert(name).second)
            Log("trigger: no node named %08X for a trigger face (its collision object) - nothing run", name);
        return false;
    }
    uint32_t script = 0;
    node->GetChecksum(kTriggerScript, &script, 0);
    if (!script)
        return false;
    if (Mdl::Skate::ShouldBeAbsent(node))
        return false;
    uint32_t& recorded = m_events;
    recorded = 0;
    if (events[0])
        recorded = 1u << (events[0] & 31);
    if (events[1])
        recorded += 1u << (events[1] & 31);
    if (events[2])
        recorded += 1u << (events[2] & 31);
    CObject* object = At<CObject*>(this, 0x0C);
    bool permanent = node->ContainsFlag(kPermanent);
    bool netEnabled = node->ContainsFlag(kNetEnabled);
    object->SpawnAndRunScript(script, static_cast<int>(name), netEnabled, permanent, params);
    return true;
}

// 0x00434BF0
bool CTriggerComponent::CheckFeeler(const int* events, Nx::CFeeler* feeler)
{
    if (!feeler->m_hit.faceFlag40 || !feeler->m_hit.objectChecksum)
        return false;
    if (feeler->GetTestedOwner()) {
        // A movable object's face, while it's being tested: through its own nodes.
        auto* owner = reinterpret_cast<CCompositeObject*>(feeler->GetTestedOwner());
        Script::CArray* nodes = NodeArrayOf(owner);
        uint32_t name = feeler->m_hit.objectChecksum;
        return TriggerNode(events, name, nodes, reinterpret_cast<const uint8_t*>(feeler->GetTestedOwner()));
    }
    if (!feeler->HitObjectAlive()) // (which clears the name when the object is gone)
        return TriggerNode(events, feeler->m_hit.objectChecksum, nullptr, nullptr);
    if (!feeler->GetHitObject())
        return false;
    Script::CArray* nodes = NodeArrayOf(feeler->GetHitObject());
    uint32_t name = feeler->m_hit.objectChecksum;
    return TriggerNode(events, name, nodes, reinterpret_cast<const uint8_t*>(feeler->GetHitObject()));
}

// 0x00434CE0
void CTriggerComponent::TriggerHits(const int* events, const uint8_t* hits)
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
bool __cdecl CTriggerComponent::FeelerHit(Nx::CFeeler* feeler)
{
    auto* component = reinterpret_cast<CTriggerComponent*>(feeler->m_unknownA8);
    return component->CheckFeeler(kNoEvents, feeler);
}

} // namespace Obj

REPLACE(0x00434B90, Obj::CTriggerComponent::Create);
REPLACE(0x00434900, &Obj::CTriggerComponent::DestroyBody);
REPLACE(0x00434910, &Obj::CTriggerComponent::Destroy);
REPLACE(0x00434940, &Obj::CTriggerComponent::InitFromStructure);
REPLACE(0x00434D90, &Obj::CTriggerComponent::Update);
REPLACE(0x00434970, &Obj::CTriggerComponent::CallMemberFunction);
REPLACE(0x00434A60, &Obj::CTriggerComponent::TriggerNode);
REPLACE(0x00434BF0, &Obj::CTriggerComponent::CheckFeeler);
REPLACE(0x00434CE0, &Obj::CTriggerComponent::TriggerHits);
REPLACE(0x00434D70, Obj::CTriggerComponent::FeelerHit);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x00646648,
    &Obj::CTriggerComponent::Destroy,
    &Obj::CTriggerComponent::Update,
    &Obj::CTriggerComponent::InitFromStructure,
    Stubs::DoNothing,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
    &Obj::CTriggerComponent::CallMemberFunction);

} // namespace
} // namespace Obj

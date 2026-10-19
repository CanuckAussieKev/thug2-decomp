// The trigger component's node lookup (game/obj/trigger_component.h).

#include "game/obj/trigger_component.h"

#include <cstdint>
#include <set>

#include "game/obj/object.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace Obj {

namespace {

constexpr uint32_t kTriggerScript = 0x2CA8A299; // TriggerScript
constexpr uint32_t kNetEnabled = 0x20209C31;    // NetEnabled
constexpr uint32_t kPermanent = 0x23627FD7;     // Permanent
constexpr uint32_t kParams = 0x5180A4DB;        // (a parameters structure on the other object's node)

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

} // namespace

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
    if (GameFunction<bool(__thiscall*)(void*, Script::CStruct*)>(0x00565BB0)(GameVariable<void*>(0x007CE478), node))
        return false;
    uint32_t& recorded = At<uint32_t>(this, 0x28);
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

} // namespace Obj

REPLACE(0x00434A60, &Obj::CTriggerComponent::TriggerNode);

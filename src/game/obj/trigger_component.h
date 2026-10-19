#pragma once

#include <cstdint>

namespace Script {
class CArray;
} // namespace Script

// The trigger component (a composite object's: what runs a node's TriggerScript when the object's collision feelers
// touch a trigger face - the skater landing on a gap's geometry, skating into a kill zone). Not rewritten yet: only
// the node lookup below; the fields used: +0x0C the object (CBaseComponent::m_object), +0x28 the events that set it
// off.
namespace Obj {

class CTriggerComponent {
public:
    // 0x00434A60: the trigger of the node named `name` (a face's collision object): the node is the one named so -
    // looked up in the node manager (GetNode) when `nodes` is null, else searched for in `nodes` from its end - its
    // TriggerScript run on the object (SpawnAndRunScript, with its NetEnabled / Permanent flags, the node's name and
    // the parameters structure 0x5180A4DB of the node named by `other` +0x14 when there is one), the events recorded
    // (+0x28: a bit for each of the three in `events` that isn't 0). False when there's no node (ours - see the .cpp),
    // no TriggerScript, or the skate module skips the node (0x00565BB0).
    bool TriggerNode(const int* events, uint32_t name, Script::CArray* nodes, const uint8_t* other);
};

} // namespace Obj
